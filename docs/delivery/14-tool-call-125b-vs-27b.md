# 125b / 27b 工具调用输出对比

来源：GUI 日志

- 125b：`gui-20261112-095627-26740.log`
- 27b：`gui-20261112-103846-5552.log`、`gui-20261112-104717-25396.log`、`gui-20261112-105104-18712.log`

## 125b

### 提示词

```text
你能看到当前的matlab会话吗
```

### 工具调用

```text
TOOL_CALL_STARTED  list_mcp_resources
args: {"server":"matlab"}

TOOL_CALL_FINISHED isError=true
Error: mcp-client(matlab): server is disconnected
```

只调用了 `list_mcp_resources`，没有继续调用 `list_mcp_resource_templates`。

### 输出

```text
MATLAB MCP 服务器当前处于未连接状态，所以我无法看到任何 MATLAB 会话。

让我也确认一下资源模板是否可用：
```

然后直接：

```text
STEP_FINISHED
TASK_FINISHED completed
```

没有第二个工具调用。

## 27b

### 测试 1：调用两个工具

提示词：

```text
调用两个工具，参数随意
```

工具调用：

```text
mcp__toy__echo  {"text":"你好，工具调用测试"}
结果：你好，工具调用测试

mcp__toy__add   {"a":12,"b":30}
结果：42
```

输出：

```text
已成功调用两个工具：

1. echo -> 你好，工具调用测试
2. add(a=12,b=30) -> 42

两个调用都是独立的，所以我在同一批次里并行发出了。
```

### 测试 2：同一条 MATLAB 会话问题

提示词：

```text
你能看到当前的matlab会话吗
```

工具调用：

```text
list_mcp_resources          {"server":"matlab"}
list_mcp_resource_templates {"server":"matlab"}
```

两个都返回：

```text
Error: mcp-client(matlab): server is disconnected
```

输出：

```text
目前看不到——MATLAB 的 MCP 服务当前处于断开连接状态，我无法访问任何 MATLAB 会话的资源。
```

### 测试 3：MATLAB 连接正常时

提示词：

```text
你能看到当前的matlab会话吗
```

工具调用：

```text
mcp__matlab__detect_matlab_toolboxes  {}
```

返回 MATLAB 信息：

```text
MATLAB R2024a
MATLAB 许可证编号: 968398
Windows 11
Simulink / 5G Toolbox / AUTOSAR Blockset 等
```

随后调用：

```text
mcp__matlab__evaluate_matlab_code
```

但 MATLAB 代码里用了：

```text
btype='block_diagram'
```

MATLAB 报错：

```text
错误: 不支持使用 '=' 运算符。
要比较值是否相等，请使用 '=='。
```

这是生成的 MATLAB 代码语法错误，不是 MCP 调用失败。

## 对比结论

| 项 | 125b | 27b |
| --- | --- | --- |
| 不指定工具名，自动选工具 | 不调用 | 调用 echo + add |
| 同一句 MATLAB 会话问题 | 只调 `list_mcp_resources` | 调 `list_mcp_resources` + `list_mcp_resource_templates` |
| 多工具调用 | 未见 | 正常 |
| MATLAB 工具调用 | 未连接 | `detect_matlab_toolboxes` 成功 |
| 工具参数 | 未验证 | `a:12,b:30` 数字正确 |
| 最终结果 | 说“让我确认”后结束 | 实际执行工具并汇总 |

## 结论

```text
125b：模型只发起一次工具调用，随后输出文本并结束。
27b：同一 SDK、同一提示词下，能自动发起多个工具调用，MATLAB 工具也能调用。
```

这不是 SDK 的工具循环问题；差异在模型/FNX 的工具选择与输出。
