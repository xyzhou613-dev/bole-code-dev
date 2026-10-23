# 伯乐智能体 SDK 交付：确认结果与剩余事项（2026-10-22 修订）

用途：记录《任务说明：伯乐智能体SDK交付_20261020_zt.md》待确认事项的答复结果，以及由此产生的新工作与待验证技术点。

> 实施状态（2026-10-23）：A/B 已实现并合入 `develop`，真实网关已验证；当前验收与打包见 [10-task-coverage.md](10-task-coverage.md) 与 [11-packaging-runbook.md](11-packaging-runbook.md)。本文保留 10-22 决策记录。
状态：七项已答复并形成结论，交付范围已划清（SDK 与示例由我方负责，IDE 侧绑定与集成归伙伴）。交付版本定为 harness `0.1.7-alpha.2`，交付仓库分支 `docs/bole-sdk`。2026-10-22 在仓库源码上复核，修正两处关键前提：**RSI 是 agent preset（模式）而不是服务开关**（见 2.2）；**本基线没有可投影的「验证」阶段**（见 2.5）。
核实基线：最初对照 `@deepseek-ai/dsh@0.1.2-rc.1`；2026-10-21 在 `0.1.7-alpha.2` 沙盒上复核；2026-10-22 在交付仓库源码上复核 preset 面、验证、运行组合与工具面，依据见第五节。

## 一、结论速览

| 编号 | 事项 | 结论 | 后续动作 |
| --- | --- | --- | --- |
| 1 | SDK 交付形态 | 已确认：以客户轻量化接入为准，接受 DLL + 随包运行时进程 | 按下方「轻量化」四条落成验收条款 |
| 2 | 是否允许扩展 SDK 协议 | 已确认：允许，实现方式不设限 | 采用补丁层 + 自建插件，官方包不改，见 2.1 |
| 3 | RSI 开关的定义 | 已确认（10-22 修正）：RSI 是 `rsi-infer` 模式；开 = 新任务用 `rsi-infer`，关 = 新任务用 `standard`；运行中的任务不变 | 见 2.2；补丁层必须组合 preset 面 |
| 4 | 验证阶段口径 | **暂缓**（10-22 复核）：本基线没有 verify 类会话事件，仓库内也没有完成检查实现 | `VERIFY_CHECK` 保留定义但不产生、不承诺，见 2.5 |
| 5 | 模型服务参数 | 已定：base 预置 `bole` 路由，目标模型 ID 为 `qwen3.8-27b`（命中经验允许列表），`bole-model` 只是占位 | 见 2.3 |
| 6 | 伙伴技术栈 | 已确认 TypeScript 为主，不影响 SDK 构建；范围为 SDK 与示例归我方、绑定与集成归伙伴 | 见 2.4 |
| 7 | 验收环境与资料 | 按正常交付执行，提前备资料 | 内部清单见第四节 |

### 第 1 项的「轻量化」落成四条可验收条款

安装即解压，不写注册表、不要求管理员权限；目标机无需预装 VS 运行库、Node 或 .NET；运行期无外网下载；集成方按示例代码完成一次调用不超过 30 行。

### 2026-10-22 复核补充确认

| 编号 | 事项 | 结论 | 后续动作 |
| --- | --- | --- | --- |
| 8 | 取消语义 | 已确认：**立即中断**，映射内核 `Agent.cancel`；不做步末终止 | 见 2.6，写入 02、03、04 |
| 9 | 三层与轨迹 | 已确认：补 step 级事件、`session/trace` 支持按 task 过滤、新增 `session/resume` | 见 2.7 |
| 10 | thinking 增量 | 已确认：新增 reasoning 通道，与正文同一合并策略 | 见 2.7 |
| 11 | 运行组合 | 已确认：`--profile sdk` + 补丁层；关闭外网面；`approval.policy: never` | 见 `01-plan.md` 第七节 |

## 二、逐项说明

### 2.1 协议扩展的做法（对应第 2 项）

已确认：允许扩展协议，实现方式不设限。

实现路线：**不改 DSH 源码，走「补丁层 + 自建插件」**。运行时组合里官方 `sdk-jsonrpc-server` 条目由补丁置为 `disabled`，再插入自建插件 `@bole/sdk-server`；插件复用官方公开导出的 `HarnessSdkJsonRpcServer` 与 `JsonRpcLineTransport`，在同一个 stdio transport 上接管请求分发与通知发送，新增消息的类型定义放在自建包内。

原方案「就地修改 `sdk/server`」的依据是 transport 只有一个注册位，而这条依据只说明了不能追加 handler，不说明不能换掉整个条目：官方 `sdk-app` 的补丁本身就用 `disabled` + `insert` 组装（`bundle/sdk-app/cordis.patch.yml`），补丁层的条目替换语义是既有能力。改走补丁后，`sdk/server` 与 `sdk/protocol` 保持 npm 原样，运行时升级不需要重新打补丁。2026-10-21 已在 `0.1.7-alpha.2` 沙盒上用插件探针实测：禁用官方条目、由自建插件接管 stdout 后，自定义方法正常返回，退出码 0。

两个实现前提（10-22 补记，源码核对所得）：

1. 自建插件必须沿用官方条目的 `inject: [sdkAppStartup, loader]`，并在 `initialize` 前 `await ctx.get('loader')?.await()`；官方把 `initialize` 当成就绪边界（`sdk/server/src/index.ts`），换掉条目时这段语义要原样保留，否则「就绪」会提前。
2. 官方 transport 的 `onRequest` 是单槽位、后注册覆盖先注册（`sdk/protocol/src/transport.ts`），自建插件接管后要自己分发官方三项方法再转交 `HarnessSdkJsonRpcServer.handleRequest`。

