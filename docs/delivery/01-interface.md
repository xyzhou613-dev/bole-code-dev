# 伯乐智能体 SDK 接口说明

版本：1.0  
适用平台：Windows 10 / 11 x64  
语言：C99；可从 C++ 直接包含  
头文件：`bole_agent.h`  

本文描述 SDK 对调用方暴露的边界、类型、函数、事件和线程模型。文中“运行时”指随 SDK 一起交付、由 SDK 启动和回收的独立进程；调用方不需要直接启动或管理它。

## 1. 概览与边界

SDK 是一层薄 C 封装：

- 一个 `bole_handle` 对应一个运行时进程；
- 一个进程可承载多个 `bole_session`；
- 控制面函数只等一次本地 JSON-RPC 往返，不等模型请求或工具执行；
- 模型输出、工具调用、任务状态通过事件回调异步送达；
- SDK 负责启动运行时、握手、进程树回收、协议分帧、事件排队和错误码映射。

调用方负责：

- 准备 `runtime_dir`、可写的 `dsh_home` 和项目工作目录；
- 提供事件回调，并在自己的线程模型里把事件切到 UI 线程；
- 在退出前调用 `bole_release` 释放句柄。

不在本文范围：模型网关地址、工具的具体实现、IDE 侧插件壳、离线包的安装步骤（见《离线部署》）。

## 2. 快速索引

### 2.1 函数

| 函数 | 用途 |
| --- | --- |
| `bole_init` | 校验配置、启动运行时、立即返回 |
| `bole_wait_ready` | 等握手完成 |
| `bole_set_event_callback` | 替换事件回调 |
| `bole_release` | 释放句柄，后台回收运行时 |
| `bole_session_create` | 按 id 创建会话 |
| `bole_session_resume` | 恢复已持久化会话 |
| `bole_session_set_preset` | 空白会话切换模式 |
| `bole_submit` | 入队一条文本消息 |
| `bole_submit_blocks` | 入队文本/图片内容块 |
| `bole_cancel` | 立即中断当前任务 |
| `bole_rsi_set` | 设置新会话默认模式 |
| `bole_rsi_get` | 读取默认模式投影 |
| `bole_trace_export` | 导出原始会话日志 |
| `bole_last_error` | 读取本句柄最近一次错误 |

### 2.2 事件

16 类可投递事件：

- 运行时：`RUNTIME_READY`(1)、`RUNTIME_FAILED`(2)、`RUNTIME_EXITED`(3)
- 任务/步骤：`TASK_STARTED`(10)、`STEP_STARTED`(11)、`STEP_FINISHED`(12)
- 计划/模型/模式：`PLAN_UPDATED`(13)、`MODEL_SELECTED`(14)、`RSI_STATE`(15)
- 增量/正文：`TEXT_DELTA`(20)、`REASONING_DELTA`(21)、`ASSISTANT_MESSAGE`(22)
- 工具：`TOOL_CALL_STARTED`(30)、`TOOL_CALL_FINISHED`(31)
- 终态：`TASK_FINISHED`(50)、`TASK_FAILED`(51)

`VERIFY_CHECK`(40) 为保留值，本运行时不产生、不承诺送达。

### 2.3 错误码

成功返回 `0`；失败返回正整数错误码。数值码是 ABI，完整表见《错误码》。常用：

| 数值 | 常量 | 含义 |
| --- | --- | --- |
| 1001 | `BOLE_E_RUNTIME_DIR_NOT_FOUND` | 运行时目录或入口缺失 |
| 1002 | `BOLE_E_INVALID_CONFIG` | 配置或参数非法 |
| 1003 | `BOLE_E_EVENT_CALLBACK_REQUIRED` | 未提供事件回调 |
| 2003 | `BOLE_E_REQUEST_TIMEOUT` | 本地请求超时 |
| 2004 | `BOLE_E_WAIT_READY_TIMEOUT` | 等待就绪超时 |
| 3002 | `BOLE_E_RUNTIME_EXITED` | 运行时已退出 |
| 4001 | `BOLE_E_SESSION_NOT_FOUND` | 会话不存在 |
| 4002 | `BOLE_E_SESSION_EXISTS` | 会话 id 已存在 |
| 4003 | `BOLE_E_PRESET_LOCKED` | 首个任务后不能切换模式 |
| 7001 | `BOLE_E_NO_ACTIVE_TASK` | 没有活动任务可取消 |

