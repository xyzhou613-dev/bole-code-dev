# 09 A 轨开发计划（协议与运行期）

状态：执行计划（2026-10-22 起草）。范围是 [08-work-split.md](08-work-split.md) 的 A1–A8；本文**不改变** 02/03/04 与 `native/include/bole_agent.h` 的 1.0 契约。契约变更仍按 08 第三节的 fixture → 文档 → A+B 双审流程走。

依据：08 分工、02 协议、03 事件、04 ABI、05 测试、06 开发环境、07 离线包，以及 2026-10-22 在 `0.1.7-alpha.2` 源码上的逐点核对。文中路径一律相对 `<repo>`，本机路径用 `$DEV_RUNTIME` / `$DEV_HOME` / `$PATCH_FILE` 占位。

## 一、目标、交付物与边界

**一句话目标**：把 `--profile sdk` 扩成伯乐 SDK 运行时——自建插件接管 stdio 协议、补齐会话/RSI/轨迹方法、把 harness 事件投影成 SDK 事件、用补丁层组合 `standard` / `rsi-infer` 两个模式，并交付可被 B 轨打包复用的插件产物、补丁、配置模板与玩具 MCP。

| 交付物 | 路径 | 内容 | 对应 |
| --- | --- | --- | --- |
| 桥接插件 | `runtime/packages/sdk-server/` | 传输、请求分发、会话表、投影、RSI、轨迹、打包入口 | A1–A5、A8 |
| 补丁层 | `runtime/packages/sdk-app/cordis.patch.yml` | 禁用官方 server；preset 面；关外网面；审批；模型行 | A4、A6 |
| 运行期配置 | `runtime/config/` | 站点配置模板（模型地址、MCP server）、profile 说明 | A6、A7 |
| 玩具 MCP | `examples/toy-mcp/` | 无外网、无第三方依赖、两三个无副作用工具 | A7 |
| 开发脚手架 | `runtime/dev/`（不随包） | 装配脚本、JSON-RPC 冒烟、mock 模型、fixture 捕获 | A0、C1 |
| Fixtures | `native/tests/fixtures/`（共同） | 真实运行捕获的 wire 通知与 manifest | A3、C1 |
| 打包输入 | `runtime/dist/`（git 忽略）、`runtime/version.inputs.json` | 插件/经验包 tarball、版本清单输入 | A8、B6 |

**不做**：C 库、两个示例、离线包脚本、对外四份文档（B 轨）；不改官方 `@deepseek-ai/*` 源码；不伪造阶段、终态或 RSI 状态；不替 B 选实现。

**A 轨完成定义**：A1–A8 的 DoD 全部有可复现证据；G1 的插件侧、G2 的插件/事件侧、G3 的打包输入齐备；`native/`、`packaging/`、`docs/delivery/` 无 A 的提交。

## 二、起点与差距（已核对源码）

### 2.1 已有

- Stage 0 插件 72 行：禁用官方 `sdk-jsonrpc-server` 后创建 `JsonRpcLineTransport` + `HarnessSdkJsonRpcServer`，接管 `initialize` / `session/prompt` / `shutdown`，真实 sdk profile 下跑通过一轮。
- Stage 0 补丁：模型行、`approval: never`、外网与 telemetry 行。
- 官方 0.1.7-alpha.2 的 base 已带 `session-persistence-jsonl`、`session-query-sqlite`、`agent-preset-registry`；`standard` / `rsi-infer` 两条声明在 Web bundle 中，sdk profile 默认不加载，需要 A4 搬进补丁层。

### 2.2 缺口

| 工作包 | 现状 | 本计划落点 |
| --- | --- | --- |
| A1 骨架/传输 | 只有官方宽松 transport | P1：严格分帧 + 错误码 + 行上限 + 接管 |
| A2 会话方法 | 只有官方 prompt 懒创建 | P1：自有会话表 + create/resume/cancel/setPreset |
| A3 事件投影 | 无 | P2：15 类投影 + 增量合并 + fixtures |
| A4 RSI/preset | 补丁无 preset 面 | P3：补丁行 + 插件选择/投影 + 收据 |
| A5 轨迹 | 无 | P3：按 turn 导出 + 前置校验 |
| A6 交付补丁层 | 基础版部分行 | P1 基础自检 + P4 收尾 |
| A7 运行时组装/玩具 MCP | 无 | P4 |
| A8 打包/版本 | 包元数据有错 | P5 |

### 2.3 源码核对发现的实施前提

