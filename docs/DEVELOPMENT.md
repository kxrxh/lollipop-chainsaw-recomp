# Development and testing

Use the [build guide](BUILDING.md) for toolchain setup and the
[compatibility guide](COMPATIBILITY.md) for controls and known game issues.
Commands below run from the repository folder.

## Source layout

| Location | Responsibility |
| --- | --- |
| `src/main.cpp`, `src/app/` | Entrypoint and application lifecycle |
| `src/game/` | Native settings, startup and timing hooks, package decoding and shader inventory |
| `src/input/` | Keyboard bindings, prompt artwork and input-device switching |
| `src/settings/` | PC settings, validation and persistence |
| `src/ui/` | Settings and shader-loading interfaces |
| `src/testing/` | Local control bridge |
| `installer/` | Standalone C# launcher, XAML layout and packaging script |
| `scripts/` | Shared build helpers, benchmarks and control tools |
| `tests/` | C++, C# and script checks |

Generated translations belong in `out/generated/`. Regenerate them after manifest
changes. Guest hook symbols and signatures must match `lollipop_manifest.toml`.
See [AGENTS.md](../AGENTS.md) for coding and thread-ownership rules.

Keep disc files, saves, generated code, raw shader caches, captures and logs out
of Git. Retain upstream notices when changing the SDK patches.

## Build and tests

`./build.ps1` regenerates code, builds and runs CTest. For tests alone in an
existing build, initialize the toolchain and run:

```powershell
. ./scripts/build-tools.ps1
$tools = Get-BuildTools
& $tools.CTest --test-dir out/build/win-amd64-release --output-on-failure
```

`Get-BuildTools` accepts `-LlvmDirectory` when LLVM is not on PATH. Tests include
settings migration, input concurrency, timing, native hooks, shader-cache recovery,
installer behavior and packaging from another working directory. Headers are
compiled independently during the build.

Installer packaging uses `./installer/build.ps1`. Its source kit includes the
files needed for the game build and compiled tests. PowerShell script checks are
disabled during installation with `LOLLIPOP_SCRIPT_TESTS=OFF`.

Use a separate save directory for game checks. For tests that need existing
progress, copy the saves to a new directory before launching; do not point
automation at the player's only save copy.

## Settings and runtime integration

`src/settings/pc_settings.cpp` defines the PC profile. Preferences are written
atomically to `user/pc-settings.toml`; invalid or duplicate-key profiles are
ignored as a whole. Explicit command-line and environment values take priority.
The old `lollipop_60fps` preference migrates to `lollipop_fps`.

Original game options are imported after native save loading. Changes use the
game's own option functions, and Apply also calls its save routine. Native
settings calls run on the engine tick thread, not a loading-movie worker.

The PC menu hides and closes the original Options movie so Back returns to its
parent menu. Settings preserve an existing native pause. Confirmation dialogs
keep gameplay paused, and held closing inputs must be released before gameplay
resumes. Display changes have a 15-second Keep/Revert timer.