## 3. 初始化与配置

### 3.1 字符串与路径

- 所有字符串都是 UTF-8、无 BOM、NUL 结尾。
- 路径由 SDK 内部转换为 Windows UTF-16；调用方只传 UTF-8。
- `cwd`、会话 `cwd` 使用绝对路径；`NULL` 时 `bole_config.cwd` 缺省为调用方进程当前目录。
- `runtime_dir`、`dsh_home`、`log_path` 建议传绝对路径；相对路径按调用方进程当前目录解析。

### 3.2 `bole_config`

调用方分配并清零，首字段 `size = sizeof(bole_config)`。

| 字段 | 必填 | 说明 |
| --- | --- | --- |
| `size` | 是 | `sizeof(bole_config)`；SDK 按此判断调用方版本 |
| `runtime_dir` | 是 | 含 `node.exe`、运行时依赖树和补丁配置的目录 |
| `dsh_home` | 是 | 可写运行目录；不存在时 SDK 创建；用于会话、日志、缓存 |
| `cwd` | 否 | 默认会话工作目录（绝对路径）；`NULL` 用调用方进程 cwd |
| `model_provider` | 是 | 模型路由名，例如 `bole` |
| `model_name` | 是 | 模型 id，例如 `qwen3.8-27b` |
| `reasoning_effort` | 否 | 推理强度；`NULL` 用运行时默认 |
| `max_tokens` | 否 | 输出 token 上限；`0` 用运行时默认 |
| `rsi_enabled` | 否 | 新会话默认是否开 RSI：`0` standard，`1` rsi-infer |
| `sandbox_mode` | 否 | `BOLE_SANDBOX_DEFAULT`（workspace-write）、`BOLE_SANDBOX_WORKSPACE_WRITE`、`BOLE_SANDBOX_DANGER_FULL_ACCESS` |
| `max_line_bytes` | 否 | 单行 JSON-RPC 上限；`0` 表示 64 MiB |
| `event_cb` | 是 | 初始事件回调；为空返回 `1003` |
| `user_data` | 否 | 原样回传给回调 |
| `log_path` | 否 | 可选日志文件；`NULL` 丢弃 stderr 日志 |

字段按 `size` 向前兼容：SDK 只读取调用方声明长度内的字段，新增字段追加在末尾，已有字段不改名、不改含义。

### 3.3 `bole_session_options`

| 字段 | 必填 | 说明 |
| --- | --- | --- |
| `size` | 是 | `sizeof(bole_session_options)` |
| `cwd` | 否 | 会话工作目录（绝对路径）；`NULL` 用 `bole_config.cwd` |
| `preset` | 否 | `standard` 或 `rsi-infer`；`NULL` 用当前默认 |

### 3.4 沙箱

- `BOLE_SANDBOX_DEFAULT` 等同 `workspace-write`：工具只能写会话工作目录。
- `DANGER_FULL_ACCESS`：关闭文件沙箱，仅用于受控环境。
- 运行时启动自检若发现沙箱后端不可用，会在 `RUNTIME_READY` 的 `capabilities.sandbox` 报 `unavailable`；此时工具可能失败，调用方应提示用户或切换模式。

## 4. 句柄、会话与生命周期

### 4.1 句柄

- `bole_init` 成功返回一个 `bole_handle`；失败不返回句柄。
- 一个句柄对应一个运行时进程。
- 句柄级错误用 `bole_last_error` 读取，任意线程可读。
- `bole_release` 返回后句柄和所有会话句柄失效，不能再传回 SDK。

### 4.2 会话

- `bole_session_create` 要求 id 非空且全局唯一；同一 id 重复创建返回 `4002`。
- `bole_session_resume` 恢复已持久化会话；preset 按会话头恢复。没有持久化返回 `3006`，日志损坏返回 `4004`。
- 会话句柄归调用方持有，随 `bole_release` 一并失效。
- 本版本不提供单会话关闭。

### 4.3 就绪

`bole_init` 只负责校验和启动，不等待握手。就绪边界是 `RUNTIME_READY`：

- `bole_wait_ready(handle, timeout_ms)` 只等握手，不等模型或工具；
- 超时返回 `2004`；
- 握手失败返回对应错误码，并投递 `RUNTIME_FAILED`。

### 4.4 释放

