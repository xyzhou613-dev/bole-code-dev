# 伯乐智能体 SDK：项目实施方案

用途：定义项目的形态、仓库结构、接口契约、分工与里程碑。所有实现决定以本文与《[决策记录](00-decisions.md)》为准。
基线：harness `0.1.7-alpha.2`，仓库 `bole-code-dev`，分支 `docs/bole-sdk`，Windows x64 交付。2026-10-22 修订：RSI 改为 agent preset 切换（2.2），运行组合改为 preset 面 + 关外网面，事件模型补三层与 thinking 通道。

## 一、一句话

用两个核心自研件——一个 C 动态库、一组运行期扩展（自建插件 `@bole/sdk-server` + 补丁层 `@bole/sdk-app`，官方源码不改）——把现成的 harness 运行时包成 Windows x64 的 C 接口 SDK，配一个离线包、两个示例、四份中文文档。RSI 不是服务开关，而是 `standard` / `rsi-infer` 两个 agent preset 的选择，由补丁层组合、由 SDK 选择与投影。

## 二、交付物与验收的对应关系

| 交付物 | 内容 | 对应验收条款 |
| --- | --- | --- |
| SDK 库 | 动态库、头文件、导入库、依赖清单 | 验收第 4 条：按文档独立集成可复现示例 |
| 离线部署包 | 运行时、preset 面、经验包、配置、工具、示例、安装脚本 | 验收第 1 条：无外网、无开发环境的目标机跑通 |
| 示例程序 | 控制台示例、界面示例（五项：输入 / 提交 / 实时状态区 / 结果区 / 取消） | 验收第 1、2 条：全流程 1 分钟内、界面可交互且取消生效 |
| 文档 | 接口说明（含线程模型）、集成指南、离线部署、错误码 | 验收第 4 条 |
| 事件流 | 会话 / 任务 / 步骤三层、thinking 增量、RSI 状态与投递收据 | 验收第 3、5 条：RSI 可观测、事件与真实阶段一致 |

## 三、系统架构

```
IDE / 示例程序
   │  C ABI（控制面立即返回）
   ▼
bole_agent.dll ─── 匿名管道 · 逐行 JSON-RPC ──► 运行时进程（--profile sdk + 补丁层）
   ▲                                              ├─ harness 内核（会话 / 任务 / 步骤 / 工具 / 模型）
   │  事件回调（SDK 自有线程）                      ├─ 自建插件 @bole/sdk-server
   └──────────────────────────────────────────────┤    （create / resume / cancel / trace / rsi / delta）
                                                  ├─ preset 面：standard / rsi-infer
                                                  │    （agent-preset-registry + 两条 preset 声明）
                                                  │    └─ rsi-infer 挂只读经验插件
                                                  └─ MCP 客户端 ──► 工具子进程
                                                         └─────────► 模型服务（OpenAI Chat Completions）
```

### 运行时由两段组成

| 段 | 是什么 | 来源 |
| --- | --- | --- |
| 壳 | `@deepseek-ai/dsh`：启动器，只有 bin + loader + profile 启动 | npm，版本 0.1.7-alpha.2 |
| 本体 | 数百个 `@deepseek-ai/dsh-*` 包，含内核与全部能力 | npm 同一版本，原样使用；扩展以自建插件与补丁层叠加 |

`bole-code-dev` 存的是本体的源码，不是「跑在别人基座上的插件」。因此交付时用 npm 的壳与包，官方包一个不改，只把自建的插件包、补丁层、preset 声明与经验包加进去，不需要构建整仓。

六条设计约束：

1. 动态库只做协议客户端，不在进程内重建 harness，符合任务书「薄封装」。
2. 运行时进程独立，工具崩溃或会话异常不影响宿主 IDE。
3. 所有模型、工具、模式语义由 harness 产生，SDK 只做投影，不伪造状态（验收第 5 条）。
4. 运行时版本锁定 `0.1.7-alpha.2`，preset 面与经验包版本一并锁定并写进 `VERSION.json`。
5. 官方包不动：协议扩展只通过补丁层与自建插件落地。
6. **preset 面与 Web 同源**：base 的 agent 面（工具、提示词段、子代理）在 Web 上已被整体挪进 preset；SDK 要用 RSI 就必须采用同一套 preset 面，升级时跟随上游 `bundle/web-app/cordis.patch.yml` 的对应段落。

