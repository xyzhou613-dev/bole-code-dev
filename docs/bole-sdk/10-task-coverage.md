# 10 原任务覆盖与交付状态（2026-10-23）

依据：`任务说明：伯乐智能体SDK交付_20261020_zt.md`。本文只做验收前核对，不改 02/03/04 冻结契约。

## 一、结论

- **SDK、运行时、示例、离线打包脚本、四份交付文档**均已实现，A/B 两条线已合入 `develop`。
- **真实内网网关**已跑通 console、B8 自检和 GUI；qwen `enable_thinking:false` 已抓包确认。
- **最终交付 zip 已生成**：`bole-offline-r6-final.zip`，SHA256 `aaeeafd8c7e3a520743fd367ce8955b00923a500a453b7d6b604dc1f30dbcc0d`；包含 toy MCP 0.2.0、acceptance、UTF-8/RSI/GUI 和 no-thinking 配置，开发机 `acceptance.ps1` 已通过。
- **干净目标 VM 由测试团队执行**；`packaging/acceptance.ps1` 已提供一键验收，开发机已完成等价演练。
- 有两处**按既有决定暂缓/限定的能力**，不是实现遗漏，但需要项目负责人确认验收口径：
  1. **验证阶段**：本基线没有 verify 类会话事件，`VERIFY_CHECK(40)` 保留定义但永不产生；不得伪造。
  2. **工具注入**：MCP v1 只支持配置文件声明 + 重启生效，不支持运行时注册；任务要求的“方向明确”已在文档写明。

## 二、原任务逐项核对

### 4 交付物

| 交付物 | 状态 | 证据/说明 |
| --- | --- | --- |
| SDK 库（DLL + 头文件 + 导入库 + 依赖清单） | 完成 | MSVC 2022 x64 `/MT`；`bole_agent.dll/.lib/.h`；DLL 仅依赖 `KERNEL32.dll`；`VERSION.json` 记录版本 |
| 离线部署包 | 完成 | r6 final zip 已生成；`build-offline.ps1`、`install.ps1`、`verify.ps1`、`acceptance.ps1`；开发机走通 install→verify→C2→selfcheck→GUI |
| 控制台示例 | 完成 | `examples/console-c`；自动路径、`--rsi`、唯一 session、UTF-8 argv |
| 界面示例 | 完成 | `examples/gui-win32`；双击自动路径、多轮、日志追加、RSI 复选框、取消、背景重绘修复 |
| 四份文档 | 完成，待最终包内核对 | `docs/delivery/01-04`；B7 内容已落地；最终包需按测试团队反馈复核 |

### 5 接口要求

| # | 要求 | 状态 | 证据/说明 |
| --- | --- | --- | --- |
| 1 | 导出 C ABI | 完成 | `native/include/bole_agent.h`；14 个导出；不导出 C++ |
| 2 | 控制面非阻塞 | 完成 | Init/Release/Submit/Cancel 立即返回；CreateSession 只等本地 create 响应，不等模型/工具 |
| 3 | 数据面异步 | 完成 | 事件回调在 SDK 自有线程；同一句柄串行；文档写明切 UI 线程 |
| 4 | 会话-任务-步骤 + 轨迹 | 完成 | `SESSION/TASK/STEP` 事件；`bole_trace_export(..., turn)`；真实 trace 测试 |
| 5 | 按 harness 阶段建模 | 部分完成（验证阶段暂缓） | 任务开始/计划/模型选择/工具开始返回/结束或错误均投影；`VERIFY_CHECK` 无真实来源，明确不产生 |
| 6 | RSI 开关（配置 + SDK + 事件） | 完成 | `bole_config.rsi_enabled`、`bole_rsi_set/get`、`RSI_STATE`；默认 standard；真实网关收据 `available`/`qwen38-service-v3` |
| 7 | 取消语义 | 完成 | 立即中断；模型/工具/排队消息处理；`TASK_FINISHED(outcome=aborted)`；自检覆盖 |
| 8 | 工具注入方向 | 完成（限定） | MCP 配置文件声明 + 重启；不支持运行时注册；04/交付文档写明 |
| 9 | 工具隔离 | 完成 | MCP 子进程；杀 toy MCP 后工具错误、运行时继续；无残留子进程 |
| 10 | 事件背压 | 完成 | delta 50ms/4KB 合并；结构事件保留；队列溢出 `2007` |

### 6 运行与部署

| 要求 | 状态 | 证据/说明 |
| --- | --- | --- |
| Windows x64 | 完成 | MSVC x64；Win11 开发机实测；Win10 目标由测试覆盖 |
| 无外网、内网模型 | 开发机实测内网网关 | 真实网关 console/selfcheck/GUI；断网 clean VM 由测试执行 |
| 不下载模型/依赖/工具 | 完成 | 依赖随 runtime 树；toy MCP 随包；经验包随 profile |
| 字符串 UTF-8、路径内部处理 | 完成 | 修了 Windows argv/GUI 输入的 ANSI→UTF-8；真实中文 prompt 验证 |

### 7 示例程序

