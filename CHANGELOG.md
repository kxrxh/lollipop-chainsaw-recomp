# Changelog

## Unreleased · WIP

- Initial Windows x64 recompilation kit for the verified Xbox 360 executable.
- 1440p / 60 FPS profile, 16× anisotropic filtering and engine timing hooks.
- Fixed concurrent input polling, UI paint waits and D3D12 fence registration.
- Added async pipeline synchronization and texture-copy bounds fixes.
- Skipped draws that cannot affect output, reducing queue stalls and animation jumps.
- Added optional gameplay controls, native capture and frame/Tick logging.
- Added ROM import validation and build documentation.

Known limitations are recorded in [compatibility](docs/COMPATIBILITY.md).