### 为什么 RSI 必须走 preset 面

RSI 的产品实现是「第五种模式」：`rsi-infer` = `standard` + `@deepseek-ai/dsh-skill-bole-experience`。模式由 agent preset 组成，preset 在 agent 创建时挂载、在会话首个 turn 后锁定。若沿用 base 的单会话全局工具面，经验插件只能全局挂载，无法做到「关就切到标准模式」。因此 SDK 的补丁层必须像 Web 一样：禁用 base 的 agent 面行，插入 `agent-preset-registry` 与两条 preset（见第五节第 2 项）。

## 四、仓库结构

```
bole-code-dev/           临时承载交付项目，C 侧与文档交付前迁出
├── <既有包组：acp/ api/ core/ llm/ mcp/ sdk/ skill/ ...>   harness 源码，交付时原样使用
├── sdk/
│   └── bole-server/  新增：JSON-RPC 插件，复用官方 server 类并加 create / resume / cancel / trace / rsi / delta
├── bundle/
│   └── bole-sdk-app/ 新增：profile 补丁层（替换官方服务器、组合 preset 面、关外网面、固定审批策略）
├── native/           新增：C SDK，不属于 pnpm workspace
│   ├── include/bole_agent.h
│   ├── src/
│   ├── tests/
│   └── CMakeLists.txt
├── examples/
│   ├── console-c/    控制台示例
│   ├── gui-electron/ 界面示例
│   └── toy-mcp/      验收用玩具 MCP server
├── runtime/          运行时组装：插件包安装、preset 声明、profile 组合与配置预置
├── packaging/        离线包组装与干净机自检
└── docs/
    ├── bole-sdk/     内部：决策、方案、协议、事件、ABI、测试
    └── delivery/     对外：接口说明、集成指南、离线部署、错误码
```

依赖方向单向：`examples → native`、`packaging → runtime → 自建插件包`。`native` 不依赖任何 TypeScript 源码，只依赖协议。

新增的两个 TypeScript 包放在 `sdk/` 与 `bundle/` 组下，需要在交付前同步这两个组的 README 包表与命名说明；若最终决定不把它们并进包仓库（与 `native/` 一样放在交付侧目录），则不受包组约定约束，`01-plan.md` 的目录图按实际调整。

## 五、三个自研件

### 1. C 动态库 `native/`

| 模块 | 职责 |
| --- | --- |
| 进程管理 | 启动运行时、传递 `DSH_HOME`、Job Object 绑定（父进程退出即回收）、崩溃检测 |
| 传输 | 匿名管道读写、逐行分帧、UTF-8、超长行保护、stdout 误写检测 |
| 协议 | JSON-RPC 编解码、请求超时、请求与响应对应、请求序号 |
| 事件 | 通知解析、阶段映射、增量合并、有界队列 |
| 会话 | 句柄表、引用计数、异步释放、preset 选择 |
| 错误 | 错误码映射、句柄级最近错误串 |
| 日志 | 可选文件日志，用于现场排障 |

### 2. 协议扩展：`@bole/sdk-server` + `@bole/sdk-app`

在官方三项协议（initialize / session/prompt / shutdown）之上扩展：

| 扩展项 | 类型 | 实现要点 |
| --- | --- | --- |
| `session/create` | 请求 | `agents.create` + 发布前 `agentPresets.mount`；不触模型、不触工具；preset 缺省取当前默认 |
| `session/resume` | 请求 | `ctx.agents.resume`；要求持久化与查询服务；preset 按投影恢复，定义缺失拒绝 |
| `session/cancel` | 请求 | `ctx.agents.get(sessionId)` 取 agent（agent id 与 session id 同值），调 `Agent.cancel({ kind: 'user' }, { keepInbox: false })`；立即返回，结束事实由 `turn/end` 给出 |
| `session/trace` | 请求 | 按 `turn` 过滤会话日志并写文件；落点内存 `eventAt` 或持久化 JSONL |
| `session/setPreset` | 请求 | 仅空白会话可切（`ctx.agentPresets.select`），否则 `agent-preset/locked` |
| `rsi.set` / `rsi.get` | 请求 | 默认 preset 的读写；等价于在 standard / rsi-infer 之间切换；不引入第二套开关语义 |
| `assistant.delta` | 通知 | 订阅 `agent/assistant-stream` 的 `text-delta` 与 `reasoning-delta`，按「50 毫秒或 4 KB」合并，`channel` 区分正文/思考 |
| `rsi.state` | 通知 | 投影会话头 / `agent-preset/selected` / 经验收据，给出 preset、来源与投递状态 |

