#requires -Version 7.2
param(
    [Parameter(Mandatory)][int]$GameProcessId,
    [string]$Directory = ([IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../out/test-control'))),
    [ValidateSet('mouse','keyboard','gamepad')][string]$ConfirmWith = 'mouse'
)
$ErrorActionPreference = 'Stop'
# Run against an isolated save. The final check closes the test game.
$process = Get-Process -Id $GameProcessId
if ($process.ProcessName -ne 'lollipop') { throw 'GameProcessId must identify the test game.' }
$null = $process.Handle
function Send([string]$Command) {
    & "$PSScriptRoot/../scripts/test-control.ps1" -Directory $Directory -Command $Command
}
function Key([string]$Name) {
    $null = Send "ui_key $Name 1"
    $null = Send "ui_key $Name 0"
}
function Pad([int]$Buttons) {
    $null = Send "input $Buttons 0 0 0 0 150"
    Start-Sleep -Milliseconds 200
    $null = Send 'input 0 0 0 0 0 0'
}
function Click([double]$X, [double]$Y) {
    $xPixel = [int]($script:originX + $X * $script:scale)
    $yPixel = [int]($script:originY + $Y * $script:scale)
    $null = Send "ui_mouse $xPixel $yPixel 1"
    $null = Send "ui_mouse $xPixel $yPixel 0"
}
function Check-Open {
    if ($process.HasExited -or (Send 'get lollipop_pc_settings') -ne 'ok true') {
        throw 'Cancelling quit closed the settings menu or the game.'
    }
}
function Check-Cancelled {
    Check-Open
    Key escape
    $deadline = [DateTime]::UtcNow.AddSeconds(2)
    while ((Send 'get lollipop_pc_settings') -ne 'ok false') {
        if ([DateTime]::UtcNow -ge $deadline) { throw 'Cancel did not dismiss the quit confirmation.' }
    }
    $null = Send 'set lollipop_pc_settings true'
    Key up
}
$settingsFile = Join-Path (Send 'get user_data_root').Substring(3) 'pc-settings.toml'
$before = if (Test-Path -LiteralPath $settingsFile) { (Get-FileHash -LiteralPath $settingsFile).Hash } else { '' }
try {
    $null = Send 'set lollipop_pc_settings false'
    $null = Send 'set lollipop_pc_settings true'
    $state = Send ui_state
    if ($state -notmatch '\| PC SETTINGS (-?\d+),(-?\d+) (\d+)x(\d+)') {
        throw 'PC settings are not visible. Finish shader preparation before running this check.'
    }
    $script:originX = [int]$Matches[1]
    $script:originY = [int]$Matches[2]
    $script:scale = [int]$Matches[3] / 1160.0
    Key up
    Key enter
    Key enter # Cancel must be selected when the quit confirmation opens.
    Check-Cancelled
    Key enter
    $null = Send 'input 32768 0 0 0 0 1000'
    if ((Send input_state) -notmatch 'guest_buttons=0 .*ui_buttons=32768') {
        throw 'The quit confirmation leaked input into gameplay.'
    }
    $null = Send 'input 0 0 0 0 0 0'
    Key escape
    Check-Cancelled
    $null = Send 'input_device gamepad'
    Pad 4096
    Pad 4096 # Controller A must also choose Cancel by default.
    Check-Cancelled
    Pad 4096
    Pad 8192
    Check-Cancelled
    Click 1000 560
    Click 805 364
    Check-Cancelled
    Click 1000 560
    switch ($ConfirmWith) {
        mouse { Click 335 364 }
        keyboard { Key left; $null = Send 'ui_key enter 1' }
        gamepad { Pad 4; $null = Send 'input 4096 0 0 0 0 150' }
    }
    if (-not $process.WaitForExit(10000) -or $process.ExitCode -ne 0) {
        throw 'Confirming Quit Game did not close the game cleanly.'
    }
    $after = if (Test-Path -LiteralPath $settingsFile) { (Get-FileHash -LiteralPath $settingsFile).Hash } else { '' }
    if ($before -ne $after) { throw 'The quit confirmation changed saved PC settings.' }
    Write-Output 'Settings quit checks passed with keyboard, controller and mouse input.'
} finally {
    if (-not $process.HasExited) { $null = Send 'input 0 0 0 0 0 0' }
    $process.Dispose()
}
