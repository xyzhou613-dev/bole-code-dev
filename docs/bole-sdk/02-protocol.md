# 02 协议方法表（骨架）

状态：骨架，2026-10-22 修订。方法表、通知表、取消顺序、RSI 接缝已按本轮结论改定；错误码编号、字段默认值与兼容规则仍待补。负责人：待定。冻结后作为 C 库与自建插件 `@bole/sdk-server` 的接缝。

## 一、传输与分帧

stdout 归协议独占：补丁层把官方 `sdk-jsonrpc-server` 置为 `disabled`，由 `@bole/sdk-server` 创建并持有 transport；运行时内其他插件不得写 stdout，日志一律走 stderr。

已定：

- stdio、逐行 JSON-RPC、每行一个完整 JSON 对象、UTF-8。
- transport 的请求槽位只有一个，自建插件接管后自行分发；官方 `HarnessSdkJsonRpcServer.handleRequest` 只接收未命中的方法。
- 自建插件保留官方条目的 `inject: [sdkAppStartup, loader]`，并在 `initialize` 前 `await ctx.get('loader')?.await()`；`initialize` 就是就绪边界，握手期间不广告就绪。

待补：单行上限与超长行的处理（必须容纳内联图片的 base64，建议可配，默认不小于 64 MB；超限回 -32600 并断开）、stdout 被误写时的检测方式（建议启动时校验首行必须是 JSON-RPC 帧）、stderr 日志的落地规则。

## 二、方法表

请求名统一用斜杠形式（与官方 `session/prompt` 一致）；响应统一 `{ result }` 或 `{ error }`。

| 方法 | 方向 | 入参 | 出参 | 是否立即返回 | 落点 | 备注 |
| --- | --- | --- | --- | --- | --- | --- |
| `initialize` | 客户端 → 运行时 | `cwd`、`provider`、`model`、可选 `reasoningEffort`、可选 `maxTokens` | `{ serverInfo }` | 协议层会等 loader 就绪（实测 0.6–0.8 秒）；C 层 `bole_init` 不等待，就绪由 `RUNTIME_READY` 告知 | `HarnessSdkJsonRpcServer.initialize` | 官方已有；就绪边界 |
| `session/create` | 客户端 → 运行时 | `sessionId`、可选 `preset` | `{ sessionId, preset }` | 是；不触模型、不触工具 | `ctx.agents.create` + 发布前 `ctx.agentPresets.mount` | 新增；preset 缺省取当前默认（见 `rsi.set`）；id 已存在返回 4xxx，恢复走 `session/resume` |
| `session/resume` | 客户端 → 运行时 | `sessionId` | `{ sessionId, preset, cwd }` | 是（只做恢复与校验） | `ctx.agents.resume` | 新增；要求 `sessionPersistence` 与 `sessions`；preset 定义缺失拒绝 |
| `session/setPreset` | 客户端 → 运行时 | `sessionId`、`preset` | `{ sessionId, preset }` | 是 | `ctx.agentPresets.select(agent, preset)` | 新增；仅空白会话（尚未开始首个 turn），否则 `agent-preset/locked` |
| `session/prompt` | 客户端 → 运行时 | `sessionId`、`contentBlocks` | `{ messageId }` | 是（入队即返回） | `HarnessSdkJsonRpcServer.prompt` | 官方已有；未知会话按默认 preset 懒创建 |
| `session/cancel` | 客户端 → 运行时 | `sessionId` | `{}` | 是 | `ctx.agents.get(sessionId).cancel({ kind: 'user' }, { keepInbox: false })` | 新增；立即中断，结束事实由 `turn/end` 给出 |
| `session/trace` | 客户端 → 运行时 | `sessionId`、可选 `turn`、`path` | `{ path, eventCount }` | 是（写文件在插件线程完成，控制面不等） | 会话日志（内存 `session.eventAt` 或持久化 JSONL） | 新增；`turn` 省略导出整个会话 |
| `rsi.set` | 客户端 → 运行时 | `enabled`、可选 `sessionId` | `{ enabled, preset, scope }` | 是 | 无 `sessionId`：设置默认 preset；有：等价 `session/setPreset` | 新增；`scope` 为 `default` 或 `session` |
| `rsi.get` | 客户端 → 运行时 | 可选 `sessionId` | `{ enabled, preset, composed }` | 是 | 读插件默认 / 会话投影 | 新增；`composed=false` 表示运行时没有 preset 面 |
| `shutdown` | 客户端 → 运行时 | 无 | `{}` | 否 | `HarnessSdkJsonRpcServer.shutdown` | 官方已有，退出进程 |

