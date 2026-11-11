#requires -Version 7.2
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. "$root/scripts/build-tools.ps1"

$scripts = @(Get-ChildItem -LiteralPath $root -Filter '*.ps1' -File)
$scripts += Get-ChildItem -LiteralPath "$root/scripts", "$root/tests" -Filter '*.ps1' -File
foreach ($script in $scripts) {
    $tokens = $null
    $errors = $null
    [Management.Automation.Language.Parser]::ParseFile($script.FullName, [ref]$tokens, [ref]$errors) | Out-Null
    if ($errors.Count) { throw "PowerShell parse failed: $($script.Name): $($errors[0].Message)" }
}

$temporary = Join-Path ([IO.Path]::GetTempPath()) ('lollipop-path-test-' + [Guid]::NewGuid().ToString('N'))
try {
    $checkout = Join-Path $temporary 'relocated project'
    New-Item -ItemType Directory -Path $checkout -Force | Out-Null
    $template = Join-Path $checkout 'lollipop_manifest.toml'
    Copy-Item -LiteralPath "$root/lollipop_manifest.toml" -Destination $template
    $game = Join-Path $checkout 'owner''s $game files'
    $generated = Join-Path $checkout 'out/generated code'
    $output = Join-Path $checkout 'out/build/codegen.toml'
    Write-CodegenManifest -Template $template -Output $output -GameDirectory $game -GeneratedDirectory $generated
    $actual = Get-Content -LiteralPath $output
    $game = $game.Replace('\', '/')
    $generated = $generated.Replace('\', '/')
    foreach ($line in @("game_root = `"$game`"", "file_path = `"$game/default.xex`"", "out_directory_path = `"$generated`"")) {
        if ($line -notin $actual) { throw "Relocated manifest path was not preserved: $line" }
    }
    $original = Get-Content -LiteralPath $template
    $otherLines = '^(?:game_root|file_path|out_directory_path)\s*='
    if ((($original | Where-Object { $_ -notmatch $otherLines }) -join "`n") -ne
        (($actual | Where-Object { $_ -notmatch $otherLines }) -join "`n")) {
        throw 'Path configuration changed game hooks or other manifest settings'
    }
    if ('game_root = "game"' -notin $original -or 'out_directory_path = "out/generated"' -notin $original) {
        throw 'Default manifest must use checkout-local directories'
    }
} finally {
    $resolved = [IO.Path]::GetFullPath($temporary)
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if (-not $resolved.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -or
        (Split-Path $resolved -Leaf) -notlike 'lollipop-path-test-*') {
        throw 'Refusing cleanup outside the test temporary directory'
    }
    if (Test-Path -LiteralPath $resolved) { Remove-Item -LiteralPath $resolved -Recurse -Force }
}
