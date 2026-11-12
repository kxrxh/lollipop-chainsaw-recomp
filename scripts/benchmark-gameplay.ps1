#requires -Version 7.2
param(
    [Parameter(Mandatory)][string]$Path,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$Directory = ([IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../out/test-control')))
)
$ErrorActionPreference = 'Stop'
# Run only after checking that the game is in live gameplay on the lab save.
$phases = @(
    @{ Name = 'idle'; Inputs = @('0 0 0 0 0 10000') },
    @{ Name = 'camera'; Inputs = @('0 0 0 9000 0 5000', '0 0 0 -9000 0 5000',
                                 '0 0 0 9000 0 5000', '0 0 0 -9000 0 5000') },
    @{ Name = 'movement'; Inputs = @('0 0 20000 0 0 2500', '0 -20000 0 0 0 2500',
                                   '0 0 -20000 0 0 2500', '0 20000 0 0 0 2500') },
    @{ Name = 'attack_inputs'; Inputs = @(1..20 | ForEach-Object {
        '32768 0 0 0 0 500'; '0 0 0 0 0 500'
    }) }
)
$duration = 0
foreach ($phase in $phases) {
    foreach ($inputState in $phase.Inputs) {
        $hold = [int]($inputState.Split(' ')[-1])
        if ($hold -le 0 -or $hold -gt 10000) { throw 'Invalid benchmark hold' }
        $duration += $hold
    }
}
if ($duration -ne 60000) { throw 'Benchmark must contain 60 seconds of input' }
if (Test-Path $OutputDirectory) { throw 'Use a fresh benchmark output directory' }
$null = New-Item -ItemType Directory $OutputDirectory
$results = @()
$profile = [ordered]@{}
foreach ($flag in 'lollipop_fps','mnk_mode','readback_memexport','readback_memexport_fast',
                  'd3d12_readback_memexport','resolution_scale','render_target_path_d3d12','vsync') {
    $reply = & "$PSScriptRoot/test-control.ps1" -Directory $Directory -Command "get $flag"
    $profile[$flag] = $reply.Substring(3)
}
function Get-HostMicroseconds {
    [long][Math]::Floor([Diagnostics.Stopwatch]::GetTimestamp() * 1000000.0 / [Diagnostics.Stopwatch]::Frequency)
}
try {
    foreach ($phase in $phases) {
        $startHostUs = $null
        foreach ($inputState in $phase.Inputs) {
            $null = & "$PSScriptRoot/test-control.ps1" -Directory $Directory -Command "input $inputState"
            if ($null -eq $startHostUs) { $startHostUs = Get-HostMicroseconds }
            Start-Sleep -Milliseconds ([int]($inputState.Split(' ')[-1]))
        }
        $endHostUs = Get-HostMicroseconds
        $null = & "$PSScriptRoot/test-control.ps1" -Directory $Directory -Command capture
        Copy-Item -LiteralPath (Join-Path $Directory 'capture.bmp') -Destination (Join-Path $OutputDirectory "$($phase.Name).bmp")
        $measured = & "$PSScriptRoot/measure-frames.ps1" -Path $Path -StartHostUs $startHostUs -EndHostUs $endHostUs -TargetFPS $profile.lollipop_fps
        $results += [pscustomobject]@{ Phase = $phase.Name; Seconds = ($endHostUs - $startHostUs) / 1e6; StartHostUs = $startHostUs; EndHostUs = $endHostUs; Timing = $measured; Profile = $profile }
        $results | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $OutputDirectory 'results.json')
        $results[-1]
    }
} finally {
    try { $null = & "$PSScriptRoot/test-control.ps1" -Directory $Directory -Command 'input 0 0 0 0 0 0' }
    catch { Write-Warning "Could not release test input: $_" }
}
