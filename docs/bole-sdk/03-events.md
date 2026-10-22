# 03 事件模型（1.0 冻结）

状态：2026-10-22 冻结 1.0。事件枚举、信封、payload schema、来源映射、合并与顺序已定；与 `02-protocol.md`、`04-c-abi.md` 同批。负责人：A 轨（投影）/ B 轨（转发）。真实运行样例 fixtures 待捕获，不阻塞接口冻结。

事件由桥接插件投影成 SDK 事件，通过 `bole.event` 通知投递；C 库只解析信封、排队、回调，不再解析 harness 事件词汇。`assistant.delta` 与 `rsi.state` 是同一套语义的专用通知，C 库等价接受。

## 一、事件信封

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `event_id` | uint64 | C 库入队时分配，句柄内单调递增；`bole.event` 里的值仅用于排障 |
| `type` | uint32 | 事件类型枚举（见第二节）；未知值必须忽略 |
| `session_id` | UTF-8 string | 会话 id（agent id 同值）；运行时级事件为 `""` |
| `task` | uint32 | 内核 turn；非任务事件为 0 |
| `step` | uint32 | 内核 step；非步骤事件为 0 |
| `seq` | uint64 | 来源会话事件的 `seq`（每会话从 0 连续）；瞬时帧与运行时事件为 0 |
| `ts_ms` | int64 | 来源事件的 `time` 或瞬时帧的 `time`；运行时事件取本地 Unix 毫秒；句柄内非递减 |
| `payload_json` | UTF-8 string | 该事件的载荷，JSON 对象，必带 `"v": 1` |

C 侧暴露方式：`bole_event` 结构体带上述标量字段与 `const char* payload_json`；字符串与结构体内存归 SDK 所有，只在本次回调期间有效，调用方要保留必须自行拷贝。

`payload_json` 的规则：

- 每个对象带 `"v": 1`；新增字段只增不改，未知字段忽略。
- 字符串 UTF-8；数字按 JSON；时间戳为 Unix 毫秒。
- 载荷不内联图片字节；需要附件时给引用 id。
- 大字段按第五节的截断规则，截断时带 `"truncated": true`；全文进 `session/trace`。

## 二、事件表

| 类型值 | SDK 事件 | 来源 | 关键字段 | 可合并 | 保证送达 |
| --- | --- | --- | --- | --- | --- |
| 1 | `RUNTIME_READY` | C 库握手完成 | serverInfo、capabilities、model | 否 | 是 |
| 2 | `RUNTIME_FAILED` | C 库启动/握手/传输失败 | code、message、phase | 否 | 是 |
| 3 | `RUNTIME_EXITED` | C 库进程退出 | exitCode、hadActiveTasks | 否 | 是（宿主被杀除外） |
| 10 | `TASK_STARTED` | `turn/start` | turn | 否 | 是 |
| 11 | `STEP_STARTED` | `step/start` | turn、step | 否 | 是 |
| 12 | `STEP_FINISHED` | `step/end` | turn、step | 否 | 是 |
| 13 | `PLAN_UPDATED` | `plan/mode`、`todo/write` | kind、active 或 todos | 否，后值覆盖前值 | 否 |
| 14 | `MODEL_SELECTED` | `request/header` | provider、model、reasoningEffort、reason | 否 | 是（每个请求前） |
| 15 | `RSI_STATE` | 会话头、`agent-preset/selected`、经验收据 | preset、source、delivery | 否 | 是（至少启动一次） |
| 20 | `TEXT_DELTA` | 瞬时帧 `text-delta` | attemptId、revision、text | 是 | 否，可丢弃 |
| 21 | `REASONING_DELTA` | 瞬时帧 `reasoning-delta` | attemptId、revision、text | 是 | 否，可丢弃 |
| 22 | `ASSISTANT_MESSAGE` | `assistant/message` | text、reasoning、usage、interrupted | 否 | 是（最终正文权威） |
| 30 | `TOOL_CALL_STARTED` | `tool/call` | callId、tool、args | 否 | 是 |
| 31 | `TOOL_CALL_FINISHED` | `tool/result` | callId、isError、error、resultSummary | 否 | 是 |
| 40 | `VERIFY_CHECK` | **暂缓**：本基线没有可投影来源 | — | 否 | 不适用：本运行时不产生 |
| 50 | `TASK_FINISHED` | `turn/end` 的非 error 终态 | turn、outcome、reason | 否 | 是（崩溃时由 `RUNTIME_EXITED` 表达） |
| 51 | `TASK_FAILED` | `turn/end` 的 `error` | code、message、status、retryable | 否 | 是（同上） |