- `bole_release` 立即返回；
- 返回后不再开始新的回调，已在执行中的一次回调允许完成；
- SDK 在后台向运行时发送关闭请求，超时后回收整棵进程树；
- 不要在事件回调中调用 `bole_release`。

### 4.5 运行时崩溃

进程异常退出时只投递 `RUNTIME_EXITED`，不为未结束的任务补造终态。该句柄作废；调用方新建句柄后可用 `bole_session_resume` 恢复已持久化会话。

## 5. 线程模型

### 5.1 四条规则

1. 事件回调只在 SDK 自有的单条事件线程上触发；同一句柄串行、FIFO；不同句柄各有线程，互不阻塞。
2. 回调必须快速返回（毫秒级）。回调慢只会让队列积压，不会阻塞运行时或控制面。
3. 调用方需要更新 UI 时，必须自行把事件切到 UI 线程；SDK 不提供 UI 线程调度。
4. 所有公开函数线程安全，除非单独注明；但 `bole_release` 之后不能再使用该句柄。

### 5.2 回调中允许

- `bole_submit`
- `bole_submit_blocks`
- `bole_cancel`
- `bole_rsi_set`
- `bole_rsi_get`
- `bole_session_set_preset`
- `bole_session_create`
- `bole_session_resume`
- `bole_last_error`
- `bole_set_event_callback`

### 5.3 回调中禁止

- `bole_init`
- `bole_wait_ready`
- `bole_release`
- `bole_trace_export`

禁止调用会返回 `1002`，不会产生死锁；但调用方不应依赖该返回值。

### 5.4 事件内存

`bole_event`、`session_id`、`payload_json` 的内存归 SDK 所有，只在本次回调期间有效。调用方要保留必须自行拷贝。

### 5.5 背压与丢弃

- 事件队列有界（4096 条 / 16 MiB）；
- 积压时先丢最旧的 `TEXT_DELTA` / `REASONING_DELTA`；
- 结构事件不因积压静默丢弃；结构事件溢出按故障处理，返回 `2007` 并断开；
- 增量可丢、可乱序到达；最终正文以 `ASSISTANT_MESSAGE` 为准。

## 6. 函数参考

所有函数都是 `__cdecl`，返回 `int32_t`，`0` 表示成功。

### 6.1 `bole_init`

```c
BOLE_API int32_t BOLE_CALL bole_init(
    const bole_config* config, bole_handle** out_handle);
```

- 校验配置、创建 `dsh_home`、启动运行时并立即返回。
- 就绪由 `RUNTIME_READY` 告知；用 `bole_wait_ready` 等待。
- 失败返回 `1001`–`1006` 或 `3001`，不返回句柄。
- 禁止在回调中调用。

### 6.2 `bole_wait_ready`

```c
BOLE_API int32_t BOLE_CALL bole_wait_ready(
    bole_handle* handle, uint32_t timeout_ms);
```

- 只等握手；`timeout_ms = 0` 只检查一次当前状态。
- 返回 `0`、`2004`（超时）、`3001`/`3002`（启动失败/已退出）。
- 禁止在回调中调用。

### 6.3 `bole_set_event_callback`

```c
BOLE_API int32_t BOLE_CALL bole_set_event_callback(
    bole_handle* handle, bole_event_cb event_cb, void* user_data);
```

- 替换回调；已入队事件按新回调投递。
- `event_cb` 不能为空；为空返回 `1003`。
- 回调中允许调用。

### 6.4 `bole_release`

```c
BOLE_API int32_t BOLE_CALL bole_release(bole_handle* handle);
```

- 释放调用方引用；立即返回；后台回收运行时进程树。
- 返回后不再开始新回调，所有会话句柄失效。
- `handle` 为 `NULL` 返回 `1002`。
- 禁止在回调中调用。

### 6.5 `bole_session_create`

```c
BOLE_API int32_t BOLE_CALL bole_session_create(
    bole_handle* handle, const char* session_id,
    const bole_session_options* options, bole_session** out_session);
```

- 按 id 建会话，可覆盖 `cwd` / `preset`。
- 只做创建和预设挂载，不触模型、不触工具。
- 返回 `0`、`1002`、`1004`、`2003`、`4002`。
- 回调中允许调用。

### 6.6 `bole_session_resume`

```c
BOLE_API int32_t BOLE_CALL bole_session_resume(
    bole_handle* handle, const char* session_id, bole_session** out_session);
```

