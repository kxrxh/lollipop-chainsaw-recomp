# Compatibility and troubleshooting

Tested on Windows x64 with Direct3D 12 ROV, an i7-10700K, 32 GB RAM and an
RTX 3080 at 2560×1440. Checks cover boot, menus, settings and prologue combat
through the third rescue and helicopter crash. A full playthrough is unverified.

## Known issues

- D3D12 GPU hangs have occurred during reloads, Continue and the phone-call
  transition after the third rescue. Texture eviction fixes passed diagnostic
  checks; normal story traversal through that transition remains unverified.
- Loading and cutscene transitions can stall.
- Shader combinations missing from the inventory can pause while compiling.
- Physical controllers, multiple monitors, other disc versions and Vulkan need
  more testing. Automated controller checks use injected input.
- macOS and Linux are unverified.

## Frame rate and display

The default is 60 FPS. Options also offers 30, 90 and 120 FPS; changing the limit
requires a restart. Physics, quick-time events and cutscenes at 90/120 FPS are
unverified. Prerecorded movies may stay at 30 FPS.

Borderless fullscreen uses the desktop resolution. Window resolution applies
in windowed mode; Render resolution sets internal resolution. VSync, monitor
refresh and GPU load affect the displayed frame rate.

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

Options opens PC settings. Quit Game is available there and on the main menu;
confirmation defaults to No. Cancelling returns to the previous menu. Quitting
loses progress since the last checkpoint.

For controller drift, raise Movement deadzone or Camera deadzone in Controls
until the drift stops. A value of 100% disables that stick.

## Shader preparation

Startup prepares recorded shaders from the disc and reuses the local cache on
later launches. Damaged caches are rebuilt automatically. To prepare separately,
use the launcher's Prepare shaders action or:

```powershell
./run.ps1 -PrecompileShaders
```

Preparation requires D3D12 ROV and `store_shaders=true`. Ordinary launches with
`--store_shaders=false` skip it.

### Shader coverage

The inventory covers all seven stages, boss maps and opening/ending/result
scenes. Coverage is incomplete for outfits, boss phases and effect combinations.
The engine-generated loading copy shader compiles on first use and can be
prepared from the local cache afterward. See
[pipeline collection](DEVELOPMENT.md#shader-preparation-and-collection).

## Gameplay problems

### Shader preparation fails

Check `cache/shader-precompile.log` for the Prepare shaders action. Ordinary
launch logs are listed in [files and logs](BUILDING.md#files-and-logs).
To check with a fresh cache and explicit renderer settings:

```powershell
./run.ps1 -CacheDirectory out/cache-check -PrecompileShaders --render_target_path_d3d12=rov --store_shaders=true
```

Choose an unused cache directory to preserve the existing one.

### Graphics crash or slow scenes

Try 60 FPS and a lower Render resolution. Report the game log, GPU, settings
and reproduction steps. For D3D12 crash diagnostics:

```powershell
./run.ps1 --d3d12_debug=true --gpu_debug_markers=true
```

Diagnostics can slow rendering. The optional Direct3D debug layer may be absent;
DRED GPU progress records can still work. To compare the RTV renderer:

```powershell
./run.ps1 --render_target_path_d3d12=rtv --readback_memexport=true
```

RTV may be slower.

### Keyboard and controller prompts do not update

Restart after rebinding keys. Mixed input needs memory-export readback enabled
and delayed readback disabled:

```powershell
./run.ps1 --readback_memexport=true --readback_memexport_fast=false
```

Use `./run.ps1 --no-mnk_mode` for controllers with the original Xbox artwork.
Mouse sensitivity is in Options.

### PC settings are unusable or do not persist

Check folder write access and command-line or environment overrides. To reset
PC preferences, close the game and rename `user/pc-settings.toml` to a backup
filename. The next launch restores defaults and imports native options.
Keep the rest of `user/` to preserve game saves.

For install and compiler errors, see
[build troubleshooting](BUILDING.md#setup-and-build-problems).
