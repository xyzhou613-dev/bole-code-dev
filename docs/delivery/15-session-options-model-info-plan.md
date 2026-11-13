# SDK 会话参数动态更新与模型能力读取施工方案

版本：r9 规划  
基线：r8（ABI 1.1，`session/create` 已支持 reasoningEffort/maxTokens，GUI 已有静态 Think/Max out，但改参数会新建会话）

## 1. 目标

实现两个能力：

1. **已有会话内修改推理强度和输出上限，不新建会话。**
2. **GUI 的 Think 档位从 runtime 最终解析的模型能力读取**，不在 GUI 里硬编码 `off/low/medium/high/xhigh`。

最终链路：

```text
GUI 启动
  → bole_model_info
  → JSON-RPC model/info
  → sdk-server ctx.llm.resolveModelInfo
  → 返回 reasoning.efforts / defaultMaxTokens
  → 填充 Think / Max out

GUI Submit
  → 已有会话：bole_session_set_reasoning_effort / set_max_tokens
  → JSON-RPC session/setOptions
  → sdk-server 更新会话可变配置
  → agent/request 监听器覆盖下一次模型请求
  → 不重建 session
```

## 2. 非目标

- 不在 GUI 里解析 `runtime/cordis.patch.yml`。
- 不支持运行中任务立即切参数；只在任务空闲时更新，下一 turn 生效。
- 不改变 provider/model；只改 reasoningEffort 和 maxTokens。
- 不保证 resume 会话恢复动态参数；resume 行为单独处理或明确限制。
- 不解析正文 `<tool_call>`。

## 3. 关键设计决定

### 3.1 档位来源

`sdk-server` 调用：

```ts
const info = await this.ctx.llm.resolveModelInfo(provider, model)
```

返回 `LlmResolvedModelInfo`：

```ts
info.reasoning?.efforts
info.reasoning?.defaultEffort
info.defaultMaxTokens
info.context?.contextWindow
```

这些值已经由 runtime 按 profile + patch + installed catalog 解析，GUI 不直接读 YAML。

### 3.2 动态修改不重建 Agent

`AgentOptions` 是创建时配置，不能直接改。使用现有 `agent/request` waterfall：

```ts
agentCtx.on('agent/request', async (_payload, next) => {
  const resolved = await next()
  // 按会话可变配置覆盖 reasoningEffort / maxTokens
  return { ...resolved, ... }
})
```

`agent/request` 每轮/每 step 模型请求前都会执行，所以下一 turn 即可生效。

### 3.3 修改时机

`session/setOptions` 只允许在会话空闲时调用：

```text
TASK_STARTED 之前 / TASK_FINISHED 之后
```

运行中返回新的业务错误：

```text
SESSION_BUSY
```

GUI 在 Submit 前设置；任务运行中 GUI 本来也会禁用 Submit。

## 4. 协议新增

### 4.1 `model/info`

请求：

```json
{
  "jsonrpc": "2.0",
  "id": 10,
  "method": "model/info",
  "params": {}
}
```

可选指定：

```json
{"provider":"bole","model":"qwen3.8-flash-next-w4"}
```

响应：

```json
{
  "provider": "bole",
  "model": "qwen3.8-flash-next-w4",
  "contextWindow": 262144,
  "defaultMaxTokens": 8192,
  "reasoning": {
    "defaultEffort": "high",
    "efforts": [
      {"id": "off", "name": "Off"},
      {"id": "low", "name": "Low"},
      {"id": "medium", "name": "Medium"},
      {"id": "high", "name": "High"},
      {"id": "xhigh", "name": "XHigh"}
    ]
  }
}
```

无 reasoning 时：

```json
{"provider":"...","model":"...","reasoning":null}
```

实现：

