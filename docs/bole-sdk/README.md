# 伯乐智能体 SDK：文档索引

本目录存放 SDK 交付的内部设计与契约文档。对外交付的四份文档在 `docs/delivery/`，由打包脚本复制进离线包。

本目录不属于任何包组：它随交付项目临时落在包仓库里（C 侧与交付文档在交付时一并迁出），因此不套用包 README 的双语与发布约定，保持中文单语。它也不在根 README 的包组表里——根 README 只登记 `@deepseek-ai/dsh-*` 包组。

## 读者与边界

- `docs/bole-sdk/` 是**内部契约与分工文档**：读者是我方 A/B 与联调；可以出现内部包名、服务名、字段名，但**不写本机绝对路径、用户名、机器名、临时目录**。路径示例统一用 `<repo>`、`<harness-workspace>`、`<dev-home>`、`<patch-file>` 等占位符，环境地址写「由部署配置提供」。
- `docs/delivery/` 是**对外文档**：读者是伙伴；不出现内部包名、源码路径、宿主服务名，只描述 SDK 边界与调用方式。

## 文档清单

| 文档 | 状态 | 内容 | 是否阻塞开工 |
| --- | --- | --- | --- |
| [00-decisions.md](00-decisions.md) | 已完成（2026-10-22 修订） | 确认结论、交付边界、技术事实核对 | 否 |
| [01-plan.md](01-plan.md) | 已完成（2026-10-22 修订） | 架构、仓库结构、接口概览、里程碑、分工、风险 | 否 |
| [02-protocol.md](02-protocol.md) | 已冻结 1.0（2026-10-22） | 协议方法表：入参、出参、错误、顺序保证 | 否 |
| [03-events.md](03-events.md) | 已冻结 1.0（2026-10-22；真实 fixtures 已捕获 12 个） | 事件模型：枚举、字段、合并规则、来源映射 | 否 |
| [04-c-abi.md](04-c-abi.md) | 已冻结 1.0（2026-10-22；B 在 B1 前确认可实现性） | C 头文件：句柄、错误码、回调、线程模型 | 否 |
| [05-testing.md](05-testing.md) | 已完成（2026-10-23） | 测试分层、fixtures、命令、验收检查表 | 否 |
| [06-dev-environment.md](06-dev-environment.md) | 已完成（2026-10-23） | 路径约定、C/插件构建、mock 模型、环境坑位 | 否 |
| [07-offline-package.md](07-offline-package.md) | 已完成（2026-10-23） | 离线包目录、C 启动约束、依赖清单、自检 | 否 |
| [08-work-split.md](08-work-split.md) | 已完成（2026-10-22） | 分工、工作包、依赖、接口约束、领取表 | 否（分工入口） |
| [09-a-track-plan.md](09-a-track-plan.md) | 已完成（2026-10-23） | A 轨执行计划：起点差距、技术决定、阶段/周排期、测试证据、风险 | 否（A 内部执行） |
| [10-task-coverage.md](10-task-coverage.md) | 已完成（2026-10-23） | 原任务逐项覆盖、已知偏差、剩余测试与打包前动作 | 否 |
| [11-packaging-runbook.md](11-packaging-runbook.md) | 已完成（2026-10-23） | 构建/打包/干净 VM 验收 runbook（测试团队使用） | 否 |
| [docs/delivery/README.md](../delivery/README.md) | 四份正文已完成（2026-10-23），待最终包内核对与测试验收 | 对外四份：接口说明、集成指南、离线部署、错误码 | 否，打包前 |

## 2026-10-22 修订摘要

本轮以《任务说明》为唯一依据，在交付仓库源码上复核后改了五处前提：

