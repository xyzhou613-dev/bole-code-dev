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

## 3. 完整示例：发送一张图片

下面的函数读取本地图片，转成 base64，再通过 `bole_submit_blocks` 发给模型。
初始化、建会话和事件回调见 `examples/console-c/main.c`，这里只保留构造和提交部分。

```c
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bole_agent.h"

static char *base64_encode(const unsigned char *data, size_t len)
{
    static const char tab[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t out_len = 4 * ((len + 2) / 3);
    char *out = (char *)malloc(out_len + 1);
    size_t i = 0;
    size_t j = 0;

    if (out == NULL) {
        return NULL;
    }

    while (i < len) {
        size_t remain = len - i;
        uint32_t a = data[i++];
        uint32_t b = remain > 1 ? data[i++] : 0;
        uint32_t c = remain > 2 ? data[i++] : 0;

        out[j++] = tab[a >> 2];
        out[j++] = tab[((a & 3) << 4) | (b >> 4)];
        out[j++] = remain > 1 ? tab[((b & 15) << 2) | (c >> 6)] : '=';
        out[j++] = remain > 2 ? tab[c & 63] : '=';
    }

    out[j] = '\0';
    return out;
}

static char *read_file_base64(const char *path)
{
    FILE *fp = fopen(path, "rb");
    long size;
    unsigned char *buf;
    char *encoded;

    if (fp == NULL) {
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    size = ftell(fp);
    if (size <= 0) {
        fclose(fp);
        return NULL;
    }
    rewind(fp);

    buf = (unsigned char *)malloc((size_t)size);
    if (buf == NULL || fread(buf, 1, (size_t)size, fp) != (size_t)size) {
        free(buf);
        fclose(fp);
        return NULL;
    }
    fclose(fp);

    encoded = base64_encode(buf, (size_t)size);
    free(buf);
    return encoded;
}

static int send_image(bole_session *session,
                      const char *path,
                      const char *mime,
                      const char *prompt)
{
    char *b64 = read_file_base64(path);
    size_t cap;
    char *json;
    int n;
    int rc;

    if (b64 == NULL) {
        return -1;
    }

    cap = strlen(prompt) * 6 + strlen(b64) + 256;
    json = (char *)malloc(cap);
    if (json == NULL) {
        free(b64);
        return -1;
    }

    n = snprintf(json, cap,
                 "[{\"type\":\"text\",\"text\":\"%s\"},"
                 "{\"type\":\"image\",\"data\":\"%s\",\"mimeType\":\"%s\"}]",
                 prompt, b64, mime);
    free(b64);

    if (n < 0 || (size_t)n >= cap) {
        free(json);
        return -1;
    }

    rc = bole_submit_blocks(session, json);
    free(json);
    return rc;
}
```

调用：

```c
int rc = send_image(session,
                    "D:\\work\\screen.png",
                    "image/png",
                    "请描述这张图片。");
if (rc != 0) {
    /* 1002：JSON/参数错误；2003/3002：会话或运行时不可用 */
    fprintf(stderr, "send_image failed: %d\n", rc);
}
```

说明：

- 上面只演示单张 `image/png`；JPEG/WebP/GIF 改 `mime` 即可；
- `prompt` 里如果有双引号或换行，需要先做 JSON 转义；
- `bole_submit_blocks` 返回 0 只表示消息已入队，最终结果仍从事件回调获取；
- 图片是否能被模型理解，取决于模型和网关配置。

## 4. 发送文件

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

## 5. SDK 不处理的内容

- 不接收图片 URL 作为附件；
- 不接收 PDF/Office/音视频原文件；
- 不返回工具产生的图片/文件二进制，只返回文本摘要和 `[image]` / `[file]` 占位；
- 不返回模型生成的图片/文件；
- 每次调用不能单独传 API key，key 在运行时 `home\.env` 配置。

## 6. 数据路径

```text
调用方程序
  → bole_submit / bole_submit_blocks
  → SDK runtime（匿名管道，JSON-RPC，UTF-8）
  → 模型网关
```

图片以 base64 进入运行时附件存储；文字和图片之外的数据不会自动上传。
