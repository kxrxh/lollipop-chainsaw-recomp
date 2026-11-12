#requires -Version 7.2

function Get-BuildTools {
    param([string]$LlvmDirectory)

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio C++ Build Tools first.' }
    $vs = & $vswhere -products '*' -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 Microsoft.VisualStudio.Component.VC.CMake.Project -property installationPath
    if ($LASTEXITCODE -ne 0 -or -not $vs) { throw 'Visual Studio C++ Build Tools not found.' }
    $vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
    $devEnvironment = & $env:ComSpec /d /c "`"$vcvars`" >nul && set"
    if ($LASTEXITCODE -ne 0) { throw 'Failed to initialize the Visual C++ environment.' }
    foreach ($line in $devEnvironment) {
        if ($line -match '^([^=]+)=(.*)$') {
            [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
        }
    }
    if (-not $LlvmDirectory) {
        $compiler = Get-Command clang++.exe -ErrorAction SilentlyContinue
        if (-not $compiler) { throw 'Install LLVM 20.1.8 on PATH or supply -LlvmDirectory.' }
        $LlvmDirectory = Split-Path $compiler.Source -Parent
    }
    $LlvmDirectory = [IO.Path]::GetFullPath($LlvmDirectory)
    $clang = Join-Path $LlvmDirectory 'clang.exe'
    $clangxx = Join-Path $LlvmDirectory 'clang++.exe'
    if (-not (Test-Path -LiteralPath $clang) -or -not (Test-Path -LiteralPath $clangxx)) {
        throw 'LlvmDirectory must contain clang.exe and clang++.exe from LLVM 20.1.8.'
    }
    $version = & $clangxx --version
    if ($LASTEXITCODE -ne 0 -or $version[0] -notmatch 'clang version 20\.1\.8\b') {
        throw 'This project requires LLVM 20.1.8.'
    }
    $cmakeBin = Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin'
    $cmake = Join-Path $cmakeBin 'cmake.exe'
    $ctest = Join-Path $cmakeBin 'ctest.exe'
    $ninja = Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
    foreach ($tool in @($cmake, $ctest, $ninja)) {
        if (-not (Test-Path -LiteralPath $tool)) { throw "Missing Visual Studio CMake/Ninja tool: $tool" }
    }
    [pscustomobject]@{ CMake = $cmake; CTest = $ctest; Ninja = $ninja; Clang = $clang; Clangxx = $clangxx }
}

function Write-CodegenManifest {
    param([string]$Template, [string]$Output, [string]$GameDirectory, [string]$GeneratedDirectory)

    $game = [IO.Path]::GetFullPath($GameDirectory).Replace('\', '/')
    $generated = [IO.Path]::GetFullPath($GeneratedDirectory).Replace('\', '/')
    $manifest = Get-Content -LiteralPath $Template -Raw
    $manifest = [regex]::Replace($manifest, '(?m)^game_root\s*=.*$', [Text.RegularExpressions.MatchEvaluator]{ param($match) "game_root = `"$game`"" })
    $manifest = [regex]::Replace($manifest, '(?m)^file_path\s*=.*$', [Text.RegularExpressions.MatchEvaluator]{ param($match) "file_path = `"$game/default.xex`"" })
    $manifest = [regex]::Replace($manifest, '(?m)^out_directory_path\s*=.*$', [Text.RegularExpressions.MatchEvaluator]{ param($match) "out_directory_path = `"$generated`"" })
    New-Item -ItemType Directory -Path (Split-Path $Output -Parent) -Force | Out-Null
    Set-Content -LiteralPath $Output -Value $manifest -NoNewline
}
