# Build guide

Run commands from the repository folder on Windows x64. For installation and
play instructions, see the [README](../README.md).

## Build the installer

```powershell
./installer/build.ps1
```

Output: `out/installer/Lollipop Chainsaw Setup.exe`. Packaging uses the .NET
Framework compiler included with Windows; the manual toolchain is not needed.
Source: [installer/](../installer/).

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
| `out/build/win-amd64-release/logs/` | Game logs; override with `--log_file` |
| `cache/shader-precompile.log` | Prepare shaders log |

The installed launcher caches its source kit under
`%LOCALAPPDATA%/Lollipop Chainsaw Recomp/installer/`.

## Setup and build problems

| Problem | Action |
| --- | --- |
| Interrupted setup or Windows restart | Reopen setup and choose the same destination. Use Update / Repair if the disc is already imported; otherwise supply the same ISO. Check `out/launcher/latest.log`. |
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
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ./installer/build.ps1
```

## Updating or removing an installation

Close the game and launcher. Open the newer setup, choose the existing install
folder and select Update / Repair. An imported disc does not need the ISO again.
The update preserves saves, settings and bindings. It builds and tests a staged
copy before replacing playable files; rerun Update / Repair after an interruption.

Back up `user/` before moving or removing the installation. Build output and
caches can be recreated. For game issues, see [troubleshooting](COMPATIBILITY.md).
