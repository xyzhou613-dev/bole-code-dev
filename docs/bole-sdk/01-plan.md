# 伯乐智能体 SDK：项目实施方案

用途：定义项目的形态、仓库结构、接口契约、分工与里程碑。所有实现决定以本文与《[决策记录](00-decisions.md)》为准。
基线：harness `0.1.7-alpha.2`，仓库 `bole-code-dev`，分支 `docs/bole-sdk`，Windows x64 交付。

## 一、一句话

用两个自研件——一个 C 动态库、一组运行期扩展包（自建插件 + 补丁层，官方源码不改）——把现成的 harness 运行时包成 Windows x64 的 C 接口 SDK，配一个离线包、两个示例、四份中文文档。

## 二、交付物与验收的对应关系

| 交付物 | 内容 | 对应验收条款 |
| --- | --- | --- |
| SDK 库 | 动态库、头文件、导入库、依赖清单 | 验收第 4 条：按文档独立集成可复现示例 |
| 离线部署包 | 运行时、配置、工具、示例、安装脚本 | 验收第 1 条：无外网、无开发环境的目标机跑通 |
| 示例程序 | 控制台示例、界面示例 | 验收第 1、2 条：全流程 1 分钟内、界面可交互且取消生效 |
| 文档 | 接口说明、集成指南、离线部署、错误码 | 验收第 4 条 |
| 事件流 | 阶段化事件与 RSI 状态 | 验收第 3、5 条：RSI 可观测、事件与真实阶段一致 |

## 三、系统架构

```
IDE / 示例程序
   │  C ABI（控制面立即返回）
   ▼
bole_agent.dll ─── 匿名管道 · 逐行 JSON-RPC ──► 运行时进程
   ▲                                              ├─ harness 内核（会话 / 工具 / 模型）
   │  事件回调（SDK 自有线程）                      ├─ 自建插件 @bole/sdk-server（cancel / rsi / delta / trace）
   └──────────────────────────────────────────────┤─ 经验记忆插件（RSI 开关控制）
                                                  └─ MCP 客户端 ──► 工具子进程
                                                         └─────────► 模型服务（OpenAI Chat Completions）
```

### 运行时由两段组成

| 段 | 是什么 | 来源 |
| --- | --- | --- |
| 壳 | `@deepseek-ai/dsh`：22 KB 的启动器，只有 bin + loader + profile 启动 | npm，版本 0.1.7-alpha.2 |
| 本体 | 数百个 `@deepseek-ai/dsh-*` 包，含内核与全部能力 | npm 同一版本，原样使用；扩展以自建插件与补丁层叠加 |

`bole-code-dev` 存的是本体的源码，不是"跑在别人基座上的插件"。因此交付时用 npm 的壳与包，官方包一个不改，只把自建的插件包与补丁层加进去，不需要构建整仓。

五条设计约束：

1. 动态库只做协议客户端，不在进程内重建 harness，符合任务书「薄封装」。
2. 运行时进程独立，工具崩溃或会话异常不影响宿主 IDE。
3. 所有模型与工具语义由 harness 产生，SDK 只做投影，不伪造状态（验收第 5 条）。
4. 运行时版本锁定 `0.1.7-alpha.2`，与经验记忆插件的引擎声明一并核对。
5. 官方包不动：协议扩展只通过补丁层与自建插件落地，跟随运行时升级不需要重新打补丁。

## 四、仓库结构

```
bole-code-dev/           临时承载交付项目，C 侧与文档交付前迁出（见根 README「非包组目录」）
├── <既有包组：acp/ api/ core/ llm/ mcp/ sdk/ skill/ ...>   harness 源码，交付时原样使用
├── sdk/
│   └── bole-server/  新增：JSON-RPC 插件，复用官方 server 类并加 cancel / rsi / delta / trace
├── bundle/
│   └── bole-sdk-app/ 新增：profile 补丁层，禁用官方 sdk-jsonrpc-server、插入 bole-server
├── native/           新增：C SDK，不属于 pnpm workspace
│   ├── include/bole_agent.h
│   ├── src/
│   ├── tests/
│   └── CMakeLists.txt
├── examples/
│   ├── console-c/    控制台示例
│   ├── gui-electron/ 界面示例
│   └── toy-mcp/      验收用玩具 MCP server
├── runtime/          运行时组装：插件包安装、profile 组合与配置预置
├── packaging/        离线包组装与干净机自检
└── docs/
    ├── bole-sdk/     内部：决策、方案、协议、事件、ABI、测试
    └── delivery/     对外：接口说明、集成指南、离线部署、错误码
```

