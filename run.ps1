#requires -Version 7.2
$ErrorActionPreference = 'Stop'
$private = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../LollipopChainsaw-private'))
$defaults = [ordered]@{
    game_data_root = "$private/game"
    user_data_root = "$private/user"
    cache_root = "$private/cache"
    window_width = 2560
    window_height = 1440
    resolution = '720p'
    resolution_scale = 2
    anisotropic_override = 5
    video_mode_refresh_rate = 60
    d3d12_pipeline_creation_threads = 4
    render_target_path_d3d12 = 'rov'
    readback_memexport = 'false'
    mnk_mode = 'true'
}
foreach ($argument in $args) {
    if ($argument -match '^--(?:no-)?([^=]+)(?:=|$)') {
        $defaults.Remove($Matches[1])
    }
}
$launchArguments = @($defaults.GetEnumerator() | ForEach-Object { "--$($_.Key)=$($_.Value)" }) + $args
if (-not (Test-Path -LiteralPath "$PSScriptRoot/out/build/win-amd64-release/lollipop.exe")) {
    throw 'Game executable not found. Run build.ps1 first.'
}
& "$PSScriptRoot/out/build/win-amd64-release/lollipop.exe" @launchArguments | Out-Host
if ($LASTEXITCODE -ne 0) { throw "Lollipop exited with code $LASTEXITCODE. Check the runtime log." }