```ts
async modelInfo(params: Record<string, unknown>) {
  this.assertInitialized()
  const provider = optionalString(params.provider, 'provider') ?? this.provider
  const model = optionalString(params.model, 'model') ?? this.model
  const llm = this.ctx.get('llm')
  if (llm === undefined) throw business('MODEL_NOT_FOUND', 'no LLM runtime is mounted')
  const info = await llm.resolveModelInfo(provider, model)
  return {
    provider: info.provider,
    model: info.id,
    ...(info.context === undefined ? {} : { contextWindow: info.context.contextWindow }),
    ...(info.defaultMaxTokens === undefined ? {} : { defaultMaxTokens: info.defaultMaxTokens }),
    reasoning: info.reasoning ?? null,
  }
}
```

`model/info` 不触网，不启动模型请求；只解析本地 catalog/profile。

### 4.2 `session/setOptions`

请求：

```json
{
  "jsonrpc": "2.0",
  "id": 11,
  "method": "session/setOptions",
  "params": {
    "sessionId": "gui-1234",
    "reasoningEffort": "high",
    "maxTokens": 8192
  }
}
```

字段语义：

```text
reasoningEffort 缺省   → 不修改
reasoningEffort null    → 清除覆盖，恢复默认
reasoningEffort 字符串  → 设置为该值
maxTokens 缺省          → 不修改
maxTokens 0 或 null     → 清除覆盖，恢复默认
maxTokens 正整数        → 设置该上限
```

返回：

```json
{"sessionId":"gui-1234","reasoningEffort":"high","maxTokens":8192}
```

错误：

```text
SESSION_NOT_FOUND
SESSION_BUSY
INVALID_ARGUMENT
UNSUPPORTED_REASONING_EFFORT
```

## 5. C SDK 变更

### 5.1 ABI

`BOLE_ABI_VERSION` 从 `0x00010001` 升到 `0x00010002`（ABI 1.2）。

新增结构追加，不改变已有字段顺序。

### 5.2 模型信息结构

```c
#define BOLE_PROVIDER_MAX 32
#define BOLE_MODEL_ID_MAX 128
#define BOLE_EFFORT_ID_MAX 32
#define BOLE_EFFORT_NAME_MAX 64
#define BOLE_EFFORT_COUNT_MAX 8

typedef struct bole_reasoning_effort_info {
  char id[BOLE_EFFORT_ID_MAX];
  char name[BOLE_EFFORT_NAME_MAX];
} bole_reasoning_effort_info;

typedef struct bole_model_info {
  uint32_t size;
  char provider[BOLE_PROVIDER_MAX];
  char model[BOLE_MODEL_ID_MAX];
  int32_t context_window;
  int32_t default_max_tokens;
  int32_t has_reasoning;
  int32_t effort_count;
  bole_reasoning_effort_info efforts[BOLE_EFFORT_COUNT_MAX];
  char default_effort[BOLE_EFFORT_ID_MAX];
} bole_model_info;

BOLE_API int32_t BOLE_CALL bole_model_info(
    bole_handle* handle,
    bole_model_info* out_info);
```

行为：

- `out_info->size` 由调用方先设为 `sizeof(bole_model_info)`。
- 调用前必须 `wait_ready`。
- `effort_count` 超过 8 时截断并返回成功，`effort_count` 记录实际数量（GUI 只展示前 8）。
- 无 reasoning 时 `has_reasoning=0`，`effort_count=0`。

### 5.3 会话动态设置

两个 setter，避免 flags 结构复杂：

```c
BOLE_API int32_t BOLE_CALL bole_session_set_reasoning_effort(
    bole_session* session,
    const char* effort_id);

BOLE_API int32_t BOLE_CALL bole_session_set_max_tokens(
    bole_session* session,
    int32_t max_tokens);
```

语义：

```text
effort_id == NULL   → 清除覆盖
max_tokens <= 0     → 清除覆盖
```

每个 setter 同步等待 `session/setOptions` 应答，正常毫秒级。

## 6. sdk-server 实现

### 6.1 SessionRecord

```ts
interface MutableSessionOptions {
  reasoningEffort?: ReasoningEffortId
  reasoningSet: boolean
  maxTokens?: number
  maxSet: boolean
}

interface SessionRecord {
  handle: AgentHandle
  preset: string | undefined
  cwd: string
  options: MutableSessionOptions
}
```

### 6.2 创建时安装监听器