`RUNTIME_*` 是 SDK 侧的传输/进程事实，不是 harness 阶段；除此之外的事件全部来自 harness 会话事件或瞬时帧，SDK 不发明阶段。插件不得投递 1–3；这三类由 C 库产生。

## 三、各事件载荷

### `RUNTIME_READY`（1）

```json
{"v":1,
 "serverInfo":{"name":"deepseek-harness-sdk-runtime","version":"0.0.1"},
 "sdkProtocolVersion":1,
 "capabilities":{"preset":true,"sandbox":"workspace-write","persistence":true,"trace":true},
 "model":{"provider":"bole","model":"qwen3.8-27b"}}
```

| 字段 | 说明 |
| --- | --- |
| `serverInfo` | 官方 `initialize` 结果的 `serverInfo` 原样 |
| `sdkProtocolVersion` | 整数；缺失按 0 |
| `capabilities.preset` | 运行时是否组合了 `agentPresets` |
| `capabilities.sandbox` | `workspace-write` / `danger-full-access` / `unavailable` |
| `capabilities.persistence` | 是否可 `session/resume` |
| `capabilities.trace` | 是否可 `session/trace` |
| `model` | 传给 `initialize` 的 provider / model |

### `RUNTIME_FAILED`（2）

```json
{"v":1,"code":3002,"message":"runtime process exited","phase":"exit"}
```

`phase` 取 `spawn` / `handshake` / `transport` / `exit`。`code` 是 `04-c-abi.md` 的数值码。握手失败后 `wait_ready` 返回同一码；`RUNTIME_FAILED` 之后除 `release` 外的控制面调用失败。

### `RUNTIME_EXITED`（3）

```json
{"v":1,"exitCode":0,"hadActiveTasks":false}
```

只表示进程退出；不为未结束的 turn 补造终态。`hadActiveTasks` v1 恒为 `false`，字段保留。

### `TASK_STARTED`（10）

```json
{"v":1,"turn":1}
```

信封的 `task` 与 `turn` 同值；`step` 为 0。

### `STEP_STARTED`（11）/ `STEP_FINISHED`（12）

```json
{"v":1,"turn":1,"step":1}
```

信封的 `task` / `step` 同值。`step/end` 在失败时也会记录（`finally`），所以即使 step 失败也应有 `STEP_FINISHED`；turn 在 pre-step 被拒绝时可能没有 step 事件。

### `PLAN_UPDATED`（13）

两种载荷，用 `kind` 区分：

```json
{"v":1,"kind":"plan_mode","active":true}
```

```json
{"v":1,"kind":"todos","todos":[
  {"content":"Read the spec","status":"completed"},
  {"content":"Implement parser","status":"in_progress"}
]}
```

- `plan_mode` 的载荷只有 `{active: boolean}`；**计划正文不在这个事件里**，它在 `exit_plan_mode` 工具调用的参数里，随 `TOOL_CALL_STARTED` 到达。
- `todos` 是整表替换（last-write-wins）；`content` 非空且列表内唯一，`status` 取 `pending` / `in_progress` / `completed`。
- 两种都是整值替换，不保证每个中间状态都送达。

### `MODEL_SELECTED`（14）

```json
{"v":1,"provider":"bole","model":"qwen3.8-27b","reasoningEffort":"high","maxTokens":8192,"reason":"initial"}
```

| 字段 | 说明 |
| --- | --- |
| `provider` / `model` | 来自 `request/header.header.config` |
| `reasoningEffort` | 可选；同上 |
| `maxTokens` | 可选；同上 |
| `reason` | `initial` / `resume` / `change` / `series` |

`reason` 语义：`initial` 首个 header；`resume` 恢复后的首个请求；`change` header 真的变了；`series` header 未变但显式开始新消息序列。模型在首个请求后与会话绑定，换模型要新建任务（不在本事件里伪造切换）。

### `RSI_STATE`（15）

```json
{"v":1,"scope":"session","sessionId":"s-001","enabled":true,
 "preset":"rsi-infer","composed":true,"source":"session"}
```

