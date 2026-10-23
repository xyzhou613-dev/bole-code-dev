# packaging/ — 离线包（B6）

目标机无外网、无 Node、无 VS 运行库；解压即用、不写注册表、不需要管理员权限。构建机前置与完整命令见 [11-packaging-runbook](../docs/bole-sdk/11-packaging-runbook.md)。

| 脚本 | 作用 |
|---|---|
| `build-offline.ps1` | 在 Windows 构建机上装配 `bole-agent-sdk-win-x64/`，生成 `VERSION.json` 与 `SHA256SUMS`，打 zip |
| `install.ps1` | 校验哈希后复制到 `%LOCALAPPDATA%\BoleAgentSdk` |
| `verify.ps1` | 安装后自检：文件、版本、profile 包、`--dump-config` 组合 |
| `acceptance.ps1` | 干净 VM 一条命令：安装、真实网关 C2、B8 自检、GUI 启动、证据收集；开发机复测可加 `-SkipCleanEnvironmentCheck` |

目录约定见 `docs/bole-sdk/07-offline-package.md`：

```
bole-agent-sdk-win-x64/
├── bin/       bole_agent.dll/.lib/.h
├── runtime/   node.exe + dsh 依赖树 + cordis.patch.yml
├── home/      可写会话目录 + profiles/sdk + .env + MCP patch
├── tools/     玩具 MCP（12 个工具）
├── samples/   console-c（含 bole_selfcheck）、gui-win32
├── docs/      四份交付文档
├── VERSION.json
├── SHA256SUMS
├── install.ps1
├── verify.ps1
└── acceptance.ps1
```

构建参数：

- `-PluginTarball` / `-ExperienceTarball`：默认取 `runtime/dist/` 最新包（先跑 `runtime/dev/pack.mjs`）；
- `-ModelGateway`：写入 `runtime/cordis.patch.yml`（C 的 `--patch` 是最高层，不能放 home）；
- `-ConsoleExe` / `-GuiExe`：可选，复制已构建的示例到 `samples/`。

包内会预创建 `home/profiles/sdk`，并把 `@bole/sdk-server` 与经验包装进
`home/profiles/sdk/node_modules`（F9）。`install.ps1 -ModelGateway ...` 可在现场再次改写
`runtime/cordis.patch.yml`；`verify.ps1` 会检查 profile 包与 `--dump-config` 组合。

验证状态（2026-10-23）：Windows + MSVC BuildTools 2022（`/MT`）构建
`bole_agent.dll/.lib`；`build-offline.ps1 → install.ps1 → verify.ps1 → acceptance.ps1`
在开发机全过（真实网关，acceptance 含 console C2、B8 selfcheck、GUI 启动、进程检查）。
MSVC `ctest` 的 json/error/platform/fixtures 4/4 通过；toy MCP 12 个工具。
r6 final zip 已生成，SHA256 `aaeeafd8c7e3a520743fd367ce8955b00923a500a453b7d6b604dc1f30dbcc0d`；开发机 acceptance 已通过，待测试团队 clean VM 验收。完整构建步骤见 [11-packaging-runbook](../docs/bole-sdk/11-packaging-runbook.md)。

