# 08 开发分工、接口与约束（2026-10-22）

状态：分工基线。按项目负责人 10-22 第三轮要求：**先不继续开发，把约束与接口落成文档，供 A/B 两人分工**。领取工作包前先读本文与 `00` / `01` / `02` / `03` / `04` / `06` / `07`。

本文是分工与边界的唯一入口；接口细节以 `02-protocol.md`、`03-events.md`、`04-c-abi.md` 与 `native/include/bole_agent.h` 为准，测试细节以 `05-testing.md` 为准，离线包细节以 `07-offline-package.md` 为准。

## 实施状态（2026-10-23）

A/B 两轨已完成并合入 `develop`：C SDK、控制台/GUI 示例、离线包脚本、acceptance 脚本与四份交付文档均落地；真实内网网关已跑通。本文保留 10-22 的分工/接口基线，当前验收与剩余测试见 [10-task-coverage.md](10-task-coverage.md)，构建步骤见 [11-packaging-runbook.md](11-packaging-runbook.md)。

## 一、目标与本轮边界

**v1 目标**：Windows x64 的 C 接口 SDK（DLL + 头文件 + 导入库）、随包运行时、控制台与界面示例、离线部署包、四份中文文档；在无外网、无开发环境的目标机上跑通「输入 → 模型 → 一次 MCP 工具调用 → 结果」并可在任务中取消；RSI 开关以 `standard` / `rsi-infer` 两个 preset 承载且状态可在事件流观测。

**不做**：IDE 侧绑定与插件壳（伙伴）、具体第三方工具适配（MATLAB 等）、硬件/一体机镜像、验证阶段（暂缓）、运行时 MCP 注册（二期）、32 位、Windows 10/11 以外系统。

**历史边界（10-22）**：不新增开发代码，尤其不代 B 轨写 C 实现、测试、示例或打包。该边界已在 10-23 解除：B 轨实现、示例、打包脚本与测试均已完成；`prototypes/native-c-sdk/` 是未授权参考，不属于交付物。

## 二、角色与所有权

| 角色 | 工作包 | 拥有目录/文件 | 不碰 |
| --- | --- | --- | --- |
| A 协议与运行期 | A1–A8 | `runtime/packages/sdk-server/`、`runtime/packages/sdk-app/`、`runtime/config/`、`runtime/preset/`、`examples/toy-mcp/` | `native/`、`examples/console-c/`、`examples/gui-win32/`、`packaging/`、`docs/delivery/` |
| B 接口与交付面 | B1–B8 | `native/`、`examples/console-c/`、`examples/gui-win32/`、`packaging/`、`docs/delivery/` | `runtime/packages/`、`examples/toy-mcp/` |
| 共同 | C1–C2、契约变更、验收演练 | `docs/bole-sdk/02-04`、`native/tests/fixtures/`、`docs/bole-sdk/05` | 对方拥有的实现目录（改动先协商） |

- 姓名待项目负责人填写；同一时间只允许一人改同一目录。
- A 的代码以 TypeScript 为主，B 以 C/C++ 与打包脚本为主；交叉评审：A 审 C 事件/协议语义，B 审插件 payload/错误码。
- 契约文档（02/03/04/header）变更必须 A+B 双方确认，不能单方面改。B 轨实现范围、取舍与代码由 B 负责人决定；A 只提供接口与约束，不替 B 选实现。

## 三、冻结接口与变更流程

已冻结 1.0：`02-protocol.md`、`03-events.md`、`04-c-abi.md`、`native/include/bole_agent.h`。冻结后**只增不改**：新增方法/通知/字段/事件类型可以，已发布的名字、编号、含义不改。

变更流程（缺一不可）：

1. 先在 `native/tests/fixtures/` 增加或修改可复现的 wire fixture（真实运行捕获，不手写理想序列）。
2. 修改 `02`/`03`/`04` 对应段落与 `bole_agent.h`（新增字段追加在末尾；新增事件用未占用类型值）。
3. A/B 双方评审签字，记入 `00-decisions.md` 确认记录；协议版本 `sdkProtocolVersion` 或 ABI 版本按需递增。
4. 两侧测试同时更新：C 的契约测试与插件的投影测试必须用同一 fixture。

不许做的事情：