- 恢复已持久化会话；preset 从会话头恢复。
- 返回 `0`、`1002`、`2003`、`3006`、`4001`、`4004`。
- 不重放历史事件；需要历史用 `bole_trace_export`。
- 回调中允许调用。

### 6.7 `bole_session_set_preset`

```c
BOLE_API int32_t BOLE_CALL bole_session_set_preset(
    bole_session* session, const char* preset_id);
```

- 只允许尚未开始首个任务的空白会话；之后返回 `4003`。
- `preset_id` 取 `standard` 或 `rsi-infer`。
- 返回 `0`、`1002`、`1004`、`2003`、`4003`。
- 回调中允许调用。

### 6.8 `bole_submit`

```c
BOLE_API int32_t BOLE_CALL bole_submit(
    bole_session* session, const char* utf8_text);
```

- 入队一条非空 UTF-8 文本消息；任务异步执行。
- 返回 `0`、`1002`、`2003`、`3002`。
- 回调中允许调用。

### 6.9 `bole_submit_blocks`

```c
BOLE_API int32_t BOLE_CALL bole_submit_blocks(
    bole_session* session, const char* content_blocks_json);
```

- 入队一条用户消息，内容块为 JSON 数组。
- 支持：
  - `{"type":"text","text":"..."}`
  - `{"type":"image","data":"<base64>","mimeType":"image/png|image/jpeg|image/webp|image/gif"}`
- 图片进入运行时附件存储；载荷不内联图片字节。
- 返回 `0`、`1002`、`2003`、`3002`。
- 回调中允许调用。

### 6.10 `bole_cancel`

```c
BOLE_API int32_t BOLE_CALL bole_cancel(bole_session* session);
```

- 立即中断当前任务：模型请求中断、已开始的工具中断、未开始的工具调用补失败结果、排队消息丢弃。
- 终态是 `TASK_FINISHED(outcome=aborted)`，不是 `TASK_FAILED`。
- 没有活动任务返回 `7001`，不当作成功。
- 不支持“步末终止”。
- 返回 `0`、`1002`、`2003`、`7001`。
- 回调中允许调用。

### 6.11 `bole_rsi_set`

```c
BOLE_API int32_t BOLE_CALL bole_rsi_set(bole_handle* handle, int32_t enabled);
```

- `1` 选 `rsi-infer`，`0` 选 `standard`；只影响后续新会话。
- 运行中没有 preset 面返回 `3004`，状态不变。
- 返回 `0`、`1002`、`2003`、`3004`。
- 回调中允许调用。

### 6.12 `bole_rsi_get`

```c
BOLE_API int32_t BOLE_CALL bole_rsi_get(
    bole_handle* handle, bole_rsi_state* out_state);
```

- 读取 SDK 侧投影；不发起网络或本地 RPC。
- `out_state->size` 必须设置；SDK 只写调用方声明长度内的字节。
- 返回 `0`、`1002`。
- 回调中允许调用。

### 6.13 `bole_trace_export`

```c
BOLE_API int32_t BOLE_CALL bole_trace_export(
    bole_handle* handle, const char* session_id, uint32_t turn,
    const char* path, uint32_t* out_event_count);
```

- 导出一行一个原始会话事件的 JSONL。
- `turn = 0` 导出整个会话；指定 turn 只导出该任务。
- `path` 的父目录必须已存在；已存在则覆盖。
- 等待文件写完，最多 30 秒。
- 返回 `0`、`1002`、`2003`、`3007`、`4001`。
- 禁止在回调中调用。

### 6.14 `bole_last_error`

```c
BOLE_API int32_t BOLE_CALL bole_last_error(
    bole_handle* handle, int32_t* out_code, char* buffer, size_t buffer_len);
```

- 读取本句柄最近一次错误（码 + 文案）；任意线程可读。
- `buffer` 在 `buffer_len > 0` 时始终 NUL 结尾；过长会截断。
- 没有记录错误时 `*out_code = 0`，`buffer` 为空串。
- 返回 `0`；`buffer_len > 0` 但 `buffer` 为 `NULL` 返回 `1002`。
- 回调中允许调用。

## 7. 事件参考