依赖方向单向：`examples → native`、`packaging → runtime → 自建插件包`。`native` 不依赖任何 TypeScript 源码，只依赖协议。

## 五、三个自研件

### 1. C 动态库 `native/`

| 模块 | 职责 |
| --- | --- |
| 进程管理 | 启动运行时、传递 DSH_HOME、Job Object 绑定（父进程退出即回收）、崩溃检测 |
| 传输 | 匿名管道读写、逐行分帧、UTF-8、超长行保护 |
| 协议 | JSON-RPC 编解码、请求超时、请求与响应对应 |
| 事件 | 通知解析、阶段映射、增量合并、有界队列 |
| 会话 | 句柄表、引用计数、异步释放 |
| 错误 | 错误码映射、最近错误串（线程局部） |
| 日志 | 可选文件日志，用于现场排障 |

### 2. 协议扩展：`@bole/sdk-server` + `@bole/sdk-app`

在官方三项协议（initialize / session/prompt / shutdown）之上扩展：

| 扩展项 | 类型 | 实现要点 |
| --- | --- | --- |
| `session/cancel` | 请求 | `ctx.agents.get(sessionId)` 取 agent（agent id 与 session id 同值），调 `Agent.cancel({ kind: 'user' }, { keepInbox: false })`；返回立即，事件流随后给出结束 |
| `rsi.set` / `rsi.get` | 请求 | 转发到运行时提供的开关入口；未提供入口时返回 `RSI_UNAVAILABLE`；SDK 不挂载记忆插件 |
| `trace.export` | 请求 | 按会话导出事件日志到文件，供问题复现 |
| `assistant.delta` | 通知 | 订阅 `agent/assistant-stream` 的 `text-delta` 帧，按 50 毫秒或 4 KB 合并 |
| `rsi.state` | 通知 | 投影运行时的开关状态与来源（配置文件 / 接口）；无来源时只在启动按配置报一次 |

两个包分工：

| 包 | 类型 | 职责 |
| --- | --- | --- |
| `@bole/sdk-server` | Cordis 插件 | 创建 `JsonRpcLineTransport` 与 `HarnessSdkJsonRpcServer`（均为官方公开导出），在官方方法之上分发新增方法；订阅会话事件与流帧，发送 `assistant.delta`、`rsi.state`；转发 `rsi.set`，不挂载产品插件 |
| `@bole/sdk-app` | profile bundle | `cordis.patch.yml`：`disabled` 官方 `sdk-jsonrpc-server`，`insert` 自己的插件；可附带 provider 预置配置；不承担产品功能组合 |

扩展只做投影与转发，不实现模型、工具、会话语义，也不组合产品功能：记忆插件等产品插件由 profile 挂载，桥接插件只转发开关并投影状态。官方 transport 的 `onRequest` 只有一个注册位，但补丁层替换的是整个条目而不是追加 handler，所以不需要改 `sdk/server` 与 `sdk/protocol`；这一点已在 `0.1.7-alpha.2` 沙盒上实测。

### 3. 玩具 MCP server `examples/toy-mcp/`

一个 stdio MCP server，提供两三个无副作用工具（如加法、取当前时间、回显）。要求：启动小于 500 毫秒、不依赖外网、可被杀死以验证隔离性。

## 六、接口设计

### 控制面

| 函数 | 语义 |
| --- | --- |
| `bole_init(cfg, &handle)` | 启动运行时；立即返回，就绪与否由状态事件告知 |
| `bole_release(handle)` | 引用计数减一；后台完成进程回收，不阻塞 |
| `bole_session_create(handle, id, &session)` | 创建或复用会话 |
| `bole_session_resume(handle, id, &session)` | 恢复已持久化会话 |
| `bole_submit(session, utf8_text)` | 入队一条用户消息；立即返回 |
| `bole_cancel(session)` | 取消当前活动；立即返回 |
| `bole_rsi_set(handle, enabled)` | 切换 RSI；立即返回 |
| `bole_trace_export(handle, session_id, path)` | 导出轨迹 |
| `bole_last_error(handle, buf, len)` | 取最近错误文案 |

