<#
  Clean-machine self-check: binaries present, runtime version pinned, no network.
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
  "VERSION.json",
  "SHA256SUMS"
)
foreach ($rel in $expect) {
  if (!(Test-Path (Join-Path $Root $rel))) { throw "missing $rel" }
  Write-Host "ok $rel"
}
$version = Get-Content (Join-Path $Root "VERSION.json") -Raw | ConvertFrom-Json
if ($version.dshLocked -ne "0.1.7-alpha.2") { throw "unexpected dsh lock: $($version.dshLocked)" }
Write-Host "verify ok"