协议在官方三项（initialize / session/prompt / shutdown）之上增加：

| 新增项 | 类型 | 用途 | 必要性 |
| --- | --- | --- | --- |
| `session/create` | 请求 | 按会话建 agent 并选定 preset（含 RSI） | 必做，控制面 CreateSession 的落点 |
| `session/resume` | 请求 | 恢复已持久化会话 | 必做，对应 01 的 `bole_session_resume` |
| `session/cancel` | 请求 | 控制面取消，映射内核 `Agent.cancel` | 必做，验收标准第 2 条直接检查 |
| `session/trace` | 请求 | 按 task（turn）导出执行轨迹 | 必做，对应需求第 4 条 |
| `session/setPreset` | 请求 | 空白会话切换 preset（RSI 之外的模式也可用） | 必做，`rsi.set` 的会话级落点 |
| `rsi.set` / `rsi.get` | 请求 | 选择/读取新任务的 preset（RSI 开关） | 必做，验收标准第 3 条直接检查 |
| `assistant.delta` | 通知 | token 级增量（正文 + thinking），带合并与限频 | 必做，对应需求第 10 条与实时回传 |
| `rsi.state` | 通知 | RSI 开关状态、来源与经验投递收据 | 必做，验收标准第 3 条直接检查 |

各项落点：

- `session/create`：`ctx.agents.create({ sessionId, meta: { cwd, agentPreset }, setup })`，`setup` 在发布前 `ctx.agentPresets.mount(agentCtx, presetId)`（照 `api/session-controller` 的 `composeAgent` 写法）；不触模型、不触工具。
- `session/resume`：`ctx.agents.resume(...)`；前置 `sessionPersistence` 与 `sessions` 查询服务，preset 按会话投影恢复，定义缺失则拒绝（与 `bundle/headless` 的 adopted-session 校验同口径）。
- `session/cancel`：`ctx.agents.get(sessionId)` 取得 agent（agent id 与 session id 同值），调 `Agent.cancel({ kind: 'user' }, { keepInbox: false })`；请求立即返回，结束事实由事件流给出。
- `session/trace`：读会话日志（内存 `session.eventAt` 或持久化 JSONL），按 `turn` 过滤后写文件；`session-log-export` 是 Web 的浏览器下载，不是 Host 路径导出，不能直接复用。
- `assistant.delta`：订阅瞬时帧 `agent/assistant-stream`（`start` / `chunk` / `end`），对 `chunk.type` 为 `text-delta` 或 `reasoning-delta` 的帧做合并与限频，`channel` 区分正文与思考。它不是持久化会话事件，因此不参与重放，也不补发。
- `rsi.set` / `rsi.get` / `rsi.state`：见 2.2。

### 2.2 RSI 开关：切换 agent preset，SDK 只选择与投影（10-22 修正）

已确认的事实（2026-10-21）：经验沉淀与经验库更新都在线下完成；运行时链路里与 RSI 有关的动作只有「把经验注入提示词」。

10-22 在源码上复核后修正实现模型：**RSI 是模式，不是服务开关**。产品侧已经把 RSI 做成第五种内置 agent preset：

| 显示名 | preset id | 挂载伯乐经验 |
| --- | --- | --- |
| 标准模式 | `standard` | 否 |
| PTC 模式 | `ptc` | 否 |
| 极简模式 | `minimal` | 否 |
| 创造模式 | `cordis` | 否 |
| RSI-infer | `rsi-infer` | 是 |

SDK 离线包**只挂 `standard` 与 `rsi-infer` 两条**（10-22 确认）；`ptc` / `minimal` / `cordis` 本期不随包。

`bundle/web-app/presets/rsi-infer.patch.yml` 与 `standard.patch.yml` 除 preset id（`standard` → `rsi-infer`）、`order` 与多出的一行 `@deepseek-ai/dsh-skill-bole-experience`（`maxContextBytes: 4096`）外逐行相同。因此：

- **开 RSI = 新任务用 `rsi-infer`；关 RSI = 新任务用 `standard`。** 默认 `standard`（`agent-preset-registry` 的 `default: standard`），即默认关闭，符合任务书第 6 条。
- **开关粒度是新任务，不是运行中的任务。** 内核只在会话还没有开始第一个 turn 时允许 `agentPresets.select()`，之后返回 `agent-preset/locked`。SDK 不提供「运行中切换」，也不假装切换成功。
- **状态是真实状态，不需要 SDK 编造。** 会话头记录创建时的 `agentPreset`，空白期切换会追加 `agent-preset/selected` 事件，投影 `agentPreset` 以它为准；经验投递另有持久收据 `user/message`，`source.kind = bole-experience`，带 `status`（`delivered` / `available` / `unsupported-model`）、`deliveredMemoryIds`、`bundleId`、`provider` / `model`、`bodySha256`。
- **SDK 边界：** 补丁层组合 preset 面（`agent-preset-registry` + `standard` / `rsi-infer` 两条 `agent-preset` 声明），经验插件由 `rsi-infer` 这一行挂载；SDK 插件不 import、不挂载经验插件，只做选择与投影。这仍然是「SDK 不组合产品功能」：产品功能由 preset 声明组合，SDK 只是选择哪个模式。
- **不可用的情况：** 运行时没有组合 preset 面（`ctx.agentPresets` 缺失）时，`rsi.set` 返回 `RSI_UNAVAILABLE`（3xxx），`rsi.state` 只报「未组合」，不伪造开关状态。
- **升级影响（新增风险）：** preset 面与 Web bundle 同源，harness 升级要跟随上游 `bundle/web-app/cordis.patch.yml` 的 preset 面变化（哪些行从 base 移到 preset），否则会出现工具重复注册或缺失。

