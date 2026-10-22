# gui-win32 — 界面示例（B5）

五个必需元素：输入、提交、实时状态、结果、取消。SDK 跑在工作线程，事件回调只
`PostMessage`，UI 线程更新控件——长任务期间窗口不卡。

```powershell
$env:BOLE_RUNTIME_DIR = "<离线包>\runtime"
$env:BOLE_DSH_HOME = "<离线包>\home"
```

构建：

```powershell
cmake -S examples/gui-win32 -B build/gui -G "Visual Studio 17 2022" -A x64
cmake --build build/gui --config Release
```

也可以把 `main.c` 加入任意 Win32 工程并链接 `bole_agent.lib`。
