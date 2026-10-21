---
description: "RSI-infer 交接：模式对照、最终经验来源、完整宿主接入、本地预览和验收限制。"
---

# RSI-infer 开发与部署交接

[English](RSI-INFER-HANDOFF.md) | 中文

## 概述

接手者可以在相同模型和任务条件下，对比标准模式与 RSI-infer 的最终经验投递。开发分支为 `feat/rsi-infer-experience-mode`，基于用户提供的 `bole-dsh-packages` 远程仓库，而非把另一条伯乐开发分支直接推入该仓库。经验只读、按准确模型 ID 隔离；历史评测分数不代表这版集成已经提分。

## 目录

- [模式与资产](#modes-and-assets)
- [完整宿主接入](#host-integration)
- [核查投递与隔离](#delivery-and-isolation)
- [验证记录](#verification)
- [本地预览](#local-preview)
- [验收与回退](#acceptance-and-rollback)
- [开发备注](#dev-note)

-----

<a id="modes-and-assets"></a>
## 模式与资产

标准模式是无伯乐经验的对照；RSI-infer 保留同一标准插件组合，只额外挂载经验插件。普通项目 Skills 在两者中均保留，不能把“无经验”解释成关闭所有 Skills。原四份预设补丁未修改；不改变 agent loop、温度、重试、工具预算或现有记忆库，也不运行在线自改进、审查或候选晋升。

| 显示名称 | 预设 ID | 挂载伯乐经验 |
|---|---|---|
| 标准模式 | `standard` | 否 |
| PTC 模式 | `ptc` | 否 |
| 极简模式 | `minimal` | 否 |
| 创造模式 | `cordis` | 否 |
| RSI-infer 模式 | `rsi-infer` | 是 |

[冻结资产](../../skill/skill-bole-experience/assets/final-experiences.json)是正文、允许模型、来源部署和历史运行身份的唯一数据源。正文修改必须同步更新 SHA-256 与所属快照，并重新验证；不能直接把新候选写成最终经验。

| 准确匹配的模型 ID | 经验包 | 已核实的来源成绩 |
|---|---|---|
| `qwen3.8-27b`、`Qwen3.8-27B` | `qwen38-service-v3`，服务经验 v3 第一轮 | Terminal-Bench 2.1：63/89，70.79% |
| `Ornith-1.5-35B-A3B`、`ornith-1.5-35b-a3b` | `ornith-v18`，已核实最高分的 Ornith 经验运行 | Terminal-Bench 2.1：66/89，74.16% |

两份经验均针对服务在 Agent 退出后继续存活及对外可验证的生命周期，不是覆盖全部代码修复任务的经验集，也不是历史运行包的完整复刻。这些来源成绩不是本仓库复测结果，更不能当作 Deep SWE 提升证据；本次集成没有跑新的评测全量。

-----

<a id="host-integration"></a>
## 完整宿主接入

本仓库根目录对应完整 DeepSeek Harness 的 `packages/`，不能当成完整应用根目录直接安装和启动。宿主还需要 `apps/`、`vendor/`、`native/`、构建脚本和根 TypeScript 配置。本地已构建宿主采用官方 `dsh-v0.1.7-alpha.2`，覆盖本仓库 package 文件，同时保留本仓库未包含的上游包。

宿主集成者将本分支文件放入宿主 `packages/` 对应位置，保留其他宿主内容，并补齐以下根配置。新插件自身和 Web bundle 的 tsconfig 已在本分支内；不要移动源文件或降低类型检查来适配 packages 子树。

在宿主 `tsconfig.base.json` 的现有 `compilerOptions.paths` 中加入此项，保留其他别名：

```json
{
  "@deepseek-ai/dsh-skill-bole-experience": ["./packages/skill/skill-bole-experience/src/index.ts"]
}
```

在宿主 `tsconfig.host.json` 的现有 `references` 数组中加入此项，保留其他引用：

```json
{ "path": "./packages/skill/skill-bole-experience" }
```

随后使用完整宿主的正常安装、构建和 `dsh` profile 启动流程；本地完整构建已完成。新包是 Cordis 功能插件，不是单独可启动的应用，也不是带 `dsh.bundle.patch` 的独立组合包；Web bundle 负责挂载它。发布必须包含 `assets/`，否则启动校验失败。

新增收据来源还需要完整宿主持久化历史中的同版本兼容记录。本地验证宿主已生成并验证该记录，未改动格式 4 或已接受的检查点。本仓库只有 packages 子树，不能推送宿主根 `docs/` 历史；集成者需使用附带的[双语说明](rsi-infer-persistence-prose.json)，针对实际宿主生成：

```sh
pnpm run persistence-changes --record 2026-09-28-bole-experience-attribution --decision same-version --prose packages/bundle/web-app/rsi-infer-persistence-prose.json
pnpm run verify-persistence-changes
pnpm run verify-persistence-formats --write
pnpm run verify-persistence-formats
```

生成器负责双语确认记录、schema 快照及当前持久化目录。将这些输出提交到完整宿主仓库，而不是放入 `packages/docs/`。若同名记录已存在，应验证而非覆盖；无关宿主变更需另行审查确认。不要降低检查器或提升 Session 格式来跳过兼容扩展记录。集成后也需重新生成 `doc-sync` 要求的其他宿主目录。

| 接手入口 | 职责 |
|---|---|
| [预设补丁](presets/rsi-infer.patch.yml)与[Web manifest](package.json) | 标准能力加经验插件、依赖和补丁顺序 |
| [经验插件说明](../../skill/skill-bole-experience/README.zh.md) | 配置、模型可见内容、长期限制 |
| [资产校验](../../skill/skill-bole-experience/src/bundles.ts)与[插件](../../skill/skill-bole-experience/src/index.ts) | 正文哈希、公开任务匹配、Agent 范围、路由校验和释放 |
| [模式映射](../../preset/agent-preset-registry/src/display.ts)与[界面文案](../../client/ui-agent-preset/src/client/locales.ts) | 第五种模式的显示与说明 |

-----

<a id="delivery-and-isolation"></a>
## 核查投递与隔离

模式标签不代表经验正文已进入模型。核查持久 `user/message` 的 `source.kind: bole-experience`，并结合实际 `skill` 工具结果判断是否加载正文。

| 状态 | 含义 |
|---|---|
| `delivered` | 完整正文自动加入请求；`deliveredMemoryIds` 非空，核对当前 provider/model、包 ID 和正文哈希 |
| `available` | 只有所属模型的 Skill 目录和短提示，不能算正文投递；显式加载要看 `skill` 结果 |
| `unsupported-model` | ID 不在允许列表，不携带伯乐正文 |
| 标准模式 | 不挂载本插件，不应出现其收据或最终伯乐 Skill |

明确启动、部署或保留服务的中英文公开任务可触发自动正文投递；一般代码问题未必匹配，但所属模型 Skill 可以显式加载。匹配不读取隐藏测试、金标或验证器答案。完整自动消息默认上限为 4096 个 UTF-8 JSON 字节，包含包装与收据；超限阻止推理而非截断正文。

每个 Agent 独立注册所属模型 Skill，直接加载其他模型经验也被拒绝。首次推理后会话绑定准确 provider/model；切换模型须新建任务，压缩不能解除绑定，带其他路由经验的分叉历史也会被拒绝。ID 只能核查配置路由，运维仍需确认后端实际权重，不能将其他模型伪装成允许 ID。

-----

<a id="verification"></a>
## 验证记录

以下为 2026-09-28 本地接入已观察证据与缺口，不是完整 CI 或评测验收。新交接文档的提交前检查结果以本次交付报告为准。

| 范围 | 结果 |
|---|---|
| 五份定向测试文件 | 56 项通过：经验插件 21、预设清单 1、显示映射 4、界面相关 30 |
| 隔离与模型可见测试 | 真实 Loader、持久消息快照、跨模型直接调用拒绝、路由切换、压缩后绑定、并发 Agent、释放和完整字节限制均有覆盖 |
| 最终受影响复测 | 插件、预设清单及相邻两个持久化来源文件共 64 项通过，包含插件卸载后的 JSON 收据恢复 |
| 类型与构建 | 相关叶子类型检查、完整宿主构建、新插件构建资产 smoke 已通过；相关依赖、JSDoc、不变量和 README 检查已通过 |
| 提交前检查 | 完整宿主 contract-ready lint 通过；改动 README 配对和插件包目录通过。广义 `doc-sync` 首轮 29 个门禁通过、13 个失败，包含宿主生成目录过期与 Windows symlink EPERM，未声明全绿 |
| 持久化与文档后续验证 | 同版本历史验证通过。快速文档检查 19 项通过；剩余的格式引用过期项重新生成后单独复验通过。不宣称完整 `doc-sync` 成功 |
| 本地 Web | 认证后 HTTP 200，真实页面显示 RSI-infer，监听仅为 `127.0.0.1:4317` |
| 全 GUI 单测 | 14 项失败、7486 项通过；含既有品牌/端点与上游期望差异、Windows symlink EPERM、CRLF 和连接测试，未声明全绿 |
| 全 Web replay | 89 项失败、22 项通过、410 项跳过；多数场景缺 Playwright Chromium，另有 fixture/连接问题，未声明全绿 |
| 真实模型与评测 | 未验证 Qwen/Ornith 真实调用；预览试发在默认 DeepSeek 路由遇到 `MISSING_CREDENTIAL`，不是经验效果测量 |

行为证据由[经验 tests](../../skill/skill-bole-experience/tests/)、[预设清单测试](tests/rsi-infer-preset.spec.ts)以及模式显示/界面所属 tests 维护。不要批量更新无关品牌快照、覆盖其他分支经验或关闭检查来让全仓库检查通过。

-----

<a id="local-preview"></a>
## 本地预览

本机预览位于当前任务工作目录下 `work/rsi-infer-local-20260928/`，使用相邻的完整宿主 `work/harness-host-20260928/`。独立 `state/` 保存设置与会话，`workspace-root/` 保存工作区，`logs/` 保存日志，不覆盖默认 Harness home。这些目录、构建产物、会话、启动 token 和本机脚本不属于推送内容。

| 本机脚本 | 用途与验证状态 |
|---|---|
| `start-preview.ps1` | 后台启动隔离 profile `rsi-local`；启动与重复启动检查已验证 |
| `open-preview.ps1` | 从启动日志读取认证 URL，在默认浏览器打开；已完成语法检查 |
| `stop-preview.ps1` | 仅停止匹配该 CLI 路径与 `rsi-local` 的进程并保留数据；已完成语法检查，未为验收停止正在使用的服务 |

首次访问使用宿主打印的带 token URL 或本机打开脚本；获得签名 cookie 后地址变为 `http://127.0.0.1:4317/`。新浏览器访问裸地址可能返回 401，这是认证要求，不是部署损坏。不要将 token、API Key、私钥或密码复制到仓库或交接文档。

运维需在“设置 → 模型”配置兼容协议、接口、凭据引用与准确模型 ID。既有 Qwen GPU 4–7 服务可按授权接入，但此预览未建立该连接，Ornith 地址也未配置。新预览未复制旧凭据，因此默认 DeepSeek 缺 Key 报错不是 RSI-infer 插件效果结果。

推送前的源码修正完善了 provider 释放并声明收据归属信息。正在使用的预览未被中断；真实模型验收前，应通过本机脚本重新构建并重启，使其使用最终源码。

-----

<a id="acceptance-and-rollback"></a>
## 验收与回退

使用两个新会话及相同初始工作区制作对照，不把改后的代码、旧会话经验或不同采样参数带入另一臂。以下真实模型验收由接手者在接口配置后执行。

1. 确认原四种模式和 RSI-infer 均可选择，标准仍为默认。
2. 用准确 Qwen ID 的公开服务任务对比标准与 RSI-infer：标准无伯乐收据；经验臂记录 Qwen 包及非空投递 ID，不含 Ornith 正文。
3. 用准确 Ornith ID 重复检查：仅有 Ornith 包，不含 Qwen 正文；用其他模型确认不投递经验。
4. 用不要求服务生命周期的任务检查 `available` 与显式 Skill 加载，不把目录提示误算成正文投递。
5. 检查首次推理后切换 provider/model 会在网络请求前拒绝，并确认新任务可以使用另一模型。
6. 对照记录质量、功能测试、耗时和异常；历史分数只说明来源，不从单题或小样本宣称整体提升。

停用经验只需新建标准模式任务，无需删除资产或清空记忆库。代码回退采用本功能之前的远程版本，不删除其他分支或覆盖原经验。已有 RSI-infer 日志保留原记录，不能充当新的无经验对照。

<a id="dev-note"></a>
## 开发备注

本节仅记录交接上下文，不取代包 README、源码或后续 CI。交接日期为 2026-09-28；下一步是接通真实模型、完成隔离验收，并解决完整宿主浏览器测试环境缺口，再安排同口径评测。此次不推送本机状态，也不操作原服务器源码树或评测 GPU。