三条边界（10-21 结论，仍有效）：

1. **SDK 不组合产品功能**：经验插件永远由 preset 声明挂载，SDK 不 import、不挂载、不持有它的 fiber。
2. **没有入口就不假装有**：运行时未组合 preset 面时 `rsi.set` 返回 `RSI_UNAVAILABLE`；`rsi.state` 如实报告。
3. **最坏情况要重议验收**：若产品侧最终不把 preset 面交给 SDK 运行时，`rsi.set` 无法生效，验收第 3 条需要与负责人重议；不能默默降级。

### 2.3 模型配置：base 已预置路由，目标模型已定（对应第 5 项）

`bundle/base/cordis.patch.yml` 已经预置以下配置（不是待办草案）：

```yaml
- id: agent-default-model
  config:
    provider: bole
    model: bole-model
- id: llm-pi-ai
  config:
    providers:
      bole:
        displayName: Bole Model
        api: openai-completions
        baseURL: http://<model-gateway>/v1   # 地址由部署配置提供
        apiKey: local          # 占位；私有网关接受任意 key
        models: [{ id: bole-model }]
```

> 实施修正（2026-10-22，F8）：`0.1.7-alpha.2` 的发布包 `dsh-llm-pi-ai` 只解析 `apiKeyEnv`，忽略内联 `apiKey`。交付补丁改用 `apiKeyEnv: BOLE_API_KEY`，由 `<DSH_HOME>/.env` 提供 `BOLE_API_KEY=local`；不改 C 的启动环境变量列表。见 [09-a-track-plan.md](09-a-track-plan.md) F8。

「模型 ID」与「是否需要 API Key」在基线上已有答案：base 预置 `bole-model` 与占位 key。2026-10-22 确认：**目标网关实际提供的模型 ID 是 `qwen3.8-27b`**，命中经验允许列表（`qwen3.8-27b` / `Qwen3.8-27B`、`Ornith-1.5-35B-A3B` / `ornith-1.5-35b-a3b`）。因此：

1. 离线包要把 base 覆盖行的 `models[].id` 与 `agent-default-model.model` 改成 `qwen3.8-27b`；`bole-model` 只是基线占位。
2. 选 `rsi-infer` 且模型命中允许列表（`qwen3.8-27b` 或 `Ornith-1.5-35B-A3B`）时，收据不会是 `unsupported-model`。**两个模型都能用经验，各自加载自己的包**（Qwen 服务经验 v3 / Ornith V18），按准确模型 ID 隔离、互不串用。是否 `delivered` 取决于任务类型——**只有明确「启动 / 部署 / 保留服务」的公开任务才自动投递正文**，普通代码任务只到 `available`（Skill 目录 + 路由提示，正文需显式 `skill` 加载）。若现场把模型改出这两者，收据变为 `unsupported-model`，经验正文不投递。同一会话在首个请求后即绑定模型，两个模型不能混用，换模型要新建任务。

配置面板只需要 Provider ID、显示名称、地址、协议与至少一个可识别模型；上下文窗口与最大输出可由「获取可用模型」（`llm/discoverModels`）带入，端点不公布时用默认值。

现场调整方式：配置文件放在固定路径并在文档中给出；运行时启动时做一次自检（地址不可达、模型不存在、鉴权失败分别给出明确错误码），避免现场只看到「连不上」。

### 2.4 交付边界：SDK 与示例由我方负责，IDE 对接归伙伴（对应第 6 项）

负责人说「主体语言是 TypeScript，不影响 SDK 构建」，这句话成立，并作为划边界的依据：我方交付的是 C 接口的 SDK，与调用方用什么语言无关；伙伴用 TypeScript 也好、C# 也好，绑定与 IDE 集成由他们自己完成。

据此把工作范围划清：

| 范围 | 归属 | 内容 |
| --- | --- | --- |
| SDK 本体 | 我方 | DLL、头文件、导入库、随包运行时、错误码 |
| 运行期组合 | 我方 | profile 补丁、preset 声明、经验包随包、配置预置 |
| 示例程序 | 我方 | 控制台示例与界面示例，用于证明 SDK 可用、长任务不卡 |
| 文档 | 我方 | 接口说明、集成指南、离线部署、错误码 |
| IDE 侧绑定与集成 | 伙伴 | FFI 绑定、插件壳、界面实现、联调 |

一处必须提醒：示例程序不能省略。任务书第四节把「控制台示例 + 界面示例」列为交付物，验收标准第 1 条要求示例程序跑通全流程，第 2 条要求任务执行期间界面可交互、取消生效。示例是我们自己的演示程序，不依赖对方技术栈，也不等于替伙伴做对接。

界面示例的选型因此不再受伙伴 IDE 形态影响，取我方最省力的方案：Electron 主进程调 DLL、worker 线程跑任务、渲染进程只显示事件，复用仓库桌面端已有的 Windows 打包路径；若考虑离线包体积，可退为 C++ Win32 窗口，两者证明力相同。界面必须展示五项：输入、提交、实时状态区、结果区、取消按钮。

可选的小成本项：集成指南里放十来行调用片段（例如 koffi）作为文档内容，帮助对方理解怎么接，但不提供可运行工程、不提供 IDE 插件。边界在文档里写明：SDK 侧问题我方负责，绑定与 IDE 集成由伙伴负责，需要我方额外支持时另行约定。

