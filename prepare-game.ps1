#requires -Version 5.1
param(
    [Parameter(Mandatory)][string]$InputPath,
    [string]$Destination = (Join-Path $PSScriptRoot 'game'),
    [string]$ExtractXiso = 'extract-xiso.exe'
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/scripts/game-disc.ps1"
$destination = [IO.Path]::GetFullPath($Destination)
$stagingDirectory = Split-Path $destination -Parent
$inputItem = Get-Item -LiteralPath $InputPath

if ($inputItem.PSIsContainer) {
    Assert-SupportedGame $inputItem.FullName
    if ($inputItem.FullName.TrimEnd('\', '/') -eq $destination.TrimEnd('\', '/')) {
        Write-Host 'The existing game directory is supported and ready.'
        return
    }
    $sourcePrefix = $inputItem.FullName.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if ($stagingDirectory.StartsWith($sourcePrefix, [StringComparison]::OrdinalIgnoreCase) -or
        $stagingDirectory -eq $inputItem.FullName.TrimEnd('\', '/')) {
        throw 'The import staging directory cannot be inside the input directory.'
    }
} elseif ($inputItem.Extension -ne '.iso') {
    throw 'Input must be an extracted game directory or an Xbox 360 .iso file. Unpack archives first.'
}
if (Test-Path -LiteralPath $destination) {
    throw 'The game directory already exists. It will not be overwritten.'
}
if (-not $inputItem.PSIsContainer) {
    if ([IO.File]::Exists($ExtractXiso)) {
        $ExtractXiso = [IO.Path]::GetFullPath($ExtractXiso)
    } else {
        $extractor = Get-Command $ExtractXiso -ErrorAction SilentlyContinue
        if (-not $extractor) { throw 'extract-xiso.exe was not found. Supply -ExtractXiso or use an extracted game directory.' }
        $ExtractXiso = $extractor.Source
    }
}
New-Item -ItemType Directory -Path $stagingDirectory -Force | Out-Null
$staging = Join-Path $stagingDirectory ('import-' + [Guid]::NewGuid().ToString('N'))
try {
    if ($inputItem.PSIsContainer) {
        Copy-Item -LiteralPath $inputItem.FullName -Destination $staging -Recurse
    } else {
        & $ExtractXiso -x -s -d $staging $inputItem.FullName
        if ($LASTEXITCODE -ne 0) { throw 'Disc extraction failed.' }
    }
    Assert-SupportedGame $staging
    [IO.Directory]::Move($staging, $destination)
} catch {
    throw "Import failed. Original input is unchanged. Any partial extraction remains at '$staging'. $($_.Exception.Message)"
}
Write-Host "Game prepared at $destination. Next: ./build.ps1"