两个包分工：

| 包 | 类型 | 职责 |
| --- | --- | --- |
| `@bole/sdk-server` | Cordis 插件 | 创建 `JsonRpcLineTransport` 与 `HarnessSdkJsonRpcServer`（均为官方公开导出），保留官方 `inject: [sdkAppStartup, loader]` 与 `initialize` 的 `loader.await()` 就绪边界；在官方方法之上分发新增方法；订阅会话事件与流帧，发送 `assistant.delta`、`rsi.state`；转发 preset 选择，不 import 经验插件 |
| `@bole/sdk-app` | profile bundle | `cordis.patch.yml`：`disabled` 官方 `sdk-jsonrpc-server`、`insert` 自己的插件；禁用 base 的 agent 面行并插入 `agent-preset-registry` 与 `standard` / `rsi-infer` 声明；关闭外网面；固定审批策略；可附带 provider 预置配置 |

preset 声明的正文直接对齐 `bundle/web-app/presets/standard.patch.yml` 与 `rsi-infer.patch.yml`（后者只多一行经验插件），只取这两份。若产品侧同意，也可以让离线包直接复用 `@deepseek-ai/dsh-web-app` 包内的这两份 patch（该包 `files` 已包含它们），少维护一份；代价是多一个与 Web 强绑定的依赖，需要评估。这是实现选型，不改变契约。

扩展只做投影与转发，不实现模型、工具、会话语义，也不组合产品功能：经验插件由 `rsi-infer` 的 preset 行挂载，桥接插件只选择 preset 并投影状态。

### 3. 玩具 MCP server `examples/toy-mcp/`

一个 stdio MCP server，提供两三个无副作用工具（如加法、取当前时间、回显）。要求：启动小于 500 毫秒、不依赖外网、可被杀死以验证隔离性。

## 六、接口设计

### 控制面

| 函数 | 语义 |
| --- | --- |
| `bole_init(cfg, &handle)` | 校验配置、启动运行时、立即返回；事件回调与 `user_data` 在 `cfg` 中给出，避免注册前的丢事件 |
| `bole_wait_ready(handle, timeout_ms)` | 只等握手完成，不等模型或工具；超时返回 2xxx |
| `bole_release(handle)` | 释放调用方持有的一次引用；内部引用归零后后台回收进程，不阻塞 |
| `bole_session_create(handle, id, &session)` | 按当前默认 preset 创建会话（含 RSI 默认）；立即返回；id 已存在返回 4xxx，恢复走 resume |
| `bole_session_set_preset(session, preset_id)` | 空白会话覆盖 preset（`standard` / `rsi-infer`）；首个 turn 开始后返回 4xxx locked |
| `bole_session_resume(handle, id, &session)` | 恢复已持久化会话；preset 按日志恢复 |
| `bole_submit(session, utf8_text)` | 入队一条用户消息；立即返回 |
| `bole_cancel(session)` | 立即中断当前任务；无活动返回 7xxx |
| `bole_rsi_set(handle, enabled)` | 设置后续新任务的默认 RSI（preset）；立即返回，运行中的任务不变 |
| `bole_rsi_get(handle, &state)` | 读默认开关、运行时是否组合了 preset 面 |
| `bole_trace_export(handle, session_id, turn, path)` | 导出轨迹；`turn = 0` 表示整个会话 |
| `bole_set_event_callback(handle, cb, user_data)` | 替换回调；与 `cfg` 中的初始回调同义 |
| `bole_last_error(handle, buf, len)` | 取本句柄最近一次错误文案（句柄级，不是线程局部） |

### 事件模型

