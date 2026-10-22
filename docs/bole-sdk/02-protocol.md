# 02 协议方法表（1.0 冻结）

状态：2026-10-22 冻结 1.0。方法表、通知表、错误码、上限、版本与顺序已定；与 `03-events.md`、`04-c-abi.md` 同批。负责人：A 轨（协议与运行期）。冻结后只增不改：新增方法/通知/字段可以，已发布的名字与含义不改。

协议是 C 库与自建插件 `@bole/sdk-server` 之间的接缝。C 库只按本文与 `03-events.md` 实现，不需要读运行时源码。

## 一、传输与分帧

已定：

- stdio、逐行 JSON-RPC 2.0、每行一个完整 JSON 对象、UTF-8 无 BOM、LF 换行。
- stdout 归协议独占：补丁层把官方 `sdk-jsonrpc-server` 置为 `disabled`，由 `@bole/sdk-server` 创建并持有 transport；运行时内其他插件不得写 stdout，日志一律走 stderr。
- transport 的请求槽位只有一个，自建插件接管后自行分发；官方 `HarnessSdkJsonRpcServer.handleRequest` 只接收未命中的方法。
- 自建插件保留官方条目的 `inject: [sdkAppStartup, loader]`，并在 `initialize` 前 `await ctx.get('loader')?.await()`；`initialize` 就是就绪边界，握手期间不广告就绪。
- 单行上限默认 64 MiB（67,108,864 字节），可由 C 配置 `max_line_bytes` 覆盖；上限同时约束两个方向。
- 超限：接收方回 `-32600`、`data.code = "LINE_TOO_LONG"`，随后关闭传输（运行时退出）；C 库返回 `2005`。
- stdout 首帧校验：C 库读到的第一行必须能解析为带 `"jsonrpc":"2.0"` 的 JSON 对象，否则返回 `2006` 并杀掉运行时。
- stderr 不作为协议解析，按行追加到 `log_path`（若配置），默认丢弃。
- 不支持批量请求；收到数组回 `-32600`。
- 未知字段一律忽略；新增字段只增不改。
- 请求 id 由 C 库生成，整数、从 1 单调递增；插件原样回显。通知不带 id。
- 响应统一 `{"jsonrpc":"2.0","id":N,"result":{...}}` 或 `{"jsonrpc":"2.0","id":N,"error":{"code":-326xx,"message":"...","data":{"code":"STABLE_NAME",...}}}`。
- `error.data.code` 是稳定字符串（见第四节），C 库映射为数值码；`error.data.details` 可选，仅用于日志。

## 二、方法表

请求名统一用斜杠形式；所有控制面方法都在本地返回，不等模型或工具。

| 方法 | 方向 | 入参 | 出参 | 落点 | 备注 |
| --- | --- | --- | --- | --- | --- |
| `initialize` | 客户端 → 运行时 | `cwd`、`provider`、`model`、可选 `reasoningEffort`、可选 `maxTokens`、可选 `rsiEnabled` | `{ serverInfo, sdkProtocolVersion, capabilities }` | 官方 `initialize` + 插件扩展 | 就绪边界；冷启动 0.6–0.8 秒 |
| `session/create` | 客户端 → 运行时 | `sessionId`、可选 `cwd`、可选 `preset` | `{ sessionId, preset, cwd }` | `ctx.agents.create` + 发布前 `ctx.agentPresets.mount` | 不触模型、不触工具；id 已存在返回 `SESSION_EXISTS` |
| `session/resume` | 客户端 → 运行时 | `sessionId` | `{ sessionId, preset, cwd }` | `ctx.agents.resume` | 要求 `sessionPersistence` 与查询服务；preset 定义缺失拒绝 |
| `session/setPreset` | 客户端 → 运行时 | `sessionId`、`preset` | `{ sessionId, preset }` | `ctx.agentPresets.select` | 仅空白会话；首个 turn 后 `PRESET_LOCKED` |
| `session/prompt` | 客户端 → 运行时 | `sessionId`、`contentBlocks` | `{ messageId }` | 官方 `prompt`（`followup`） | 入队即返回；未知会话按默认 cwd/preset 懒创建 |
| `session/cancel` | 客户端 → 运行时 | `sessionId` | `{}` | `ctx.agents.get(id).cancel({ kind: 'user' }, { keepInbox: false })` | 立即中断；无活动返回 `NO_ACTIVE_TASK` |
| `session/trace` | 客户端 → 运行时 | `sessionId`、可选 `turn`、`path` | `{ path, eventCount }` | 会话日志（`sessionQuery` / 持久化 JSONL） | 写文件在插件侧完成后才应答；`turn` 省略或 0 = 全会话 |
| `rsi.set` | 客户端 → 运行时 | `enabled`、可选 `sessionId` | `{ enabled, preset, scope }` | 无 `sessionId`：插件默认；有：等价 `session/setPreset` | `scope` 为 `default` 或 `session` |
| `rsi.get` | 客户端 → 运行时 | 可选 `sessionId` | `{ enabled, preset, composed }` | 插件默认 / 会话投影 | `composed=false` 表示未组合 preset 面 |
| `shutdown` | 客户端 → 运行时 | 无 | `{}` | 官方 `shutdown` | 退出进程；C 的 `release` 先发它再兜底杀进程 |