字段与取值（新增方法）：

| 方法 | 字段 | 类型 | 必填 | 说明 |
| --- | --- | --- | --- | --- |
| `session/create` | `sessionId` | string | 是 | 与 agent id 同值；已存在返回 4xxx，不静默复用 |
| | `preset` | string | 否 | `standard` 或 `rsi-infer`（v1 承诺）；缺省取当前默认 |
| `session/resume` | `sessionId` | string | 是 | 必须已持久化且当前无活动 agent |
| `session/setPreset` | `preset` | string | 是 | 未定义返回 `agent-preset/not-found`；非空白返回 `agent-preset/locked` |
| `session/trace` | `turn` | number | 否 | 省略导出整个会话；0 与省略同义 |
| | `path` | string | 是 | 目标文件（JSONL，一行一个会话事件）；父目录必须存在 |
| `rsi.set` | `enabled` | boolean | 是 | true → `rsi-infer`，false → `standard` |
| | `sessionId` | string | 否 | 省略表示设置后续新任务的默认 |

「落点」是这条请求最终落到运行时的哪个服务或方法。落点为空的方法不能算冻结——接口文档只能约束请求的形状，造不出落点。

新增方法由自建插件先分发，未命中再交给官方 `HarnessSdkJsonRpcServer.handleRequest`；`session/cancel` 立即返回，结束事实由 `turn/end` 事件给出。

待补：`contentBlocks` 的完整块类型（沿用官方 `SdkPromptContentBlock`）、每个方法的完整错误码、时间戳与请求 id 的回显规则。

## 三、通知表

通知名统一用点形式（与官方 `session.event` 一致）。

| 通知 | 方向 | 载荷 | 说明 |
| --- | --- | --- | --- |
| `session.event` | 运行时 → 客户端 | `sessionId`、事件信封 | 官方已有，全量会话事件；SDK 的三层与阶段投影都从这里来 |
| `session.status` | 运行时 → 客户端 | `sessionId`、`idle` / `running` | 官方已有 |
| `subagent.started` / `subagent.finished` | 运行时 → 客户端 | 父子会话 id、状态 | 官方已有，按需透传 |
| `assistant.delta` | 运行时 → 客户端 | `sessionId`、`turn`、`step`、`channel`、`attemptId`、`revision`、`text` | 新增；`channel` 取 `text` 或 `reasoning`；50ms/4KB 合并，尽力而为；最终以 `assistant/message` 为准 |
| `rsi.state` | 运行时 → 客户端 | `scope`、可选 `sessionId`、`enabled`、`preset`、`source`、可选 `delivery` | 新增；见下表 |

`assistant.delta` 的 `turn` / `step` 取自同一 attempt 的 `start` 帧；`chunk` 帧本身只带 `attemptId` / `revision` / `index`，插件按 `attemptId` 关联。

`rsi.state` 字段：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `scope` | `default` / `session` | 默认值变化或某会话状态变化 |
| `sessionId` | string | `scope=session` 时必填 |
| `enabled` | boolean | `preset === 'rsi-infer'` |
| `preset` | string | 实际 preset id |
| `source` | `config` / `interface` / `session` / `receipt` | 本次状态的来源 |
| `delivery` | object | 仅 `source=receipt`：`status`（`delivered` / `available` / `unsupported-model`）、`bundleId`、`deliveredMemoryIds`、`provider`、`model`、`bodySha256` |

发出时点：

