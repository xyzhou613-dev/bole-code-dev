# 125b / 27b 测试提示词提取

来源：GUI 日志

- 125b：`gui-20261112-095627-26740.log`，模型 `qwen3.8-flash-next-w4`
- 27b：`gui-20261112-103846-5552.log`、`gui-20261112-104717-25396.log`、`gui-20261112-105104-18712.log`、`gui-20261112-105343-15256.log`，模型 `qwen3.8-27b`

## 125b 提示词

### task 1

```text
你好
```

### task 2

```text
你能看到当前的matlab会话吗
```

### task 3

```text
检查maplib mcp可用吗
```

### task 4

```text
你目前注册了多少个mcp工具
```

### task 5

```text
你调用两个工具试试
```

### task 6

```text
调用 mcp__toy__echo，参数 text=hello。
```

### task 7

```text
调用两次 mcp__toy__echo，参数 分别是hello， 你好。
```

### task 8

```text
调用两次 mcp__toy__echo，参数随意
```

### task 9

```text
调用两次注册的工具，参数随意
```

## 27b 提示词

### 公共测试

```text
你好
```

```text
你能看到当前的matlab会话吗
```

### 工具调用测试

```text
调用两个工具，参数随意
```

```text
你看看现在的matblb mcp工具可用吗
```

```text
列出你已经注册的mcp工具
```

### 完整 MATLAB 任务

```text
对在matlab中打开的FPGA_Dev_Comp模型进行编辑，指标读取文件："D:\apps\BoleAgentSdk\samples\workspace\提示词.md"，工作空间："D:\apps\BoleAgentSdk\samples\workspace"，只允许使用隔离出来的工作空间内的脚本和skill，产物也输出在工作空间内
```

## 备注

- `matblb` 是日志里的原始拼写，没有改。
- 125b 日志共有 9 条，包含显式工具名和不显式工具名两种。
- 27b 日志里还包含工具调用测试和完整 MATLAB 任务。
