<#
  Offline install: verify checksums, then copy next to the target directory.
  No registry writes, no administrator rights, no network access.
#>
param(
  [Parameter(Mandatory = $true)][string]$Package,  # extracted bole-agent-sdk-win-x64
  [string]$Target = "$env:LOCALAPPDATA\BoleAgentSdk"
)

$ErrorActionPreference = "Stop"
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

if (Test-Path $Target) { Remove-Item -Recurse -Force $Target }
New-Item -ItemType Directory -Force -Path $Target | Out-Null
Copy-Item "$Package\*" $Target -Recurse
Write-Host "installed to $Target"