- 不改官方 `@deepseek-ai/*` 源码；扩展只走 `runtime/packages/sdk-server` + `sdk-app` 补丁层。
- 不把 `@bole/*` 包并进 `sdk/` 或 `bundle/` 包组；不放根 workspace、不引 `workspace:*` 依赖。
- 不让 C 库解析 harness 事件词汇；事件投影只在插件，C 只解析 `bole.event` 信封。
- 不在运行时里伪造阶段、补造终态或伪造 RSI 状态。
- 不在代码里写死现场路径、密钥、模型地址以外的环境事实；模型地址只出现在配置模板。

## 四、工作包

工作量为粗估（人日，不含现场联调与等待时间）。A1 的「打样」是 A 的 Stage 0 参考；B1/B2 **没有实现**，`prototypes/native-c-sdk/` 只是未授权参考，由 B 决定是否采用。

### A 轨：协议与运行期

| ID | 工作包 | 产出 | 依赖 | DoD | 粗估 |
| --- | --- | --- | --- | --- | --- |
| A1 | 插件骨架与传输接管 | `runtime/packages/sdk-server` 可构建的包；禁用官方 server 后接管 stdout；`initialize` 就绪边界；`shutdown` 退出 | 02、04 | 真实 `--profile sdk` 下 initialize/shutdown 通过；stderr 无协议污染；有注入流的单测 | 1d（打样） |
| A2 | 会话方法 | `session/create`（cwd/preset/重复）、`resume`、`cancel`、`setPreset` | A1、02 | 四个方法在真实运行时通过；错误码与 02/04 一致；取消终态 `aborted` | 2d |
| A3 | 事件投影与增量 | `bole.event` 结构事件、`assistant.delta` 合并、`ASSISTANT_MESSAGE`、顺序/截断/背压 | A1、03 | 15 类事件按 03 投影；fixtures 回放逐条一致；增量可丢、终态不丢 | 3d |
| A4 | RSI 与 preset 面 | 禁用 base agent 面、插入 registry + `standard`/`rsi-infer`；经验包与 assets；`rsi.set/get/state`；收据投影 | A2、A6 | 两个 preset 各建会话；`standard` 无收据；`rsi-infer` 的 delivered/available/unsupported-model 可观测；空白期切换、首 turn 后 locked；preset 行写入 `sdk-app` 补丁并验证 | 3.5d |
| A5 | 轨迹与持久化 | `session/trace` 按 turn 导出原始 JSONL；resume 前置校验 | A2、02 | turn=0 全会话、指定 turn 过滤；路径/父目录错误可读；resume 无历史重放 | 1.5d |
| A6 | 交付补丁层（非 preset 部分） | `sdk-app/cordis.patch.yml` 基础版：模型、approval、关外网、telemetry；启动自检能力位 | A1、02/07 | `--dump-config` 快照稳定；无重复注册；`approval never`；外网行全关；`RUNTIME_READY.capabilities` 真实 | 1d |
| A7 | 运行时组装与玩具 MCP | `runtime/` 目录、配置模板、`examples/toy-mcp`、启动/自检脚本 | A6/A4、07；玩具 MCP 可先做 | 玩具 MCP 启动 <500ms、两三个无副作用工具、可被杀；配置模板可声明 MCP server | 1d |
| A8 | 插件打包与版本 | `npm pack`/`lib/index.js`、`@bole/sdk-server` 在运行时树可解析、`VERSION.json` 输入 | A6 | 产物可被 B6 安装；裸名解析成功；版本与 dsh 0.1.7-alpha.2 锁定 | 1d |

### B 轨：接口与交付面

