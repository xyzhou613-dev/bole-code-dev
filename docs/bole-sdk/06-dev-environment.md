# 06 开发环境（2026-10-22）

状态：Linux/WSL 开发环境约定；Windows 构建环境暂缓（按 10-22 口径，先文档与分工）。本文只写开发侧，不替代 `07-offline-package.md` 的交付布局。

## 一、路径约定

本文不写死任何本机路径；命令里的变量由开发者按自己的目录布局设置：

| 变量 | 含义 |
| --- | --- |
| `$REPO` / `<repo>` | 本 git 仓库（只有 `packages/` 子树，无根 workspace、无 `node_modules`） |
| `$HARNESS_WORKSPACE` / `<harness-workspace>` | 完整 harness monorepo（pnpm workspace、已构建 `lib/`、`node_modules`） |
| `$DEV_HOME` / `<dev-home>` | 临时 `DSH_HOME`（不要用默认用户目录） |
| `$PATCH_FILE` / `<patch-file>` | 开发期 patch 文件 |
| `<smoke-script>` | 本地冒烟脚本 |

- 所有编辑在 `$REPO`；构建、测试、跑运行时在 `$HARNESS_WORKSPACE`。
- 不要往 `$HARNESS_WORKSPACE/packages/` 新建包：同步脚本可能覆盖该目录；需要临时放入的包用软链，或在源仓改完再同步。
- 文档里的占位符在复制命令时替换成自己的路径；提交到仓库的文档不写具体路径。

## 二、C SDK（B 轨负责）

B 轨按 04/05/07 实现 `native/src`、`native/tests`、`native/CMakeLists.txt`；本会话不提供交付实现。`native/include/bole_agent.h` 是冻结接口；`native/` 当前只有该头文件与 README。`prototypes/native-c-sdk/` 是未授权参考，不属于交付物，B 可决定采用/丢弃/重写。

目标构建方式（B 实现后）：

```bash
cmake -S "$REPO/native" -B "$REPO/build/native" -DCMAKE_BUILD_TYPE=Debug
cmake --build "$REPO/build/native" -j4
ctest --test-dir "$REPO/build/native" --output-on-failure
```

- Linux 用 `platform_posix.c`；Windows 用 `platform_win32.c`（CreatePipe/CreateProcessW/Job Object），CMake 在 WIN32 上缺该文件时应明确报错。
- 测试覆盖（B 实现时）：JSON 扫描/构造、初始化/就绪、建会话、提交、事件、取消错误码、RSI、轨迹导出、释放。
- 假运行时通过编译定义 `FAKE_RUNTIME_PATH` 注入；测试自己建 `runtime_dir`、`dsh_home`、`cordis.patch.yml` 和 `node.exe` 包装脚本。

## 三、桥接插件

包在 `$REPO/runtime/packages/sdk-server`。开发期不构建，直接让 `tsx` 加载 `.ts`。

### 开发期依赖链接

```bash
mkdir -p "$REPO/runtime/packages/sdk-server/node_modules"
ln -sfn "$HARNESS_WORKSPACE/python/sdk-runtime/node_modules/@deepseek-ai" \
  "$REPO/runtime/packages/sdk-server/node_modules/@deepseek-ai"
ln -sfn "$HARNESS_WORKSPACE/node_modules/@types" \
  "$REPO/runtime/packages/sdk-server/node_modules/@types"
```

- 这两个软链只用于开发，已在 `.gitignore` 内，不要提交。
- 如果宿主工作区没有 `python/sdk-runtime`，就指向任意一份锁定 `0.1.7-alpha.2` 的 `@deepseek-ai` 依赖树（例如交付运行时的 `node_modules`）；插件只需要官方包与 `@types/node`。
- 类型检查：`"$HARNESS_WORKSPACE/node_modules/.bin/tsc" -p "$REPO/runtime/packages/sdk-server/tsconfig.json" --noEmit`。

### 跑真实 `sdk` profile

```bash
cd "$HARNESS_WORKSPACE"
env -u DSH_HOME DSH_HOME="$DEV_HOME" DSH_TELEMETRY_DISABLED=1 \
  DSH_PERMISSION_MODE=danger-full-access \
  pnpm dsh --profile sdk --patch "$PATCH_FILE"
```

`$PATCH_FILE` 内容：

```yaml
- id: sdk-jsonrpc-server
  disabled: true
- insert:
    - id: bole-sdk-server
      name: '<absolute path to $REPO/runtime/packages/sdk-server/src/index.ts>'
      inject: [sdkAppStartup, loader]
```

- Loader 不展开 shell 变量；在本地生成 patch 时替换成自己的绝对路径，不要把该文件提交进仓库。
- 构建成 JS 后，用 `node "$HARNESS_WORKSPACE/apps/cli/lib/bin.js"`，插件 `name` 改成 `@bole/sdk-server`，由运行时树的 `node_modules` 解析。
- 冒烟期望：stderr 出现 `[bole-sdk-server] loaded`；`initialize` / `session/prompt` / `turn/end` / `shutdown` 全链路；退出码 0。

## 四、本地模型（无内网时）

- **Messages / SSE**（`deepseek-official` 兜底）：`packages/test-support/llm-mock-server`；或自己起一个返回 Anthropic Messages SSE 的本地 HTTP 服务（地址由开发者自定，文档用 `<mock-endpoint>` 占位），`initialize` 用 `provider: "deepseek-official"`，设 `DEEPSEEK_API_KEY=mock-key`、`DEEPSEEK_BASE_URL=<mock-endpoint>`。
- **OpenAI chat-completions**（`bole` 路由）：`packages/llm/llm-pi-ai/tests/mock-server.ts`；用 `--patch` 整体替换 `llm-pi-ai.providers.bole.baseURL` 指本地 mock。
- 交付目标的内网网关地址由部署配置提供，不在文档里写死；真实模型效果只能在现场/内网机复验。

## 五、环境变量与坑

- **永远显式设 `DSH_HOME="$DEV_HOME"`**，否则落到默认用户目录（可能含真实凭据与别人的 profile）。
- 开发用 `DSH_PERMISSION_MODE=danger-full-access`，避免沙箱后端不可用时工具硬失败；交付默认 `workspace-write` + 自检。
- `DSH_TELEMETRY_DISABLED=1` 兜底；补丁层仍会关 telemetry 行。
- `pnpm --filter <pkg> test` **静默空跑**（所有包都没有 `scripts`）；用根目录 `vitest run <path>`。
- 插件裸名解析失败只是 warning，进程照常 exit 0；必看 stderr 的 `did not activate`。
- patch 的 `config` 是**整体替换**，不是深合并。
- `--patch` 里的相对插件路径相对 patch 文件，不是 cwd；开发期用本地生成的绝对路径。
- `tsconfig.base.json` 当前 stale（`skill-bole-experience` 缺别名）；`constraints` 当前 3 条违规；`sdk/server` 有 2 个 fork 改名导致的既有红，不是本次改动。
- 不提交 `node_modules`、构建产物、临时 `DSH_HOME`、密钥、本地 patch。

## 六、Windows（暂缓）

- 正式件需要 MSVC Build Tools 2022 + pnpm；C 库用 `/MT` 静态运行库。
- WSL 侧可装 `mingw-w64` 交叉编译做语法/链接检查；`platform_win32.c` 必须用 `CreatePipe` + `CreateProcessW` + Job Object（`KILL_ON_JOB_CLOSE`）实现。
- 需要至少两台 Windows：一台构建/日常，一台干净验收 VM；现场安全策略（未签名 DLL / EDR）仍未知。