### 2.5 验证阶段：暂缓（10-22 复核，对应第 4 项）

10-21 的口径是「验证映射到伯乐完成检查」。10-22 在源码上核对：

- 会话事件词汇表 60 项里没有 verify 类事件（`core/session/src/known-event-types.ts`）。
- 仓库里没有「完成检查」的实现；`skill/skill-bole-experience` 只在 model 请求前注入只读经验并记收据，不做检查、不写回记忆库。
- 在完整宿主工作区（本仓库 `packages/` 覆盖到上游 tag `dsh-v0.1.7-alpha.2` 的 pnpm 工作区）上再查一遍：没有完成检查实现，也没有额外的伯乐记忆包；唯一的「完成检查」字样在 typert 生成器文档里，与 RSI 无关。
- 因此运行时没有可投影的「验证」阶段。

结论：**暂缓**。`VERIFY_CHECK` 的定义保留在事件模型里，但本运行时不产生、不承诺送达；等产品侧（若完整 bole-agent 侧确有完成检查）给出会话事件或 SDK 可达的服务方法，再冻结映射关系。对伙伴的接口说明要写明：验证阶段暂不可观测，事件序列不会为它伪造状态（验收第 5 条不受影响，但任务书第 5 条的「验证」阶段只能标注暂缓）。

### 2.6 取消语义：立即中断（10-22 确认）

已确认口径：立即中断，映射内核现状，不做步末终止。

- **控制面**：`session/cancel` 立即返回，不等待模型或工具收尾。
- **内核行为**：`Agent.cancel({ kind: 'user' }, { keepInbox: false })` abort 当前任务；模型请求中断；已开始的工具调用中断；未开始的工具调用补一条 `tool/result`（`aborted before dispatch`）；排队但未开始的用户消息丢弃。
- **终态**：`turn/end` 的 `reason.kind = aborted`，SDK 映射 `TASK_FINISHED`，`outcome = aborted`（不是 `TASK_FAILED`）。
- **无活动可取消**：返回 7xxx，不当作成功。
- **不支持**「步末终止」：那需要在 SDK 侧另造任务队列语义，与「薄封装」冲突。

### 2.7 三层、思考增量与轨迹（10-22 确认）

- **三层**：会话 `/` 任务（内核 `turn`）`/` 步骤（内核 `step`）。补 `STEP_STARTED`、`STEP_FINISHED`，来源 `step/start`、`step/end`（载荷 `{ turn, step }`）。
- **thinking**：`agent/assistant-stream` 的 chunk 有 `reasoning-delta`。协议 `assistant.delta` 增加 `channel: text | reasoning`，SDK 事件拆成 `TEXT_DELTA` 与 `REASONING_DELTA`，合并/限频策略相同；两者都是尽力而为，正文最终以 `assistant/message` 为准。
- **轨迹**：`session/trace` 支持按 `turn` 过滤，落点是会话日志（内存或持久化 JSONL），由自建插件实现，不依赖 Web 的 `session-log-export`（那是浏览器下载）。
- **恢复**：新增 `session/resume`，落点 `ctx.agents.resume`；要求 `sessionPersistence` 与 `sessions` 查询服务，preset 定义缺失时拒绝恢复。
- **模型事件**：`MODEL_SELECTED` 只取 `request/header`（SDK 组合可用）；`model/selection` 由 `api/session-controller` 写入，SDK 运行时不挂这个包，不列为来源。模型在首个请求后与会话绑定，换模型要新建任务。

### 2.8 开发取舍（10-22 第二轮确认）

| 编号 | 事项 | 结论 | 落点 |
| --- | --- | --- | --- |
| 18 | 一进程多项目 | 支持：`session/create` 带可选 `cwd`；`bole_config` 带默认 `cwd`；沙箱按会话头 `cwd` 解析 | 02 §2、04 §2 |
| 19 | 图片/附件输入 | v1 支持：保留 `bole_submit` 纯文本，另加 `bole_submit_blocks`（JSON content blocks）；协议沿用官方 text/image 块 | 02 §2、04 §3 |
| 20 | 默认沙箱 | `workspace-write` + `approval.policy: never`；启动自检沙箱后端，不可用报 `SANDBOX_UNAVAILABLE`，允许配置切 `danger-full-access` | 01 §7、03 §2、04 §2 |
| 21 | 原始事件 | 不透传；排障用 `session/trace` 导出原始 JSONL | 03 §2 |
| 22 | 自建 TS 包位置 | 放 `runtime/packages/`（交付侧），不并进 `sdk/` 与 `bundle/` 包组 | 01 §4 |

外部口径（同日，未改变契约）：验收机是「无外网但有内网」，模型为自部署，先按现有地址调用，不行再改；Windows 构建环境暂缓，先文档与分工；目标机安全策略（未签名 DLL / EDR）仍未知，按未签名交付并在文档写风险；两人已在现场；对外文档只用 Markdown，本轮只做文档与分工。验证阶段仍按 2.5 暂缓。

### 2.9 分工前的文档冻结（10-22 第三轮）

项目负责人要求：**先不继续开发，把约束与接口落成文档，供 A/B 两人分工**。执行口径：