在 `createRecord` 中，把原有 preset `composition.setup` 和本会话监听器组合：

```ts
const optionsRef: MutableSessionOptions = {
  reasoningSet: options.reasoningEffort !== undefined,
  ...(options.reasoningEffort === undefined ? {} : { reasoningEffort: options.reasoningEffort }),
  maxSet: options.maxTokens !== undefined,
  ...(options.maxTokens === undefined ? {} : { maxTokens: options.maxTokens }),
}

const originalSetup = composition.setup
const setup = async (agentCtx, agent) => {
  const commit = originalSetup === undefined ? undefined : await originalSetup(agentCtx, agent)
  agentCtx.on('agent/request', async (_payload, next) => {
    const resolved = await next()
    const nextConfig = { ...resolved }
    if (optionsRef.reasoningSet) {
      delete nextConfig.reasoningEffort
      if (optionsRef.reasoningEffort !== undefined) {
        nextConfig.reasoningEffort = optionsRef.reasoningEffort
      }
    }
    if (optionsRef.maxSet) {
      delete nextConfig.maxTokens
      if (optionsRef.maxTokens !== undefined) nextConfig.maxTokens = optionsRef.maxTokens
    }
    return nextConfig
  })
  return commit
}
```

注意：

- `agentCtx` 的 scope 销毁时会自动移除监听器。
- 不能直接改 `AgentOptions`。
- `composition.setup` 返回的 `commit` 必须原样返回。
- `agent/request` 里先 `await next()`，再覆盖，保证其他 listener 先执行。

### 6.3 setOptions

```ts
async setOptions(params: Record<string, unknown>) {
  this.assertInitialized()
  const sessionId = requireString(params.sessionId, 'sessionId')
  const record = this.sessions.get(sessionId)
  if (record === undefined) throw business('SESSION_NOT_FOUND', ...)
  this.assertLive(sessionId, record)
  if (this.sessionIsActive(record.agent)) throw business('SESSION_BUSY', ...)

  if (Object.hasOwn(params, 'reasoningEffort')) {
    record.options.reasoningSet = true
    if (params.reasoningEffort === null) {
      delete record.options.reasoningEffort
    } else {
      record.options.reasoningEffort = ReasoningEffortId(requireString(...))
    }
  }
  if (Object.hasOwn(params, 'maxTokens')) {
    record.options.maxSet = true
    if (params.maxTokens === null || params.maxTokens === 0) {
      delete record.options.maxTokens
    } else {
      // 正整数校验
      record.options.maxTokens = params.maxTokens
    }
  }
  return snapshot(record.options)
}
```

`sessionIsActive` 复用现有 `cancel()` 中的 active 判断。

### 6.4 协议分发

```ts
case 'model/info': return this.modelInfo(params ?? {})
case 'session/setOptions': return this.setOptions(params ?? {})
```

## 7. GUI 改造

### 7.1 启动后读能力

`worker()` 在 `bole_wait_ready` 后调用：

```c
bole_model_info info;
memset(&info, 0, sizeof(info));
info.size = sizeof(info);
if (bole_model_info(g_handle, &info) == 0) {
  PostMessageA(hwnd, WM_BOLE_MODEL_INFO, 0, (LPARAM)&info);
}
```

WM 消息处理：

- 清空 Think combo；
- 加 `default`；
- 按 `info.efforts[i].id/name` 添加；
- 有 `default_effort` 时选中它，否则选 `default`；
- Max out 初始化为 `info.default_max_tokens > 0 ? ... : 8192`。

UI 线程不能直接读 worker 栈上的 `info`；用堆拷贝或复制到全局。

### 7.2 Submit 时

```text
session == NULL:
  按现有 bole_session_create(options) 创建

session != NULL:
  bole_session_set_reasoning_effort(session, effort_or_NULL)
  bole_session_set_max_tokens(session, max_or_0)
  bole_submit(...)
```

不再因参数变化设置 `g_session = NULL`。

如果 setter 返回 `SESSION_BUSY`：

```text
提示用户等当前任务结束
```

如果 setter 返回 `SESSION_NOT_FOUND`：

