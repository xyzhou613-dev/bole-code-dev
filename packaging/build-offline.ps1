<#
  Assemble the offline package described in docs/bole-sdk/07-offline-package.md.

  Run on a Windows build machine that already has:
    - a 0.1.7-alpha.2 runtime tree (node.exe + node_modules/@deepseek-ai/dsh)
    - a built bole_agent.dll/.lib/.h (MSVC /MT for delivery)
    - the two A8 tarballs under runtime\dist (run node runtime/dev/pack.mjs)

  No network access is used here. The package pre-creates the SDK profile and
  installs @bole/sdk-server and the experience plugin into
  home\profiles\sdk\node_modules (F9); install.ps1 fills the site paths into
  home\cordis.patch.yml.
#>
param(
  [Parameter(Mandatory = $true)][string]$RuntimeDir,   # node.exe + node_modules/@deepseek-ai/dsh
  [Parameter(Mandatory = $true)][string]$NativeBuild,  # dir containing bole_agent.dll/.lib/.h/.pdb
  [string]$OutDir = "dist\bole-agent-sdk",
  [string]$PluginTarball = "",                          # default: newest runtime\dist\bole-sdk-server-*.tgz
  [string]$ExperienceTarball = "",                      # default: newest runtime\dist\*skill-bole-experience*.tgz
  [string]$ModelGateway = "http://192.168.19.203:9001", # deployment gateway, no /v1 suffix
  [string]$ConsoleExe = "",                             # optional prebuilt bole_console.exe
  [string]$GuiExe = ""                                  # optional prebuilt bole_gui.exe
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
if (![System.IO.Path]::IsPathRooted($OutDir)) { $OutDir = Join-Path $repo $OutDir }
$root = Join-Path $OutDir "bole-agent-sdk-win-x64"

if (Test-Path $OutDir) { Remove-Item -Recurse -Force $OutDir }
New-Item -ItemType Directory -Force -Path `
  "$root\bin", "$root\runtime\config", "$root\home", "$root\tools\toy-mcp",
  "$root\samples\console-c", "$root\samples\gui-win32", "$root\docs" | Out-Null

# --- helpers ----------------------------------------------------------------
function Write-Utf8NoBom([string]$Path, [string]$Text) {
  [System.IO.File]::WriteAllText($Path, $Text, (New-Object System.Text.UTF8Encoding($false)))
}

function Resolve-Tarball([string]$Explicit, [string]$Pattern, [string]$What) {
  if ($Explicit -ne "") {
    if (!(Test-Path $Explicit)) { throw "$What tarball not found: $Explicit" }
    return (Resolve-Path $Explicit).Path
  }
  $found = Get-ChildItem (Join-Path $repo "runtime\dist") -Filter $Pattern -File -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
  if ($null -eq $found) {
    throw "$What tarball not found; run node runtime/dev/build-experience.mjs && node runtime/dev/pack.mjs"
  }
  return $found.FullName
}

function Expand-Package([string]$Tarball, [string]$Destination) {
  New-Item -ItemType Directory -Force -Path $Destination | Out-Null
  $tar = Join-Path $env:SystemRoot "System32\tar.exe"
  if (!(Test-Path $tar)) { $tar = "tar.exe" }
  & $tar -xzf $Tarball -C $Destination --strip-components=1
  if ($LASTEXITCODE -ne 0) { throw "failed to extract $Tarball" }
}

# --- 1. C SDK binaries and header ------------------------------------------
foreach ($name in @("bole_agent.dll", "bole_agent.lib", "bole_agent.h")) {
  $source = Join-Path $NativeBuild $name
  if (!(Test-Path $source)) {
    throw "missing $source; the delivery build must use MSVC and emit bole_agent.lib"
  }
  Copy-Item $source (Join-Path $root "bin") -Force
}
if (Test-Path (Join-Path $NativeBuild "bole_agent.pdb")) {
  Copy-Item (Join-Path $NativeBuild "bole_agent.pdb") (Join-Path $root "bin") -Force
}

# --- 2. Runtime tree as installed ------------------------------------------
$runtimeNode = Join-Path $RuntimeDir "node.exe"
$dshEntry = Join-Path $RuntimeDir "node_modules\@deepseek-ai\dsh\lib\bin.js"
if (!(Test-Path $runtimeNode)) { throw "runtime tree has no node.exe: $RuntimeDir" }
if (!(Test-Path $dshEntry)) { throw "runtime tree has no @deepseek-ai/dsh entry: $RuntimeDir" }
Copy-Item "$RuntimeDir\*" (Join-Path $root "runtime") -Recurse -Force
Copy-Item (Join-Path $repo "runtime\packages\sdk-app\cordis.patch.yml") (Join-Path $root "runtime\cordis.patch.yml") -Force
Copy-Item (Join-Path $repo "runtime\config\*") (Join-Path $root "runtime\config") -Recurse -Force

# The C launcher passes runtime\cordis.patch.yml as the LAST --patch layer,
# after the profile and home patches, so the model gateway belongs here.
$runtimePatch = Join-Path $root "runtime\cordis.patch.yml"
$patchText = Get-Content $runtimePatch -Raw
$patchText = [regex]::Replace($patchText, "baseURL:\s*\S+", "baseURL: '$($ModelGateway.TrimEnd('/'))/v1'")
Write-Utf8NoBom $runtimePatch $patchText

# --- 3. Custom packages in the SDK profile ---------------------------------
$pluginTarball = Resolve-Tarball $PluginTarball "bole-sdk-server-*.tgz" "plugin"
$experienceTarball = Resolve-Tarball $ExperienceTarball "*skill-bole-experience*.tgz" "experience"

$node = Join-Path $root "runtime\node.exe"
$dsh = Join-Path $root "runtime\node_modules\@deepseek-ai\dsh\lib\bin.js"
$sdkHome = Join-Path $root "home"
$previousHome = $env:DSH_HOME
try {
  # First boot creates home\profiles\sdk from the shipped sdk template. It runs
  # offline; --dump-config does not start the app provider or the model.
  $env:DSH_HOME = $sdkHome
  & $node $dsh --profile sdk --dump-config 2>&1 | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "failed to initialize the sdk profile" }
} finally {
  $env:DSH_HOME = $previousHome
}

$profileModules = Join-Path $sdkHome "profiles\sdk\node_modules"
Expand-Package $pluginTarball (Join-Path $profileModules "@bole\sdk-server")
Expand-Package $experienceTarball (Join-Path $profileModules "@deepseek-ai\dsh-skill-bole-experience")
if (!(Test-Path (Join-Path $profileModules "@bole\sdk-server\lib\index.js"))) {
  throw "plugin tarball did not contain lib/index.js"
}
if (!(Test-Path (Join-Path $profileModules "@deepseek-ai\dsh-skill-bole-experience\assets\final-experiences.json"))) {
  throw "experience tarball did not contain assets/final-experiences.json"
}

# --- 4. Site home layer ----------------------------------------------------
Copy-Item (Join-Path $repo "runtime\config\home.env.template") (Join-Path $sdkHome ".env") -Force
Copy-Item (Join-Path $repo "runtime\config\home.patch.yml.template") (Join-Path $sdkHome "cordis.patch.yml.template") -Force

# --- 5. Toy MCP, samples, delivery documents, installer --------------------
Copy-Item (Join-Path $repo "examples\toy-mcp\*") (Join-Path $root "tools\toy-mcp") -Recurse -Force
Copy-Item (Join-Path $repo "examples\console-c\*") (Join-Path $root "samples\console-c") -Recurse -Force
Copy-Item (Join-Path $repo "examples\gui-win32\*") (Join-Path $root "samples\gui-win32") -Recurse -Force
if ($ConsoleExe -ne "") {
  if (!(Test-Path $ConsoleExe)) { throw "console binary not found: $ConsoleExe" }
  Copy-Item $ConsoleExe (Join-Path $root "samples\console-c\bole_console.exe") -Force
  Copy-Item (Join-Path $root "bin\bole_agent.dll") (Join-Path $root "samples\console-c") -Force
}
if ($GuiExe -ne "") {
  if (!(Test-Path $GuiExe)) { throw "GUI binary not found: $GuiExe" }
  Copy-Item $GuiExe (Join-Path $root "samples\gui-win32\bole_gui.exe") -Force
  Copy-Item (Join-Path $root "bin\bole_agent.dll") (Join-Path $root "samples\gui-win32") -Force
}
Copy-Item (Join-Path $repo "docs\delivery\*") (Join-Path $root "docs") -Recurse -Force
Copy-Item (Join-Path $repo "packaging\install.ps1") $root -Force
Copy-Item (Join-Path $repo "packaging\verify.ps1") $root -Force

# --- 6. Version manifest and checksums ------------------------------------
$inputs = Get-Content (Join-Path $repo "runtime\version.inputs.json") -Raw | ConvertFrom-Json
$runtimeVersion = (Get-Content (Join-Path $RuntimeDir "node_modules\@deepseek-ai\dsh\package.json") -Raw | ConvertFrom-Json).version
$nodeVersion = (& $node --version).Trim()
$version = [ordered]@{
  sdk      = $inputs.sdk
  abi      = $inputs.abi
  protocol = $inputs.protocol
  harness  = $inputs.harness
  runtime  = $runtimeVersion
  node     = $nodeVersion
  presets  = $inputs.presets
  plugins  = $inputs.plugins
  experience = [ordered]@{
    name    = "@deepseek-ai/dsh-skill-bole-experience"
    version = $inputs.plugins.'@deepseek-ai/dsh-skill-bole-experience'
  }
  tools    = [ordered]@{ "toy-mcp" = $inputs.toyMcp }
  builtAt  = (Get-Date).ToUniversalTime().ToString("o")
}
Write-Utf8NoBom (Join-Path $root "VERSION.json") (($version | ConvertTo-Json -Depth 6) + "`n")

Get-ChildItem $root -Recurse -File |
  Where-Object { $_.Name -ne "SHA256SUMS" } |
  ForEach-Object {
    $rel = $_.FullName.Substring($root.Length + 1)
    $hash = (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower()
    "$hash  $rel"
  } | Set-Content (Join-Path $root "SHA256SUMS") -Encoding ASCII

Compress-Archive -Path $root -DestinationPath "$OutDir.zip" -Force
Write-Host "built $OutDir.zip"
