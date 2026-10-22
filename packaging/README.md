# packaging/ — 离线包（B6）

目标机无外网、无 Node、无 VS 运行库；解压即用、不写注册表、不需要管理员权限。

| 脚本 | 作用 |
|---|---|
| `build-offline.ps1` | 在 Windows 构建机上装配 `bole-agent-sdk-win-x64/`，生成 `VERSION.json` 与 `SHA256SUMS`，打 zip |
| `install.ps1` | 校验哈希后复制到 `%LOCALAPPDATA%\BoleAgentSdk` |
| `verify.ps1` | 安装后自检：文件齐、dsh 版本锁定 |

目录约定见 `docs/bole-sdk/07-offline-package.md`：

```
bole-agent-sdk-win-x64/
├── bin/       bole_agent.dll/.lib/.h
├── runtime/   node.exe + dsh 依赖树 + cordis.patch.yml + home/
├── tools/     玩具 MCP
├── samples/   console-c、gui-win32
├── docs/      四份交付文档
├── VERSION.json
├── SHA256SUMS
└── install.ps1
```

构建参数：

- `-PluginTarball` / `-ExperienceTarball`：默认取 `runtime/dist/` 最新包（先跑 `runtime/dev/pack.mjs`）；
- `-ModelGateway`：写入 `runtime/cordis.patch.yml`（C 的 `--patch` 是最高层，不能放 home）；
- `-ConsoleExe` / `-GuiExe`：可选，复制已构建的示例到 `samples/`。

包内会预创建 `home/profiles/sdk`，并把 `@bole/sdk-server` 与经验包装进
`home/profiles/sdk/node_modules`（F9）。`install.ps1 -ModelGateway ...` 可在现场再次改写
`runtime/cordis.patch.yml`；`verify.ps1` 会检查 profile 包与 `--dump-config` 组合。

验证状态（2026-10-22）：在 Windows 主机上用 MinGW 构建的 DLL（临时替代 `.lib`）跑了
`build-offline.ps1` → `install.ps1` → `verify.ps1`，verify 通过；从安装目录运行
`console-c` 完成 `mcp__toy__add(1,2) -> 3`，约 1.9s。正式交付仍需用 MSVC `/MT` 产出
`bole_agent.lib` 后在 Windows 构建机复跑同一流程。