| 字段 | 说明 |
| --- | --- |
| `scope` | `default` / `session` |
| `sessionId` | `scope=session` 时必填 |
| `enabled` | `preset === "rsi-infer"` |
| `preset` | 实际 preset id；未组合时为空串 |
| `composed` | 运行时是否组合了 preset 面 |
| `source` | `config` / `interface` / `session` / `receipt` |
| `delivery` | 仅 `source=receipt`：`status`（`delivered` / `available` / `unsupported-model`）、`bundleId`、`deliveredMemoryIds`、`provider`、`model`、`bodySha256` |

发出时点：

1. 运行时启动、插件读取默认配置后，发一次 `scope=default`；未组合 preset 面时 `composed=false`、`preset=""`、`enabled=false`。
2. `rsi.set` 成功后，发对应的 `scope`（`source=interface`）。
3. 会话创建（会话头 `agentPreset`）或空白期 `agent-preset/selected` 时，发 `scope=session`、`source=session`。
4. 收到经验投递收据（`user/message` 的 `source.kind = "bole-experience"`）时，发 `scope=session`、`source=receipt`。
5. 标准模式不挂载经验插件，不应出现 `bole-experience` 收据；`RSI_STATE` 本身仍要发。

### `TEXT_DELTA`（20）/ `REASONING_DELTA`（21）

```json
{"v":1,"channel":"text","attemptId":"s-001:1","revision":3,"text":"Hel"}
```

```json
{"v":1,"channel":"reasoning","attemptId":"s-001:1","revision":4,"text":"thinking"}
```

- `attemptId` / `revision` 来自 `agent/assistant-stream` 的同一 attempt；`revision` 在同一 Agent 生命周期内单调，替换后从 1 重开。
- 信封的 `task` / `step` 取同一 attempt 的 `start` 帧。
- 插件按 `attemptId` 分组、按 `revision` 排序；同一通道 50ms 或 4KB 先到者合并；`revision` 更旧的片段丢弃。
- 两种增量都是尽力而为，不保证送达或完整；最终以 `ASSISTANT_MESSAGE` 为准。

### `ASSISTANT_MESSAGE`（22）

```json
{"v":1,"turn":1,"step":1,"messageId":"0b1d2c33-...",
 "text":"Reading it.","reasoning":"need the file first",
 "usage":{"inputTokens":10,"outputTokens":5,"totalTokens":15},
 "interrupted":true}
```

- 由 `assistant/message` 投影：`text` 是按顺序拼接的 `text` 内容块；`reasoning` 是拼接的 `reasoning` 内容块；两者都可为空串。
- `messageId` 取 `message.id`；`usage` 取 `data.usage`，字段按 `TokenUsage` 原样；`interrupted` 仅在 `data.interrupted === true` 时出现。
- 这是最终正文/思考的**权威**事件；调用方应以此替换已收到的增量文本。工具调用不在这里重复，走 `TOOL_CALL_STARTED/FINISHED`。
- 一个 step 可能没有 `ASSISTANT_MESSAGE`（例如失败 attempt 只写 `assistant/attempt`）；此时以 `TASK_FAILED` / 增量为准，不伪造正文。
- `text` / `reasoning` 各设 256 KB 上限；超限截断并带 `"truncated": true`，全文进 trace。

### `TOOL_CALL_STARTED`（30）

```json
{"v":1,"callId":"call_01","tool":"read","args":{"file_path":"README.md"}}
```

- `callId` 取 `tool/call.callId`；`tool` 取 `name`；`args` 是 `arguments` 原始字符串解析后的 JSON 值。
- 若 `arguments` 不是合法 JSON，`args` 为原始字符串并带 `"argsRaw": true`。
- 序列化后的 `args` 超过 64 KB 时省略 `args` 并带 `"truncated": true`；全文进 trace。
- 工具名可能是 `mcp__<server>__<tool>`；SDK 不改名。

### `TOOL_CALL_FINISHED`（31）

```json
{"v":1,"callId":"call_01","tool":"read","isError":false,
 "resultSummary":"# README\n","truncated":false}
```

```json
{"v":1,"callId":"call_02","tool":"bash","isError":true,
 "error":{"name":"AbortError","code":"ABORTED_BEFORE_DISPATCH"},
 "resultSummary":"","truncated":false}
```

