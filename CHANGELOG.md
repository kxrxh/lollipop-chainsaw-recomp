# Changelog

## 0.1.2 — 2026-11-14

- Launcher 0.2.5 adds **Updates → Update launcher** with verified downloads and
  automatic restart. Updating the launcher leaves the installed game unchanged.
- Reuse unchanged translations, compiled objects and precompiled headers across
  game updates. SDK and compiler packages keep the same cache paths when repacked.
- Reject damaged downloads and launcher downgrades. Preserve saves, settings and
  the selected game folder.

## 0.1.1 — 2026-11-14

- Bundle a ready-to-use ReXGlue SDK and runtime DLLs with the launcher. Setup
  compiles only the game from the player's ISO, without downloading Git or building the SDK.
- Bundle minimal LLVM 20.1.8 with the compiler, linker and builtin headers, instead
  of downloading the complete LLVM distribution on the player's computer.
- Setup reuses compatible installed tools and downloads missing build tools privately,
  without installing Visual Studio Build Tools, Windows SDK or VC++ Redistributable.
- Keep C++ runtime DLLs with the game and launcher state beside the launcher, with
  import of the previously remembered game folder.
- Automatically prepare recorded shaders before D3D12 ROV gameplay, with an animated
  progress screen and cache reuse on later launches. Preparation without launching
  the game remains available as a separate action.
- Prepare the engine loading-copy shader without a prior play session, and extend
  the recorded prologue shader combinations.
- Handle chunked streaming copies and export rebasing without unnecessary GPU readbacks.
- Prefetch translated executable code before launch to reduce first-use paging stalls.
- Prevent archived source or SDK timestamps from trapping Ninja in a regeneration loop.

## 0.1.0 — 2026-11-13

- Remember the selected game folder across launcher restarts and updates.
- Launch through Lollipop Chainsaw Launcher, with desktop shortcuts available during setup and in Manage game.
- Removed manual smoke scripts, the scripted gameplay benchmark and unused launcher assets.
- Render changelogs and release notes as Markdown with headings, lists, code and links.
- Modernized the WPF launcher into an SDK-style .NET 10 solution with compiled XAML,
  separate application and test projects, and assets under `launcher/assets/`.
- Reworked the installer into a launcher with full-window comic artwork, floating
  navigation, game management, version display and GitHub release checking.
- Added an in-app help modal and separate launcher and recomp version tracking.
- Added launcher display and graphics settings using the existing game profile,
  with preservation of audio, controls and custom resolutions.
- Show the active build phase and CMake output instead of leaving a completed
  download percentage on screen during configuration.
- Player installations build only the game and its runtime dependencies, without
  compiling developer tests or shipping their sources.
- Added a Windows installer with ISO import, verified downloads and resumable setup.
- Added startup shader preparation, a launcher precompile action and a pipeline
  inventory collected across stage/boss maps and prologue combat.
- Expanded shader preparation with Star Soul, the third rescue and the helicopter
  sequence.
- Fixed damaged shader-cache recovery and launches with shader storage disabled.
- Retry brief file-sharing locks when publishing shader caches.
- Reduced checkpoint loading overhead and cached keyboard artwork for reuse.
- Unified Options with display, graphics, audio, camera and controller settings,
  including pause handling and display rollback.
- Added 30, 60, 90 and 120 FPS limits, with 1440p / 60 FPS defaults.
- Added keyboard/mouse controls and live device prompts; fixed held-modifier
  movement, blaster shortcuts and QTE labels.
- Fixed input polling, rendering synchronization and texture-copy bounds.
- Fixed texture eviction racing with CPU callbacks and corrupting memory-watch
  handles.
- Invalidated cached texture bindings when mid-frame GPU waits reclaim textures.
- Fixed controller deadzones ignoring left/down drift and disabling themselves
  at 100%.
- Held triggers stay blocked after leaving PC settings until released, preventing
  accidental Star Soul activation or blaster fire.
- Added Quit Game to the main menu and PC settings with native confirmation that
  defaults to No, including controller support while input is captured.
- Added Update / Repair without reimporting the ISO, with staged builds,
  interrupted-update recovery and preservation of saves and settings.
- Added optional GPU draw and shader diagnostics for investigating reload crashes.
- Moved installer source into [launcher/](launcher/), removed redundant script
  wrappers and simplified the README and guides.

See [compatibility and known issues](docs/COMPATIBILITY.md) for current limitations.
