# Development and local testing

## Source boundaries

The manifest records verified entrypoints and timing hooks. `src/` contains the
handwritten host; translated game code belongs in the private sibling `generated/`
directory. Keep disc images, assets, generated C++, saves, caches and GPU captures
out of Git. The SDK patch targets the commit pinned in [BUILDING.md](BUILDING.md).
Preserve upstream notices when modifying or redistributing it.

## Launch profile and controls

`run.ps1` targets 2560×1440 / 60 FPS with ROV and CPU memexport readback off.
Explicit arguments override defaults without duplicate CLI options.
`--no-lollipop_60fps` preserves original timing registers;
`--resolution_scale=1` compares the original internal resolution.

Keyboard: WASD moves, arrow keys control the camera, Enter is Start, Space is A,
Backspace is B, L is X and P is Y. SDL supplies gamepad input. During cutscenes,
Start opens Skip/Cancel and A confirms skipping.

## Frame and engine measurements

```powershell
./run.ps1 --perf_log_csv=../LollipopChainsaw-private/frames.csv --lollipop_tick_trace_csv=../LollipopChainsaw-private/ticks.csv
./scripts/measure-frames.ps1 -Path ../LollipopChainsaw-private/frames.csv
```

CSV allows concurrent readers. `-AfterFrame <cursor>` selects samples after an
earlier `TotalFrames`; `-LastFrames <count>` bounds the interval. Results include
percentiles, waits, pipeline/texture misses, texture-cache memory/evictions and
skipped no-effect draws. Missing older counters are reported as unavailable.

The optional trace records host time, engine object and unmodified delta at
`0x829D0CF8`, before Tick at `0x8253A7E8`. Group by object before interpreting
cadence. The hook does not clamp the game's delta.

## Background control bridge

Use a separate **copy** of the save directory for automation:

```powershell
./run.ps1 --user_data_root=../LollipopChainsaw-private/user-perf-lab --lollipop_test_directory=../LollipopChainsaw-private/test-control --window_hidden=true --fullscreen=false --audio_mute=true --no-mnk_mode --perf_log_csv=../LollipopChainsaw-private/lab-frames.csv
```

The bridge is absent unless its directory is supplied. In this mode only the
synthetic controller drives player 1; physical controllers and keyboard emulation
do not affect the guest. No desktop focus or network service is used.

Send commands with `./scripts/test-control.ps1 '<command>'`:

| Command | Behavior |
| --- | --- |
| `get name` | Read a setting |
| `set name value` | Set a validated live setting; restart-only settings are rejected |
| `input buttons lx ly rx ry duration_ms` | Controller state, up to 10 seconds; releases automatically |
| `input_state` | Compare guest and SDK UI button snapshots |
| `ui_state` | List SDK dialog rectangles |
| `ui_mouse x y down` | Send SDK UI mouse down/up without moving the desktop pointer |
| `capture` | Save native output to `capture.bmp` in the control directory |
| `gpu_capture_start` / `gpu_capture_stop` | Capture when launched under RenderDoc |
| `quit` | Close the test process |

Button masks are decimal Xbox bits; stick axes are signed 16-bit values.
`input 4096 0 0 0 0 150` presses A. Send both down and up for UI mouse clicks.
Guest captures exclude SDK overlays.

After visually confirming live gameplay:

```powershell
./scripts/benchmark-gameplay.ps1 -Path ../LollipopChainsaw-private/lab-frames.csv -OutputDirectory ../LollipopChainsaw-private/benchmark-new
```

The 60-second sequence records idle, camera, movement and attack phases, with
JSON summaries and captures. Inspect the images: tutorials, cutscenes and death
screens are not sustained combat measurements. CSV boundaries are approximate
by up to 60 frames. Verify the native pause menu before leaving combat unattended;
Start can open a cutscene skip prompt or be rejected during a finisher.

Stop the game before replaying RenderDoc captures or profiling another GPU
workload. Keep diagnostic output private and report timing-source limits.
