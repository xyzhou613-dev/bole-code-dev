# 07 离线包布局与自检（2026-10-22）

状态：分工前基线。负责人：B 轨（接口与交付面）；A 轨提供插件、补丁、preset 与经验包产物。本文是 C 库启动与打包的共同约束，不包含实现；具体实现、脚本与示例由 B 负责人决定；当前仓库不含 C 实现或离线包。05 的验收演练以本文的目录为准。

## 一、目标与硬性要求

目标机：Windows 10 / 11 x64，内网环境、无外网出口。硬性要求：

- 解压即用；不写注册表、不要求管理员权限。
- 目标机无需预装 Node、.NET、VS 运行库；C 库用 `/MT` 静态运行库。
- 运行期无外网下载；模型、依赖、工具、经验包全部随包。
- 集成方按示例完成一次调用不超过 30 行代码。
- 离线包自检能证明：运行时能起、协议能握手、会话能建、能提交、能完成一次工具调用。

## 二、目录树

```
bole-agent-sdk-<版本>-win-x64/
├── bin/
│   ├── bole_agent.dll
│   ├── bole_agent.lib
│   └── bole_agent.h
├── runtime/
│   ├── node.exe                         # 锁定的 Node 运行时
│   ├── node_modules/
│   │   ├── @deepseek-ai/dsh/            # npm 壳（0.1.7-alpha.2）
│   │   │   └── lib/bin.js               # C 库启动入口
│   │   ├── @deepseek-ai/dsh-*/          # 官方依赖树（版本锁定）
│   │   ├── @bole/sdk-server/            # 自建 JSON-RPC 插件（lib/index.js）
│   │   └── ...                          # 经验插件与其它运行时依赖
│   ├── cordis.patch.yml                 # sdk-app 补丁层（C 库按此路径启动）
│   ├── preset/                          # standard / rsi-infer 声明（如独立文件）
│   ├── config/                          # 模型、MCP server 模板、profile 配置
│   └── assets/                          # 经验包 assets（final-experiences.json 等）
├── home/                                # DSH_HOME：可写；会话/存储/日志
│   ├── profiles/sdk/node_modules/       # 自定义插件安装位（见下方实施修正）
│   │   ├── @bole/sdk-server/
│   │   └── @deepseek-ai/dsh-skill-bole-experience/
│   ├── sessions/
│   ├── storages/
│   └── logs/
├── tools/
│   └── toy-mcp/                         # 验收用玩具 MCP server（stdio，无外网）
├── samples/
│   ├── console-c/                       # 控制台示例源码 + 可执行
│   └── gui-win32/                       # 界面示例源码 + 可执行
├── docs/
│   ├── 01-interface.md                  # 接口说明（含线程模型）
│   ├── 02-integration.md                # 集成指南
│   ├── 03-offline-deploy.md             # 离线部署
│   └── 04-error-codes.md                # 错误码
├── VERSION.json                         # 组件版本清单
├── SHA256SUMS                           # 校验清单
└── install.ps1                          # 校验/安装（无管理员）
```

`home/` 与 `runtime/` 分离：运行时树只读（安装后不被改写），会话、日志、缓存都落 `home/`。如果现场需要多用户/多实例，复制整个包目录或给每个实例独立 `home/`。

> 实施修正（2026-10-22，F9）：Loader 对补丁/预设里的裸包名按 profile 解析。`@bole/sdk-server` 与经验包必须装进 `home/profiles/sdk/node_modules`（可预置整个 profile），只放 `runtime/node_modules` 会报 `failed to import`。`runtime/node_modules` 仍放官方依赖树与 node.exe。见 [09-a-track-plan.md](09-a-track-plan.md) F9 与 `runtime/config/README.md`。

## 三、C 库启动约束

C 库按以下约定启动，B 轨实现、A 轨保证运行时布局：

| 输入 | 值 |
| --- | --- |
| `bole_config.runtime_dir` | `<包根>/runtime` |
| `bole_config.dsh_home` | `<包根>/home`（可写） |
| `bole_config.cwd` | 调用方项目目录；缺省用调用方进程 cwd |
| 可执行 | `<runtime_dir>/node.exe` |
| 入口 | `<runtime_dir>/node_modules/@deepseek-ai/dsh/lib/bin.js` |
| 参数 | `--profile sdk --patch <runtime_dir>/cordis.patch.yml` |
| 环境 | `DSH_HOME=<dsh_home>`、`DSH_PERMISSION_MODE=<workspace-write|danger-full-access>`、`DSH_TELEMETRY_DISABLED=1` |

`bole_init` 的启动校验（失败返回 `1001`）：

- `node.exe` 存在且可执行；
- `node_modules/@deepseek-ai/dsh/lib/bin.js` 存在；
- `cordis.patch.yml` 存在；
- `dsh_home` 不存在时由 SDK 创建（`mkdir -p`）。

运行时布局变更（入口文件名、patch 路径、profile 名）必须同步改 04/本文/C 库，不能只改一侧。

## 四、依赖与版本清单

`VERSION.json` 至少包含：

```json
{
  "sdk": "0.1.0",
  "abi": "1.0.0",
  "protocol": 1,
  "harness": "0.1.7-alpha.2",
  "node": "24.x.x",
  "plugins": {
    "@bole/sdk-server": "0.1.0",
    "@deepseek-ai/dsh-skill-bole-experience": "0.1.7-alpha.2"
  },
  "presets": ["standard", "rsi-infer"],
  "experience": {
    "qwen3.8-27b": "Qwen 服务经验 v3",
    "Ornith-1.5-35B-A3B": "Ornith V18"
  },
  "tools": {
    "toy-mcp": "0.1.0"
  },
  "builtAt": "2026-10-22T00:00:00Z"
}
```

