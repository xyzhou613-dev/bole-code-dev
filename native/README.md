# native/ — 伯乐 SDK 的 C 库

Windows x64 交付件：`bole_agent.dll` + `bole_agent.lib` + `bole_agent.h`。
不进 pnpm workspace，用 CMake + MSVC 构建；POSIX 平台层只用于本机契约测试。

契约以 [`docs/bole-sdk/04-c-abi.md`](../docs/bole-sdk/04-c-abi.md)、
[`02-protocol.md`](../docs/bole-sdk/02-protocol.md)、
[`03-events.md`](../docs/bole-sdk/03-events.md) 与冻结头文件
[`include/bole_agent.h`](include/bole_agent.h) 为准。头文件只增不改。

## 状态

| 工作包 | 内容 | 状态 |
|---|---|---|
| B1 传输/RPC/队列 | NDJSON 分帧、行上限、请求关联与超时、首帧校验、有界事件队列、`last_error` | 已实现；`2005` / `2006` / `2007` 路径有测试 |
| B2 C ABI | 04 的全部函数、错误码映射、会话表、RSI 投影、trace、release | 已实现；POSIX 契约测试 + 真实运行时 C ABI 测试通过 |
| B3 Win32 平台层 | `CreatePipe` / `CreateProcessW` / Job Object / UTF-16 路径 / stderr | 已实现；mingw-w64 交叉编译通过，**待在 Windows/MSVC 上运行验证** |
| B4/B5 示例 | 控制台、Win32 界面 | 不在本目录 |
| C1 fixtures | 4 个 wire 场景 + `manifest.json` + `validate.mjs` | 已入库（0.1.7-alpha.2 + mock 模型捕获） |

## 结构

```text
native/
├── include/bole_agent.h      冻结的 C ABI 1.0（唯一签名来源）
├── src/
│   ├── agent.c               公开函数、句柄状态机、会话表、事件队列、控制面
│   ├── transport.c           运行时 stdio 的 NDJSON 传输与行上限
│   ├── rpc.c                 请求 id、应答关联、超时
│   ├── json.c                单遍 JSON 扫描与构造（成员视图，不建 DOM）
│   ├── error.c               wire `data.code` → 数值码 + 文案
│   ├── platform.h            mutex/cond/thread/process/pipe 与 UTF-8 文件抽象
│   ├── platform_posix.c      POSIX 实现（macOS/Linux 本地测试用）
│   └── platform_win32.c      Win32 实现（交付目标）
├── tests/
│   ├── fake_runtime.c        按 `BOLE_FAKE_MODE` 回放握手/会话/事件/故障
│   ├── test_json.c           JSON 扫描、转义、数字、构造器边界
│   ├── test_error.c          `data.code` → 数值码映射
│   ├── test_agent.c          传输、事件、会话、RSI、trace、取消、背压、release
│   ├── test_runtime.c        真实 --profile sdk + mock 模型的 C ABI 端到端
│   └── fixtures/             wire 通知样例与校验脚本
├── scripts/
│   ├── cross-win64.sh        mingw-w64 语法/链接检查
│   └── integration-runtime.mjs 真实运行时测试编排（mock 模型 + 临时 DSH_HOME）
└── CMakeLists.txt
```

## 构建与测试

POSIX（开发机自测；`<build-dir>` 自定）：

```bash
cmake -S native -B <build-dir> -DCMAKE_BUILD_TYPE=Debug
cmake --build <build-dir> -j4
ctest --test-dir <build-dir> --output-on-failure
```

如果开发机已按 [`runtime/dev/README.md`](../runtime/dev/README.md) 准备好锁定的
运行时树、已构建插件并执行过 `build-experience.mjs`，配置时带上
`BOLE_DEV_RUNTIME`，CTest 会额外注册真实运行时用例：

```bash
BOLE_DEV_RUNTIME=<runtime> cmake -S native -B <build-dir>
cmake --build <build-dir> -j4
BOLE_DEV_RUNTIME=<runtime> ctest --test-dir <build-dir> --output-on-failure
```

也可以单独跑：

```bash
BOLE_DEV_RUNTIME=<runtime> node native/scripts/integration-runtime.mjs \
  <build-dir>/bole_test_runtime
```

Windows x64（正式交付，需 MSVC Build Tools 2022）：

```powershell
cmake -S native -B native/build -G "Visual Studio 17 2022" -A x64
cmake --build native/build --config Release
ctest --test-dir native/build -C Release --output-on-failure
```

POSIX 主机上的 Win32 语法/链接检查（可选，不能替代 MSVC）：

```bash
native/scripts/cross-win64.sh <build-dir>
```

脚本用 `x86_64-w64-mingw32-gcc -Werror` 编译全部源码、链接 DLL 与
`json` / `error` 测试，并依赖 `--exclude-all-symbols` 只导出 14 个公开 ABI。

