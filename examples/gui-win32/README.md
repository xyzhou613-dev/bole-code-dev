# gui-win32 — 界面示例（B5）

五个必需元素：输入、提交、实时状态、结果、取消。SDK 跑在工作线程，事件回调只
`PostMessage`，UI 线程更新控件——长任务期间窗口不卡。

## 使用

离线包内直接双击：

```text
<package>\samples\gui-win32\bole_gui.exe
```

exe 会从自身位置推导：

```text
<package>\runtime
<package>\home
```

也可以在命令行覆盖：

```powershell
.\bole_gui.exe <runtime_dir> <dsh_home>
```

或设置环境变量：

```powershell
$env:BOLE_RUNTIME_DIR = "<package>\runtime"
$env:BOLE_DSH_HOME = "<package>\home"
.\bole_gui.exe
```

优先级：命令行参数 > 环境变量 > exe 位置推导。

## 行为

- 同一进程内可以连续提交多轮，复用同一个 session；不需要清 `home\sessions`；
- 每个 GUI 进程使用 `gui-<pid>` 作为 session id，重启不会和上一次冲突；
- 结果框按事件顺序**追加**，不是只显示最新一条；
- 状态栏显示 `runtime ready`、`task started`、`tool running`、`tool done`、
  `finished` / `finished (aborted)` / `failed`；
- Cancel 调用 `bole_cancel`，终态由 SDK 决定；
- 窗口关闭时取消活动任务、等待工作线程最多 5 秒，再释放 handle。

## 构建

```powershell
cmake -S examples/gui-win32 -B build/gui -G "Visual Studio 17 2022" -A x64
cmake --build build/gui --config Release
```

也可以把 `main.c` 加入任意 Win32 工程并链接 `bole_agent.lib` + `shell32.lib`。
