# r7 更新说明

版本：r7 final  
日期：2026-11-09  
适用：`bole-offline-r7-final.zip`

## 安装方式

这次建议直接用 r7 替换旧包。

如果客户不需要保留旧会话、旧日志和自定义 MCP 配置，可以删除旧安装目录后重新安装：

```powershell
.\install.ps1 `
  -Target D:\apps\BoleAgentSdk `
  -ModelGateway <新网关>
```

如果客户需要保留历史数据，建议不要删 `home`：

```text
<SDK_HOME>\home\
```

`home` 里有会话、日志、`.env` 和 `home\cordis.patch.yml`。删除重装会一起清掉；需要保留就先备份。

【截图位置 1】  
截安装完成后的目录，能看出：

```text
bin\
runtime\
samples\
home\
```

以及 `install.ps1` 最后输出的：

```text
installed to D:\apps\BoleAgentSdk
```

## 这次更新了什么

### 1. 换模型不用再重新编译

以前示例程序里写死了：

```c
config.model_name = "qwen3.8-27b";
```

现在改成：

```c
config.model_name = NULL;
```

SDK 会自动读取：

```text
runtime/cordis.patch.yml
  → agent-default-model
```

客户只需要改配置文件，不用改代码、不用重新编译示例。

### 2. GUI 日志修复

- 新任务只显示当前任务，不再带出上一个任务的旧日志；
- 日志缓冲满了会保留最新内容，不会停止更新；
- 不再显示原始 JSON；
- 新增按任务分组的 `.log`；
- 新增完整原始事件 `.jsonl`；
- 新增 **Open Log** 按钮；
- RSI 开启/关闭会写进日志。

### 3. RSI

RSI 勾选、状态栏和日志都保留。  
使用 125b 时经验包不匹配，会出现 `unsupported-model`，这是预期行为；普通聊天和 MCP 工具不受影响。

## 怎么切换到 125b

先确认三件事：

1. 125b 的准确模型 ID；
2. 125b 的网关地址；
3. 125b 是否支持工具调用。

然后编辑：

```text
<SDK_HOME>\runtime\cordis.patch.yml
```

### 第一处：默认模型

```yaml
- id: agent-default-model
  config:
    provider: bole
    model: <125b-model-id>
```

### 第二处：网关地址

```yaml
baseURL: 'http://<125b-gateway>:<port>/v1'
```

### 第三处：模型列表

```yaml
models:
  - id: <125b-model-id>
```

这三处要保持一致：

```text
agent-default-model.model
models[].id
```

如果两处模型 ID 不一致，SDK 初始化或请求时会报模型不存在。

【截图位置 2】  
打开 `runtime\cordis.patch.yml`，截这三处并圈出来：

```text
model: <125b-model-id>
baseURL: 'http://<125b-gateway>:<port>/v1'
- id: <125b-model-id>
```

### 第四处：API key

如果新网关需要 key，编辑：

```text
<SDK_HOME>\home\.env
```

```text
BOLE_API_KEY=<新 key>
```

不需要 key 就保持 `local`。

### 重启并验证

关闭 GUI/控制台，再重新打开。然后执行：

```powershell
cd <SDK_HOME>\samples\console-c

.\bole_console.exe "你好"
```

看到：

```text
[ready]
[answer] ...
[task] finished
```

就说明模型切换成功。

【截图位置 3】  
截控制台输出，重点拍：

```text
[ready]
[answer] ...
[task] finished
```

如果客户用 GUI，也可以截：

```text
输入框里的问题
状态栏 finished
结果区的回答
```

【截图位置 4】  
截 GUI 连续跑两个任务后的结果区，证明：

- 第二个任务没有带第一个任务的旧日志；
- 没有原始 JSON；
- 状态栏是 `finished`。

## 注意事项

- 删除重装后，`runtime\cordis.patch.yml` 会恢复成默认值，需要重新填 125b 的模型 ID 和网关；
- RSI 在 125b 下大概率不可用，按约定接受；
- 125b 如果不支持 function calling，MCP 工具无法调用，普通聊天仍可用；
- 旧的 r6 包不要只替换 `bole_gui.exe`，这次改了 DLL、插件和示例，直接换 r7 完整包；
- 更新前建议备份 `home`，尤其是客户自己加过 MCP 配置的情况。

## 包信息

```text
bole-offline-r7-final.zip
SHA256: 7aa149b40fdf185890ec5652bc2ba144fb6ef970c9e7a2e372f10024ff692fe2
tag: bole-m0-20261109-r7
```
