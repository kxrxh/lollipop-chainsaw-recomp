# Build guide

Run commands from the repository folder on Windows x64. For installation and
play instructions, see the [README](../README.md).

## Build the launcher

```powershell
./build-sdk.ps1
./launcher/build.ps1
```

Output: `out/launcher/Lollipop Chainsaw Launcher.exe`, a self-contained Windows x64
app with the patched ReXGlue SDK, runtime DLLs, minimal LLVM 20.1.8 and dependency
notices. Players do not need .NET installed.

The script downloads the pinned .NET SDK into `out/tools/` and restores locked NuGet
packages into `out/nuget/`. Pass `-SdkDirectory <folder>` for an existing patched SDK,
or `-LlvmDirectory <LLVM bin or root>` if LLVM is not on PATH or under Program Files.
The SDK's patch identity must match the game sources.

Open [Lollipop.Launcher.slnx](../launcher/Lollipop.Launcher.slnx) in an IDE for the
WPF application and its test project. Build and publish output stays under `out/launcher/`.
UI-only IDE builds and launcher tests can run without an SDK; installing a game
requires the packaged launcher produced by `launcher/build.ps1`.

`VERSION` sets the bundled recomp version; `launcher/VERSION` sets the launcher
version. Stable release tags use `vMAJOR.MINOR.PATCH`; development builds use `-dev`.

Setup installs the launcher in the game folder. Create a desktop shortcut during
setup or from **Manage game → Create desktop shortcut**. The shortcut opens the
launcher; select **Play** to start the game.

The selected game folder is saved in `launcher-data/game-location.txt` beside the
launcher. A launcher inside an installation uses that game folder. Older preferences
are imported from LocalAppData. Use **Locate game** if the saved folder has moved.

### Portable player setup

The **Costumes & DLC** page imports Xbox 360 STFS costume packages for title
`565707D0`, from ZIPs or recursively from folders (including Xbox content-tree
folders). Imports are staged in `out/launcher/dlc/`, validated with the bundled
ReXGlue container reader, and installed under
`user/0000000000000000/565707D0/00000002/`, with metadata in the adjacent
`Headers/00000002/` folder. Installed packs are shared across local profiles.
Existing packages are kept; installing does not replace disc files or saves.
Close the game before installing, then restart it to load the packs.
The recomp opens installed content through the runtime and passes it to UE3's
native DLC manager at the main menu, so local packs load without Xbox LIVE.

Setup extracts the bundled SDK into `out/tools/sdk/<archive hash>/` and reuses it
on repair. It compiles the game locally from your disc; Git and an SDK build are
not required. The launcher contains no game files or generated translations.

Compatible installed tools are reused. Bundled LLVM is extracted into
`out/tools/packages/` by the launcher's bundled .NET runtime, without requiring
`tar.exe` or `xz` on the player's computer. Missing CMake, Ninja, Microsoft C++
headers/libraries and Windows SDK files are downloaded there from pinned official
packages. Runtime DLLs are copied beside the game and SDK executables. No system
installers or administrator rights are required. Microsoft license terms are linked in setup.

Keep the launcher in a writable folder. `launcher-data/` holds its source cache and
preferences; game files, saves and settings stay in the selected installation. The
.NET runtime may extract files to the Windows temporary directory.

## Supported game

| Field | Supported value |
| --- | --- |
| Platform | Xbox 360 |
| Title ID | `565707D0` |
| Media ID | `31BE13D9` |
| Executable version | `0.0.0.2` |
| `default.xex` SHA-256 | `A687F47D537B011600146C665E59F5D2B4D4243284799D84FEE7DA45CDD2DF26` |

Setup and manual builds reject other executables.

## Manual build

### Requirements

