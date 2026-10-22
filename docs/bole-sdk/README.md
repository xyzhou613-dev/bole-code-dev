# 伯乐智能体 SDK：文档索引

本目录存放 SDK 交付的内部设计与契约文档。对外交付的四份文档在 `docs/delivery/`，由打包脚本复制进离线包。

本目录不属于任何包组：它随交付项目临时落在包仓库里（C 侧与交付文档在交付时一并迁出），因此不套用包 README 的双语与发布约定，保持中文单语。它也不在根 README 的包组表里——根 README 只登记 `@deepseek-ai/dsh-*` 包组。

## 文档清单

| 文档 | 状态 | 内容 | 是否阻塞开工 |
| --- | --- | --- | --- |
| [00-decisions.md](00-decisions.md) | 已完成（2026-10-22 修订） | 确认结论、交付边界、技术事实核对 | 否 |
| [01-plan.md](01-plan.md) | 已完成（2026-10-22 修订） | 架构、仓库结构、接口概览、里程碑、分工、风险 | 否 |
| [02-protocol.md](02-protocol.md) | 骨架，方法/通知/顺序已改定 | 协议方法表：入参、出参、错误、顺序保证 | 是（错误码编号待补） |
| [03-events.md](03-events.md) | 骨架，事件表/合并/顺序/RSI 已改定 | 事件模型：枚举、字段、合并规则、来源映射 | 是（样例报文待补） |
| [04-c-abi.md](04-c-abi.md) | 骨架，原则/函数表/线程模型已改定 | C 头文件：句柄、错误码、回调、线程模型 | 是（编号与字段细节待补） |
| 05-testing.md | 待写 | 测试分层与验收演练脚本 | 否，M1 前完成 |
| 06-dev-environment.md | 待写 | 运行时沙盒、自建插件包构建、MSVC 与 pnpm 准备 | 是，第二个人接手前 |
| 07-offline-package.md | 待写 | 离线包目录、依赖清单、安装与自检 | 否，M2 前完成 |
| `docs/delivery/` | 待写 | 对外四份：接口说明、集成指南、离线部署、错误码 | 否，M2 前完成 |

## 2026-10-22 修订摘要

本轮以《任务说明》为唯一依据，在 `bole-code-dev` 源码上复核后改了五处前提：

1. **RSI 是模式，不是服务开关**：产品侧已把 RSI 做成第五种 agent preset（`rsi-infer` = `standard` + 只读经验插件）。开关 = 新任务在 `standard` / `rsi-infer` 之间选择；运行中的任务不变（内核锁定）。见 [00-decisions.md](00-decisions.md) 2.2。
2. **验证阶段暂缓**：本基线没有 verify 类会话事件，仓库与本机完整宿主 `/root/bole-deploy` 里也都没有完成检查实现；`VERIFY_CHECK` 保留定义但不产生、不承诺。见 2.5。
3. **运行组合改为 preset 面**：补丁层要像 Web 一样禁用 base 的 agent 面行，插入 `agent-preset-registry` 与两条 preset；同时关闭外网面、固定 `approval.policy: never`。见 [01-plan.md](01-plan.md) 第七节。
4. **取消、三层与 thinking 落定**：立即中断；补 step 事件与按 task 过滤的轨迹；`session/resume` 新增；thinking 走 reasoning 通道。见 2.6、2.7。
5. **协议命名统一**：请求用斜杠（`session/create`、`session/cancel`、`session/trace`、`rsi.set`），通知用点（`assistant.delta`、`rsi.state`）。
6. **另三处确认（10-22）**：目标模型 `qwen3.8-27b`（允许列表另一个是 `Ornith-1.5-35B-A3B`，两个模型各自加载对应经验）；离线包只挂 `standard` + `rsi-infer`；MCP v1 只支持配置文件声明 + 重启生效。

RSI 产品实现的权威参考是同仓库的 [RSI-infer 交接](../../bundle/web-app/RSI-INFER-HANDOFF.zh.md) 与 [`skill-bole-experience`](../../skill/skill-bole-experience/README.md)；本目录的文档只描述 SDK 如何选择与投影，不重复实现细节。

## 为什么这三份阻塞开工

两个人要并行开发，中间必须有稳定的接缝。协议、事件、ABI 三份文档就是那条接缝：

- 一个人改 `sdk/bole-server` 时，按 `02-protocol.md` 与 `03-events.md` 写。
- 另一个人写 C 库时，按 `04-c-abi.md` 写，并用假运行时回放 `02`、`03` 里定义的报文。
- 两份文档冻结后，双方不需要等对方的代码，可以用 mock 各自推进。

## 写作约定

- 中文单语，术语与 `00-decisions.md`、`01-plan.md` 保持一致：会话 / 任务（内核 turn）/ 步骤（内核 step）/ 模式（agent preset）。
- 一份文档只有一个负责人，改完同步更新本表的「状态」。
- 契约类文档写「接口是什么」，不写「怎么实现」；实现细节进代码注释。
- 报文示例与字段表必须能直接转成测试用例；示例必须来自真实运行，不手写理想序列。
- 请求名用斜杠形式，通知名用点形式；新增项只增不改。
- 每个方法的「落点」必须真实存在（运行时的服务或方法）；落点为空不算冻结。
- 事件必须映射到真实的 harness 来源；没有来源就不定义，或明确标「暂缓」。