### 事件模型

| 事件 | 真实来源 | 说明 |
| --- | --- | --- |
| `TASK_STARTED` | `turn/start` | 轮次开始 |
| `PLAN_UPDATED` | `plan/mode`、`todo/write` | 计划变化 |
| `MODEL_SELECTED` | `model/selection`、`request/header` | 模型或路由变化 |
| `TEXT_DELTA` | 瞬时帧 `agent/assistant-stream` 的 `text-delta` | 可合并、可丢弃旧值 |
| `TOOL_CALL_STARTED` | `tool/call` | 含调用 id、工具名、参数 |
| `TOOL_CALL_FINISHED` | `tool/result` | 成功或失败、结果摘要 |
| `VERIFY_CHECK` | 经验记忆完成检查注入的消息 | 仅 RSI 开启时出现；若完成检查只在线下，本运行时不出现 |
| `RSI_STATE` | 协议扩展 | 开关状态与来源 |
| `TASK_FINISHED` | `turn/end` | 带结束原因 |
| `TASK_FAILED` | `turn/end`、运行时错误 | 带错误码 |

### 线程模型四条铁律

数据面形态：**事件回调**，不用队列拉取；回调由 SDK 自有线程触发，调用方自行切回 UI 线程。

1. 控制面函数可在任意线程调用，一律立即返回，内部只做入队。
2. 回调只在 SDK 自有的事件线程触发，同一句柄串行、有序。
3. 回调中允许调用 `submit`、`cancel`、`rsi_set`，不允许调用 `release`。
4. 回调不得阻塞超过毫秒级，否则增量事件会积压；积压时丢弃旧的 `TEXT_DELTA`，保留结构事件，结构事件溢出按故障处理。

### 错误码分层

| 区间 | 类别 | 例子 |
| --- | --- | --- |
| 1xxx | 初始化与配置 | 运行时路径不存在、配置非法 |
| 2xxx | 传输与协议 | 管道失败、解析失败、请求超时 |
| 3xxx | 运行时 | 进程退出、版本不匹配 |
| 4xxx | 会话 | 会话不存在、重复创建 |
| 5xxx | 模型 | 鉴权失败、限流、上下文超限 |
| 6xxx | 工具 | 工具不存在、超时、崩溃 |
| 7xxx | 取消 | 无活动可取消 |

## 七、运行时组合与配置

- 壳与全部官方包从 npm 安装，版本锁 `0.1.7-alpha.2`，一个不改。
- 自建插件 `@bole/sdk-server` 与补丁层 `@bole/sdk-app` 随离线包安装：profile 的 `cordis.patch.yml` 或 `--patch` 引入补丁，插件按包名解析。两包的依赖写死 `0.1.7-alpha.2`，不用 `workspace:*`，否则装不进 npm 运行时树。
- 一个运行时进程可承载多个会话，SDK 句柄与运行时进程一一对应。
- 启动参数 `--profile sdk --patch <离线包内补丁路径>`，`DSH_HOME` 指向离线包内独立目录，不读用户家目录。
- 模型配置预置 provider `bole`、显示名 `Bole Model`、地址 `http://192.168.19.203:9001/v1`、协议 OpenAI Chat Completions，现场可改。
- MCP 工具两份来源：随包玩具 server（验收用）；调用方在配置文件声明的 MCP server（stdio 或 streamable-http）。v1 只支持配置文件声明、重启生效，运行时注册接口放二期（建议，待确认）。
- RSI 默认关闭；记忆插件由产品 profile 挂载，SDK 不挂载。配置文件的开关在启动时生效；接口切换依赖运行时提供的开关入口（形式待定），切换在下一个 turn 生效。
- 运行时未提供开关入口时，`rsi.set` 返回 `RSI_UNAVAILABLE`（3xxx），`rsi.state` 只在启动按配置报一次，运行照常。

## 八、离线部署包

