#requires -Version 7.2
param(
    [Parameter(Mandatory)][string]$Path,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$Directory = ([IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../LollipopChainsaw-private/test-control')))
)
$ErrorActionPreference = 'Stop'
# Run only after checking that the game is in live gameplay on the lab save.
# shortcut: CSV flushes every 60 frames; add an explicit GPU-thread flush for shorter probes.
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
try {
    foreach ($phase in $phases) {
        $before = & "$PSScriptRoot/measure-frames.ps1" -Path $Path -LastFrames 1
        $startHostUs = [Math]::Round([Diagnostics.Stopwatch]::GetTimestamp() * 1000000.0 / [Diagnostics.Stopwatch]::Frequency)
        $timer = [Diagnostics.Stopwatch]::StartNew()
        foreach ($inputState in $phase.Inputs) {
            $null = & "$PSScriptRoot/test-control.ps1" -Directory $Directory -Command "input $inputState"
            Start-Sleep -Milliseconds ([int]($inputState.Split(' ')[-1]))
        }
        $timer.Stop()
        $endHostUs = [Math]::Round([Diagnostics.Stopwatch]::GetTimestamp() * 1000000.0 / [Diagnostics.Stopwatch]::Frequency)
        $measured = & "$PSScriptRoot/measure-frames.ps1" -Path $Path -AfterFrame $before.TotalFrames -LastFrames 100000
        if ($measured.TotalFrames -le $before.TotalFrames) { throw 'Game frame log stopped advancing' }
        $results += [pscustomobject]@{ Phase = $phase.Name; Seconds = $timer.Elapsed.TotalSeconds; StartHostUs = $startHostUs; EndHostUs = $endHostUs; Timing = $measured }
        $results | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $OutputDirectory 'results.json')
        $null = & "$PSScriptRoot/test-control.ps1" -Directory $Directory -Command capture
        Copy-Item -LiteralPath (Join-Path $Directory 'capture.bmp') -Destination (Join-Path $OutputDirectory "$($phase.Name).bmp")
        $results[-1]
    }
} finally {
    try { $null = & "$PSScriptRoot/test-control.ps1" -Directory $Directory -Command 'input 0 0 0 0 0 0' }
    catch { Write-Warning "Could not release test input: $_" }
}
