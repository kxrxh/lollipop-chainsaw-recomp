#requires -Version 7.2
param(
    [string]$GameDirectory = (Join-Path $PSScriptRoot 'game'),
    [string]$UserDirectory = (Join-Path $PSScriptRoot 'user'),
    [string]$CacheDirectory = (Join-Path $PSScriptRoot 'cache'),
    [Parameter(ValueFromRemainingArguments)][string[]]$GameArguments = @()
)
$ErrorActionPreference = 'Stop'
$defaults = [ordered]@{
    game_data_root = [IO.Path]::GetFullPath($GameDirectory)
    user_data_root = [IO.Path]::GetFullPath($UserDirectory)
    cache_root = [IO.Path]::GetFullPath($CacheDirectory)
    d3d12_pipeline_creation_threads = 4
    render_target_path_d3d12 = 'rov'
}
foreach ($argument in $GameArguments) {
    if ($argument -match '^--(?:no-)?([^=]+)(?:=|$)') {
        $defaults.Remove($Matches[1])
    }
}
$launchArguments = @($defaults.GetEnumerator() | ForEach-Object { "--$($_.Key)=$($_.Value)" }) + $GameArguments
if (-not (Test-Path -LiteralPath "$PSScriptRoot/out/build/win-amd64-release/lollipop.exe")) {
    throw 'Game executable not found. Run build.ps1 first.'
}
& "$PSScriptRoot/out/build/win-amd64-release/lollipop.exe" @launchArguments | Out-Host
if ($LASTEXITCODE -ne 0) { throw "Lollipop exited with code $LASTEXITCODE. Check the runtime log." }
