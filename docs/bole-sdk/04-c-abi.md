# 04 C 接口（骨架）

状态：骨架，2026-10-22 修订。头文件原则、类型清单、函数表、线程模型与生命周期已按本轮结论改定；错误码编号、结构体字段细节与版本规则仍待补。负责人：待定。冻结后 C 库与示例按本文实现。

## 一、头文件原则

已定：

- 纯 C：头文件按 C99 写，同时可被 C++ 直接包含（`extern "C"`）；不导出 C++ 类、模板、异常或引用。
- 调用约定统一：所有导出函数用 `__cdecl`（Windows x64 只有一种约定，宏保留是为了 32 位兼容）；导出宏 `BOLE_API` 处理 `__declspec(dllexport/dllimport)`。
- 所有字符串 UTF-8（无 BOM），长度用 `size_t` / `uint32_t` 显式传；路径在 SDK 内部转 UTF-16 调 Win32，调用方只给 UTF-8。
- 错误通过返回值 + `bole_last_error` 表达，不用异常、不设全局 errno。
- 头文件自包含：只依赖 `<stdint.h>` / `<stddef.h>`。
- 事件里的字符串与结构体内存归 SDK 所有，只在本次回调期间有效；调用方要保留必须自行拷贝。

待补：`BOLE_ABI_VERSION` 宏名与取值、头文件里是否暴露 `bole_event_type` 的完整枚举值（决定新增事件是否算 ABI 变更）。

## 二、类型定义

已定的类型清单（字段细节待补）：

| 类型 | 用途 | 可见性约定 |
| --- | --- | --- |
| `bole_handle` | 运行时句柄，对应一个运行时进程 | 不透明指针 |
| `bole_session` | 会话句柄，对应一个 agent / session id | 不透明指针 |
| `bole_config` | 初始化配置，含 `size` 做向前兼容 | 值传递，调用方分配 |
| `bole_event` | 事件信封（`00-decisions.md` / `03-events.md` 的公共字段） | 只在回调期间有效 |
| `bole_event_cb` | `void (*)(void* user_data, const bole_event* event)` | 回调期间有效 |
| `bole_rsi_state` | RSI 状态：`enabled`、`preset`、`composed` | 值传递 |

`bole_config` 至少包含：

| 字段 | 说明 |
| --- | --- |
| `size` | `sizeof(bole_config)`；SDK 按此判断调用方版本 |
| `runtime_dir` | 运行时目录（含 `node.exe` 与 dsh 依赖树），UTF-8 |
| `dsh_home` | 运行时独立 home，UTF-8；不读用户家目录 |
| `model_provider` / `model_name` | 传给 `initialize` 的路由，例如 `bole` / `qwen3.8-27b`（目标模型，命中经验允许列表） |
| `rsi_enabled` | 新任务默认是否开 RSI；默认 0 |
| `event_cb` | 初始事件回调，必填；为空则 `bole_init` 返回 1xxx |
| `user_data` | 原样回传给回调 |
| `log_path` | 可选文件日志，UTF-8 |

## 三、函数表

| 函数 | 语义 | 线程约束 |
| --- | --- | --- |
| `bole_init(cfg, &handle)` | 校验配置、启动运行时、立即返回；就绪由 `RUNTIME_READY` 告知 | 任意线程；回调中禁止 |
| `bole_wait_ready(handle, timeout_ms)` | 只等握手完成，不等模型或工具；超时返回 2xxx | 任意线程；回调中禁止（会阻塞事件线程） |
| `bole_set_event_callback(handle, cb, user_data)` | 替换回调；初始回调来自 `cfg` | 任意线程 |
| `bole_release(handle)` | 释放调用方持有的一次引用；内部引用归零后后台回收进程，不阻塞 | 任意线程；回调中禁止 |
| `bole_session_create(handle, id, &session)` | 按当前默认 preset 创建会话；立即返回 | 任意线程 |
| `bole_session_set_preset(session, preset_id)` | 空白会话覆盖 preset（`standard` / `rsi-infer`）；首个 turn 开始后返回 4xxx | 任意线程；回调中允许 |
| `bole_session_resume(handle, id, &session)` | 恢复已持久化会话；preset 按日志恢复 | 任意线程 |
| `bole_submit(session, utf8_text)` | 入队一条用户消息；立即返回 | 任意线程；回调中允许 |
| `bole_cancel(session)` | 立即中断当前任务；无活动返回 7xxx | 任意线程；回调中允许 |
| `bole_rsi_set(handle, enabled)` | 设置后续新任务的默认 RSI；立即返回，运行中的任务不变 | 任意线程；回调中允许 |
| `bole_rsi_get(handle, &state)` | 读默认开关、preset 与运行时是否组合了 preset 面 | 任意线程 |
| `bole_trace_export(handle, session_id, turn, path)` | 导出轨迹；`turn = 0` 表示整个会话 | 任意线程；回调中允许（后台写文件） |
| `bole_last_error(handle, buf, len)` | 取本句柄最近一次错误文案 | 同句柄任意线程 |

回调签名与内存约定：`void (*bole_event_cb)(void* user_data, const bole_event* event)`，`user_data` 原样回传；`bole_event` 及其 `payload_json`、`session_id` 只在本次回调期间有效，调用方要保留必须自行拷贝。重复注册替换旧回调，已入队的旧事件按新回调投递。

