#requires -Version 5.1
param(
    [string]$SdkDirectory = (Join-Path $PSScriptRoot '../../out/sdk'),
    [Parameter(Mandatory)][string]$OutputFile
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. (Join-Path $root 'scripts/sdk-package.ps1')
$sdk = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($SdkDirectory).TrimEnd('\', '/')
$output = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputFile)
$required = @('lollipop-sdk.id','bin/rexglue.exe','bin/rexruntime.dll','bin/rexgpu-xenos.dll',
    'bin/msvcp140.dll','bin/msvcp140_atomic_wait.dll','bin/vcruntime140.dll','bin/vcruntime140_1.dll',
    'include/rex/rex_app.h','lib/rexruntime.lib','lib/cmake/rexglue/rexglueConfig.cmake',
    'lib/cmake/rexglue/rexglueTargets-release.cmake',
    'share/rexglue/rex_app.cpp','licenses/rexglue/LICENSE','licenses/SOURCES.txt')
$binaries = @('rexglue.exe','rexruntime.dll','rexgpu-xenos.dll','msvcp140.dll','msvcp140_1.dll',
    'msvcp140_2.dll','msvcp140_atomic_wait.dll','msvcp140_codecvt_ids.dll','vcruntime140.dll',
    'vcruntime140_1.dll','vcruntime140_threads.dll','vccorlib140.dll','concrt140.dll')
foreach ($name in $required) {
    if (-not [IO.File]::Exists((Join-Path $sdk $name))) {
        throw "SDK package is missing $name. Build it with ./build-sdk.ps1 first, or pass -SdkDirectory."
    }
}
if ([IO.File]::ReadAllText((Join-Path $sdk 'lollipop-sdk.id')) -ne (Get-SdkIdentity -Root $root)) {
    throw 'The SDK was built with different patches. Rebuild it with ./build-sdk.ps1 before packaging the launcher.'
}
$files = @(Get-ChildItem -LiteralPath $sdk -Recurse -File | Where-Object {
    $_.Name -notmatch '\.(pdb|ilk|exp|pc)$' -and $_.Name -notmatch '^SDL(_|3[-_])test'
} | Sort-Object FullName)
foreach ($file in $files) {
    $name = $file.FullName.Substring($sdk.Length + 1).Replace('\', '/')
    if ($name -ne 'lollipop-sdk.id' -and $name -notmatch '^(bin|cmake|include|lib|licenses|share)/') {
        throw "Unexpected SDK package file: $name"
    }
    if (($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
        $name -match '\.(xex|iso|obj|o)$|(^|/)sources\.cmake$|lollipop_(recomp|init|register|pch)' -or
        ($name.StartsWith('bin/') -and $file.Name -notin $binaries) -or
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
try {
    $archive = [IO.Compression.ZipFile]::Open($stage, 'Create')
    try {
        foreach ($file in $files) {
            $name = $file.FullName.Substring($sdk.Length + 1).Replace('\', '/')
            if ($file.Extension -eq '.cmake') {
                $text = [IO.File]::ReadAllText($file.FullName)
                $match = [Regex]::Match($text, 'file\(GLOB _cmake_config_files "\$\{CMAKE_CURRENT_LIST_DIR\}/([^"\r\n]+)"\)')
                if ($match.Success) {
                    # Generated glob imports misread brackets in the installation path.
                    $configs = @(Get-ChildItem -LiteralPath $file.DirectoryName -File |
                        Where-Object Name -Like $match.Groups[1].Value | Sort-Object Name |
                        ForEach-Object { '"${CMAKE_CURRENT_LIST_DIR}/' + $_.Name + '"' })
                    $text = $text.Remove($match.Index, $match.Length).Insert($match.Index, ('set(_cmake_config_files ' + ($configs -join ' ') + ')'))
                    $entry = $archive.CreateEntry($name)
                    $entry.LastWriteTime = [DateTimeOffset]$file.LastWriteTime
                    $writer = [IO.StreamWriter]::new($entry.Open(), [Text.UTF8Encoding]::new($false))
                    try { $writer.Write($text) } finally { $writer.Dispose() }
                    continue
                }
            }
            [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $file.FullName, $name) | Out-Null
        }
    } finally { $archive.Dispose() }
    if ([IO.File]::Exists($output)) { [IO.File]::Replace($stage, $output, [NullString]::Value) }
    else { [IO.File]::Move($stage, $output) }
} finally { if ([IO.File]::Exists($stage)) { [IO.File]::Delete($stage) } }
Write-Output $output