1. 运行时启动、SDK 应用配置后，发一次 `scope=default` 的初始状态。
2. `rsi.set` 成功后，发对应的 `scope`。
3. 会话创建或空白期切换 preset 成功时，发 `scope=session`。
4. 收到经验投递收据（会话事件里的 `user/message`，`source.kind = bole-experience`）时，发 `scope=session`、`source=receipt`。
5. 运行时没有组合 preset 面时，只发一次 `scope=default`，`composed` 语义由 `rsi.get` 表达，不伪造。

待补：通知与 `session.event` 的精确先后（当前约定：状态变更先记会话事件、再发通知；同一会话内顺序一致）、通知丢失后的补偿方式。

## 四、错误响应

待填：JSON-RPC 错误码与 SDK 错误码的映射表、可重试与不可重试的区分、`last_error` 的填充规则。

已定：

| 情形 | JSON-RPC error | SDK 归类 | 说明 |
| --- | --- | --- | --- |
| 未知方法 | `-32601` | 2xxx | 自建插件与官方服务器都不认识 |
| 参数非法 | `-32602` | 1xxx | preset 类型/取值、路径、id 为空等 |
| handler 抛错 | `-32603` | 按错误内容映射 | preset 未定义、锁定、无持久化等 |
| `agent-preset/not-found` | `-32603` + `data.code` | 1xxx | 未定义 preset；创建与恢复都可能出现 |
| `agent-preset/locked` | `-32603` + `data.code` | 4xxx | 会话已开始首个 turn，不能再切 preset |
| 未组合 preset 面 | 本地判定，不发请求 | 3xxx `RSI_UNAVAILABLE` | `ctx.agentPresets` 缺失；`rsi.state` 不改变 |
| 无活动可取消 | 本地判定 | 7xxx | 会话空闲或不存在活动任务 |
| 传输关闭 | 本地判定 | 2xxx | 管道 EOF、进程退出 |

已知一条：运行时未提供 preset 面时，`rsi.set` 返回 `RSI_UNAVAILABLE`（3xxx），此时不改 `rsi.state`，运行继续；preset 未定义与已锁定分别返回 `PRESET_NOT_FOUND`（1xxx）与 `PRESET_LOCKED`（4xxx），不归这一类。

## 五、顺序保证

已定：

1. **同一会话串行**：请求按到达顺序处理；跨会话并发，互不阻塞。
2. **任务内顺序**：`turn/start` →（`step/start` → `tool/call` → `tool/result` → `step/end`）* → `turn/end`；`request/header` 在对应 step 的模型请求前；`assistant/message` 在对应 step 内。
3. **增量**：`assistant.delta` 与 `session.event` 共享同一会话内的相对顺序，但不保证跨通道的严格交错；`TEXT_DELTA` / `REASONING_DELTA` 可以丢弃，结构事件不丢。
4. **取消**：`session/cancel` 返回后，已经在管道里的增量仍可能到达；该任务的最后一条事件是 `turn/end`（`aborted`）；此后再无该任务的 step / tool / delta 事件。未开始的用户消息丢弃。
5. **终态唯一**：每个任务恰好一个 `TASK_FINISHED` 或 `TASK_FAILED`，二选一；`aborted` 归 `TASK_FINISHED`，不是失败。
6. **运行时异常**：进程退出只发 `RUNTIME_EXITED`，不为未结束的任务补造终态事件；SDK 侧把该会话标成「运行时丢失」并允许恢复。
7. **恢复**：`session/resume` 不重放历史事件；需要历史就调 `session/trace`。

待补：跨会话并发上限、`session.event` 与投影事件在同一 tick 内的先后（当前实现是同一条读线程顺序分发）。

## 六、版本与兼容

待补：协议版本号位置、运行时版本不匹配时的行为、新增字段的向前兼容规则。

已定（待冻结细节）：

- `initialize` 结果保留官方 `serverInfo`；计划追加 `sdkProtocolVersion` 与 `extensions`，未知字段由调用方忽略。
- 运行时版本与 `VERSION.json` 不一致时，SDK 在 `RUNTIME_READY` 里报出两者，由调用方决定是否继续；默认继续并记日志。
- 新增方法/通知只增不改；已发布字段不改名、不改含义。

