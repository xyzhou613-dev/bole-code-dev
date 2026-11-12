# 远程排查清单（MATLAB / MCP / FNX）

远程工具：向日葵，客户给识别码和验证码。  
连上后先只读检查，不要先改配置。

## 0. 先确认

```text
1. 远程的是 MATLAB 那台 Windows，不是 192.168.18.206 模型服务器
2. MATLAB 已打开 FPGA_Dev_Comp
3. MATLAB License 正常
4. 客户允许读取日志和配置
```

## 1. 基础版本

```cmd
cd /d <SDK_HOME>

type VERSION.json

powershell -NoProfile -ExecutionPolicy Bypass -File verify.ps1 -Root <SDK_HOME>

dir bin\bole_agent.dll
dir samples\console-c\bole_console.exe
dir samples\gui-win32\bole_gui.exe
```

`verify.ps1` 要输出 `verify ok`。

## 2. 看模型配置

```cmd
findstr /N "baseURL model:" runtime\cordis.patch.yml
```

确认：

```text
baseURL 指向客户实际网关
model 和 models[].id 一致
125b 的 reasoningEfforts 只声明 low/medium/xhigh（high 映射 xhigh），并设置 supportsDeveloperRole: false
```

## 3. 看 MATLAB MCP

```cmd
dir tools\matlab-mcp
findstr /N "mcp-matlab matlab-mcp MATLAB_ROOT TOOLKIT_DIR" home\cordis.patch.yml
dir home\logs\matlab-mcp
```

确认：

```text
matlab-mcp-server-windows-x64.exe 存在
home\cordis.patch.yml 有 mcp-matlab
MATLAB_ROOT / TOOLKIT_DIR 路径正确
```

看 MCP 日志：

```cmd
powershell -NoProfile -Command "Get-Content home\logs\matlab-mcp\* -Tail 50"
```

## 4. 看 MATLAB 是否在跑

```cmd
tasklist | findstr /I "MATLAB matlab-mcp node"
```

如果 MATLAB 没启动，先启动 MATLAB 和模型。

## 5. 直接测 MATLAB MCP

```cmd
cd /d <SDK_HOME>\samples\console-c

bole_console.exe "调用 mcp__matlab__evaluate_matlab_code 执行 disp(1+1)，然后只回答结果。"
```

期望：

```text
[tool] start mcp__matlab__evaluate_matlab_code
[tool] done ...
[answer] ... 2
[task] finished
```

如果没有 `[tool] start`：

```text
模型没产生结构化 tool_calls
或 MCP 没注册
```

## 6. 测 FNX 工具参数

```cmd
bole_console.exe "调用 mcp__toy__add 计算 20+22，然后只回答结果。"
```

看日志里的参数：

```text
args {"a":"20","b":"22"}  -> FNX 类型丢失
args {"a":20,"b":22}      -> 正常
```

如果报：

```text
add requires numeric a and b
```

就是 FNX 把数字转成了字符串。

## 7. 跑 GUI 任务并看日志

```cmd
dir /o-d /b home\logs\gui-*
findstr /I "MODEL_SELECTED TOOL_CALL TASK_FAILED STEP_STARTED" home\logs\gui-*.jsonl
```

重点看：

```text
MODEL_SELECTED 的 model 对不对
有没有 TOOL_CALL
TOOL_CALL 的 arguments 是数字还是字符串
TASK_FAILED 的 code/message
```

## 8. 直接测网关

```cmd
curl http://<网关>:<端口>/v1/models
```

再用 `body.json` 测 chat：

```json
{"model":"<模型ID>","messages":[{"role":"user","content":"Reply OK"}],"max_tokens":32}
```

```cmd
curl -s -i -H "Content-Type: application/json" -d @body.json http://<网关>:<端口>/v1/chat/completions
```

看：

```text
HTTP 200/500
finish_reason
message.tool_calls
```

## 9. 判断表

| 现象 | 判断 | 下一步 |
| --- | --- | --- |
| MCP 目录或配置缺失 | MCP 未安装 | 向客户要 MCP + tools.json |
| MATLAB 没进程 | MATLAB 未启动/无 license | 启动 MATLAB，查 license |
| 没有 `[tool] start` | 模型没给结构化 tool_calls | 查 FNX parser |
| `args` 是字符串 | FNX 类型丢失 | FNX 修 parser |
| `finish_reason=length` | 输出被截断 | 提高 max tokens 或 FNX 修 |
| HTTP 500 | 网关/模型错误 | 查 FNX 日志 |
| MATLAB 报缺库 | IPLib/VMC 缺失 | 向客户要波形库 |

## 10. 收证据

```cmd
mkdir D:\bole-remote-evidence

copy VERSION.json D:\bole-remote-evidence\
copy runtime\cordis.patch.yml D:\bole-remote-evidence\
copy home\cordis.patch.yml D:\bole-remote-evidence\
copy samples\console-c\console.log D:\bole-remote-evidence\ 2>nul
copy home\logs\gui-*.jsonl D:\bole-remote-evidence\ 2>nul
copy home\logs\matlab-mcp\* D:\bole-remote-evidence\ 2>nul
```

不要复制 `home\.env` 里的 key；需要时只记录“已配置”。

## 结论

```text
先确认 MATLAB/MCP 能跑
再确认模型产生 tool_calls
再看参数类型
最后才看 MATLAB 任务本身
```
