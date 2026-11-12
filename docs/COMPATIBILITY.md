# Compatibility and performance

**WIP — experimental development build, not a finished release.**

## Tested configuration

Windows x64, Intel Core i7-10700K, 32 GB RAM, NVIDIA GeForce RTX 3080 (10 GB).
ReXGlue 0.10.1 with the included patch, Direct3D 12 ROV, 2× resolution scale
from 720p guest mode, 16× anisotropic filtering and a 60 Hz guest mode.
The published 60 FPS combat measurement below used CPU memexport readback
disabled. The current default enables synchronous readback for mixed keyboard
and controller prompts.

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

### Mixed-input prompt cost and checkpoint loading

Two 1440p / 60 FPS runs used the same copied prologue checkpoint, warm shader
storage, ROV, VSync and synchronous memexport readback. Each covered idle,
camera movement, movement and attack inputs. The prompt matcher first checks
64 bytes and makes a full byte comparison before replacing a texture.

| Guest-frame measurement | Full-texture hashing | 64-byte sampling |
| --- | ---: | ---: |
| Frames across the four phases | 3,749 | 3,726 |
| Bytes hashed by the prompt matcher | 10,075,897,856 | 11,168,576 |
| Mean time matching prompts per frame | 0.1075 ms | 0.0038 ms |
| Prompt replacements | 2 | 2 |

The matcher times exclude telemetry bookkeeping. The runs used the same
scripted inputs and had slightly different frame counts. Both sustained the
60 FPS guest cap. Switching input devices refreshed the open pause menu and
the school-gate quick-time prompt in place.

Two PIX-instrumented checkpoint loads with mixed input produced guest intervals
of 300 and 317 ms. Synchronous memexport fence waits accounted for 169 and
176 ms of those intervals respectively. Both frames recorded zero pipeline
misses and zero prompt-matching time. A diagnostic replay with readback disabled
still reached 151 ms. These are loading measurements with instrumentation overhead.

The D3D12 checkpoint path now mirrors a verified 64-byte-per-vertex copy shader
when all source pages are current on the CPU. The GPU still runs the draw.
Partial exports and GPU-only sources retain synchronous readback. A validation
run compared 22,180 eligible mirrors with the GPU result and found zero byte
differences. After loading, the open pause menu, school-gate interaction and
gate quick-time prompt all switched between keyboard/mouse and controller.

Two pairs of warm-cache replays used the same executable and copied save.
Only the renderer DLL changed, and the second pair reversed the run order.
The table gives the individual runs in pair order. Timings cover the first
14 seconds after checkpoint confirmation with PIX detached.

| Checkpoint measurement | Original readback | Copy mirror |
| --- | ---: | ---: |
| Cumulative memexport fence wait | 847 / 890 ms | 0.32 / 0 ms |
| Longest guest swap interval | 133 / 185 ms | 117 / 118 ms |
| Longest engine Tick gap | 646 / 666 ms | 676 / 670 ms |
| First observed prologue street image | 8.86 / 8.77 s | 8.54 / 8.66 s |

The street-image check polled screenshots, so its timing is approximate.
The largest engine Tick gap persisted across both pairs.

The game thread also spends time creating Unreal package exports. In a 1.8 s
window of the original PIX trace, 619 of 1,019 game-thread CPU samples included
the export-creation routine at guest address `0x822545D8`. This sample count
includes callees and covers only that part of the load.

### Package decompression during checkpoint loading

The export loop polls for package data while background workers decode LZX
blocks. Two workers accounted for 993 samples in the same translated LZX
routine during that 1.8 s PIX window. The asynchronous package worker now
uses the SDK's native decoder for supported blocks. A replay from boot through
the prologue checkpoint compared 3,838 decoded blocks (349 MiB) against the
guest decoder and found zero byte differences.

Two more pairs of warm-cache runs used the D3D12 copy mirror on both sides.
The 14-second measurements start at checkpoint confirmation, with PIX detached.

| Checkpoint measurement | Guest LZX | Native LZX |
| --- | ---: | ---: |
| Longest engine Tick gap | 655 / 647 ms | 434 / 425 ms |
| First observed prologue street image | 8.62 / 8.65 s | 8.19 / 8.19 s |
| Longest guest swap interval | 101 / 99 ms | 134 / 100 ms |

The screenshot check observed the street roughly 0.4–0.5 s earlier. The
longest guest swap interval varied independently of the engine Tick gap.

In paired 12.5 s PIX captures that both reached the street, CPU samples with
the async decompression job on the stack fell from 2,412 to 1,136. The game
thread still had 1,099 samples in package export creation, including its callees.
An earlier native-decoder capture was still loading at the street check and was
repeated. The wall times above come from the runs with PIX detached.

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

The shader inventory contains 1,916 pipelines from all seven stages, separate boss
maps and final-stage variants, plus opening/ending/result scenes. The automated
sweep checked 760 distinct streaming packages and 14,467 camera positions in four
directions. Each direction waited for three guest swaps. Normal rendering and
offline collection produced identical sets of 656 pipelines in a controlled
school-scene comparison with separate fresh caches.

Fresh-cache preparation completed all 1,916 pipelines without failures. After
merging three additional combinations found by normal rendering, a second normal
replay visited all 14 playable/boss map entries with no new material pipelines or
later-stage pipeline misses. The only additional pipeline was the known native
startup copy shader. Replays used default graphics settings and muted audio.

A complete story playthrough and every outfit, boss phase and combat-effect
combination remain unverified. The engine-generated copy pipeline is prepared
from the user's local cache on later launches; its first compilation occurs during
native startup. These checks cover stage entries and the automated asset sweep;
they do not certify every event in a complete playthrough.

- A rare D3D12 ROV `DEVICE_HUNG` occurred after Continue. Three subsequent
  death/Continue cycles with diagnostic instrumentation passed, but the
  original failure has not been conclusively fixed.
- Some loading and cutscene transitions still produce long GPU queue waits.
- Extreme camera/streaming collection workloads also triggered D3D12 GPU hangs;
  offline pipeline collection avoids issuing their color/depth draws. This mode
  verifies pipeline discovery and creation, not the correctness of those GPU draws.
- Other disc revisions, full-game progression, Vulkan, macOS and Linux are unverified.

For a renderer compatibility comparison:

```powershell
./run.ps1 --render_target_path_d3d12=rtv --readback_memexport=true
```

RTV was slower in earlier measurements. It is a diagnostic alternative,
not a verified equivalent performance profile.
