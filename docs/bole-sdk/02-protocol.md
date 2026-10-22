# 02 协议方法表（骨架）

状态：待填。负责人：待定。冻结后作为 C 库与自建插件 `@bole/sdk-server` 的接缝。

## 一、传输与分帧

stdout 归协议独占：补丁层把官方 `sdk-jsonrpc-server` 置为 `disabled`，由 `@bole/sdk-server` 创建并持有 transport；运行时内其他插件不得写 stdout，日志一律走 stderr。

待填：stdio、逐行 JSON-RPC、UTF-8、单行上限、超长行的处理方式、stdout 被误写时的检测方式。

## 二、方法表

| 方法 | 方向 | 入参 | 出参 | 是否立即返回 | 落点 | 备注 |
| --- | --- | --- | --- | --- | --- | --- |
| `initialize` | 客户端 → 运行时 | 工作目录、provider、模型、可选推理强度 | 服务标识与版本 | 是 | `HarnessSdkJsonRpcServer.initialize` | 官方已有 |
| `session/prompt` | 客户端 → 运行时 | 会话 id、内容块 | 消息 id | 是 | `HarnessSdkJsonRpcServer.prompt` | 官方已有，未知会话懒创建 |
| `session/cancel` | 客户端 → 运行时 | 会话 id | 空 | 是 | `ctx.agents.get(sessionId).cancel()` | 新增，映射内核 `Agent.cancel` |
| `rsi.set` | 客户端 → 运行时 | 开关布尔值 | 切换后的状态 | 是 | **待定：产品侧开关入口** | 新增，转发给入口；无入口返回 `RSI_UNAVAILABLE` |
| `rsi.get` | 客户端 → 运行时 | 无 | 当前状态与来源 | 是 | **待定：同上** | 新增 |
| `trace.export` | 客户端 → 运行时 | 会话 id、目标路径 | 导出结果 | 是 | 会话 JSONL 日志（插件自实现） | 新增，二期可降级为导出日志 |
| `shutdown` | 客户端 → 运行时 | 无 | 空 | 否 | `HarnessSdkJsonRpcServer.shutdown` | 官方已有，退出进程 |

「落点」是这条请求最终落到运行时的哪个服务或方法。落点为空的方法不能算冻结——接口文档只能约束请求的形状，造不出落点。

新增方法由自建插件先分发，未命中再交给官方 `HarnessSdkJsonRpcServer.handleRequest`；`session/cancel` 立即返回，结束事实由 `turn/end` 事件给出。

待补：每个方法的完整字段表、必填与可选项、取值范围、错误码。

## 三、通知表

| 通知 | 方向 | 载荷 | 说明 |
| --- | --- | --- | --- |
| `session.event` | 运行时 → 客户端 | 会话 id、事件信封 | 官方已有，全量会话事件 |
| `session.status` | 运行时 → 客户端 | 会话 id、idle/running | 官方已有 |
| `assistant.delta` | 运行时 → 客户端 | 会话 id、序号、文本片段 | 新增，尽力而为；50ms/4KB 合并，最终以 `assistant/message` 为准 |
| `rsi.state` | 运行时 → 客户端 | enabled、source | 新增，投影运行时状态；无来源时只在启动按配置报一次 |
| `subagent.started` / `subagent.finished` | 运行时 → 客户端 | 父子会话 id、状态 | 官方已有，按需透传 |

待补：每种通知在什么时点发出、与 `session.event` 的先后关系。

## 四、错误响应

待填：JSON-RPC 错误码与 SDK 错误码的映射表、可重试与不可重试的区分、`last_error` 的填充规则。

已知一条：运行时未提供 RSI 开关入口、或切换失败时，`rsi.set` 返回 `RSI_UNAVAILABLE`，建议归 3xxx（运行时/能力不可用），编号待定；此时不改 `rsi.state`，运行继续。

## 五、顺序保证

待填：同一会话内事件的相对顺序、跨会话的并发语义、取消后是否还会收到事件、结束时是否保证收到终止事件。

## 六、版本与兼容

待填：协议版本号位置、运行时版本不匹配时的行为、新增字段的向前兼容规则。

## 七、集成接缝：RSI 由谁负责

| 层 | 职责 |
| --- | --- |
| C 库 | 只把 `rsi.set` 编成请求、把 `rsi.state` 交给调用方；不判断也不实现开关语义 |
| 桥接插件 `@bole/sdk-server` | 把 `rsi.set` 转发给运行时提供的开关入口，把结果投影成 `rsi.state`；无入口返回 `RSI_UNAVAILABLE`；不 import、不挂载产品插件 |
| 记忆插件（bole-agent 侧） | 由产品 profile 挂载；定义「关闭」意味着什么，并（或由产品侧）提供开关入口 |
| profile 补丁层 | 负责挂载产品插件；不承担开关逻辑 |

这条接缝是内部的，不进 C 头文件；对 IDE 可见的只有 `rsi.set`、`rsi.get` 与 `rsi.state`。入口的具体形式（服务方法 / 事件 / settings）待与负责人确认。

## 八、工具注入与工具隔离

任务书第五节的两条要求，SDK 只负责「明确方向、验证保证」，不实现工具本身。

### 工具注入方向

- **内置工具**：由 profile 挂载（`tool-fs`、`tool-bash`、`tool-todo` 等），调用方无需声明，SDK 也不代理。
- **调用方工具**：一律走 MCP。每条一个 `@deepseek-ai/dsh-mcp-client` 条目，`transport` 取 `stdio` 或 `streamable-http`；挂载后它的工具与内置工具并列进入模型工具表。
- **v1 方向（建议，待确认）**：调用方在运行时配置文件里声明 MCP server，重启生效；运行时注册/注销接口放二期。理由是 MCP 的 stdio 连接与工具发现是异步的，运行时增删要处理工具表变更与在途调用，超出「补事件」的补丁层范围。
- **落点**：`@deepseek-ai/dsh-mcp-client`（harness 包，已知），不是产品特性。

### 工具隔离

- **执行位置**：stdio MCP server 是运行时进程的子进程，streamable-http 是外部服务；内置 shell 工具经 `subprocess-local` 起子进程。
- **崩溃**：某个 MCP server 崩溃只影响该 server 的工具调用，结果为 `tool/result` 的 `isError`，运行时继续，不终止会话。
- **超时**：由工具调用自身的超时/预算处理；SDK 控制面立即返回，调用方可用 `session/cancel` 中断，宿主 UI 不被阻塞。
- **SDK 侧只做进程级隔离**：运行时与 IDE 分属不同进程；运行时退出时用 Job Object 回收整棵进程树，避免工具子进程残留。
- **待验证（M1）**：杀 MCP 子进程、工具超时、断网三种场景下的事件序列与错误码。
