#requires -Version 7.2
param(
    [string]$SourceDirectory = (Join-Path $PSScriptRoot 'out/tools/rexglue-source'),
    [string]$InstallDirectory = (Join-Path $PSScriptRoot 'out/sdk'),
    [string]$BuildDirectory = (Join-Path $PSScriptRoot 'out/build/sdk'),
    [string]$LlvmDirectory
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/scripts/build-tools.ps1"
$tools = Get-BuildTools -LlvmDirectory $LlvmDirectory
$source = [IO.Path]::GetFullPath($SourceDirectory)
if (-not (Test-Path -LiteralPath $source)) {
    New-Item -ItemType Directory -Path (Split-Path $source -Parent) -Force | Out-Null
    & git clone --branch v0.10.1 --depth 1 https://github.com/rexglue/rexglue-sdk.git $source
    if ($LASTEXITCODE -ne 0) { throw 'ReXGlue source checkout failed.' }
}
$commit = & git -C $source rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $commit -ne 'bd833a2ab69d305f99ddf57418fc38ffec8af93d') {
    throw 'ReXGlue source must be checked out at v0.10.1 (bd833a2). See docs/BUILDING.md.'
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
& $tools.CMake -S $source -B $BuildDirectory -G Ninja "-DCMAKE_MAKE_PROGRAM=$($tools.Ninja)" `
    "-DCMAKE_C_COMPILER=$($tools.Clang)" "-DCMAKE_CXX_COMPILER=$($tools.Clangxx)" `
    "-DCMAKE_INSTALL_PREFIX=$([IO.Path]::GetFullPath($InstallDirectory))" -DCMAKE_BUILD_TYPE=Release `
    -DREXGLUE_ENABLE_TRACY=OFF -DREXGLUE_USE_VULKAN=OFF -DREXGLUE_BUILD_TESTS=OFF
if ($LASTEXITCODE -ne 0) { throw 'SDK configuration failed.' }
& $tools.CMake --build $BuildDirectory --target install --parallel 6
if ($LASTEXITCODE -ne 0) { throw 'SDK build failed.' }
