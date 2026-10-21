---
description: "在 RSI-infer 中使用冻结的 Qwen 或 Ornith 伯乐经验，按准确模型隔离，并保留持久投递收据。"
kind: "package-reference"
---

# @deepseek-ai/dsh-skill-bole-experience

[English](README.md) | 中文

## 概述

使用已确认的伯乐经验，不改变标准工具、模型采样或现有记忆库。RSI-infer 根据准确模型 ID 选择 Qwen 服务经验 v3 或 Ornith V18，提供适用的服务生命周期指引。标准模式不挂载此插件。其他模型不接收伯乐经验。

## 目录

- [使用本包](#use-this-package)
- [实现原理](#understand-the-implementation)
- [模型体验](#model-experience)
- [已知限制与延后工作](#known-limitations-and-deferred-work)
- [开发备注](#dev-note)

-----

<a id="use-this-package"></a>
## 使用本包

新任务选择 RSI-infer 使用经验；选择标准模式作为无伯乐经验对照。对比时保持模型、输入、权限和初始工作区一致。两种模式均保留普通项目 Skills。

### 最小配置

内置 [RSI-infer 预设](../../bundle/web-app/presets/rsi-infer.patch.yml) 在标准插件之外增加：

```yaml
- id: bole-experience
  name: '@deepseek-ai/dsh-skill-bole-experience'
  config:
    maxContextBytes: 4096
```

| 字段 | 默认值 | 含义 |
|---|---|---|
| `maxContextBytes` | `4096` | 正整数，限制完整自动消息的 UTF-8 JSON 字节数，包含包装和收据；超出时阻止请求进入，不截断经验 |

[最终资产](assets/final-experiences.json) 保存经验原文、哈希、允许的模型 ID、来源部署和历史全量运行身份。自动选择采用准确 ID，不使用模型家族的子串匹配。当前 provider/model 与历史来源部署分别记录。

| 允许的模型 ID | 冻结经验 | 历史结果 |
|---|---|---|
| `qwen3.8-27b`、`Qwen3.8-27B` | 服务经验 v3，第一轮 | 63/89（70.79%） |
| `Ornith-1.5-35B-A3B`、`ornith-1.5-35b-a3b` | Ornith V18，已核实最高分经验运行 | 66/89（74.16%） |

### 核查投递

来源 kind 为 `bole-experience` 的持久 `user/message` 记录选中经验包和当前路由。`status: delivered` 且 `deliveredMemoryIds` 非空，表示完整指引已进入该次请求。`available` 仅表示提供了 Skill 目录和路由提示；后续 `skill` 工具结果证明显式加载。`unsupported-model` 不携带经验。标准模式不产生这些收据。

-----

<a id="understand-the-implementation"></a>
## 实现原理

<details>
<summary>内部实现 — 点击展开</summary>

提示装配在读取 Skill 目录前解析当前模型。每个 Agent 拥有独立 provider：提供所属模型经验，并以不可调用项遮蔽从父级继承的其他模型经验。直接加载其他模型项不会返回正文。明确要求启动或保留服务的公开请求，通过已有持久化 pre-step 机制收到一次经验原文。请求监听器在进入推理前校验实际路由。

插件不读写实时伯乐记忆、不晋升候选、不改变自身上下文上限之外的预算，也不添加审查或重试策略。[来源选择](src/bundles.ts) 校验包内记录及正文 SHA-256；[插件](src/index.ts) 管理范围注册和收据。Agent 销毁或插件卸载时移除 provider。

收据声明为 producer 归属信息，不是恢复会话必须的解释器。未加载本插件的读者仍保留 JSON 元数据并派生持久化的用户正文；只有本插件据此去重并校验路由。完整宿主集成须通过宿主持久化历史生成器确认新增来源类型，无需提升 Session 格式版本。

**运行时不变量：** 不发布伴随模块。插件没有独立维护的状态投影；已有 Skill 注册表和持久会话消息负责注册项及投递正文。

</details>

-----

<a id="model-experience"></a>
## 模型体验

### 模型专属最终经验

#### 模型看到什么

只有当前已确认模型的 Skill 摘要可供模型调用。匹配服务生命周期的公开请求收到 `<bole-experience>` 中的完整正文。无关请求仅收到简短可用性提示，不收到正文。所有投递文本及收据都保存在会话日志中。

#### Token 影响

RSI-infer 增加一个 Skill 摘要和一次初始提示或指引。后续匹配请求可再投递一次正文；重复轮次不重复投递。标准模式不增加伯乐文本。

#### KV Cache 影响

经验作为持久用户上下文追加；标准系统提示和工具 schema 不变。模型专属目录及上下文可能改变请求前缀复用；不保证 provider 的缓存行为。

## 已知限制与延后工作

<a id="known-limitations-and-deferred-work"></a>

最终经验是冻结的指引，不是历史评测运行时的完整重放。

- 历史 Terminal-Bench 分数不能证明 Deep SWE 提升，也不能证明本仓库部署提分。
- provider/model ID 识别配置路由，不独立验证后端权重。RSI-infer 会话在首次请求后不能切换路由，即使压缩已移除收据也一样；应新建任务。继承其他路由收据的分叉历史会被拒绝。
- 插件采用保守的中英文公开任务匹配，不是语义分类器，可能漏匹配；所属模型的 Skill 仍可直接加载。
- 仅打包已确认的最终服务经验，不包含拒绝项、候选项、退役项、隐藏答案、凭据或题目专属补丁。
- 本仓库是 packages 子树。完整 Harness 宿主需在 `tsconfig.host.json` 添加包引用，并在 `tsconfig.base.json` 添加源码别名后执行正常构建；包自身 tsconfig 遵循现有三级宿主布局。

<a id="dev-note"></a>
### 开发备注

<details>
<summary>维护者工作上下文 — 点击展开</summary>

无。

</details>
