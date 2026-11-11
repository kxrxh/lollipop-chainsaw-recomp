# Compatibility and performance

**WIP — experimental development build, not a finished release.**

## Tested configuration

Windows x64, Intel Core i7-10700K, 32 GB RAM, NVIDIA GeForce RTX 3080 (10 GB).
ReXGlue 0.10.1 with the included patch, Direct3D 12 ROV, 2× resolution scale
from 720p guest mode, 16× anisotropic filtering and a 60 Hz guest mode.
CPU memexport readback is disabled in the launcher profile.

Boot, menus, prologue movement, attacks, dismemberment, fire and scoring have
been observed. Latest manual playtesting also reported smooth gameplay. Full-game
progression and other hardware configurations have not been tested.

## Measurements

Before the no-effect draw fix, an enemy-combat probe reached a 99.21 ms swap
interval and a 91.21 ms engine Tick delta. The CPU blocked at the three-frame
GPU queue limit. Draw bursts included thousands of calls unable to modify
color, depth, stencil or memory.

The SDK now skips those calls while preserving submission bookkeeping and
active occlusion-query draws. In a later approximately 40-second combat probe,
the character survived and the score increased to 4000:

| Measurement | Result |
| --- | --- |
| Swap samples | 2,460 |
| Mean interval / effective rate | 16.666 ms / 60.00 FPS |
| 99th percentile swap interval | 19.55 ms |
| Maximum swap interval | 20.53 ms |
| Intervals above 25 ms | 0 |
| GPU fence waits | 0 |
| Maximum engine Tick delta | 17.30 ms |
| No-effect draws skipped | 1,702,447 |

Scenes evolved and were not an identical replay. Intervals come from the SDK's
guest-frame boundary, not display-presentation or GPU timestamps. These results
describe the tested prologue, not the whole game. Prerecorded movies may remain
30 FPS.

### 90 / 120 FPS modes

Graphics settings also offer 90 and 120 FPS limits. Both require a restart so
the engine limit and guest vblank cadence change together. In native-720p menu
probes with VSync enabled, 450 samples at the 90 FPS limit averaged 90.05 FPS;
600 samples at the 120 FPS limit averaged 120.00 FPS. A 90 FPS VSync-off probe
averaged 89.99 FPS. These are guest-frame measurements, not monitor presentation
rates. Game time advanced at normal speed, and saving/restarting preserved 90.

These limits do not guarantee that every scene reaches the target. Shader
compilation, GPU load, VSync and monitor refresh affect the displayed rate.
Full-game physics, QTE and cutscene behavior above 60 FPS remain unverified.

## Known issues

- A rare D3D12 ROV `DEVICE_HUNG` occurred after Continue. Three subsequent
  death/Continue cycles with diagnostic instrumentation passed, but the
  original failure has not been conclusively fixed.
- Some loading and cutscene transitions still produce long GPU queue waits.
- Other disc revisions, full-game progression, Vulkan, macOS and Linux are unverified.

For a renderer compatibility comparison:

```powershell
./run.ps1 --render_target_path_d3d12=rtv --readback_memexport=true
```

RTV was slower in earlier measurements. It is a diagnostic alternative,
not a verified equivalent performance profile.
