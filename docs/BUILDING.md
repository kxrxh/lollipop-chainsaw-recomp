# Build guide

The current kit targets **Windows x64**. A Python or shell wrapper alone
would not add macOS or Linux support; those platforms need separate runtime,
graphics and build validation.

## Requirements

- [PowerShell 7.2 or newer](https://github.com/PowerShell/PowerShell/releases).
- Git for Windows, available on PATH.
- Visual Studio 2022 Build Tools with **Desktop development with C++**, the
  MSVC x64 libraries, a Windows SDK, and CMake/Ninja tools.
- [LLVM 20.1.8 for Windows x64](https://github.com/llvm/llvm-project/releases/tag/llvmorg-20.1.8),
  on PATH or supplied with `-LlvmDirectory`.
- A supported Xbox 360 disc dump that you provide locally.
- Enough free space for the extracted game, SDK build and translated C++.

The scripts initialize the Visual Studio environment for their own process.
Use `pwsh`, not Windows PowerShell 5.

## Local directories

All defaults are inside the checkout. These data and output folders are ignored
by Git; they do not need to exist before setup:

```text
repository/
├── game/                         your extracted disc files
├── user/                         saves and runtime settings
├── cache/                        shader and artwork caches
└── out/
    ├── generated/                translated C++; generated locally
    ├── tools/rexglue-source/     downloaded pinned SDK source
    ├── sdk/                      installed patched SDK
    └── build/
        ├── sdk/                  SDK build output
        └── win-amd64-release/    game executable and tests
```

Scripts resolve their defaults from their own location, including when the
checkout path contains spaces. Existing CMake caches contain absolute paths;
use a fresh build directory after moving a checkout.

## Supported game

| Field | Supported value |
| --- | --- |
| Platform | Xbox 360 |
| Title ID | `565707D0` |
| Media ID | `31BE13D9` |
| Executable version | `0.0.0.2` |
| `default.xex` SHA-256 | `A687F47D537B011600146C665E59F5D2B4D4243284799D84FEE7DA45CDD2DF26` |

Other editions and title updates need separate address validation. The kit
rejects a mismatched executable before code generation.

## 1. Prepare the game

For an extracted disc folder containing `default.xex`, `AshGame/` and `Engine/`:

```powershell
./prepare-game.ps1 -InputPath "D:/Disc Dumps/Lollipop Chainsaw"
```

For an ISO, obtain [extract-xiso](https://github.com/XboxDev/extract-xiso)
separately and put it on PATH or specify its executable:

```powershell
./prepare-game.ps1 -InputPath "D:/Disc Dumps/Lollipop Chainsaw.iso" -ExtractXiso "D:/Tools/extract-xiso.exe"
```

Unpack `.7z` or `.zip` archives first. Import preserves the original input,
checks the executable and refuses to overwrite an existing `game/` directory.
Failed extraction remains in an ignored `import-*` folder for inspection.
To verify files already in the expected location, pass that folder as `-InputPath`.
Use `-Destination` to select another import location.

## 2. Build the patched SDK

From the repository directory:

```powershell
./build-sdk.ps1
```

Expected SDK commit: `bd833a2ab69d305f99ddf57418fc38ffec8af93d`.
The script clones the pinned public SDK when absent, applies
[the SDK patch](../patches/rexglue-fixes.patch), fetches the required submodules
and installs a Release SDK in `out/sdk/`. Conflicting local SDK source changes
are rejected. If LLVM is not on PATH, pass its bin directory:

```powershell
./build-sdk.ps1 -LlvmDirectory "C:/Program Files/LLVM/bin"
./build.ps1 -LlvmDirectory "C:/Program Files/LLVM/bin"
```

## 3. Recompile and run

```powershell
./build.ps1
./run.ps1
```

`build.ps1` validates the executable, generates code, compiles and runs the
checks. The locally generated game executable is
`out/build/win-amd64-release/lollipop.exe`. Prefer `run.ps1`: it supplies the
checkout's game, save and cache paths plus the tested profile.

Existing installations can be reused without copying data or changing the
repository's manifest. For example:

```powershell
./build.ps1 -GameDirectory "E:/Games/My Disc Dump" -SdkDirectory "E:/SDKs/rexglue"
./run.ps1 -GameDirectory "E:/Games/My Disc Dump"
```

`build.ps1` also accepts `-GeneratedDirectory` and `-BuildDirectory`;
`build-sdk.ps1` accepts `-SourceDirectory`, `-InstallDirectory` and
`-BuildDirectory`. Custom build directories contain their own game executable.
`run.ps1` accepts `-GameDirectory`, `-UserDirectory` and `-CacheDirectory`;
use these for filesystem paths and append native flags such as `--lollipop_fps=90`.
For direct CMake use, set `LOLLIPOP_GAME_DIR`, `LOLLIPOP_GENERATED_DIR` and
`rexglue_DIR` as needed. A configured codegen manifest is written only to build
output; the checked-in manifest retains portable defaults.

New scenes may create shader pipelines. Offline Xbox LIVE dialogs can be
dismissed by declining the connection attempt and confirming offline play.

## Updating and removal

Keep `user/` (or your custom save directory) to preserve saves, and keep the original
disc dump separately. Generated code and build products can be rebuilt and are
excluded from the source kit. SDK version changes require fresh patch and
address validation.

## Repository scope

This is a WIP source repository, with no release package or installer. Share
only the repository contents; keep game data, generated translations, saves,
SDK tools and local build output private.
