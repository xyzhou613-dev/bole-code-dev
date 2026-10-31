# SDK 数据传输能力说明

适用：伯乐 Agent SDK v1 / ABI 1.0 / protocol 1。  
结论先行：**文字和图片可以传；任意文件（PDF/Word/Excel/音视频/二进制）不能直接传。**

## 1. 能传 / 不能传

| 数据类型 | 能否通过 SDK 传 | 说明 |
| --- | --- | --- |
| 文字 | 可以 | `bole_submit` 或 `bole_submit_blocks` 的 text block |
| 图片 | 可以传，模型不一定能理解 | `bole_submit_blocks` 的 image block，base64 |
| 多张图片 | 可以 | contentBlocks 数组里放多个 image block，受模型上下文限制 |
| PDF / Word / Excel | 不可以直接传 | 没有 file 输入块；需先转文本或用工具读取 |
| 音频 / 视频 | 不可以 | 没有对应 content block |
| 任意二进制文件 | 不可以 | SDK 不提供文件上传/下载接口 |
| 文件 URL / 图片 URL | 不可以作为附件 | image block 只接受 base64，不接受 URL |
| 工具返回的图片/文件 | 不可以取回原文件 | C 事件只显示 `[image]` / `[file]` 占位和文本摘要 |
| 模型返回图片/文件 | 不可以 | SDK 只回传文字/JSON 事件，没有二进制下载 |
| 每次调用单独传 API key | 不可以 | key 在运行时 `home\.env` 统一配置 |

## 2. 文字

```c
bole_submit(session, "你好");
```

或：

```json
[{"type":"text","text":"你好"}]
```

文字按 UTF-8 传输。

## 3. 图片

```c
const char* blocks =
  "[{\"type\":\"text\",\"text\":\"这张图里是什么？\"},"
  " {\"type\":\"image\",\"data\":\"<base64>\",\"mimeType\":\"image/png\"}]";

bole_submit_blocks(session, blocks);
```

支持：

```text
image/png
image/jpeg
image/webp
image/gif
```

图片会进入运行时附件存储，再把引用交给模型。

### 重要：模型能看图，但要用“图片”而不是“base64 文本”

根据阿里云 Model Studio 的模型说明，`qwen3.8-27b` 是原生视觉语言模型：

- 输入：Image / Text / Video
- 输出：Text
- 来源：<https://docs.modelstudio.console.alibabacloud.com/en/model-studio/qwen3-8-27b.md>

所以模型本身具备看图能力。但 base64 只是编码，不能把 base64 字符串当普通文字丢给模型；
必须作为 image content block（OpenAI-compatible 接口里是 `image_url` 的 data URI）发送。

当前 SDK 的 `bole_submit_blocks` 会生成 image block，路径是正确的。  
仍需在实际网关上确认：

- 网关是否接受 `data:image/...;base64,...` 形式；
- 运行时模型配置是否允许图片输入；
- 模型是否返回了正确的图片理解结果。

当前环境访问不到实际网关，尚未完成端到端图片测试。

## 4. 文件

SDK v1 没有 `file` 输入块，不能把 PDF/Word/Excel 原文件直接发给模型。

可行方案：

### 方案 A：转成文本

```text
读取文件 → 提取文本/Markdown → text block 发给模型
```

适合 txt、md、csv、日志、代码。

### 方案 B：让模型用工具读路径

把文件放到会话工作目录，prompt 里给路径：

```text
请读取 D:\work\report.md 并总结
```

模型通过内置文件工具或 MCP 文件工具读取；文件内容不经过 SDK 管道。

### 方案 C：写一个文件读取 MCP 工具

例如：

```text
read_document(path) -> 提取后的文本
```

模型调用 MCP 工具拿文本，SDK 仍然只传 JSON。

### 方案 D：扩展 SDK

如果必须原生传 PDF/Office，需要在附件层增加 file content block 和解析器，属于后续能力，不是当前 v1。

## 5. 工具结果里的图片/文件

工具（MCP 或内置）如果返回图片/文件，C SDK 事件里的 `resultSummary` 只给文本摘要，图片/文件块显示为：

```text
[image]
[file]
```

SDK 不提供接口把这些二进制内容取回给调用方。

## 6. 数据路径

```text
调用方程序
  ↓ C ABI
bole_submit / bole_submit_blocks
  ↓ 匿名管道，NDJSON JSON-RPC，UTF-8
SDK runtime
  ↓ OpenAI-compatible HTTP
模型网关
```

- SDK 不经过自己的云端服务；
- 文件内容不会自动上传；
- 图片以 base64 进入运行时附件存储；
- 模型网关是否接受图片，取决于模型/gateway 能力。

## 7. 对产品/客户选型的建议

1. **纯文字对话**：直接用 `bole_submit`。
2. **要发截图/图片**：先确认模型有视觉能力；SDK 侧用 `bole_submit_blocks`。
3. **要发 PDF/Office**：先做文本提取，或提供文件读取 MCP 工具；不要承诺“原文件直传”。
4. **要回传生成的文件/图片**：SDK v1 不支持；需要业务侧用文件系统/MCP 工具自行处理。
5. **只是要裸模型 API**：如果不需要会话、工具、RSI、轨迹，直接调模型网关更简单；SDK 会多一层 runtime。

## 8. 已验证 / 未验证

已验证：

- 文字 prompt、工具调用、事件回传；
- 真实网关文本对话；
- toy MCP 工具结果文本回传。

官方资料确认：

- `qwen3.8-27b` 是原生视觉语言模型，支持 Image/Text/Video 输入、Text 输出；
- base64 需要作为 image block / data URI 发送，不能作为普通文字。

未验证：

- 实际网关是否接受 base64 data URI；
- 当前运行时配置是否已打开图片输入；
- PDF/Office 解析；
- 图片/文件回传。

图片能力需要在当前实际网关上补一次端到端验证。
