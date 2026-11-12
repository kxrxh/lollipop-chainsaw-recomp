#requires -Version 5.1
[CmdletBinding(PositionalBinding = $false)]
param(
    [Parameter(Position=0)][string]$GameDirectory = (Join-Path $PSScriptRoot 'game'),
    [Parameter(Position=1)][string]$UserDirectory = (Join-Path $PSScriptRoot 'user'),
    [Parameter(Position=2)][string]$CacheDirectory = (Join-Path $PSScriptRoot 'cache'),
    [Parameter(ValueFromRemainingArguments)][string[]]$GameArguments = @(),
    [string]$BuildDirectory = (Join-Path $PSScriptRoot 'out/build/win-amd64-release'),
    [switch]$Player,
    [switch]$PrecompileShaders
)
$ErrorActionPreference = 'Stop'
$defaults = [ordered]@{
    game_data_root = [IO.Path]::GetFullPath($GameDirectory)
    user_data_root = [IO.Path]::GetFullPath($UserDirectory)
    cache_root = [IO.Path]::GetFullPath($CacheDirectory)
    d3d12_pipeline_creation_threads = 4
    render_target_path_d3d12 = 'rov'
}
if ($PrecompileShaders) { $defaults.log_file = Join-Path $defaults.cache_root 'shader-precompile.log' }
foreach ($argument in $GameArguments) {
    if ($argument -match '^--(?:no-)?([^=]+)(?:=|$)') {
        $defaults.Remove($Matches[1])
    }
}
$launchArguments = @($defaults.GetEnumerator() | ForEach-Object { "--$($_.Key)=$($_.Value)" }) + $GameArguments
trap {
    if (-not $Player) { throw $_ }
    Add-Type -AssemblyName PresentationFramework
    [void][Windows.MessageBox]::Show($_.Exception.Message, 'Lollipop Chainsaw Recomp')
    exit 1
}
if (-not (Test-Path -LiteralPath "$BuildDirectory/lollipop.exe")) {
    throw 'Game executable not found. Run build.ps1 first.'
}
if ($PrecompileShaders) {
    if (-not [IO.File]::Exists("$BuildDirectory/shader-precompile-v1.ready")) {
        throw 'This game build needs an update before it can precompile shaders. Rebuild or update the installation first.'
    }
    $null = [IO.Directory]::CreateDirectory([IO.Path]::GetFullPath($CacheDirectory))
    $report = Join-Path ([IO.Path]::GetFullPath($CacheDirectory)) 'shader-precompile-progress.txt'
    [IO.File]::WriteAllText($report, '')
    $launchArguments += '--lollipop_precompile_only=true', "--lollipop_shader_progress=$report"
    $quoted = foreach ($argument in $launchArguments) {
        '"' + [regex]::Replace([regex]::Replace($argument, '(\\*)"', '$1$1\"'), '(\\+)$', '$1$1') + '"'
    }
    $process = Start-Process -FilePath ([IO.Path]::GetFullPath("$BuildDirectory/lollipop.exe")) `
        -ArgumentList $quoted -WindowStyle Hidden -PassThru
    try {
        $last = ''
        do {
            try { $status = [IO.File]::ReadAllText($report) } catch [IO.IOException] { $status = '' }
            if ($status -and $status -ne $last -and $status -match '^(\d+) (\d+) (\d+) ([^\r\n]+)\r?\n$') {
                $percent = if ([long]$Matches[2] -gt 0) { [Math]::Min(100, [int](100.0 * [long]$Matches[1] / [long]$Matches[2])) } else { -1 }
                Write-Progress -Activity $Matches[4] -Status "$($Matches[1]) / $($Matches[2])" -PercentComplete $percent
                Write-Output "LollipopShaders $($Matches[1]) $($Matches[2]) $($Matches[3]) $($Matches[4])"
                $last = $status
            }
        } while (-not $process.WaitForExit(100))
        $process.WaitForExit()
        $status = [IO.File]::ReadAllText($report)
        if ($process.ExitCode -ne 0 -or $status -notmatch '^\d+ \d+ 0 Complete\r?\n$') {
            $runtimeLog = Join-Path ([IO.Path]::GetFullPath($CacheDirectory)) 'shader-precompile.log'
            if ([IO.File]::Exists($runtimeLog)) { Get-Content -LiteralPath $runtimeLog -Tail 30 | Write-Output }
            throw 'Shader preparation did not finish. Check the runtime log before retrying.'
        }
    } finally { $process.Dispose() }
    Write-Progress -Activity 'Shaders ready' -Completed
    return
}
& "$BuildDirectory/lollipop.exe" @launchArguments | Out-Host
if ($LASTEXITCODE -ne 0) { throw "Lollipop exited with code $LASTEXITCODE. Check the runtime log." }
