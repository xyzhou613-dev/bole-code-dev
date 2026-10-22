# 04 C 接口（1.0 冻结）

状态：2026-10-22 接口冻结 1.0（A 起草；B 负责人在领取 B1 时确认可实现性，变更按 08 流程）。头文件 [`native/include/bole_agent.h`](../../native/include/bole_agent.h) 是唯一签名来源；本文写语义、错误码、线程模型与生命周期。**本文与头文件是接口契约，不包含实现**：C 实现、测试、示例、打包由 B 轨负责人决定与完成。当前仓库只保留头文件；`prototypes/native-c-sdk/` 是未授权参考，不属于交付物。冻结后只增不改：新增函数、结构体字段追加在末尾，已有字段不改名、不改含义。

## 一、头文件原则

已定：

- 纯 C99，可被 C++ 直接包含（`extern "C"`）；不导出 C++ 类、模板、异常或引用。
- 调用约定统一 `__cdecl`（`BOLE_CALL`）；导出宏 `BOLE_API` 处理 `__declspec(dllexport/dllimport)`；静态链接用 `BOLE_AGENT_STATIC`，构建 DLL 用 `BOLE_AGENT_BUILD`。
- 所有字符串 UTF-8（无 BOM），长度用 `size_t` / `uint32_t` 显式传；路径在 SDK 内部转 UTF-16 调 Win32，调用方只给 UTF-8。
- 错误通过返回值 + `bole_last_error` 表达，不用异常、不设全局 errno。
- 头文件自包含：只依赖 `<stdint.h>` / `<stddef.h>`。
- 事件里的字符串与结构体内存归 SDK 所有，只在本次回调期间有效；调用方要保留必须自行拷贝。

ABI 版本宏 `BOLE_ABI_VERSION = 0x00010000`（高 16 位主版本、低 16 位次版本）。

## 二、类型与字段

| 类型 | 用途 | 可见性约定 |
| --- | --- | --- |
| `bole_handle` | 运行时句柄，对应一个运行时进程 | 不透明指针 |
| `bole_session` | 会话句柄，对应一个 agent / session id | 不透明指针 |
| `bole_config` | 初始化配置，含 `size` 做向前兼容 | 值传递，调用方分配 |
| `bole_session_options` | 建会话时的 cwd / preset 覆盖 | 值传递，可传 NULL |
| `bole_event` | 事件信封 | 只在回调期间有效 |
| `bole_event_cb` | `void (*)(void* user_data, const bole_event* event)` | 回调期间有效 |
| `bole_rsi_state` | RSI 默认状态：`enabled`、`composed`、`preset[64]` | 值传递 |

### `bole_config`

| 字段 | 说明 |
| --- | --- |
| `size` | `sizeof(bole_config)`；SDK 按此判断调用方版本 |
| `runtime_dir` | 运行时目录（含 `node.exe` 与 dsh 依赖树），UTF-8 |
| `dsh_home` | 运行时独立 home，UTF-8；SDK 设为 `DSH_HOME` |
| `cwd` | 默认会话 cwd（绝对路径）；NULL 用调用方进程 cwd |
| `model_provider` / `model_name` | 传给 `initialize` 的路由，例如 `bole` / `qwen3.8-27b` |
| `reasoning_effort` | 可选；传给 `initialize` |
| `max_tokens` | 可选；0 用运行时默认 |
| `rsi_enabled` | 新会话默认是否开 RSI；默认 0 |
| `sandbox_mode` | `BOLE_SANDBOX_DEFAULT`（workspace-write）/ `WORKSPACE_WRITE` / `DANGER_FULL_ACCESS`；SDK 映射为运行时环境变量 |
| `max_line_bytes` | 单行 JSON-RPC 上限；0 用 64 MiB |
| `event_cb` | 初始事件回调，必填；为空返回 `BOLE_E_EVENT_CALLBACK_REQUIRED` |
| `user_data` | 原样回传给回调 |
| `log_path` | 可选文件日志，UTF-8 |

### `bole_session_options`

| 字段 | 说明 |
| --- | --- |
| `size` | `sizeof(bole_session_options)` |
| `cwd` | 绝对路径；NULL 用 `bole_config.cwd` |
| `preset` | `standard` 或 `rsi-infer`；NULL 用当前默认 |

### `bole_event`

| 字段 | 说明 |
| --- | --- |
| `size` | `sizeof(bole_event)` |
| `type` | `bole_event_type`；未知值必须忽略 |
| `event_id` | 句柄内单调递增，用于排障与去重 |
| `seq` | 来源会话事件的 `seq`；瞬时帧与运行时事件为 0 |
| `ts_ms` | Unix 毫秒；句柄内非递减 |
| `task` / `step` | 内核 turn / step；非对应层级为 0 |
| `session_id` | 会话 id（agent id 同值）；运行时事件为 `""` |
| `payload_json` | 事件载荷，JSON 对象，带 `"v": 1`；schema 见 `03-events.md` |