### 7.1 信封

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `size` | `uint32_t` | `sizeof(bole_event)` |
| `type` | `uint32_t` | 事件类型；未知值应忽略 |
| `event_id` | `uint64_t` | 句柄内单调递增 |
| `seq` | `uint64_t` | 来源会话事件序号；瞬时帧/运行时事件为 0 |
| `ts_ms` | `int64_t` | Unix 毫秒；句柄内非递减 |
| `task` | `uint32_t` | 任务（turn）号；非任务事件为 0 |
| `step` | `uint32_t` | 步骤（step）号；非步骤事件为 0 |
| `session_id` | `const char*` | 会话 id；运行时事件为 `""` |
| `payload_json` | `const char*` | JSON 对象，必带 `"v":1` |

### 7.2 运行时事件

**`RUNTIME_READY`(1)**

```json
{"v":1,
 "serverInfo":{"name":"runtime","version":"0.0.1"},
 "sdkProtocolVersion":1,
 "capabilities":{"preset":true,"sandbox":"workspace-write","persistence":true,"trace":true},
 "model":{"provider":"bole","model":"qwen3.8-27b"}}
```

**`RUNTIME_FAILED`(2)**

```json
{"v":1,"code":3002,"message":"runtime process exited","phase":"exit"}
```

`phase` 取 `spawn` / `handshake` / `transport` / `exit`。

**`RUNTIME_EXITED`(3)**

```json
{"v":1,"exitCode":0,"hadActiveTasks":false}
```

只表示进程退出，不补造任务终态。

### 7.3 任务与步骤

**`TASK_STARTED`(10)**

```json
{"v":1,"turn":1}
```

**`STEP_STARTED`(11) / `STEP_FINISHED`(12)**

```json
{"v":1,"turn":1,"step":1}
```

`step/end` 在失败时也会记录；任务在 pre-step 被拒绝时可能没有步骤事件。

### 7.4 计划与模型

**`PLAN_UPDATED`(13)**

```json
{"v":1,"kind":"plan_mode","active":true}
```

```json
{"v":1,"kind":"todos","todos":[
  {"content":"Read the spec","status":"completed"},
  {"content":"Implement parser","status":"in_progress"}
]}
```

`todos` 是整表替换；`status` 取 `pending` / `in_progress` / `completed`。

**`MODEL_SELECTED`(14)**

```json
{"v":1,"provider":"bole","model":"qwen3.8-27b",
 "reasoningEffort":"high","maxTokens":8192,"reason":"initial"}
```

`reason` 取 `initial` / `resume` / `change` / `series`。

### 7.5 RSI

**`RSI_STATE`(15)**

```json
{"v":1,"scope":"session","sessionId":"s-001","enabled":true,
 "preset":"rsi-infer","composed":true,"source":"session"}
```

`source=receipt` 时可能带：

```json
{"delivery":{"status":"delivered","bundleId":"...",
 "deliveredMemoryIds":["..."],"provider":"bole",
 "model":"qwen3.8-27b","bodySha256":"..."}}
```

`delivery.status` 取 `delivered` / `available` / `unsupported-model`。

### 7.6 增量与最终正文

**`TEXT_DELTA`(20) / `REASONING_DELTA`(21)**

```json
{"v":1,"channel":"text","attemptId":"s-001:1",
 "revision":3,"text":"Hel"}
```

- 增量是尽力而为，不保证完整或按序；
- 最终正文以 `ASSISTANT_MESSAGE` 为准；
- `revision` 在同一 Agent 生命周期内单调。

**`ASSISTANT_MESSAGE`(22)**

```json
{"v":1,"turn":1,"step":1,"messageId":"...",
 "text":"Reading it.","reasoning":"need the file first",
 "usage":{"inputTokens":10,"outputTokens":5,"totalTokens":15},
 "interrupted":true}
```

`text` / `reasoning` 各设 256 KB 上限；超限截断并带 `"truncated":true`。

### 7.7 工具

**`TOOL_CALL_STARTED`(30)**

```json
{"v":1,"callId":"call_01","tool":"read",
 "args":{"file_path":"README.md"}}
```

`args` 超过 64 KB 时省略并带 `"truncated":true`；参数不是合法 JSON 时保留原串并带 `"argsRaw":true`。

**`TOOL_CALL_FINISHED`(31)**

```json
{"v":1,"callId":"call_01","tool":"read","isError":false,
 "resultSummary":"# README\n","truncated":false}
```

```json
{"v":1,"callId":"call_02","tool":"bash","isError":true,
 "error":{"name":"AbortError","code":"ABORTED_BEFORE_DISPATCH"},
 "resultSummary":"","truncated":false}
```

`resultSummary` 是文本结果前 8 KB；图片/文件块只给占位。

