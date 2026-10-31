# SDK 可传输的数据

结论：当前 SDK 可以直接把**文字**和**图片**传给模型；图片使用 base64 编码。

## 1. 文字

```c
bole_submit(session, "你好");
```

或使用内容块：

```json
[{"type":"text","text":"你好"}]
```

文字按 UTF-8 传输。

## 2. 图片

```c
const char* blocks =
  "[{\"type\":\"text\",\"text\":\"这张图里是什么？\"},"
  " {\"type\":\"image\",\"data\":\"<base64>\",\"mimeType\":\"image/png\"}]";

bole_submit_blocks(session, blocks);
```

支持的图片格式：

```text
image/png
image/jpeg
image/webp
image/gif
```

图片以 base64 内容块进入运行时附件存储，再交给模型。

## 3. 图文混合

`bole_submit_blocks` 支持一次传多个内容块：

```json
[
  {"type":"text","text":"对比这两张图，指出差异："},
  {"type":"image","data":"<base64-1>","mimeType":"image/png"},
  {"type":"image","data":"<base64-2>","mimeType":"image/jpeg"}
]
```

可以一条消息里同时包含文字和多张图片。

## 4. 模型是否支持看图

当前使用的 `qwen3.8-27b` 是原生视觉语言模型，官方说明的输入模态包括
Image、Text、Video，输出为 Text。

来源：

- <https://docs.modelstudio.console.alibabacloud.com/en/model-studio/qwen3-8-27b.md>
- <https://www.ovhcloud.com/en-ca/public-cloud/ai-endpoints/catalog/qwen-3-8-27b/>

因此模型能力上支持图片。实际网关仍需确认是否接受
`data:image/...;base64,...` 形式的图片输入；这属于网关配置/联调项。

## 5. 数据路径

```text
调用方程序
  ↓ C ABI
bole_submit / bole_submit_blocks
  ↓ 匿名管道，NDJSON JSON-RPC，UTF-8
SDK runtime
  ↓ OpenAI-compatible HTTP
模型网关
```

- 文字按 UTF-8 JSON 传输；
- 图片按 base64 放在 image block 中传输；
- SDK 不经过自己的云端服务；
- 不需要额外部署文件/图片上传服务。

## 6. 传输注意事项

- 图片必须放在 image block 里，不能把 base64 当普通文字发；
- image block 只接受 base64，不接受图片 URL；
- 单行 JSON-RPC 默认上限 64 MiB，base64 会放大图片体积；
- 图片数量、分辨率、token 消耗受模型上下文和网关限制；
- 图片最终能否被理解，取决于模型和网关是否启用了视觉输入。
