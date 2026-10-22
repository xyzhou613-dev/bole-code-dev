# 站点配置模板

本目录放**运行时现场配置**的模板，由离线包安装脚本复制到 `<包根>/home/`（即 `DSH_HOME`）。模板不进入交付 patch；现场只需要改这里，不必改只读的运行时树。

## `home.env.template`

复制为 `<DSH_HOME>/.env`：

```sh
BOLE_API_KEY=local
```

`0.1.7-alpha.2` 的发布包 `@deepseek-ai/dsh-llm-pi-ai` 通过 `apiKeyEnv` 解析密钥，**忽略**配置里的内联 `apiKey`（仓库源码里两种都支持，发布构建只保留前者）。因此交付 patch 使用：

```yaml
- id: llm-pi-ai
  config:
    providers:
      bole:
        apiKeyEnv: BOLE_API_KEY
```

`home/.env` 由 `dsh` 启动时作为 user-env 层加载，不需要改 C 库的启动环境变量列表，也不把密钥写进只读的运行时树。私有网关接受任意 key，这里的 `local` 只是占位。

## 模型地址与 MCP

模型网关地址与调用方声明的 MCP server 放在 `<DSH_HOME>/cordis.patch.yml`（home patch 层，高于 profile）。模板在 A6/A7 补齐：

- `home.patch.yml.template`：`llm-pi-ai.providers.bole.baseURL`、额外 `@deepseek-ai/dsh-mcp-client` 行；
- `mcp.servers.example.yml`：stdio / streamable-http 两种写法。

## 注意

- 不要把真实的网关地址、密钥、机器名写进仓库；模板里用占位值。
- 改完 home 层配置需要重启运行时；v1 不做运行中注册。
