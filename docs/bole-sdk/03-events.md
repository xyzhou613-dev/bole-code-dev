# 03 事件模型（骨架）

状态：待填。负责人：待定。与 `02-protocol.md` 同批冻结。

已核实（2026-10-21，`0.1.7-alpha.2`）：会话事件表没有 `assistant/chunk`；token 增量来自瞬时帧 `agent/assistant-stream`（`start` / `chunk` / `end`，chunk 含 `text-delta`、`tool-call-delta`、`usage`、`finish`）。本表据此修正。

## 一、事件信封

待填：公共字段（事件 id、会话 id、turn、step、序号、时间戳、类型、载荷），以及 C 侧如何暴露（结构体 + 字符串载荷）。

## 二、事件表

| SDK 事件 | harness 来源 | 关键字段 | 是否可合并 | 是否保证送达 |
| --- | --- | --- | --- | --- |
| `TASK_STARTED` | `turn/start` | turn | 否 | 是 |
| `PLAN_UPDATED` | `plan/mode`、`todo/write` | 计划步骤 | 否 | 否，可能被后续覆盖 |
| `MODEL_SELECTED` | `model/selection`、`request/header` | provider、模型 | 否 | 是 |
| `TEXT_DELTA` | 瞬时帧 `agent/assistant-stream` 的 `text-delta` | 文本片段、attempt id、revision | 是 | 否，可丢弃旧值；不参与重放 |
| `TOOL_CALL_STARTED` | `tool/call` | 调用 id、工具名、参数 | 否 | 是 |
| `TOOL_CALL_FINISHED` | `tool/result` | 调用 id、成功与否、结果摘要 | 否 | 是 |
| `VERIFY_CHECK` | 经验记忆完成检查注入的消息 | 触发原因 | 否 | 是，仅 RSI 开启时出现；若完成检查只在线下，本运行时不出现（待定稿时确认） |
| `RSI_STATE` | `rsi.state` 通知 | enabled、source | 否 | 是 |
| `TASK_FINISHED` | `turn/end` | 结束原因 | 否 | 是 |
| `TASK_FAILED` | `turn/end`、运行时错误 | 错误码、消息 | 否 | 是 |

待补：每类事件的完整字段、被判为「可合并」的依据、结构事件永不被丢弃的兜底策略。

## 三、合并与限频

合并只作用于瞬时帧，不发生在会话事件层：帧自带 attempt id 与 revision，可据此丢弃过期片段。

已定策略（v1）：

- **服务端合并**：桥接插件按「50 毫秒或 4 KB 先到者」把 `text-delta` 合成一条 `assistant.delta`，同一 attempt 内 revision 递增。
- **客户端队列**：SDK 的读管道线程与回调解耦，队列有界；溢出时丢最旧的 `TEXT_DELTA`，结构事件永不丢弃。
- **语义**：`TEXT_DELTA` 是尽力而为的实时投影，不保证送达、不保证完整；最终文本以 `assistant/message` 会话事件为准，SDK 不因丢 delta 而伪造状态。
- **故障**：结构事件队列溢出按 2xxx 处理并断开，不静默丢。

待填：队列上限（条数/字节）、多会话并发时的公平性、回调持续慢于 1 秒时的降级行为。

## 四、顺序与生命周期

待填：同一轮次内事件的顺序、`TASK_STARTED` 与首个 `TEXT_DELTA` 的先后、取消后的事件尾巴、结束时是否一定有一个终态事件。

## 五、RSI 与验证的联动

已确认：运行时经验记忆只有注入动作，RSI 关闭 = 停止注入，`VERIFY_CHECK` 不出现。`RSI_STATE` 无论开关都要发出，但状态必须来自运行时——有开关入口时按切换结果投影，没有入口时只在启动按配置文件报一次；切换失败不改状态，也不发明知错误的事件。

待填：切换开关时的事件顺序（`RSI_STATE` 与下一个 `TASK_STARTED` 的先后）、配置文件开关的初始状态事件、验收标准第 3、5 条的对应说明。

## 六、示例报文

待填：每种事件给出三条真实样例（正常、失败、取消），可直接作为契约测试的输入。
