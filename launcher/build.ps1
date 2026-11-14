#requires -Version 5.1
param(
    [string]$OutputFile = (Join-Path $PSScriptRoot '../out/launcher/Lollipop Chainsaw Launcher.exe'),
    [string]$SdkDirectory = (Join-Path $PSScriptRoot '../out/sdk'),
    [string]$LlvmDirectory,
    [switch]$Tests
)
$ErrorActionPreference = 'Stop'
$dotnet = & "$PSScriptRoot/scripts/Get-Dotnet.ps1"
$env:DOTNET_ROOT = Split-Path $dotnet -Parent
$env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
$env:DOTNET_NOLOGO = '1'
$root = Split-Path $PSScriptRoot -Parent
if (-not $Tests) {
    & "$PSScriptRoot/scripts/Pack-Sdk.ps1" -SdkDirectory $SdkDirectory -OutputFile (Join-Path $root 'out/launcher/sdk.zip') | Out-Null
    if (-not $LlvmDirectory) {
        $compiler = Get-Command clang++.exe -ErrorAction SilentlyContinue
        $LlvmDirectory = if ($compiler) { Split-Path $compiler.Source -Parent } else { Join-Path $env:ProgramFiles 'LLVM/bin' }
    }
    & "$PSScriptRoot/scripts/Pack-Compiler.ps1" -LlvmDirectory $LlvmDirectory -OutputFile (Join-Path $root 'out/launcher/llvm.tar.xz') | Out-Null
}
$project = if ($Tests) { Join-Path $PSScriptRoot 'tests/Lollipop.Launcher.Tests.csproj' } else { Join-Path $PSScriptRoot 'Lollipop.Launcher.csproj' }
$assembly = if ($Tests) { 'Lollipop.Launcher.Tests.exe' } else { 'Lollipop.Launcher.exe' }
$publish = Join-Path $root ('out/launcher/publish/' + $(if ($Tests) { 'tests' } else { 'app' }))
Push-Location $PSScriptRoot
try {
    & $dotnet publish $project -c Release --runtime win-x64 --self-contained true --output $publish --nologo
    if ($LASTEXITCODE -ne 0) { throw 'Launcher publish failed.' }
} finally { Pop-Location }
$destination = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputFile)
[IO.Directory]::CreateDirectory((Split-Path $destination -Parent)) | Out-Null
Copy-Item -LiteralPath (Join-Path $publish $assembly) -Destination $destination -Force
Write-Output $destination