| ID | 工作包 | 产出 | 依赖 | DoD | 粗估 |
| --- | --- | --- | --- | --- | --- |
| B1 | C 核心：传输/RPC/队列 | 分帧、JSON-RPC 关联、超时、stdout 首帧校验、有界事件队列、`last_error` | 04 | 单测覆盖边界（超长行、解析失败、超时、队列溢出）；无线程泄漏 | 2d（B 决定是否参考 prototypes） |
| B2 | C ABI 完整实现 | 04 的全部函数、错误码映射、会话表、RSI、trace、release 顺序 | B1、04 | 契约测试用 fixtures 回放；所有错误码有测试；ABI 结构体 size/字段校验 | 2d（B 决定是否参考 prototypes） |
| B3 | Win32 进程与平台层 | `platform_win32.c`：CreatePipe、CreateProcessW、Job Object（KILL_ON_JOB_CLOSE）、UTF-16 路径、stderr | B1、Windows 环境 | Windows 上 DLL 构建；父进程退出回收整棵进程树；崩溃/退出码可观测；`/MT` | 3d |
| B4 | 控制台示例 | `examples/console-c`，30 行内完成一次调用 | B2、玩具 MCP | 输入→模型→工具→结果→退出；计时 <1 分钟；取消可演示；CMake/VS 工程 | 1d |
| B5 | 界面示例 | Win32（推荐）或 Electron 备选；输入/提交/实时状态/结果/取消 | B4 | 五项 UI 齐；任务期间可交互；取消生效；长任务不卡 | 2d |
| B6 | 离线包 | 07 的目录、install.ps1、自检、SHA256SUMS、VERSION.json | A7/A8、B2/B4/B5 | 干净 Windows 解压即用、无管理员/注册表、无外网、无 VS 运行库 | 3d |
| B7 | 四份交付文档 | 接口说明（含线程模型）、集成指南、离线部署、错误码 | 02/03/04/07、B4/B6 | 中文 Markdown；按文档独立集成可复现示例；与代码逐条核对 | 3d |
| B8 | 验收演练脚本与检查表 | 脚本、检查表、未签名说明、哈希清单 | B6 | 干净 VM 断网按四条标准逐条过；记录人/时间/结果 | 1d |

### 共同

| ID | 工作包 | 产出 | 依赖 | DoD | 粗估 |
| --- | --- | --- | --- | --- | --- |
| C1 | Fixtures 与契约测试 | `native/tests/fixtures/*.jsonl` + `manifest.json`；两侧回放 | A1/A3、B1、05 | 每类事件至少正常/失败/取消；C 与插件用同一 fixture；可一键重放 | 2d（C1a 捕获可先做） |
| C2 | M0 端到端 | 控制台示例 + 真实运行时 + 玩具 MCP + mock 模型；计时与事件序列 | A2/A3/A6/A7、B2/B4 | 一次工具调用 <1 分钟；事件顺序与 03 一致；记录报告 | 2d |

**合计粗估**：A 轨 14 人日，B 轨 17 人日，共同 4 人日；两人并行 + 现场联调，按 M0–M3 六周排。

## 五、依赖与并行顺序

| 波次 | 时间 | 可并行工作 | 说明 |
| --- | --- | --- | --- |
| 0 | 已完成 | 02/03/04/header、06、本文、05、07、delivery 索引 | 分工前文档 |
| 1 | 第 1 周 | A1、A2、A3、A6、A7 的玩具 MCP；B1、B2、B4 骨架；C1a 捕获 | 两侧都不等对方：A 用 Stage 0/注入流，B 用假运行时 |
| 2 | 第 2–3 周 | A4、A5、A7 的运行时组装、A8；B3（如 Windows 就绪）、B5；C1b 回放 | preset 面与 Win32 是两条长杆 |
| 3 | 第 4–5 周 | A8、B6、B7、B8、C2 | 离线包与文档依赖前两波 |
| 4 | 第 6 周 | M3 验收演练 | 干净 VM + 现场联调 |

关键路径：`A2/A3 → A4 → A7 → C2 → M0`；`B2 → B4 → C2`；`B5 → B6 → B8 → M3`。A6 在波次 1 完成基础版，preset 行由 A4 追加。B3 受 Windows 环境阻塞，不占 A 轨关键路径；若 Windows 环境继续暂缓，B3/B5/B6 的 Windows 验证顺延，但代码与文档可先在 WSL/交叉编译下推进。

## 六、接口接缝（具体约束）

### 1. JSON-RPC 传输

- stdio、逐行 JSON-RPC 2.0、UTF-8、LF、一行一个对象；单行上限 64 MiB（C 配置 `max_line_bytes`）。
- C 生成整数请求 id；插件原样回显；通知无 id。
- 错误：`error.code` 用 JSON-RPC 保留码，`error.data.code` 用稳定字符串（见 04 表）；C 映射为数值码。
- `initialize` 是就绪边界；插件在答复前 `await loader`。C 的 `bole_init` 不等待，`wait_ready` 等握手。
- 结构事件由插件投影，走 `bole.event`；C 不解析 harness 事件。`assistant.delta` / `rsi.state` 是专用通知，语义等价。
- 官方 `session.event` 可以继续发，但 C 不用于投影；`session/trace` 提供原始日志。

### 2. `bole.event` 信封

```json
{"sessionId":"s-001","event":{
  "event_id":42,"type":10,"session_id":"s-001","task":1,"step":0,
  "seq":18,"ts_ms":1761100000000,"payload":{"v":1,"turn":1}}}
```

