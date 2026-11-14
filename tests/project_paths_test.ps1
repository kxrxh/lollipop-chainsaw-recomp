#requires -Version 7.2
param(
    [string]$RexGlueDirectory = (Join-Path $PSScriptRoot '../out/sdk/lib/cmake/rexglue'),
    [string]$GeneratedDirectory = (Join-Path $PSScriptRoot '../out/generated'),
    [string]$LlvmDirectory
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. "$root/scripts/build-tools.ps1"

$scripts = @(Get-ChildItem -LiteralPath $root -Filter '*.ps1' -File)
$scripts += Get-ChildItem -LiteralPath "$root/scripts", "$root/launcher", "$root/tests" -Filter '*.ps1' -File
$scripts += Get-ChildItem -LiteralPath "$root/launcher/scripts" -Filter '*.ps1' -File
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
    $manifestTime = [DateTime]::UtcNow.AddDays(-1)
    [IO.File]::SetLastWriteTimeUtc($output, $manifestTime)
    Write-CodegenManifest -Template $template -Output $output -GameDirectory $game -GeneratedDirectory $generated
    if ([IO.File]::GetLastWriteTimeUtc($output) -ne $manifestTime) {
        throw 'An unchanged manifest was rewritten during an incremental build'
    }
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
    $tools = Get-BuildTools -LlvmDirectory $LlvmDirectory
    $sdk = [IO.Path]::GetFullPath((Join-Path $RexGlueDirectory '../../..'))
    Push-Location $temporary
    try {
        $installer = Join-Path $temporary 'launcher output/Lollipop Chainsaw Launcher.exe'
        & "$root/launcher/build.ps1" -OutputFile $installer -SdkDirectory $sdk `
            -LlvmDirectory (Split-Path $tools.Clangxx -Parent) | Out-Null
    } finally { Pop-Location }
    if (-not (Test-Path -LiteralPath $installer)) { throw 'Standalone launcher was not published.' }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::OpenRead("$root/out/launcher/kit.zip")
    try {
        foreach ($name in 'CMakeLists.txt','launcher/assets/lollipop.ico','src/main.cpp','VERSION','THIRD_PARTY.md',
                          'launcher/assets/LICENSE.Markdig','launcher/assets/LICENSE.NET',
                          'launcher/assets/LICENSE.LLVM','launcher/assets/LICENSE.Clang','launcher/assets/LICENSE.LLD') {
            if (-not $archive.GetEntry($name)) { throw "Game source kit is missing $name" }
        }
        if ($archive.Entries.FullName -match '^(game|user|cache|out|generated|tests|launcher/tests)/|\.ps1$|\.cs$|\.xaml$|default\.xex$') {
            throw 'Launcher payload contains private data, tests or development scripts'
        }
        $playerSource = Join-Path $temporary 'player source kit'
        foreach ($entry in $archive.Entries) {
            $target = Join-Path $playerSource $entry.FullName
            [IO.Directory]::CreateDirectory((Split-Path $target -Parent)) | Out-Null
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $target)
        }
    } finally { $archive.Dispose() }
    $playerBuild = Join-Path $temporary 'player build'
    & $tools.CMake -S $playerSource -B $playerBuild -G Ninja "-DCMAKE_MAKE_PROGRAM=$($tools.Ninja)" `
        "-DCMAKE_CXX_COMPILER=$($tools.Clangxx)" "-Drexglue_DIR=$RexGlueDirectory" `
        "-DLOLLIPOP_GENERATED_DIR=$GeneratedDirectory" -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release
    if ($LASTEXITCODE -ne 0) { throw 'Player source kit could not be configured without test files.' }
    $targets = (& $tools.Ninja -C $playerBuild -t targets all) -join "`n"
    if ($LASTEXITCODE -ne 0 -or $targets -notmatch '(?m)^lollipop\.exe:') { throw 'Player build is missing the game target.' }
    if ($targets -match '(?m)^lollipop_(?:[a-z_]*test|header_check|setup_check|shader_tool)(?:\.exe)?:') {
        throw 'Player build includes development targets.'
    }
    $commands = (& $tools.Ninja -C $playerBuild -t commands lollipop) -join "`n"
    if ($LASTEXITCODE -ne 0 -or $commands -match 'tests[/\\][a-z_]+_test\.(cpp|cs)|header-check|setup_test\.cs') {
        throw 'Player compilation plan includes developer tests.'
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
