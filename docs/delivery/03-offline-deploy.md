# 伯乐智能体 SDK 离线部署

本文面向客户现场运维。目标是在**无公网、无开发环境、无 Node、无 Visual Studio 运行库**的 Windows 10 / 11 x64 机器上，解压即用。

## 1. 包内容与目录

```text
bole-agent-sdk-<版本>-win-x64/
├── bin/
│   ├── bole_agent.dll          # C SDK 动态库
│   ├── bole_agent.lib          # 导入库
│   └── bole_agent.h            # 头文件
├── runtime/                    # 运行时（随包只读）
│   ├── node.exe
│   ├── node_modules/           # 运行时依赖树
│   ├── cordis.patch.yml        # 运行时补丁配置
│   ├── config/                 # 模型、MCP 配置模板
│   └── assets/                 # 经验包等随包资产
├── home/                       # 可写运行目录（会话、日志、缓存）
├── tools/
│   └── toy-mcp/                # 验收用玩具 MCP（stdio，无副作用）
├── samples/
│   ├── console-c/              # 控制台示例
│   └── gui-win32/              # Win32 界面示例
├── docs/
│   ├── 01-interface.md
│   ├── 02-integration.md
│   ├── 03-offline-deploy.md
│   └── 04-error-codes.md
├── VERSION.json                # 组件版本清单
├── SHA256SUMS                  # 文件校验清单
├── install.ps1                 # 校验与目录准备
└── selfcheck.ps1               # 启动/握手/会话/工具/取消/轨迹自检
```

**目录职责**

- `runtime/`：只读，安装后不要改；升级时整体替换。
- `home/`：可写，保存会话、日志、缓存；升级时保留。
- `bin/`：SDK 动态库和头文件，供伙伴程序链接/加载。
- `samples/`、`tools/`、`docs/`：示例、验收工具和文档。

## 2. 安装与卸载

### 2.1 安装

1. 把 zip 解压到有写权限的目录，例如 `D:\bole-agent-sdk`。
2. 打开 PowerShell，进入包根目录：

   ```powershell
   cd D:\bole-agent-sdk
   .\install.ps1
   ```

3. `install.ps1` 会：
   - 校验 `SHA256SUMS` 中的所有文件；
   - 检查 `bin/`、`runtime/node.exe`、运行时入口、`runtime/cordis.patch.yml`；
   - 创建/确认 `home/` 可写，不覆盖已有 `home/sessions`；
   - 输出安装路径、版本和下一步命令；
   - 失败时以非零退出码和可读文案结束。

4. 不写注册表、不写系统 `PATH`、不需要管理员权限。

### 2.2 卸载

- 直接删除包目录；
- `home/` 是否保留由用户决定：保留可继续使用已有会话，删除则彻底清理；
- 不残留服务、注册表项或系统级组件。

## 3. 版本与校验

`VERSION.json` 至少包含：

```json
{
  "sdk": "0.1.0",
  "abi": "1.0.0",
  "protocol": 1,
  "runtime": "0.1.7-alpha.2",
  "node": "24.x.x",
  "presets": ["standard", "rsi-infer"],
  "experience": {
    "qwen3.8-27b": "Qwen 服务经验 v3",
    "Ornith-1.5-35B-A3B": "Ornith V18"
  },
  "tools": {
    "toy-mcp": "0.1.0"
  },
  "builtAt": "2026-10-22T00:00:00Z"
}
```

校验：

```powershell
Get-Content .\SHA256SUMS
Get-FileHash .\bin\bole_agent.dll -Algorithm SHA256
```

`SHA256SUMS` 应覆盖包内所有可执行文件和运行期依赖；`VERSION.json` 与实际文件不一致时不要继续部署。

## 4. 自检

运行：

```powershell
.\selfcheck.ps1
```

或控制台示例的 `--selfcheck`。自检按顺序验证：

| # | 检查 | 通过标准 |
| --- | --- | --- |
| 1 | 校验和与版本 | `SHA256SUMS` 全过；`VERSION.json` 与文件一致 |
| 2 | 启动运行时 | `bole_init` 返回 `0`；`RUNTIME_READY` 在 30 秒内到达 |
| 3 | 能力位 | `capabilities.preset` / `persistence` / `trace` 为 true；`sandbox` 非 `unavailable`（或按配置明确告警） |
| 4 | 建会话 | `session/create` 返回 `0`；`standard` 与 `rsi-infer` 各建一个会话 |
| 5 | 提交与工具 | 玩具 MCP 工具调用完成；`TASK_FINISHED(outcome=completed)`；总耗时小于 60 秒 |
| 6 | RSI 收据 | `rsi-infer` 会话出现 `RSI_STATE`；`delivered` 或 `available` 符合任务类型；`standard` 无收据 |
| 7 | 取消 | 长任务中 `bole_cancel` 返回 `0`；终态 `aborted`；之后无该任务事件 |
| 8 | 轨迹 | `bole_trace_export` 写出 JSONL；事件数大于 0 |
| 9 | 无外网 | 运行期无公网请求；telemetry 无外发 |
| 10 | 退出 | `bole_release` 返回 `0`；运行时和工具子进程全部退出 |

