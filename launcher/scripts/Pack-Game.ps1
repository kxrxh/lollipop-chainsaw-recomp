#requires -Version 5.1
param([Parameter(Mandatory)][string]$OutputFile)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$output = [IO.Path]::GetFullPath($OutputFile)
[IO.Directory]::CreateDirectory((Split-Path $output -Parent)) | Out-Null
$files = @('LICENSE','THIRD_PARTY.md','VERSION','CMakeLists.txt','lollipop_manifest.toml',
    'assets/lollipop.rc','assets/settings_resources.h','launcher/assets/lollipop.ico',
    'assets/settings/controls.png','assets/settings/panel.png','assets/settings/quit-game.png',
    'assets/shaders/pipelines.txt','patches/rexglue-fixes.patch','patches/rexglue-shader-precompile.patch',
    'patches/LICENSE.rexglue','patches/LICENSE.libmspack','patches/SSX-RexGlue-LICENSE.txt')
$files += Get-ChildItem -LiteralPath "$root/src" -Recurse -File |
    Where-Object Extension -in '.cpp','.h' | ForEach-Object { $_.FullName.Substring($root.Length + 1).Replace('\','/') }
$files += Get-ChildItem -LiteralPath "$root/launcher/assets" -File |
    Where-Object Name -Match '^(LICENSE|NOTICES)\.' | ForEach-Object { 'launcher/assets/' + $_.Name }
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$stage = $output + '.' + [guid]::NewGuid().ToString('N') + '.tmp'
try {
    $archive = [IO.Compression.ZipFile]::Open($stage, 'Create')
    try {
        foreach ($name in $files) {
            [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, (Join-Path $root $name), $name) | Out-Null
        }
    } finally { $archive.Dispose() }
    if (Test-Path -LiteralPath $output) { [IO.File]::Replace($stage, $output, [NullString]::Value) }
    else { [IO.File]::Move($stage, $output) }
} finally { if (Test-Path -LiteralPath $stage) { [IO.File]::Delete($stage) } }
