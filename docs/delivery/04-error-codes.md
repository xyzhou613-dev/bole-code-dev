# 伯乐智能体 SDK 错误码

本文列出 SDK 的数值错误码、对应常量、协议错误名、是否可重试和典型场景。程序判断**只使用数值码**，不要依赖错误文案。

## 1. 错误模型

SDK 的错误通过两层表达：

1. **函数返回值**：`0` 成功；正整数为错误码。
2. **`bole_last_error`**：读取本句柄最近一次错误（码 + 文案）。

```c
int32_t code = 0;
char message[256];
if (bole_last_error(handle, &code, message, sizeof(message)) == 0) {
    /* code 为 0 表示没有记录错误；message 仅用于日志 */
}
```

- 错误码是稳定 ABI，只增不改；新增码使用未占用数值。
- `bole_last_error` 是句柄级，不是线程局部；任意线程可读。
- 文案是英文短句，仅用于日志和排障。
- 协议层的 `error.data.retryable` 存在时，以该字段为准；SDK 的默认重试建议见第 4 节。

## 2. 1xxx–7xxx 全表

### 1xxx：配置与本地参数

| 数值 | 常量 | 协议错误名 | 可重试 | 说明 |
| --- | --- | --- | --- | --- |
| 1001 | `BOLE_E_RUNTIME_DIR_NOT_FOUND` | —（本地） | 否 | `runtime_dir` 不存在或缺少运行时入口 |
| 1002 | `BOLE_E_INVALID_CONFIG` | `INVALID_ARGUMENT` | 否 | 配置或参数非法 |
| 1003 | `BOLE_E_EVENT_CALLBACK_REQUIRED` | —（本地） | 否 | 未提供事件回调 |
| 1004 | `BOLE_E_PRESET_NOT_FOUND` | `PRESET_NOT_FOUND` | 否 | 模式未定义 |
| 1005 | `BOLE_E_CONFIG_PARSE_FAILED` | —（本地） | 否 | 配置文件解析失败 |
| 1006 | `BOLE_E_INVALID_ARGUMENT` | `INVALID_ARGUMENT` | 否 | 单个参数非法 |

### 2xxx：传输与协议

| 数值 | 常量 | 协议错误名 | 可重试 | 说明 |
| --- | --- | --- | --- | --- |
| 2001 | `BOLE_E_PIPE_FAILED` | —（本地） | 是 | 管道读写失败 |
| 2002 | `BOLE_E_PROTOCOL_PARSE_FAILED` | `-32700` / `-32601` | 视情况 | JSON 解析失败或未知方法 |
| 2003 | `BOLE_E_REQUEST_TIMEOUT` | —（本地） | 是 | 本地 RPC 超时 |
| 2004 | `BOLE_E_WAIT_READY_TIMEOUT` | —（本地） | 是 | 等待就绪超时 |
| 2005 | `BOLE_E_LINE_TOO_LONG` | `LINE_TOO_LONG` | 否 | 单行超过上限，随后断开 |
| 2006 | `BOLE_E_STDOUT_VIOLATION` | —（本地） | 否 | 运行时 stdout 首帧不是 JSON-RPC |
| 2007 | `BOLE_E_EVENT_QUEUE_OVERFLOW` | —（本地） | 否 | 结构事件队列溢出，断开 |
| 2008 | `BOLE_E_PROTOCOL_VERSION_MISMATCH` | —（本地） | 否 | 协议主版本不兼容 |

### 3xxx：运行时与能力

| 数值 | 常量 | 协议错误名 | 可重试 | 说明 |
| --- | --- | --- | --- | --- |
| 3001 | `BOLE_E_RUNTIME_START_FAILED` | —（本地） | 视情况 | 运行时启动失败 |
| 3002 | `BOLE_E_RUNTIME_EXITED` | —（本地/事件） | 否 | 运行时已退出；可新开句柄后恢复 |
| 3003 | `BOLE_E_RUNTIME_VERSION_MISMATCH` | —（本地） | 否 | 版本清单与实际不一致（默认继续并告警） |
| 3004 | `BOLE_E_RSI_UNAVAILABLE` | `RSI_UNAVAILABLE` | 否 | 运行时未组合 preset 面 |
| 3005 | `BOLE_E_SANDBOX_UNAVAILABLE` | `SANDBOX_UNAVAILABLE` | 否 | 文件沙箱后端不可用 |
| 3006 | `BOLE_E_PERSISTENCE_UNAVAILABLE` | `PERSISTENCE_UNAVAILABLE` | 否 | 没有会话持久化，不能恢复 |
| 3007 | `BOLE_E_RUNTIME_REQUEST_FAILED` | `-32603`（无具体名） | 视情况 | 运行时处理失败且无可映射错误名 |

