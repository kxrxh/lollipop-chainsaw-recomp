#requires -Version 7.2
param(
    [string]$SourceDirectory = (Join-Path $PSScriptRoot 'out/tools/rexglue-source-0.11.0'),
    [string]$InstallDirectory = (Join-Path $PSScriptRoot 'out/sdk-0.11.0'),
    [string]$BuildDirectory = (Join-Path $PSScriptRoot 'out/build/sdk-0.11.0'),
    [string]$LlvmDirectory,
    [string]$PythonExecutable
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/scripts/build-tools.ps1"
. "$PSScriptRoot/scripts/sdk-package.ps1"
$sdkIdentity = Get-SdkIdentity -Root $PSScriptRoot
$tools = Get-BuildTools -LlvmDirectory $LlvmDirectory
if (-not $PythonExecutable) {
    $python = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($python) { $PythonExecutable = $python.Source }
}
if (-not $PythonExecutable) { throw 'Install Python 3 on PATH or pass -PythonExecutable.' }
$pythonPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($PythonExecutable)
if (-not [IO.File]::Exists($pythonPath)) { throw "Python executable not found: $pythonPath" }
$version = & $pythonPath --version
if ($LASTEXITCODE -ne 0 -or $version -notmatch '^Python 3\.') {
    throw 'glslang needs Python 3. Install it on PATH or pass -PythonExecutable.'
}
$source = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($SourceDirectory)
$install = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($InstallDirectory)
$build = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($BuildDirectory)
function Initialize-PinnedRepository {
    param([string]$Directory, [string]$Url, [string]$Tag, [string]$Commit)
    if (-not (Test-Path -LiteralPath $Directory)) {
        $parent = Split-Path $Directory -Parent
        [IO.Directory]::CreateDirectory($parent) | Out-Null
        # Publish a complete checkout so an interrupted clone can be retried.
        $staging = Join-Path $parent ('sdk-clone-' + [Guid]::NewGuid().ToString('N'))
        & git clone --progress --branch $Tag --depth 1 $Url $staging
        if ($LASTEXITCODE -ne 0) { throw "Checkout failed: $Url at $Tag." }
        $head = & git -C $staging rev-parse HEAD
        if ($LASTEXITCODE -ne 0 -or $head -ne $Commit) { throw "Unexpected revision for $Url at $Tag." }
        [IO.Directory]::Move($staging, $Directory)
    }
    $head = & git -C $Directory rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or $head -ne $Commit) {
        throw "$Directory must be checked out at $Tag ($Commit). See docs/BUILDING.md."
    }
}

$commit = Get-SdkCommit
Initialize-PinnedRepository -Directory $source -Url 'https://github.com/rexglue/rexglue-sdk.git' -Tag 'v0.11.0' -Commit $commit
$vulkan = Join-Path $PSScriptRoot 'out/tools/vulkan-headers-1.4.313'
$vulkanCommit = '409c16be502e39fe70dd6fe2d9ad4842ef2c9a53'
Initialize-PinnedRepository -Directory $vulkan -Url 'https://github.com/KhronosGroup/Vulkan-Headers.git' `
    -Tag 'vulkan-sdk-1.4.313.0' -Commit $vulkanCommit
$vma = Join-Path $PSScriptRoot 'out/tools/vulkan-memory-allocator-3.3.0'
$vmaCommit = '1d8f600fd424278486eade7ed3e877c99f0846b1'
Initialize-PinnedRepository -Directory $vma -Url 'https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git' `
    -Tag 'v3.3.0' -Commit $vmaCommit
& git -C $source submodule update --init --progress --depth 1 --jobs 8 `
    thirdparty/cli11 thirdparty/libmspack thirdparty/FFmpeg thirdparty/tomlplusplus `
    thirdparty/simde thirdparty/xxHash thirdparty/spdlog thirdparty/imgui thirdparty/sdl3 `
    thirdparty/tlsf thirdparty/excrypt thirdparty/glslang thirdparty/inja
if ($LASTEXITCODE -ne 0) { throw 'SDK dependency checkout failed.' }
foreach ($patchName in (Get-SdkPatches)) {
    $patch = Join-Path $PSScriptRoot "patches/$patchName"
    & git -C $source apply --reverse --check $patch 2>$null
    if ($LASTEXITCODE -ne 0) {
        & git -C $source apply --check $patch
        if ($LASTEXITCODE -ne 0) { throw "SDK patch does not match the checkout: $patchName." }
        & git -C $source apply $patch
        if ($LASTEXITCODE -ne 0) { throw "Failed to apply SDK patch: $patchName." }
    }
}
& $tools.CMake -S $source -B $build -G Ninja "-DCMAKE_MAKE_PROGRAM=$($tools.Ninja)" `
    "-DCMAKE_C_COMPILER=$($tools.Clang)" "-DCMAKE_CXX_COMPILER=$($tools.Clangxx)" `
    "-DCMAKE_INSTALL_PREFIX=$install" -DCMAKE_BUILD_TYPE=Release `
    -DREXGLUE_ENABLE_TRACY=OFF -DREXGLUE_BUILD_TESTS=OFF -DREXGLUE_REGISTER_PACKAGE=OFF `
    "-DPYTHON_EXECUTABLE=$pythonPath" "-DVulkan_INCLUDE_DIR=$vulkan/include" "-DREXGLUE_VMA_INCLUDE_DIR=$vma/include"
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
        Where-Object {
            $_.Name -match '^(LICENSE|COPYING|COPYRIGHT|UNLICENSE)' -or
                ($dependency.Name -eq 'tlsf' -and $_.Name -eq 'tlsf.h')
        }
    if ($notices) {
        $target = Join-Path $licenses $dependency.Name
        [IO.Directory]::CreateDirectory($target) | Out-Null
        $notices | Copy-Item -Destination $target -Force
    }
}
foreach ($dependency in @($vulkan, $vma)) {
    $target = Join-Path $licenses (Split-Path $dependency -Leaf)
    [IO.Directory]::CreateDirectory($target) | Out-Null
    Get-ChildItem -LiteralPath $dependency -File -Filter 'LICENSE*' | Copy-Item -Destination $target -Force
}
Copy-Item -LiteralPath (Join-Path $vulkan 'LICENSES') -Recurse -Force `
    -Destination (Join-Path $licenses (Split-Path $vulkan -Leaf))
$patchLicenses = Join-Path $licenses 'lollipop-patches'
[IO.Directory]::CreateDirectory($patchLicenses) | Out-Null
foreach ($name in ((Get-SdkPatches) + @('LICENSE.rexglue','LICENSE.libmspack','SSX-RexGlue-LICENSE.txt'))) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot "patches/$name") -Destination $patchLicenses -Force
}
$sourceInfo = @(
    'ReXGlue SDK v0.11.0',
    ('Source: https://github.com/rexglue/rexglue-sdk/tree/' + $commit),
    ('Vulkan Headers: https://github.com/KhronosGroup/Vulkan-Headers/tree/' + $vulkanCommit),
    ('Vulkan Memory Allocator: https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/tree/' + $vmaCommit),
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
