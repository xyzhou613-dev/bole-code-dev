# 玩具 MCP server

零依赖、无外网的 stdio MCP server，用于 SDK 验收与联调。当前版本 `0.2.0`，提供以下无持久化副作用的工具：

| 工具 | 参数 | 返回 |
| --- | --- | --- |
| `add` | `a: number`、`b: number` | 两数之和 |
| `subtract` | `a`、`b` | `a - b` |
| `multiply` | `a`、`b` | `a * b` |
| `echo` | `text: string` | 原样返回 |
| `now` | 无 | 当前时间 ISO-8601 |
| `word_count` | `text: string` | 字符数、词数、行数 JSON |
| `sha256` | `text: string` | UTF-8 文本的 SHA-256 hex |
| `json_get` | `json: string`、`path: string` | 按点路径取值，例如 `a.b.0` |
| `uuid` | 无 | 随机 UUID v4 |
| `sleep` | `ms: 0-5000` | 等待后返回，用于取消/超时测试 |
| `big_text` | `bytes: 1-65536` | 重复 ASCII，用于结果截断测试 |
| `fail` | `message: string` | 固定返回 MCP 工具错误 |

模型看到的工具名是 `mcp__toy__add`、`mcp__toy__subtract`、`mcp__toy__multiply`、
`mcp__toy__echo`、`mcp__toy__now`、`mcp__toy__word_count`、`mcp__toy__sha256`、
`mcp__toy__json_get`、`mcp__toy__uuid`、`mcp__toy__sleep`、`mcp__toy__big_text`、
`mcp__toy__fail`。

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

改配置后需要重启运行时。

## 手工验证

```sh
printf '%s\n' \
  '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"test","version":"1"}}}' \
  '{"jsonrpc":"2.0","id":2,"method":"tools/list"}' \
  '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"add","arguments":{"a":2,"b":3}}}' \
  '{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"json_get","arguments":{"json":"{\"a\":{\"b\":[7,8]}}","path":"a.b.0"}}}' \
  | node examples/toy-mcp/server.mjs
```

`add` 返回 `content: [{"type":"text","text":"5"}]`；`json_get` 返回 `7`。

一键校验全部工具：

```sh
node examples/toy-mcp/smoke.mjs
```

## 边界

- `sleep` 和 `big_text` 是有上限的测试探针，不写文件、不访问网络。
- `fail` 只返回工具错误，不崩溃 server。
- 单个 MCP server 崩溃只影响它的工具调用；运行时继续（`failOnStartupError: false`）。
- 不用于生产；验收包按目录整体复制即可。
