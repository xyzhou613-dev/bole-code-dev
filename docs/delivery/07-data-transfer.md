# SDK 数据发送示例

SDK 可以发送文字和 base64 图片；文件不能直接发送。

完整接口说明见同目录 `01-interface.md`：

- `6.8 bole_submit`
- `6.9 bole_submit_blocks`

头文件：`bin\bole_agent.h`。

## 1. 文字

```c
int rc = bole_submit(session, "你好");
if (rc != 0) {
    /* 1002 参数错误，2003/3002 会话或运行时不可用 */
}
```

返回值 `0` 表示消息已入队，最终回答从事件回调获取。

## 2. 图片

图片先由调用方转成 base64：

```c
const char* blocks =
  "[{\"type\":\"text\",\"text\":\"请描述这张图片。\"},"
  " {\"type\":\"image\",\"data\":\"<base64>\",\"mimeType\":\"image/png\"}]";

int rc = bole_submit_blocks(session, blocks);
if (rc != 0) {
    /* 1002 参数错误，2003/3002 会话或运行时不可用 */
}
```

说明：

- `<base64>` 换成真实图片的 base64，不是占位符；
- 支持 `image/png`、`image/jpeg`、`image/webp`、`image/gif`；
- 不支持图片 URL，只接受 base64；
- `bole_submit_blocks` 返回 `0` 只表示入队，最终结果仍从事件回调获取。

## 3. 文件

SDK 没有 `file` 输入块，不能直接发 PDF/Word/Excel/音视频。

两种处理方式：

1. 先把文件内容转成文本，再用 `bole_submit` 发送；
2. 把文件放到工作目录，在 prompt 里给路径，让模型用工具读取。

## 4. 接口文档

- 交付接口说明：`docs/01-interface.md`，看 6.8、6.9 节；
- C 头文件：`bin\bole_agent.h`；
- 内部冻结 C ABI：`docs/bole-sdk/04-c-abi.md`。