- `callId` 取 `message.toolCallId`；`tool` 由插件按 `callId` 关联 `TOOL_CALL_STARTED`，关联不到时省略。
- `isError` 取 `message.isError`；`error` 取 `tool/result.error` 的 `name` / `code` / 可选 `reason`。
- `resultSummary` 是 `message.content` 中文本块按顺序拼接后的前 8 KB；`truncated` 表示是否截断。图片/文件块只给占位（`[image]` / `[file]`），不内联字节。
- 工具崩溃、超时、`aborted before dispatch` 都走这一条，不升级为 `TASK_FAILED`。

### `TASK_FINISHED`（50）

```json
{"v":1,"turn":1,"outcome":"completed","reason":{"kind":"completed"}}
```

`outcome` 由 `turn/end.reason.kind` 映射：

| reason.kind | outcome |
| --- | --- |
| `completed` | `completed` |
| `aborted` | `aborted` |
| `max-tokens` | `max_tokens` |
| `blocked` | `blocked` |
| `interrupted` | `interrupted`（resume/冷读补写，不是实时） |
| `forked` | `forked`（只出现在 fork 种子，不是实时终态） |

`reason` 原样保留，便于排障；`aborted` 的 `reason.reason.kind` 是 `user` / `parent` / `hook` / `disposed` / `legacy`。

### `TASK_FAILED`（51）

```json
{"v":1,"turn":1,"code":"SERVER","message":"upstream 500","status":500,
 "providerRetryAfterMs":1000,"requestId":"req_abc","retryable":true}
```

- 只来自 `turn/end` 的 `reason.kind === "error"`；`reason.error` 是 `LlmFailure`。
- `code` / `message` 必填；`status`、`providerRetryAfterMs`、`requestId` 可选。
- `retryable`：429、5xx、网络类错误为 true；其余为 false（插件按下表判断，缺失时 false）。
- `TASK_FAILED` 之后不再有该 turn 的事件；`TASK_FINISHED` 与它二选一。

### `VERIFY_CHECK`（40）

保留定义，本运行时不产生、不承诺送达。原因见 `00-decisions.md` 2.5：本基线没有 verify 类会话事件，仓库与完整宿主都没有完成检查实现。若产品侧以后给出可观测信号，再冻结映射与字段；SDK 不伪造。

## 四、来源映射（插件实现）

| SDK 事件 | harness 来源 | 投影规则 |
| --- | --- | --- |
| `TASK_STARTED` | `turn/start` | `turn`；`seq` / `ts_ms` 取来源 |
| `STEP_STARTED` / `STEP_FINISHED` | `step/start` / `step/end` | `turn`、`step` |
| `PLAN_UPDATED` | `plan/mode` | `kind=plan_mode`、`active` |
| `PLAN_UPDATED` | `todo/write` | `kind=todos`、`todos` 原样 |
| `MODEL_SELECTED` | `request/header` | `header.config.provider/model/reasoningEffort/maxTokens`、`reason` |
| `RSI_STATE` | 会话头 `agentPreset`、`agent-preset/selected`、`user/message` 收据 | 见第三节；收据字段取 `source` |
| `TEXT_DELTA` / `REASONING_DELTA` | `agent/assistant-stream` 的 `chunk` 帧 | `chunk.type` 为 `text-delta` / `reasoning-delta`；`text` 字段（不是 `delta`）；`attemptId` / `revision` |
| `ASSISTANT_MESSAGE` | `assistant/message` | `message.content` 文本/思考块、`message.id`、`usage`、`interrupted` |
| `TOOL_CALL_STARTED` | `tool/call` | `callId`、`name`、`arguments`（字符串，插件解析） |
| `TOOL_CALL_FINISHED` | `tool/result` | `message.toolCallId`、`message.isError`、`message.content` 文本、`error` |
| `TASK_FINISHED` / `TASK_FAILED` | `turn/end` | `reason.kind` / `reason.error` |
| `RUNTIME_*` | C 库 | 不由插件投递 |
| — | `session.event` | 官方全量事件，C 库不用它做投影；保留排障 |
| — | `session.status` | 官方通知；C 库内部跟踪 |
| — | `subagent.started` / `finished` | v1 不透传 |

注意：`SessionEvent` 信封没有 `id` 字段；`seq` 是每会话从 0 连续的序号；`user/message` 的 `data` 直接是 `UserMessage`（没有 `message` 包装）；`tool/call.arguments` 是模型产出的原始 JSON 字符串，未解析。

