#requires -Version 7.2
param(
    [string]$GameDirectory = (Join-Path $PSScriptRoot 'game'),
    [string]$SdkDirectory = (Join-Path $PSScriptRoot 'out/sdk'),
    [string]$GeneratedDirectory = (Join-Path $PSScriptRoot 'out/generated'),
    [string]$BuildDirectory = (Join-Path $PSScriptRoot 'out/build/win-amd64-release'),
    [string]$LlvmDirectory
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/scripts/build-tools.ps1"
$sdk = [IO.Path]::GetFullPath($SdkDirectory)
$game = [IO.Path]::GetFullPath($GameDirectory)
$generated = [IO.Path]::GetFullPath($GeneratedDirectory)
$build = [IO.Path]::GetFullPath($BuildDirectory)
if (-not (Test-Path -LiteralPath "$sdk/bin/rexglue.exe")) { throw 'Run build-sdk.ps1 or supply -SdkDirectory.' }
$tools = Get-BuildTools -LlvmDirectory $LlvmDirectory
& $tools.CMake "-DGAME_XEX=$game/default.xex" -P "$PSScriptRoot/tests/verify_input.cmake"
if ($LASTEXITCODE -ne 0) { throw 'The input executable does not match the verified build.' }
$manifest = Join-Path $build 'codegen.toml'
Write-CodegenManifest -Template "$PSScriptRoot/lollipop_manifest.toml" -Output $manifest `
    -GameDirectory $game -GeneratedDirectory $generated
& "$sdk/bin/rexglue.exe" codegen $manifest
if ($LASTEXITCODE -ne 0) { throw 'ReXGlue code generation failed. See the analysis log.' }
$configureArgs = @()
$cache = Join-Path $build 'CMakeCache.txt'
if ([IO.File]::Exists($cache)) {
    # A newer packaged installer may resume this build from a different source kit.
    $cachedSource = Select-String -LiteralPath $cache -Pattern '^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)$'
    if ($cachedSource -and [IO.Path]::GetFullPath($cachedSource.Matches[0].Groups[1].Value) -ne $PSScriptRoot) {
        $configureArgs += '--fresh'
    }
}
& $tools.CMake @configureArgs -S $PSScriptRoot -B $build -G Ninja "-DCMAKE_MAKE_PROGRAM=$($tools.Ninja)" `
    "-DCMAKE_CXX_COMPILER=$($tools.Clangxx)" "-DCMAKE_PREFIX_PATH=$sdk" `
    "-Drexglue_DIR=$sdk/lib/cmake/rexglue" "-DLOLLIPOP_GAME_DIR=$game" `
    "-DLOLLIPOP_GENERATED_DIR=$generated" "-DLOLLIPOP_MANIFEST=$manifest" -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& $tools.CMake --build $build --parallel 6
if ($LASTEXITCODE -ne 0) { throw 'Compilation failed.' }
& $tools.CTest --test-dir $build --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Project tests failed.' }
