# GUI 日志更新说明

版本：r6-patch1  
日期：2026-11-09  
适用：`bole-offline-r6-final` 安装包

这次只改了 GUI 示例，SDK 的 DLL、运行时、协议和配置都没有动。客户更新时只需要替换一个 `bole_gui.exe`。

## 之前的问题

之前 GUI 的结果区会显示原始 JSON，而且日志缓冲是固定大小、跑完任务也不会清空。  
任务一多或者回答一长，缓冲满了以后新日志就不再追加，界面上一直停在上一个任务的旧内容。客户看到的“任务已经完成，但日志还显示中断”，就是这个原因。

另外，原来的日志里混着 `TEXT_DELTA`、`ASSISTANT_MESSAGE` 的原始 JSON，读起来很费劲。

## 这次改了什么

### 1. 结果区只显示当前任务

每次开始新任务时清空结果区，不再把上一个任务的内容带过来。

### 2. 日志缓冲不会再“卡死”

缓冲满的时候会丢掉最旧的一部分，保留最新内容，不会像以前一样停止更新。

### 3. 日志写入文件

每个 GUI 进程会生成两个文件：

```text
<SDK_HOME>\home\logs\gui-YYYYMMDD-HHMMSS-<pid>.log
<SDK_HOME>\home\logs\gui-YYYYMMDD-HHMMSS-<pid>.jsonl
```

`.log` 给人看，`.jsonl` 给开发排查。

### 4. 可读日志按任务分组

`.log` 里现在是这样：

```text
============================================================
任务 #1  2026-11-09 09:52:17
用户：
Task one: reply exactly FIRST_OK.

回答：
FIRST_OK

状态：finished    耗时：0.5s
============================================================
```

有工具调用时：

```text
工具：mcp__toy__add
参数：{"a":1,"b":2}
结果：3
```

有思考内容时，会多一段：

```text
思考：
...
```

当前 qwen 配置关闭了 thinking，所以这一版一般没有“思考”内容；以后开启后会自动写进来。

### 5. 原始事件单独保存

`.jsonl` 一行一个事件，包含：

```text
time
type
name
payload
```

`payload` 是完整原始内容，包括 reasoning、文本增量、工具调用和结果。

### 6. 增加 Open Log 按钮

GUI 上新增 **Open Log**，点击直接打开当前任务的 `.log` 文件，不用自己找目录。

### 7. 中文日志兼容

`.log` 使用 UTF-8 BOM，用 Windows 记事本或 VS Code 打开中文不会乱码。  
如果直接用 PowerShell 的 `Get-Content` 看，终端代码页不对时可能显示乱码，这是终端显示问题，文件本身是正常的。

## 更新方法

1. 关闭正在运行的 `bole_gui.exe`；
2. 备份原来的：
   ```text
   <SDK_HOME>\samples\gui-win32\bole_gui.exe
   ```
3. 用补丁里的 `bole_gui.exe` 覆盖；
4. 重新打开 GUI。

不需要替换 `bole_agent.dll`，也不需要改 `home` 配置。

## 验证方法

连续提交两个任务：

- 第二个任务的结果区不应该出现第一个任务的内容；
- 结果区里不应该出现 `"v":1` 这种原始 JSON；
- 看 `<SDK_HOME>\home\logs\`，应该生成一对新的 `.log` / `.jsonl`；
- 点 **Open Log**，应该能直接打开当前日志；
- `.log` 里两个任务都应该在，按任务分开。

## 截图位置

截图时不用把整块屏幕都截进去，窗口和关键内容清楚就行。

【截图位置 1】  
截 GUI 连续跑两个任务后的结果区，重点拍到：

```text
[task] started
[answer] ...
[tool] mcp__toy__add -> 3
[task] finished
```

要能看出来第二个任务里没有第一个任务的旧内容，也没有原始 JSON。

【截图位置 2】  
截 `<SDK_HOME>\home\logs\` 目录，重点拍到同一时间生成的一对文件：

```text
gui-YYYYMMDD-HHMMSS-<pid>.log
gui-YYYYMMDD-HHMMSS-<pid>.jsonl
```

【截图位置 3】  
打开 `.log`，截“任务 #1 / 用户 / 工具 / 回答 / 状态 / 耗时”这几段，证明日志是可读的。

【截图位置 4】  
打开 `.jsonl`，截一两行，重点能看到：

```text
"time"
"type"
"name"
"payload"
```

说明完整原始事件也保留了。

## 补丁文件

```text
bole_gui.exe
```

SHA256：

```text
4291a330395022ccd9f234a43023d61c0016f515ad8c2be5c05fd6b807a418c8
```

注意：这个补丁只适用于 r6 final。更早的包建议直接发完整 r7。