- 02/03/04 冻结 1.0；本轮补齐 05（测试）、07（离线包）、08（分工与接口）与 `docs/delivery/` 索引。
- `runtime/packages/sdk-server` Stage 0 是 A 轨负责人的参考实现；B 轨（C 实现、测试、示例、打包）**未开始**，本会话不代 B 做实现决定。`prototypes/native-c-sdk/` 是未授权参考，不属于交付物，B 负责人可决定采用/丢弃/重写。开发暂停，按 08 的工作包领取。
- 契约变更走 08 第三节：只增不改、先改 fixture、A+B 双签、同步 02/03/04 与 `bole_agent.h`。
- 分工边界走 08 第二节：A 拥有 `runtime/packages/` 等，B 拥有 `native/`、`examples/console-c`、`examples/gui-win32`、`packaging/`、`docs/delivery/`；契约与 fixtures 共同拥有。
- 文档不写本机绝对路径、用户名、机器名、临时目录；命令用 `$REPO` / `$HARNESS_WORKSPACE` / `$DEV_HOME` / `$PATCH_FILE` 或 `<...>` 占位符，环境地址写「由部署配置提供」。

## 三、确认结果产生的新工作项

1. 补丁层组合 preset 面：禁用 base 的 agent 面行，插入 `agent-preset-registry` 与 `standard` / `rsi-infer` 两条 preset；经验包随离线包发布（含 `assets/`）。
2. 冻结协议方法表：`session/create`、`session/resume`、`session/cancel`、`session/trace`、`session/setPreset`、`rsi.set`、`rsi.get` 必做；`assistant.delta`（双通道）、`rsi.state` 必做。
3. 冻结事件模型：补 `STEP_*`、`REASONING_DELTA`、`RUNTIME_READY/FAILED/EXITED`、`TASK_FINISHED.outcome`；`PLAN_UPDATED` 载荷按 `plan/mode` 与 `todo/write` 的真实字段重写；`VERIFY_CHECK` 标暂缓。
4. 冻结 C ABI：事件回调进配置结构、`bole_wait_ready`、`bole_session_set_preset`、`bole_rsi_set/get`、`bole_trace_export(..., turn)`；取消语义与 `release` 顺序写死。
5. 补控制台示例与界面示例（Electron，可退为 Win32），界面五项必须齐；不提供 TypeScript 工程与 IDE 插件，集成指南内可放一段调用片段。
6. 离线包：关外网面（web 工具与 telemetry）、`approval.policy: never`、`DSH_HOME` 独立目录、`VERSION.json` 含 preset/经验包版本。
7. 现场核对模型 ID 与经验允许列表；不命中就在交付说明里写清 RSI 只能到 `unsupported-model`。

## 四、已经确定、不需要再确认的事项

| 事项 | 结论 | 说明 |
| --- | --- | --- |
| 交付形态取向 | 客户轻量化接入 | 按第一节四条验收条款落地 |
| 交付文档语言 | 仅中文 | 接口说明、集成指南、离线部署、错误码四份；不进 `packages/*/README`，避免中英配对门禁 |
| 验收用示例工具 | 由我方提供玩具 MCP server | 符合任务书「不依赖具体第三方工具」；伙伴自有工具按调用方注册处理 |
| 验收机器 | 由对方提供 | 我方自备干净 Windows 虚拟机先演练 |
| 目标操作系统 | Windows 10 / 11（x64） | 不做更老系统兼容；验收机按这两版准备 |
| 数据面形态 | 事件回调（不用队列拉取） | 回调由 SDK 自有线程触发，调用方自行切 UI 线程 |
| 取消语义 | 立即中断 | 见 2.6 |
| RSI 实现 | agent preset 切换 | 见 2.2 |
| 运行组合 | `--profile sdk` + 补丁层 | 关外网面、approval=never，见 `01-plan.md` 第七节 |
| 工具注入 | v1 只支持配置文件声明 + 重启生效 | 已确认（10-22）；运行时注册二期 |
| 离线包 preset | 只挂 `standard` + `rsi-infer` | 见 2.2 |
| 交付基调 | 保障顺利交付，提前备资料 | 见下方清单 |
| 会话 cwd | 按会话传，缺省回落默认 cwd | 见 2.8 第 18 项 |
| 图片输入 | v1 支持文本 + 图片块 | 见 2.8 第 19 项 |
| 默认沙箱 | workspace-write + never，可切 danger | 见 2.8 第 20 项 |
| 原始事件 | 不透传，trace 导出 | 见 2.8 第 21 项 |
| 自建包位置 | `runtime/packages/` | 见 2.8 第 22 项 |
| 验收网络 | 无外网、有内网 | 自部署模型，先按现有地址调用 |
| 对外文档 | Markdown | 本轮只做开发 |
| Windows 构建 | 暂不处理 | 先出可运行代码 |

交付资料清单（建议在 M3 前备齐）：

| 资料 | 用途 |
| --- | --- |
| SDK 包（DLL、头文件、导入库、运行时目录） | 集成主体 |
| 依赖清单与版本清单 | 对应任务书「依赖清单」，含运行时版本、node 版本、preset 与经验包版本、随包工具 |
| 四份文档（接口说明含线程模型、集成指南、离线部署、错误码） | 对应任务书文档要求 |
| C 控制台示例、界面示例源码 | 对应任务书示例要求 |
| 玩具 MCP server 与示例配置文件 | 验收演示 |
| 验收演练脚本与检查表 | 现场按表逐条勾 |
| 未签名程序说明与哈希清单 | 应对现场安全软件拦截 |
| 常见故障排查页 | 现场定位用 |

## 五、技术现状与事实核对（2026-10-22 源码复核）

### 协议与运行时