- PowerShell 7.2 or newer (`pwsh`) and Git on PATH.
- Visual Studio 2022 Build Tools: x64 C++ tools, Windows SDK, CMake and Ninja.
- [LLVM 20.1.8](https://github.com/llvm/llvm-project/releases/tag/llvmorg-20.1.8).
- Python 3 on PATH for the SDK shader toolchain. Pass `-PythonExecutable` if needed.
- The supported disc files.

The scripts initialize the Visual Studio environment. If LLVM is not on PATH,
pass `-LlvmDirectory "C:/Program Files/LLVM/bin"` to either build script.

### Import the game

For an extracted folder containing `default.xex`, `AshGame/` and `Engine/`:

```powershell
./prepare-game.ps1 -InputPath "D:/Disc Dumps/Lollipop Chainsaw"
```

For an ISO, install [extract-xiso](https://github.com/XboxDev/extract-xiso):

```powershell
./prepare-game.ps1 -InputPath "D:/Disc Dumps/Lollipop Chainsaw.iso" -ExtractXiso "D:/Tools/extract-xiso.exe"
```

The importer validates the executable and writes to `game/`. It preserves the
source and refuses to overwrite an existing destination. Partial imports stay
in an `import-*` folder beside the destination. Unpack archives before importing.
To validate an existing import, run `./prepare-game.ps1 -InputPath ./game`.

### Build and run

```powershell
./build-sdk.ps1
./build.ps1
./run.ps1
```

The SDK script downloads ReXGlue 0.11.0 at
`a3e1d82678ca20d68d4837a8e9ee4652ff073745`, applies the patches in `patches/`,
and installs to `out/sdk-0.11.0/`. It fetches the pinned Vulkan 1.4.313 headers
and Vulkan Memory Allocator 3.3.0 for the Windows SDK build. The new
versioned SDK source, build and install directories leave earlier checkouts
and caches intact. `build.ps1` validates the executable, updates generated
C++, builds changed sources and runs CTest. Unchanged translations and compiled
objects are reused. For launch behavior and shader caches, see
[shader preparation](COMPATIBILITY.md#shader-preparation).

The launcher and run script continue to use the installation's user and cache
directories. Existing saves and their headers remain readable in place. Direct
EXE launches default to `Documents/lollipop` for saves and the user directory's
`cache` subfolder. Advanced configuration is loaded from `lollipop.toml` beside
the EXE.

### Custom paths

```powershell
./build.ps1 -GameDirectory "E:/Games/My Disc Dump" -SdkDirectory "E:/SDKs/rexglue"
./run.ps1 -GameDirectory "E:/Games/My Disc Dump"
```

| Script | Path parameters |
| --- | --- |
| `prepare-game.ps1` | `-InputPath`, `-Destination`, `-ExtractXiso` |
| `build-sdk.ps1` | `-SourceDirectory`, `-InstallDirectory`, `-BuildDirectory`, `-LlvmDirectory`, `-PythonExecutable` |
| `build.ps1` | `-GameDirectory`, `-SdkDirectory`, `-GeneratedDirectory`, `-BuildDirectory`, `-LlvmDirectory` |
| `run.ps1` | `-GameDirectory`, `-UserDirectory`, `-CacheDirectory`, `-BuildDirectory` |

Use the same `-BuildDirectory` for build and launch. Append runtime flags after
path options, for example `./run.ps1 --lollipop_fps=60`.

Direct CMake builds need `rexglue_DIR`, `LOLLIPOP_GAME_DIR`,
`LOLLIPOP_GENERATED_DIR` and `LOLLIPOP_MANIFEST`. `build.ps1` writes a manifest
with the chosen paths into its build directory.

## Files and logs

Paths are relative to the checkout or chosen install folder.

| Location | Contents |
| --- | --- |
| `game/` | Imported disc files |
| `user/` | Saves and `pc-settings.toml` |
| `cache/` | Shader and UI artwork caches |
| `out/generated/` | Generated C++ |
| `out/source/` | Stable game source kit used by launcher builds |
| `out/tools/sdk/` | Bundled SDK unpacked by player setup |
| `out/tools/`, `out/sdk-0.11.0/` | Compiler packages and the developer-built SDK |
| `out/build/` | SDK, game and test builds |
| `out/build/win-amd64-release/lollipop.exe` | Playable game |
| `out/build/win-amd64-release/lollipop_shader_tool.exe` | Exports a pipeline report from the local shader cache |
| `out/launcher/latest.log` | Setup and launcher log |
| `out/build/win-amd64-release/recomp-version.txt` | Installed recomp version, published with the game |
| `out/build/win-amd64-release/logs/` | Game logs; override with `--log_file` |
| `cache/shader-precompile.log` | Prepare shaders log |

The launcher caches its source kit under `launcher-data/source/` beside its EXE.

## Setup and build problems

### Launcher setup

Setup writes its log to `out/launcher/latest.log` in the installation folder.

| Symptom | What to try |
| --- | --- |
| Setup stopped halfway | Reopen the launcher and select the same destination. If the disc was imported, choose **Manage game → Apply launcher build / Repair**. Otherwise, select the same ISO again. |
| Preparing game tools failed | Download a fresh copy of the launcher and retry. |
| A tool download failed | Check your connection and free disk space, then retry. Setup reuses verified downloads. |
| Install folder rejected | For a new install, choose an empty, writable folder. Close the game and any other launcher windows. |
| Unsupported or incomplete disc | Check the [supported disc identifiers](#supported-game). For an extracted copy, verify the `default.xex` hash and that `AshGame/` and `Engine/` are present. Re-extract from the supported disc if needed. |
| Ninja reports `build.ninja` is still dirty after 100 tries | Update the launcher, then run **Repair**. |

### Manual builds

- **Missing compiler.** Check the [required tools](#requirements). If LLVM is installed elsewhere, pass its path with `-LlvmDirectory`.
- **Paths point to an old checkout.** Rerun `build.ps1`. If you moved the SDK, build it in a fresh build directory.
- **Ninja keeps regenerating `build.ninja`.** Check Windows date and time synchronization. Files dated in the future can trigger this loop.

If an SDK patch fails to apply, build in fresh directories to keep the existing checkout intact.

```powershell
./build-sdk.ps1 -SourceDirectory out/tools/rexglue-clean -BuildDirectory out/build/sdk-clean
./build.ps1
```

If PowerShell blocks a script, use a process-local execution-policy override.

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File ./build.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ./launcher/build.ps1
```

## Updating or removing an installation

1. Close the game. In **Updates**, select **Update launcher**. The launcher verifies the download and restarts. On launcher 0.2.4 or earlier, download the latest launcher manually and use **Locate game** to select your installation.
2. Select **Manage game → Apply launcher build / Repair** to update the game itself. Repair reuses the imported disc and preserves saves, settings and bindings. If interrupted, run Repair again.

A new SDK or compiler may require a full rebuild. Keep `out/source/`,
`out/generated/` and `out/build/game-compile/` to reuse previous build work.

Before moving or removing the installation, back up `user/`. It contains your saves and settings.
