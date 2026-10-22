# Bole SDK 运行时组装

本目录是交付侧运行时的源码/模板，不是 `dsh` 包组，也不进入根 workspace。离线包的最终布局、安装脚本与自检由 B 轨按 [07-offline-package.md](../docs/bole-sdk/07-offline-package.md) 实现；这里只放 A 轨负责的运行时组成。

## 组成

| 路径 | 作用 |
| --- | --- |
| `packages/sdk-server/` | `@bole/sdk-server` 插件（协议、会话、事件、RSI、轨迹） |
| `packages/sdk-app/cordis.patch.yml` | profile 补丁层：preset 面、关外网、审批、模型行、插件行 |
| `config/` | 站点配置模板（模型地址、MCP server、`home/.env`） |
| `dev/` | 开发脚手架（冒烟、mock 模型、fixture 捕获、E2E），不随包 |

## 最小组装步骤

1. 安装锁定的 `@deepseek-ai/dsh@0.1.7-alpha.2` 运行时树。
2. 构建 `@bole/sdk-server`，并把 `@bole/sdk-server` 与 `@deepseek-ai/dsh-skill-bole-experience` 装进 **SDK profile 的 `node_modules`**（`<DSH_HOME>/profiles/sdk/node_modules`）。只放运行时树会 `failed to import`（见 09 计划 F9）。
3. 复制 `packages/sdk-app/cordis.patch.yml` 到 `<runtime>/cordis.patch.yml`。
4. 复制 `config/home.env.template` 到 `<DSH_HOME>/.env`；按 `config/home.patch.yml.template` 生成 `<DSH_HOME>/cordis.patch.yml`。
5. 把 `examples/toy-mcp/` 复制到 `<package>/tools/toy-mcp`，按需在 home patch 里声明 MCP server。
6. 启动：`<runtime>/node.exe <runtime>/node_modules/@deepseek-ai/dsh/lib/bin.js --profile sdk --patch <runtime>/cordis.patch.yml`。

## 本地开发

见 [dev/README.md](dev/README.md)：`npm --prefix runtime/dev install`、`node runtime/dev/build-experience.mjs`、`node runtime/dev/smoke.mjs`、`node runtime/dev/session.mjs`、`node runtime/dev/trace.mjs`、`node runtime/dev/e2e.mjs`。

## 版本输入

`version.inputs.json` 给 B 的 `VERSION.json` 提供 A 轨组件版本；最终 `VERSION.json` 由打包脚本合并生成。
