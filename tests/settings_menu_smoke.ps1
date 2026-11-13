#requires -Version 7.2
param(
    [Parameter(Mandatory)][int]$GameProcessId,
    [string]$Directory = ([IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../out/test-control'))),
    [ValidateSet('mouse','keyboard','gamepad')][string]$ConfirmWith = 'mouse',
    [switch]$FromMainMenu
)
$ErrorActionPreference = 'Stop'
# Run against an isolated save. The final check closes the test game.
$process = Get-Process -Id $GameProcessId
if ($process.ProcessName -ne 'lollipop') { throw 'GameProcessId must identify the test game.' }
$null = $process.Handle
function Send([string]$Command) {
    if ($process.HasExited) { throw 'The test game exited unexpectedly.' }
    & "$PSScriptRoot/../scripts/test-control.ps1" -Directory $Directory -Command $Command
}
function Key([string]$Name) {
    $null = Send "ui_key $Name 1"
    $null = Send "ui_key $Name 0"
}
function Pad([int]$Buttons) {
    $null = Send "input $Buttons 0 0 0 0 150"
    Start-Sleep -Milliseconds 220
    $null = Send 'input 0 0 0 0 0 0'
    Start-Sleep -Milliseconds 80
}
function Wait-State([string]$Expected) {
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        $state = Send quit_state
        if ($state -eq "ok $Expected") { return }
        if ($state -eq 'ok failed') { throw 'The native quit dialog failed.' }
        Start-Sleep -Milliseconds 60
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Expected quit state $Expected, received $state."
}
function Click([double]$X, [double]$Y) {
    $width = [int](Send 'get window_width').Substring(3)
    $height = [int](Send 'get window_height').Substring(3)
    $xPixel = [int]($X * $width)
    $yPixel = [int]($Y * $height)
    $null = Send "ui_mouse $xPixel $yPixel 1"
    $null = Send "ui_mouse $xPixel $yPixel 0"
}
function Check-Cancelled {
    Wait-State idle
    $expected = if ($FromMainMenu) { 'ok false' } else { 'ok true' }
    if ((Send 'get lollipop_pc_settings') -ne $expected) {
        throw 'Cancel returned to the wrong menu.'
    }
    if ($FromMainMenu -and (Send ui_state) -match '\| PC SETTINGS ') {
        throw 'Cancel opened settings behind the main menu.'
    }
}
function Open-Quit {
    if ($FromMainMenu) { Pad 4096 } else { Key enter }
    Wait-State active
    if ((Send quit_selection) -ne 'ok 2') { throw 'No must be selected by default.' }
    if ((Send ui_state) -match '\| (?:PC SETTINGS|QUIT GAME) ') {
        throw 'The custom page covered the native dialog.'
    }
}
$settingsFile = Join-Path (Send 'get user_data_root').Substring(3) 'pc-settings.toml'
$before = if (Test-Path -LiteralPath $settingsFile) { (Get-FileHash -LiteralPath $settingsFile).Hash } else { '' }
try {
    Wait-State idle
    $null = Send 'input_device gamepad'
    $null = Send 'set lollipop_pc_settings false'
    if (-not $FromMainMenu) {
        $null = Send 'set lollipop_pc_settings true'
        Key right # Leave an unapplied display change in the draft.
        Key up # From the first row, wrap to Quit Game.
    }
    Open-Quit
    Key enter
    Check-Cancelled
    if (-not $FromMainMenu) {
        Key escape
        if ((Send 'get lollipop_pc_settings') -ne 'ok true') {
            throw 'Cancelling quit lost the settings draft.'
        }
        Key escape # Dismiss Discard Changes without losing the draft.
    }

    if ($FromMainMenu) {
        $null = Send 'input 4096 0 0 0 0 1000'
        Start-Sleep -Milliseconds 400
        if ((Send quit_state) -ne 'ok idle') { throw 'Held opening input reached the dialog.' }
        $null = Send 'input 0 0 0 0 0 0'
        Wait-State active
        if ((Send quit_selection) -ne 'ok 2') { throw 'Held input changed the default choice.' }
        $null = Send 'input 4096 0 0 0 0 1000'
        Start-Sleep -Milliseconds 700
        if ((Send quit_state) -ne 'ok cancelled') { throw 'Held No reopened the native slot.' }
        $null = Send 'input 0 0 0 0 0 0'
        Check-Cancelled
    }
    Open-Quit
    Pad 4096
    Check-Cancelled
    Open-Quit
    Pad 8192
    Check-Cancelled
    Open-Quit
    $null = Send 'ui_key escape 1'
    Start-Sleep -Milliseconds 700
    if ((Send quit_state) -ne 'ok cancelled') { throw 'Held Escape escaped its parent menu.' }
    $null = Send 'ui_key escape 0'
    Check-Cancelled
    foreach ($cancelKey in @('backspace', 'space')) {
        Open-Quit
        Key $cancelKey
        Check-Cancelled
    }
    Open-Quit
    Click 0.02 0.02
    if ((Send quit_state) -ne 'ok active') { throw 'Clicking outside the buttons dismissed the dialog.' }
    Click 0.795 0.62
    Check-Cancelled
    Open-Quit
    switch ($ConfirmWith) {
        mouse { Click 0.655 0.62 }
        keyboard { Key left; $null = Send 'ui_key enter 1' }
        gamepad { Pad 4; $null = Send 'input 4096 0 0 0 0 150' }
    }
    if (-not $process.WaitForExit(10000) -or $process.ExitCode -ne 0) {
        throw "Confirming Quit Game did not close cleanly (exit code: $($process.ExitCode))."
    }
    $after = if (Test-Path -LiteralPath $settingsFile) { (Get-FileHash -LiteralPath $settingsFile).Hash } else { '' }
    if ($before -ne $after) { throw 'Quit changed saved PC settings.' }
    Write-Output "Native quit checks passed; final confirmation: $ConfirmWith; main menu: $FromMainMenu."
} finally {
    if (-not $process.HasExited) {
        $null = Send 'input 0 0 0 0 0 0'
        $null = Send 'ui_key escape 0'
        $null = Send 'ui_key enter 0'
    }
    $process.Dispose()
}