## 三、函数表

「阻塞」指最多等一次本地 JSON-RPC 往返；所有控制面函数都不等模型请求或工具执行。

| 函数 | 语义 | 阻塞 / 超时 | 回调中允许 | 主要错误 |
| --- | --- | --- | --- | --- |
| `bole_init(cfg, &handle)` | 校验配置、启动运行时、立即返回；就绪由 `RUNTIME_READY` 告知 | 进程启动，毫秒级 | 禁止 | 1001–1006、3001 |
| `bole_wait_ready(handle, timeout_ms)` | 只等握手完成，不等模型或工具 | 调用方超时 | 禁止 | 2004、3001、3002 |
| `bole_set_event_callback(handle, cb, user_data)` | 替换回调；已入队事件按新回调投递 | 否 | 允许 | 1002、1003 |
| `bole_release(handle)` | 释放调用方引用；后台回收进程树，不阻塞 | 否 | 禁止 | 1002 |
| `bole_session_create(handle, id, options, &session)` | 按 id 建会话，可覆盖 cwd / preset | 本地往返，5 秒 | 允许 | 1002、1004、2003、4002 |
| `bole_session_resume(handle, id, &session)` | 恢复已持久化会话；preset 按日志恢复 | 本地往返，10 秒 | 允许 | 1002、2003、3006、4001、4004 |
| `bole_session_set_preset(session, preset_id)` | 空白会话覆盖 preset；首个 turn 后锁定 | 本地往返，5 秒 | 允许 | 1002、1004、2003、4003 |
| `bole_submit(session, utf8_text)` | 入队一条文本消息；turn 异步执行 | 本地入队往返，5 秒 | 允许 | 1002、2003、3002 |
| `bole_submit_blocks(session, blocks_json)` | 入队文本 / 图片 content blocks；图片进附件存储 | 本地入队往返，5 秒 | 允许 | 1002、2003、3002 |
| `bole_cancel(session)` | 立即中断当前 turn；无活动返回 7xxx | 本地往返，5 秒 | 允许 | 1002、2003、7001 |
| `bole_rsi_set(handle, enabled)` | 设置后续新会话的默认 preset | 本地往返，5 秒 | 允许 | 1002、2003、3004 |
| `bole_rsi_get(handle, &state)` | 读 SDK 投影的默认开关与是否组合 preset 面 | 否 | 允许 | 1002 |
| `bole_trace_export(handle, id, turn, path, &count)` | 按 turn 导出原始会话日志 JSONL；`turn=0` 全会话 | 等文件写完，30 秒 | 禁止 | 1002、2003、4001 |
| `bole_last_error(handle, &code, buf, len)` | 取本句柄最近一次错误（码 + 文案） | 否 | 允许 | 1002 |

回调签名与内存约定：`void (*bole_event_cb)(void* user_data, const bole_event* event)`，`user_data` 原样回传；`bole_event` 及其 `payload_json`、`session_id` 只在本次回调期间有效，调用方要保留必须自行拷贝。重复注册替换旧回调；已入队的旧事件按新回调投递。

设计说明：`session/create`、`submit`、`cancel`、`set_preset`、`rsi_set` 同步等一次**本地**应答，是为了把「会话已存在」「preset 已锁定」「无活动可取消」「preset 面未组合」这类错误直接返回给调用方；它们不等待模型或工具，正常耗时毫秒级。`init` / `release` 不等待，`wait_ready` 只等握手。

## 四、错误码表

数值码是 ABI；协议 `error.data.code` 是稳定字符串。C 库把字符串映射为数值码；未知字符串按区间归入通用码（1xxx→1006、2xxx→2002、3xxx→3007、4xxx→4001、5xxx→5005、6xxx→6004、7xxx→7002）。

