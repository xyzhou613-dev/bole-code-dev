<#
  Clean-VM acceptance for the Bole Agent SDK offline package.

  Run inside the target Windows VM, from the extracted package (or pass -Package).
  It installs into a fresh target, verifies the composition, runs a real C2 and
  the B8 self-check, optionally starts the GUI, and writes evidence under -OutDir.

  Usage:
    .\acceptance.ps1 -Package . -Target D:\apps\BoleAgentSdk `
      -ModelGateway http://<gateway>:<port> `
      -Prompt "Call mcp__toy__add with 1+2 and answer with the result."
#>
param(
  [Parameter(Mandatory = $true)][string]$Package,
  [Parameter(Mandatory = $true)][string]$Target,
  [Parameter(Mandatory = $true)][string]$ModelGateway,
  [string]$Prompt = "Call mcp__toy__add with 1+2 and answer with the result.",
  [string]$ApiKey = "",
  [string]$ZipPath = "",
  [string]$OutDir = "",
  [bool]$CheckGui = $true,
  [bool]$RequireOffline = $false,
  [switch]$SkipCleanEnvironmentCheck
)

$ErrorActionPreference = "Stop"
$script:StartedAt = Get-Date
$script:StepLogs = @{}

if ($OutDir -eq "") {
  $OutDir = Join-Path (Split-Path $Target -Parent) ("bole-acceptance-" + (Get-Date -Format "yyyyMMdd-HHmmss"))
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
Start-Transcript -Path (Join-Path $OutDir "transcript.log") -Force | Out-Null

function Write-Step([string]$Name) {
  Write-Host ""
  Write-Host "=== $Name ==="
}

function Test-Tcp([string]$HostName, [int]$Port, [int]$TimeoutMs = 3000) {
  $client = New-Object System.Net.Sockets.TcpClient
  try {
    $async = $client.BeginConnect($HostName, $Port, $null, $null)
    if (-not $async.AsyncWaitHandle.WaitOne($TimeoutMs)) { return $false }
    $client.EndConnect($async)
    return $true
  } catch {
    return $false
  } finally {
    $client.Close()
  }
}

function Invoke-LoggedStep {
  param([string]$Name, [scriptblock]$Action)
  $log = Join-Path $OutDir "$Name.log"
  $script:StepLogs[$Name] = $log
  $global:LASTEXITCODE = 0
  & $Action 2>&1 | Tee-Object -FilePath $log
  if (-not $?) { throw "$Name failed" }
  if ($LASTEXITCODE -ne 0) { throw "$Name exited with code $LASTEXITCODE" }
  return $log
}

function Assert-Contains([string]$Path, [string[]]$Needles) {
  $text = Get-Content $Path -Raw
  foreach ($needle in $Needles) {
    if ($text -notmatch [regex]::Escape($needle)) {
      throw "expected '$needle' in $Path"
    }
  }
}

$exitCode = 1
try {
  $Package = (Resolve-Path $Package).Path
  if (Test-Path $Target) { throw "target already exists; use a fresh target: $Target" }

  $install = Join-Path $Package "install.ps1"
  $verify = Join-Path $Package "verify.ps1"
  foreach ($required in @($install, $verify)) {
    if (!(Test-Path $required)) { throw "missing package file: $required" }
  }

  Write-Step "environment"
  $envLog = Join-Path $OutDir "environment.log"
  $script:StepLogs["environment"] = $envLog
  $os = Get-CimInstance Win32_OperatingSystem
  $cs = Get-CimInstance Win32_ComputerSystem
  $node = Get-Command node -ErrorAction SilentlyContinue
  $cl = Get-Command cl -ErrorAction SilentlyContinue
  @(
    "computer=$($env:COMPUTERNAME)",
    "user=$($env:USERNAME)",
    "admin=$((New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator))",
    "os=$($os.Caption) $($os.Version)",
    "ram_gb=$([math]::Round($cs.TotalPhysicalMemory / 1GB, 1))",
    "node=$($(if ($node) { $node.Source } else { 'not found' }))",
    "msvc_cl=$($(if ($cl) { $cl.Source } else { 'not found' }))",
    "gateway=$ModelGateway",
    "package=$Package",
    "target=$Target"
  ) | Tee-Object -FilePath $envLog

  if (-not $SkipCleanEnvironmentCheck) {
    if ($node -ne $null) { throw "node is on PATH; this is not a clean target VM (use -SkipCleanEnvironmentCheck only on a dev host)" }
    if ($cl -ne $null) { throw "MSVC cl is on PATH; this is not a clean target VM" }
  } else {
    "clean-environment check skipped by -SkipCleanEnvironmentCheck" | Tee-Object -FilePath $envLog -Append
  }

  Write-Step "gateway"
  $uri = [uri]$ModelGateway
  if ($uri.Scheme -notin @("http", "https")) { throw "ModelGateway must be http(s): $ModelGateway" }
  $gatewayPort = if ($uri.IsDefaultPort) { if ($uri.Scheme -eq "https") { 443 } else { 80 } } else { $uri.Port }
  if (-not (Test-Tcp $uri.Host $gatewayPort)) { throw "gateway is not reachable: $($uri.Host):$gatewayPort" }
  "gateway TCP $($uri.Host):$gatewayPort OK" | Tee-Object -FilePath (Join-Path $OutDir "gateway.log")
  $internet = Test-Tcp "8.8.8.8" 443 2000
  "internet_443=$internet" | Tee-Object -FilePath (Join-Path $OutDir "gateway.log") -Append
  if ($RequireOffline -and $internet) { throw "public internet is reachable; RequireOffline was set" }

  Write-Step "install"
  Invoke-LoggedStep "install" { & $install -Package $Package -Target $Target -ModelGateway $ModelGateway } | Out-Null
  if ($ApiKey -ne "") {
    [System.IO.File]::WriteAllText((Join-Path $Target "home\.env"), "BOLE_API_KEY=$ApiKey`n", (New-Object System.Text.UTF8Encoding($false)))
  }

  Write-Step "verify"
  Invoke-LoggedStep "verify" { & $verify -Root $Target } | Out-Null

  $runtimePatch = Join-Path $Target "runtime\cordis.patch.yml"
  $homePatch = Join-Path $Target "home\cordis.patch.yml"
  Assert-Contains $runtimePatch @($ModelGateway)
  Assert-Contains $homePatch @("mcp-toy")

  Write-Step "console C2"
  $console = Join-Path $Target "samples\console-c\bole_console.exe"
  Invoke-LoggedStep "console" { & $console $Prompt } | Out-Null
  Assert-Contains $script:StepLogs["console"] @("mcp__toy__add", '"isError":false', "[task] finished")

  Write-Step "B8 selfcheck"
  $selfcheck = Join-Path $Target "samples\console-c\bole_selfcheck.exe"
  Invoke-LoggedStep "selfcheck" { & $selfcheck $Prompt } | Out-Null
  Assert-Contains $script:StepLogs["selfcheck"] @("PASS selfcheck")
  if ((Get-Content $script:StepLogs["selfcheck"] -Raw) -match "FAIL ") {
    throw "selfcheck reported a failure"
  }

  if ($CheckGui) {
    Write-Step "GUI startup"
    $gui = Join-Path $Target "samples\gui-win32\bole_gui.exe"
    $guiLog = Join-Path $OutDir "gui.log"
    $script:StepLogs["gui"] = $guiLog
    $proc = Start-Process -FilePath $gui -WorkingDirectory (Split-Path $gui) -PassThru
    $window = [IntPtr]::Zero
    for ($i = 0; $i -lt 100 -and $window -eq [IntPtr]::Zero; $i++) {
      Start-Sleep -Milliseconds 100
      $proc.Refresh()
      if ($proc.HasExited) { throw "GUI exited during startup: $($proc.ExitCode)" }
      $window = $proc.MainWindowHandle
    }
    if ($window -eq [IntPtr]::Zero) { throw "GUI window did not appear" }
    "GUI window appeared (PID $($proc.Id))" | Tee-Object -FilePath $guiLog
    [void]$proc.CloseMainWindow()
    if (-not $proc.WaitForExit(5000)) { $proc.Kill(); throw "GUI did not close on WM_CLOSE" }
  }

  Write-Step "cleanup"
  Start-Sleep -Seconds 2
  function Get-SdkProcesses {
    Get-CimInstance Win32_Process -ErrorAction SilentlyContinue | Where-Object {
      $_.Name -in @("node.exe", "bole_console.exe", "bole_selfcheck.exe", "bole_gui.exe") -and
      $_.CommandLine -ne $null -and $_.CommandLine -like "*$Target*"
    }
  }
  $left = Get-SdkProcesses
  if ($left -ne $null) {
    Start-Sleep -Seconds 5
    $left = Get-SdkProcesses
    if ($left -ne $null) {
      $left | Select-Object ProcessId, Name, CommandLine |
        Tee-Object -FilePath (Join-Path $OutDir "leaked-processes.log")
      throw "runtime/tool processes are still running under $Target"
    }
  }
  "no leaked SDK processes under $Target" | Tee-Object -FilePath (Join-Path $OutDir "cleanup.log")

  Write-Step "evidence"
  Copy-Item (Join-Path $Target "VERSION.json") $OutDir -Force
  Copy-Item $runtimePatch $OutDir -Force
  Copy-Item $homePatch $OutDir -Force
  $trace = Join-Path $Target "home\selfcheck-trace.jsonl"
  if (Test-Path $trace) { Copy-Item $trace $OutDir -Force }
  if ($ZipPath -ne "") {
    if (!(Test-Path $ZipPath)) { throw "zip not found: $ZipPath" }
    (Get-FileHash $ZipPath -Algorithm SHA256).Hash | Set-Content (Join-Path $OutDir "zip.sha256") -Encoding ASCII
  }

  $summary = [ordered]@{
    result = "PASS"
    startedAt = $script:StartedAt.ToString("o")
    finishedAt = (Get-Date).ToString("o")
    gateway = $ModelGateway
    package = $Package
    target = $Target
    internetReachable = $internet
  }
  ($summary | ConvertTo-Json) | Set-Content (Join-Path $OutDir "summary.json") -Encoding UTF8
  Write-Host "ACCEPTANCE PASS"
  Write-Host "evidence: $OutDir"
  $exitCode = 0
} catch {
  Write-Host "ACCEPTANCE FAIL: $($_.Exception.Message)"
  $summary = [ordered]@{
    result = "FAIL"
    startedAt = $script:StartedAt.ToString("o")
    finishedAt = (Get-Date).ToString("o")
    error = $_.Exception.Message
    gateway = $ModelGateway
    target = $Target
  }
  ($summary | ConvertTo-Json) | Set-Content (Join-Path $OutDir "summary.json") -Encoding UTF8
} finally {
  try { Stop-Transcript | Out-Null } catch {}
}
exit $exitCode
