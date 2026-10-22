# console-c — 最小集成示例

30 行核心调用：`bole_init → bole_wait_ready → bole_session_create → bole_submit → 事件回调 → bole_release`。
不依赖第三方工具；配合 `examples/toy-mcp` 完成一次 MCP 工具调用。

```bash
bole_console <runtime_dir> <dsh_home> "算一下 2+2"
```

事件按 `bole_event.type` 分支打印；真实集成里应把 `TEXT_DELTA` 追加到 UI，`TASK_FINISHED/FAILED` 结束一轮。

## 构建

```bash
cmake -S examples/console-c -B build/console -DCMAKE_BUILD_TYPE=Release
cmake --build build/console -j4
```

## C2/M0 证据

开发机准备好锁定的运行时树、桥接插件和经验包后：

```bash
BOLE_DEV_RUNTIME=<runtime> node examples/console-c/smoke.mjs \
  build/console/bole_console
```

脚本会启动 mock 模型（工具调用 `mcp__toy__add`）和玩具 MCP，装配临时运行时，
执行一次「工具调用 → 结果 → 终态」，并打印耗时。期望输出包含：

```text
PASS tool started
PASS tool finished
PASS task finished
PASS exit code
TIMING <毫秒> ms
PASS console-c C2/M0 tool call
```

## B8 自检

包内 `bole_selfcheck.exe` 用当前 runtime/home 跑一次 ABI 自检：

```powershell
.\bole_selfcheck.exe <runtime_dir> <dsh_home> "用玩具工具计算 1+2"
```

检查项：runtime ready、preset registry、standard 工具调用完成、rsi-infer 收据、
取消后 `aborted`、trace 导出。失败时打印首个失败检查并以非零码退出。