1. **RSI 是模式，不是服务开关**：产品侧已把 RSI 做成第五种 agent preset（`rsi-infer` = `standard` + 只读经验插件）。开关 = 新任务在 `standard` / `rsi-infer` 之间选择；运行中的任务不变（内核锁定）。见 [00-decisions.md](00-decisions.md) 2.2。
2. **验证阶段暂缓**：本基线没有 verify 类会话事件，仓库与完整宿主工作区里也都没有完成检查实现；`VERIFY_CHECK` 保留定义但不产生、不承诺。见 2.5。
3. **运行组合改为 preset 面**：补丁层要像 Web 一样禁用 base 的 agent 面行，插入 `agent-preset-registry` 与两条 preset；同时关闭外网面、固定 `approval.policy: never`。见 [01-plan.md](01-plan.md) 第七节。
4. **取消、三层与 thinking 落定**：立即中断；补 step 事件与按 task 过滤的轨迹；`session/resume` 新增；thinking 走 reasoning 通道。见 2.6、2.7。
5. **协议命名统一**：请求用斜杠（`session/create`、`session/cancel`、`session/trace`、`rsi.set`），通知用点（`assistant.delta`、`rsi.state`）。
6. **另三处确认（10-22）**：目标模型 `qwen3.8-27b`（允许列表另一个是 `Ornith-1.5-35B-A3B`，两个模型各自加载对应经验）；离线包只挂 `standard` + `rsi-infer`；MCP v1 只支持配置文件声明 + 重启生效。

RSI 产品实现的权威参考是同仓库的 [RSI-infer 交接](../../bundle/web-app/RSI-INFER-HANDOFF.zh.md) 与 [`skill-bole-experience`](../../skill/skill-bole-experience/README.md)；本目录的文档只描述 SDK 如何选择与投影，不重复实现细节。

## 当前阶段：实施完成，验收交接（2026-10-23）

- A/B 两条线已合入 `develop`；C SDK、示例、离线打包脚本、验收脚本、四份交付文档均已落地。
- 已用真实内网网关跑通 console、B8 自检、GUI；qwen `enable_thinking:false` 已抓包确认。
- 干净目标 VM 由测试团队执行，使用 `packaging/acceptance.ps1`；开发机已完成等价演练。
- **最终交付 zip 尚未生成**：r5 是旧包；已有 r6 候选包但早于 toy MCP 0.2.0/acceptance 改动，不能交付；当前代码需要重打最终 r6。
- 验收口径以 [10-task-coverage.md](10-task-coverage.md) 为准；构建步骤见 [11-packaging-runbook.md](11-packaging-runbook.md)。
- 两处既定限制：验证阶段（`VERIFY_CHECK`）暂缓；MCP v1 仅配置文件声明 + 重启，不支持运行时注册。

## 三份契约已冻结（2026-10-22 1.0）

02 / 03 / 04 已按 10-22 的两轮确认冻结 1.0；C 头文件是 `native/include/bole_agent.h`。冻结后只增不改，A/B 两轨可以各自用 mock 推进。

架构上有一处实现修订：**事件由插件投影成 SDK 事件，通过 `bole.event` 通知投递**；C 库只解析信封、排队、回调，不再解析 harness 事件词汇。`assistant.delta` / `rsi.state` 保留为专用通知，语义等价。这样 C 侧不依赖 harness 事件类型，A/B 的接缝是 `bole.event` 的 payload schema。

两个人要并行开发，中间必须有稳定的接缝。协议、事件、ABI 三份文档就是那条接缝：

- 一个人改 `runtime/packages/sdk-server` 时，按 `02-protocol.md` 与 `03-events.md` 写。
- 另一个人写 C 库时，按 `04-c-abi.md` 写，并用假运行时回放 `02`、`03` 里定义的报文与 `native/tests/fixtures/`。
- 两份文档冻结后，双方不需要等对方的代码，可以用 mock 各自推进。

## 写作约定

- 中文单语，术语与 `00-decisions.md`、`01-plan.md` 保持一致：会话 / 任务（内核 turn）/ 步骤（内核 step）/ 模式（agent preset）。
- 一份文档只有一个负责人，改完同步更新本表的「状态」。
- 契约类文档写「接口是什么」，不写「怎么实现」；实现细节进代码注释。
- 报文示例与字段表必须能直接转成测试用例；示例必须来自真实运行，不手写理想序列。
- 请求名用斜杠形式，通知名用点形式；新增项只增不改。
- 每个方法的「落点」必须真实存在（运行时的服务或方法）；落点为空不算冻结。
- 事件必须映射到真实的 harness 来源；没有来源就不定义，或明确标「暂缓」。
- 不写本机绝对路径、用户名、机器名、临时目录；路径示例用 `<repo>`、`<harness-workspace>`、`<dev-home>`、`<patch-file>` 等占位符，环境地址写「由部署配置提供」。
