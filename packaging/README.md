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

已知限制：脚本未在 Windows 上执行过（本机无 PowerShell/Win 工具链）；首次运行需核对
`$Root` 变量拼写与 `runtime/` 的实际布局。
