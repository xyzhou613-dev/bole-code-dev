<#
  Assemble the offline package described in docs/bole-sdk/07-offline-package.md.
  Run on a Windows build machine that already has the npm runtime tree.
  No network access is used here.
#>
param(
  [Parameter(Mandatory = $true)][string]$RuntimeDir,   # node.exe + node_modules/@deepseek-ai/dsh
  [Parameter(Mandatory = $true)][string]$NativeBuild,  # dir containing bole_agent.dll/.lib/.h/.pdb
  [string]$OutDir = "dist\bole-agent-sdk"
)

$ErrorActionPreference = "Stop"
$root = Join-Path $OutDir "bole-agent-sdk-win-x64"

if (Test-Path $OutDir) { Remove-Item -Recurse -Force $OutDir }
New-Item -ItemType Directory -Force -Path `
  "$root\bin", "$root\runtime\config", "$root\home", "$root\tools\toy-mcp",
  "$root\samples\console-c", "$root\samples\gui-win32", "$root\docs" | Out-Null

# 1. C SDK binaries and header.
Copy-Item "$NativeBuild\bole_agent.dll", "$NativeBuild\bole_agent.lib", "$NativeBuild\bole_agent.h" $root\bin
if (Test-Path "$NativeBuild\bole_agent.pdb") { Copy-Item "$NativeBuild\bole_agent.pdb" $root\bin }

# 2. Runtime tree as installed (never rebuilt on the target machine).
Copy-Item "$RuntimeDir\*" "$root\runtime" -Recurse
Copy-Item "native\include\bole_agent.h" $root\bin -Force
Copy-Item "runtime\packages\sdk-app\cordis.patch.yml" "$root\runtime\cordis.patch.yml" -Force
Copy-Item "runtime\config\*" "$root\runtime\config" -Recurse -ErrorAction SilentlyContinue

# 3. Toy MCP and samples (sources, so the package is an integration reference).
Copy-Item "examples\toy-mcp\*" "$root\tools\toy-mcp" -Recurse
Copy-Item "examples\console-c\*" "$root\samples\console-c" -Recurse
Copy-Item "examples\gui-win32\*" "$root\samples\gui-win32" -Recurse

# 4. Delivery documents (A/B keep them under docs/delivery).
Copy-Item "docs\delivery\*" $root\docs -Recurse -ErrorAction SilentlyContinue
Copy-Item "packaging\install.ps1" $root

# 5. Version manifest and checksums.
$runtimeVersion = (Get-Content "$RuntimeDir\node_modules\@deepseek-ai\dsh\package.json" -Raw | ConvertFrom-Json).version
$version = [ordered]@{
  sdk       = "0.1.0"
  abi       = "1.0.0"
  protocol  = 1
  runtime   = $runtimeVersion
  node      = (& "$root\runtime\node.exe" --version)
  presets   = @("standard", "rsi-infer")
  experience = @{}
  tools     = @{ "toy-mcp" = "0.1.0" }
  builtAt   = (Get-Date).ToUniversalTime().ToString("o")
}
$version | ConvertTo-Json | Set-Content "$root\VERSION.json" -Encoding UTF8

Get-ChildItem $root -Recurse -File |
  Where-Object { $_.Name -ne "SHA256SUMS" } |
  ForEach-Object {
    $rel = $_.FullName.Substring($root.Length + 1)
    $hash = (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower()
    "$hash  $rel"
  } | Set-Content "$root\SHA256SUMS" -Encoding ASCII

Compress-Archive -Path $root -DestinationPath "$OutDir.zip" -Force
Write-Host "built $OutDir.zip"