| 编号 | 发现 | 证据 | 对计划的影响 |
| --- | --- | --- | --- |
| F1 | 官方 transport 不满足 02：解析失败静默忽略、无 64 MiB 行上限、数组被归一成 `{}`、错误响应没有 `data.code` | `sdk/protocol/src/transport.ts`（`handleLine` / `handleIncomingRequest` / `writeError`） | A1 必须自带严格传输，不能直接把官方类当合规实现 |
| F2 | 官方 server 的会话表是私有字段，只有三个方法，且创建 agent 时不组合 preset；子类无法复用其状态 | `sdk/server/src/server.ts`（`sessions`、`createSession`、`handleRequest`） | A2 需要自有 server（fork + 扩展），见 D1 |
| F3 | preset 的正确落点已存在：`agents.create({ meta.agentPreset, setup })` + `agentPresets.mount(ctx, id)`；空白期切换用 `select`；恢复按会话头投影 | `core/agent/src/index.ts`、`api/session-controller/src/agent.ts`（`composeAgent`）、`preset/agent-preset-registry/src/index.ts` | A4 照既有消费方写法实现，不另造状态 |
| F4 | `@deepseek-ai/dsh-skill-bole-experience` 未发布到 npm；其 tsconfig 依赖展平仓库里不存在的根 `tsconfig.base.json` | 该包 `package.json`、`tsconfig.json`；npm 查询 404 | A4 前必须解决经验包的可构建产物（D7），否则离线包无法组合 `rsi-infer` |
| F5 | 插件 `peerDependencies` 把 `@deepseek-ai/cordis` 写成 `0.1.7-alpha.2`（实际为 `~4.0.4`）；`files` 只列 `lib/index.js` | `runtime/packages/sdk-server/package.json` | A8 修正，否则装进运行时树解析失败/漏发多文件 |
| F6 | `--patch` 可重复，且 `$DSH_HOME/cordis.patch.yml` 是高于 profile 的独立层；C 的启动参数只承诺一个 `--patch` | CLI 参考（profile → home → argv 层叠） | 站点配置（模型地址、MCP）放 home 层，交付 patch 保持单一文件，见 D6 |
| F7 | 轨迹可读的权威入口：活会话用 `Session.events`，冷会话用 `ctx.sessionQuery.readSession(id)` | `core/session`、`session-query/session-query/src/index.ts` | A5 不依赖 Web 的 `session-log-export` |
| F8 | 发布包 `@deepseek-ai/dsh-llm-pi-ai@0.1.7-alpha.2` 只解析 `apiKeyEnv`，忽略配置里的内联 `apiKey`；仓库源码两者都支持 | 安装树 `lib/index.js` 的 `resolveApiKey`；P0 实测返回 `No API key for provider: bole` | 交付 patch 改用 `apiKeyEnv: BOLE_API_KEY`，由 `<DSH_HOME>/.env` 提供占位 key；不能依赖内联 `apiKey` |
| F9 | Loader 对插入行/预设行的裸包名按 profile 解析；只把 `@bole/sdk-server` 或经验包放 `runtime/node_modules` 会 `failed to import` | P0 实测 `@bole/sdk-server` 与 `@deepseek-ai/dsh-skill-bole-experience` 均失败；复制到 `$DSH_HOME/profiles/sdk/node_modules` 后成功 | B6 必须把自定义包装进 SDK profile 的 `node_modules`，或 patch 用相对/绝对入口；见 `runtime/config/README.md` |
| F10 | SDK 的 preset 面缺少 Web 的 `subagent-model-selection-settings` 宿主行时，`standard`/`rsi-infer` mount 报 `tool-subagent ... modelSelectionSettings requires ...` | A4 实测 | `sdk-app` 补丁必须插入该宿主行 |

### 2.4 开发环境现状与选择

当前交付仓库只有包子树，没有根 workspace、没有 `node_modules`。如果开发机上有完整 harness 检出，先确认它锁在 0.1.7-alpha.2；其他版本不能直接当基线用。

- **主路径（推荐）**：在 `$DEV_RUNTIME` 用 npm 安装 `@deepseek-ai/dsh@0.1.7-alpha.2`（其依赖闭包已含官方 server/protocol/preset/persistence），把 `@bole/sdk-server` 以软链或 tarball 装进 `node_modules`，用真实 `lib/bin.js` 跑。
- **备用路径**：在完整 harness 检出上 `git worktree add` 到标签 `dsh-v0.1.7-alpha.2`，`pnpm install && pnpm build`，用于需要官方 testkit 的场景；成本高，只在 npm 树缺测试依赖时启用。
- 类型检查、单测、mock 模型的工具装在 `runtime/dev/`（私有 `package.json`，`node_modules` 忽略），不进入离线包。