### `initialize`

```json
{"jsonrpc":"2.0","id":1,"method":"initialize","params":{
  "cwd":"<project-dir>","provider":"bole","model":"qwen3.8-27b","rsiEnabled":false
}}
```

结果：

```json
{"jsonrpc":"2.0","id":1,"result":{
  "serverInfo":{"name":"deepseek-harness-sdk-runtime","version":"0.0.1"},
  "sdkProtocolVersion":1,
  "capabilities":{"preset":true,"sandbox":"workspace-write","persistence":true,"trace":true}
}}
```

- `cwd` 必填、绝对路径；作为所有未显式给 cwd 的会话默认值。
- `provider` / `model` 必填；由运行时 `llm.resolveCallConfig` 校验，失败映射 `MODEL_NOT_FOUND` / `MODEL_AUTH_FAILED`。
- `sdkProtocolVersion` 整数，当前 1；官方服务器不返回该字段时按 0 处理。
- `rsiEnabled` 可选，布尔；插件据此设置**后续新会话**的默认 preset（true → `rsi-infer`，false/缺省 → `standard`），不需要额外一次 `rsi.set`。
- `capabilities.preset` 表示运行时是否组合了 `agentPresets`；`capabilities.sandbox` 取 `workspace-write` / `danger-full-access` / `unavailable`；`persistence` 表示能否 `resume`；`trace` 表示能否导出。

### `session/create`

```json
{"jsonrpc":"2.0","id":2,"method":"session/create","params":{
  "sessionId":"s-001","cwd":"<project-a-dir>","preset":"rsi-infer"
}}
```

结果：

```json
{"jsonrpc":"2.0","id":2,"result":{"sessionId":"s-001","preset":"rsi-infer","cwd":"<project-a-dir>"}}
```

- `sessionId` 必填、非空、全局唯一；与 agent id 同值。
- `cwd` 可选、绝对路径；省略用 `initialize.cwd`。
- `preset` 可选；省略用当前默认（`rsi.set` 设置的值，初始 `standard`）。
- 只做创建与预设挂载，不触模型、不触工具。

### `session/resume`

```json
{"jsonrpc":"2.0","id":3,"method":"session/resume","params":{"sessionId":"s-001"}}
```

结果同 `session/create`，`cwd` 取会话头。`preset` 从会话头与 `agent-preset/selected` 投影恢复；定义缺失返回 `PRESET_NOT_FOUND`，不改写日志。

### `session/setPreset`

```json
{"jsonrpc":"2.0","id":4,"method":"session/setPreset","params":{"sessionId":"s-001","preset":"standard"}}
```

结果 `{ sessionId, preset }`。只允许空白会话（尚未开始首个 turn）；否则 `PRESET_LOCKED`。

### `session/prompt`

```json
{"jsonrpc":"2.0","id":5,"method":"session/prompt","params":{
  "sessionId":"s-001",
  "contentBlocks":[{"type":"text","text":"读一下 README"}]
}}
```

结果 `{ "messageId": "..." }`。内容块 v1 只支持：

| 类型 | 字段 | 说明 |
| --- | --- | --- |
| `text` | `text` | 非空 UTF-8 |
| `image` | `data`（base64）、`mimeType` | `image/png` / `image/jpeg` / `image/webp` / `image/gif`；进运行时附件存储 |

未知会话按 `initialize.cwd` 与当前默认 preset 懒创建；要显式 cwd / preset 请先 `session/create`。图片解码失败返回 `INVALID_ARGUMENT`。

### `session/cancel`

```json
{"jsonrpc":"2.0","id":6,"method":"session/cancel","params":{"sessionId":"s-001"}}
```

结果 `{}`。请求立即应答，不等待模型或工具收尾；结束事实由 `turn/end`（`aborted`）给出。没有活动 turn 返回 `NO_ACTIVE_TASK`。