- POSIX 测试使用 `src/platform_posix.c`；Windows 测试目前只构建 `json` / `error`
  与 fixture 校验，`platform_win32.c` 的 DLL/Job Object 行为在 B3 构建机上验证。
- 原始 clang 命令（不装 CMake 时）见本文件末尾。

## 契约要点

- **配置版本兼容**：`bole_config`、`bole_session_options` 按调用方声明的 `size`
  只读取已声明字段；缺失字段取默认值/零。输出结构 `bole_rsi_state` 只写调用方
  声明长度内的字节。
- **首帧校验**：stdout 第一行必须是带 `"jsonrpc":"2.0"` 的 JSON 对象，否则
  `2006` 并断开；之后解析失败报 `2002`。
- **行上限**：双向都受 `max_line_bytes` 约束；超限报 `2005`、回
  `LINE_TOO_LONG` 并断开。`0` 表示 64 MiB。
- **就绪**：`bole_init` 只启动进程并返回；`initialize` 的应答是就绪边界。
  `bole_wait_ready` 超时返回 `2004`，控制面本地往返超时返回 `2003`。
- **错误映射**：先按 04 的稳定字符串精确映射；未知字符串按 1xxx–7xxx 区间
  归档；`INVALID_ARGUMENT` 在 `-32600` 下映射 `1002`，在 `-32602` 下映射
  `1006`。
- **事件**：`bole.event` 只解析信封字段；`type` 1–3 由 C 库产生，插件投递的
  会被忽略；40（`VERIFY_CHECK`，本运行时不产生）与未知类型被忽略，已定义类型
  原样转发。`event_id` 由 C 重编号，`ts_ms` 句柄内非递减，`payload` 必须带
  `"v":1`。
- **背压**：队列有界（4096 条 / 16 MiB）；先丢最旧的 `TEXT_DELTA` /
  `REASONING_DELTA`，结构事件溢出报 `2007`、清空待投递队列并断开，不静默丢。
- **回调线程**：单条 SDK 事件线程、串行 FIFO；回调内允许提交/取消/preset/
  RSI/`last_error`/替换回调，禁止 `bole_init`、`bole_wait_ready`、
  `bole_release`、`bole_trace_export`（返回 `1002`，不产生死锁）。
- **release**：立即返回；置停止位后由后台回收线程在调用方引用归零时发
  `shutdown`，最多等 2 秒再杀进程树并释放句柄。返回后不再开始新回调，已在
  飞行中的一次允许完成；所有会话句柄失效。
- **进程树**：Windows 用 `KILL_ON_JOB_CLOSE` 的 Job Object；POSIX 测试层用
  独立进程组 + `SIGKILL`，保证工具子进程不残留。
- **日志**：`log_path` 只追加 stderr 与诊断；未配置时默认丢弃，不写协议 stdout。

## 已知限制与待办

- `platform_win32.c` 已用 mingw-w64 交叉编译并检查 DLL 导出；仍未在
  Windows/MSVC 上运行，B3 需验证 Job Object 回收、UTF-16 路径、管道继承与
  `CREATE_NO_WINDOW`。
- 真实运行时用例使用 `runtime/dev` 的 mock 模型；真实内网网关、玩具 MCP
  工具调用、Windows 上的 `--profile sdk` 仍需现场/Windows 环境验证。
- `tests/fixtures/` 是 0.1.7-alpha.2 + mock 模型的捕获；真实内网网关还需按
  05 §3 做一次集成抽检，不写入 fixtures。
- `session/trace` 的文件写入由运行时插件完成，C 库只传 UTF-8 路径并等待应答。
- 不提供单会话关闭；会话随 `bole_release` 失效。
- 未实现 MCP 动态注册（二期）；`VERIFY_CHECK` 保留定义但永不产生。
- [`prototypes/native-c-sdk/`](../prototypes/native-c-sdk/README.md) 是开发暂停
  前的未授权参考，不属于交付物；本目录实现已按 02/03/04 重写并加固。

## 不装 CMake 的快速自测

```bash
cd native
F="-std=c99 -Wall -Wextra -Wpedantic -Werror -Iinclude -Isrc -DBOLE_AGENT_STATIC"
clang $F -o <build-dir>/fake tests/fake_runtime.c src/json.c
clang $F -o <build-dir>/tj tests/test_json.c src/json.c && <build-dir>/tj
clang $F -o <build-dir>/te tests/test_error.c src/error.c && <build-dir>/te
clang $F -D"FAKE_RUNTIME_PATH=\"<build-dir>/fake\"" \
  -D"BOLE_FIXTURE_DIR=\"$PWD/tests/fixtures\"" -o <build-dir>/ta \
  tests/test_agent.c src/agent.c src/error.c src/json.c src/rpc.c \
  src/transport.c src/platform_posix.c -lpthread && <build-dir>/ta
```
