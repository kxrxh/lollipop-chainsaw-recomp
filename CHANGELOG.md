# Changelog

## Unreleased · WIP

- Unified the original Options menu with display, graphics, audio, camera and controller settings.
- Added per-user Apply/Discard, automatic pause/resume and a 15-second display rollback timer.
- Added 30/60/90/120 FPS limits with matching engine/vblank pacing and legacy-profile migration.
- Added comic-style settings artwork, native control icons and mouse, keyboard and controller navigation.
- Added RePOP-style keyboard/mouse controls, mouse deadzone compensation and live input-device hints.
- Fixed modifier-held movement, Shift+LMB blaster input and vertical QTE key prompts.
- Preserved physical controllers in the optional test bridge and added settings/input regression checks.
- Initial Windows x64 recompilation kit for the verified Xbox 360 executable.
- 1440p / 60 FPS profile, 16× anisotropic filtering and engine timing hooks.
- Fixed concurrent input polling, UI paint waits and D3D12 fence registration.
- Added async pipeline synchronization and texture-copy bounds fixes.
- Skipped draws that cannot affect output, reducing queue stalls and animation jumps.
- Added optional gameplay controls, native capture and frame/Tick logging.
- Added ROM import validation and build documentation.

Known limitations are recorded in [compatibility](docs/COMPATIBILITY.md).
