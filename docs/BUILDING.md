# Build guide

The current kit targets **Windows x64**. A Python or shell wrapper alone
would not add macOS or Linux support; those platforms need separate runtime,
graphics and build validation.

## Requirements

- [PowerShell 7.2 or newer](https://github.com/PowerShell/PowerShell/releases).
- Git for Windows, available on PATH.
- Visual Studio 2022 Build Tools with **Desktop development with C++**, the
  MSVC x64 libraries, a Windows SDK, and CMake/Ninja tools.
- [LLVM 20.1.8 for Windows x64](https://github.com/llvm/llvm-project/releases/tag/llvmorg-20.1.8).
- A supported Xbox 360 disc dump that you provide locally.
- Enough free space for the extracted game, SDK build and translated C++.

The scripts initialize the Visual Studio environment for their own process.
Use `pwsh`, not Windows PowerShell 5.

## Local directories

Paths are relative to the source folder; no particular drive letter or account
name is required:

```text
your-work-folder/
├── Lollipop Chainsaw Recomp/       this repository
├── LollipopChainsaw-private/
│   ├── game/                       extracted game, including default.xex
│   ├── generated/                  translated C++; generated locally
│   ├── user/                       saves and runtime settings
│   └── cache/                      shader/pipeline caches
└── LollipopChainsaw-tools/
    ├── llvm-20.1.8/bin/             clang.exe and clang++.exe
    ├── rexglue-source/             pinned SDK source
    ├── extract-xiso/artifacts/     optional extract-xiso.exe
    └── rexglue-sdk-0.10.1-lollipop/win-amd64/  installed patched SDK
```

Install or unpack LLVM into the location above. The scripts use that exact
toolchain location. Keep the repository and sibling directories together if
moving the working folder. Existing CMake caches contain absolute paths and
must be regenerated after relocation.

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
separately and specify its executable, or put it in the tools directory above:

```powershell
./prepare-game.ps1 -InputPath "D:/Disc Dumps/Lollipop Chainsaw.iso" -ExtractXiso "D:/Tools/extract-xiso.exe"
```

Unpack `.7z` or `.zip` archives first. Import preserves the original input,
checks the executable and refuses to overwrite an existing private `game/`
directory. Failed extraction remains in a private `import-*` folder for inspection.
To verify files already in the expected location, pass that folder as `-InputPath`.

## 2. Build the patched SDK

From the repository directory:

```powershell
git clone --branch v0.10.1 --depth 1 https://github.com/rexglue/rexglue-sdk.git ../LollipopChainsaw-tools/rexglue-source
./build-sdk.ps1
```

Expected SDK commit: `bd833a2ab69d305f99ddf57418fc38ffec8af93d`.
The script applies [the SDK patch](../patches/rexglue-fixes.patch), fetches the
required submodules and installs a separate Release SDK. Conflicting local
SDK source changes are rejected.

## 3. Recompile and run

```powershell
./build.ps1
./run.ps1
```

`build.ps1` validates the executable, generates code, compiles and runs the
checks. The locally generated game executable is
`out/build/win-amd64-release/lollipop.exe`. Prefer `run.ps1`: it supplies the
private game, save and cache paths plus the tested profile.

New scenes may create shader pipelines. Offline Xbox LIVE dialogs can be
dismissed by declining the connection attempt and confirming offline play.

## Updating and removal

Keep `LollipopChainsaw-private/user/` to preserve saves, and keep the original
disc dump separately. Generated code and build products can be rebuilt and are
excluded from the source kit. SDK version changes require fresh patch and
address validation.

## Repository scope

This is a WIP source repository, with no release package or installer. Share
only the repository contents; keep game data, generated translations, saves,
SDK tools and local build output private.