### 7.8 终态

**`TASK_FINISHED`(50)**

```json
{"v":1,"turn":1,"outcome":"completed","reason":{"kind":"completed"}}
```

`outcome` 取 `completed` / `aborted` / `max_tokens` / `blocked` / `interrupted` / `forked`。

**`TASK_FAILED`(51)**

```json
{"v":1,"turn":1,"code":"SERVER","message":"upstream 500","status":500,
 "providerRetryAfterMs":1000,"requestId":"req_abc","retryable":true}
```

只来自任务错误终态；与 `TASK_FINISHED` 二选一。

### 7.9 顺序与唯一性

- 同一会话内串行：`TASK_STARTED` →（`STEP_STARTED` → `MODEL_SELECTED`? → 增量* → `ASSISTANT_MESSAGE`? → 工具事件* → `STEP_FINISHED`）* → 终态。
- 每个任务恰好一个 `TASK_FINISHED` 或 `TASK_FAILED`；`aborted` 归 `TASK_FINISHED`。
- `RSI_STATE` 与任务无关，可在任意时点出现；默认模式的初始状态在第一个 `TASK_STARTED` 前发出。
- 多会话事件在同一回调线程上按到达顺序分发，不保证跨会话全局顺序；调用方按 `session_id` + `task` 过滤。

## 8. RSI

RSI 是模式，不是独立服务开关：

- `standard`：不挂载只读经验插件；
- `rsi-infer`：在 standard 基础上挂载只读经验插件，并按模型 id 注入经验。

开关粒度：

| 入口 | 影响范围 |
| --- | --- |
| `bole_config.rsi_enabled` | 初始化后的新会话默认值 |
| `bole_rsi_set` | 后续新会话默认值 |
| `bole_session_set_preset` | 指定空白会话 |
| `bole_session_create.options.preset` | 本次创建的会话 |

- 运行中的任务不切换模式；首个任务开始后切换返回 `4003`。
- 运行时没有组合 preset 面时，`bole_rsi_set` 返回 `3004`，`bole_rsi_get` 的 `composed=0`、`preset=""`。
- `bole_rsi_get` 读 SDK 投影，不回运行时取值；需要确认权威状态时以 `RSI_STATE` 事件为准。
- `RSI_STATE` 的 `source` 取 `config` / `interface` / `session` / `receipt`。
- 标准模式不应出现经验投递收据。

## 9. 轨迹

`bole_trace_export` 导出原始会话日志，一行一个 JSON 对象：

- 用于排障和复现，不是 SDK 事件；
- `turn = 0` 导出整个会话；指定 turn 只导出该任务；
- 只导出调用时已经存在的事件，不等待未来事件；
- 父目录必须存在；文件已存在则覆盖；
- 调用会等文件写完，最多 30 秒；
- 禁止在事件回调中调用；
- 文件是 UTF-8 JSONL，路径由调用方负责权限和清理。

## 10. 限制与版本

- 平台：Windows 10 / 11 x64；不支持 32 位和其他系统。
- C ABI 版本：`BOLE_ABI_VERSION = 0x00010000`。
- 协议版本：`sdkProtocolVersion = 1`。
- 单行 JSON-RPC 上限默认 64 MiB，可由 `max_line_bytes` 覆盖。
- 图片支持 `image/png`、`image/jpeg`、`image/webp`、`image/gif`。
- MCP 工具在运行时配置文件中声明，重启生效；本版本不提供运行中注册/注销。
- 不提供单会话关闭；会话随 `bole_release` 失效。
- 验证阶段（`VERIFY_CHECK`）本版本不产生，不承诺送达。
- 运行时崩溃只发 `RUNTIME_EXITED`，不补造任务终态。
- 新增函数、结构体字段、事件类型只增不改；已有字段不改名、不改含义。

## 11. 检查清单

- [ ] 调用方按 `size` 设置 `bole_config` / `bole_session_options` / `bole_rsi_state`
- [ ] `runtime_dir`、`dsh_home`、`cwd` 路径正确且可写
- [ ] `event_cb` 非空，回调内不调用 `release` / `wait_ready` / `trace_export`
- [ ] UI 更新已切到 UI 线程
- [ ] 保留事件字段时已自行拷贝
- [ ] 退出前调用 `bole_release`，且未在回调中调用
- [ ] 取消、RSI、轨迹、错误码按本文和《错误码》处理
