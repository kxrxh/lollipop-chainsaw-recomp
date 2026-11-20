#requires -Version 5.1

function Get-SdkCommit {
    'a3e1d82678ca20d68d4837a8e9ee4652ff073745'
}

function Get-SdkPatches {
    'rexglue-fixes.patch','rexglue-shader-precompile.patch','rexglue-live-settings.patch',
    'rexglue-input.patch','rexglue-game.patch'
}

function Get-SdkRuntimeFiles {
    'rexapp.dll','rexapu.dll','rexapu-sdl3.dll','rexbase.dll','rexfilesystem.dll',
    'rexgpu.dll','rexgpu-xenos-d3d12.dll','rexgpu-xenos-null.dll','rexgpu-xenos-vulkan.dll',
    'rexhid.dll','rexhid-sdl3.dll','rexhid-xinput.dll','rexkernel.dll','rexppc.dll',
    'rexui.dll','rexui-sdl3.dll','rexxam.dll','rexxbdm.dll','SDL3.dll'
}

function Get-PackageFileHash {
    param([Parameter(Mandatory)][string]$Path)
    $stream = [IO.File]::OpenRead($Path)
    $hash = [Security.Cryptography.SHA256]::Create()
    try {
        ([BitConverter]::ToString($hash.ComputeHash($stream))).Replace('-', '').ToLowerInvariant()
    } finally { $hash.Dispose(); $stream.Dispose() }
}

function Get-SdkIdentity {
    param([Parameter(Mandatory)][string]$Root)
    $hash = [Security.Cryptography.SHA256]::Create()
    try {
        $parts = @((Get-SdkCommit))
        foreach ($name in (Get-SdkPatches)) {
            $parts += Get-PackageFileHash -Path (Join-Path $Root "patches/$name")
        }
        $bytes = [Text.Encoding]::UTF8.GetBytes(($parts -join "`n"))
        ([BitConverter]::ToString($hash.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant().Substring(0, 16)
    } finally { $hash.Dispose() }
}
