# 03 事件模型（骨架）

状态：待填。负责人：待定。与 `02-protocol.md` 同批冻结。

## 一、事件信封

待填：公共字段（事件 id、会话 id、turn、step、序号、时间戳、类型、载荷），以及 C 侧如何暴露（结构体 + 字符串载荷）。

## 二、事件表

| SDK 事件 | harness 来源 | 关键字段 | 是否可合并 | 是否保证送达 |
| --- | --- | --- | --- | --- |
| `TASK_STARTED` | `turn/start` | turn | 否 | 是 |
| `PLAN_UPDATED` | `plan/mode`、`todo/write` | 计划步骤 | 否 | 否，可能被后续覆盖 |
| `MODEL_SELECTED` | `model/selection`、`request/header` | provider、模型 | 否 | 是 |
| `TEXT_DELTA` | `assistant/chunk` | 文本片段、序号 | 是 | 否，可丢弃旧值 |
| `TOOL_CALL_STARTED` | `tool/call` | 调用 id、工具名、参数 | 否 | 是 |
| `TOOL_CALL_FINISHED` | `tool/result` | 调用 id、成功与否、结果摘要 | 否 | 是 |
| `VERIFY_CHECK` | 经验记忆完成检查注入的消息 | 触发原因 | 否 | 是，仅 RSI 开启时出现 |
| `RSI_STATE` | `rsi.state` 通知 | enabled、source | 否 | 是 |
| `TASK_FINISHED` | `turn/end` | 结束原因 | 否 | 是 |
| `TASK_FAILED` | `turn/end`、运行时错误 | 错误码、消息 | 否 | 是 |

待补：每类事件的完整字段、被判为「可合并」的依据、结构事件永不被丢弃的兜底策略。

## 三、合并与限频

待填：`TEXT_DELTA` 的合并窗口（建议 50 毫秒或 4 KB 先到者触发）、队列上限、溢出时丢弃顺序、回调阻塞时的行为。

## 四、顺序与生命周期

待填：同一轮次内事件的顺序、`TASK_STARTED` 与首个 `TEXT_DELTA` 的先后、取消后的事件尾巴、结束时是否一定有一个终态事件。

## 五、RSI 与验证的联动

待填：RSI 关闭时 `VERIFY_CHECK` 不出现，`RSI_STATE` 仍要发出；切换开关时事件顺序；文档与验收标准第 3、5 条的对应说明。

## 六、示例报文

待填：每种事件给出三条真实样例（正常、失败、取消），可直接作为契约测试的输入。
