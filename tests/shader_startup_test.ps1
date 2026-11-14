#requires -Version 7.2
param(
    [string]$GameDirectory = (Join-Path $PSScriptRoot '../game'),
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '../out/build/win-amd64-release'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot ('../out/shader-startup-' + [Guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $root) { throw 'Use a new output directory to keep existing caches and saves intact.' }
$null = New-Item -ItemType Directory -Path $root
$game = [IO.Path]::GetFullPath($GameDirectory)
$build = [IO.Path]::GetFullPath($BuildDirectory)

foreach ($pass in @('cold', 'warm')) {
    $log = Join-Path $root "$pass.log"
    $report = Join-Path $root "$pass-progress.txt"
    $control = Join-Path $root "$pass-control"
    $arguments = @(
        "--game_data_root=$game", "--user_data_root=$root/user", "--cache_root=$root/cache",
        "--log_file=$log", "--lollipop_shader_progress=$report", "--lollipop_test_directory=$control",
        '--lollipop_gpu_backend=d3d12', '--render_target_path_d3d12=rov', '--store_shaders=true',
        '--d3d12_pipeline_creation_threads=4', '--window_hidden=true', '--fullscreen=false', '--audio_mute=true'
    )
    $quoted = $arguments | ForEach-Object { '"' + $_ + '"' }
    $process = Start-Process -FilePath (Join-Path $build 'lollipop.exe') -ArgumentList $quoted -WindowStyle Hidden -PassThru
    try {
        $deadline = [DateTime]::UtcNow.AddMinutes(10)
        do {
            if ($process.HasExited) { throw "$pass launch exited before gameplay; see $log" }
            if ([DateTime]::UtcNow -ge $deadline) { throw "$pass launch timed out; see $log" }
            Start-Sleep -Milliseconds 250
            $status = if (Test-Path -LiteralPath $report) { Get-Content -LiteralPath $report -Raw }
            $text = if (Test-Path -LiteralPath $log) { Get-Content -LiteralPath $log -Raw }
            if ($status -match '^\d+ \d+ [1-9]\d* ') { throw "Shader preparation failed; see $log" }
        } until ($status -match '^\d+ \d+ 0 Complete\r?\n$' -and $text -match 'Frame output')
        if ([regex]::Matches($text, 'Created \d+ graphics pipelines .*from the storage').Count -ne 1) {
            throw "Expected exactly one pipeline preparation before gameplay; see $log"
        }
        if ($text -match 'Initializing shader storage for title') {
            throw "The launch path initialized shader storage a second time; see $log"
        }
        $null = & "$PSScriptRoot/../scripts/test-control.ps1" -Directory $control -Command capture
        $null = & "$PSScriptRoot/../scripts/test-control.ps1" -Directory $control -Command quit
        if (-not $process.WaitForExit(10000) -or $process.ExitCode -ne 0) { throw "$pass launch did not close cleanly" }
        Write-Output "$pass launch: prepared shaders once, reached gameplay and captured a frame."
    } finally {
        if (-not $process.HasExited) { Stop-Process -Id $process.Id }
        $process.Dispose()
    }
}
Write-Output "Evidence: $root"