### 4xxx：会话

| 数值 | 常量 | 协议错误名 | 可重试 | 说明 |
| --- | --- | --- | --- | --- |
| 4001 | `BOLE_E_SESSION_NOT_FOUND` | `SESSION_NOT_FOUND` | 否 | 会话不存在 |
| 4002 | `BOLE_E_SESSION_EXISTS` | `SESSION_EXISTS` | 否 | 同一 id 重复创建 |
| 4003 | `BOLE_E_PRESET_LOCKED` | `PRESET_LOCKED` | 否 | 首个任务已开始，不能切模式 |
| 4004 | `BOLE_E_SESSION_NOT_RESUMABLE` | `SESSION_NOT_RESUMABLE` | 否 | 日志损坏或没有可恢复状态 |

### 5xxx：模型请求

| 数值 | 常量 | 协议错误名 | 可重试 | 说明 |
| --- | --- | --- | --- | --- |
| 5001 | `BOLE_E_MODEL_AUTH_FAILED` | `MODEL_AUTH_FAILED` | 否 | 鉴权失败 |
| 5002 | `BOLE_E_MODEL_RATE_LIMITED` | `MODEL_RATE_LIMITED` | 是 | 限流 |
| 5003 | `BOLE_E_MODEL_CONTEXT_EXCEEDED` | `MODEL_CONTEXT_EXCEEDED` | 否 | 上下文超限 |
| 5004 | `BOLE_E_MODEL_NOT_FOUND` | `MODEL_NOT_FOUND` | 否 | 模型不存在 |
| 5005 | `BOLE_E_MODEL_REQUEST_FAILED` | `MODEL_REQUEST_FAILED` | 是 | 其它模型请求失败 |

### 6xxx：工具

| 数值 | 常量 | 协议错误名 | 可重试 | 说明 |
| --- | --- | --- | --- | --- |
| 6001 | `BOLE_E_TOOL_NOT_FOUND` | `TOOL_NOT_FOUND` | 否 | 工具不存在 |
| 6002 | `BOLE_E_TOOL_TIMEOUT` | `TOOL_TIMEOUT` | 是 | 工具超时 |
| 6003 | `BOLE_E_TOOL_CRASHED` | `TOOL_CRASHED` | 是 | 工具子进程崩溃 |
| 6004 | `BOLE_E_TOOL_FAILED` | `TOOL_FAILED` | 是 | 其它工具失败 |

### 7xxx：取消

| 数值 | 常量 | 协议错误名 | 可重试 | 说明 |
| --- | --- | --- | --- | --- |
| 7001 | `BOLE_E_NO_ACTIVE_TASK` | `NO_ACTIVE_TASK` | 否 | 没有活动任务可取消 |
| 7002 | `BOLE_E_CANCEL_FAILED` | `CANCEL_FAILED` | 是 | 取消请求本身失败 |

## 3. 协议错误名与 JSON-RPC 映射

| JSON-RPC error | 协议错误名 | SDK 数值码 | 场景 |
| --- | --- | --- | --- |
| `-32700` | `PROTOCOL_PARSE_FAILED` | 2002 | JSON 解析失败 |
| `-32600` | `INVALID_ARGUMENT` / `LINE_TOO_LONG` | 1002 / 2005 | 非法请求对象、超长行 |
| `-32601` | `METHOD_NOT_FOUND` | 2002 | 未知方法 |
| `-32602` | `INVALID_ARGUMENT` | 1006 | 参数类型/取值非法 |
| `-32603` | `PRESET_NOT_FOUND` | 1004 | 模式未定义 |
| `-32603` | `PRESET_LOCKED` | 4003 | 首个任务后切模式 |
| `-32603` | `SESSION_EXISTS` | 4002 | 重复创建 |
| `-32603` | `SESSION_NOT_FOUND` | 4001 | 会话不存在 |
| `-32603` | `SESSION_NOT_RESUMABLE` | 4004 | 日志损坏或缺状态 |
| `-32603` | `PERSISTENCE_UNAVAILABLE` | 3006 | 没有持久化服务 |
| `-32603` | `RSI_UNAVAILABLE` | 3004 | 未组合 preset 面 |
| `-32603` | `NO_ACTIVE_TASK` | 7001 | 无活动任务可取消 |
| `-32603` | `TRACE_WRITE_FAILED` | 3007 | 轨迹文件写失败 |
| `-32603` | `MODEL_*` | 5xxx | 模型请求失败 |
| `-32603` | `TOOL_*` | 6xxx | 工具失败 |
| `-32603` | `CANCEL_FAILED` | 7002 | 取消失败 |
| `-32603` | 其它/缺失 | 3007 | 未映射的运行时错误 |