## 三、技术方案与关键决定

### 3.1 分层

```text
stdin/stdout
  │  BoleLineTransport：分帧、行上限、JSON-RPC 错误、写入
  ▼
BoleSdkServer：会话表、方法分发、preset 选择、错误映射、关闭
  ├─ SessionEvent 投影器（turn/step/plan/header/tool/message/end）
  ├─ 增量合并器（agent/assistant-stream：start/chunk/end）
  ├─ RSI 投影（会话头、agent-preset/selected、经验收据）
  └─ Trace 导出（活会话 events / 冷会话 sessionQuery）
  ▼
bole.event（结构事件与 RSI） + assistant.delta（增量）
```

约束：所有出站帧走同一写入点；投影只读 harness 事实；没有来源就不产生事件；`event.type` 只允许 03 的枚举。

### 3.2 模块布局

```text
runtime/packages/sdk-server/src/
├── index.ts        插件入口：配置、transport/server 生命周期、关闭与退出
├── transport.ts    严格 JSON-RPC 行传输（fork 官方实现，保留署名与版本注释）
├── server.ts       BoleSdkServer：会话表、方法实现、preset 组合、错误映射
├── protocol.ts     插件侧入参/出参类型与校验（不导出为公共 API）
├── errors.ts       BoleRpcError、data.code、retryable 默认值
├── preset.ts       preset 面适配：resolve/mount/select/default/composed
├── trace.ts        session/trace：读取、按 turn 切片、写文件
└── events/
    ├── projection.ts  SessionEvent → SdkEvent（纯函数 + 每会话小状态）
    ├── payloads.ts    正文/思考/参数/结果提取与截断
    ├── deltas.ts      assistant-stream 合并器（50ms/4KB/revision）
    └── ids.ts         event_id、ts_ms
runtime/packages/sdk-server/tests/   单元与注入流测试
runtime/dev/                         装配、冒烟、mock 模型、fixture 捕获
examples/toy-mcp/                    零依赖 stdio MCP server
runtime/config/                      站点配置模板（home patch、MCP 示例）
```

### 3.3 关键决定

| 编号 | 决定 | 理由 | 备选与否决原因 |
| --- | --- | --- | --- |
| D1 | **fork 官方 299 行 server 到本包并扩展**，不修改官方源码 | 官方会话表私有、无 preset 组合、无自定义方法，包装/子类无法共享状态；fork 能原样保留附件、关闭、DeepSeek 兜底等语义 | 「子类只重写 handleRequest」：无法拿到私有 `sessions`，prompt 会二次创建；「从零重写」：语义漂移风险更大 |
| D2 | **fork 严格 transport**，实现 02 的分帧、64 MiB 上限、`-32700`/`-32600`/`-32602` 与 `data.code` | 官方 transport 与冻结协议不一致；合规性不能靠文档声明 | 「在官方 transport 外套预解析」：双缓冲、无法干净地限行并在超限后关闭 |
| D3 | **默认 preset 由插件持有**（`rsi.enabled` / `initialize.rsiEnabled` → `standard` 或 `rsi-infer`），不改 registry 的 volatile 默认值 | `rsi.set` 只影响后续新会话，插件是最小 owner；避免与 settings/UI 默认值互相覆盖 | 「写 registry `selectedDefault`」：跨进程状态、与 Web 语义耦合 |
| D4 | **结构事件与 RSI 走 `bole.event`；增量走 `assistant.delta`**（02 允许二选一，C 两种都收） | 结构事件获得统一信封、`seq` 与 fixtures；高频增量走紧凑专用通知，避免事件 id 抖动 | 「全部走 bole.event」：也可行，但增量体积/编号噪声更大；交付前需 B 确认选用形态并据此写 fixture |
| D5 | **轨迹：活会话读 `Session.events`，冷会话读 `sessionQuery.readSession`**；按 `turn/start..turn/end` 切片 | 活会话含未落盘的最新前缀，符合「调用时已存在」；冷会话有查询服务 | 「只读 JSONL」：活会话会漏最新事件；「只读内存」：resume 前的会话不可用 |
| D6 | **交付 patch 单文件**（禁用行 + 插件行 + preset 行 + 外网/审批/模型行）；**站点配置放 `$DSH_HOME/cordis.patch.yml`**（模型地址、额外 MCP 行），密钥占位放 `$DSH_HOME/.env` | C 的启动参数固定一个 `--patch`；home 层天然高于 profile、可写、可重启生效，满足「配置文件声明 MCP」；`.env` 解决 F8 的 `apiKeyEnv` 解析 | 「拆多个 `--patch`」：要改 04/07 的启动契约；「全部塞交付 patch」：现场改只读运行时树 |
| D7 | **经验包以 A 构建的 tarball 交付**：最小 tsconfig/esbuild 构建 `lib/index.js` + `assets`，必要时把 `zod` 打进产物或随树安装；只消费、不修改 `skill/skill-bole-experience` 源码 | 该包未发布 npm、展平仓库无根 tsconfig；离线包必须能解析 `@deepseek-ai/dsh-skill-bole-experience` | 「B 的打包脚本现场构建」：跨轨依赖、B 无 TS 构建环境；「改用 Web 包内的 preset 文件」：仍缺经验插件本体 |

