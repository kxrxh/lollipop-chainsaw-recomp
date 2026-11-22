# Compatibility and troubleshooting

A player reported completing the prologue and all six stages on Windows. They saw occasional stutter, some cutscene dialogue drifted out of sync, and a few surfaces flickered in Stage 4. [Playthrough report](https://github.com/kxrxh/lollipop-chainsaw-recomp/issues/6#issuecomment-7075747322) · [Stage 4 report](https://github.com/kxrxh/lollipop-chainsaw-recomp/issues/6#issuecomment-7002269759)

The tested configuration is Windows x64, Direct3D 12 ROV, an i7-10700K, 32 GB of RAM and an RTX 3080 at 2560×1440. Setup requires the [supported Xbox 360 disc](BUILDING.md#supported-game). Linux, macOS, multi-monitor setups and physical controllers have limited or no test coverage.

## Known issues

- **First-use stutter.** A new effect can briefly freeze the picture while its graphics pipeline compiles. Reports include explosions and cutscenes. If it happens again on the same effect, mention that in your report.
- **Stage 4 scenery flicker.** Some objects in the digital sections flicker at 1440p. A screenshot or clip showing the surface would help identify it.
- **Reload hangs.** Earlier builds could hang on Continue, Restart or the phone call after the third prologue rescue. Recent checkpoint tests passed. If a hang recurs, include the checkpoint and the game log.

## Gameplay problems

For setup, disc import or compiler errors, see [setup and build problems](BUILDING.md#setup-and-build-problems). For a gameplay issue, include your game version, GPU, frame-rate limit, render resolution, and the steps that trigger it. Game logs are in `out/build/win-amd64-release/logs/` under the installation folder.

### Shader preparation

The first Direct3D 12 ROV launch can spend several minutes preparing shaders. Later launches reuse the cache. To run preparation separately, select **Manage game → Prepare shaders**. From a source checkout, run `./run.ps1 -PrecompileShaders`.

### Shader coverage

The inventory includes effects from all seven stages, including boss transformations, Juliet’s unlockable finishers and barrel explosions. Other moves, outfits and effects can still compile during play.

To report a hitch, close the game and run this command from the installation folder. Replace the quoted text with the stage and effect you saw.

```powershell
./out/build/win-amd64-release/lollipop_shader_tool.exe collect cache stage-pipelines.txt "Stage 5, exploding barrel"
```

Attach `stage-pipelines.txt` to the issue. On older installations without the tool, attach `cache/shaders/shareable/565707D0.rov.d3d12.xpso` instead.

### Shader preparation fails

Check `cache/shader-precompile.log` after using **Prepare shaders**. Preparation requires D3D12 ROV and shader storage. To test from a source checkout, choose an unused cache directory so your existing cache stays intact.

```powershell
./run.ps1 -CacheDirectory out/cache-check -PrecompileShaders --render_target_path_d3d12=rov --store_shaders=true
```

### Graphics crash or slow scenes

Try 60 FPS and a lower **Render resolution**. For a repeatable crash, a source checkout can collect D3D12 diagnostics with `./run.ps1 --d3d12_debug=true --gpu_debug_markers=true`. Debug logging can slow rendering. To compare the RTV renderer, run `./run.ps1 --render_target_path_d3d12=rtv --readback_memexport=true`.

### Keyboard and controller prompts do not update

Restart the game after rebinding keys. For mixed keyboard and controller input, run `./run.ps1 --readback_memexport=true --readback_memexport_fast=false` from a source checkout. For the original Xbox button artwork, use `./run.ps1 --no-mnk_mode`.

### PC settings are unusable or do not persist

Check that the installation folder is writable and that a launch argument or environment variable isn’t overriding the setting. To reset PC preferences, close the game and rename **only** `user/pc-settings.toml` to `pc-settings.toml.bak`. The next launch restores defaults and imports native options. The rest of `user/` contains your saves.

## Frame rate and display

- **Frame rate.** The default is 60 FPS. You can switch to 30, 90 or 120 FPS during play. Physics, QTEs and cutscenes have limited testing above 60 FPS. Prerecorded movies may run at 30 FPS.
- **Resolution.** Borderless fullscreen uses your desktop resolution. **Window resolution** sets the window size. **Render resolution** sets the resolution used to draw the game.
- **Shadows.** **Original** keeps the game’s character shadows, normally capped at 128. The 256, 512 and 1024 settings add detail and increase GPU load. You can switch during play.

## Controls

Adjust mouse sensitivity in **Options**. If a controller stick drifts, raise its movement or camera deadzone until the drift stops. At 100%, that stick is disabled.

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