- `type` 只允许 `03-events.md` 的枚举；1–3 由 C 产生，插件不得投递；10–51 由插件投影。
- `payload` 必须带 `"v":1`；新增字段只增不改。
- `seq` 取来源会话事件的每会话序号；瞬时帧与运行时事件为 0。
- `event_id` 由 C 入队时重编号；插件值只用于排障。
- 大字段截断规则与上限见 03 第五节；截断必须带 `"truncated":true`，全文进 trace。

### 3. 事件语义

- `ASSISTANT_MESSAGE`（22）是最终正文/思考的权威；`TEXT_DELTA`/`REASONING_DELTA` 可丢。
- 每个 turn 恰好一个 `TASK_FINISHED`（50）或 `TASK_FAILED`（51）；`aborted` 归 50，不是失败。
- `VERIFY_CHECK`（40）保留定义，本运行时不产生、不承诺。
- 来源映射以 03 第四节为准；不得为「看起来完整」补事件。

### 4. C ABI

- 头文件 `native/include/bole_agent.h`；`BOLE_ABI_VERSION = 0x00010000`；结构体首字段 `size`。
- 所有导出函数 `__cdecl`、`BOLE_API`；C99 + C++ `extern "C"`；字符串 UTF-8。
- 控制面只等本地 RPC，不等模型/工具；`trace_export` 等文件写完，禁止在回调中调用。
- 错误码数值是 ABI；协议 `data.code` 是稳定字符串；两者映射见 04 第四节。
- 一个 `bole_handle` 对应一个运行时进程；一个进程可承载多个 `bole_session`；`release` 失效全部会话。

### 5. 运行时布局与启动

C 库启动 `<runtime_dir>/node.exe`，参数固定：

```
<runtime_dir>/node.exe
  <runtime_dir>/node_modules/@deepseek-ai/dsh/lib/bin.js
  --profile sdk
  --patch <runtime_dir>/cordis.patch.yml
```

- 环境：`DSH_HOME=<dsh_home>`、`DSH_PERMISSION_MODE=workspace-write|danger-full-access`、`DSH_TELEMETRY_DISABLED=1`。
- 实施修正（10-22，F9）：`@bole/sdk-server` 与经验包装进 `<dsh_home>/profiles/sdk/node_modules`，不只放 `runtime/node_modules`；Loader 对裸包名按 profile 解析。
- 工作目录：`bole_config.cwd`（默认调用方进程 cwd）。
- `bole_init` 校验 `node.exe`、入口、`cordis.patch.yml` 存在；缺失返回 `1001`。
- 离线包目录与 `VERSION.json` 见 07；A 负责插件/patch，B 负责包与启动配置。

### 6. Fixtures

- 路径：`native/tests/fixtures/`；一行一个 wire 通知（`bole.event` / `assistant.delta` / `rsi.state`），JSONL。
- `manifest.json` 描述场景、期望事件数、终态、超时；不写「理想」序列，必须来自真实运行。
- 命名：实际 12 个 fixture 以 `native/tests/fixtures/manifest.json` 为准（happy-tool、text-turn、cancel-mid-turn、model-error、tool-error、max-tokens、long-tool-output、plan-updated、reasoning、rsi-delivered/available/unsupported）。
- 捕获脚本与沙盒一起入库；路径、session id、时间戳可替换为稳定的占位值，但事件形状与顺序不能改。

### 7. 构建产物

- C：`bole_agent.dll`、`bole_agent.lib`、`bole_agent.h`；`/MT`；PDB 可选；无 VS 运行库依赖。
- 插件：`lib/index.js` + `lib/types/**/*.d.ts` + `package.json`；依赖 dsh 0.1.7-alpha.2。
- 补丁：`cordis.patch.yml`；preset 声明与经验包随包。
- 版本：`VERSION.json` 含 dsh、node、`@bole/sdk-server`、preset、经验包、玩具 MCP、C 库版本。

## 七、协作规则