约束：

- harness 版本锁 `0.1.7-alpha.2`；C 库与插件依赖写死这个版本，不用 `workspace:*`。
- Node 版本选 LTS（当前开发机 24.19.0），随包 `node.exe`；`VERSION.json` 记录精确版本。
- 经验包必须包含 `assets/final-experiences.json`；缺资产时插件启动校验失败，自检必须覆盖。
- 玩具 MCP 的 Node 依赖随 `tools/toy-mcp/` 自带，不依赖全局安装。

## 五、安装脚本

`install.ps1` 只做校验与目录准备，不做系统级安装：

1. 校验 `SHA256SUMS`（所有文件）。
2. 检查 `bin/`、`runtime/node.exe`、`runtime/node_modules/@deepseek-ai/dsh/lib/bin.js`、`runtime/cordis.patch.yml` 存在。
3. 创建/确认 `home/` 可写；不覆盖已有 `home/sessions`。
4. 输出安装路径、版本、下一步（运行 samples/console-c）。
5. 不写注册表、不写系统 PATH、不需要管理员；失败以非零退出码和可读文案结束。

卸载 = 删除包目录；`home/` 是否保留由用户决定，文档写明。

## 六、自检

`selfcheck.ps1`（或控制台示例的 `--selfcheck`）按顺序验证：

| # | 检查 | 通过标准 |
| --- | --- | --- |
| 1 | 校验和与版本 | `SHA256SUMS` 全过；`VERSION.json` 与文件一致 |
| 2 | 启动运行时 | `bole_init` 返回 0；`RUNTIME_READY` 在 30s 内到达 |
| 3 | 能力位 | `capabilities.preset/persistence/trace` 为 true；`sandbox` 非 `unavailable`（或按配置明确告警） |
| 4 | 建会话 | `session/create` 返回 0；两个 preset 各建一个会话 |
| 5 | 提交与工具 | 玩具 MCP 工具调用完成；`TASK_FINISHED(outcome=completed)`；总耗时 <60s |
| 6 | RSI 收据 | rsi-infer 会话出现 `RSI_STATE`；`delivered` 或 `available` 符合任务类型；standard 无收据 |
| 7 | 取消 | 长任务中 `cancel` 返回 0；终态 aborted；之后无该 turn 事件 |
| 8 | 轨迹 | `trace_export` 写出 JSONL；事件数 >0 |
| 9 | 无外网 | 运行期无公网请求（网络审计/代理日志）；telemetry 无外发 |
| 10 | 退出 | `release` 返回 0；运行时进程与工具子进程全部退出 |

自检日志写 `home/logs/selfcheck-<时间>.log`，包含版本、命令、耗时、错误码。

## 七、签名与现场安全

- 当前不假设有代码签名证书；`bole_agent.dll`、`node.exe` 可能被 EDR/杀软拦截。
- 随包提供：未签名说明、所有可执行文件的 SHA256、构建时间、来源说明。
- M3 前在目标同款安全策略的机器上试运行；若被拦截，记录拦截名称/规则，再决定是否采购签名证书。
- 不把 `home/` 放到网络盘；ACL 沙箱对临时目录与工作区有假设（见 02/04）。

## 八、打包步骤（B 轨）

1. 构建 C：`cmake --build build/native --config Release`，产出 `bole_agent.dll/.lib/.h`；确认 `/MT`、无 VS 运行库依赖。
2. 构建插件：`tsc -p runtime/packages/sdk-server/tsconfig.json`，产出 `lib/index.js` + 类型；或 `npm pack` 后安装到运行时树。
3. 组装运行时：`npm install @deepseek-ai/dsh@0.1.7-alpha.2`（打包机联网），把 `@bole/sdk-server`、经验包、preset 声明、`cordis.patch.yml` 放入 `runtime/`。
4. 复制 `samples/`、`tools/toy-mcp/`、`docs/`；生成 `VERSION.json`。
5. 用 `install.ps1` + `selfcheck.ps1` 在干净 VM 跑一遍；记录日志与耗时。
6. 生成 `SHA256SUMS`、未签名说明；打包 zip；在另一台干净 VM 解压复跑。
7. 把 zip 哈希、版本、自检日志交给项目负责人。

## 九、已知限制

- 不提供单会话关闭；会话随 `bole_release` 失效（见 04）。
- MCP 只支持配置文件声明 + 重启生效；运行时注册二期。
- 验证阶段暂缓，`VERIFY_CHECK` 不产生。
- 模型网关在内网；完全离线（无内网）时无法完成第 1 条验收，需现场确认网络口径。
- 经验包正文随包分发需商务确认（见 08 第九节）。
- 不承诺 Windows 10/11 以外的系统；不提供 32 位。

## 十、验收对应

| 验收条款 | 离线包证据 |
| --- | --- |
| 第 1 条：无外网/无开发环境跑通全流程 | 干净 VM 解压 + selfcheck 1/2/4/5/9/10 |
| 第 2 条：界面可交互、取消生效 | 界面示例 + selfcheck 7 |
| 第 3 条：RSI 可观测 | selfcheck 6 + 事件日志 |
| 第 4 条：按文档独立集成 | `docs/02-integration.md` + samples/console-c |
| 第 5 条：事件与真实阶段一致 | 事件日志对照 03；verify 不出现 |