### `session/trace`

```json
{"jsonrpc":"2.0","id":7,"method":"session/trace","params":{
  "sessionId":"s-001","turn":2,"path":"<trace-file>"
}}
```

结果 `{ "path": "...", "eventCount": 37 }`。

- `path` 必填、目标文件；父目录必须已存在；已存在则覆盖。
- 写出的是一行一个原始会话事件（`SessionEvent` 信封），不是 SDK 事件；供排障与复现。
- `turn` 省略或 0 导出整个会话；只导出调用时已经存在的日志，不等待未来事件。
- 写文件失败返回 `-32603` + `data.code="TRACE_WRITE_FAILED"`（C 归类 `3007`）。

### `rsi.set` / `rsi.get`

```json
{"jsonrpc":"2.0","id":8,"method":"rsi.set","params":{"enabled":true}}
```

结果 `{ "enabled": true, "preset": "rsi-infer", "scope": "default" }`。

- 无 `sessionId`：改后续新会话的默认 preset；返回 `scope=default`。
- 有 `sessionId`：等价 `session/setPreset`；返回 `scope=session`。
- 运行时未组合 preset 面：`RSI_UNAVAILABLE`，状态不变。
- `rsi.get` 返回 `{ enabled, preset, composed }`；`composed=false` 时不伪造 preset，`preset` 为空串。

### `shutdown`

```json
{"jsonrpc":"2.0","id":9,"method":"shutdown"}
```

结果 `{}`；随后进程退出。C 的 `bole_release` 先发 `shutdown`，超时后杀进程树。

## 三、通知表

通知名统一用点形式；无 id。

| 通知 | 方向 | 载荷 | 说明 |
| --- | --- | --- | --- |
| `session.event` | 运行时 → 客户端 | `sessionId`、事件信封 | 官方已有，全量原始会话事件；C 库**不**用它做投影，保留给排障与未来扩展 |
| `session.status` | 运行时 → 客户端 | `sessionId`、`idle` / `running` | 官方已有；C 库内部用于状态跟踪，不单独投递 |
| `subagent.started` / `subagent.finished` | 运行时 → 客户端 | 父子会话 id、状态 | 官方已有；v1 不透传为 SDK 事件 |
| `bole.event` | 运行时 → 客户端 | `sessionId`、SDK 事件信封 | 新增；结构事件（任务/步骤/计划/模型/工具/运行时/终态）由插件投影后走这一条 |
| `assistant.delta` | 运行时 → 客户端 | `sessionId`、`turn`、`step`、`channel`、`attemptId`、`revision`、`text` | 新增；`channel` 取 `text` / `reasoning`；50ms/4KB 合并；尽力而为 |
| `rsi.state` | 运行时 → 客户端 | `scope`、可选 `sessionId`、`enabled`、`preset`、`composed`、`source`、可选 `delivery` | 新增；见下表 |

`bole.event` 是结构事件的统一投递口，插件负责把 harness 会话事件投影成 SDK 事件；C 库只解析信封、排队、回调，不再解析 harness 事件词汇。

```json
{"jsonrpc":"2.0","method":"bole.event","params":{
  "sessionId":"s-001",
  "event":{
    "event_id":42,"type":10,"session_id":"s-001",
    "task":1,"step":0,"seq":18,"ts_ms":1761100000000,
    "payload":{"v":1,"turn":1}
  }
}}
```

- `event.type` 取 `03-events.md` 的 SDK 事件枚举；`payload` 是该事件的载荷对象，必带 `"v":1`。
- `event_id` 由插件按会话/句柄单调递增；`seq` 取来源会话事件的序号，瞬时帧与运行时事件为 0。
- 插件也可用同一条 `bole.event` 投递 `TEXT_DELTA` / `REASONING_DELTA` / `RSI_STATE`；v1 的 C 库同时接受 `assistant.delta` / `rsi.state` 专用通知，二者语义相同，以先到者为准（插件只发一种）。

`assistant.delta` 的 `turn` / `step` 取自同一 attempt 的 `start` 帧；`chunk` 帧本身只带 `attemptId` / `revision` / `index`，插件按 `attemptId` 关联。`text` 是合并后的增量，UTF-8。

