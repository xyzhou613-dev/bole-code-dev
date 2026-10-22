# 玩具 MCP server

零依赖、无外网的 stdio MCP server，用于 SDK 验收与联调。提供三个无副作用工具：

| 工具 | 参数 | 返回 |
| --- | --- | --- |
| `add` | `a: number`、`b: number` | 两数之和 |
| `echo` | `text: string` | 原样返回 |
| `now` | 无 | 当前时间 ISO-8601 |

启动（约 70ms，不依赖 Node 以外的运行时）：

```sh
node examples/toy-mcp/server.mjs
```

stdout 只走 MCP 协议；诊断写 stderr。

## 在运行时里声明

复制到离线包的 `tools/toy-mcp/`，在 `<DSH_HOME>/cordis.patch.yml` 里加：

```yaml
- insert:
    - id: mcp-toy
      name: '@deepseek-ai/dsh-mcp-client'
      config:
        transport: stdio
        serverName: toy
        command: '<runtime-dir>/node.exe'
        args: ['<package-root>/tools/toy-mcp/server.mjs']
        cwd: '<package-root>'
        toolCallTimeoutMs: 10000
        failOnStartupError: false
```

模型看到的工具名是 `mcp__toy__add`、`mcp__toy__echo`、`mcp__toy__now`。改配置后需要重启运行时。

## 手工验证

```sh
printf '%s\n' \
  '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"test","version":"1"}}}' \
  '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"add","arguments":{"a":2,"b":3}}}' \
  | node examples/toy-mcp/server.mjs
```

期望第二行返回 `content: [{"type":"text","text":"5"}]`。

## 边界

- 不做资源、提示词、订阅或网络访问。
- 单个 MCP server 崩溃只影响它的工具调用；运行时继续（`failOnStartupError: false`）。
- 不用于生产；验收包按目录整体复制即可。
