#requires -Version 7.2
param(
    [string]$GameDirectory = (Join-Path $PSScriptRoot '../game'),
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '../out/build/win-amd64-release'),
    [string]$SaveDirectory,
    [string]$OutputDirectory = (Join-Path $PSScriptRoot ('../out/live-settings-' + [Guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $root) { throw 'Choose a new output directory to preserve existing saves and captures.' }
$null = New-Item -ItemType Directory -Path $root
$game = [IO.Path]::GetFullPath($GameDirectory)
$build = [IO.Path]::GetFullPath($BuildDirectory)
$controlScript = Join-Path $PSScriptRoot '../scripts/test-control.ps1'
$api = 'd3d12'
$lab = Join-Path $root $api
$null = New-Item -ItemType Directory -Path $lab
if ($SaveDirectory) { Copy-Item -LiteralPath $SaveDirectory -Destination "$lab/user" -Recurse }
$control = "$lab/control"
$log = "$lab/game.log"
$arguments = @(
    "--game_data_root=$game", "--user_data_root=$lab/user", "--cache_root=$lab/cache",
    "--log_file=$log", "--lollipop_test_directory=$control", "--lollipop_gpu_backend=$api",
    '--resolution_scale=1', '--window_hidden=true', '--fullscreen=false', '--audio_mute=true',
    '--window_width=1280', '--window_height=720',
    '--lollipop_fps=60', '--swap_post_effect=none', '--present_dither=false', '--lollipop_shadow_resolution=0',
    '--store_shaders=false', '--render_target_path_d3d12=rov', '--d3d12_pipeline_creation_threads=4'
)
$quoted = foreach ($argument in $arguments) {
    '"' + [regex]::Replace([regex]::Replace($argument, '(\\*)"', '$1$1\"'), '(\\+)$', '$1$1') + '"'
}
$process = Start-Process -FilePath "$build/lollipop.exe" -ArgumentList $quoted -WindowStyle Hidden -PassThru
function Control([string]$Command) {
    if ($process.HasExited) { throw "$api exited unexpectedly; see $log" }
    & $controlScript -Directory $control -Command $Command
}
try {
    $deadline = [DateTime]::UtcNow.AddMinutes(4)
    while (-not (Test-Path -LiteralPath $control)) {
        if ($process.HasExited -or [DateTime]::UtcNow -ge $deadline) { throw "$api startup failed; see $log" }
        Start-Sleep -Milliseconds 100
    }
    foreach ($scale in @(1,2,3,1,2)) {
        $null = Control "set resolution_scale $scale"
        $expected = 'ok ' + (1280 * $scale) + 'x' + (720 * $scale)
        $deadline = [DateTime]::UtcNow.AddMinutes(3)
        do {
            Start-Sleep -Milliseconds 200
            try { $size = Control capture } catch {
                if ($process.HasExited -or [DateTime]::UtcNow -ge $deadline) { throw }
                continue
            }
            if ([DateTime]::UtcNow -ge $deadline) { throw "$api did not reach $expected; last capture: $size" }
        } until ($size -eq $expected)
        Copy-Item -LiteralPath "$control/capture.bmp" -Destination "$lab/scale-$scale.bmp" -Force
    }
    foreach ($fps in @(30,60,90,120,60)) {
        $null = Control "set lollipop_fps $fps"
        $refresh = Control 'get video_mode_refresh_rate'
        if ([double]($refresh -replace '^ok ', '') -ne [Math]::Max(60, $fps)) {
            throw "$api frame rate change did not update guest refresh: $refresh"
        }
        $expectedHz = [Math]::Max(60, $fps)
        $deadline = [DateTime]::UtcNow.AddSeconds(5)
        while ((Get-Content -LiteralPath $log -Raw) -notmatch "Guest refresh rate applied live: $expectedHz Hz") {
            if ($process.HasExited -or [DateTime]::UtcNow -ge $deadline) { throw "$api kept its old virtual refresh timer" }
            Start-Sleep -Milliseconds 50
        }
    }
    foreach ($effect in @('fxaa','fxaa_extreme','none')) {
        $null = Control "set swap_post_effect $effect"
        $deadline = [DateTime]::UtcNow.AddSeconds(30)
        while ((Get-Content -LiteralPath $log -Raw) -notmatch "Live anti-aliasing applied: $effect(?:\r?\n|$)") {
            if ($process.HasExited -or [DateTime]::UtcNow -ge $deadline) { throw "$api did not render with $effect" }
            Start-Sleep -Milliseconds 100
        }
    }
    foreach ($value in @('true','false')) {
        $null = Control "set present_dither $value"
        if ((Control presentation_state) -ne "ok dither=$value") {
            throw "$api did not update the presenter's dithering state"
        }
    }
    foreach ($shadow in @(1024,256,512,0,1024,0)) {
        $beforeShadow = (Get-Content -LiteralPath $log -Raw).Length
        if ((Control "set lollipop_shadow_resolution $shadow") -ne "ok $shadow") {
            throw "$api rejected the live shadow resolution change"
        }
        $expectedShadow = if ($shadow) { $shadow } else { 128 }
        $deadline = [DateTime]::UtcNow.AddSeconds(15)
        while ((Get-Content -LiteralPath $log -Raw).Substring($beforeShadow) -notmatch "Character shadow resolution applied: $expectedShadow,") {
            if ($process.HasExited -or [DateTime]::UtcNow -ge $deadline) { throw "$api did not apply shadow resolution $shadow" }
            Start-Sleep -Milliseconds 100
        }
        $null = Control capture
        Copy-Item -LiteralPath "$control/capture.bmp" -Destination "$lab/shadows-$shadow.bmp" -Force
    }
    if ((Get-Content -LiteralPath $log -Raw) -notmatch 'Live draw resolution scale applied: 3x3') {
        throw "$api did not record the live scale change"
    }
    $deadline = [DateTime]::UtcNow.AddSeconds(90)
    while ((Control game_settings_state) -notlike 'ok ready *') {
        if ([DateTime]::UtcNow -ge $deadline) { throw 'Native options did not become ready.' }
        $null = Control 'input 16 0 0 0 0 100'
        Start-Sleep -Milliseconds 250
    }
    $null = Control 'set lollipop_pc_settings true'
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    while ((Control ui_state) -notmatch 'PC SETTINGS') {
        if ([DateTime]::UtcNow -ge $deadline) { throw 'Settings did not open.' }
        Start-Sleep -Milliseconds 100
    }
    $null = Control 'ui_key right 1'
    Start-Sleep -Milliseconds 100
    $null = Control 'ui_key right 0'
    $null = Control 'ui_mouse 180 590 1'
    Start-Sleep -Milliseconds 100
    $null = Control 'ui_mouse 180 590 0'
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    while ((Control 'get window_width') -ne 'ok 1600') {
        if ([DateTime]::UtcNow -ge $deadline) { throw 'Display preview did not start.' }
        Start-Sleep -Milliseconds 100
    }
    $null = Control 'set lollipop_cheats_menu true'
    $deadline = [DateTime]::UtcNow.AddSeconds(17)
    while ((Control 'get window_width') -ne 'ok 1280') {
        if ([DateTime]::UtcNow -ge $deadline) { throw 'Cheats blocked the display rollback timer.' }
        Start-Sleep -Milliseconds 100
    }
    if ((Control 'get window_height') -ne 'ok 720' -or
        (Control 'get fullscreen') -ne 'ok false' -or
        (Control 'get lollipop_pc_settings') -ne 'ok true' -or
        (Control 'get lollipop_cheats_menu') -ne 'ok true' -or
        (Control ui_state) -notmatch 'CHEATS') {
        throw 'Display rollback changed the active menus or failed to restore the display.'
    }
    $null = Control 'set lollipop_cheats_menu false'
    $null = Control quit
    if (-not $process.WaitForExit(10000) -or $process.ExitCode -ne 0) { throw "$api did not exit cleanly" }
    Write-Output "$api passed: live 720p/1440p/4K, FPS, FXAA, dithering, shadows and display rollback with Cheats open in PID $($process.Id)."
} finally {
    if (-not $process.HasExited) { Stop-Process -Id $process.Id }
    $process.Dispose()
}
Write-Output "Evidence: $root"