| 要求 | 状态 | 证据/说明 |
| --- | --- | --- |
| 最简单工具调用 <1 分钟 | 完成 | 真实网关 console 工具调用；selfcheck < 60s 门限 |
| 界面五项元素 | 完成 | 输入/提交/状态/结果/取消 |
| 示例源码随包 | 完成 | build-offline 复制 `examples/console-c`、`gui-win32` |
| 长任务不卡 | 完成（本机） | 真实网关让模型调用 `mcp__toy__sleep(5000)`；睡眠期间 `Responding=True`，点 Cancel 后 `finished (aborted)`，进程存活；截图 `gui-long-running` / `gui-long-cancelled` 已生成。干净 VM 由测试复跑录屏。 |

### 8 验收标准

| # | 验收 | 状态 | 说明 |
| --- | --- | --- | --- |
| 1 | 无外网无开发环境目标机跑通 <1 分钟 | 待测试团队 | 开发机 + 真实网关等价通过；clean VM 由测试执行 |
| 2 | 任务期间界面可交互、取消生效 | 完成（本机） | 真实网关 sleep 5s 期间 GUI `Responding=True`；Cancel 后 `finished (aborted)`；干净 VM 录屏由测试复跑 |
| 3 | RSI 开关生效、状态可观测 | 完成 | 真实 qwen；`available`/`bundleId=qwen38-service-v3`；standard 无 receipt |
| 4 | 按文档独立集成可复现 | 待测试团队 | 四份文档已齐；最终 zip 内核对 + 伙伴/测试独立集成 |
| 5 | 事件序列与 harness 一致、无伪造 | 完成（验证阶段除外） | fixtures + 真实运行；verify 明确不伪造 |

## 三、已经做过的测试

- A 侧：vitest 31；fixtures 12 全过；session 6；trace 4；e2e 4（含杀 MCP）；check-config 15。
- C 侧：Linux ctest 4/4；MSVC ctest 4/4（json/error/platform/fixtures）；MinGW ctest 4/4。
- Windows：真实网关 console C2、B8 selfcheck 全 PASS、GUI 两轮追加 + RSI 复选框。
- GUI 长任务：真实网关 `mcp__toy__sleep(5000)` 期间 `Responding=True`，Cancel 后
  `finished (aborted)`，进程存活；截图在交付证据目录。
- 抓包：qwen 请求体确认 `chat_template_kwargs.enable_thinking=false`，reasoning 为空。
- 开发机一键 acceptance：install→verify→console→selfcheck→GUI→cleanup 全 PASS。

## 四、还缺的测试/动作

1. **干净目标 VM**：测试团队执行；用 `packaging/acceptance.ps1`，不跳过 clean-environment 检查。
2. **Windows 10 x64**：目前只在 Win11 开发机跑过；目标含 Win10 时补一台。
3. **真实 Ornith 网关**：qwen 已验；Ornith 允许列表/经验包需实际网关复跑（任务不强制，但计划里列了）。
4. **最终包**：r6 final 已生成并通过开发机 acceptance；测试团队仍应在目标 VM 复跑。
5. **签名/EDR**：未签名交付；目标机安全策略由测试记录。
6. **独立集成复现**：伙伴/测试按四份 delivery 文档从零集成一次。

## 五、结论与建议

实现层面已经具备交付条件；**r6 final zip 已生成**。第 1/2/4 条的目标机与独立集成复现仍由测试团队在干净 VM 完成（GUI 长任务/取消已在开发机通过，测试复跑留证）。建议：

1. 把 r6 final zip + SHA256 交给测试；
2. 测试团队跑 `acceptance.ps1` + GUI 长任务/取消 + 独立集成 + Win10（如适用）；
3. 通过后再决定正式交付 tag。

验证阶段（`VERIFY_CHECK`）和运行时 MCP 注册是既定范围限制，不应在验收时被误判为 bug；若客户要求，需另立需求。

## 六、文档审计（2026-10-23）

本轮检查了 `docs/bole-sdk/` 与 `docs/delivery/`，修正了会误导验收的过期表述：

- 00/01/06/07/08/README：把“B 未开始/只有头文件/开发暂停”等历史状态标注为历史，并补当前实施状态；
- 05-testing：fixtures 从 10 个旧列表更新为实际 12 个，补 `plan-updated`、`reasoning`，修正取消场景名，指向 `runtime/dev/capture.mjs`；
- 07-offline-package：打包步骤改为指向 11，`selfcheck.ps1` 改为实际的 `verify.ps1` + `bole_selfcheck.exe`，trace 路径改写；
- delivery/README：四份文档状态更新为“已完成、待最终包内核对/测试验收”；
- delivery/03：修正 `VERSION.json` 的 `experience` 形状（name/version，而不是按模型 keyed 的对象），修正 selfcheck 日志/ stdout 与 trace 路径；
- packaging/README：修正目录树（`home/` 与 `runtime/` 分离）、`verify.ps1` 描述、玩具工具数量与“r6 候选不能交付”的状态；
- 新增 11：构建机前置、MSVC/CMake 命令、tarball、运行时树、build-offline、哈希、干净 VM acceptance、常见坑和发布动作。

仍未由我方完成、需测试/负责人确认：

- 四份 delivery 文档在**最终 zip 内的路径与独立集成复现**；
- Windows 10、Ornith、真实 EDR/签名策略；
- 测试团队 clean VM 验收与正式交付 tag。

冻结契约 02/03/04 与 `native/include/bole_agent.h` 未改。