## 七、集成接缝：RSI 由谁负责

RSI 是模式，不是服务开关。接缝按层划分：

| 层 | 职责 |
| --- | --- |
| C 库 | 读 SDK 配置里的 `rsi.enabled`、调 `rsi.set` / `rsi.get`；把 `rsi.state` 交给调用方；不判断开关语义 |
| 桥接插件 `@bole/sdk-server` | 选择 preset（`agentPresets.select` / 创建时 `mount`）、投影 `rsi.state`；不 import、不挂载经验插件 |
| preset 声明（补丁层） | 声明 `standard` 与 `rsi-infer`（仅这两条）；`rsi-infer` 的那一行挂载经验插件 |
| 经验插件 `@deepseek-ai/dsh-skill-bole-experience` | 按准确模型 ID 注入只读经验，记投递收据；不写记忆库 |
| 预设面 registry | `agent-preset-registry` 管定义与默认值；preset 锁定与恢复由它负责 |

这条接缝是内部的，不进 C 头文件；对 IDE 可见的只有 `rsi.set`、`rsi.get` 与 `rsi.state`。任务书要求的「配置文件开关」由 SDK 配置文件承担（`rsi.enabled`，初始化后转成一次 `rsi.set`）；「SDK 接口开关」就是 `rsi.set`。两者都只影响新任务。

## 八、工具注入与工具隔离

任务书第五节的两条要求，SDK 只负责「明确方向、验证保证」，不实现工具本身。

### 工具注入方向

- **内置工具**：由 preset 声明挂载（`tool-fs`、`tool-pwsh`、`tool-todo`、`tool-skill` 等），调用方无需声明，SDK 也不代理。注意：采用 preset 面后，这些行不再由 base 全局挂载。
- **调用方工具**：一律走 MCP。每条一个 `@deepseek-ai/dsh-mcp-client` 条目，`transport` 取 `stdio` 或 `streamable-http`；挂载后它的工具与内置工具并列进入模型工具表，工具名形如 `mcp__<serverName>__<rawName>`。
- **v1 方向（已确认，10-22）**：调用方在运行时配置文件里声明 MCP server，重启生效；运行时注册/注销接口放二期。理由是 MCP 的 stdio 连接与工具发现是异步的，运行时增删要处理工具表变更与在途调用，超出「补事件」的补丁层范围。
- **落点**：`@deepseek-ai/dsh-mcp-client`（harness 包，已知），不是产品特性。

### 工具隔离

- **执行位置**：stdio MCP server 是运行时进程的子进程，streamable-http 是外部服务；内置 shell 工具经 `subprocess-local` 起子进程。
- **崩溃**：某个 MCP server 崩溃只影响该 server 的工具调用，结果为 `tool/result` 的 `isError`（带 `error.code`），运行时继续，不终止会话。`failOnStartupError` 默认 false，启动期连不上不会拖垮运行时。
- **超时**：每条 MCP 条目可配 `toolCallTimeoutMs`（每次工具调用/资源请求的毫秒上限）；内置工具的超时由工具自身与 `@deepseek-ai/dsh-tool-call-timeout-policy` 决定。SDK 控制面立即返回，调用方可用 `session/cancel` 中断。
- **审批**：headless 运行时没有审批应答者，`approval: ask` 会 fail-closed。审批只在 `tools/pre-execute` 返回 `ask` 时触发，SDK 组合不挂载会产生 `ask` 的组件（`hooks-claude-code`、`auto-review`），所以工具调用不会停在审批上；补丁层仍固定 `approval.policy: never` 作确定性兜底，并在接口说明里写明该取舍。
- **SDK 侧只做进程级隔离**：运行时与 IDE 分属不同进程；运行时退出时用 Job Object 回收整棵进程树，避免工具子进程残留。
- **待验证（M1）**：杀 MCP 子进程、工具超时、断网三种场景下的事件序列与错误码。
