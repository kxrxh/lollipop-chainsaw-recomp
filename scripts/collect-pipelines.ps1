#requires -Version 5.1
[CmdletBinding()]
param(
    [string]$CacheDirectory = (Join-Path $PSScriptRoot '../cache'),
    [string]$OutputFile = (Join-Path $PSScriptRoot '../out/pipeline-inventory.txt'),
    [Parameter(Mandatory)][string]$Coverage,
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '../out/build/win-amd64-release')
)
$ErrorActionPreference = 'Stop'
$tool = Join-Path $BuildDirectory 'lollipop_shader_tool.exe'
if (-not [IO.File]::Exists($tool)) { throw 'Build the project before collecting pipelines.' }
# Collect only after closing the game so the renderer has flushed its records.
& $tool collect ([IO.Path]::GetFullPath($CacheDirectory)) ([IO.Path]::GetFullPath($OutputFile)) $Coverage
if ($LASTEXITCODE -ne 0) { throw 'Pipeline collection failed. Close the game and check the cache and output paths.' }
Write-Output ([IO.Path]::GetFullPath($OutputFile))
