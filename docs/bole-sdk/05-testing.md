# 05 测试分层与验收演练（2026-10-22）

状态：分工前基线。负责人：共同（A 负责插件/事件侧，B 负责 C/打包侧，C1/C2 共同）。本文定义「测什么、怎么测、什么算过」；接口以 `02`/`03`/`04` 为准，工作包以 `08-work-split.md` 为准。

## 一、原则

- 测契约，不测实现细节；断言以 02/03/04 的字段、顺序、错误码为准。
- 契约测试用**真实运行捕获**的 wire fixture，不手写「理想」序列；样例必须能复现。
- 单元测试不依赖真实进程、网络、Windows；契约测试不依赖真模型；集成测试用本地 mock 模型 + 玩具 MCP。
- 每个测试自己建临时目录/`DSH_HOME`/端口，测试结束清理；不在共享目录留会话、进程、日志。
- 测试可并发跑；端口、临时目录、子进程生命周期由测试自己拥有。
- 已知红要写进报告（如 `sdk/server` 两个 fork 改名导致的既有失败），不能算本次回归。

## 二、分层

| 层 | 范围 | 依赖 | 谁写 | 频率 |
| --- | --- | --- | --- | --- |
| L0 单元 | JSON 扫描/构造、分帧、RPC 关联、超时、错误码映射、事件队列、增量合并、preset 解析 | 无 | A/B | 每次提交 |
| L1 契约 | 假运行时回放 fixtures；C API 全函数；插件投影纯函数/注入流 | fixtures | 共同 | 每次提交 |
| L2 补丁层 | 禁用官方 server、插件加载、`initialize`/`shutdown`、自定义方法答复 | 真实 `dsh` | A | 每次 PR |
| L3 集成 | 真实 `--profile sdk` + mock 模型 + 玩具 MCP；控制台示例全流程 | 真实运行时 + mock | A+B | 每日/PR |
| L4 稳定性 | 杀工具、杀运行时、断网、连续多轮、长任务、队列溢出 | L3 | B | M1/M2 |
| L5 验收演练 | 干净 Windows 10/11 VM、断网、四条验收标准 | 离线包 | 共同 | M3 |

### L0 单元

- C（B 轨实现后）：`native/tests/test_json.c`（扫描/转义/非法输入）、`test_agent.c` 的传输/RPC/队列部分；新增 `test_queue.c`、`test_error.c`。
- 插件：事件投影、payload 构造、增量合并、preset 解析、错误映射的纯函数测试；用注入的 `Readable`/`Writable` 跑 transport。
- 断言边界：空串、超长行、非法 UTF-8/转义、负数/超大数、重复 id、超时、队列满、结构事件与 delta 的丢弃策略。

### L1 契约

- 假运行时按请求回放 fixture：`initialize` 固定能力位；`session/create` 固定结果；`session/prompt` 回放一段 wire 通知；`session/cancel` 回放 aborted；`session/trace` 写固定文件。
- C 侧断言：`bole_event` 的 type/task/step/seq/ts/session_id/payload 字段、事件顺序、终态唯一、错误码。
- 插件侧断言：同一 fixture 输入（原始 `session.event`/`assistant-stream`）产出同一 wire 通知；字段逐条比对。
- fixture 与断言一一对应；新增事件必须新增 fixture 与两侧断言。

### L2 补丁层

- `dsh --profile sdk --patch runtime/packages/sdk-app/cordis.patch.yml --dump-config`：行存在、无重复注册、官方 server 被禁用、模型/approval/外网/telemetry 行生效。
- 真实启动：stderr 出现 `[bole-sdk-server] loaded`，无 `did not activate`；`initialize` 返回 `sdkProtocolVersion=1` 与 `capabilities`；`shutdown` 后退出码 0。
- 自定义方法：至少 `session/create`/`cancel`/`rsi.set` 在真实运行时返回预期结果或错误码。

### L3 集成

