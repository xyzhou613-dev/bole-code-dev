<#
  Offline install: verify checksums, then copy next to the target directory.
  No registry writes, no administrator rights, no network access.

  Usage:
    .\install.ps1
    .\install.ps1 -Package D:\bole-agent-sdk -Target D:\apps\BoleAgentSdk
#>
param(
  [string]$Package = $PSScriptRoot,
  [string]$Target = "$env:LOCALAPPDATA\BoleAgentSdk"
)

$ErrorActionPreference = "Stop"
$Package = (Resolve-Path $Package).Path
$sums = Join-Path $Package "SHA256SUMS"
if (!(Test-Path $sums)) { throw "SHA256SUMS missing: $Package" }

$failed = 0
Get-Content $sums | ForEach-Object {
  if ($_ -match '^([0-9a-f]{64})\s+(.+)$') {
    $expected, $rel = $Matches[1], $Matches[2]
    $file = Join-Path $Package $rel
    if (!(Test-Path $file)) { Write-Error "missing $rel"; $failed++; return }
    $actual = (Get-FileHash $file -Algorithm SHA256).Hash.ToLower()
    if ($actual -ne $expected) { Write-Error "hash mismatch $rel"; $failed++ }
  }
}
if ($failed -gt 0) { throw "$failed file(s) failed verification" }

New-Item -ItemType Directory -Force -Path $Target | Out-Null
# Preserve an existing home/ (sessions, logs, cache); only seed it on first install.
Copy-Item "$Package\*" $Target -Recurse -Force -Exclude "home"
if (!(Test-Path (Join-Path $Target "home"))) {
  Copy-Item "$Package\home" (Join-Path $Target "home") -Recurse -Force
}
Write-Host "installed to $Target"
Write-Host "next: $Target\samples\console-c\bole_console <runtime> <home> `"hello`""
