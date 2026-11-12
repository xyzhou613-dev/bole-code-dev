# FNX / SDK 联调问题汇总

日期：2026-11-12  
测试模型：`qwen3.8-flash-next-w4`  
测试服务：`http://192.168.18.206:9002/v1`（FNX）  
测试客户端：BoleAgentSdk r7，console / GUI

## 一、已复现的问题

| # | 问题 | 证据 | 归属 | 状态 |
| --- | --- | --- | --- | --- |
| 1 | FNX 把工具参数的数字、数组转成字符串 | 请求 `{"limit":60,"todos":["alpha","beta"]}`，返回 `arguments: "{\"limit\":\"60\",\"todos\":\"[...]\"}"`；toy add 收到 `"20","22"` 报 `add requires numeric a and b` | FNX tool parser | **存在，必须修** |
| 2 | 工具调用被截断时，FNX 返回正文 `<tool_call>`，没有 `message.tool_calls` | `max_tokens=16/32` 时 content 是未闭合 `<tool_call>`，`tool_calls=null` | FNX parser / 截断处理 | **存在** |
| 3 | SDK 遇到没有结构化 tool_calls 的回复，会正常结束任务 | console 收到 XML 文本后输出 `[task] finished` | SDK 预期行为 | **不是 SDK bug** |
| 4 | 交付包 `runtime/cordis.patch.yml` 与 `SHA256SUMS` 不一致 | `install.ps1` 报 `hash mismatch runtime\cordis.patch.yml` | 交付包 | **已复现，本地已修正** |
| 5 | GUI `.log` 可能截断 | `.jsonl` 保留完整 JSON-RPC，`.log` 是格式化层 | SDK GUI | 已知，待修 |

## 二、未复现 / 与客户环境不一致

| # | 客户反馈 | 当前 9002 实测 | 判断 |
| --- | --- | --- | --- |
| 6 | `max_completion_tokens:8` 被忽略，实际生成 128 | 实际生成 8，`finish_reason=length` | 当前未复现；可能是旧版本或另一条部署 |
| 7 | 流式 `max_tokens=8` 报 `stop` | 返回 `finish_reason=length` | 当前未复现 |
| 8 | 未闭合 `<tool_call>` 后服务端报 `stop` | 未闭合调用可复现，但当前报 `length` | 当前是 `length`，客户应提供具体请求/版本 |
| 9 | “本质是多任务执行不了” | SDK 连续执行 step 1/2/3 工具，循环正常 | **不成立**；失败点是参数类型和工具报错 |
| 10 | GUI 完全不能跑 | 当前 SDK 调 9002 可完成 `MODEL_OK` 和 toy 工具调用 | 模型链路本身通 |

## 三、非 SDK 问题 / 环境阻塞

| # | 问题 | 说明 |
| --- | --- | --- |
| 11 | 本机没有 MATLAB | 无法执行 `load_system`、`add_block`、`update` |
| 12 | 本机没有 MATLAB MCP | `tools` 目录只有 toy-mcp |
| 13 | 本机没有 VMC / IPLib | `FPGA_Dev_Comp.slx` 缺库依赖，无法完整复现 |
| 14 | MATLAB 是客户内网 | 当前拿不到远程权限 |

## 四、根因结论

```text
1. 工具参数类型丢失：FNX parser 的问题，确定存在。
2. 工具调用没变成结构化 tool_calls：FNX parser/截断处理的问题，不是 SDK。
3. 任务提前结束：因为 SDK 没收到 tool_calls，按正常结束处理；不是调度器不执行多任务。
4. max_completion_tokens / 流式 stop：当前 9002 未复现，需客户提供原请求和部署版本。
5. 完整 MATLAB 任务：缺 MATLAB、MCP、VMC、IPLib，当前无法复现。
```

## 五、下一步

### FNX

- 保留工具参数的 JSON 类型：数字、数组、嵌套对象不能字符串化。
- 截断时返回 `finish_reason=length`，不要返回 `stop`。
- 未闭合的 `<tool_call>` 不应伪装成正常完成。
- 建议同时支持 `max_tokens` 和 `max_completion_tokens`。

### SDK

- 125b 配置固定 `maxTokensField: max_tokens`。
- 客户端把 `length` 作为截断处理，不标记正常完成。
- 不解析正文 `<tool_call>`；要求服务端返回结构化 `message.tool_calls`。
- 修复交付包 SHA。
- `.log` 截断不丢失 `.jsonl` 原始数据。

### 测试

- 覆盖数字、数组、嵌套对象参数。
- 覆盖 `max_tokens` / `max_completion_tokens`。
- 覆盖流式截断 `length`。
- 覆盖未闭合 tool_call。
- 完整 MATLAB 任务需要在有 MATLAB/MCP/IPLib 的机器上跑。
