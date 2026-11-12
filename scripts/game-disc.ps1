function Assert-SupportedGame([string]$Directory) {
    $executable = Join-Path $Directory 'default.xex'
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
        throw 'Missing default.xex. Choose the extracted folder containing default.xex, AshGame and Engine.'
    }
    $expected = 'A687F47D537B011600146C665E59F5D2B4D4243284799D84FEE7DA45CDD2DF26'
    if ((Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash -ne $expected) {
        throw 'Unsupported default.xex. Use the original Xbox 360 disc with media ID 31BE13D9, version 0.0.0.2.'
    }
    foreach ($assetDirectory in @('AshGame', 'Engine')) {
        if (-not (Test-Path -LiteralPath (Join-Path $Directory $assetDirectory) -PathType Container)) {
            throw "Missing game directory: $assetDirectory. Check that your disc was fully extracted."
        }
    }
}