自检日志写到 `home/logs/selfcheck-<时间>.log`，包含版本、命令、耗时和错误码。

## 5. 网络与安全

### 5.1 网络

- 运行期只需要访问内网模型网关；不需要公网。
- 模型地址、密钥占位、MCP server 由部署配置提供，不在文档中写死。
- 如现场有代理，按部署配置设置；SDK 不自动读取系统代理。
- 遥测默认关闭；不随包上传数据。
- 无自动更新；升级靠替换离线包。

### 5.2 未签名与 EDR

- 当前包可能没有代码签名证书；`bole_agent.dll`、`node.exe`、示例程序可能被 EDR/杀软拦截。
- 随包提供未签名说明、所有可执行文件的 SHA256、构建时间和来源说明。
- 如被拦截，记录拦截名称/规则，交给安全团队加白；不要关闭系统防护。
- 不要从网络盘运行 `home/`；会话和缓存的 ACL 按本机目录处理。

### 5.3 权限

- 不需要管理员权限。
- `home/` 必须对运行账户可写。
- `runtime/` 只需可读、可执行。
- 多用户/多实例：复制整个包目录，或给每个实例独立 `home/`。

## 6. 升级与回滚

### 6.1 升级

1. 停止正在使用 SDK 的应用程序；
2. 备份旧包目录和 `home/`（`home/` 可选，但建议保留）；
3. 解压新包到新目录；
4. 把旧 `home/` 复制/迁移到新包；
5. 运行 `install.ps1` 和 `selfcheck.ps1`；
6. 确认通过后切换应用加载路径。

不要在原目录上覆盖解压；保留旧目录便于回滚。

### 6.2 回滚

- 停止应用；
- 切回旧包目录；
- 使用升级前的 `home/` 备份；
- 重新运行自检。

## 7. 常见故障

| 现象 | 可能原因 | 处理 |
| --- | --- | --- |
| `install.ps1` 校验失败 | 文件损坏、下载不完整、被修改 | 重新解压；对比 `SHA256SUMS` |
| `bole_init` 返回 `1001` | `runtime/` 缺 `node.exe`、入口或补丁配置 | 检查包是否完整；重新解压 |
| `bole_init` 返回 `1003` | 未设置事件回调 | 调用方设置 `event_cb` |
| `wait_ready` 超时 `2004` | 运行时启动失败、`home/` 不可写、模型配置错误 | 看 `home/logs/`；检查权限和配置 |
| `capabilities.sandbox = unavailable` | 文件沙箱后端不可用 | 检查系统策略；必要时按部署配置切 `danger-full-access` 并评估风险 |
| 模型鉴权失败 `5001` | 网关 key 缺失/错误 | 检查部署配置中的 key 占位和环境变量 |
| 连不上模型 `5005` | 内网网关不可达、代理/防火墙 | 检查网络、端口、白名单 |
| 工具调用失败 | 玩具 MCP 未启动、工具配置错误 | 看 `TOOL_CALL_FINISHED.error`；检查 `tools/toy-mcp` |
| `RSI_UNAVAILABLE` `3004` | 运行期未组合 preset 面 | 检查补丁配置和部署版本 |
| 退出后仍有子进程 | 运行时未正常释放 | 确认调用了 `bole_release`；检查 Job Object/权限 |
| EDR 拦截 | 未签名 DLL/node.exe | 记录拦截项，走安全加白流程 |

## 8. 部署检查清单

- [ ] 目标机 Windows 10/11 x64，解压目录有写权限
- [ ] `SHA256SUMS` 全部通过
- [ ] `VERSION.json` 与实际文件一致
- [ ] `install.ps1` 成功，未写注册表、未要求管理员
- [ ] `home/` 可写，未覆盖已有会话
- [ ] `selfcheck.ps1` 第 1–10 项全部通过
- [ ] 无公网请求，telemetry 无外发
- [ ] `standard` 无经验收据，`rsi-infer` 收据符合预期
- [ ] 取消后终态 `aborted`，无残留子进程
- [ ] 轨迹文件可导出、事件数大于 0
- [ ] 已记录未签名说明、SHA256、构建时间和验收人/时间
