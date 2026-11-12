# Changelog

## Unreleased

- Added a Windows installer with ISO import, verified downloads and resumable setup.
- Added startup shader preparation, a launcher precompile action and a pipeline
  inventory collected across stage/boss maps and prologue combat.
- Fixed damaged shader-cache recovery and launches with shader storage disabled.
- Reduced checkpoint loading overhead and cached keyboard artwork for reuse.
- Unified Options with display, graphics, audio, camera and controller settings,
  including pause handling and display rollback.
- Added 30, 60, 90 and 120 FPS limits, with 1440p / 60 FPS defaults.
- Added keyboard/mouse controls and live device prompts; fixed held-modifier
  movement, blaster shortcuts and QTE labels.
- Fixed input polling, rendering synchronization and texture-copy bounds.
- Fixed texture eviction racing with CPU callbacks and corrupting memory-watch
  handles.
- Fixed controller deadzones ignoring left/down drift and disabling themselves
  at 100%.
- Added Quit Game to PC settings with a confirmation that defaults to Cancel.
- Added Update / Repair without reimporting the ISO, with staged builds,
  interrupted-update recovery and preservation of saves and settings.
- Added optional GPU draw and shader diagnostics for investigating reload crashes.
- Moved installer source into [installer/](installer/), removed redundant script
  wrappers and simplified the README and guides.

See [compatibility and known issues](docs/COMPATIBILITY.md) for current limitations.
