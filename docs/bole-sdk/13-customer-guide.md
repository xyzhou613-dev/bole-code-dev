# 伯乐 Agent SDK 使用说明

本文只说明怎么把 SDK 跑起来。按顺序做 5 步即可；第 6 章是两个 MCP 工具示例；新增/安装步骤在独立文档中。

## 第 1 步：解压

把 `bole-offline-r6-final.zip` 解压到一个目录，例如：

```text
D:\bole-agent-sdk-win-x64
```

解压后应能看到：

```text
bin\        SDK 的 dll/lib/头文件
runtime\    随包运行时
samples\    控制台和界面示例
tools\      玩具 MCP
install.ps1
verify.ps1
```

![解压后的目录](assets/image-20261023103741537.png)

## 第 2 步：安装

打开 PowerShell，进入解压目录：

```powershell
cd D:\bole-agent-sdk-win-x64

.\install.ps1 `
  -Target D:\apps\BoleAgentSdk `
  -ModelGateway <gateway>
```

说明：

- `-Target` 是 SDK 的安装目录，**不要填解压目录**；
- `<gateway>` 由部署方提供，例如 `http://<网关地址>:8009`，不要带 `/v1`；
- 不需要管理员权限，不会写注册表。

成功会显示：

```text
installed to D:\apps\BoleAgentSdk
```

![install 执行结果](assets/image-20261023104845907.png)

## 第 3 步：检查

```powershell
.\verify.ps1 -Root D:\apps\BoleAgentSdk
```

最后一行应为：

```text
verify ok
```

![verify 通过](assets/image-20261023105001708.png)

## 第 4 步：运行示例

### 方式 A：控制台

```powershell
cd D:\apps\BoleAgentSdk\samples\console-c

.\bole_console.exe "你好"
```

看到 `[answer]` 和 `[task] finished` 就表示正常运行。

```powershell
.\bole_console.exe `
  "调用玩具工具 mcp__toy__add 计算 1+2，然后用结果回答。"
```

应看到工具调用和结果 `3`。

### 方式 B：界面

```powershell
cd D:\apps\BoleAgentSdk\samples\gui-win32

.\bole_gui.exe
```

在输入框输入：

```text
调用玩具工具 mcp__toy__add 计算 1+2，然后用结果回答。
```

点 **Submit**，状态变为 `finished` 即成功。

![GUI 正常运行](assets/image-20261023113455842.png)

需要 RSI 时，勾选界面上的 **RSI** 再提交；控制台对应参数是 `--rsi`。

## 第 5 步：接入自己的程序

1. 把 `bin\bole_agent.h` 加入 include；
2. 链接 `bin\bole_agent.lib`；
3. 运行时可执行文件同目录放 `bole_agent.dll`；
4. 按 `docs/02-integration.md` 的示例调用。

更完整的接口、错误码和部署说明在包内 `docs/`。

## 第 6 章：MCP 工具示例

### 6.1 示例一：调用自带的 12 个工具

SDK 安装包内已经带了一个玩具 MCP，注册了 12 个工具：

```text
add, subtract, multiply, echo, now, word_count,
sha256, json_get, uuid, sleep, big_text, fail
```

选一个有代表性的 `mcp__toy__add` 做截图即可：

```powershell
cd D:\apps\BoleAgentSdk\samples\console-c

.\bole_console.exe `
  "调用玩具工具 mcp__toy__add 计算 1+2，然后用结果回答。"
```

输出中应出现：

```text
[tool] start ... "tool":"mcp__toy__add","args":{"a":1,"b":2}
[tool] done  ... "isError":false,"resultSummary":"3"
[task] finished
```

![image-20261023151530802](assets/image-20261023151530802.png)

如果需要证明 12 个工具都已注册：

```powershell
D:\apps\BoleAgentSdk\runtime\node.exe `
  D:\apps\BoleAgentSdk\tools\toy-mcp\smoke.mjs
```

期望：

```text
PASS toy MCP: 12 tools
```

### 6.2 示例二：调用外部开源 MCP 服务

本示例使用官方开源的 **Everything Server**：

- 文档：<https://modelcontextprotocol.io/examples>
- 源码：<https://github.com/modelcontextprotocol/servers/tree/main/src/everything>
- npm 包：`@modelcontextprotocol/server-everything@2.0.0`

它是独立于 SDK 的第三方 MCP 进程，免费、开源，通过 stdio 接入。

安装和接入步骤见《外部 MCP 服务安装指南》（随客户版压缩包一起提供，
`06-external-mcp-install.md`）。按该文档安装、配置并重启运行时后，再执行：

```powershell
cd D:\apps\BoleAgentSdk\samples\console-c

.\bole_console.exe `
  "Use the external MCP tool mcp__everything__echo with message hello-external-mcp, then answer with its output."
```

输出中应出现：

```text
[tool] start ... "tool":"mcp__everything__echo"
[tool] done  ... "resultSummary":"Echo: hello-external-mcp"
[task] finished
```

截图时框住：

```text
① mcp__everything__echo
② Echo: hello-external-mcp
③ finished
```

> 【贴图位置 2】把外部 MCP 截图放在这里；用红框圈住
> `mcp__everything__echo`、`Echo: hello-external-mcp`、`finished`。

> 新增其他 MCP 服务：见《MCP 工具接入与示例》
> （`05-mcp-integration.md`）。
>
> 安装外部 MCP 服务：见《外部 MCP 服务安装指南》
> （`06-external-mcp-install.md`）。
>
> 本文只展示两个示例，不展开安装和配置步骤。

## 常见问题

**Q：`[ready]` 后一直等到 `timeout waiting for the task`？**

`[ready]` 表示 SDK 和本地运行时已经启动。此时通常是模型网关未返回结果，请联系部署方确认网关和模型服务。

**Q：提示 `failed to import @bole/sdk-server`？**

没有走 `install.ps1`，或安装目录的 `home\profiles\sdk\node_modules` 不完整。重新执行第 2 步。

**Q：玩具工具不出现？**

确认 `home\cordis.patch.yml` 已由 `install.ps1` 生成；新增 MCP 工具需要修改该文件并重启运行时。

## 说明

- 支持 Windows 10 / 11 x64。
- 本版本不产生“验证阶段”事件；任务是否结束以 `TASK_FINISHED` / `TASK_FAILED` 为准。
- MCP 工具只支持配置文件声明 + 重启，不支持运行时注册。
