# MCP 工具接入与示例

本文给两类示例，并说明如何新增一个 MCP 服务：

1. SDK 调用自带的 12 个工具（选一个代表截图）；
2. SDK 调用外部开源 MCP 服务（官方 Everything Server）；
3. 新增 MCP 服务的配置步骤。

文中的 `<SDK_HOME>` 指安装目录，例如 `D:\apps\BoleAgentSdk`；工具名为
`mcp__<serverName>__<toolName>`。

## 1. 示例一：调用自带的 12 个工具

安装包内的 toy MCP 已注册 12 个工具：

| 工具 | 用途 |
| --- | --- |
| `mcp__toy__add` | 两数相加 |
| `mcp__toy__subtract` | 两数相减 |
| `mcp__toy__multiply` | 两数相乘 |
| `mcp__toy__echo` | 回显文本 |
| `mcp__toy__now` | 当前时间 |
| `mcp__toy__word_count` | 字符/单词/行数统计 |
| `mcp__toy__sha256` | 文本 SHA-256 |
| `mcp__toy__json_get` | 按点路径取 JSON 值 |
| `mcp__toy__uuid` | 生成 UUID v4 |
| `mcp__toy__sleep` | 等待指定毫秒 |
| `mcp__toy__big_text` | 返回指定大小文本 |
| `mcp__toy__fail` | 故意返回工具错误，用于异常测试 |

### 1.1 代表任务：add 1+2

在控制台执行：

```powershell
cd <SDK_HOME>\samples\console-c

.\bole_console.exe `
  "调用玩具工具 mcp__toy__add 计算 1+2，然后用结果回答。"
```

输出中会看到：

```text
[tool] start ... "tool":"mcp__toy__add","args":{"a":1,"b":2}
[tool] done  ... "isError":false,"resultSummary":"3","tool":"mcp__toy__add"
[task] finished
```

### 1.2 截图位置

如果截控制台，框住这三行：

```text
第 1 处：[tool] start ... mcp__toy__add
第 2 处：[tool] done  ... "resultSummary":"3"
第 3 处：[task] finished
```

如果截 GUI，框住：

```text
输入框：调用玩具工具 mcp__toy__add 计算 1+2，然后用结果回答。
状态栏：finished
结果区：mcp__toy__add 的返回结果 3，或模型回答 1 + 2 = 3
```

### 1.3 证明 12 个工具已注册

```powershell
<SDK_HOME>\runtime\node.exe <SDK_HOME>\tools\toy-mcp\smoke.mjs
```

期望：

```text
PASS toy MCP: 12 tools
```

截图时可以把这行和 `add` 的调用放在同一张图里；不需要把 12 个工具都跑一遍。

> `fail` 是故意返回错误的工具，不要拿它作为“成功调用”的截图。

## 2. 示例二：调用外部开源 MCP 服务

选用官方开源参考服务器 **Everything Server**：

- 文档：<https://modelcontextprotocol.io/examples>
- 源码：<https://github.com/modelcontextprotocol/servers/tree/main/src/everything>
- npm 包：`@modelcontextprotocol/server-everything@2.0.0`
- 免费、开源，适合验证客户端能否启动并调用外部 MCP。

它是独立于本 SDK 的 Node 进程，通过 stdio 接入，不需要公网 API。

### 2.1 安装服务

在能联网的构建机（或目标机）上：

```powershell
mkdir D:\mcp-servers\everything
cd D:\mcp-servers\everything
npm init -y
npm install @modelcontextprotocol/server-everything@2.0.0
```

需要 Node.js 24+；也可以用 SDK 自带的 `runtime\node.exe` 运行，但安装依赖仍需要 npm。

### 2.2 加入配置

编辑：

```text
<SDK_HOME>\home\cordis.patch.yml
```

在已有 `- insert:` 列表下追加一项（注意缩进，不要再写一个 `- insert:`）：

```yaml
    - id: mcp-everything
      name: '@deepseek-ai/dsh-mcp-client'
      config:
        transport: stdio
        serverName: everything
        command: 'D:/apps/BoleAgentSdk/runtime/node.exe'
        args: ['D:/mcp-servers/everything/node_modules/@modelcontextprotocol/server-everything/dist/index.js']
        cwd: 'D:/mcp-servers/everything'
        toolCallTimeoutMs: 15000
        failOnStartupError: false