D4、D6、D7 涉及 B 的消费方式，落地前在 PR/群里确认；D6 若 B 坚持多 `--patch`，先改 04/07 再改代码。

### 3.4 方法与落点

| 方法 | 落点 | 关键校验/错误 | 备注 |
| --- | --- | --- | --- |
| `initialize` | fork 的官方实现 + 扩展字段 | `provider/model` 由 `llm.resolveCallConfig` 校验；失败映射 `MODEL_NOT_FOUND` / `MODEL_AUTH_FAILED`；`cwd` 必须绝对路径 | 先 `await loader`；返回 `sdkProtocolVersion:1` 与真实 `capabilities`；按 `rsiEnabled` 设定默认 preset |
| `session/create` | `ctx.agents.create({ sessionId, meta:{cwd, agentPreset}, setup })` | 空 id/非绝对 cwd → `INVALID_ARGUMENT`；活跃或已持久化 id → `SESSION_EXISTS`；preset 不存在/损坏 → `PRESET_NOT_FOUND` | `setup` 内 `await agentPresets.mount(agentCtx, presetId)`；发布前完成组合；不触模型/工具 |
| `session/resume` | `ctx.agents.resume({ resumeSessionId, agentOptions, setup })` | 无持久化/查询服务 → `PERSISTENCE_UNAVAILABLE`；无会话 → `SESSION_NOT_FOUND`；日志损坏 → `SESSION_NOT_RESUMABLE`；preset 定义缺失 → `PRESET_NOT_FOUND` | 先从 `sessionQuery` 读头，按头部 `agentPreset` 组合；不重放历史 |
| `session/prompt` | 自有懒创建 + `agent.followup(message)` | 未初始化 → `-32603`；内容块非法/图片解码失败 → `INVALID_ARGUMENT`；未知会话按 `initialize.cwd` + 当前默认 preset 建 | 复用 `admitEncodedImages` / `createUserMessage`；入队即返回 |
| `session/cancel` | `ctx.agents.get(id)` → `cancel({kind:'user'},{keepInbox:false})` | 无 agent → `SESSION_NOT_FOUND`；无活动 turn → `NO_ACTIVE_TASK` | 活动判定 = `turnBoundary.openTurnStartSeq !== null` 或 `inbox.nextTurn/nextStep` 非空或 `status=running`；覆盖「模型调用中 / 工具执行中 / 排队中」三种取消时机；立即应答，终态由 `turn/end` 给出 |
| `session/setPreset` | `ctx.agentPresets.select(agent, preset)` | 首 turn 后 → `PRESET_LOCKED`；未定义 → `PRESET_NOT_FOUND` | 只对空白会话；成功后发 `RSI_STATE` |
| `rsi.set` / `rsi.get` | 插件默认值 + `select`（带 sessionId 时） | 未组合 preset 面 → `RSI_UNAVAILABLE`，状态不变；`composed=false` 时 `preset=""` | 默认只影响后续新会话；返回 `scope` |
| `session/trace` | D5 | 父目录不存在/写失败 → `TRACE_WRITE_FAILED`；无会话 → `SESSION_NOT_FOUND` | 写原始 `SessionEvent` JSONL；`turn` 省略/0 为全会话；只导出调用时已存在的事件 |
| `shutdown` | 官方 teardown + 自有会话表/投影订阅释放 | — | 先 flush 再 dispose 根 fiber，退出码 0 |

### 3.5 事件投影规则（实现摘要）

