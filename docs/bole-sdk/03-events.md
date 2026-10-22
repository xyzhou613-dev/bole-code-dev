# 03 事件模型（骨架）

状态：骨架，2026-10-22 修订。事件表、合并规则、顺序与 RSI 段已按本轮结论改定；每种事件的样例报文仍待补。负责人：待定。与 `02-protocol.md` 同批冻结。

已核实（2026-10-21，`0.1.7-alpha.2`）：会话事件表没有 `assistant/chunk`；token 增量来自瞬时帧 `agent/assistant-stream`（`start` / `chunk` / `end`，chunk 含 `text-delta`、`reasoning-delta`、`tool-call-delta`、`usage`、`finish`）。2026-10-22 在 `bole-code-dev` 源码上复核事件词汇表（60 项）、三层事件、thinking 帧、preset 事件与经验收据，见 `00-decisions.md` 第五节。本表据此修正。

## 一、事件信封

已定公共字段：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `event_id` | uint64 | SDK 内单调递增，用于排障与去重 |
| `type` | uint32 | 事件类型枚举 |
| `session_id` | UTF-8 string | 会话 id（agent id 同值）；运行时级事件为空 |
| `task` | uint32 | 内核 turn；非任务事件为 0 |
| `step` | uint32 | 内核 step；非步骤事件为 0 |
| `seq` | uint64 | 来源会话事件的 `seq`；瞬时帧与运行时事件为 0 |
| `ts_ms` | int64 | Unix 毫秒 |
| `payload_json` | UTF-8 string | 该事件的载荷，JSON 对象 |

C 侧暴露方式：`bole_event` 结构体带上述标量字段与 `const char* payload_json`；字符串与结构体内存归 SDK 所有，只在本次回调期间有效，调用方要保留必须自行拷贝。

待补：`payload_json` 的 schema 版本字段、时间戳单调性保证。

## 二、事件表

| SDK 事件 | harness 来源 | 关键字段 | 是否可合并 | 是否保证送达 |
| --- | --- | --- | --- | --- |
| `RUNTIME_READY` | 握手完成 | `serverInfo`、运行时版本、模型路由 | 否 | 是 |
| `RUNTIME_FAILED` | 启动失败 / 握手失败 | 错误码、文案 | 否 | 是 |
| `RUNTIME_EXITED` | 运行时进程退出 | 退出码、是否有未结束任务 | 否 | 是（宿主被杀除外） |
| `TASK_STARTED` | `turn/start` | task | 否 | 是 |
| `STEP_STARTED` | `step/start` | task、step | 否 | 是 |
| `STEP_FINISHED` | `step/end` | task、step | 否 | 是 |
| `PLAN_UPDATED` | `plan/mode`、`todo/write` | `kind=plan_mode` 时 `{ active }`；`kind=todos` 时待办列表 | 否 | 否，后值覆盖前值 |
| `MODEL_SELECTED` | `request/header` | provider、model、reasoningEffort、header 原因（initial / resume / change / series） | 否 | 是（每个 step 的请求前） |
| `RSI_STATE` | 会话头 `agentPreset`、`agent-preset/selected`、经验收据 | preset、来源、投递状态与 `deliveredMemoryIds` | 否 | 是（至少启动一次） |
| `TEXT_DELTA` | 瞬时帧 `text-delta` | 文本片段、attemptId、revision | 是 | 否，可丢弃旧值；不参与重放 |
| `REASONING_DELTA` | 瞬时帧 `reasoning-delta` | 思考片段、attemptId、revision | 是 | 否，可丢弃旧值；不参与重放 |
| `TOOL_CALL_STARTED` | `tool/call` | callId、工具名、原始参数 JSON | 否 | 是 |
| `TOOL_CALL_FINISHED` | `tool/result` | callId、`isError`、`error.code`、结果摘要 | 否 | 是 |
| `VERIFY_CHECK` | **暂缓**：本基线没有可投影来源 | — | 否 | 不适用：本运行时不产生、不承诺 |
| `TASK_FINISHED` | `turn/end` | `outcome`：completed / aborted / max_tokens / blocked / interrupted（`forked` 只出现在 fork 种子，不是实时终态） | 否 | 是（运行时崩溃时改由 `RUNTIME_EXITED` 表达） |
| `TASK_FAILED` | `turn/end` 的 `error`、运行时错误 | 错误码、消息、是否可重试 | 否 | 是（同上） |

三条要点：

1. **`PLAN_UPDATED` 不是「计划文本」**：`plan/mode` 的载荷只有 `{ active: boolean }`；计划正文在 `exit_plan_mode` 工具调用的参数里，随 `TOOL_CALL_STARTED` 到达。`todo/write` 才是待办列表。
2. **`MODEL_SELECTED` 只取 `request/header`**：`model/selection` 由 `api/session-controller` 写入，SDK 运行时不挂该包。模型在首个请求后与会话绑定，换模型要新建任务。
3. **没有 verify 事件**：见第五节。
4. **`RUNTIME_*` 是 SDK 侧的传输/进程事实**，不是 harness 阶段；除此之外的事件全部来自 harness 会话事件或瞬时帧，SDK 不发明阶段。

待补：每类事件的完整字段、被判为「可合并」的依据、结构事件永不被丢弃的兜底策略。

## 三、合并与限频

