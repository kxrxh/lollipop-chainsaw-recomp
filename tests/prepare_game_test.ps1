#requires -Version 7.2
$ErrorActionPreference = 'Stop'
$temporary = Join-Path ([IO.Path]::GetTempPath()) ('lollipop-import-test-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temporary | Out-Null
function Expect-Failure([scriptblock]$Action, [string]$Message) {
    $caught = $null
    try { & $Action | Out-Null } catch { $caught = $_.Exception.Message }
    if ($null -eq $caught -or $caught -notlike "*$Message*") {
        throw "Expected failure containing '$Message'; got '$caught'"
    }
}
try {
    $fixture = Join-Path $temporary 'repository'
    New-Item -ItemType Directory -Path $fixture | Out-Null
    Copy-Item -LiteralPath "$PSScriptRoot/../prepare-game.ps1" -Destination $fixture
    $importer = Join-Path $fixture 'prepare-game.ps1'
    $invalidGame = Join-Path $temporary 'invalid-game'
    New-Item -ItemType Directory -Path $invalidGame | Out-Null
    Expect-Failure { & $importer -InputPath $invalidGame } 'Missing default.xex'
    [IO.File]::WriteAllText((Join-Path $invalidGame 'default.xex'), 'unsupported executable')
    Expect-Failure { & $importer -InputPath $invalidGame } 'Unsupported default.xex'
    $unsupported = Join-Path $temporary 'test.zip'
    [IO.File]::WriteAllText($unsupported, 'not an archive')
    Expect-Failure { & $importer -InputPath $unsupported } 'Input must be'

    $existingGame = Join-Path $temporary 'LollipopChainsaw-private/game'
    New-Item -ItemType Directory -Path $existingGame -Force | Out-Null
    $canary = Join-Path $existingGame 'preserve.txt'
    [IO.File]::WriteAllText($canary, 'preserve')
    $iso = Join-Path $temporary 'test.iso'
    [IO.File]::WriteAllText($iso, 'not a real disc image')
    Expect-Failure { & $importer -InputPath $iso } 'will not be overwritten'
    if ([IO.File]::ReadAllText($canary) -ne 'preserve') { throw 'Existing game was modified' }
} finally {
    $resolved = [IO.Path]::GetFullPath($temporary)
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if (-not $resolved.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -or
        (Split-Path $resolved -Leaf) -notlike 'lollipop-import-test-*') {
        throw 'Refusing cleanup outside the test temporary directory'
    }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