`rsi.state` 字段：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `scope` | `default` / `session` | 默认值变化或某会话状态变化 |
| `sessionId` | string | `scope=session` 时必填 |
| `enabled` | boolean | `preset === 'rsi-infer'` |
| `preset` | string | 实际 preset id；未组合时为空串 |
| `composed` | boolean | 运行时是否组合了 preset 面 |
| `source` | `config` / `interface` / `session` / `receipt` | 本次状态的来源 |
| `delivery` | object | 仅 `source=receipt`：`status`（`delivered` / `available` / `unsupported-model`）、`bundleId`、`deliveredMemoryIds`、`provider`、`model`、`bodySha256` |

发出时点：

1. 运行时启动、插件读取默认配置后，发一次 `scope=default` 的初始状态；未组合 preset 面时 `composed=false`、`preset=""`。
2. `rsi.set` 成功后，发对应的 `scope`。
3. 会话创建或空白期切换 preset 成功时，发 `scope=session`、`source=session`。
4. 收到经验投递收据（`user/message` 的 `source.kind = bole-experience`）时，发 `scope=session`、`source=receipt`。
5. 没有 preset 面时只发第 1 条，不伪造后续状态。

先后：状态变更先写会话事件（或先由运行时确认），再发通知；同一会话内顺序一致。通知不保证不丢；`rsi.get` 是补偿读取的权威入口。

## 四、错误响应

| JSON-RPC error | `data.code`（稳定串） | C 数值码 | 场景 |
| --- | --- | --- | --- |
| `-32700` | `PROTOCOL_PARSE_FAILED` | 2002 | JSON 解析失败 |
| `-32600` | `INVALID_ARGUMENT` / `LINE_TOO_LONG` | 1002 / 2005 | 非法请求对象、超长行 |
| `-32601` | `METHOD_NOT_FOUND` | 2002 | 未知方法 |
| `-32602` | `INVALID_ARGUMENT` | 1006 | 参数类型/取值非法 |
| `-32603` | `PRESET_NOT_FOUND` | 1004 | preset 未定义 |
| `-32603` | `PRESET_LOCKED` | 4003 | 首个 turn 后切 preset |
| `-32603` | `SESSION_EXISTS` | 4002 | 重复创建 |
| `-32603` | `SESSION_NOT_FOUND` | 4001 | 恢复/取消/轨迹的会话不存在 |
| `-32603` | `SESSION_NOT_RESUMABLE` | 4004 | 日志损坏或缺状态 |
| `-32603` | `PERSISTENCE_UNAVAILABLE` | 3006 | 没有持久化服务 |
| `-32603` | `RSI_UNAVAILABLE` | 3004 | 未组合 preset 面 |
| `-32603` | `NO_ACTIVE_TASK` | 7001 | 无活动 turn 可取消 |
| `-32603` | `TRACE_WRITE_FAILED` | 3007 | 轨迹文件写失败 |
| `-32603` | `MODEL_*` / `TOOL_*` / `CANCEL_FAILED` | 5xxx / 6xxx / 7002 | 运行期失败 |
| `-32603` | 其它 / 缺失 | 3007 | 未映射的 handler 错误 |

- 错误 `message` 是英文短句，仅用于日志；调用方按 `data.code` 判断。
- `data.retryable` 可选布尔；C 库按 04 表给默认值，字段存在时以字段为准。
- 可重试：传输失败、超时、限流、工具超时/崩溃、取消失败；其余不可重试。
- `bole_last_error` 由 C 库按上表填充；协议层错误不改变句柄的「就绪」状态。

## 五、顺序保证

已定：

1. **同一会话串行**：请求按到达顺序处理；跨会话并发，互不阻塞。
2. **任务内顺序**：`turn/start` →（`step/start` → `request/header`? → `tool/call` → `tool/result`? → `step/end`）* → `turn/end`；`assistant/message` 在对应 step 内，最终正文以它为准。
3. **增量**：`assistant.delta` / `bole.event` 与 `session.event` 在同一读线程上按到达顺序分发，但不保证跨通道严格交错；`TEXT_DELTA` / `REASONING_DELTA` 可丢弃，结构事件不丢。
4. **取消**：`session/cancel` 应答后，已经在管道里的增量仍可能到达；该 turn 的最后一条事件是 `turn/end`（`aborted`）；此后再无该 turn 的 step / tool / delta 事件。未开始的用户消息丢弃。
5. **终态唯一**：每个 turn 恰好一个 `TASK_FINISHED` 或 `TASK_FAILED`；`aborted` 归 `TASK_FINISHED`，不是失败。
6. **运行时异常**：进程退出只发 `RUNTIME_EXITED`，不为未结束的 turn 补造终态；该句柄作废，新句柄可用 `session/resume` 恢复。
7. **恢复**：`session/resume` 不重放历史事件；需要历史就调 `session/trace`。
8. **轨迹**：`session/trace` 只导出应答时已持久化/已在内存的日志；导出期间新产生的事件不一定包含。`seq` 在恢复后从持久化日志继续，不清零。
9. **多会话**：单条事件线程上按到达顺序分发，不保证跨会话的全局顺序；调用方按 `session_id` + `task` 过滤。

