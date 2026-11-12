# Build guide

Commands below run from the repository folder on Windows x64.
For install and play instructions, see the [README](../README.md).

## Build the installer

Run this in PowerShell:

```powershell
./installer/build.ps1
```

Open `out/installer/Lollipop Chainsaw Setup.exe`, choose your ISO and an install
folder, then select Install game. Setup downloads missing tools, imports the
disc and builds the game. Windows may ask for administrator approval or a restart.
To resume, open setup again and choose the same ISO and folder.

Packaging uses the .NET Framework compiler included with Windows. You do not
need to install the manual build tools below to package the installer.
The installer source and layout are in [installer/](../installer/).

## Supported game

| Field | Supported value |
| --- | --- |
| Platform | Xbox 360 |
| Title ID | `565707D0` |
| Media ID | `31BE13D9` |
| Executable version | `0.0.0.2` |
| `default.xex` SHA-256 | `A687F47D537B011600146C665E59F5D2B4D4243284799D84FEE7DA45CDD2DF26` |

Setup and the manual build reject other executables. Other disc editions and
updates need their own code and hook validation.

## Manual build

### Requirements

- [PowerShell 7.2 or newer](https://github.com/PowerShell/PowerShell/releases).
- Git for Windows on PATH.
- Visual Studio 2022 Build Tools with the x64 C++ tools, Windows SDK and
  CMake/Ninja components.
- [LLVM 20.1.8](https://github.com/llvm/llvm-project/releases/tag/llvmorg-20.1.8),
  on PATH or supplied with `-LlvmDirectory`.
- The supported disc files and space for the SDK, generated code and game build.

Use `pwsh` for the manual build. The scripts set up the Visual Studio environment
automatically; a Developer Command Prompt is not required.

### Import the game

For an extracted folder containing `default.xex`, `AshGame/` and `Engine/`:

```powershell
./prepare-game.ps1 -InputPath "D:/Disc Dumps/Lollipop Chainsaw"
```

For an ISO, install [extract-xiso](https://github.com/XboxDev/extract-xiso)
separately and pass its path, or put it on PATH:

```powershell
./prepare-game.ps1 -InputPath "D:/Disc Dumps/Lollipop Chainsaw.iso" -ExtractXiso "D:/Tools/extract-xiso.exe"
```

Unpack archives before importing. The importer checks the game version and
copies or extracts into `game/`. It leaves the source intact and refuses to
overwrite an existing destination. Any partial import remains in an `import-*`
folder beside the destination for inspection.

To check files already in `game/`, run:

```powershell
./prepare-game.ps1 -InputPath ./game
```

### Build the SDK

```powershell
./build-sdk.ps1
```

This downloads ReXGlue 0.10.1 at commit
`bd833a2ab69d305f99ddf57418fc38ffec8af93d`, applies the
[SDK fixes](../patches/rexglue-fixes.patch) and
[shader-preparation patch](../patches/rexglue-shader-precompile.patch), and
installs the SDK in `out/sdk/`.

If LLVM is not on PATH, supply the folder containing `clang.exe` and `clang++.exe`:

```powershell
./build-sdk.ps1 -LlvmDirectory "C:/Program Files/LLVM/bin"
```

### Build and run the game

```powershell
./build.ps1
./run.ps1
```

`build.ps1` checks the executable, generates code, compiles and runs CTest.
It also accepts `-LlvmDirectory` if needed.

The game executable is `out/build/win-amd64-release/lollipop.exe`. `run.ps1`
sets the game, save and cache paths. The first launch prepares shaders before
starting the game; see [shader preparation](COMPATIBILITY.md#shader-preparation)
if it fails.

### Custom paths

You can use an existing disc folder and SDK without importing them again:

```powershell
./build.ps1 -GameDirectory "E:/Games/My Disc Dump" -SdkDirectory "E:/SDKs/rexglue"
./run.ps1 -GameDirectory "E:/Games/My Disc Dump"
```

| Script | Path parameters |
| --- | --- |
| `prepare-game.ps1` | `-InputPath`, `-Destination`, `-ExtractXiso` |
| `build-sdk.ps1` | `-SourceDirectory`, `-InstallDirectory`, `-BuildDirectory`, `-LlvmDirectory` |
| `build.ps1` | `-GameDirectory`, `-SdkDirectory`, `-GeneratedDirectory`, `-BuildDirectory`, `-LlvmDirectory` |
| `run.ps1` | `-GameDirectory`, `-UserDirectory`, `-CacheDirectory`, `-BuildDirectory` |

Pass the same `-BuildDirectory` to `run.ps1` when using a custom game build.
Append native game flags after the path options, for example
`./run.ps1 --lollipop_fps=60`.

For direct CMake builds, set `rexglue_DIR`, `LOLLIPOP_GAME_DIR`,
`LOLLIPOP_GENERATED_DIR` and `LOLLIPOP_MANIFEST` to match your installation.
`build.ps1` writes the configured manifest to its build directory; the repository's
manifest keeps its portable defaults.

## Files and logs

Paths below are relative to the checkout or the destination chosen in setup.
Custom path options change the corresponding locations.

| Location | Contents |
| --- | --- |
| `game/` | Imported disc files |
| `user/` | Saves and `pc-settings.toml` |
| `cache/` | Shader and keyboard artwork caches |
| `out/generated/` | Generated C++ translations |
| `out/tools/` | Downloaded SDK source and setup tools |
| `out/sdk/` | Installed SDK |
| `out/build/sdk/` | SDK build |
| `out/build/win-amd64-release/` | Game executable and tests |
| `out/launcher/latest.log` | Standalone setup and launcher log |
| `out/build/win-amd64-release/logs/` | Game logs, unless `--log_file` is supplied |
| `cache/shader-precompile.log` | Log from the launcher's Precompile shaders action or `run.ps1 -PrecompileShaders` |

The installed launcher can run without the original checkout or setup download.
Its source kit is cached under
`%LOCALAPPDATA%/Lollipop Chainsaw Recomp/installer/`.

## Setup and build problems

### Setup stopped or Windows requested a restart

Restart if requested, then select the same ISO and destination. Completed
downloads and imported game files are reused. Incomplete downloads resume when
the server supports it. Open setup log in the launcher shows the full error;
the file is `out/launcher/latest.log` under the destination.

For a new installation, choose an empty folder. An existing manual build or save
folder is not accepted as a new setup destination.

### The installation is already in use or is not writable

Close the game and its other launcher window. Check that you can write to the
chosen folder, or choose another destination.

### Unsupported or incomplete disc

Compare `default.xex` with the [supported version](#supported-game). Re-extract
the original disc if the hash differs or `AshGame/` or `Engine/` is missing.
An archive file cannot be used directly as the import source.

### LLVM, Visual Studio, CMake or Ninja was not found

For manual builds, check the [requirements](#requirements). Use LLVM 20.1.8 and
pass `-LlvmDirectory` if it is not on PATH. In Visual Studio Installer, add the
x64 C++ tools, Windows SDK and CMake tools components to your Build Tools install.

### The SDK patch conflicts with local changes

For a manual build, use new SDK source and build directories. This keeps the
old checkout available for inspection and still installs to `out/sdk/`:

```powershell
./build-sdk.ps1 -SourceDirectory out/tools/rexglue-clean -BuildDirectory out/build/sdk-clean
./build.ps1
```

### CMake still refers to an old checkout

Rerun `build.ps1` after moving the repository. It refreshes the game build's
configuration when the source folder changes. If an SDK build still refers to
the old path, use a new SDK build directory. Keep custom path arguments consistent
between build and launch commands.

### PowerShell says running scripts is disabled

For a single manual build, run:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File ./build.ps1
```

For installer packaging with Windows PowerShell:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ./installer/build.ps1
```

These commands apply the policy setting to that process.

## Updating or removing an installation

Back up `user/` before reinstalling or moving the game. Keep the original disc
dump separately. Build output and caches can be recreated; saves cannot.
For game crashes, input problems and settings recovery, see
[compatibility and troubleshooting](COMPATIBILITY.md).