| 事件 | 真实来源 | 说明 |
| --- | --- | --- |
| `RUNTIME_READY` | 握手完成 | 含 serverInfo、运行时版本、模型路由 |
| `RUNTIME_FAILED` | 握手失败 / 进程启动失败 | 含错误码与文案 |
| `RUNTIME_EXITED` | 进程退出 | 含退出码；意外退出按 3xxx 处理 |
| `TASK_STARTED` | `turn/start` | 任务（turn）开始 |
| `STEP_STARTED` / `STEP_FINISHED` | `step/start` / `step/end` | 步骤层，载荷 `{ turn, step }` |
| `PLAN_UPDATED` | `plan/mode`、`todo/write` | `kind = plan_mode` 时载荷是 `{ active }`；`kind = todos` 时是待办列表 |
| `MODEL_SELECTED` | `request/header` | provider、模型、推理强度、header 变化原因 |
| `RSI_STATE` | 会话头 / `agent-preset/selected` / 经验收据 | preset、来源、投递状态与 `deliveredMemoryIds` |
| `TEXT_DELTA` | 瞬时帧 `text-delta` | 可合并、可丢弃旧值；最终以 `assistant/message` 为准 |
| `REASONING_DELTA` | 瞬时帧 `reasoning-delta` | 思考增量，合并策略同正文 |
| `TOOL_CALL_STARTED` | `tool/call` | 调用 id、工具名、原始参数 JSON |
| `TOOL_CALL_FINISHED` | `tool/result` | 调用 id、成功与否、错误码与结果摘要 |
| `VERIFY_CHECK` | **暂缓**：本基线没有可投影来源 | 保留定义，本运行时不产生、不承诺送达 |
| `TASK_FINISHED` | `turn/end` | `outcome`：completed / aborted / max_tokens / blocked / interrupted（`forked` 只出现在 fork 种子，不是实时终态） |
| `TASK_FAILED` | `turn/end` 的 `error`、运行时错误 | 错误码、消息、是否可重试 |

### 线程模型四条铁律

数据面形态：**事件回调**，不用队列拉取；回调由 SDK 自有线程触发，调用方自行切回 UI 线程。

1. 控制面函数可在任意线程调用；除显式等待的 `bole_wait_ready` 外一律立即返回，内部只做入队。
2. 回调只在 SDK 自有的事件线程触发，同一句柄串行、有序。
3. 回调中允许调用 `submit`、`cancel`、`rsi_set`、`session_set_preset`，不允许调用 `release`。
4. 回调不得阻塞超过毫秒级，否则增量事件会积压；积压时丢弃最旧的 `TEXT_DELTA` / `REASONING_DELTA`，保留结构事件，结构事件溢出按故障处理（2xxx 并断开）。

### 错误码分层

| 区间 | 类别 | 例子 |
| --- | --- | --- |
| 1xxx | 初始化与配置 | 运行时路径不存在、配置非法、preset 未定义 |
| 2xxx | 传输与协议 | 管道失败、解析失败、请求超时 |
| 3xxx | 运行时 | 进程退出、版本不匹配、`RSI_UNAVAILABLE`（未组合 preset 面） |
| 4xxx | 会话 | 会话不存在、重复创建、preset 已锁定 |
| 5xxx | 模型 | 鉴权失败、限流、上下文超限 |
| 6xxx | 工具 | 工具不存在、超时、崩溃 |
| 7xxx | 取消 | 无活动可取消 |

## 七、运行时组合与配置

