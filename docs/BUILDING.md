# Build guide

Run commands from the repository folder on Windows x64. For installation and
play instructions, see the [README](../README.md).

## Build the launcher

```powershell
./build-sdk.ps1
./launcher/build.ps1
```

Output: `out/launcher/Lollipop Chainsaw Launcher.exe`. The script fetches the pinned
.NET 10 SDK into `out/tools/` and restores the locked NuGet packages into `out/nuget/`.
It publishes a self-contained Windows x64 executable; players do not need to install
.NET. Build the patched SDK once using the manual build requirements below.
The launcher embeds that SDK, its runtime DLLs, a minimal LLVM 20.1.8 package and
dependency notices in the EXE. The compiler package keeps `clang`, `clang++`,
`lld-link`, the LLVM library tools and builtin headers; debuggers, sanitizers and
unrelated target tools are omitted. Identical executable aliases share archive data.
To use an SDK built elsewhere, pass `-SdkDirectory <folder>` to `launcher/build.ps1`.
Use `-LlvmDirectory <LLVM bin or root>` if LLVM 20.1.8 is not on PATH or under Program Files.
Packaging rejects an SDK whose patch identity differs from the bundled game sources.

Open [Lollipop.Launcher.slnx](../launcher/Lollipop.Launcher.slnx) in an IDE for the
WPF application and its test project. Build and publish output stays under `out/launcher/`.
UI-only IDE builds and launcher tests can run without an SDK; installing a game
requires the packaged launcher produced by `launcher/build.ps1`.

`VERSION` sets the bundled recomp version; `launcher/VERSION` independently sets
the launcher version. Updates shows both alongside the installed game version.
Development builds use a `-dev` suffix. Stable recomp release tags use `vMAJOR.MINOR.PATCH`; update checking
uses the public GitHub latest-release API. Release downloads open in the browser.
The launcher never replaces itself while running.

Setup installs `Lollipop Chainsaw Launcher.exe` in the game folder. Keep
**Create a desktop shortcut to the launcher** checked during setup, or use
**Create desktop shortcut** in Manage game afterward. The shortcut opens the
launcher; select Play there to start the game.

The launcher remembers the last selected game folder across restarts and updates
kept in the same folder. Its preference is stored beside the launcher in
`launcher-data/game-location.txt`. Older preferences are read once from LocalAppData.
A launcher installed inside a game folder uses that installation. If a saved folder
is unavailable, use Locate game to select it again.

### Portable player setup

The launcher includes a prebuilt ReXGlue SDK in `out/launcher/sdk.zip`, embedded in
the published EXE. Setup unpacks it into `out/tools/sdk/<archive hash>/` in the game
folder and reuses it on repair. The package includes the code generator, runtime
and GPU DLLs, headers, libraries, CMake files and dependency notices. No game files
or generated game translations are included. The player does not need Git, and
setup does not build the SDK.

Setup reuses compatible installed compiler tools without modifying them. Otherwise it
unpacks the bundled minimal LLVM from `out/launcher/llvm.tar.xz` into the game folder's
`out/tools/packages/`. Missing CMake, Ninja, Microsoft C++ headers/libraries and Windows
SDK files are downloaded from pinned official packages into that same private folder.
No Build Tools, SDK or VC++ Redistributable installer is run; administrator rights
and global PATH changes are not needed. Runtime DLLs are copied beside the game
and SDK executables. Microsoft tools retain their linked license terms in setup.

Keep the launcher in a writable folder. Its source kit and remembered location stay
under `launcher-data/` beside its EXE. The self-contained .NET runtime may use the
Windows temporary directory while running. Game files, saves and settings remain
inside the selected game folder.

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
C++, compiles and runs CTest. Normal launches start the game directly and reuse
cached shaders. Shader preparation is optional; see
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
| Preparing game tools failed | Download the complete launcher again. The SDK is bundled; no SDK compilation or Git download is needed. Check `out/launcher/latest.log`. |
| Install folder rejected | Choose an empty folder for a new install. Close the game and other launcher windows; check write access. |
| Unsupported or incomplete disc | Compare the executable hash above. Re-extract if it differs or `AshGame/` or `Engine/` is missing. |
| Tool download failed in the launcher | Check internet access and free space, then retry. Verified packages are reused. No system installation is needed. |
| Missing compiler in a manual build | Check the manual build requirements above. Supply `-LlvmDirectory` if needed. |
| SDK patch conflict | Use new SDK source and build directories; see below. |
| Paths refer to an old checkout | Rerun `build.ps1`. Use a new build directory for an SDK built at the old location. |
| Ninja reports `build.ninja` still dirty after 100 tries | CMake is stuck regenerating its build files. Future-dated inputs can cause this; check Windows date/time synchronization. Use the updated launcher and retry setup or Repair. Launcher builds explicitly configure before compiling and disable automatic regeneration, so archived source or SDK timestamps cannot cause this loop. Manual developer builds keep automatic regeneration enabled. |

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