- 只投影 SDK 自己管理的会话；子代理子会话不透传（03 要求 v1 不透传）。
- 信封：`session_id` 取会话 id；`task`/`step` 取事件 `data.turn`/`data.step`；`seq` 取 `event.seq`；`ts_ms` 取 `event.time`；`payload.v=1`。
- 15 类事件全部有纯函数与单测；`VERIFY_CHECK` 不产生。
- 终态唯一：`turn/end.reason.kind` 映射 `TASK_FINISHED` / `TASK_FAILED`；`aborted` 归 50。
- 截断：`args` 64 KB、`resultSummary` 8 KB、正文/思考各 256 KB；按 UTF-8 字节切、带 `truncated:true`；全文只在 trace。
- 增量：`agent/assistant-stream` 的 `start` 记录 `attemptId→{turn,step}`；`chunk` 按 `attemptId+channel` 合并，50 ms 或 4 KB 先到者发送，`revision` 更旧丢弃；`end`/turn 结束/关闭时 flush。
- RSI：初始状态在首个 `initialize` 后、首个 `TASK_STARTED` 前发出；会话创建/空白切换发 `source=session`；经验收据（`user/message.source.kind='bole-experience'`）发 `source=receipt`。

### 3.6 错误与关闭

- 处理器抛 `BoleRpcError(code, message, details)`：transport 写 `error.data={code,...details}`；未知异常写 `-32603`、`data.code` 缺失或 `INTERNAL`，由 C 归 3007。
- 行超限：回 `-32600` + `LINE_TOO_LONG`，随后关闭传输并让进程退出；解析失败：回 `-32700` + `PROTOCOL_PARSE_FAILED`，进程继续。
- `shutdown`：停在接收新请求 → flush → 逐会话 dispose → 释放投影/合并器定时器 → dispose 根 fiber → `exit(0)`；重复请求复用同一退出任务。

## 四、阶段计划

总工作量按 08 的 14 人日，加 1 人日环境/脚手架；下面按依赖排序，不按工作包编号机械排序。

| 阶段 | 工作包 | 估算 | 出口 |
| --- | --- | --- | --- |
| P0 环境与脚手架 | A0 | 1d | 一条命令起真实运行时；Stage 0 冒烟绿 |
| P1 协议核心 | A1、A2、A6 基础 | 3.5d | 会话四方法 + initialize/shutdown 真实通过；错误码对齐 |
| P2 事件投影 | A3 | 3d | 15 类事件 + 增量合并 + 首批 fixtures |
| P3 模式与轨迹 | A4、A5 | 5d | 两 preset、RSI 三态、trace、G2 |
| P4 运行时组装 | A6 收尾、A7 | 1.5d | 真实 E2E：模型 → 玩具 MCP → 结果 |
| P5 打包与联调 | A8、C1/C2 支持 | 1d+缓冲 | tarball 可装、VERSION 输入齐、G3 插件侧 |

### P0 环境与脚手架（1d）

任务：

1. `$DEV_RUNTIME` 安装 `@deepseek-ai/dsh@0.1.7-alpha.2`；确认 `lib/bin.js`、`--profile sdk`、`--dump-config` 可用。npm 11 默认拦截安装脚本，按 01 风险表处理（`npm approve-scripts`），否则原生模块缺失。
2. 插件软链/构建进运行时树；`runtime/dev/` 建 `package.json`（typescript、vitest、@types/node、esbuild、tsx 按需），忽略 `node_modules`。
3. 写 `runtime/dev/smoke.mjs`：启动运行时、发 `initialize`/`shutdown`、断言 stdout 首帧、stderr 含 `[bole-sdk-server] loaded`、无 `did not activate`、退出码 0。
4. 写 `runtime/dev/mock-model.mjs`：OpenAI chat-completions SSE，脚本化回复；随机端口、测试自己关闭。
5. 建 fixture 捕获脚本骨架（收到通知按行写 JSONL；路径/id/时间戳占位替换）。

DoD：`node runtime/dev/smoke.mjs` 绿；`--dump-config` 快照留档；Stage 0 三个方法仍通过。

### P1 协议核心（3.5d）

任务：

1. `transport.ts`：严格分帧、行上限、错误码、`data.code`、批量拒绝、关闭语义；注入流单测覆盖超长/非法/数组/未知方法/写失败。
2. `server.ts`：fork 官方实现 + 自有会话表；`initialize` 扩展字段、`capabilities` 从服务读取；`shutdown` 复用官方 teardown。
3. `session/create` / `resume` / `cancel` / `setPreset`；`prompt` 支持懒创建；错误映射集中到 `errors.ts`。
4. A6 基础：补丁自检（`--dump-config` 行存在、官方 server 禁用、无重复注册）、`approval never`、外网/telemetry 关、模型行。
5. 把官方 server 的既有测试语义移植到本包（初始化、prompt 懒创建、附件、关闭幂等）。

DoD：真实运行时下 initialize/create/prompt/cancel/resume/setPreset/shutdown 全绿；错误码与 02 表一致；`build` 与 `tsc --noEmit` 通过。

### P2 事件投影（3d）

任务：