- 壳与全部官方包从 npm 安装，版本锁 `0.1.7-alpha.2`，一个不改。
- 自建插件 `@bole/sdk-server` 与补丁层 `@bole/sdk-app` 随离线包安装：profile 的 `cordis.patch.yml` 或 `--patch` 引入补丁，插件按包名解析。两包的依赖写死 `0.1.7-alpha.2`，不用 `workspace:*`，否则装不进 npm 运行时树。
- **preset 面**（RSI 的载体）：禁用 base 的 agent 面行（`tool-bash`、`tool-pwsh`、`tool-fs`、`tool-fs-search`、`tool-jobs`、`skill-filesystem`、`tool-skill`、`command-goal`、`tool-goal`、`plan-mode`、`compaction-basic`、`command-compact`、`tool-result-pruner`、`tool-subagent*`、`workflow-ptc`、`tool-workflow`、`tool-ralph`、`agent-instructions`、`tool-todo`、`tool-web`、`tool-plugin-manager`），插入 `agent-preset-registry`（`default: standard`）与 `standard` / `rsi-infer` 两条 `agent-preset` 声明。**本期只随包这两条**（10-22 确认），`ptc` / `minimal` / `cordis` 不随包。禁止重复注册：这些行不能同时留在 base。部分行在 base 里本来就是 disabled，仍显式列出，避免上游重排后意外复活。
- **外网面**：关闭 `tool-web`、`web-search-deepseek`、`web-fetch-http`、`web` 与 `session-telemetry-otel`（或用 `DSH_TELEMETRY_DISABLED` 兜底）。任务书要求运行期无外网访问，这一条不能只写文档。
- **审批与沙箱**：`approval.policy: never`（headless 没有应答者，`ask` 会 fail-closed），沙箱模式按现场选 `workspace-write` 或 `danger-full-access`。审批只在 `tools/pre-execute` 返回 `ask` 时发生，SDK 组合不挂载会产生 `ask` 的组件（`hooks-claude-code`、`auto-review`），因此工具调用不会停住。
- **模型配置**：沿用 base 的 `bole` 路由（`Bole Model`、`http://192.168.19.203:9001/v1`、`openai-completions`、占位 key），但模型 ID 覆盖为 `qwen3.8-27b`（10-22 确认；base 的 `bole-model` 只是占位）。经验允许列表有两个模型：`qwen3.8-27b`（Qwen 服务经验 v3）与 `Ornith-1.5-35B-A3B`（Ornith V18），各自加载自己的包、按准确 ID 隔离；改出这两个 ID 会让 RSI 退化为 `unsupported-model`。
- **MCP 工具两份来源**：随包玩具 server（验收用）；调用方在配置文件声明的 MCP server（stdio 或 streamable-http，每条一个 `@deepseek-ai/dsh-mcp-client` 条目，可配 `toolCallTimeoutMs`、`failOnStartupError`）。v1 只支持配置文件声明、重启生效，运行时注册接口放二期（已确认，10-22）。
- 一个运行时进程可承载多个会话，SDK 句柄与运行时进程一一对应。
- 启动参数 `--profile sdk --patch <离线包内补丁路径>`，`DSH_HOME` 指向离线包内独立目录，不读用户家目录。
- RSI 默认关闭：registry `default: standard`；`rsi.set` 改的是后续新任务的默认，`session/setPreset` 只对空白会话生效。运行中的任务不能切换（内核锁定）。
- 运行时未组合 preset 面时，`rsi.set` 返回 `RSI_UNAVAILABLE`（3xxx），`rsi.state` 如实报「未组合」，运行照常。

## 八、离线部署包

```
bole-agent-sdk-<版本>-win-x64/
├── bin/        bole_agent.dll、bole_agent.lib、bole_agent.h
├── runtime/    node.exe、dsh 依赖树、@bole 插件包、preset 声明、经验包与 assets、profile 与补丁、独立 DSH_HOME
├── tools/      玩具 MCP server
├── samples/    控制台示例、界面示例、源码
├── docs/       四份交付文档
├── VERSION.json 组件版本清单（运行时、preset、经验包、node、工具）
├── SHA256SUMS   校验清单
└── install.ps1  校验与安装
```

硬性要求：解压即用、不写注册表、不需要管理员权限、目标机无 Node 与 VS 运行库（C 库用 `/MT` 静态运行库）、运行期无外网访问。

经验包发布必须包含 `assets/final-experiences.json`；缺资产时插件启动校验会失败，离线包要在自检里覆盖这一条。

## 九、测试与验收

| 层级 | 内容 |
| --- | --- |
| 单元 | 分帧、编解码、事件映射、错误码、合并与队列 |
| 契约 | 用假运行时回放固定报文，不依赖真进程；覆盖 create / resume / cancel / trace / rsi / delta |
| 补丁层 | 禁用官方 `sdk-jsonrpc-server` 后，自建插件接管 stdout 并答复自定义方法 |
| preset 面 | `standard` 与 `rsi-infer` 各建会话：`standard` 不出现经验收据；`rsi-infer` 在服务生命周期任务上为 `delivered`、普通任务为 `available`、非允许模型为 `unsupported-model`；`qwen3.8-27b` 与 `Ornith-1.5-35B-A3B` 各跑一次确认只加载对应包；空白期切换成功、首个 turn 后 `locked` |
| 取消 | 模型调用中、工具执行中、排队中三种时机各测一次；终态为 `aborted`，无残留事件 |
| 三层与 thinking | step 事件顺序、reasoning 增量与正文分流、最终正文以 `assistant/message` 为准 |
| 集成 | 真运行时加玩具工具，Windows 上跑通全流程 |
| 稳定性 | 杀工具进程、杀运行时、断网、连续多轮、长任务（界面示例） |
| 验收演练 | 干净 Windows 虚拟机断网，按四条验收标准逐条过 |