合并只作用于瞬时帧，不发生在会话事件层：帧自带 `attemptId` 与 `revision`（agent 生命周期内单调递增的帧序号），可据此丢弃过期片段。`turn` / `step` 只在同一 attempt 的 `start` 帧里，`chunk` 帧只带 `attemptId` / `revision` / `index`，按 `attemptId` 关联。

已定策略（v1）：

- **服务端合并**：桥接插件按「50 毫秒或 4 KB 先到者」把同一通道的增量合成一条 `assistant.delta`，按 `attemptId` 分组、按 `revision` 排序；`channel=text` 与 `channel=reasoning` 分别合并、分别计数。
- **客户端队列**：SDK 的读管道线程与回调解耦，队列有界；溢出时先丢最旧的 `TEXT_DELTA` / `REASONING_DELTA`，结构事件永不丢弃。
- **语义**：两种增量都是尽力而为的实时投影，不保证送达、不保证完整；最终正文与思考以 `assistant/message` 会话事件里对应的内容块为准，SDK 不因丢 delta 而伪造状态。
- **故障**：结构事件队列溢出按 2xxx 处理并断开，不静默丢。

待填：队列上限（条数/字节）、多会话并发时的公平性、回调持续慢于 1 秒时的降级行为。

## 四、顺序与生命周期

已定：

1. 同一会话内串行：`TASK_STARTED` →（`STEP_STARTED` → `MODEL_SELECTED`? → `TOOL_CALL_STARTED` → `TOOL_CALL_FINISHED`? → `STEP_FINISHED`）* → `TASK_FINISHED` / `TASK_FAILED`。
2. `TASK_STARTED` 一定早于该任务的第一条 `TEXT_DELTA` / `REASONING_DELTA`。
3. 每个任务恰好一个终态事件；`aborted`（取消）归 `TASK_FINISHED`，不是失败。
4. 取消后不再有该任务的 step / tool / delta 事件；取消前已在管道中的增量仍可能到达，调用方按 `task` 过滤。
5. `RSI_STATE` 与任务无关，可在任意时点出现；默认开关的初始状态一定在第一个 `TASK_STARTED` 之前发出。
6. 运行时进程异常退出：只发 `RUNTIME_EXITED`，不补造任务终态；SDK 把该会话标成「运行时丢失」，可用 `session/resume` 恢复。
7. `bole_release` 返回后不再投递新回调；已在飞行中的那一次允许完成。

待补：多会话事件在单条回调线程上的交错规则、恢复后的 seq 连续性。

## 五、RSI 与验证的联动

### RSI

RSI 是 agent preset 的选择，不是服务开关。事件流里的 RSI 事实来自三处真实来源：

| 来源 | 时点 | 事件 |
| --- | --- | --- |
| 会话头 `agentPreset` | 会话创建 | `RSI_STATE`（`scope=session`） |
| `agent-preset/selected` | 空白会话切换 preset | `RSI_STATE`（`scope=session`，`source=session`） |
| `user/message` 的 `source.kind = bole-experience` | 每次推理前的经验投递 | `RSI_STATE`（`source=receipt`，带 `delivery`） |

已确认：

- 默认关闭：registry 默认 `standard`，启动时发一次 `enabled=false` 的初始状态。
- 开 = 新任务用 `rsi-infer`，关 = 新任务用 `standard`；运行中的任务不变（内核 `agent-preset/locked`）。
- 投递状态三值：`delivered`（正文已进请求，`deliveredMemoryIds` 非空）、`available`（只有 Skill 目录与提示，正文未自动投递，靠显式 `skill` 调用）、`unsupported-model`（模型 ID 不在允许列表，无经验正文）。允许列表有两个模型：`qwen3.8-27b`（Qwen 服务经验 v3）与 `Ornith-1.5-35B-A3B`（Ornith V18），各自加载自己的包、按准确 ID 隔离。本交付确认的网关模型是 `qwen3.8-27b`，正常不会是 `unsupported-model`；`delivered` 只对明确「启动 / 部署 / 保留服务」的任务出现，普通任务为 `available`。
- `RSI_STATE` 无论开关都要发出；状态必须来自上述三处，SDK 不自行发明。
- 切换失败不改状态，也不发明知错误的事件；错误走 `rsi.set` 的响应。
- 标准模式不挂载经验插件，因此不应出现 `bole-experience` 收据。

### 验证

`VERIFY_CHECK` 暂缓（`00-decisions.md` 2.5）：本基线没有 verify 类会话事件，仓库内与本机完整宿主 `/root/bole-deploy` 里也都没有完成检查实现。事件模型保留该类型定义，但本运行时不产生、不承诺送达；等产品侧给出可观测信号（会话事件或 SDK 可达的服务方法）再冻结映射与字段。

对伙伴的说明：任务书第 5 条里的「验证」阶段目前不可观测；事件序列只呈现真实发生过的阶段，不为它伪造状态（验收第 5 条要求）。若现场必须展示验证阶段，需要产品侧先提供信号，不能由 SDK 侧补一个。

待填：切换开关时的事件顺序（`RSI_STATE` 与下一个 `TASK_STARTED` 的先后已定：前者先）、配置文件开关的初始状态事件、验收标准第 3、5 条的逐条对应表。

## 六、示例报文

待填：每类事件给出三条真实样例（正常、失败、取消），可直接作为契约测试的输入。样例必须来自真实运行（沙盒或现场），不允许手写一个「理想」的事件序列充当证据。