- 分支：先把 `docs/bole-sdk` 合入 `develop`；A 从 `develop` 开 `feat/bole-runtime`，B 开 `feat/bole-native`。契约变更单独开小分支，A+B 双审。
- 目录所有权见第二节；跨目录改动先在群里/PR 说明，禁止同日双方改同一文件。
- 一个工作包一个 PR；PR 必含：代码、测试、fixture（如涉及）、文档更新、验证命令与输出。
- DoD：代码可构建、测试绿、错误码与 02/04 一致、文档同步、无新增已知红、交叉评审通过。
- 每日 15 分钟对齐：昨天完成、今天计划、阻塞、契约变更。
- 不提交 `node_modules`、`lib/`、`build/`、临时 `DSH_HOME`、密钥、现场路径；`runtime/packages/sdk-server/node_modules` 是开发期软链，已在 `.gitignore`。
- 不代对方做实现决定；`prototypes/` 不计入工作包、验收门或测试证据。
- 文档、PR、脚本不写本机绝对路径、用户名、机器名、临时目录；命令统一用 `$REPO`、`$HARNESS_WORKSPACE`、`$DEV_HOME`、`$PATCH_FILE` 或 `<...>` 占位符。
- Windows 机器只有一台时，A/B 预约使用；测试用独立 `DSH_HOME` 与临时目录，避免端口/会话冲突。

## 八、验收门

| 门 | 时点 | 必须给出 | 签字 |
| --- | --- | --- | --- |
| G0 文档冻结 | 本轮 | 02/03/04/header + 05/07/08 + delivery 索引；A/B 领取表填好；B 负责人确认 04/header 可实现（或提出变更）；逐项见 §11 | 项目负责人 |
| G1 M0 | 第 1 周末 | 控制台示例 + 真实运行时 + 玩具 MCP 跑通一次工具调用并计时；preset 面最小可运行 | A+B |
| G2 M1 | 第 3 周末 | 全接口、事件投影、取消、RSI、step/thinking；fixtures 回放绿 | A+B |
| G3 M2 | 第 5 周末 | 两个示例、四份文档、离线包干净机可运行 | A+B |
| G4 M3 | 第 6 周末 | 干净 Windows 10/11 断网演练四条验收 | 项目负责人 |

验收证据：命令与输出、计时、fixture 数量、退出码、截图/录屏、`VERSION.json`、`SHA256SUMS`。不接受「口头通过」。

## 九、未决事项与风险

| 事项 | 状态 | 谁跟进 | 需要时点 |
| --- | --- | --- | --- |
| Windows 构建环境（Build Tools/pnpm/机器数量） | 暂缓（10-22 口径） | 项目负责人 | B3/B5/B6 前 |
| 目标机安全策略（未签名/EDR） | 未知 | 项目负责人 | M3 前 |
| 经验包正文随包分发的商务确认 | 未确认 | 项目负责人 | M2 前 |
| 内网网关可达性、Ornith 模型、图片支持 | 现场确认 | 项目负责人 | M0 前 |
| 沙箱后端不可用的兜底 | 已定：自检 + `SANDBOX_UNAVAILABLE` + 可切 danger | A6/B2 | M1 |
| upstream preset 面漂移 | 锁定 0.1.7-alpha.2；升级逐行核对 | A4 | 升级时 |
| `sdk/server` 两个既有测试红（fork 改名） | 已知，不是本次引入 | B | 记录，不阻塞 |
| 交付文档格式 | Markdown（10-22 已定） | B7 | M2 |

## 十、领取确认（项目负责人填）

| 工作包 | 负责人 | 开始 | 目标完成 | 备注 |
| --- | --- | --- | --- | --- |
| A1–A8 |  |  |  |  |
| B1–B8 |  |  |  |  |
| C1–C2 |  |  |  |  |

领取后把本表填好，并在 `00-decisions.md` 确认记录追加一行（编号 24 起；23 已用于本轮文档冻结）。任何工作包的范围变化先改本文，再动代码。

## 十一、G0 签收清单（分工前）

- [ ] 02 / 03 / 04 / `bole_agent.h` 已读；B 负责人确认 04/header 可实现，或按 §3 提出变更。
- [ ] 05 / 06 / 07 / 08 已读；`$REPO` / `$HARNESS_WORKSPACE` / `$DEV_HOME` / `$PATCH_FILE` 等占位符约定确认。
- [ ] §10 领取表已填：A1–A8、B1–B8、C1–C2 都有负责人与目标完成时间。
- [ ] 文档无本机绝对路径、用户名、机器名、临时目录；相对链接可用。
- [ ] 第九节未决事项都有 owner 与需要时点。
- [ ] 开发暂停状态确认：A 轨 Stage 0 归 A；B 轨实现归 B；`prototypes/` 未授权、不计入工作包。
- [ ] G1 验收证据格式确认（05 §5）：ctest/插件测试输出、`--dump-config`、集成日志、计时。

签收人：____________　日期：____________