Keyboard prompt packages are built from the disc assets and current bindings
under `cache/keyboard-prompts/`. Rebinding needs a restart. Mixed input enables
synchronous memory-export readback to keep prompt copies current; see
[input troubleshooting](COMPATIBILITY.md#keyboard-and-controller-prompts-do-not-update)
before changing those renderer flags.

The package worker uses the host LZX decoder only for supported, validated
buffers. Unsupported layouts and decoder errors fall back to the guest decoder.
The D3D12 copy optimization keeps the GPU draw and mirrors its result only when
the source data is current on the CPU. Other exports retain normal readback.

## Shader preparation and collection

`assets/shaders/pipelines.txt` stores pipeline state, shader fingerprints,
executable locations and patches for runtime variants. Shader programs come from
the user's disc and loaded executable. They are checked against their fingerprints
before use and cached under `cache/shaders/`.

The local scan index uses package names, sizes and modification times. Damaged
cache files are rebuilt, and cached pipelines with missing shaders are dropped.
The bundled inventory is still validated strictly. Coverage is described in
[compatibility](COMPATIBILITY.md#shader-coverage).

To collect new combinations, use a copied save and a separate cache. For example:

```powershell
./run.ps1 -UserDirectory out/user-collection -CacheDirectory out/cache-collection
```

Finish preparation, play the scenes to record, then close the game so the cache
is stable. Collect its pipeline records with:

```powershell
./out/build/win-amd64-release/lollipop_shader_tool.exe collect `
    out/cache-collection out/pipeline-inventory.txt "Stage and situations exercised"
```

Reusing the output file merges earlier records and coverage notes. Review it
before replacing `assets/shaders/pipelines.txt`, rebuild, then verify with a fresh
cache and normal rendering. The frame log's `pipeline_cache_misses` counter helps
identify combinations still compiled during play.

For automated collection, `--d3d12_pipeline_collection=true --audio_mute=true`
records selected pipelines without color/depth draws. Memory exports still run.
This mode produces no game image, so check the collected inventory with normal
rendering afterward.

The collector excludes the engine-generated loading copy pipeline from the
portable inventory. It can be prepared from a player's runtime cache on later
launches. Stream `Swan_Event_PL` into an initialized final-stage world; direct
map boot is unsupported.

## Background control bridge

Launch with a separate save directory and the bridge enabled:

```powershell
./run.ps1 -UserDirectory out/user-test --lollipop_test_directory=out/test-control `
    --window_hidden=true --fullscreen=false --audio_mute=true --no-mnk_mode `
    --perf_log_csv=out/lab-frames.csv
```

The bridge is enabled only when its directory is supplied. It uses local files
and sends input without desktop focus. Injected input shares player 1 with real
devices. Send a command with:

```powershell
./scripts/test-control.ps1 -Command 'input 4096 0 0 0 0 150'
```

Use `-Directory` if the bridge is in a different location.

| Command | Purpose |
| --- | --- |
| `get name` | Read a runtime setting |
| `set name value` | Change a live setting; restart-only settings are rejected |
| `input buttons lx ly rx ry duration_ms` | Set controller input for up to 10 seconds |
| `input_device keyboard` / `input_device gamepad` | Choose the injected device |
| `input_state` | Read guest/UI buttons and the active hint device |
| `game_settings_state` | Read cached native option values |
| `game_console command` | Queue a native console command on the engine thread |
| `game_console_state` | Read console status |
| `ui_state` | List active UI dialog rectangles |
| `ui_mouse x y down` | Send mouse down/up to the SDK UI |
| `ui_key key down` | Send a navigation key down/up to the SDK UI |
| `capture` | Save the game image to `capture.bmp`; SDK overlays are excluded |
| `gpu_capture_start` / `gpu_capture_stop` | Capture when launched under RenderDoc |
| `quit` | Close the test process |

Button masks are decimal Xbox bits; stick axes range from -32768 to 32767.
The example presses A. Mouse/key `down` values are 1 for down and 0 for up; send
both events. UI keys are `enter`, `escape`, `backspace`, `tab`, `up`, `down`,
`left` and `right`.

Console commands accept up to 240 printable ASCII characters on one line.
For example, `game_console open STAGE02_PL?game=AshGame.ASH_GameInfo` requests
the stadium map. Poll `game_console_state` until `complete` or `error` before
queuing another command. Completion means the command returned; check that the
expected scene actually loaded. Use copied saves for console travel and `--cl`
guest launch arguments.

## Frame and engine measurements

To record frame intervals and engine ticks:

```powershell
./run.ps1 --perf_log_csv=out/frames.csv --lollipop_tick_trace_csv=out/ticks.csv
./scripts/measure-frames.ps1 -Path out/frames.csv -TargetFPS 60
```

The frame log measures guest swap intervals. It does not measure monitor
presentation or GPU execution time. Tick records include the engine object;
group by that object when comparing cadence. The tick trace flushes every 60 rows.

`measure-frames.ps1` accepts `-LastFrames` and `-AfterFrame` for sample ranges,
or `-StartHostUs` and `-EndHostUs` for a timed window in Windows Stopwatch
microseconds. Only complete intervals inside a timed window count. Results
include frame percentiles, fence waits, cache misses and available prompt counters.

With the bridge running, confirm live gameplay before starting the benchmark:

```powershell
./scripts/benchmark-gameplay.ps1 -Path out/lab-frames.csv -OutputDirectory out/benchmark-new
```

Use a new output directory. The benchmark sends 60 seconds of idle, camera,
movement and attack inputs, and saves per-phase timing, settings and screenshots.
Inspect the screenshots for tutorials, cutscenes or death screens before comparing
results. Keep the save, scene and graphics/input settings consistent between runs.

Verify the native pause menu before leaving a combat test unattended. Start can
open a cutscene skip prompt or be ignored during a finisher. Close the game before
replaying a GPU capture or profiling a different GPU workload.