未知的协议错误名按首个数字区间归档：

| 区间 | SDK 数值码 |
| --- | --- |
| 1xxx | 1006 |
| 2xxx | 2002 |
| 3xxx | 3007 |
| 4xxx | 4001 |
| 5xxx | 5005 |
| 6xxx | 6004 |
| 7xxx | 7002 |

## 4. 重试与降级建议

### 4.1 可重试

- 传输失败 `2001`
- 本地 RPC 超时 `2003`
- 等待就绪超时 `2004`
- 模型限流 `5002`
- 其它模型请求失败 `5005`
- 工具超时 `6002`、工具崩溃 `6003`、工具失败 `6004`
- 取消失败 `7002`

建议使用指数退避，并设置总超时；不要无限重试。

### 4.2 不可重试

- 配置/参数错误 `1001`–`1006`
- 行超限 `2005`、首帧违规 `2006`、队列溢出 `2007`
- 协议版本不匹配 `2008`
- 运行时已退出 `3002`（同一句柄不可用；新建句柄后可恢复会话）
- RSI 不可用 `3004`、沙箱不可用 `3005`、持久化不可用 `3006`
- 会话 `4001`–`4004`
- 模型鉴权失败 `5001`、上下文超限 `5003`、模型不存在 `5004`
- 工具不存在 `6001`
- 无活动任务 `7001`

### 4.3 降级

- `3004` RSI 不可用：继续用 `standard`，界面隐藏 RSI 相关状态。
- `3005` 沙箱不可用：提示用户；由部署方决定是否切 `danger-full-access`，不要由应用静默切换。
- `4003` 模式锁定：新建会话后重新选择模式。
- `5002` 限流：显示“请求过多，稍后重试”，按退避重试。
- `5003` 上下文超限：提示缩短输入或新建会话。
- `3002` 运行时退出：新建句柄，调用 `bole_session_resume` 恢复；不要复用旧句柄。

## 5. 示例

### 5.1 取消无活动任务

```c
int32_t rc = bole_cancel(session);
if (rc == BOLE_E_NO_ACTIVE_TASK) {
    /* 当前没有可取消的任务，不是故障 */
}
```

### 5.2 首个任务后切模式

```c
int32_t rc = bole_session_set_preset(session, "rsi-infer");
if (rc == BOLE_E_PRESET_LOCKED) {
    /* 该会话已开始任务；新建会话后再切 */
}
```

### 5.3 RSI 不可用

```c
int32_t rc = bole_rsi_set(handle, 1);
if (rc == BOLE_E_RSI_UNAVAILABLE) {
    bole_rsi_state state = {0};
    state.size = sizeof(state);
    bole_rsi_get(handle, &state);   /* composed == 0, preset == "" */
}
```

### 5.4 运行时退出

```c
if (bole_wait_ready(handle, 30000) == BOLE_E_RUNTIME_EXITED) {
    /* 该句柄作废；释放后新建句柄 */
    bole_release(handle);
    handle = NULL;
    /* 新句柄初始化完成后可以： */
    /* bole_session_resume(new_handle, "demo-1", &session); */
}
```

## 6. 检查清单

- [ ] 程序按数值码判断错误，不依赖文案
- [ ] 每次失败后读取 `bole_last_error` 并写日志
- [ ] 可重试错误有退避和总超时
- [ ] `3002` 后不复用旧句柄，新建句柄再恢复
- [ ] `3004` / `3005` / `4003` 有界面降级提示
- [ ] `7001` 当作正常状态，不弹错误框
- [ ] 错误码表与所用 SDK 版本一致
