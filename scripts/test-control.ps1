#requires -Version 7.2
param(
    [Parameter(Mandatory)][string]$Command,
    [string]$Directory = ([IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../out/test-control')))
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Directory -PathType Container)) {
    throw 'Launch run.ps1 with --lollipop_test_directory pointing to this directory first.'
}
$requestId = [Guid]::NewGuid().ToString('N')
$temporary = Join-Path $Directory "$requestId.tmp"
[IO.File]::WriteAllText($temporary, "$requestId`n$Command`n")
$deadline = [DateTime]::UtcNow.AddSeconds(5)
while ($true) {
    try {
        [IO.File]::Move($temporary, (Join-Path $Directory 'command.txt'), $true)
        break
    } catch {
        if ([DateTime]::UtcNow -ge $deadline) { throw }
        Start-Sleep -Milliseconds 20
    }
}
while ([DateTime]::UtcNow -lt $deadline) {
    $reply = $null
    try {
        $stream = [IO.File]::Open((Join-Path $Directory 'response.txt'), 'Open', 'Read', 'ReadWrite')
        $reader = [IO.StreamReader]::new($stream)
        try {
            $content = $reader.ReadToEnd()
            if ($content.EndsWith("`n")) { $reply = $content -split '\r?\n' }
        }
        finally { $reader.Dispose() }
    }
    catch [IO.IOException] { }
    if ($reply.Count -ge 3 -and $reply[0] -eq $requestId) {
        if (-not $reply[1].StartsWith('ok ')) { throw $reply[1] }
        return $reply[1]
    }
    Start-Sleep -Milliseconds 20
}
throw 'Test bridge timed out; verify that the matching test build is running.'
