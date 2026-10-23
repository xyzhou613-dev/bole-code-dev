# 11 离线包构建与验收 Runbook（测试团队）

本文给构建/测试同学使用。所有路径用占位符，按自己的环境替换；不要把本机绝对路径提交回仓库。

## 一、构建机前置

- Windows 10/11 x64 构建机；
- Visual Studio 2022 Build Tools，含 “使用 C++ 的桌面开发” / x64 MSVC；
- CMake ≥ 3.20、Ninja（VS 自带或独立安装）；
- Windows Node 24.x（用于安装运行时、构建插件、跑自检）；
- PowerShell 5.1+；
- 仓库源码：`<repo>`，分支 `develop`；
- 一个锁在 `0.1.7-alpha.2` 的运行时树：`<runtime-dir>`；
- A 侧插件/经验包 tarball：默认在 `<repo>\runtime\dist\`。

## 二、准备运行时树

```powershell
New-Item -ItemType Directory -Force <runtime-dir>
npm --prefix <runtime-dir> init -y
npm --prefix <runtime-dir> install @deepseek-ai/dsh@0.1.7-alpha.2 --no-audit --no-fund
Copy-Item "<node-dir>\node.exe" "<runtime-dir>\node.exe" -Force
```

验收构建脚本要求：

```text
<runtime-dir>\node.exe
<runtime-dir>\node_modules\@deepseek-ai\dsh\lib\bin.js
```

## 三、构建插件与经验包 tarball

```powershell
cd <repo>
npm --prefix runtime/dev install
node runtime/dev/build-experience.mjs
node runtime/dev/pack.mjs
node runtime/dev/pack-test.mjs
```

确认 `<repo>\runtime\dist\` 下有：

```text
bole-sdk-server-0.1.0.tgz
deepseek-ai-dsh-skill-bole-experience-0.1.7-alpha.2.tgz
```

`pack-test.mjs` 必须通过；它会用裸包名在 profile 中启动，验证 F9。

## 四、MSVC 构建 C SDK

在 “x64 Native Tools Command Prompt for VS 2022” 中：

```bat
call "<vs-install>\VC\Auxiliary\Build\vcvars64.bat"

cmake -S <repo>\native -B <build>\native -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DBOLE_NODE_EXECUTABLE=<node-dir>\node.exe
cmake --build <build>\native
ctest --test-dir <build>\native --output-on-failure
```

期望：

```text
100% tests passed, 0 tests failed out of 4
（json / error / platform / fixtures）
```

产物：

```text
<build>\native\bole_agent.dll
<build>\native\bole_agent.lib
<build>\native\bole_agent.h 由打包脚本从 native/include 复制
```

检查：

```bat
dumpbin /dependents <build>\native\bole_agent.dll
```

期望只有 `KERNEL32.dll`（`/MT`，无 VCRUNTIME/MSVCP）。

## 五、构建示例

```bat
cmake -S <repo>\examples\console-c -B <build>\console -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DBOLE_NODE_EXECUTABLE=<node-dir>\node.exe
cmake --build <build>\console

cmake -S <repo>\examples\gui-win32 -B <build>\gui -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DBOLE_NODE_EXECUTABLE=<node-dir>\node.exe
cmake --build <build>\gui
```

产物：

```text
<build>\console\bole_console.exe
<build>\console\bole_selfcheck.exe
<build>\gui\bole_gui.exe
```

`build-offline.ps1` 会从 console 同目录自动复制 `bole_selfcheck.exe` 到包内。

## 六、打离线包

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File <repo>\packaging\build-offline.ps1 `
  -RuntimeDir <runtime-dir> `
  -NativeBuild <build>\native `
  -OutDir <out> `
  -ModelGateway <gateway> `
  -ConsoleExe <build>\console\bole_console.exe `
  -GuiExe <build>\gui\bole_gui.exe
```

说明：

