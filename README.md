<p align="center">
  <img src="assets/banner.png" alt="Lollipop Chainsaw Recomp" width="100%">
</p>

<h1 align="center">Lollipop Chainsaw Recomp · WIP</h1>

<p align="center">Xbox 360 → Windows x64 · ReXGlue 0.10.1 · Direct3D 12</p>

<p align="center">
  <a href="docs/BUILDING.md">Build guide</a> ·
  <a href="docs/COMPATIBILITY.md">Compatibility</a> ·
  <a href="docs/DEVELOPMENT.md">Development</a> ·
  <a href="CHANGELOG.md">Changelog</a>
</p>

**Work in progress.** A recompilation kit for the original Xbox 360 **Lollipop Chainsaw**.
Turn your own supported disc dump into a locally built Windows executable.
This repository provides the host code, recompilation manifest and SDK fixes;
the game's executable and assets are supplied by you.

### What is included

- Ahead-of-time PowerPC recompilation through [ReXGlue](https://github.com/rexglue/rexglue-sdk).
- A 1440p / 60 FPS launch profile, with 16× anisotropic filtering.
- Input synchronization, UI wait and graphics fixes in a reproducible SDK patch.
- Local frame analysis and optional background gameplay controls for development.

**Windows x64 is the currently tested platform.** macOS and Linux support are
not implemented or verified. This is a source kit, without an installer or a
precompiled game executable. Graphics still use the SDK's Xenos emulation.

### Build your copy

Install the prerequisites in the [build guide](docs/BUILDING.md), then run
these commands from PowerShell 7 in the project directory:

```powershell
# Import an extracted disc folder (or your ISO with extract-xiso).
./prepare-game.ps1 -InputPath "D:/Games/My Lollipop Chainsaw Dump"

# Build the patched SDK once, then recompile and launch the game.
./build-sdk.ps1
./build.ps1
./run.ps1
```

The build accepts one verified disc executable. Check the
[supported version and SHA-256](docs/BUILDING.md#supported-game) before starting.
Game data, generated translations, saves and shader caches stay outside the
repository in a private sibling directory.

### Current status · WIP

Boot, menus, prologue movement and combat have been tested at 2560×1440.
The latest measured combat interval sustained 60 FPS with no interval above
25 ms; full-game compatibility remains unverified. A rare ROV device-loss
failure after Continue and some loading-transition stalls remain known issues.
See [compatibility and measurements](docs/COMPATIBILITY.md) for the test limits.

### Repository layout

```text
assets/       README artwork
docs/         Build, compatibility and development guides
patches/      ReXGlue fixes and upstream license notices
scripts/      Benchmark, frame analysis and local test controls
src/          Host application and timing hooks
tests/        Input, timing, bridge and tooling checks
```

### Credits and license

Lollipop Chainsaw Recomp by [kxrxh](https://github.com/kxrxh).
Redistributed versions and forks must retain this credit and link under the
[project Attribution License](LICENSE).

Built with ReXGlue and its Xenia foundations, with fixes adapted from
PM.RAZOR's SSX work. Full notices and artwork attribution are in
[THIRD_PARTY.md](THIRD_PARTY.md). This is an unofficial project.
