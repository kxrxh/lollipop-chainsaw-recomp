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

Setup extracts the bundled SDK into `out/tools/sdk/<archive hash>/` and reuses it
on repair. It compiles the game locally from your disc; Git and an SDK build are
not required. The launcher contains no game files or generated translations.

Compatible installed tools are reused. Bundled LLVM is extracted into
`out/tools/packages/`; missing CMake, Ninja, Microsoft C++ headers/libraries and
Windows SDK files are downloaded there from pinned official packages. Runtime DLLs
are copied beside the game and SDK executables. No system installers or administrator
rights are required. Microsoft license terms are linked in setup.

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

The SDK script downloads ReXGlue 0.10.1 at
`bd833a2ab69d305f99ddf57418fc38ffec8af93d`, applies the patches in `patches/`,
and installs to `out/sdk/`. `build.ps1` validates the executable, updates generated
C++, builds changed sources and runs CTest. Unchanged translations and compiled
objects are reused. For launch behavior and shader caches, see
[shader preparation](COMPATIBILITY.md#shader-preparation).

### Custom paths

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
| `out/tools/`, `out/sdk/` | Compiler packages and the developer-built SDK |
| `out/build/` | SDK, game and test builds |
| `out/build/win-amd64-release/lollipop.exe` | Playable game |
| `out/launcher/latest.log` | Setup and launcher log |
| `out/build/win-amd64-release/recomp-version.txt` | Installed recomp version, published with the game |
| `out/build/win-amd64-release/logs/` | Game logs; override with `--log_file` |
| `cache/shader-precompile.log` | Prepare shaders log |

The launcher caches its source kit under `launcher-data/source/` beside its EXE.

## Setup and build problems

| Problem | Action |
| --- | --- |
| Interrupted setup | Reopen the launcher and use the remembered destination. Use Repair game if the disc is already imported; otherwise supply the same ISO in Manage game. Check `out/launcher/latest.log`. |
| Preparing game tools failed | Download the complete launcher again and check `out/launcher/latest.log`. |
| Install folder rejected | Choose an empty folder for a new install. Close the game and other launcher windows; check write access. |
| Unsupported or incomplete disc | Compare the executable hash above. Re-extract if it differs or `AshGame/` or `Engine/` is missing. |
| Tool download failed in the launcher | Check internet access and free space, then retry. Verified packages are reused. |
| Missing compiler in a manual build | Check the manual build requirements above. Supply `-LlvmDirectory` if needed. |
| SDK patch conflict | Use new SDK source and build directories; see below. |
| Paths refer to an old checkout | Rerun `build.ps1`. Use a new build directory for an SDK built at the old location. |
| Ninja reports `build.ninja` still dirty after 100 tries | Update the launcher and retry Repair. For manual builds, check Windows date/time synchronization; future-dated inputs can cause a CMake regeneration loop. |

To build the SDK from a clean checkout without deleting the old one:

```powershell
./build-sdk.ps1 -SourceDirectory out/tools/rexglue-clean -BuildDirectory out/build/sdk-clean
./build.ps1
```

If PowerShell blocks scripts, use a process-local execution-policy override:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File ./build.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ./launcher/build.ps1
```

## Updating or removing an installation

Close the game, then select **Updates → Update launcher**. It checks the download's
size and SHA-256, replaces the launcher after it exits, and restarts in the same
folder. Older published versions cannot replace a newer local launcher. Downloads
and the update helper are cached in `launcher-data/updates/`.

With launcher 0.2.4 or earlier, download the new EXE once manually. Use **Locate game**
if needed. Later launcher updates can be installed with the button.

To update the game, select **Manage game → Apply launcher build / Repair**. The ISO
is not needed again. Setup checks a staged build before replacing playable files;
saves, settings and bindings are kept. After an interruption, run Repair again.

Unchanged translations and objects are reused. The first update from an older
launcher, or a change of SDK/compiler, may require a full rebuild. Keep `out/source/`,
`out/generated/` and `out/build/game-compile/` to retain the build cache. Updating
only the launcher does not rebuild the game.

Back up `user/` before moving or removing the installation. Build output and
caches can be recreated. For game issues, see [troubleshooting](COMPATIBILITY.md).