| 事实 | 依据 |
| --- | --- |
| 交付基线为 harness `0.1.7-alpha.2` | 各包 `package.json` 版本字段 |
| 官方 SDK 协议只有 3 个请求（initialize / session/prompt / shutdown）与 4 个通知（session.event / session.status / subagent.started / subagent.finished） | `sdk/protocol/src/types.ts` 的 `HarnessSdkRequestMap`、`HarnessSdkNotificationMap` |
| 官方服务器类与传输类公开导出，可被自建插件复用 | `sdk/server/src/index.ts` 的 `export * from './server.ts'`、`sdk/protocol/src/index.ts` |
| 官方 `initialize` 是就绪边界，会 `await loader` 后再答复 | `sdk/server/src/index.ts` |
| transport 的 `onRequest` 单槽位，后注册覆盖先注册 | `sdk/protocol/src/transport.ts` |
| 补丁层可禁用官方条目并插入自有插件（已实测） | `bundle/sdk-app/cordis.patch.yml` 的 `disabled` + `insert`；0.1.7 沙盒探针 |
| 取消能力在内核存在，但未暴露到 SDK 协议 | `core/agent/src/runtime-types.ts` 的 `Agent.cancel(cause, options)`；`CancelOptions.keepInbox` |
| 官方还有 TypeScript 客户端 `@deepseek-ai/dsh-sdk-client`，是同一协议的参考实现 | `sdk/client/src/api.ts`（`DeepSeekHarness` / `HarnessSession`），可作为 C 客户端的语义对照 |
| `--profile sdk` = `dsh-base` + `dsh-sdk-app`；另有独立精简组合 `--profile sdk-minimal` | `boot/app-boot/src/profile.ts` 的 `PROFILE_TEMPLATES` |
| `sdk-minimal` 不套 base：只有 DeepSeek adapter、shell、JSONL 会话，无 bole 路由与 preset 面 | `bundle/sdk-minimal/cordis.patch.yml` |
| `--patch` 覆盖层、`DSH_HOME/profiles/<name>` 均存在 | `boot/cmdline/src/index.ts`、`boot/app-boot/README.md` |

### 事件与轨迹

| 事实 | 依据 |
| --- | --- |
| 会话事件词汇表 60 项，无 verify 类事件，也无 `assistant/chunk` | `core/session/src/known-event-types.ts` |
| token 级增量是瞬时帧 `agent/assistant-stream`，帧含 `attemptId` / `revision`，chunk 含 `text-delta` / `reasoning-delta` / `tool-call-delta` / `usage` / `finish` | `core/agent/src/runtime-types.ts`、`core/agent-loop/src/assistant-stream.ts`、`llm/llm/src/types.ts` 的 `StreamChunk` |
| 三层可投影：`turn/start`、`turn/end`、`step/start`、`step/end` | `core/session/src/types.ts` 的 `SessionEventMap` |
| `turn/end.reason` 共 7 种：completed / aborted / blocked / error / max-tokens / interrupted / forked | 同上 `TurnEndReasonMap` |
| `plan/mode` 载荷只有 `{ active: boolean }`；计划正文在 `exit_plan_mode` 的工具调用参数里 | `plan/plan-mode/src/index.ts` |
| `request/header` 由 agent-loop 写，含 provider/model/reasoningEffort 与工具表 | `core/agent-loop/src/agent.ts` |
| `model/selection` 由 `api/session-controller` 写，SDK 组合不挂该包 | `api/session-controller/src/agent.ts` |
| 按会话导出在 Host 侧没有现成落点；`session-log-export` 是 Web 浏览器下载 | `session-query/session-log-export/README.md` |

### 模式与 RSI

| 事实 | 依据 |
| --- | --- |
| 五种内置 preset：standard / ptc / minimal / cordis / rsi-infer | `bundle/web-app/presets/*.patch.yml` |
| `rsi-infer` 等于 `standard` 再加一行经验插件 | 两份 preset 文件逐行对比 |
| registry 默认 `standard`，即 RSI 默认关闭 | `bundle/web-app/cordis.patch.yml` 的 `agent-preset-registry.config.default` |
| preset 只能在空白会话切换，之后 `agent-preset/locked` | `preset/agent-preset-registry/src/index.ts` 的 `select()` |
| 会话头记录 `agentPreset`；投影从 header 初始化，之后由 `agent-preset/selected` 推进 | `preset/agent-preset-registry/src/session.ts` |
| 经验插件 `@deepseek-ai/dsh-skill-bole-experience` 在**本仓库**，版本 `0.1.7-alpha.2`，`publishConfig.access: public` | `skill/skill-bole-experience/package.json`（此前「不在公共 npm」的判断作废） |
| 经验只对两个允许模型 ID 投递：`qwen3.8-27b`（Qwen 服务经验 v3）、`Ornith-1.5-35B-A3B`（Ornith V18）；两者按准确 ID 隔离、不串用 | 插件 README、`assets/final-experiences.json`、`client/ui-agent-preset/src/client/locales.ts` 与 `guide-locales.ts` |
| 投递收据：`user/message.source.kind = bole-experience`，字段含 status / deliveredMemoryIds / bundleId / provider / model / bodySha256 | `skill/skill-bole-experience/src/index.ts` |
| 经验是只读注入：不写记忆库、不做在线自改进/审查/晋升 | 插件 README |
| 目标网关模型 ID 为 `qwen3.8-27b`，在经验允许列表内 | 10-22 确认；插件 README 与 `assets/final-experiences.json`（`models: ["qwen3.8-27b", "Qwen3.8-27B"]`） |
| 经验自动投递只对「启动 / 部署 / 保留服务」类公开任务出现；普通任务为 `available` | 插件 README 与 RSI-infer 交接「核查投递与隔离」段 |
| 完整宿主工作区内也没有完成检查实现，没有额外伯乐记忆包 | 10-22 在 `packages/`、`apps/`、`docs/` 检索；`node_modules/@deepseek-ai` 下没有 bole 相关包 |
| base 的 agent 面是单会话全局组合，Web 已把它整体挪进 preset 面 | `bundle/web-app/cordis.patch.yml` 的「the agent plane moves behind agent presets」段 |