| 数值 | 常量 | 协议 `data.code` | 可重试 | 说明 |
| --- | --- | --- | --- | --- |
| 1001 | `BOLE_E_RUNTIME_DIR_NOT_FOUND` | —（本地） | 否 | `runtime_dir` 不存在或没有 `node.exe` |
| 1002 | `BOLE_E_INVALID_CONFIG` | `INVALID_ARGUMENT` | 否 | 配置或参数非法 |
| 1003 | `BOLE_E_EVENT_CALLBACK_REQUIRED` | —（本地） | 否 | `event_cb` 为空 |
| 1004 | `BOLE_E_PRESET_NOT_FOUND` | `PRESET_NOT_FOUND` | 否 | preset 未定义 |
| 1005 | `BOLE_E_CONFIG_PARSE_FAILED` | —（本地） | 否 | 配置文件解析失败 |
| 1006 | `BOLE_E_INVALID_ARGUMENT` | `INVALID_ARGUMENT` | 否 | 单个参数非法 |
| 2001 | `BOLE_E_PIPE_FAILED` | —（本地） | 是 | 管道读写失败 |
| 2002 | `BOLE_E_PROTOCOL_PARSE_FAILED` | `-32700` / `-32601` | 视情况 | JSON 解析失败或未知方法 |
| 2003 | `BOLE_E_REQUEST_TIMEOUT` | —（本地） | 是 | 本地 RPC 超时 |
| 2004 | `BOLE_E_WAIT_READY_TIMEOUT` | —（本地） | 是 | `wait_ready` 超时 |
| 2005 | `BOLE_E_LINE_TOO_LONG` | `LINE_TOO_LONG` | 否 | 单行超过上限，随后断开 |
| 2006 | `BOLE_E_STDOUT_VIOLATION` | —（本地） | 否 | stdout 首帧不是 JSON-RPC |
| 2007 | `BOLE_E_EVENT_QUEUE_OVERFLOW` | —（本地） | 否 | 结构事件队列溢出，断开 |
| 2008 | `BOLE_E_PROTOCOL_VERSION_MISMATCH` | —（本地） | 否 | `sdkProtocolVersion` 主版本不兼容 |
| 3001 | `BOLE_E_RUNTIME_START_FAILED` | —（本地） | 视情况 | 进程启动失败 |
| 3002 | `BOLE_E_RUNTIME_EXITED` | —（本地/事件） | 否 | 运行时已退出；可新开句柄后 `resume` |
| 3003 | `BOLE_E_RUNTIME_VERSION_MISMATCH` | — | 否 | `VERSION.json` 与实际版本不一致（默认继续并告警） |
| 3004 | `BOLE_E_RSI_UNAVAILABLE` | `RSI_UNAVAILABLE` | 否 | 运行时未组合 preset 面 |
| 3005 | `BOLE_E_SANDBOX_UNAVAILABLE` | `SANDBOX_UNAVAILABLE` | 否 | 文件沙箱后端不可用 |
| 3006 | `BOLE_E_PERSISTENCE_UNAVAILABLE` | `PERSISTENCE_UNAVAILABLE` | 否 | 没有会话持久化，不能 resume |
| 3007 | `BOLE_E_RUNTIME_REQUEST_FAILED` | `-32603`（无 data.code） | 视情况 | 运行时 handler 抛错且无可映射码 |
| 4001 | `BOLE_E_SESSION_NOT_FOUND` | `SESSION_NOT_FOUND` | 否 | 会话不存在 |
| 4002 | `BOLE_E_SESSION_EXISTS` | `SESSION_EXISTS` | 否 | 同一 id 重复创建 |
| 4003 | `BOLE_E_PRESET_LOCKED` | `PRESET_LOCKED` | 否 | 首个 turn 已开始，不能切 preset |
| 4004 | `BOLE_E_SESSION_NOT_RESUMABLE` | `SESSION_NOT_RESUMABLE` | 否 | 日志损坏或没有可恢复状态 |
| 5001 | `BOLE_E_MODEL_AUTH_FAILED` | `MODEL_AUTH_FAILED` | 否 | 鉴权失败 |
| 5002 | `BOLE_E_MODEL_RATE_LIMITED` | `MODEL_RATE_LIMITED` | 是 | 限流 |
| 5003 | `BOLE_E_MODEL_CONTEXT_EXCEEDED` | `MODEL_CONTEXT_EXCEEDED` | 否 | 上下文超限 |
| 5004 | `BOLE_E_MODEL_NOT_FOUND` | `MODEL_NOT_FOUND` | 否 | 模型不存在 |
| 5005 | `BOLE_E_MODEL_REQUEST_FAILED` | `MODEL_REQUEST_FAILED` | 是 | 其它模型请求失败 |
| 6001 | `BOLE_E_TOOL_NOT_FOUND` | `TOOL_NOT_FOUND` | 否 | 工具不存在 |
| 6002 | `BOLE_E_TOOL_TIMEOUT` | `TOOL_TIMEOUT` | 是 | 工具超时 |
| 6003 | `BOLE_E_TOOL_CRASHED` | `TOOL_CRASHED` | 是 | 工具子进程崩溃 |
| 6004 | `BOLE_E_TOOL_FAILED` | `TOOL_FAILED` | 是 | 其它工具失败 |
| 7001 | `BOLE_E_NO_ACTIVE_TASK` | `NO_ACTIVE_TASK` | 否 | 没有活动 turn 可取消 |
| 7002 | `BOLE_E_CANCEL_FAILED` | `CANCEL_FAILED` | 是 | 取消请求本身失败 |

