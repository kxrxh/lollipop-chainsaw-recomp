#requires -Version 5.1
param([string]$OutputFile = (Join-Path $PSScriptRoot '../out/installer/Lollipop Chainsaw Setup.exe'))
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$exe = [IO.Path]::GetFullPath($OutputFile)
$directory = [IO.Directory]::CreateDirectory((Split-Path $exe -Parent)).FullName
$zip = Join-Path $directory ([guid]::NewGuid().ToString('N') + '.zip')
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
# Explicit public inputs keep private discs, translated code and build products out of the installer.
$files = @('LICENSE','THIRD_PARTY.md','CMakeLists.txt','lollipop_manifest.toml',
    'assets/lollipop.ico','assets/lollipop.rc','assets/settings_resources.h',
    'assets/settings/controls.png','assets/settings/panel.png','assets/installer/hero.png','assets/installer/paper.png',
    'patches/rexglue-fixes.patch','patches/rexglue-shader-precompile.patch','patches/LICENSE.rexglue','patches/LICENSE.libmspack','patches/SSX-RexGlue-LICENSE.txt',
    'assets/shaders/pipelines.txt','installer/launcher.xaml',
    'installer/setup.cs','installer/setup-install.cs','installer/setup-tools.cs','installer/setup-update.cs')
$files += Get-ChildItem -LiteralPath "$root/src","$root/tests" -Recurse -File |
    Where-Object Extension -in '.cpp','.h','.cs','.cmake' | ForEach-Object { $_.FullName.Substring($root.Length + 1).Replace('\','/') }
try {
    $archive = [IO.Compression.ZipFile]::Open($zip, 'Create')
    try {
        foreach ($name in $files) {
            $null = [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, (Join-Path $root $name), $name)
        }
    } finally { $archive.Dispose() }
    $compiler = "$env:SystemRoot/Microsoft.NET/Framework64/v4.0.30319/csc.exe"
    $icon = Join-Path $root 'assets\lollipop.ico'
    $source = Join-Path $PSScriptRoot 'setup.cs'
    $installSource = Join-Path $PSScriptRoot 'setup-install.cs'
    $toolsSource = Join-Path $PSScriptRoot 'setup-tools.cs'
    $updateSource = Join-Path $PSScriptRoot 'setup-update.cs'
    $wpf = Split-Path $compiler -Parent
    & $compiler /nologo /optimize+ /target:winexe /platform:x64 /main:lollipop.Setup "/out:$exe" "/win32icon:$icon" `
        "/resource:$zip,kit.zip" "/resource:$root/installer/launcher.xaml,launcher.xaml" "/resource:$icon,lollipop.ico" `
        "/resource:$root/assets/installer/hero.png,hero.png" "/resource:$root/assets/installer/paper.png,paper.png" `
        /reference:System.IO.Compression.dll /reference:System.IO.Compression.FileSystem.dll /reference:System.Windows.Forms.dll `
        /reference:System.Xaml.dll "/reference:$wpf/WPF/WindowsBase.dll" "/reference:$wpf/WPF/PresentationCore.dll" `
        "/reference:$wpf/WPF/PresentationFramework.dll" $source $installSource $toolsSource $updateSource
    if ($LASTEXITCODE -ne 0) { throw 'Could not build the Windows installer.' }
    if (-not ('LollipopInstallerShell' -as [type])) {
        Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class LollipopInstallerShell {
    [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
    public static extern void SHChangeNotify(int change, uint flags, string path, IntPtr unused);
}
'@
    }
    # Refresh this EXE after replacing a package Windows may have cached under its old icon.
    [LollipopInstallerShell]::SHChangeNotify(0x2000, 0x1005, $exe, [IntPtr]::Zero)
    Write-Output $exe
} finally { [IO.File]::Delete($zip) }