跨会话并发上限：v1 不设硬上限，由运行时调度；C 库的请求表按会话独立。

## 六、版本与兼容

- `initialize` 结果带 `sdkProtocolVersion`（当前 1）。C 库支持 1；缺失按 0 处理并继续；主版本更高时继续但记告警。
- 运行时版本与 `VERSION.json` 不一致时，默认继续并记日志；`RUNTIME_READY` 同时报两者。
- 新增方法/通知/字段只增不改；已发布字段不改名、不改含义。未知通知忽略，未知字段忽略。
- 单行上限与图片内联上限一致：默认 64 MiB；图片 base64 也走同一行上限。
- 不支持批量请求；未来若要加，另开方法。

## 七、集成接缝：RSI 由谁负责

RSI 是模式，不是服务开关。接缝按层划分：

| 层 | 职责 |
| --- | --- |
| C 库 | 读 SDK 配置里的 `rsi.enabled`，初始化后调 `rsi.set`；把 `rsi.state` 交给调用方；不判断开关语义 |
| 桥接插件 `@bole/sdk-server` | 选择 preset（`agentPresets.select` / 创建时 `mount`）、把 harness 事件投影成 `bole.event`、投影 `rsi.state`；不 import、不挂载经验插件 |
| preset 声明（补丁层） | 声明 `standard` 与 `rsi-infer`（仅这两条）；`rsi-infer` 的那一行挂载经验插件 |
| 经验插件 `@deepseek-ai/dsh-skill-bole-experience` | 按准确模型 ID 注入只读经验，记投递收据；不写记忆库 |
| 预设面 registry | `agent-preset-registry` 管定义与默认值；preset 锁定与恢复由它负责 |

这条接缝是内部的，不进 C 头文件；对 IDE 可见的只有 `rsi.set`、`rsi.get` 与 `rsi.state`。任务书要求的「配置文件开关」由 SDK 配置文件承担（`rsi.enabled`，初始化后转成一次 `rsi.set`）；「SDK 接口开关」就是 `rsi.set`。两者都只影响新任务。

## 八、工具注入与工具隔离

### 工具注入方向

- **内置工具**：由 preset 声明挂载（`tool-fs`、`tool-pwsh`、`tool-todo`、`tool-skill` 等），调用方无需声明，SDK 也不代理。采用 preset 面后，这些行不再由 base 全局挂载。
- **调用方工具**：一律走 MCP。每条一个 `@deepseek-ai/dsh-mcp-client` 条目，`transport` 取 `stdio` 或 `streamable-http`；挂载后它的工具与内置工具并列进入模型工具表，工具名形如 `mcp__<serverName>__<rawName>`。
- **v1 方向（已确认）**：调用方在运行时配置文件里声明 MCP server，重启生效；运行时注册/注销接口放二期。
- **落点**：`@deepseek-ai/dsh-mcp-client`。

### 工具隔离

- **执行位置**：stdio MCP server 是运行时进程的子进程，streamable-http 是外部服务；内置 shell 工具经 `subprocess-local` 起子进程。
- **崩溃**：某个 MCP server 崩溃只影响该 server 的工具调用，结果为 `tool/result` 的 `isError`（带 `error.code`），运行时继续，不终止会话。`failOnStartupError` 默认 false。
- **超时**：每条 MCP 条目可配 `toolCallTimeoutMs`；内置工具的超时由工具自身与 `@deepseek-ai/dsh-tool-call-timeout-policy` 决定。SDK 控制面不等工具，调用方可用 `session/cancel` 中断。
- **审批**：headless 运行时没有审批应答者，`approval: ask` 会 fail-closed；SDK 补丁层固定 `approval.policy: never`，并且不挂载会产生 `ask` 的组件（`hooks-claude-code`、`auto-review`）。
- **沙箱**：默认 `workspace-write`；启动自检探测后端，不可用在 `capabilities.sandbox` 报 `unavailable`，并按配置可切 `danger-full-access`。
- **进程级隔离**：运行时与 IDE 分属不同进程；运行时退出时用 Job Object 回收整棵进程树。

M1 待验证：杀 MCP 子进程、工具超时、断网三种场景下的事件序列与错误码。
