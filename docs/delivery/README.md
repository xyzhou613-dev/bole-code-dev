# 对外交付文档索引

状态：四份正文已完成（2026-10-23），已按源码与真实网关结果核对；最终离线包路径与干净 VM 验收由测试团队执行。语言：中文；格式：Markdown。这些文档会由打包脚本复制进离线包 `docs/`，不放进 `packages/*/README`。

## 文档清单

| 文件 | 内容 | 主要读者 | 依赖 |
| --- | --- | --- | --- |
| `01-interface.md` | 接口说明：SDK 边界、C ABI、线程模型、生命周期、配置、函数与事件、RSI、限制、版本 | 伙伴的 C/C++/绑定开发者 | 02/03/04 |
| `02-integration.md` | 集成指南：链接/加载 DLL、30 行调用示例、事件切 UI 线程、取消、错误处理、排障 | 伙伴集成工程师 | 01 + 示例 |
| `03-offline-deploy.md` | 离线部署：目录、安装、版本、校验、自检、网络与安全、常见故障 | 客户现场运维 | 07 |
| `04-error-codes.md` | 错误码：数值、常量、wire code、可重试、`last_error`、示例 | 伙伴开发者/排障 | 04 |
| `05-mcp-integration.md` | MCP 示例与新增服务：自带 12 工具、外部 Everything Server、配置模板、截图位置 | 伙伴开发者/测试 | 01/03 |
| `06-external-mcp-install.md` | 外部 MCP 安装指南：Everything Server 安装、配置、重启、验证、卸载 | 伙伴开发者/现场 | 05 |

## 写作规则

- 只写「接口是什么、怎么用」，不重复内部决策与实现细节；需要背景时链接内部文档（不随包时改为摘要）。
- 代码片段必须能编译/运行；示例来自 `samples/console-c` 或真实运行，不手写理想序列。
- 事件示例必须来自 fixture 或真实运行；字段以 `03-events.md` 为准。
- 错误码表与 `native/include/bole_agent.h`、`04-c-abi.md` 逐条一致；新增码只增不改。
- 线程模型写清楚：回调在 SDK 自有线程、同一句柄串行、调用方自行切 UI 线程、回调中禁止 `release`/`wait_ready`/`trace_export`。
- 限制要写明：验证阶段暂不可观测；MCP 配置声明 + 重启生效；不提供单会话关闭；Windows 10/11 x64。
- 不写内部包名、源码路径、宿主服务名；伙伴只感知 SDK。
- 每份文档末尾有「检查清单」，B 自检 + A 复核。

## 各文档最低章节

### `01-interface.md`

1. 概览与边界（薄封装、独立运行时进程、DLL + 随包运行时）
2. 快速索引（函数表、事件表、错误码表）
3. 初始化与配置（`bole_config` 字段、路径、环境、默认沙箱、模型）
4. 句柄、会话、生命周期（引用、release 顺序、崩溃/恢复）
5. 线程模型（四条铁律、回调允许/禁止、背压）
6. 函数参考（逐个：签名、参数、返回、错误、线程约束）
7. 事件参考（信封、16 类事件、payload、顺序、截断、最终正文）
8. RSI（standard/rsi-infer、开关粒度、收据、`RSI_UNAVAILABLE`）
9. 轨迹（turn 过滤、JSONL、文件权限）
10. 限制与版本（verify、MCP、单会话关闭、Windows 版本、ABI/protocol）

### `02-integration.md`

1. 前置条件（Windows x64、无 Node/VS 运行库、包布局）
2. 链接与加载（`.lib`/`.dll`、延迟加载、C/C++/C# 绑定注意）
3. 最小示例（30 行内：init → wait_ready → create → submit → 事件 → release）
4. 事件回调与 UI 线程（队列/切线程、取消按钮）
5. 错误处理（返回码、`last_error`、重试建议）
6. 日志与排障（`log_path`、stderr、trace、常见错误）
7. 集成检查清单

### `03-offline-deploy.md`

1. 包内容与目录
2. 安装/卸载（`install.ps1`、无管理员/注册表）
3. 版本与校验（`VERSION.json`、`SHA256SUMS`）
4. 自检（`selfcheck`、期望输出）
5. 网络与安全（内网模型、无外网、未签名说明、EDR）
6. 升级/回滚（替换包目录、保留 `home/`）
7. 常见故障

### `04-error-codes.md`

1. 错误模型（返回值 + `last_error`、wire `data.code` 映射）
2. 1xxx–7xxx 全表（数值/常量/含义/可重试/典型场景）
3. JSON-RPC 错误码与映射
4. 重试与降级建议
5. 示例（取消无活动、preset 锁定、RSI 不可用、运行时退出）

## 评审

- 开发自检：与 02/03/04/07 逐条对照；示例可运行；错误码表无缺项；2026-10-23 已完成。
- 测试验收：干净 VM 按 `packaging/acceptance.ps1` 跑通；按本文档从零集成一次；GUI 长任务/取消录屏。
- 项目负责人签收：决定是否打包交付。
