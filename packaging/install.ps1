<#
  Offline install: verify checksums, copy next to the target directory, then
  render the site home layer. No registry writes, no administrator rights, no
  network access.

  Usage:
    .\install.ps1
    .\install.ps1 -Package D:\bole-agent-sdk -Target D:\apps\BoleAgentSdk
    .\install.ps1 -Target D:\apps\BoleAgentSdk -ModelGateway http://10.0.0.5:9001
#>
param(
  [string]$Package = $PSScriptRoot,
  [string]$Target = "$env:LOCALAPPDATA\BoleAgentSdk",
  [string]$ModelGateway = ""
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

function Write-Utf8NoBom([string]$Path, [string]$Text) {
  [System.IO.File]::WriteAllText($Path, $Text, (New-Object System.Text.UTF8Encoding($false)))
}

New-Item -ItemType Directory -Force -Path $Target | Out-Null
$targetHome = Join-Path $Target "home"

# Preserve an existing home/ (sessions, logs, cache); only seed it on first
# install. Custom packages live under home/profiles/sdk/node_modules (F9).
if (!(Test-Path $targetHome)) {
  Copy-Item (Join-Path $Package "home") $targetHome -Recurse -Force
} elseif (Test-Path (Join-Path $Package "home\profiles\sdk\node_modules")) {
  $destination = Join-Path $targetHome "profiles\sdk\node_modules"
  New-Item -ItemType Directory -Force -Path $destination | Out-Null
  Copy-Item (Join-Path $Package "home\profiles\sdk\node_modules\*") $destination -Recurse -Force
}

Get-ChildItem $Package | Where-Object { $_.Name -ne "home" } |
  ForEach-Object { Copy-Item $_.FullName $Target -Recurse -Force }

# Render the site layer: absolute paths differ after moving the package.
$templatePath = Join-Path $targetHome "cordis.patch.yml.template"
if (Test-Path $templatePath) {
  $yamlTarget = $Target.Replace('\', '/')
  $text = Get-Content $templatePath -Raw
  $text = $text.Replace('<runtime-dir>', "$yamlTarget/runtime").Replace('<package-root>', $yamlTarget)
  Write-Utf8NoBom (Join-Path $targetHome "cordis.patch.yml") $text
} else {
  Write-Warning "home\cordis.patch.yml.template missing; create home\cordis.patch.yml manually"
}

# The model gateway is a --patch layer above home; update the runtime patch.
if ($ModelGateway -ne "") {
  $runtimePatch = Join-Path $Target "runtime\cordis.patch.yml"
  if (Test-Path $runtimePatch) {
    $text = Get-Content $runtimePatch -Raw
    $text = [regex]::Replace($text, "baseURL:\s*\S+", "baseURL: '$($ModelGateway.TrimEnd('/'))/v1'")
    Write-Utf8NoBom $runtimePatch $text
  }
}

$envPath = Join-Path $targetHome ".env"
if (!(Test-Path $envPath)) {
  $envTemplate = Join-Path $Package "home\.env"
  if (Test-Path $envTemplate) { Copy-Item $envTemplate $envPath -Force }
  else { Write-Utf8NoBom $envPath "BOLE_API_KEY=local`n" }
}

Write-Host "installed to $Target"
Write-Host "next: $Target\samples\console-c\bole_console $Target\runtime $targetHome `"hello`""
