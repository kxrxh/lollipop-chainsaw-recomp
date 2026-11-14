#requires -Version 5.1

function Get-SdkCommit {
    'bd833a2ab69d305f99ddf57418fc38ffec8af93d'
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
        foreach ($name in 'rexglue-fixes.patch','rexglue-shader-precompile.patch') {
            $parts += Get-PackageFileHash -Path (Join-Path $Root "patches/$name")
        }
        $bytes = [Text.Encoding]::UTF8.GetBytes(($parts -join "`n"))
        ([BitConverter]::ToString($hash.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant().Substring(0, 16)
    } finally { $hash.Dispose() }
}