1. `events/projection.ts` + `payloads.ts`：15 类事件的纯函数、截断、终态映射、tool call 关联、plan/todo、header。
2. `events/deltas.ts`：合并器与 attempt 表；50ms/4KB/revision 单测（fake timers）。
3. 会话过滤：只发 SDK 会话；子代理不透传。
4. fixtures：先捕 `happy-tool`、`tool-error`、`model-error`、`cancel-mid-tool`；manifest 记场景/期望终态/超时。
5. L1 回放：同一 fixture 喂投影器，逐条比对 wire 形态。

DoD：15 类事件都有断言；增量可丢、终态不丢；fixtures 来自真实运行；`ASSISTANT_MESSAGE` 权威替换。

### P3 模式与轨迹（5d）

任务（A4）：

1. 补丁层加入 preset 面：禁用 base 的 agent 面行（01 第七节清单逐项核对），插入 `agent-preset-registry(default: standard)` 与两条 `agent-preset` 声明；只随包这两条。
2. 插件 `preset.ts`：`resolve`/`mount`/`select`/`defaultId`/`composedPreset`；`composeAgent` 照 `api/session-controller` 写法。
3. `rsi.set/get`、`session/setPreset`、`RSI_STATE` 投影（default/session/receipt）；标准模式无收据。
4. 经验包构建/安装 spike（D7）；用仓库 `assets/final-experiences.json` 验证 `delivered` / `available` / `unsupported-model` 三态。
5. 锁定校验：首个 turn 后 `PRESET_LOCKED`；空白期切换成功；未组合面时 `RSI_UNAVAILABLE`。

任务（A5）：

1. `trace.ts`：活会话/冷会话读取、turn 切片、路径校验、覆盖写、事件计数。
2. 错误：无会话/无持久化/父目录缺失/写失败；大字段全文（与投影截断对照）。
3. 测试：turn=0、指定 turn、无历史重放、resume 后 seq 连续。

DoD：两个 preset 各建会话；RSI 三态可观测；trace 文件可按 turn 过滤；G2 插件侧证据齐。

### P4 运行时组装（1.5d）

1. 补丁收尾：外网面全关、`session-title-llm` 关、审批、模型行、preset 行合并为单文件；补一个「base 行与 preset 行不得重复」的静态检查脚本。
2. `runtime/config/`：`home.patch.yml.template`（模型地址、额外 MCP 行、可选 rsi 默认）、`home.env.template`（`BOLE_API_KEY=local`，见 F8）、`mcp.servers.example.yml`、profile 说明。
3. `examples/toy-mcp/`：零依赖 stdio MCP（`add`/`echo`/`now`），启动 <500ms，可被杀；`README` 写工具 schema。
4. `runtime/dev/e2e.mjs`：真实运行时 + mock 模型 + 玩具 MCP，跑「输入 → 模型 → 一次工具调用 → 结果」并计时。
5. 启动自检：`capabilities.sandbox` 真实；后端不可用按 08 兜底；`RUNTIME_READY` 字段齐。

DoD：E2E <1 分钟；杀玩具 MCP 后工具错误可见且运行时继续；`--dump-config` 无重复行；配置模板可声明 MCP。

### P5 打包与联调（1d + 缓冲）

1. `package.json`：修 `cordis` peer 为 `~4.0.4`、`files` 收全部 `lib/**/*.js`、版本与 dsh 锁定；`tsc` 产物可被裸名解析。
2. `npm pack` 产出插件 tarball；经验包 tarball；在干净 `$DEV_RUNTIME` 安装验证。
3. `runtime/version.inputs.json`：dsh、node、插件、preset、经验包、玩具 MCP 版本；交 B 合并进 `VERSION.json`。
4. fixtures 补齐（RSI 三态、max-tokens、long-tool-output）与 manifest；支持 C2 的计时与事件序列记录。
5. 升级核对清单：官方 server/transport 与 preset 面的 diff 步骤。

DoD：tarball 在干净树安装后 `initialize` 成功；版本输入齐；fixtures 可一键回放；G3 插件侧证据齐。

### 实施进度（2026-10-22）

