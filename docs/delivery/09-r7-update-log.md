# r7 更新说明

版本：r7 final  
适用：`bole-offline-r7-final.zip`

## 更新内容

- 示例不再硬编码模型，`model_name` 可留空，SDK 自动读取 `runtime/cordis.patch.yml` 的 `agent-default-model`。
- 换模型只需改配置，不需要重新编译示例。
- GUI 新任务只显示当前任务日志，不再残留旧任务内容。
- 日志缓冲满时保留最新内容，不再停止更新。
- 新增可读 `.log` 和完整原始事件 `.jsonl`。
- 新增 **Open Log** 按钮。
- RSI 状态和 RSI 会话修复。

## 更新步骤

1. 关闭 GUI 和 console。
2. 备份 `<SDK_HOME>\home`（需要保留会话、日志、自定义 MCP 配置时）。
3. 用 r7 包重新安装或覆盖安装到原目录：

```powershell
.\install.ps1 `
  -Target D:\apps\BoleAgentSdk `
  -ModelGateway <新网关>
```

4. 启动 GUI 或 console，确认能正常回答。

【截图位置 1】安装完成后的目录，或 `installed to ...` 输出。

## 切换到 125b

编辑：

```text
<SDK_HOME>\runtime\cordis.patch.yml
```

修改三处：

```yaml
- id: agent-default-model
  config:
    provider: bole
    model: <125b-model-id>
```

```yaml
baseURL: 'http://<125b-gateway>:<port>/v1'
```

```yaml
models:
  - id: <125b-model-id>
```

如果新网关需要 key，修改：

```text
<SDK_HOME>\home\.env
BOLE_API_KEY=<新 key>
```

改完重启 GUI/console。

【截图位置 2】`runtime\cordis.patch.yml` 中 `model`、`baseURL`、`models[].id` 三处。

## 验证

```powershell
cd <SDK_HOME>\samples\console-c

.\bole_console.exe "你好"
```

确认输出：

```text
[ready]
[answer] ...
[task] finished
```

【截图位置 3】console 的 `[ready]`、`[answer]`、`[task] finished`。

GUI 连续跑两个任务，确认第二个任务不显示旧日志、没有原始 JSON。

【截图位置 4】GUI 第二个任务的结果区。

## 包信息

```text
bole-offline-r7-final.zip
SHA256: 7aa149b40fdf185890ec5652bc2ba144fb6ef970c9e7a2e372f10024ff692fe2
tag: bole-m0-20261109-r7
```