## 五、合并、限频与背压

- **插件侧合并**：同一 `attemptId`、同一 `channel` 的增量按「50 毫秒或 4 KB 先到者」合并，按 `revision` 排序；`revision` 更旧的片段丢弃。`text` 与 `reasoning` 分别计数。
- **C 侧队列**：读管道线程与回调解耦，队列有界（4096 条 / 16 MB）；溢出时先丢最旧的 `TEXT_DELTA` / `REASONING_DELTA`，结构事件永不丢弃。
- **结构事件溢出**：按故障处理（`BOLE_E_EVENT_QUEUE_OVERFLOW`，2007），断开传输，不静默丢。
- **截断**：`TOOL_CALL_STARTED.args` 64 KB、`TOOL_CALL_FINISHED.resultSummary` 8 KB、`ASSISTANT_MESSAGE.text/reasoning` 各 256 KB；超限带 `"truncated": true`，全文进 `session/trace`。
- **语义**：两种增量都是实时投影，不保证送达、不保证完整；最终正文与思考以 `ASSISTANT_MESSAGE` 为准。SDK 不因丢 delta 而伪造状态。
- **慢回调**：回调持续慢于毫秒级会让队列积压；积压只影响增量，结构事件优先保留。

## 六、顺序与生命周期

1. 同一会话内串行：`TASK_STARTED` →（`STEP_STARTED` → `MODEL_SELECTED`? → 增量* → `ASSISTANT_MESSAGE`? → `TOOL_CALL_STARTED` → `TOOL_CALL_FINISHED`? → `STEP_FINISHED`）* → `TASK_FINISHED` / `TASK_FAILED`。
2. `TASK_STARTED` 一定早于该任务的第一条 `TEXT_DELTA` / `REASONING_DELTA`；`MODEL_SELECTED` 在该 step 的第一个增量之前。
3. `ASSISTANT_MESSAGE` 在对应 `STEP_FINISHED` 之前；工具调用事件可能在它之后、`STEP_FINISHED` 之前。
4. 每个 turn 恰好一个终态事件；`aborted` 归 `TASK_FINISHED`，不是失败。`TASK_FAILED` 只用于 `reason.kind === "error"`。
5. 取消后不再有该 turn 的 step / tool / delta 事件；取消前已在管道中的增量仍可能到达，调用方按 `task` 过滤。
6. `RSI_STATE` 与任务无关，可在任意时点出现；默认开关的初始状态一定在第一个 `TASK_STARTED` 之前发出。
7. 运行时进程异常退出：只发 `RUNTIME_EXITED`，不补造任务终态；该句柄作废，新句柄可用 `session/resume` 恢复。
8. `bole_release` 返回后不再投递新回调；已在飞行中的那一次允许完成。
9. 恢复后 `seq` 从持久化日志继续，不清零；`session/resume` 不重放历史事件。

## 七、验收与事件对应

| 验收条款 | 事件/字段 |
| --- | --- |
| 第 1 条：全流程实时可见 | `TASK_STARTED` / `STEP_*` / `TEXT_DELTA` / `TOOL_CALL_*` / `TASK_FINISHED` |
| 第 2 条：取消生效 | `session/cancel` 返回 0 后，`TASK_FINISHED(outcome=aborted)` 收尾 |
| 第 3 条：RSI 状态可观测 | `RSI_STATE`（scope / enabled / preset / source / delivery） |
| 第 4 条：按文档独立集成 | 事件与 ABI 文档、fixtures 一致 |
| 第 5 条：不伪造阶段 | 来源映射表；`VERIFY_CHECK` 明确暂缓，不产生 |

## 八、样例与 fixtures

- 真实样例从 `0.1.7-alpha.2` 运行捕获，放在 `native/tests/fixtures/`：一行一个 wire 通知（`bole.event` / `assistant.delta` / `rsi.state`），可直接喂给假运行时或契约测试。
- 每个事件至少覆盖：正常、失败、取消三种路径；工具事件另加 `isError` 与截断。
- 样例必须来自真实运行；不得手写「理想」序列冒充证据。捕获脚本与沙盒一起入库（`06-dev-environment.md`）。
- 契约测试用 fixtures 回放；单元测试用构造的最小 JSON 覆盖边界。
