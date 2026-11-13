# Build guide

Run commands from the repository folder on Windows x64. For installation and
play instructions, see the [README](../README.md).

## Build the launcher

```powershell
./launcher/build.ps1
```

Output: `out/launcher/Lollipop Chainsaw Launcher.exe`. The script fetches the pinned
.NET 10 SDK into `out/tools/` and restores the locked NuGet packages into `out/nuget/`.
It publishes a self-contained Windows x64 executable; players do not need to install
.NET. The C++ toolchain is not needed to build the launcher.

Open [Lollipop.Launcher.slnx](../launcher/Lollipop.Launcher.slnx) in an IDE for the
WPF application and its test project. Build and publish output stays under `out/launcher/`.

`VERSION` sets the bundled recomp version; `launcher/VERSION` independently sets
the launcher version. Updates shows both alongside the installed game version.
Development builds use a `-dev` suffix. Stable recomp release tags use `vMAJOR.MINOR.PATCH`; update checking
uses the public GitHub latest-release API. Release downloads open in the browser.
The launcher never replaces itself while running.

Setup installs `Lollipop Chainsaw Launcher.exe` in the game folder. Keep
**Create a desktop shortcut to the launcher** checked during setup, or use
**Create desktop shortcut** in Manage game afterward. The shortcut opens the
launcher; select Play there to start the game.

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
and installs to `out/sdk/`. The game build validates the executable, generates
C++, compiles and runs CTest. Launch prepares shaders before starting the game.

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
| `out/tools/`, `out/sdk/` | Downloaded tools and installed SDK |
| `out/build/` | SDK, game and test builds |
| `out/build/win-amd64-release/lollipop.exe` | Playable game |
| `out/launcher/latest.log` | Setup and launcher log |
| `out/build/win-amd64-release/recomp-version.txt` | Installed recomp version, published with the game |
| `out/build/win-amd64-release/logs/` | Game logs; override with `--log_file` |
| `cache/shader-precompile.log` | Prepare shaders log |

The installed launcher caches its source kit under
`%LOCALAPPDATA%/Lollipop Chainsaw Recomp/installer/`.

## Setup and build problems

| Problem | Action |
| --- | --- |
| Interrupted setup or Windows restart | Reopen the launcher, select Locate game and choose the same destination. Use Repair game if the disc is already imported; otherwise supply the same ISO in Manage game. Check `out/launcher/latest.log`. |
| Preparing game tools appears idle | Compiler and dependency configuration can run without a percentage. Check the current activity or `out/launcher/latest.log`; a completed Git percentage only describes that download. |
| Install folder rejected | Choose an empty folder for a new install. Close the game and other launcher windows; check write access. |
| Unsupported or incomplete disc | Compare the executable hash above. Re-extract if it differs or `AshGame/` or `Engine/` is missing. |
| Missing compiler or tools | Check the requirements above. Supply `-LlvmDirectory` if needed. |
| SDK patch conflict | Use new SDK source and build directories; see below. |
| Paths refer to an old checkout | Rerun `build.ps1`. Use a new build directory for an SDK built at the old location. |

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

Close the game and launcher. Open the newer launcher, select Locate game and
choose the existing install folder. In Manage game, select Apply launcher build / Repair.
An imported disc does not need the ISO again.
The update preserves saves, settings and bindings. It builds the game in a staged
folder and checks required runtime files before replacing playable files.
Player installations skip developer tests and tools; manual development builds
run the full test suite. Rerun Repair game after an interruption.

Back up `user/` before moving or removing the installation. Build output and
caches can be recreated. For game issues, see [troubleshooting](COMPATIBILITY.md).