```
bole-agent-sdk-<版本>-win-x64/
├── bin/        bole_agent.dll、bole_agent.lib、bole_agent.h
├── runtime/    node.exe、dsh 依赖树、自建插件包、记忆插件、profile 与补丁、独立 DSH_HOME
├── tools/      玩具 MCP server
├── samples/    控制台示例、界面示例、源码
├── docs/       四份交付文档
├── VERSION.json 组件版本清单（对应「依赖清单」）
├── SHA256SUMS   校验清单
└── install.ps1  校验与安装
```

硬性要求：解压即用、不写注册表、不需要管理员权限、目标机无 Node 与 VS 运行库、运行期无外网访问。

## 九、测试与验收

| 层级 | 内容 |
| --- | --- |
| 单元 | 分帧、编解码、事件映射、错误码 |
| 契约 | 用假运行时回放固定报文，不依赖真进程 |
| 补丁层 | 禁用官方 `sdk-jsonrpc-server` 后，自建插件接管 stdout 并答复自定义方法 |
| RSI 转发 | 有入口：转发并回状态；无入口：返回 `RSI_UNAVAILABLE` 且状态不变 |
| 集成 | 真运行时加玩具工具，Windows 上跑通全流程 |
| 稳定性 | 杀工具进程、杀运行时、断网、取消中途、连续多轮 |
| 验收演练 | 干净 Windows 虚拟机断网，按四条验收标准逐条过 |

## 十、里程碑

| 阶段 | 时长 | 完成标志 |
| --- | --- | --- |
| M0 契约与打样 | 第 1 周 | 三份契约冻结；控制台跑通一次工具调用并掐表 |
| M1 功能完整 | 第 2-3 周 | SDK 全接口、协议扩展全功能、取消与 RSI 可观测 |
| M2 示例与打包 | 第 4-5 周 | 两个示例、四份文档、离线包可解压运行 |
| M3 演练 | 第 6 周 | 干净机断网跑通四条验收 |
| M4 联调 | 缓冲 | 与伙伴联调，修现场问题 |

## 十一、分工

| 角色 | 负责 |
| --- | --- |
| A 协议与运行期 | `sdk/bole-server` 与 `bundle/bole-sdk-app` 两个包、运行时组装、玩具 MCP server |
| B 接口与交付面 | `native/` 的 C 动态库、两个示例、四份交付文档、离线包装配 |
| 共同 | 三份契约、验收演练脚本、现场联调 |

## 十二、风险与前置条件

| 风险 | 现状 | 应对 |
| --- | --- | --- |
| 仓库无法整仓构建 | 缺根 workspace、vendor、tsconfig base；展平后包的 tsconfig 还多一级 `../` | 走 npm 运行时；自建包写独立 tsconfig 与写死版本的依赖，不依赖整仓构建 |
| 无 MSVC | 未安装；技术上不是硬约束 | 正式件用 Build Tools 2022（`/MT`、PDB），WSL 用 mingw-w64 迭代；koffi 在 Windows 上装机时要联网取预编译或现场编译 |
| npm 11 默认拦截安装脚本 | 已实测告警 | Windows 打包机先 `npm approve-scripts`，否则运行时缺原生模块 |
| 官方单文件运行时不可用 | PyPI 无 0.1.7 的 win-x64 wheel | 离线包自带 `node.exe` 与依赖树 |
| Windows 无 pnpm | 已确认未安装 | `corepack enable pnpm` |
| RSI 开关入口未知 | 形式与归属未确认（服务 / 事件 / settings） | 与负责人确认；SDK 不挂载产品插件，最坏只保留状态观测并重议验收第 3 条 |
| 模型 ID 未定 | 待确认 | 现场可用「获取可用模型」拉取，不阻塞 |
| 目标机安全策略 | 未确认 | M3 前备好未签名说明与哈希清单 |

## 十三、开工前的准备

1. 三份契约：协议方法表、事件模型、C 头文件。
2. 运行时沙盒：已跑通（`0.1.7-alpha.2`，见《决策记录》第八节）；把它固化成可复用的验收沙盒。
3. 玩具 MCP server：不依赖私有包，可直接完成。
4. 环境：Windows 装 Build Tools 与 pnpm；WSL 装 mingw-w64 供日常迭代。
5. 目录骨架：`sdk/bole-server/`、`bundle/bole-sdk-app/`、`native/`、`examples/`、`runtime/`、`packaging/`、`docs/delivery/`。