```text
g_session = NULL
下次重新 create
```

### 7.3 UI 档位

不要硬编码列表。兜底列表只在 `bole_model_info` 失败时使用：

```text
default / off / low / medium / high / xhigh
```

`default` 始终作为第一个选项。

## 8. 测试计划

### 8.1 C SDK

编译探针：

```c
bole_model_info info;
bole_model_info(h, &info);
printf("%s %s efforts=%d default=%s\n", ...);
```

验证：

- FNX 配置返回 `off/low/medium/high/xhigh`（按 YAML keys）。
- `default_effort=high`，`default_max_tokens=8192`。

### 8.2 动态 setOptions

1. 创建 session；
2. `set_reasoning_effort("high")`；
3. `set_max_tokens(512)`；
4. submit；
5. 检查 `MODEL_SELECTED`：
   - `reasoningEffort=high`
   - `maxTokens=512`
6. 再次 `set_reasoning_effort("off")` + `set_max_tokens(8192)`；
7. 同一 session 再 submit；
8. 检查 `MODEL_SELECTED` 变化，且 `sessionId` 不变。

### 8.3 GUI

- 启动后 Think 选项来自 `bole_model_info`。
- 选 high，Max out 512，连续两次 Submit：
  - 两次 sessionId 相同；
  - 第一次/第二次都带 high/512（除非改参数）。
- 改 off/8192 后 Submit：
  - 不新开会话；
  - 下一轮 `MODEL_SELECTED` 变为 off/8192。
- `default` 选项对应 NULL，使用 `agent-default-model.reasoningEffort`。

### 8.4 回归

- `session/create` 直接传 options 仍有效。
- 旧 ABI 调用方使用旧 `size` 的 `bole_session_options` 不崩溃。
- 不影响 RSI/preset/工具调用。
- 125B FNX:
  - `supportsDeveloperRole:false`
  - `high` 映射 `xhigh`
  - 无 400。

## 9. 构建与交付

```powershell
npm --prefix runtime/dev run build
node runtime/dev/pack.mjs

# native
cmake --build <native-build>

# gui/console
cmake --build <gui-build>
cmake --build <console-build>
```

更新 `runtime/version.inputs.json`：

```json
"abi": "1.2.0"
```

打包：

```powershell
build-offline.ps1 -RuntimeDir ... -NativeBuild ... -OutDir D:\bole-r9-final ...
```

同时生成补丁包：

```text
bin/bole_agent.dll/.lib/.h
samples/gui-win32/bole_gui.exe
samples/console-c/bole_console.exe
samples/*/bole_agent.dll
home/profiles/sdk/node_modules/@bole/sdk-server/
```

## 10. 风险与边界

1. **监听器顺序**：`agent/request` 可能有其他 listener；本方案在 `await next()` 后覆盖，后设置的监听器可能再次覆盖，需要测试。
2. **运行中切换**：必须拒绝，避免一个 turn 内参数不一致。
3. **`maxTokens` 语义**：仍是每轮模型请求的 output 上限，不是 session 总预算。
4. **resume**：本次只保证新建会话的动态设置；resume 使用默认 agentOptions，后续可扩展。
5. **模型能力**：`model/info` 依赖 `ctx.llm.resolveModelInfo`；如果没有 llm 服务，返回明确错误。
6. **GUI 选项数量**：结构最多 8 个；超过截断并记录实际 count。
7. **配置生效**：修改 YAML 后必须重启 runtime，`model/info` 才能读到新值。

## 11. 交付验收清单

```text
[ ] model/info 返回真实 efforts/defaultMaxTokens
[ ] GUI Think 下拉框来自 model/info
[ ] GUI Max out 默认来自 defaultMaxTokens
[ ] setOptions 不新建 session
[ ] 同一 session 连续两轮参数可不同
[ ] SESSION_BUSY 正确拒绝运行中修改
[ ] ABI 1.2 header/lib/dll/exe 同步
[ ] 125B FNX 无 developer role 400
[ ] 安装/verify/补充补丁包通过
[ ] 文档更新 01-interface、04-c-abi、02-protocol
```
