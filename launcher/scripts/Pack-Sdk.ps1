#requires -Version 5.1
param(
    [string]$SdkDirectory = (Join-Path $PSScriptRoot '../../out/sdk-0.11.0'),
    [Parameter(Mandatory)][string]$OutputFile
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. (Join-Path $root 'scripts/sdk-package.ps1')
$sdk = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($SdkDirectory).TrimEnd('\', '/')
$output = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputFile)
$runtime = @(Get-SdkRuntimeFiles)
$required = @('lollipop-sdk.id','bin/rexglue.exe',
    'bin/msvcp140.dll','bin/msvcp140_atomic_wait.dll','bin/vcruntime140.dll','bin/vcruntime140_1.dll',
    'include/rex/app/title_app.h','include/renderdoc_app.h',
    'lib/rexapp.lib','lib/rexfilesystem.lib','lib/rexgpu.lib','lib/rexhid.lib','lib/rexui.lib',
    'lib/cmake/rexglue/rexglueConfig.cmake','lib/cmake/rexglue/rexglueTargets-release.cmake',
    'lib/cmake/rexglue/rexglueModules-Release.cmake','cmake/SDL3Config.cmake',
    'cmake/SDL3sharedTargets-release.cmake','licenses/rexglue/LICENSE','licenses/SOURCES.txt') +
    @($runtime | ForEach-Object { "bin/$_" })
$binaries = $runtime + @('rexglue.exe','msvcp140.dll','msvcp140_1.dll','msvcp140_2.dll',
    'msvcp140_atomic_wait.dll','msvcp140_codecvt_ids.dll','vcruntime140.dll',
    'vcruntime140_1.dll','vcruntime140_threads.dll','vccorlib140.dll','concrt140.dll')
$binaryPaths = @($binaries | ForEach-Object { 'bin/' + $_ })
foreach ($name in $required) {
    if (-not [IO.File]::Exists((Join-Path $sdk $name))) {
        throw "SDK package is missing $name. Build it with ./build-sdk.ps1 first, or pass -SdkDirectory."
    }
}
if ([IO.File]::ReadAllText((Join-Path $sdk 'lollipop-sdk.id')) -ne (Get-SdkIdentity -Root $root)) {
    throw 'The SDK was built with a different revision or patch set. Rebuild it with ./build-sdk.ps1 before packaging the launcher.'
}
$files = @(Get-ChildItem -LiteralPath $sdk -Recurse -File | Where-Object {
    $relative = $_.FullName.Substring($sdk.Length + 1).Replace('\', '/')
    $codegenLog = $relative -match '^bin/logs/rexglue_[0-9]+\.log$' -and
        -not ($_.Attributes -band [IO.FileAttributes]::ReparsePoint)
    -not $codegenLog -and $_.Name -notmatch '\.(pdb|ilk|exp|pc)$' -and $_.Name -notmatch '^SDL(_|3[-_])test'
} | Sort-Object FullName)
foreach ($file in $files) {
    $name = $file.FullName.Substring($sdk.Length + 1).Replace('\', '/')
    if ($name -ne 'lollipop-sdk.id' -and $name -notmatch '^(bin|cmake|include|lib|licenses|share)/') {
        throw "Unexpected SDK package file: $name"
    }
    if (($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
        $name -match '\.(xex|iso|obj|o)$|(^|/)sources\.cmake$|lollipop_(recomp|init|register|pch)' -or
        ($name.StartsWith('bin/', [StringComparison]::OrdinalIgnoreCase) -and $name -notin $binaryPaths) -or
        ($file.Extension -eq '.exe' -and $name -ne 'bin/rexglue.exe')) {
        throw "Build output or game code is not allowed in the SDK package: $name"
    }
    if ($file.Extension -eq '.cmake' -and [IO.File]::ReadAllText($file.FullName) -match '(?<![A-Za-z])[A-Za-z]:[/\\]|/home/|/Users/') {
        throw "The SDK CMake package contains an absolute machine path: $name"
    }
}
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
[IO.Directory]::CreateDirectory((Split-Path $output -Parent)) | Out-Null
$stage = $output + '.' + [Guid]::NewGuid().ToString('N') + '.tmp'
# SDK cache paths depend on archive contents, including ZIP timestamps.
$timestamp = [DateTimeOffset]::new(2000, 1, 1, 0, 0, 0, [TimeSpan]::Zero)
try {
    $archive = [IO.Compression.ZipFile]::Open($stage, 'Create')
    try {
        foreach ($file in $files) {
            $name = $file.FullName.Substring($sdk.Length + 1).Replace('\', '/')
            if ($file.Extension -eq '.cmake') {
                $text = [IO.File]::ReadAllText($file.FullName)
                # Generated glob imports misread brackets in the installation path.
                $imports = [Regex]::Replace($text,
                    'file\(GLOB (_cmake_config_files|_rexglue_module_files) "\$\{CMAKE_CURRENT_LIST_DIR\}/([^"\r\n]+)"\)',
                    [Text.RegularExpressions.MatchEvaluator]{
                        param($match)
                        $configs = @(Get-ChildItem -LiteralPath $file.DirectoryName -File |
                            Where-Object Name -Like $match.Groups[2].Value | Sort-Object Name |
                            ForEach-Object { '"${CMAKE_CURRENT_LIST_DIR}/' + $_.Name + '"' })
                        'set(' + $match.Groups[1].Value + ' ' + ($configs -join ' ') + ')'
                    })
                if ($imports -cne $text) {
                    $entry = $archive.CreateEntry($name)
                    $entry.LastWriteTime = $timestamp
                    $writer = [IO.StreamWriter]::new($entry.Open(), [Text.UTF8Encoding]::new($false))
                    try { $writer.Write($imports) } finally { $writer.Dispose() }
                    continue
                }
            }
            $entry = $archive.CreateEntry($name)
            $entry.LastWriteTime = $timestamp
            $input = [IO.File]::OpenRead($file.FullName)
            $contents = $entry.Open()
            try { $input.CopyTo($contents) } finally { $contents.Dispose(); $input.Dispose() }
        }
    } finally { $archive.Dispose() }
    if ([IO.File]::Exists($output)) { [IO.File]::Replace($stage, $output, [NullString]::Value) }
    else { [IO.File]::Move($stage, $output) }
} finally { if ([IO.File]::Exists($stage)) { [IO.File]::Delete($stage) } }
Write-Output $output
