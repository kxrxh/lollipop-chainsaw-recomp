#requires -Version 5.1
param(
    [Parameter(Mandatory)][string]$LlvmDirectory,
    [Parameter(Mandatory)][string]$OutputFile
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. (Join-Path $root 'scripts/sdk-package.ps1')
$llvm = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($LlvmDirectory).TrimEnd('\', '/')
if ([IO.File]::Exists((Join-Path $llvm 'clang++.exe'))) { $llvm = Split-Path $llvm -Parent }
$binaries = @('clang.exe','clang++.exe','lld-link.exe','llvm-ar.exe','llvm-lib.exe','llvm-ranlib.exe')
foreach ($name in $binaries) {
    if (-not [IO.File]::Exists((Join-Path $llvm "bin/$name"))) { throw "LLVM package is missing bin/$name" }
}
$version = & (Join-Path $llvm 'bin/clang++.exe') --version
if ($LASTEXITCODE -ne 0 -or ($version -join "`n") -notmatch 'clang version 20\.1\.8\b' -or
    ($version -join "`n") -notmatch 'Target: x86_64-pc-windows-msvc') {
    throw 'Package LLVM 20.1.8 for Windows x64, matching the SDK compiler.'
}
$include = Join-Path $llvm 'lib/clang/20/include'
if (-not [IO.File]::Exists((Join-Path $include 'stddef.h'))) { throw 'LLVM builtin headers are missing.' }
$licenseHashes = @{
    LLVM = '8d85c1057d742e597985c7d4e6320b015a9139385cff4cbae06ffc0ebe89afee'
    Clang = 'ebcd9bbf783a73d05c53ba4d586b8d5813dcdf3bbec50265860ccc885e606f47'
    LLD = 'f7891568956e34643eb6a0db1462db30820d40d7266e2a78063f2fe233ece5a0'
}
foreach ($part in $licenseHashes.Keys) {
    if ((Get-PackageFileHash -Path (Join-Path $root "launcher/assets/LICENSE.$part")) -ne $licenseHashes[$part]) {
        throw "The pinned LLVM 20.1.8 notice changed: $part"
    }
}
$output = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputFile)
if ($output.StartsWith($llvm + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Write the compiler package outside the LLVM installation.' }
$parent = Split-Path $output -Parent
[IO.Directory]::CreateDirectory($parent) | Out-Null
$stage = Join-Path $parent ([Guid]::NewGuid().ToString('N'))
$archive = $output + '.' + [Guid]::NewGuid().ToString('N') + '.tmp'
if (-not ('LollipopCompilerFiles' -as [type])) {
    Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class LollipopCompilerFiles {
  [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
  public static extern bool CreateHardLink(string path, string existing, IntPtr reserved);
}
'@
}
try {
    foreach ($folder in 'bin','lib/clang/20/include','licenses') {
        [IO.Directory]::CreateDirectory((Join-Path $stage $folder)) | Out-Null
    }
    $copies = @{}
    $sources = @('LLVM 20.1.8; Windows x64; player-side C++ compiler',
        'Upstream: https://github.com/llvm/llvm-project/releases/tag/llvmorg-20.1.8',
        'Official archive: clang+llvm-20.1.8-x86_64-pc-windows-msvc.tar.xz',
        'Official archive SHA-256: f229769f11d6a6edc8ada599c0cda964b7dee6ab1a08c6cf9dd7f513e85b107f',
        'Profile: clang, clang++, lld-link, llvm-ar, llvm-lib, llvm-ranlib, builtin headers and x64 builtins.',
        '', 'Input binary SHA-256:')
    foreach ($name in $binaries) {
        $source = Join-Path $llvm "bin/$name"
        $target = Join-Path $stage "bin/$name"
        $hash = Get-PackageFileHash -Path $source
        $sources += "$hash  bin/$name"
        if (-not $copies.ContainsKey($hash) -or
            -not [LollipopCompilerFiles]::CreateHardLink($target, $copies[$hash], [IntPtr]::Zero)) {
            [IO.File]::Copy($source, $target)
        }
        $copies[$hash] = $target
    }
    foreach ($file in Get-ChildItem -LiteralPath $include -Recurse -File) {
        if ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'LLVM headers must be regular files.' }
        $target = Join-Path (Join-Path $stage 'lib/clang/20/include') $file.FullName.Substring($include.Length + 1)
        [IO.Directory]::CreateDirectory((Split-Path $target -Parent)) | Out-Null
        [IO.File]::Copy($file.FullName, $target)
    }
    $builtins = Join-Path $llvm 'lib/clang/20/lib/windows/clang_rt.builtins-x86_64.lib'
    if ([IO.File]::Exists($builtins)) {
        $target = Join-Path $stage 'lib/clang/20/lib/windows'
        [IO.Directory]::CreateDirectory($target) | Out-Null
        [IO.File]::Copy($builtins, (Join-Path $target 'clang_rt.builtins-x86_64.lib'))
    }
    foreach ($part in $licenseHashes.Keys) {
        [IO.File]::Copy((Join-Path $root "launcher/assets/LICENSE.$part"), (Join-Path $stage "licenses/LICENSE.$part"))
    }
    [IO.File]::WriteAllText((Join-Path $stage 'licenses/SOURCES.txt'), ($sources -join "`n") + "`n", [Text.UTF8Encoding]::new($false))
    & (Join-Path ([Environment]::SystemDirectory) 'tar.exe') -cJf $archive -C $stage bin lib licenses
    if ($LASTEXITCODE -ne 0) { throw 'Compiler package creation failed.' }
    if ([IO.File]::Exists($output)) { [IO.File]::Replace($archive, $output, [NullString]::Value) }
    else { [IO.File]::Move($archive, $output) }
} finally {
    if ([IO.File]::Exists($archive)) { [IO.File]::Delete($archive) }
    if ([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($stage)) -ne [IO.Path]::GetFullPath($parent) -or
        [IO.Path]::GetFileName($stage) -notmatch '^[0-9a-f]{32}$') { throw 'Unexpected compiler staging directory.' }
    if ([IO.Directory]::Exists($stage)) { Remove-Item -LiteralPath $stage -Recurse -Force }
}
Write-Output $output