### 工具、网络与审批

| 事实 | 依据 |
| --- | --- |
| MCP：每 server 一条 `@deepseek-ai/dsh-mcp-client`，`transport` 取 `stdio` 或 `streamable-http` | `mcp/mcp-client/src/index.ts` 的 `Config` |
| 工具超时可配 `toolCallTimeoutMs`，启动失败可配 `failOnStartupError`（默认 false） | 同上 |
| 审批只由 `tools/pre-execute` 返回 `ask` 触发；SDK 组合不挂载会产生 `ask` 的组件（`hooks-claude-code`、`auto-review`），工具调用不会停在审批上；仍固定 `never` 作兜底 | `core/tools/src/index.ts` 的 `serviceAsk`；`hooks/hooks-claude-code`；`deliverables/workspace-changes` 只调 `next()` |
| `approval: ask` 在没有应答者时 fail-closed（`unavailable`），headless SDK 必须显式设 `never` | `interaction/user-approval/src/index.ts` |
| base 挂了公网面：`web`、`tool-web`、`web-search-deepseek`、`web-fetch-http`、`session-telemetry-otel` | `bundle/base/cordis.patch.yml` |
| 工具超时策略由 `@deepseek-ai/dsh-tool-call-timeout-policy` 承担 | `bundle/base/cordis.patch.yml` |
| npm 11 默认拦截生命周期脚本，Windows 打包机需先 `npm approve-scripts` | 10-21 安装告警 |
| `node-pty` 自带 win32-x64 预编译；`koffi` 无预编译，安装时需联网取预编译或现场编译 | 10-21 解包与安装日志 |
| PyPI 的 `deepseek-harness-runtime-bin` 没有 0.1.7 的 win-x64 wheel | 10-21 PyPI 版本表 |

## 六、下一步（2026-10-22 第三轮起：先文档后开发）

1. **文档签收**：项目负责人签收 02/03/04/header、05/07/08 与 `docs/delivery/` 索引；A/B 填 08 第十节领取表。
2. **契约不变**：开发按 02/03/04 实现；变更走 08 第三节（fixture 先行、双方签字、只增不改）。
3. **fixtures**：C1 先捕获真实 wire 报文入库；C 与插件用同一份回放。
4. **M0**：控制台示例 + 玩具 MCP + 真实运行时跑通一次工具调用并计时。
5. **preset 面**：A4 在真实 Windows 上跑——`qwen3.8-27b` 与 `Ornith-1.5-35B-A3B` 各一轮，`standard` / `rsi-infer` 各建会话；`standard` 无收据；`rsi-infer` 在服务生命周期任务上 `delivered`、普通任务 `available`、非允许模型 `unsupported-model`；确认只加载当前模型对应的包；空白期切换成功、首 turn 后 `locked`。Windows 环境按 10-22 口径暂缓。
6. **M3**：干净 Windows 虚拟机（无外网、内网模型可达）演练四条验收标准。

## 七、确认记录

| 编号 | 结论 | 确认人 | 日期 | 备注 |
| --- | --- | --- | --- | --- |
| 1 | 满足客户轻量化接入即可 | 项目负责人 | 2026-10-21 | 接受 DLL + 随包运行时，落成四条验收条款 |
| 2 | 可以，最上层加 patch 挂载，增加自定义事件 | 项目负责人 | 2026-10-21 | 实现方式不设限，采用补丁层 + 自建插件 |
| 3 | RSI 只控制经验注入，运行时不做沉淀 | 项目负责人 | 2026-10-21 | 10-22 细化：RSI 由 `rsi-infer` 模式承载，开/关 = 切换 preset |
| 4 | 验证映射到伯乐完成检查 | 项目负责人 | 2026-10-21 | 10-22 复核：本基线无该信号，改为暂缓 |
| 5 | 现有信息基本够用 | 项目负责人 | 2026-10-21 | 10-22 复核：base 已预置 `bole` / `bole-model` / 占位 key |
| 6 | 主体语言 TypeScript，不影响 SDK 构建；只交付 SDK 与示例，对接由伙伴负责 | 项目负责人 | 2026-10-21 | 边界见 2.4 |
| 7 | 保障客户交付，提前备资料 | 项目负责人 | 2026-10-21 | 按内部清单执行 |
| 8 | 取消按立即中断写 | 项目负责人 | 2026-10-22 | 映射 `Agent.cancel`，终态 `aborted` |
| 9 | 补 step 事件、按 task 过滤轨迹、加 `session/resume` | 项目负责人 | 2026-10-22 | 见 2.7 |
| 10 | thinking 也进事件流 | 项目负责人 | 2026-10-22 | 协议加 `channel`，事件拆两种 |
| 11 | 用 `--profile sdk` + 补丁层；关外网面；approval=never | 项目负责人 | 2026-10-22 | 见 `01-plan.md` 第七节 |
| 12 | RSI 集成在 RSI-infer 模式里，开/关即切模式 | 项目负责人 | 2026-10-22 | 见 2.2；`standard` 与 `rsi-infer` 都不改原四种模式 |
| 13 | 有完成检查就映射，没有就暂缓 | 项目负责人 | 2026-10-22 | 复核：本仓库与完整宿主工作区都没有该实现，按暂缓执行 |
| 14 | 网关模型 ID 是 `qwen3.8-27b` | 项目负责人 | 2026-10-22 | 命中经验允许列表；另一个允许模型是 Ornith-1.5-35B-A3B，两者各自加载对应经验 |
| 15 | 离线包只挂 `standard` + `rsi-infer` | 项目负责人 | 2026-10-22 | 其余三种模式本期不随包 |
| 16 | MCP v1 只支持配置文件声明 + 重启生效 | 项目负责人 | 2026-10-22 | 运行时注册放二期 |
| 17 | 目标机支持 Windows 10 / 11 | 项目负责人 | 2026-10-22 | 不需要额外考虑，不做更老系统兼容 |
| 18 | 一进程多项目 | 项目负责人 | 2026-10-22 | 按会话 cwd；`bole_config` 带默认 cwd |
| 19 | 图片/附件输入 | 项目负责人 | 2026-10-22 | 加 `bole_submit_blocks`，保留纯文本入口 |
| 20 | 默认沙箱 | 项目负责人 | 2026-10-22 | workspace-write + never，自检失败报码可切 danger |
| 21 | 原始事件 | 项目负责人 | 2026-10-22 | 不透传；trace 导出原始 JSONL |
| 22 | 自建 TS 包位置 | 项目负责人 | 2026-10-22 | `runtime/packages/`，不并包组 |
| 23 | 先文档后开发，供分工；文档不写本机路径 | 项目负责人 | 2026-10-22 | 02/03/04 冻结；05/07/08 与 delivery 索引本轮补齐；A 轨 Stage 0 为 A 的参考，B 轨未开始；路径用占位符 |

