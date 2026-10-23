# 12 伯乐 Agent SDK 交付验收交接报告（模板）

> 用途：交给产品/测试的验收说明。产品重点看第 1、2、5、6、7 节；测试重点看第 3、4、8、9 节。  
> 使用前把 `<...>` 替换成现场值，删掉本提示，补上截图。

## 1. 结论摘要

- 交付物：Windows x64 C SDK + 随包运行时 + 控制台/界面示例 + 离线部署包 + 四份文档。
- 已完成：真实内网网关下的 console 工具调用、B8 自检、GUI 长任务/取消、RSI 开关；开发机 `acceptance.ps1` 全过。
- 待测试：干净目标 VM、Windows 10（如适用）、Ornith（如现场使用）、EDR/签名、伙伴独立集成。
- 已知限制：验证阶段事件不产生；MCP 仅配置文件声明 + 重启，不支持运行时注册。
- 最终包：`bole-offline-r6-final.zip`
  - SHA256：`aaeeafd8c7e3a520743fd367ce8955b00923a500a453b7d6b604dc1f30dbcc0d`
  - tag：`bole-m0-20261023-r6`
  - 包内 toy MCP：12 个工具；运行时 `0.1.7-alpha.2`；SDK ABI `1.0.0`。

## 2. 交付物清单

| 交付物 | 位置 | 说明 |
| --- | --- | --- |
| SDK 动态库 | `bin/bole_agent.dll` | MSVC x64 `/MT`，仅依赖 `KERNEL32.dll` |
| 导入库/头文件 | `bin/bole_agent.lib`、`bin/bole_agent.h` | C ABI |
| 随包运行时 | `runtime/` | node.exe + dsh 依赖树 + 补丁 |
| 可写运行目录 | `home/` | profile、会话、日志、MCP 配置 |
| 控制台示例 | `samples/console-c/` | `bole_console.exe`、`bole_selfcheck.exe` |
| 界面示例 | `samples/gui-win32/` | 双击即用、多轮、RSI、取消 |
| 玩具 MCP | `tools/toy-mcp/` | 12 个无副作用工具 |
| 交付文档 | `docs/` | 接口、集成、离线部署、错误码 |
| 安装/验收脚本 | 包根 | `install.ps1`、`verify.ps1`、`acceptance.ps1` |

## 3. 环境与前置条件

| 项 | 值 |
| --- | --- |
| 目标 OS | Windows 10 / 11 x64（本次实测 `<Windows 版本>`） |
| 目标机依赖 | 无 Node、无 VS/VC++ 运行库；普通用户 |
| 模型网关 | `<gateway>`（内网，OpenAI-compatible，无 `/v1` 后缀传给 install） |
| 模型 ID | `qwen3.8-27b` |
| API key | `<需要/不需要；不要写真实 key>` |
| 网络 | 只能访问网关；不能访问公网（如适用） |

## 4. 安装与自检

```powershell
Expand-Archive .\bole-offline-r6-final.zip <dir>
cd <dir>\bole-agent-sdk-win-x64

.\install.ps1 -Target <target> -ModelGateway <gateway>
.\verify.ps1 -Root <target>
```

期望：

```text
verify ok
```

然后：

```powershell
cd <target>
.\acceptance.ps1 `
  -Package <package-root> `
  -Target <target-2> `
  -ModelGateway <gateway> `
  -Prompt "调用玩具工具 mcp__toy__add 计算 1+2，然后用结果回答。" `
  -ZipPath <zip>
```

干净 VM 不要加 `-SkipCleanEnvironmentCheck`。期望：

```text
ACCEPTANCE PASS
```

## 5. 验收结果对照

| 原任务验收 | 结果 | 证据 |
| --- | --- | --- |
| 1. 无外网/无开发环境，输入→模型→工具→结果 <1 分钟 | 开发机真实网关通过；干净 VM 待测试 | console 截图 + 耗时 + `acceptance` |
| 2. 任务期间界面可交互，取消生效 | 本机通过：sleep 5s 期间 `Responding=True`，Cancel 后 `finished (aborted)` | GUI 截图/录屏 |
| 3. RSI 开关生效且可观测 | 通过；收据 `available`，`bundleId=qwen38-service-v3` | GUI RSI 截图 + selfcheck |
| 4. 按文档独立集成可复现 | 待测试/伙伴 | 四份文档 + 集成日志 |
| 5. 事件序列与 harness 一致、无伪造 | 通过；验证阶段明确不产生 | trace/fixtures；不伪造 `VERIFY_CHECK` |

## 6. 截图清单（至少这些）

建议新建 `screenshots/`，按编号放图，并在图下写一句结论。

| # | 截图内容 | 必须能看到 |
| --- | --- | --- |
| 01 | 包与版本 | zip 文件名、SHA256、`VERSION.json` |
| 02 | 安装/verify | `installed to ...`、`verify ok` |
| 03 | console 工具调用 | `[tool] start`、`mcp__toy__add`、`isError:false`、结果 `3`、`[task] finished` |
| 04 | B8 自检 | 7 个 PASS，尤其 RSI receipt、cancel、trace |
| 05 | GUI 输入与结果 | 输入框、Submit、finished、结果区追加内容 |
| 06 | GUI RSI | RSI 复选框勾选、状态显示 RSI on |
| 07 | GUI 长任务 | 状态 `tool running`，窗口可操作 |
| 08 | GUI 取消 | 状态 `finished (aborted)`，任务已停止 |
| 09 | acceptance | `ACCEPTANCE PASS` 和 evidence 路径 |
| 10 | 进程/网络（可选） | 无残留 node/toy 进程；公网不可达 |

截图要求：

- 保留窗口标题/时间/命令，能看出是本次包；
- 不要把 API key 截进去；网关地址按公司要求打码；
- 图放在文档同级的 `screenshots/`，文件名与上表一致。

## 7. 已知限制（必须写进交付说明）

1. **验证阶段**：当前没有真实验证事件来源，`VERIFY_CHECK(40)` 不产生、不承诺；不伪造。
2. **MCP 工具注册**：只支持 `<DSH_HOME>/cordis.patch.yml` 声明 + 重启；不支持运行时注册/注销。
3. 不提供单会话关闭；会话随 `bole_release` 失效。
4. 仅 Windows 10/11 x64；不提供 32 位。
5. 未签名 DLL/exe；EDR/杀软策略需现场确认。
6. 经验包只对 `qwen3.8-27b` / `Ornith-1.5-35B-A3B` 投递；其他模型为 `unsupported-model`。

## 8. 测试团队执行项

- [ ] 干净 Win10/11 x64 VM，无 Node/VS/VC++，普通用户；
- [ ] 只开网关，断公网；
- [ ] 解压、核对 SHA256；
- [ ] `install.ps1` + `verify.ps1`；
- [ ] `acceptance.ps1`（不要加 `-SkipCleanEnvironmentCheck`）；
- [ ] GUI 长任务 + Cancel 录屏；
- [ ] 按四份 delivery 文档独立集成一次；
- [ ] 记录 OS、模型、网关、包 hash、日志、耗时、失败复现步骤。

## 9. 附件清单

```text
bole-offline-r6-final.zip
SHA256.txt
VERSION.json
acceptance/            # transcript、install/verify/console/selfcheck/gui 日志
screenshots/           # 01-10
selfcheck-trace.jsonl
四份交付文档（包内 docs/）
```

## 10. 签署

| 角色 | 姓名 | 日期 | 结论 |
| --- | --- | --- | --- |
| 开发/交付 |  |  |  |
| 测试 |  |  |  |
| 产品/项目负责人 |  |  |  |
