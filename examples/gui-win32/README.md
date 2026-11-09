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

- 模型默认取 `runtime/cordis.patch.yml` 的 `agent-default-model`；C 配置里的 `model_name` 可留空；
- RSI 复选框控制**新任务**的 preset（`standard` / `rsi-infer`）；
  首次 Submit 前勾选直接生效；已有任务后勾选，会在下一轮/下一次提交时用新 preset 建新 session；
- 同一进程内可以连续提交多轮，复用同一个 session；不需要清 `home\sessions`；
- 每个 GUI 进程使用 `gui-<pid>` 作为 session id，重启不会和上一次冲突；
- 结果框只显示**当前任务**，新任务开始时清空；跨任务历史在日志文件里；
- 状态栏显示 `runtime ready`、`task started`、`tool running`、`tool done`、
  `finished` / `finished (aborted)` / `failed`；
- 每个进程写一份可读日志：
  ```text
  <dsh_home>\logs\gui-YYYYMMDD-HHMMSS-<pid>.log
  ```
  按任务分组，包含用户输入、工具调用/结果、最终回答、状态和耗时；
- 原始事件另写一份：
  ```text
  <dsh_home>\logs\gui-YYYYMMDD-HHMMSS-<pid>.jsonl
  ```
  一行一个事件，包含 `time`、`type`、`name`、`payload`，保留 reasoning、delta 和工具原始结果；
- 界面上的 **Open Log** 按钮直接打开当前可读日志文件；
- Cancel 调用 `bole_cancel`，终态由 SDK 决定；
- 窗口关闭时取消活动任务、等待工作线程最多 5 秒，再释放 handle。

## 构建

```powershell
cmake -S examples/gui-win32 -B build/gui -G "Visual Studio 17 2022" -A x64
cmake --build build/gui --config Release
```

也可以把 `main.c` 加入任意 Win32 工程并链接 `bole_agent.lib` + `shell32.lib`。
