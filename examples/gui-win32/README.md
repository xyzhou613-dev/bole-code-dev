# gui-win32 — 界面示例（B5）

五个必需元素：输入、提交、实时状态、结果、取消。SDK 跑在工作线程，事件回调只
`PostMessage`，UI 线程更新控件——长任务期间窗口不卡。

```powershell
set BOLE_RUNTIME_DIR=<离线包>\runtime
set BOLE_DSH_HOME=<离线包>\runtime\home
```

构建：把 `main.c` 加入任意 Win32 工程并链接 `bole_agent.lib`（或 CMake 里
`add_executable(bole_gui WIN32 main.c)` + `target_link_libraries(bole_gui PRIVATE bole_agent)`）。