## 五、线程模型

数据面形态已定：**事件回调**，不用队列拉取。

1. 回调只在 SDK 自有的单条事件线程上触发，同一句柄串行、FIFO；不同句柄各有线程，互不阻塞。
2. 回调中允许调用 `submit`、`submit_blocks`、`cancel`、`rsi_set`、`rsi_get`、`session_set_preset`、`session_create`、`session_resume`、`last_error` 与 `set_event_callback`；禁止调用 `release`、`bole_init`、`wait_ready`、`trace_export`。
3. 回调必须快速返回（毫秒级）。SDK 内部是「独立读管道线程 + 有界队列 + 事件线程」，回调变慢只会让队列积压，不会阻塞运行时或控制面。
4. 积压时先丢最旧的 `TEXT_DELTA` / `REASONING_DELTA`，结构事件永不丢弃；结构事件队列溢出按故障处理（报 2007 并断开），不静默丢。
5. `bole_release` 立即返回；返回后不再投递新回调，已在飞行中的那一次允许完成，调用方必须容忍。所有 `bole_session` 句柄在 `release` 返回后失效，不能再传回 SDK。
6. 控制面函数在参数校验失败时同步返回错误码；其余最多等一次本地 RPC 应答，不等模型或工具。
7. 回调不得让异常/长跳转穿过 C 边界；跨语言绑定自行捕获。

取消语义（写死，与 `00-decisions.md` 2.6 一致）：

- `bole_cancel` 立即中断内核当前 turn：模型请求中断、已开始的工具调用中断、未开始的工具调用补一条 `aborted before dispatch` 的 `tool/result`、排队未开始的用户消息丢弃。
- 终态是 `TASK_FINISHED(outcome=aborted)`，不是 `TASK_FAILED`。
- 没有活动任务时返回 7001，不当作成功。
- 不支持「步末终止」。

## 六、生命周期与内存

已定：

- 回调在 `bole_init` 的配置里给出，因此不存在「init 返回后、注册回调前」的丢事件窗口；`bole_set_event_callback` 只用于替换。
- `bole_init` 返回后、`RUNTIME_READY` 之前，控制面调用会在 SDK 侧等到就绪或调用超时；`RUNTIME_FAILED` 之后除 `release` 外的调用返回失败码。
- 一个 `bole_handle` 对应一个运行时进程；一个进程可承载多个 `bole_session`。
- `bole_session_create` 与 `bole_session_resume` 返回的句柄归调用方持有；`bole_release` 会一并失效。v1 不提供单会话关闭；会话随句柄释放。
- 句柄级错误串：`bole_last_error` 返回本句柄最近一次错误（码 + 文案），任意线程可读，由 SDK 内部加锁；不是线程局部。
- 释放顺序：先 `release` 句柄，再释放调用方自己的资源；SDK 用 Job Object 回收整棵进程树。
- 回调期间释放会话句柄或句柄是未定义行为（禁止）。
- 运行时崩溃：只发 `RUNTIME_EXITED`，不补造任务终态；该句柄作废，调用方新开句柄后可用 `session/resume` 恢复。

## 七、版本与兼容

已定：

- 动态库 `bole_agent.dll`、导入库 `bole_agent.lib`、头文件 `bole_agent.h`。
- `bole_config`、`bole_session_options`、`bole_event`、`bole_rsi_state` 都带 `size` 字段；SDK 只读调用方声明的长度内的字段，新增字段追加在末尾。
- 字符串一律 UTF-8；SDK 内部转 UTF-16 调 Win32，路径分隔符差异由 SDK 处理。
- 运行时版本从 `RUNTIME_READY` 的 `payload_json` 与本地 `VERSION.json` 读出；不一致时默认继续并记日志（`3003` 只作为诊断，不作为失败）。
- `initialize` 结果带 `sdkProtocolVersion`；C 库支持 1。缺失按 0 处理并继续；主版本更高时继续但记告警，未知字段忽略。

## 八、M1 待验证

- 杀 MCP 子进程、工具超时、断网三种场景下的事件序列与错误码。
- Windows ACL 沙箱后端不可用时的启动自检与 `SANDBOX_UNAVAILABLE` 路径。
- `release` 在还有未完成 `submit` 时的回收行为。
- Job Object 在运行时异常退出时对工具子进程树的回收。
