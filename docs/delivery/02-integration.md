# 伯乐智能体 SDK 集成指南

本文面向第一次把 SDK 接入桌面应用的工程师。读完应能完成：

1. 链接 `bole_agent.lib` / 加载 `bole_agent.dll`；
2. 启动运行时并等待就绪；
3. 创建会话、提交一条消息；
4. 在事件回调里拿到流式输出和最终结果；
5. 取消任务、处理错误、释放句柄。

接口细节以《接口说明》为准。

## 1. 前置条件

- Windows 10 / 11 x64。
- 目标机不需要预装 Node、.NET 或 Visual Studio 运行库；运行时随包提供。
- 调用方进程与 SDK 位数一致（x64）。
- 有可写的运行目录（用于会话、日志、缓存）和一个项目工作目录。
- 模型网关地址、工具配置已由部署方在运行时配置中提供；调用方只传 provider / model。

典型的包布局：

```text
<包根>/
├── bin/
│   ├── bole_agent.dll
│   ├── bole_agent.lib
│   └── bole_agent.h
├── runtime/        # 运行时与依赖，随包只读
├── home/           # 可写运行目录
├── samples/        # 示例源码与可执行
└── docs/           # 本组文档
```

## 2. 链接与加载

### 2.1 C / C++

1. 把 `bole_agent.h` 加入 include 路径。
2. 把 `bole_agent.lib` 加入链接输入。
3. 把 `bole_agent.dll` 放到可执行文件同目录，或加入 `PATH`。

C++ 直接包含头文件即可，头文件已带 `extern "C"`：

```cpp
#include "bole_agent.h"
```

- 调用约定是 `__cdecl`。
- 字符串统一 UTF-8，不要传 UTF-16 或 ANSI 代码页。
- 结构体首字段是 `size`，必须设置为 `sizeof(...)`。

### 2.2 C# / 其他绑定

- 用 `DllImport("bole_agent.dll", CallingConvention = CallingConvention.Cdecl)`。
- 回调委托必须保持存活（保存为字段或 `GCHandle`），否则会被 GC 回收。
- 结构体按 C 布局声明；`size` 用 `Marshal.SizeOf` 或与头文件一致的值。
- 事件字符串只在回调期间有效，需要保留时立即复制成托管字符串。

### 2.3 加载失败排查

- `LoadLibrary` 失败：确认 DLL 位数、依赖和路径。
- 找不到入口点：确认头文件、`.lib`、`.dll` 是同一版本。
- `bole_init` 返回 `1001`：`runtime_dir` 下缺少 `node.exe`、运行时入口或补丁配置。

## 3. 最小示例

以下示例演示 `init → wait_ready → create → submit → 事件 → release`。实际应用应把等待改成自己的消息循环，不要在 UI 线程里长时间阻塞。

```c
#include "bole_agent.h"
#include <windows.h>
static volatile LONG g_done = 0;
static void BOLE_CALL on_event(void* user_data, const bole_event* event) {
    (void)user_data;
    if (event->type == BOLE_EVENT_TASK_FINISHED
        || event->type == BOLE_EVENT_TASK_FAILED) InterlockedExchange(&g_done, 1);
}
int main(void) {
    bole_config config = {0};
    config.size = sizeof(config);
    config.runtime_dir = "<包根>\\runtime";
    config.dsh_home = "<包根>\\home";
    config.cwd = "<项目目录>";
    config.model_provider = "bole";
    config.model_name = "qwen3.8-27b";
    config.event_cb = on_event;
    bole_handle* handle = NULL;
    bole_session* session = NULL;
    if (bole_init(&config, &handle) != 0) return 1;
    if (bole_wait_ready(handle, 30000) != 0) { bole_release(handle); return 1; }
    if (bole_session_create(handle, "demo-1", NULL, &session) != 0) {
        bole_release(handle); return 1;
    }
    if (bole_submit(session, "用玩具工具计算 1+2") != 0) { bole_release(handle); return 1; }
    while (g_done == 0) Sleep(50);
    bole_release(handle);
    return 0;
}
```

要点：

- `bole_init` 只启动进程，不等待模型；就绪由 `RUNTIME_READY` 和 `bole_wait_ready` 表达。
- `bole_submit` 返回只表示消息已入队，不表示任务完成。
- 终态由 `TASK_FINISHED` / `TASK_FAILED` 给出。
- `bole_release` 会回收运行时和工具进程树；退出前必须调用。

## 4. 事件回调与 UI 线程

SDK 的回调在 SDK 自有线程上执行，同一句柄串行、FIFO。**不要直接在回调里操作 UI 控件。**

推荐两种做法：

### 4.1 投递到 UI 线程

回调里只做拷贝和投递：

```c
typedef struct ui_event {
    uint32_t type;
    uint32_t task;
    uint32_t step;
    char* payload;      /* 自己拷贝的 UTF-8 JSON */
    char* session_id;   /* 自己拷贝的 UTF-8 id */
} ui_event;
```

- 在回调中 `malloc` 拷贝 `payload_json` / `session_id`；
- `PostMessage(hwnd, WM_APP + 1, 0, (LPARAM)ui_event)`；
- UI 线程处理完释放。