- 本地 mock 模型：Messages SSE（`deepseek-official`）或 OpenAI chat-completions（`bole`）；见 06。
- 玩具 MCP：stdio，两个无副作用工具（加法和回显/时间），启动 <500ms。
- 流程：`bole_init` → `wait_ready` → `session/create` → `submit` → 观察 `TASK_STARTED`/`STEP_*`/`TOOL_CALL_*`/`ASSISTANT_MESSAGE`/`TASK_FINISHED` → `trace_export` → `release`。
- 断言：工具参数与结果、事件顺序、最终正文、耗时 <1 分钟、退出码。
- 取消：模型调用中、工具执行中、排队中三种时机各一次；终态 `aborted`，之后无该 turn 事件。

### L4 稳定性

- 杀玩具 MCP 子进程：工具调用返回 `isError`，运行时继续，下一条消息可执行。
- 杀运行时：只收到 `RUNTIME_EXITED`，不补造任务终态；新句柄 `resume` 可恢复（若持久化已落盘）。
- 断网：mock 模型下断开网络不崩；真实网关不可达时错误码可读。
- 连续多轮/多会话：句柄/会话不泄漏；事件队列不无限增长。
- 长任务：界面示例执行期间可交互、取消生效。
- 队列溢出：注入大量 delta，验证丢旧 delta、结构事件保留、结构溢出报 2007 并断开。

### L5 验收演练

见第六节。

## 三、Fixtures

### 格式

- 目录 `native/tests/fixtures/`；每个场景一个 `.jsonl`，每行一个 wire JSON 对象（`bole.event` / `assistant.delta` / `rsi.state`），与插件实际输出完全同形。
- `manifest.json`：场景名、描述、来源（运行时间/机器/模型）、期望事件数、期望终态、超时、是否含截断。
- 路径、session id、时间戳可替换为稳定占位值（如 `s-001`、`1761100000000`），但事件顺序、字段、错误码不能改。

### 场景清单（v1）

| 文件 | 覆盖 |
| --- | --- |
| `happy-tool.jsonl` | 一次工具调用：turn/step/header/deltas/assistant/tool/result/finish |
| `cancel-mid-tool.jsonl` | 工具执行中取消，终态 aborted |
| `model-error.jsonl` | `turn/end.reason.error` → `TASK_FAILED` |
| `tool-error.jsonl` | `tool/result.isError`，turn 仍 completed |
| `rsi-delivered.jsonl` | rsi-infer + 服务生命周期任务 + 收据 delivered |
| `rsi-available.jsonl` | rsi-infer + 普通任务 + available |
| `rsi-unsupported.jsonl` | 非允许模型 + unsupported-model |
| `no-preset-plane.jsonl` | `composed=false`，rsi.set 返回 3004 |
| `max-tokens.jsonl` | outcome=max_tokens |
| `long-tool-output.jsonl` | resultSummary 8KB 截断 + trace 全文 |

### 捕获

- 在 `0.1.7-alpha.2` 沙盒或真实内网机上跑一次完整会话，抓 stdout 的 wire 通知；过滤掉 `session.event`（C 不用）。
- 捕获脚本与沙盒配置入库（`scripts/capture-fixtures`，待 B8/C1 建）；脚本必须能重复产出同形状 fixture。
- 捕获时用 mock 模型保证确定性；真实模型用于集成抽检，不写入 fixtures。

## 四、命令

### 变量约定

命令里的 `$REPO`（源码仓库）、`$HARNESS_WORKSPACE`（完整 harness 工作区）、`$DEV_HOME`（临时 `DSH_HOME`）、`$SMOKE_SCRIPT`（本地冒烟脚本）由开发者按自己的布局设置；文档不写死本机路径。

### C（B 轨实现后）

以下为 B 轨实现后的目标命令；当前仓库不提供交付实现，`prototypes/native-c-sdk/` 是未授权参考，不计入 G1/G2。

```bash
cmake -S native -B build/native -DCMAKE_BUILD_TYPE=Debug
cmake --build build/native -j4
ctest --test-dir build/native --output-on-failure
```

### 插件（开发期在 `$HARNESS_WORKSPACE`）

```bash
"$HARNESS_WORKSPACE/node_modules/.bin/tsc" -p "$REPO/runtime/packages/sdk-server/tsconfig.json" --noEmit
# 插件测试 harness 由 A1 建立；目标：
# ./node_modules/.bin/vitest run runtime/packages/sdk-server/tests
```

### 补丁层

