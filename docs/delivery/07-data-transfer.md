# SDK 数据发送步骤

SDK 当前能直接发送：

- 文字
- base64 图片

文件不能直接发送，需要先转文本或用工具读取。

## 1. 发送文字

1. 创建或恢复会话。
2. 调用 `bole_submit`：

```c
bole_submit(session, "你好");
```

也可以用内容块：

```json
[{"type":"text","text":"你好"}]
```

文字按 UTF-8 传输。

## 2. 发送图片

1. 把图片转成 base64。
2. 组装 text + image 内容块。
3. 调用 `bole_submit_blocks`。

```c
const char* blocks =
  "[{\"type\":\"text\",\"text\":\"这张图里是什么？\"},"
  " {\"type\":\"image\",\"data\":\"<base64>\",\"mimeType\":\"image/png\"}]";

bole_submit_blocks(session, blocks);
```

支持的格式：

```text
image/png
image/jpeg
image/webp
image/gif
```

一次发多张图：

```json
[
  {"type":"text","text":"对比这两张图"},
  {"type":"image","data":"<base64-1>","mimeType":"image/png"},
  {"type":"image","data":"<base64-2>","mimeType":"image/jpeg"}
]
```

注意事项：

- base64 必须放在 image block 里，不能当普通文字发；
- image block 只接受 base64，不接受图片 URL；
- base64 会放大体积，注意单行 JSON-RPC 上限和模型上下文；
- `qwen3.8-27b` 支持图像输入，实际网关需确认能接受 `data:image/...;base64,...`。

## 3. 发送文件

SDK v1 没有 `file` 输入块，不能直接发 PDF/Word/Excel/音视频/二进制文件。

按以下任一方式处理：

### 方式一：转成文本

1. 读取文件，提取文本或 Markdown。
2. 按“发送文字”调用 `bole_submit`。

适合 txt、md、csv、日志、代码。

### 方式二：给文件路径

1. 把文件放到会话工作目录。
2. prompt 中写文件路径：

```text
请读取 D:\work\report.md 并总结。
```

3. 模型通过文件工具或 MCP 文件工具读取。

### 方式三：写一个文件读取 MCP 工具

1. 实现 `read_document(path)`，返回提取后的文本。
2. 按 MCP 配置接入。
3. 模型调用该工具获取文件内容。

## 4. SDK 不处理的内容

- 不接收图片 URL 作为附件；
- 不接收 PDF/Office/音视频原文件；
- 不返回工具产生的图片/文件二进制，只返回文本摘要和 `[image]` / `[file]` 占位；
- 不返回模型生成的图片/文件；
- 每次调用不能单独传 API key，key 在运行时 `home\.env` 配置。

## 5. 数据路径

```text
调用方程序
  → bole_submit / bole_submit_blocks
  → SDK runtime（匿名管道，JSON-RPC，UTF-8）
  → 模型网关
```

图片以 base64 进入运行时附件存储；文字和图片之外的数据不会自动上传。