不要在回调里 `SendMessage` 到 UI 线程，容易造成阻塞或死锁。

### 4.2 自建队列

- 回调把事件拷贝进线程安全队列；
- 单独的 UI 定时器或工作线程取出并更新界面；
- 队列满时优先丢 `TEXT_DELTA` / `REASONING_DELTA`，保留结构事件和终态。

### 4.3 切线程规则

- 回调必须快速返回（毫秒级）。
- 回调中可以调用 `bole_submit`、`bole_cancel`、`bole_rsi_get`、`bole_last_error` 等；
- 回调中禁止 `bole_release`、`bole_wait_ready`、`bole_trace_export`、`bole_init`。
- 回调里的字符串只在本次回调期间有效，必须自行拷贝。

## 5. 取消

取消按钮应调用：

```c
int32_t rc = bole_cancel(session);
```

- `0`：取消请求已受理；随后会收到 `TASK_FINISHED(outcome=aborted)`。
- `7001`：当前没有活动任务，不是错误，界面可提示“当前没有可取消的任务”。
- 取消是立即中断，不支持“步末终止”。
- 取消后，管道里可能还有少量已在飞行中的增量；按 `task` 过滤即可。
- 取消后不应再收到该任务的步骤/工具/增量事件；终态只有 `aborted`。

如果界面在等待终态，建议加超时保护；超时后仍以 `TASK_FINISHED` / `TASK_FAILED` 为准，不要自行伪造终态。

## 6. 错误处理

### 6.1 返回值

- `0` 成功；
- 正整数为错误码，完整表见《错误码》；
- 错误码是稳定 ABI，不随文案变化。

### 6.2 `last_error`

```c
int32_t code = 0;
char message[256];
bole_last_error(handle, &code, message, sizeof(message));
```

- 读取本句柄最近一次错误；
- 文案只用于日志，不要用于程序判断；
- 句柄级错误不是线程局部。

### 6.3 重试建议

| 场景 | 建议 |
| --- | --- |
| `2003` 本地 RPC 超时 | 可重试一次；持续失败检查运行时是否卡住 |
| `2004` 等待就绪超时 | 检查运行时启动、模型配置、磁盘权限 |
| `3002` 运行时已退出 | 新建句柄后 `session/resume`，不要复用旧句柄 |
| `5002` 模型限流 | 按 `providerRetryAfterMs` 或退避重试 |
| `6002` / `6003` 工具超时/崩溃 | 可重试任务；检查工具进程和日志 |
| `7001` 无活动任务 | 不重试，提示用户 |
| `4002` / `4003` / `4004` | 不重试，修正 id / preset / 会话状态 |

## 7. 日志与排障

### 7.1 SDK 日志

`bole_config.log_path` 指向文件时，SDK 把运行时 stderr 和自身诊断追加到该文件；不配置时默认丢弃。

建议：

- 每次启动使用独立日志文件或按日期轮转；
- 日志路径放在 `home/logs/` 下；
- 不要把日志写到运行时只读目录。

### 7.2 轨迹

需要复盘某个任务时：

```c
uint32_t count = 0;
bole_trace_export(handle, "demo-1", 0, "<项目目录>\\trace\\demo-1.jsonl", &count);
```

- `turn = 0` 全会话；指定 turn 只导出该任务；
- 文件是一行一个原始会话事件；
- 只能在非回调线程调用；
- 导出的是调用时已存在的日志，不包含未来事件。

### 7.3 常见问题

| 现象 | 排查 |
| --- | --- |
| `bole_init` 返回 `1001` | 检查 `runtime_dir` 下 `node.exe`、运行时入口、补丁配置 |
| `bole_init` 返回 `1003` | 设置 `event_cb` |
| `wait_ready` 超时 | 看日志、检查 `dsh_home` 可写、模型配置、端口/代理 |
| 没有 `TEXT_DELTA` | 增量可丢；看 `ASSISTANT_MESSAGE` 和终态 |
| 没有终态 | 检查运行时是否崩溃；崩溃只发 `RUNTIME_EXITED` |
| 工具调用失败 | 看 `TOOL_CALL_FINISHED.isError` 和 `error.code`；检查工具配置 |
| 切 preset 返回 `4003` | 首个任务已开始，不能再切；新建会话 |
| RSI 设置返回 `3004` | 运行时未组合 preset 面；检查部署配置 |

## 8. 集成检查清单

- [ ] 目标机是 Windows 10/11 x64，DLL 位数与进程一致
- [ ] `runtime_dir`、`dsh_home`、`cwd` 正确；`dsh_home` 可写
- [ ] `bole_config.size` / `event_cb` 已设置
- [ ] 事件已切到 UI 线程，回调内没有 UI 操作
- [ ] 回调内没有调用 `release` / `wait_ready` / `trace_export` / `init`
- [ ] 保留事件字段时已自行拷贝
- [ ] 取消按钮处理 `0` 和 `7001` 两种情况
- [ ] 错误码按数值判断，不依赖文案
- [ ] 退出前调用 `bole_release`，且不在回调中调用
- [ ] 已用 `log_path` / `bole_trace_export` 留下排障材料