## 十、里程碑

| 阶段 | 时长 | 完成标志 |
| --- | --- | --- |
| M0 契约与打样 | 第 1 周 | 三份契约冻结；控制台跑通一次工具调用并掐表；preset 面最小可运行 |
| M1 功能完整 | 第 2-3 周 | SDK 全接口、协议扩展全功能、取消与 RSI 可观测、step 与 thinking 事件 |
| M2 示例与打包 | 第 4-5 周 | 两个示例、四份文档、离线包可解压运行 |
| M3 演练 | 第 6 周 | 干净机断网跑通四条验收 |
| M4 联调 | 缓冲 | 与伙伴联调，修现场问题 |

## 十一、分工

| 角色 | 负责 |
| --- | --- |
| A 协议与运行期 | `sdk/bole-server`、`bundle/bole-sdk-app`、preset 面、运行时组装、玩具 MCP server |
| B 接口与交付面 | `native/` 的 C 动态库、两个示例、四份交付文档、离线包装配 |
| 共同 | 三份契约、验收演练脚本、现场联调 |

## 十二、风险与前置条件

| 风险 | 现状 | 应对 |
| --- | --- | --- |
| preset 面跟随上游 | base 的 agent 面在 Web 已被挪进 preset；SDK 要 RSI 必须复制同一套 | 锁定 0.1.7-alpha.2，升级时对照 `bundle/web-app/cordis.patch.yml` 的 preset 段逐行核对；补一个「base 行与 preset 行不得重复」的自检 |
| 现场改模型导致 RSI 退化 | 允许列表是 `qwen3.8-27b` / `Ornith-1.5-35B-A3B`；改成其它 ID 即退回 `unsupported-model` | 交付说明写明允许列表；启动自检发现模型不在列表时给出警告 |
| 仓库无法整仓构建 | 缺根 workspace、vendor、tsconfig base；展平后包的 tsconfig 还多一级 `../` | 走 npm 运行时；自建包写独立 tsconfig 与写死版本的依赖，不依赖整仓构建 |
| 无 MSVC | 未安装；技术上不是硬约束 | 正式件用 Build Tools 2022（`/MT`、PDB），WSL 用 mingw-w64 迭代；koffi 在 Windows 上装机时要联网取预编译或现场编译 |
| npm 11 默认拦截安装脚本 | 已实测告警 | Windows 打包机先 `npm approve-scripts`，否则运行时缺原生模块 |
| 官方单文件运行时不可用 | PyPI 无 0.1.7 的 win-x64 wheel | 离线包自带 `node.exe` 与依赖树 |
| Windows 无 pnpm | 已确认未安装 | `corepack enable pnpm` |
| 验证阶段不可观测 | 本基线无信号，已暂缓 | 事件模型标暂缓；若产品侧提供信号再映射；不伪造 |
| 离线机外网请求 | base 挂 web 工具与 telemetry | 补丁层显式关闭，M3 前用断网机 + 进程网络审计验证 |
| 目标机安全策略 | 未确认 | M3 前备好未签名说明与哈希清单 |

## 十三、开工前的准备

1. 三份契约：协议方法表、事件模型、C 头文件。
2. 运行时沙盒：`0.1.7-alpha.2` 已跑通（《决策记录》第八节）；把它连同 preset 面一起固化成可复用的验收沙盒，并入库。
3. preset 面最小可运行：`standard` / `rsi-infer` 两条声明 + 经验包，先在没有模型的情况下验证会话创建、preset 锁定与收据事件。
4. 玩具 MCP server：不依赖私有包，可直接完成。
5. 环境：Windows 装 Build Tools 与 pnpm；WSL 装 mingw-w64 供日常迭代。
6. 目录骨架：`sdk/bole-server/`、`bundle/bole-sdk-app/`、`native/`、`examples/`、`runtime/`、`packaging/`、`docs/delivery/`。
