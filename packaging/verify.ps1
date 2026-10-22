<#
  Clean-machine self-check: binaries and profile packages present, runtime
  version pinned, composed configuration contains the SDK plugin and presets.
  Run after install.ps1; exits non-zero on the first missing expectation.
#>
param([Parameter(Mandatory = $true)][string]$Root)

$ErrorActionPreference = "Stop"
$expect = @(
  "bin\bole_agent.dll",
  "bin\bole_agent.lib",
  "bin\bole_agent.h",
  "runtime\node.exe",
  "runtime\node_modules\@deepseek-ai\dsh\package.json",
  "runtime\cordis.patch.yml",
  "home\.env",
  "home\cordis.patch.yml",
  "home\profiles\sdk\node_modules\@bole\sdk-server\lib\index.js",
  "home\profiles\sdk\node_modules\@deepseek-ai\dsh-skill-bole-experience\lib\index.js",
  "home\profiles\sdk\node_modules\@deepseek-ai\dsh-skill-bole-experience\assets\final-experiences.json",
  "VERSION.json",
  "SHA256SUMS"
)
foreach ($rel in $expect) {
  if (!(Test-Path (Join-Path $Root $rel))) { throw "missing $rel" }
  Write-Host "ok $rel"
}

$version = Get-Content (Join-Path $Root "VERSION.json") -Raw | ConvertFrom-Json
if ($version.runtime -ne "0.1.7-alpha.2") { throw "unexpected runtime version: $($version.runtime)" }
if ($version.protocol -ne 1) { throw "unexpected protocol: $($version.protocol)" }

$envText = Get-Content (Join-Path $Root "home\.env") -Raw
if ($envText -notmatch "BOLE_API_KEY=") { throw "home\.env has no BOLE_API_KEY" }

# Compose the profile tree without starting the app provider. This catches a
# missing plugin row, preset plane, or bad patch path before the first run.
$node = Join-Path $Root "runtime\node.exe"
$dsh = Join-Path $Root "runtime\node_modules\@deepseek-ai\dsh\lib\bin.js"
$patch = Join-Path $Root "runtime\cordis.patch.yml"
$previousHome = $env:DSH_HOME
try {
  $env:DSH_HOME = Join-Path $Root "home"
  $dump = & $node $dsh --profile sdk --patch $patch --dump-config 2>&1
  $code = $LASTEXITCODE
} finally {
  $env:DSH_HOME = $previousHome
}
$text = ($dump | Out-String)
if ($code -ne 0) { throw "--dump-config failed ($code):`n$text" }
foreach ($needle in @("bole-sdk-server", "preset-standard", "preset-rsi-infer", "agent-preset-registry")) {
  if ($text -notmatch [regex]::Escape($needle)) { throw "composed config has no $needle" }
}
Write-Host "verify ok"