## 八、实测记录

### 2026-10-21　`0.1.7-alpha.2` 沙盒

| 验证 | 做法 | 结果 |
| --- | --- | --- |
| 版本可获得 | `npm install @deepseek-ai/dsh@0.1.7-alpha.2` | 511 包、538 MB、约 2 分钟；`dsh --version` 输出 `0.1.7-alpha.2` |
| sdk profile 可用 | `dsh --profile sdk --dump-config`，以及带 EOF 直接启动 | 组合树完整，进程退出码 0 |
| SDK 协议全链路 | 向 stdin 依次写入 initialize / session/prompt / shutdown | initialize 0.6–0.8 秒返回；prompt 产生 turn/start、step/start、tool/call、tool/result、turn/end 等事件；shutdown 后进程退出码 0 |
| 真实工具调用 | 本地假 OpenAI 兼容服务 + `--patch` 注入 provider `bole` | 模型发起 `read` 调用，工具执行并回传结果，第二轮给出最终文本，`turn/end` 为 completed |
| 补丁层替换官方服务器 | `disabled: true` 官方条目 + insert 本地插件接管 stdout | 自定义方法 `bole/ping` 正常返回，退出码 0 |
| token 增量来源 | 插件订阅 `agent/assistant-stream` | 收到 start、text-delta×2、end 帧；会话事件中没有 `assistant/chunk` |
| 无模型凭据时的行为 | 不配 key 直接 prompt | initialize 成功；turn 走到 `turn/end` 报 `no api key for provider` |

环境限制：内网模型端点（地址由部署配置提供）从开发机不可达，DeepSeek 官方密钥未配置，真实模型效果需在内网机器上复验。

### 2026-10-22　源码复核（非沙盒）

在交付仓库上核对第五节各项，确认：RSI 已是 preset（`rsi-infer`），经验插件在本仓库且公开；本基线无验证阶段信号；base 挂公网面且审批默认 `ask`；transport/initialize 的就绪边界与单槽位约束。另在完整宿主工作区上核对：`packages/` 是本仓库的拷贝加上游 tag，`apps/` 只有 cli/desktop/web，没有额外的伯乐记忆包或完成检查实现。未执行 preset 面的运行时验证——需要在 Windows 上按第六节第 3 条补。

沙盒脚本未入库（临时目录），含假模型、注入补丁与两个插件探针；需要时按上表复现。这一条是验收演练的缺口：没有脚本就不能证明可复现，建议 M1 前入库。

### 2026-10-22　参考实现（开发暂停前）

| 验证 | 做法 | 结果 |
| --- | --- | --- |
| C SDK 垂直切片（未授权参考，不属于 B 轨） | `cmake -S prototypes/native-c-sdk ...` + `ctest` | 2/2 通过，仅证明 04/07 的接口可走通；B 负责人决定采用/丢弃/重写 |
| 插件 Stage 0（A 轨参考） | 禁用官方 `sdk-jsonrpc-server`，用绝对 `.ts` patch + `pnpm dsh` 加载自建插件 | `initialize` / `session/prompt` / `turn/end` / `shutdown` 全链路，退出码 0，stderr 有 `[bole-sdk-server] loaded` |
| 交付补丁组合 | `--profile sdk --patch runtime/packages/sdk-app/cordis.patch.yml --dump-config` | 模型 `qwen3.8-27b`、`approval: never`、web/telemetry 关闭行生效 |
| 插件类型检查 | `tsc -p runtime/packages/sdk-server/tsconfig.json --noEmit` | 通过 |

以上只作参考实现，不作为验收依据；A 轨由 A 负责人决定，B 轨实现由 B 负责人决定，正式实现按 08 的工作包推进。
