# GUI 日志更新说明

版本：r6-patch2 
日期：2026-11-09 
适用：`bole-offline-r6-final` 安装包

这次只改了 GUI 示例，SDK 的 DLL、运行时、协议和配置都没有动。客户更新时只需要替换一个 `bole_gui.exe`。

更新方法：

![image-20261109100001318](assets/image-20261109100001318.png)

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

每个 GUI 进程（新开的GUI会是一个新进程）会生成两个文件：

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

![image-20261109100413911](assets/image-20261109100413911.png)

### 5. 原始事件单独保存

`.jsonl` 一行一个事件，包含：

```text
time
type
name
payload
```

`payload` 是完整原始内容，包括 reasoning、文本增量、工具调用和结果，读起来确定费劲，排查问题时贴给模型也行，也可以找关键词自己定位问题。

![image-20261109100507555](assets/image-20261109100507555.png)

### 6. 增加 Open Log 按钮

GUI 上新增 **Open Log**，点击直接打开当前任务的 `.log` 文件，不用自己找目录，不过具体的目录是在：<SDK_HOME>\home\logs\。

![image-20261109100208644](assets/image-20261109100208644.png)

### 7. 中文日志兼容

`.log` 使用 UTF-8 BOM，用 Windows 记事本或 VS Code 打开中文不会乱码。
如果直接用 PowerShell 的 `Get-Content` 看，终端代码页不对时可能显示乱码，这是终端显示问题，文件本身是正常的。

### 8. RSI 状态

开启 RSI 后，任务日志里会多两行：

```text
RSI：on
经验：available  bundle=qwen38-service-v3
```

关闭 RSI 时：

```text
RSI：off
```

这次同时修正了 GUI 首次提交时只依赖 config 默认值、个别情况下 session 回落到 `standard` 的问题：新会话创建后会显式设置 `rsi-infer`，再提交第一个任务。勾选框、状态栏和原来的 RSI 逻辑都没有变。

【截图位置 5】  
开启 RSI 后跑一个任务，截 `.log` 里的：

```text
RSI：on
经验：available  bundle=...
```

## 补丁文件

```text
bole_gui.exe
```

SHA256：

```text
7fcf2ce21147205a83e1eb0e10c40966a941b7a0d572834d8752443bdff186d7
```

注意：这个补丁只适用于 r6 final。更早的包建议直接发完整 r7。
