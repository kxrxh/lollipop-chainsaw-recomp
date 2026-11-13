#requires -Version 7.2
param(
    [Parameter(Mandatory)][int]$GameProcessId,
    [Parameter(Mandatory)][string]$Directory,
    [Parameter(Mandatory)][string]$FrameLog,
    [Parameter(Mandatory)][string]$GameLog
)
$ErrorActionPreference = 'Stop'
$control = Join-Path $PSScriptRoot '../scripts/test-control.ps1'
function Send-PhoneCommand([string]$Command) {
    if (-not (Get-Process -Id $GameProcessId -ErrorAction SilentlyContinue)) {
        throw 'The phone test process exited.'
    }
    & $control -Directory $Directory -Command $Command
}
function Invoke-PhoneEvent([string]$Event) {
    $null = Send-PhoneCommand "game_console ce $Event"
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        Start-Sleep -Milliseconds 100
        $state = Send-PhoneCommand 'game_console_state'
        if ($state -eq 'ok complete') { return }
        if ($state.StartsWith('ok error')) { throw $state }
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Native event did not complete: $Event"
}

# Start from a normally loaded, copied checkpoint 25 save with reduced cache limits.
$frames = @(Import-Csv -LiteralPath $FrameLog | Where-Object { $_.host_us -match '^\d+$' })
if (-not $frames.Count) { throw 'The frame log has no completed frames.' }
$started = [long]$frames[-1].host_us
$null = Send-PhoneCommand 'set lollipop_pc_settings false'
$null = Send-PhoneCommand 'input 0 0 0 0 0 1000'
Start-Sleep -Milliseconds 1100
for ($cycle = 1; $cycle -le 3; $cycle++) {
    Invoke-PhoneEvent 'TelephoneIntro'
    Start-Sleep -Seconds 5
    $null = Send-PhoneCommand 'capture'
    Copy-Item -LiteralPath (Join-Path $Directory 'capture.bmp') `
        -Destination (Join-Path $Directory "phone-intro-$cycle.bmp")
    Start-Sleep -Seconds 22
    Invoke-PhoneEvent 'TeleTest'
    Start-Sleep -Seconds 10
    $null = Send-PhoneCommand 'capture'
    Copy-Item -LiteralPath (Join-Path $Directory 'capture.bmp') `
        -Destination (Join-Path $Directory "phone-outro-$cycle.bmp")
    Write-Output "Phone load/unload cycle $cycle completed."
}
$null = Send-PhoneCommand 'set lollipop_pc_settings true'
$frames = @(Import-Csv -LiteralPath $FrameLog | Where-Object {
    $_.host_us -match '^\d+$' -and [long]$_.host_us -gt $started
})
$evictions = ($frames | Measure-Object -Property texture_cache_evictions -Sum).Sum
if ($frames.Count -lt 300) { throw 'The phone test did not produce enough frames.' }
if ($evictions -le 0) { throw 'No texture eviction occurred; repeat with reduced cache limits.' }
if (Select-String -LiteralPath $GameLog -Pattern 'D3D12 device removed|DRED page fault') {
    throw 'The game log contains a GPU failure.'
}
Write-Output "Passed: $($frames.Count) frames, $evictions texture evictions. Inspect the phone captures."
