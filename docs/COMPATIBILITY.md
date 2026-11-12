# Compatibility and troubleshooting

Windows x64 with Direct3D 12 is the tested platform. Full story progression,
other disc versions, Vulkan, macOS and Linux remain unverified.

## Tested configuration

Recorded gameplay checks used an Intel Core i7-10700K, 32 GB RAM and an NVIDIA
GeForce RTX 3080, at 2560×1440 with the D3D12 ROV renderer. Boot, menus, prologue
movement and combat have been checked. Settings checks covered saving,
discarding changes, display rollback and pausing.

Keyboard/controller prompt switching was checked in menus and the prologue's
gate interaction. Automated controller tests use injected input; physical
controllers and multi-monitor setups need more testing.

## Known issues

- A rare graphics crash after Continue reported D3D12 `DEVICE_HUNG`. Its cause
  has not been fixed conclusively.
- Loading and cutscene transitions can stall.
- Unrecorded shader combinations can cause pauses while they compile.
- Heavy automated camera and streaming tests have also caused GPU hangs.

## Frame rate and display

The default frame limit is 60 FPS. Graphics settings also offer 30, 90 and
120 FPS; changing the limit requires a restart. The 90 and 120 FPS modes have
had menu timing checks, but full-game physics, quick-time events and cutscenes
at those rates remain unverified. Prerecorded movies may stay at 30 FPS.

VSync, monitor refresh and GPU load affect the displayed frame rate. Borderless
fullscreen uses the desktop resolution. Window resolution changes apply in
windowed mode; Render resolution controls the game's internal resolution.

## Controls

| Action | Default key |
| --- | --- |
| Move | WASD |
| Camera | Mouse or arrow keys |
| Chainsaw slash / interact | Left mouse |
| Pom-pom bash | Right mouse |
| Low slash | Middle mouse |
| Dodge / leapfrog | Space |
| Lock-on | Q |
| Chainsaw dash | E |
| Nick roulette | R |
| Star mode | V |
| Aim blaster / fire | Hold Shift / Shift + left mouse |
| Recover / use lollipop | C or 1 |
| D-pad down, left, right | 2, 3, 4 |
| Reset camera | F |
| Help | Tab |
| Pause / cutscene skip dialog | Esc |
| Confirm menus / skip | Enter or middle mouse |
| Cancel menus | Backspace or Space |

Options and F4 open PC settings. Close PC settings before pressing F6 to open
advanced settings and rebind keys. Restart after rebinding to refresh the prompt
artwork. During cutscenes, Esc opens the skip dialog and Enter confirms it.

## Shader preparation

Startup prepares the recorded shaders before gameplay. The first scan reads
shader programs from your disc files; later launches reuse the local library.
Damaged local caches are rebuilt automatically.

The launcher also has a Precompile shaders action. From a manual build:

```powershell
./run.ps1 -PrecompileShaders
```

Precompilation requires D3D12 ROV and `store_shaders=true`. Ordinary launches
with `--store_shaders=false` skip preparation.

### Shader coverage

The inventory was collected across all seven stages, boss maps and
opening/ending/result scenes. Checks included fresh-cache preparation and normal
rendering at the playable and boss map entries. A complete story playthrough,
every outfit, boss phase and combat-effect combination remain unverified.

The engine-generated loading copy shader is compiled locally on its first use.
Later launches can prepare it from that local cache. Instructions for recording
new combinations are in [pipeline collection](DEVELOPMENT.md#shader-preparation-and-collection).

## Gameplay problems

### Shader preparation fails

For the launcher's Precompile shaders action or the command above, check
`cache/shader-precompile.log`. Ordinary launch logs are listed in the
[build guide](BUILDING.md#files-and-logs).

Check for renderer or shader-storage overrides in advanced settings. To compare
with a fresh cache, choose an unused directory and run:

```powershell
./run.ps1 -CacheDirectory out/cache-check -PrecompileShaders --render_target_path_d3d12=rov --store_shaders=true
```

This keeps the existing cache and saves in place.

### Graphics crash or slow scenes

Try the 60 FPS limit and a lower Render resolution, then reproduce the same scene.
For a renderer comparison in a manual build:

```powershell
./run.ps1 --render_target_path_d3d12=rtv --readback_memexport=true
```

RTV is a diagnostic alternative and may be slower. When reporting a problem,
include the game log, GPU, graphics settings and the steps that led to it.

### Keyboard and controller prompts do not update

Restart after changing key bindings. For mixed input, memory-export readback
must be enabled and delayed readback disabled:

```powershell
./run.ps1 --readback_memexport=true --readback_memexport_fast=false
```

To use controllers with the original Xbox artwork:

```powershell
./run.ps1 --no-mnk_mode
```

Mouse sensitivity is available in Options and F6's advanced input settings.

### PC settings are unusable or do not persist

Check that the install folder is writable. Command-line and environment settings
can override saved preferences.

To reset PC preferences, close the game and rename `user/pc-settings.toml` to a
backup filename. The next launch uses default PC settings and imports native
options from the save. Keep the rest of `user/` to preserve native game saves.

For setup, disc import and compiler errors, see the
[build troubleshooting steps](BUILDING.md#setup-and-build-problems).