| 项 | 状态 | 证据 |
| --- | --- | --- |
| P0 环境与脚手架 | 完成 | `runtime/dev` 冒烟、mock 模型、捕获脚本；0.1.7 运行时起停通过 |
| A1 传输与骨架 | 完成 | 严格 transport 单测 5 个；真实 profile `initialize`/`shutdown`；stdout 全为 JSON-RPC |
| A2 会话方法 | 完成 | create/duplicate/prompt/cancel/setPreset/resume 冒烟全绿；无 preset 面时 `PRESET_NOT_FOUND`/`RSI_UNAVAILABLE` |
| A3 事件投影 | 核心完成 | 13 类可投影事件 + 增量合并；真实覆盖 10/11/12/14/15/22/30/31/50/51；4 个 fixtures |
| A4 RSI/preset 面 | 核心完成 | 两个 preset 创建、空白切换、`PRESET_LOCKED`、`rsi.set/get`、默认状态；收据 `delivered`/`available`/`unsupported-model` 实测 |
| A5 轨迹 | 未开始 | — |

fixtures：`happy-tool`、`text-turn`、`cancel-mid-turn`、`model-error`、`rsi-delivered`、`rsi-available`、`rsi-unsupported`。`max-tokens`、`long-tool-output`、`tool-error` 等 A5 后再补。

## 五、周排期与门

按每周约 3 个有效工作日估算（与 B 并行、含联调等待）；若全职投入可压到 3–4 周，但 G1/G2 仍按契约与 B 的节奏对齐。

| 周 | A 轨 | B/C 依赖 | 门 |
| --- | --- | --- | --- |
| W1 | P0；P1 的 transport + initialize/create | B1/B2 骨架；C1a 捕获脚本 | G1 的 A 侧：真实运行时 + 最小会话 + 计时 |
| W2 | P1 收尾（resume/cancel/setPreset）；P2 投影核心 | B4 控制台骨架；C1a 首批 fixture | — |
| W3 | P2 收尾；A4 的 preset 面与 RSI | B2 接口面；C1b 回放 | G2：全方法、15 类事件、取消、RSI（全职投入）；否则顺延 W4 初 |
| W4 | A4 收尾；A5 trace；P4 组装 | B3（若 Windows 就绪）、B5 | — |
| W5 | P4 收尾；P5 打包输入 | B6/B7 开始 | G3 的 A 侧输入 |
| W6 | C2/M3 支持与修复 | B6/B8、现场联调 | G4（B 主导） |

关键路径：`P1 → P2 → A4 → P4 → C2`；A5 与 P4 可在等 B 时并行。P4 的玩具 MCP 可提前到 W1 做（不依赖 A2/A3）。

08 的 G2 排在 W3 末；本计划在「每周 3 个有效工作日」下预计 W4 初达成，全职投入可保持 W3。这个差异在 W1 对齐会上确认，必要时请 PM 调整 G2 时点，而不是压缩测试。

## 六、测试与证据

| 层 | A 轨内容 | 证据 |
| --- | --- | --- |
| L0 单元 | transport 分帧/上限/错误；投影 15 类；截断边界（空/恰好/超限/多字节）；合并器时序；turn 切片；路径校验 | vitest 输出 |
| L1 契约 | 注入流跑 server；fixture 回放投影；与 B 共用同一 fixture | fixture 文件 + 断言 |
| L2 补丁层 | 真实 `--profile sdk --patch`；`--dump-config`；stderr 断言；自定义方法答复 | 命令 + 输出 + 退出码 |
| L3 集成 | mock 模型 + 玩具 MCP；一次工具调用计时；取消三时机；RSI 三态；trace | 日志 + 计时 + 事件序列 |

命令占位（按 06 的变量约定）：

```bash
# 类型检查与构建
"$DEV_RUNTIME/node_modules/.bin/tsc" -p "$REPO/runtime/packages/sdk-server/tsconfig.json"

# 补丁层快照
env DSH_HOME="$DEV_HOME" DSH_TELEMETRY_DISABLED=1 \
  node "$DEV_RUNTIME/node_modules/@deepseek-ai/dsh/lib/bin.js" \
  --profile sdk --patch "$PATCH_FILE" --dump-config

# 冒烟 / E2E / fixture 捕获
node "$REPO/runtime/dev/smoke.mjs"
node "$REPO/runtime/dev/e2e.mjs"
node "$REPO/runtime/dev/capture.mjs" --scenario happy-tool
```

已知红单独记录：`sdk/server` 两个 fork 改名导致的既有失败不是本次引入（05 第七节）。

## 七、风险与待决