```

把 `command`、`args` 换成自己的实际路径；Windows 路径用 `/` 或双反斜杠。

### 2.3 重启运行时

关闭 GUI/控制台，再新开一个进程。MCP 配置只在运行时启动时读取，**改完必须重启**。

### 2.4 验证调用

```powershell
cd <SDK_HOME>\samples\console-c

.\bole_console.exe `
  "Use the external MCP tool mcp__everything__echo with message hello-external-mcp, then answer with its output."
```

应看到：

```text
[tool] start ... "tool":"mcp__everything__echo","args":{"message":"hello-external-mcp"}
[tool] done  ... "isError":false,"resultSummary":"Echo: hello-external-mcp"
[task] finished
```

模型回答中会出现：

```text
Echo: hello-external-mcp
```

### 2.5 截图位置

```text
第 1 处：[tool] start ... mcp__everything__echo
第 2 处：[tool] done  ... "resultSummary":"Echo: hello-external-mcp"
第 3 处：模型最终回答中的 Echo: hello-external-mcp
```

如果截 GUI，框住输入框里的 `mcp__everything__echo`、状态栏 `finished`、结果区的 `Echo: hello-external-mcp`。

## 3. 如何新增一个 MCP 服务

### 第 1 步：确认服务能独立启动

新 MCP 服务必须能通过 stdio 启动并完成 MCP 握手。先在命令行直接运行它，确认不依赖交互输入、不会一启动就退出。

### 第 2 步：在 home 配置中加一项

文件：`<SDK_HOME>\home\cordis.patch.yml`

模板：

```yaml
    - id: mcp-<name>
      name: '@deepseek-ai/dsh-mcp-client'
      config:
        transport: stdio
        serverName: <name>
        command: '<可执行文件绝对路径>'
        args:
          - '<参数1>'
          - '<参数2>'
        cwd: '<服务工作目录>'
        toolCallTimeoutMs: 15000
        failOnStartupError: false
```

字段说明：

| 字段 | 说明 |
| --- | --- |
| `id` | 配置项唯一名，建议 `mcp-<name>` |
| `serverName` | 工具前缀，最终工具名为 `mcp__<serverName>__<tool>` |
| `command` | 启动可执行文件，例如 `node.exe` 或 `python.exe` |
| `args` | 启动参数，例如服务脚本路径 |
| `cwd` | 服务工作目录；相对路径可能受启动目录影响，建议绝对路径 |
| `toolCallTimeoutMs` | 单次工具调用超时 |
| `failOnStartupError` | 工具启动失败时是否让运行时失败；演示建议 `false` |

### 第 3 步：重启运行时

关闭所有 GUI/console，再启动新进程。旧进程不会热加载新配置。

### 第 4 步：用工具名调用

工具名规则：

```text
mcp__<serverName>__<toolName>
```

例如 `serverName: everything`、工具 `echo`，模型看到的是：

```text
mcp__everything__echo
```

### 第 5 步：确认成功

- 模型输出中出现 `[tool] start` / `[tool] done`；
- `[tool] done` 的 `isError` 为 `false`；
- 工具结果出现在模型回答里；
- 任务最终为 `[task] finished`。

## 4. 常见问题

| 现象 | 处理 |
| --- | --- |
| 工具没有出现 | 检查 `home\cordis.patch.yml` 是否在 `- insert:` 下、缩进是否正确 |
| `failed to import` / 启动失败 | 检查 `command`、`args`、`cwd` 路径；先手动启动服务 |
| 工具调用超时 | 加大 `toolCallTimeoutMs`；检查服务是否卡住 |
| 改了配置不生效 | 没有重启运行时；MCP 只在启动时读取 |
| 工具名不对 | 用 `mcp__<serverName>__<toolName>`，不要用裸工具名 |

## 5. 说明与限制

- 本文示例使用 stdio；远程 HTTP/SSE MCP 是否可用取决于运行时 MCP 客户端实现，接入前需单独验证。
- 外部 MCP 进程拥有当前用户权限；只接入可信服务，不要把密钥写进 `args`。
- 本版本不支持运行中注册/注销 MCP；新增/删除都要改配置并重启。
- `server-everything` 是官方测试服务器，适合验证协议，不是业务服务；业务接入时替换成实际的 MCP 服务。
- 经验包/RSI 与 MCP 工具是两套独立机制，互不影响。
