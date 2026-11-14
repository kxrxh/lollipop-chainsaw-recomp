#requires -Version 7.2
param(
    [string]$SourceDirectory = (Join-Path $PSScriptRoot 'out/tools/rexglue-source'),
    [string]$InstallDirectory = (Join-Path $PSScriptRoot 'out/sdk'),
    [string]$BuildDirectory = (Join-Path $PSScriptRoot 'out/build/sdk'),
    [string]$LlvmDirectory
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/scripts/build-tools.ps1"
. "$PSScriptRoot/scripts/sdk-package.ps1"
$sdkIdentity = Get-SdkIdentity -Root $PSScriptRoot
$tools = Get-BuildTools -LlvmDirectory $LlvmDirectory
$source = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($SourceDirectory)
$install = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($InstallDirectory)
$build = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($BuildDirectory)
if (-not (Test-Path -LiteralPath $source)) {
    New-Item -ItemType Directory -Path (Split-Path $source -Parent) -Force | Out-Null
    # Publish a complete checkout so an interrupted clone can be retried.
    $staging = Join-Path (Split-Path $source -Parent) ('sdk-clone-' + [Guid]::NewGuid().ToString('N'))
    & git clone --progress --branch v0.10.1 --depth 1 https://github.com/rexglue/rexglue-sdk.git $staging
    if ($LASTEXITCODE -ne 0) { throw 'ReXGlue source checkout failed.' }
    [IO.Directory]::Move($staging, $source)
}
$commit = & git -C $source rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $commit -ne (Get-SdkCommit)) {
    throw 'ReXGlue source must be checked out at v0.10.1 (bd833a2). See docs/BUILDING.md.'
}
& git -C $source submodule update --init --progress --depth 1 --jobs 8 `
    thirdparty/cli11 thirdparty/libmspack thirdparty/FFmpeg thirdparty/tomlplusplus `
    thirdparty/simde thirdparty/xxHash thirdparty/spdlog thirdparty/fmt thirdparty/utfcpp `
    thirdparty/imgui thirdparty/sdl3 thirdparty/o1heap thirdparty/inja
if ($LASTEXITCODE -ne 0) { throw 'SDK dependency checkout failed.' }
foreach ($patchName in 'rexglue-fixes.patch','rexglue-shader-precompile.patch') {
    $patch = Join-Path $PSScriptRoot "patches/$patchName"
    & git -C $source apply --reverse --check $patch 2>$null
    if ($LASTEXITCODE -ne 0) {
        & git -C $source apply --check $patch
        if ($LASTEXITCODE -ne 0) { throw 'SDK patch conflicts with local source changes.' }
        & git -C $source apply $patch
        if ($LASTEXITCODE -ne 0) { throw 'Failed to apply SDK fixes.' }
    }
}
& $tools.CMake -S $source -B $build -G Ninja "-DCMAKE_MAKE_PROGRAM=$($tools.Ninja)" `
    "-DCMAKE_C_COMPILER=$($tools.Clang)" "-DCMAKE_CXX_COMPILER=$($tools.Clangxx)" `
    "-DCMAKE_INSTALL_PREFIX=$install" -DCMAKE_BUILD_TYPE=Release `
    -DREXGLUE_ENABLE_TRACY=OFF -DREXGLUE_USE_VULKAN=OFF -DREXGLUE_BUILD_TESTS=OFF -DSDL_TEST_LIBRARY=OFF
if ($LASTEXITCODE -ne 0) { throw 'SDK configuration failed.' }
& $tools.CMake --build $build --target install --parallel 6
if ($LASTEXITCODE -ne 0) { throw 'SDK build failed.' }

$prefix = $install
$runtime = Join-Path $env:VCToolsRedistDir 'x64/Microsoft.VC143.CRT'
foreach ($name in 'msvcp140.dll','msvcp140_atomic_wait.dll','vcruntime140.dll','vcruntime140_1.dll') {
    if (-not (Test-Path -LiteralPath (Join-Path $runtime $name))) { throw "Missing redistributable runtime: $name" }
}
Get-ChildItem -LiteralPath $runtime -File -Filter '*.dll' | Copy-Item -Destination (Join-Path $prefix 'bin') -Force
$licenses = Join-Path $prefix 'licenses'
[IO.Directory]::CreateDirectory((Join-Path $licenses 'rexglue')) | Out-Null
Copy-Item -LiteralPath (Join-Path $source 'LICENSE') -Destination (Join-Path $licenses 'rexglue/LICENSE') -Force
foreach ($dependency in Get-ChildItem -LiteralPath (Join-Path $source 'thirdparty') -Directory) {
    $notices = Get-ChildItem -LiteralPath $dependency.FullName -File |
        Where-Object Name -Match '^(LICENSE|COPYING|COPYRIGHT|UNLICENSE)'
    if ($notices) {
        $target = Join-Path $licenses $dependency.Name
        [IO.Directory]::CreateDirectory($target) | Out-Null
        $notices | Copy-Item -Destination $target -Force
    }
}
$patchLicenses = Join-Path $licenses 'lollipop-patches'
[IO.Directory]::CreateDirectory($patchLicenses) | Out-Null
foreach ($name in 'rexglue-fixes.patch','rexglue-shader-precompile.patch','LICENSE.rexglue','LICENSE.libmspack','SSX-RexGlue-LICENSE.txt') {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot "patches/$name") -Destination $patchLicenses -Force
}
$sourceInfo = @(
    'ReXGlue SDK v0.10.1',
    ('Source: https://github.com/rexglue/rexglue-sdk/tree/' + $commit),
    'Local patches and their notices: licenses/lollipop-patches/',
    'Build instructions: https://github.com/kxrxh/lollipop-chainsaw-recomp/blob/main/docs/BUILDING.md',
    'Microsoft runtime terms: https://visualstudio.microsoft.com/license-terms/vs2022-ga-diagnosticbuildtools/',
    '', 'Pinned submodules:'
)
$sourceInfo += & git -C $source submodule status
if ($LASTEXITCODE -ne 0) { throw 'Could not record the SDK dependency revisions.' }
[IO.File]::WriteAllText((Join-Path $licenses 'SOURCES.txt'), ($sourceInfo -join "`n") + "`n", [Text.UTF8Encoding]::new($false))
if ($sdkIdentity -ne (Get-SdkIdentity -Root $PSScriptRoot)) { throw 'SDK patches changed during the build. Rebuild before packaging.' }
[IO.File]::WriteAllText((Join-Path $prefix 'lollipop-sdk.id'), $sdkIdentity, [Text.UTF8Encoding]::new($false))
