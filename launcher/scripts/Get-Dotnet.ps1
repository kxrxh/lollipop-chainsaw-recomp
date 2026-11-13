#requires -Version 5.1
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$version = (Get-Content -LiteralPath (Join-Path $PSScriptRoot '../global.json') -Raw | ConvertFrom-Json).sdk.version
$directory = Join-Path $root "out/tools/dotnet-$version"
$dotnet = Join-Path $directory 'dotnet.exe'
if (Test-Path -LiteralPath $dotnet) { return $dotnet }
$expected = '330ddd2665b43cd2e428216a0d8ea1ac7bb19b5434fdc530e62010dc3b52823a620bf95489d628e1ed6f5122401793ee4887cca4a019122361a1231497f632ee'
$archive = Join-Path $root "out/tools/dotnet-$version.zip"
[IO.Directory]::CreateDirectory((Split-Path $archive -Parent)) | Out-Null
if (-not (Test-Path -LiteralPath $archive) -or (Get-FileHash -LiteralPath $archive -Algorithm SHA512).Hash -ne $expected) {
    Write-Host "Downloading .NET SDK $version..."
    Invoke-WebRequest -UseBasicParsing -Uri "https://builds.dotnet.microsoft.com/dotnet/Sdk/$version/dotnet-sdk-$version-win-x64.zip" -OutFile $archive
}
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA512).Hash -ne $expected) { throw '.NET SDK integrity check failed.' }
Add-Type -AssemblyName System.IO.Compression.FileSystem
$stage = Join-Path (Split-Path $directory -Parent) ([guid]::NewGuid().ToString('N'))
try {
    [IO.Compression.ZipFile]::ExtractToDirectory($archive, $stage)
    Move-Item -LiteralPath $stage -Destination $directory
} finally {
    $resolved = [IO.Path]::GetFullPath($stage)
    if ((Split-Path $resolved -Parent) -ne [IO.Path]::GetFullPath((Join-Path $root 'out/tools')) -or (Split-Path $resolved -Leaf) -notmatch '^[0-9a-f]{32}$') { throw 'Unexpected SDK staging path.' }
    if (Test-Path -LiteralPath $resolved) { Remove-Item -LiteralPath $resolved -Recurse -Force }
}
return $dotnet