| 编号 | 风险/待决 | 影响 | 应对 | Owner |
| --- | --- | --- | --- | --- |
| R1 | 经验包不可构建/未发布 | A4、G2、离线包 RSI 全线阻塞 | P0/P3 做构建 spike；esbuild 打包 + assets；必要时请 PM 定构建归属 | A + B |
| R2 | fork 官方 server/transport 漂移 | 升级后语义偏差 | 锁定 0.1.7-alpha.2；移植官方测试；升级 diff 清单 | A |
| R3 | 通知形态（D4）与 B 预期不一致 | fixtures/C 侧返工 | P1 结束前书面确认；fixtures 先定形态 | A + B |
| R4 | preset 面跟随上游漂移 | 工具重复/缺失 | 锁定版本；重复行静态检查；升级对照 Web patch | A |
| R5 | 真实网关/模型不可达 | L3 只能用 mock | mock 模型覆盖确定性路径；现场复验；允许列表写进文档 | PM |
| R6 | Windows 环境暂缓 | 无法本机验 pwsh/路径 | A 代码保持平台中立；WSL 单测路径/分隔符；B 负责 Windows 验证 | PM/B |
| R7 | 站点 MCP 配置落点未定 | A7 返工 | D6 的 home patch 方案先与 B 确认；模板先行 | A + B |
| R8 | trace 的 path 语义（相对/绝对、父目录）未冻结 | C 侧调用不一致 | 实现按「绝对优先、相对按 initialize.cwd 解析、父目录必须存在」；报 B 确认 | A + B |
| R9 | `session/create` 对已持久化 id 的判定未冻结 | 覆盖日志风险 | 默认同时查活跃与持久化，返回 `SESSION_EXISTS`；报 B 确认 | A + B |
| R10 | 沙箱后端不可用 | 工具硬失败 | `capabilities.sandbox` 如实报；按 08 可切 danger-full-access | A6/B2 |
| R11 | 周排期偏紧 | G1/G2 延期 | P1/P3 优先；P4 玩具 MCP 提前；每周对齐调整 | A |

需 B/PM 确认的事项：D4 通知形态、R8 trace 路径、R9 重复 id、D6 站点配置层、D7 经验包构建归属、08 第十节领取表与 G0 签收。

## 八、协作与提交

- 分支：从 `develop` 开 `feat/bole-runtime`；一个工作包一个 PR；契约变更单独小分支、A+B 双审。
- PR 必含：代码、测试、fixture（如涉及）、文档更新、验证命令与输出；不提交 `node_modules`、`lib/`、临时 `DSH_HOME`、本地 patch。
- 目录边界：只改 `runtime/packages/sdk-server/`、`runtime/packages/sdk-app/`、`runtime/config/`、`runtime/dev/`、`examples/toy-mcp/`；`native/`、`packaging/`、`docs/delivery/` 不动。`runtime/preset/` 若最终不单独落文件（D6 内联），先改 07/08 的目录描述再定。
- 契约：02/03/04/header 的名字、编号、含义不改；新增字段只追加；变更按 08 第三节。
- 同步：每日 15 分钟对齐；fixtures 与 B 共用；任何跨目录改动先在 PR 说明。

## 九、本周立即可做

1. PM 填 08 第十节领取表与 00 确认记录；确认 G0 清单（02/03/04/header 已读、占位符约定）。
2. 切 `feat/bole-runtime`；按 P0 建 `$DEV_RUNTIME` 与 `runtime/dev/`，先让 Stage 0 冒烟可重复。
3. A1 技术验证：用最小自定义方法验证「fork server + 严格 transport」能接管真实 sdk profile；结论写回本文 3.3 与代码注释。
4. A4 前置验证：构建经验包 tarball，装进 `$DEV_RUNTIME`，确认 `rsi-infer` 能 mount；不通则立即升级 R1。
5. 捕获第一个真实 fixture（`happy-tool` 的最小版本），与 B 确认 JSONL 形状与 manifest 字段。
6. 把 P1 的 DoD 写成可执行脚本，作为 W1 门证据。

## 附录 A 文件清单（A 轨）

```text
runtime/
├── packages/sdk-server/{src,tests,package.json,tsconfig.json}
├── packages/sdk-app/cordis.patch.yml
├── config/{home.patch.yml.template,mcp.servers.example.yml,README.md}
├── dev/{package.json,smoke.mjs,e2e.mjs,capture.mjs,mock-model.mjs}
└── version.inputs.json
examples/toy-mcp/{server.mjs,package.json,README.md}
native/tests/fixtures/            （共同；A 捕获、B 回放）
```

## 附录 B A 轨证据清单

- P1：`--dump-config` 快照、真实运行日志（stderr 无 `did not activate`）、方法/错误码断言输出。
- P2：15 类事件单测输出、fixture 列表与 manifest、增量合并用例。
- P3：两 preset 会话日志、RSI 三态事件、trace 文件与事件数。
- P4：E2E 计时、玩具 MCP 启动耗时/杀进程记录、重复行检查输出。
- P5：tarball 校验和、干净树安装日志、`VERSION.json` 输入清单、fixture 回放结果。