```bash
cd "$HARNESS_WORKSPACE"
env DSH_HOME="$DEV_HOME" DSH_TELEMETRY_DISABLED=1 \
  node apps/cli/lib/bin.js --profile sdk \
  --patch "$REPO/runtime/packages/sdk-app/cordis.patch.yml" \
  --dump-config
```

### 集成

```bash
# 本地 Messages SSE mock + 真实 sdk profile；脚本模板见 06
node "$SMOKE_SCRIPT"
```

## 五、门与证据

| 门 | 测什么 | 证据 |
| --- | --- | --- |
| G1 M0 | L0/L1 + L2 + 一次 L3 | B 轨 ctest 输出（实现后）、插件测试输出、`--dump-config`、集成日志、计时 |
| G2 M1 | L1 全 fixtures + L2 自定义方法 + L3 取消/RSI | fixture 数量、事件断言、取消三种时机、RSI 三态 |
| G3 M2 | L3 两个示例 + L4 稳定性 + 离线包自检 | 干净机日志、退出码、SHA256、VERSION.json |
| G4 M3 | L5 四条验收 | 检查表、录屏/截图、计时、签名/哈希说明 |

## 六、验收演练检查表（M3）

环境：自备干净 Windows 10 / 11 x64 VM；无开发环境、无 Node、无 VS 运行库；无外网（内网模型可达，按 10-22 口径）；记录日期、机器、操作人。

| # | 步骤 | 期望 | 记录 |
| --- | --- | --- | --- |
| 1 | 解压离线包，运行 `install.ps1` | 校验通过；不写注册表、不需要管理员 |  |
| 2 | 启动控制台示例，输入「用玩具工具计算 1+2」 | 模型回复、工具调用、结果返回；<1 分钟 | 耗时/日志 |
| 3 | 启动界面示例，提交长任务 | 界面可交互；状态区实时更新 | 截图/录屏 |
| 4 | 任务中点「取消」 | 立即中断；终态 aborted；界面恢复 | 日志 |
| 5 | 新建 standard 会话 | 无经验收据 | 事件 |
| 6 | 新建 rsi-infer 会话，跑服务生命周期任务 | `RSI_STATE` delivered；`deliveredMemoryIds` 非空 | 事件 |
| 7 | 同一 rsi-infer 会话跑普通任务 | `available`（不是 delivered） | 事件 |
| 8 | 把模型改成非允许 ID | `unsupported-model`；经验正文不投递 | 事件 |
| 9 | 空白会话切 preset；首个 turn 后再切 | 前者成功；后者 `PRESET_LOCKED` | 错误码 |
| 10 | 导出 turn 轨迹 | 文件存在、事件数正确、可按 task 过滤 | 文件 |
| 11 | 杀玩具 MCP 子进程 | 工具错误可见；运行时继续；无残留子进程 | 进程列表 |
| 12 | 断外网（保留内网） | 运行正常、无公网请求；telemetry 无外发 | 网络审计 |
| 13 | 按四份交付文档独立集成一次 | 30 行内调用成功 | 代码/日志 |
| 14 | 事件序列对照 03 | 无伪造阶段；verify 不出现 | 事件日志 |

判定：1–14 全过为通过；任何一条失败记录现象、日志、复现步骤，回到对应工作包修复后重跑。

## 七、常见测试坑

- `pnpm --filter <pkg> test` 静默空跑（所有包无 `scripts`）；用根目录 `vitest run <path>`。
- 不设 `DSH_HOME` 会落到默认用户级目录（可能含真实凭据）；测试必须显式临时目录。
- 插件解析失败只是 warning，进程仍 exit 0；必须断言 stderr 无 `did not activate`，并检查 `[bole-sdk-server] loaded`。
- patch 的 `config` 是整体替换；测试断言完整配置，不要只断言改动的字段。
- Windows ACL 沙箱后端不可用会让工具硬失败；集成测试在开发机用 `danger-full-access`，验收机用 `workspace-write` + 自检。
- 端口冲突：mock 模型用随机端口；测试自己关闭 server。
- `sdk/server` 两个既有红是 fork 改名（`Bole Model` vs `DeepSeek`），不是本次引入；报告里单独列出。
- 测试并发时不要共用 `~/.dsh`、固定端口、固定临时路径；每个测试自己 `mkdtemp`。
