#requires -Version 7.2
$ErrorActionPreference = 'Stop'
$tools = Join-Path $PSScriptRoot '../LollipopChainsaw-tools'
$sdk = Join-Path $tools 'rexglue-sdk-0.10.1-lollipop/win-amd64'
if (-not (Test-Path "$sdk/bin/rexglue.exe")) { throw 'Run build-sdk.ps1 first.' }
$llvm = Join-Path $tools 'llvm-20.1.8/bin'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -products '*' -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $vs) { throw 'Visual Studio C++ Build Tools not found.' }

$vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
$devEnvironment = & $env:ComSpec /d /c "`"$vcvars`" >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'Failed to initialize the Visual C++ environment.' }
foreach ($line in $devEnvironment) {
    if ($line -match '^([^=]+)=(.*)$') {
        [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
    }
}
$cmake = Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$ctest = Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/ctest.exe'
$ninja = Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
$build = Join-Path $PSScriptRoot 'out/build/win-amd64-release'

& $cmake "-DGAME_XEX=$PSScriptRoot/../LollipopChainsaw-private/game/default.xex" `
    -P "$PSScriptRoot/tests/verify_input.cmake"
if ($LASTEXITCODE -ne 0) { throw 'The input executable does not match the verified build.' }
Push-Location $PSScriptRoot
try {
    & "$sdk/bin/rexglue.exe" codegen "$PSScriptRoot/lollipop_manifest.toml"
    if ($LASTEXITCODE -ne 0) { throw 'ReXGlue code generation failed. See the analysis log.' }
} finally { Pop-Location }
& $cmake -S $PSScriptRoot -B $build -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" `
    "-DCMAKE_CXX_COMPILER=$llvm/clang++.exe" "-DCMAKE_PREFIX_PATH=$sdk" `
    "-Drexglue_DIR=$sdk/lib/cmake/rexglue" -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& $cmake --build $build --parallel 6
if ($LASTEXITCODE -ne 0) { throw 'Compilation failed.' }
& $ctest --test-dir $build --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Project tests failed.' }
