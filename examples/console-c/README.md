# console-c — 最小集成示例

30 行核心调用：`bole_init → bole_wait_ready → bole_session_create → bole_submit → 事件回调 → bole_release`。
不依赖第三方工具；配合 `examples/toy-mcp` 完成一次 MCP 工具调用。

```bash
bole_console <runtime_dir> <dsh_home> "算一下 2+2"
```

事件按 `bole_event.type` 分支打印；真实集成里应把 `TEXT_DELTA` 追加到 UI，`TASK_FINISHED/FAILED` 结束一轮。