- `<gateway>` 不带 `/v1`，例如 `http://<gateway-host>:<port>`；
- 默认取 `runtime\dist` 最新 tarball；需要指定时加 `-PluginTarball` / `-ExperienceTarball`；
- 产物：`<out>.zip` 和 `<out>\bole-agent-sdk-win-x64\`；
- 包内必须有 `install.ps1`、`verify.ps1`、`acceptance.ps1`、`bin\bole_agent.lib`、`home\profiles\sdk\node_modules`。

打包后先核对包内：

```powershell
Get-Content <out>\bole-agent-sdk-win-x64\VERSION.json
Select-String "baseURL" <out>\bole-agent-sdk-win-x64\runtime\cordis.patch.yml
Select-String "enable_thinking" <out>\bole-agent-sdk-win-x64\runtime\cordis.patch.yml
```

期望 `baseURL` 是真实网关；qwen 条目带 `reasoningEfforts` 和
`chatTemplateKwargs.enable_thinking: false`（F13）。

## 七、生成哈希并交给测试

```powershell
Get-FileHash <out>.zip -Algorithm SHA256
```

记录：

```text
包路径、SHA256、VERSION.json、git commit、构建时间、构建人
```

不要把 zip 解压后再修改；修改任何一个文件都必须重打 zip 并重算哈希。

## 八、干净目标 VM 验收（测试执行）

目标 VM 要求：

```text
Windows 10/11 x64
无 Node、无 VS、无 VC++ 运行库、无旧 SDK
普通用户
只能访问内网网关，不能访问公网
```

把 zip 拷进 VM，解压后运行：

```powershell
cd <extracted-package>
.\acceptance.ps1 `
  -Package . `
  -Target <target> `
  -ModelGateway <gateway> `
  -Prompt "调用玩具工具 mcp__toy__add 计算 1+2，然后用结果回答。" `
  -ZipPath <zip>
```

干净 VM **不要**加 `-SkipCleanEnvironmentCheck`；该开关只给已有 Node/MSVC 的开发机复测。

脚本会：

1. 检查环境（node/cl 不在 PATH）；
2. 检查网关 TCP；
3. install → verify；
4. console 真实模型工具调用；
5. `bole_selfcheck`；
6. GUI 启动/关闭；
7. SDK 进程残留检查；
8. 把日志、trace、VERSION、patch 收进 `<OutDir>`。

成功输出：

```text
ACCEPTANCE PASS
```

## 九、人工补测

`acceptance.ps1` 不能替代：

- GUI **长任务期间交互 + Cancel** 的录屏/截图；
- Windows 10 目标机（若客户包含）；
- 真实 Ornith 网关（如现场使用）；
- 未签名 DLL 在目标 EDR/杀软策略下的表现；
- 伙伴按四份 delivery 文档从零独立集成。

## 十、常见坑

| 现象 | 处理 |
| --- | --- |
| `failed to import @bole/sdk-server` | profile 未装插件；检查 F9，不要只放 runtime/node_modules |
| `No API key for provider: bole` | `home\.env` 的 `BOLE_API_KEY`；网关不需要 key 时保留 local |
| 模型 404 / model not found | `-ModelGateway` 不带 `/v1`；核对模型 ID |
| qwen 仍返回 reasoning | 检查 F13 的 `reasoningEfforts` + `enable_thinking:false` |
| 安装脚本 hash mismatch | 包内文件被改过；重打 zip，不要手改包 |
| acceptance 说 node/CL 在 PATH | 不是干净 VM；换干净快照，或仅在开发机用 `-SkipCleanEnvironmentCheck` |
| console 第二次 `create failed` | 旧包可能固定 session id；当前版本是 `console-<pid>`，重打新包 |
| GUI 第二次 `submit failed` | 旧包；当前 GUI 每进程唯一 session，多轮复用 |
| toy MCP 工具少 | 当前应为 12 个；旧包可能只有 3 个，重打 |

## 十一、发布动作（测试通过后）

1. 保存 `<out>.zip` 哈希、acceptance 日志、GUI 录屏、VERSION；
2. 在干净提交上打 tag（不要直接拿移动的 develop 当发布点）：

   ```powershell
   git tag -a bole-m0-<date> -m "M0: ..."
   git push origin bole-m0-<date>
   ```

3. 记录测试人/时间/OS/网关/模型 ID；
4. 未签名说明与 EDR 例外记录随包交付。