待补：每个函数的完整签名、返回值、参数校验规则、空指针与重复调用的行为；`bole_last_error` 是否返回结构化错误码而不只是文案。

## 四、错误码表

| 区间 | 类别 | 已定义码 |
| --- | --- | --- |
| 1xxx | 初始化与配置 | 运行时路径不存在、配置非法、preset 未定义、`event_cb` 为空 |
| 2xxx | 传输与协议 | 管道失败、解析失败、请求超时、`wait_ready` 超时 |
| 3xxx | 运行时 | 进程退出、版本不匹配、`RSI_UNAVAILABLE`（未组合 preset 面） |
| 4xxx | 会话 | 会话不存在、重复创建、preset 已锁定（首个任务后） |
| 5xxx | 模型 | 鉴权失败、限流、上下文超限 |
| 6xxx | 工具 | 工具不存在、超时、崩溃 |
| 7xxx | 取消 | 无活动可取消 |

`RSI_UNAVAILABLE` 的语义（10-22 修正）：运行时没有组合 preset 面（`ctx.agentPresets` 缺失）时返回，归 3xxx；此时不改 `rsi.state`，运行继续。preset 未定义（`PRESET_NOT_FOUND`，1xxx）与已锁定（`PRESET_LOCKED`，4xxx）是另外两个码，不归这一类。它不再表示「记忆插件缺失或挂载失败」——RSI 已由 preset 承载。

待补：每个码的中文文案、英文常量名、是否可重试、对应协议错误。preset 相关错误建议固定为 `PRESET_NOT_FOUND`（1xxx）与 `PRESET_LOCKED`（4xxx），编号待补。

## 五、线程模型

数据面形态已定：**事件回调**，不用队列拉取。

1. 回调只在 SDK 自有的单条事件线程上触发，同一句柄串行、FIFO；不同句柄各有线程，互不阻塞。
2. 回调中允许调用 `submit`、`cancel`、`rsi_set`、`session_set_preset`、`trace_export`；禁止调用 `release`、`bole_init`、`bole_wait_ready`。调用方若要更新 UI，自行切回 UI 线程。
3. 回调必须快速返回（毫秒级）。SDK 内部是「独立读管道线程 + 有界队列 + 事件线程」，回调变慢只会让队列积压，不会阻塞运行时或控制面。
4. 积压时先丢最旧的 `TEXT_DELTA` / `REASONING_DELTA`，结构事件永不丢弃；结构事件队列溢出按故障处理（报 2xxx 并断开），不静默丢。
5. `bole_release` 立即返回；返回后不再投递新回调，已在飞行中的那一次允许完成，调用方必须容忍。所有 `bole_session` 句柄在 `release` 返回后失效，不能再传回 SDK。
6. 控制面函数在参数校验失败时可以同步返回错误码；除此之外一律立即返回，不等模型或工具。
7. 回调不得让异常/长跳转穿过 C 边界；跨语言绑定自行捕获。

取消语义（写死，与 `00-decisions.md` 2.6 一致）：

- `bole_cancel` 立即返回；abort 内核当前任务：模型请求中断、已开始的工具调用中断、未开始的工具调用补一条 `aborted before dispatch` 的 `tool/result`、排队未开始的用户消息丢弃。
- 终态是 `TASK_FINISHED(outcome=aborted)`，不是 `TASK_FAILED`。
- 没有活动任务时返回 7xxx，不当作成功。
- 不支持「步末终止」。

待补：控制面函数的阻塞上限、多句柄并发时的线程数上限、进程退出时回调的收尾顺序。

## 六、生命周期与内存

已定：

- 回调在 `bole_init` 的配置里给出，因此不存在「init 返回后、注册回调前」的丢事件窗口；`bole_set_event_callback` 只用于替换。
- `bole_init` 返回后、`RUNTIME_READY` 之前，控制面调用允许入队（运行时尚未就绪时请求在 SDK 侧排队）；`RUNTIME_FAILED` 之后除 `release` 外的调用返回 3xxx。
- 一个 `bole_handle` 对应一个运行时进程；一个进程可承载多个 `bole_session`。
- `bole_session_create` 与 `bole_session_resume` 返回的句柄归调用方持有；`bole_release` 会一并失效。
- 句柄级错误串：`bole_last_error` 返回本句柄最近一次错误，任意线程可读，由 SDK 内部加锁；不是线程局部。
- 释放顺序：先 `release` 句柄，再释放调用方自己的资源；SDK 用 Job Object 回收整棵进程树。
- 回调期间释放会话句柄是未定义行为（禁止）。

待补：引用计数规则（`release` 在还有未完成 `submit` 时的行为）、关闭顺序的细节、运行时崩溃后句柄的重建规则。

## 七、版本与兼容

待补：结构体 `size` 字段做向前兼容的规则、ABI 版本号、动态库文件名规则、导入库名称。

已定：

- 动态库 `bole_agent.dll`、导入库 `bole_agent.lib`、头文件 `bole_agent.h`。
- `bole_config`、`bole_event`、`bole_rsi_state` 都带 `size` 字段；SDK 只读调用方声明的长度内的字段，新增字段追加在末尾。
- 字符串一律 UTF-8；SDK 内部转 UTF-16 调 Win32，路径分隔符差异由 SDK 处理。
- 运行时版本从 `RUNTIME_READY` 的 `payload_json` 读出，与 `VERSION.json` 不一致时默认继续并记日志。
