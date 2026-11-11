#requires -Version 7.2
$ErrorActionPreference = 'Stop'
$tools = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../LollipopChainsaw-tools'))
$source = Join-Path $tools 'rexglue-source'
$commit = & git -C $source rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $commit -ne 'bd833a2ab69d305f99ddf57418fc38ffec8af93d') {
    throw 'ReXGlue source must be checked out at v0.10.1 (bd833a2). See README.md.'
}
$patch = Join-Path $PSScriptRoot 'patches/rexglue-fixes.patch'
& git -C $source apply --reverse --check $patch 2>$null
if ($LASTEXITCODE -ne 0) {
    & git -C $source apply --check $patch
    if ($LASTEXITCODE -ne 0) { throw 'SDK patch conflicts with local source changes.' }
    & git -C $source apply $patch
    if ($LASTEXITCODE -ne 0) { throw 'Failed to apply SDK fixes.' }
}
& git -C $source submodule update --init --depth 1 --jobs 8 `
    thirdparty/cli11 thirdparty/libmspack thirdparty/FFmpeg thirdparty/tomlplusplus `
    thirdparty/simde thirdparty/xxHash thirdparty/spdlog thirdparty/fmt thirdparty/utfcpp `
    thirdparty/imgui thirdparty/sdl3 thirdparty/o1heap thirdparty/inja
if ($LASTEXITCODE -ne 0) { throw 'SDK dependency checkout failed.' }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -products '*' -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $vs) { throw 'Visual Studio C++ Build Tools not found.' }
$vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
$devEnvironment = & $env:ComSpec /d /c "`"$vcvars`" >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'Failed to initialize the compiler environment.' }
foreach ($line in $devEnvironment) {
    if ($line -match '^([^=]+)=(.*)$') {
        [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
    }
}
$cmake = Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$ninja = Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
$llvm = Join-Path $tools 'llvm-20.1.8/bin'
$build = Join-Path $tools 'build-sdk-lollipop'
$prefix = Join-Path $tools 'rexglue-sdk-0.10.1-lollipop/win-amd64'
& $cmake -S $source -B $build -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" `
    "-DCMAKE_C_COMPILER=$llvm/clang.exe" "-DCMAKE_CXX_COMPILER=$llvm/clang++.exe" `
    "-DCMAKE_INSTALL_PREFIX=$prefix" -DCMAKE_BUILD_TYPE=Release `
    -DREXGLUE_ENABLE_TRACY=OFF -DREXGLUE_USE_VULKAN=OFF -DREXGLUE_BUILD_TESTS=OFF
if ($LASTEXITCODE -ne 0) { throw 'SDK configuration failed.' }
& $cmake --build $build --target install --parallel 6
if ($LASTEXITCODE -ne 0) { throw 'SDK build failed.' }
