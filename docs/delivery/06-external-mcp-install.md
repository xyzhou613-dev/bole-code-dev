# 外部 MCP 服务安装指南

本文以官方开源的 **Everything Server** 为例，说明如何安装一个外部 MCP 服务，
并接入伯乐 Agent SDK。

- MCP 示例列表：<https://modelcontextprotocol.io/examples>
- Everything Server 源码：<https://github.com/modelcontextprotocol/servers/tree/main/src/everything>
- npm 包：`@modelcontextprotocol/server-everything@2.0.0`

> 本文只负责“安装 + 接入 + 验证”；客户版使用说明里只展示调用效果和截图位置。

## 1. 准备

需要：

- Windows 10/11 x64；
- Node.js 24+（安装 npm 包用）；
- 可以访问 npm，或提前在构建机装好再整个目录拷贝；
- 已安装 SDK，例如 `D:\apps\BoleAgentSdk`。

## 2. 安装 Everything Server

```powershell
mkdir D:\mcp-servers\everything
cd D:\mcp-servers\everything
npm init -y
npm install @modelcontextprotocol/server-everything@2.0.0
```

确认入口文件存在：

```text
D:\mcp-servers\everything\node_modules\@modelcontextprotocol\server-everything\dist\index.js
```

### 目标机不能联网

在能联网的构建机执行上面的 `npm install`，然后把整个
`D:\mcp-servers\everything` 目录复制到目标机相同或任意路径。运行时不需要联网。

## 3. 加入 SDK 配置

编辑安装目录下的：

```text
D:\apps\BoleAgentSdk\home\cordis.patch.yml
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

字段说明：

| 字段 | 说明 |
| --- | --- |
| `id` | 配置项唯一名 |
| `serverName` | 工具前缀，最终工具名为 `mcp__everything__<tool>` |
| `command` | 用 SDK 自带 node 运行 |
| `args` | Everything Server 的入口文件 |
| `cwd` | 服务工作目录，建议绝对路径 |
| `toolCallTimeoutMs` | 单次工具调用超时 |
| `failOnStartupError` | 启动失败是否让运行时失败；演示用 `false` |

路径建议使用 `/`，或者 YAML 中用双反斜杠。

## 4. 重启运行时

关闭所有 GUI 和控制台，再新开进程。MCP 配置只在运行时启动时读取，**改完必须重启**。

## 5. 验证调用

```powershell
cd D:\apps\BoleAgentSdk\samples\console-c

.\bole_console.exe `
  "Use the external MCP tool mcp__everything__echo with message hello-external-mcp, then answer with its output."
```

期望输出：

```text
[tool] start ... "tool":"mcp__everything__echo"
[tool] done  ... "isError":false,"resultSummary":"Echo: hello-external-mcp"
[task] finished
```

模型回答中应出现：

```text
Echo: hello-external-mcp
```

## 6. 卸载

1. 删除 `home\cordis.patch.yml` 里的 `mcp-everything` 配置项；
2. 重启运行时；
3. 删除 `D:\mcp-servers\everything` 目录。

## 7. 常见问题

| 现象 | 处理 |
| --- | --- |
| 工具没有出现 | 检查配置项是否在 `- insert:` 下、缩进是否正确；是否重启 |
| 服务启动失败 | 手动执行 `node dist/index.js`，确认 Node 24+、路径正确 |
| 工具调用超时 | 加大 `toolCallTimeoutMs`；检查服务是否卡住 |
| `failed to import` | 检查 `command`/`args` 是否是实际入口文件 |
| 路径含空格 | YAML 中用引号包住整个路径 |

## 8. 换成其他 MCP 服务

其他 stdio MCP 服务同样处理：

1. 安装服务，拿到启动命令；
2. 在 `home\cordis.patch.yml` 增加一项；
3. 修改 `serverName`、`command`、`args`、`cwd`；
4. 重启；
5. 用 `mcp__<serverName>__<tool>` 调用。

官方参考服务还有 `filesystem`、`time`、`memory`、`git` 等，可按业务替换。
接入前确认服务可信、许可证允许分发，并避免把密钥写进 `args`。
