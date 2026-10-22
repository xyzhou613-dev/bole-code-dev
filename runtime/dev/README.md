# Bole SDK 开发脚手架

本目录只用于开发与验收演练，不进入离线包。脚本按自身位置解析 `<repo>`，不依赖固定工作目录。

## 前置

- Node 22/24；`npm`。
- 一个锁在 `0.1.7-alpha.2` 的运行时树（`$BOLE_DEV_RUNTIME`，默认 `$HOME/.bole-dev/runtime`），入口为 `<runtime>/node_modules/@deepseek-ai/dsh/lib/bin.js`。

## 准备

```sh
# 1. 运行时树（一次性）
mkdir -p "$BOLE_DEV_RUNTIME"
npm --prefix "$BOLE_DEV_RUNTIME" init -y
npm --prefix "$BOLE_DEV_RUNTIME" install @deepseek-ai/dsh@0.1.7-alpha.2 --no-audit --no-fund

# 2. 开发工具（一次性）
npm --prefix runtime/dev install

# 3. 构建插件并链接进运行时（可重复）
npm --prefix runtime/dev run build
node -e "import('./runtime/dev/shared/runtime.mjs').then(m => m.ensureDevLinks())"
```

`ensureDevLinks()` 建立三个软链（都在 `.gitignore` 内）：

- `runtime/packages/sdk-server/node_modules/@deepseek-ai` → 运行时树的官方包，供 tsc 解析；
- `runtime/packages/sdk-server/node_modules/@types` → `runtime/dev/node_modules/@types`；
- `<runtime>/node_modules/@bole/sdk-server` → 插件包，供 `@bole/sdk-server` 裸名解析。

## 冒烟

```sh
node runtime/dev/smoke.mjs
```

断言：stdout 首帧是 JSON-RPC 2.0；`initialize` 返回 `serverInfo`；`shutdown` 后退出码 0；stderr 有 `[bole-sdk-server] loaded` 且没有 `did not activate`。

## mock 模型

```sh
# 前台起一个随机端口的 OpenAI chat-completions mock（打印 MOCK_MODEL_URL）
node runtime/dev/mock-model.mjs

# 工具调用脚本
MOCK_TOOL=add MOCK_TOOL_ARGS='{"a":1,"b":2}' node runtime/dev/mock-model.mjs
```

`capture.mjs` 会自动起 mock，并写一个只覆盖 `llm-pi-ai` 的临时 patch（`runtime/dev/.dev/mock.patch.yml`），再叠加到交付 patch 之后。

## 捕获

```sh
node runtime/dev/capture.mjs --scenario happy-tool --text "用一句话介绍你自己"
```

输出 `runtime/.dev/<scenario>.jsonl`，一行一个原始 stdout 帧。fixture 清洗、占位替换与 manifest 在 A3 补齐；正式 fixture 放 `native/tests/fixtures/`。

## 注意

- 所有运行使用临时 `DSH_HOME`，不要用默认用户目录。
- 开发机用 `DSH_PERMISSION_MODE=danger-full-access`；目标机用 `workspace-write` + 自检。
- 开发期 patch 里的插件名按运行时树的软链解析；本目录不提交任何绝对路径。
- `runtime/dev/.dev/` 与 `runtime/dist/` 是本地产物，不提交。
