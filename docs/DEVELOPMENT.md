# Development and testing

See [BUILDING.md](BUILDING.md) for setup and [COMPATIBILITY.md](COMPATIBILITY.md)
for controls and known issues. Run commands from the repository folder.

## Source layout

| Location | Responsibility |
| --- | --- |
| `src/main.cpp`, `src/app/` | Entrypoint and lifecycle |
| `src/game/` | Guest bridge, native menus, hooks, package decoding and shaders |
| `src/input/` | Bindings, prompt artwork and device switching |
| `src/settings/` | PC settings and persistence |
| `src/ui/` | Settings page, exit flow and shader loading |
| `src/testing/` | Local control bridge |
| `installer/` | C# launcher, XAML and packaging |
| `scripts/`, `tests/` | Development tools and checks |

Follow [AGENTS.md](../AGENTS.md). Regenerate `out/generated/` after manifest
changes; hook symbols and signatures must match `lollipop_manifest.toml`.
Keep disc files, saves, generated code, caches, captures and logs out of Git.
Retain upstream notices when editing SDK patches.

## Build and tests

`./build.ps1` regenerates code, builds and runs CTest. To test an existing build:

```powershell
. ./scripts/build-tools.ps1
$tools = Get-BuildTools
& $tools.CTest --test-dir out/build/win-amd64-release --output-on-failure
```

Pass `-LlvmDirectory` to `Get-BuildTools` if LLVM is not on PATH. Headers compile
independently. Installer builds include compiled tests and disable script checks
with `LOLLIPOP_SCRIPT_TESTS=OFF`.

Update / Repair builds in `out/build/game-compile/`, stages runtime files in
`out/build/game-staging/`, and publishes only after CTest passes. Recovery data
in `out/launcher/` protects interrupted directory swaps.

Use an isolated save directory for game checks. Copy existing saves there when
progress is needed. With the [bridge](#background-control-bridge) running:

```powershell
./tests/settings_menu_smoke.ps1 -GameProcessId 1234 -Directory out/test-control
```

This checks quit confirmation and closes the test game. Repeat on fresh processes
with `-ConfirmWith keyboard` or `-ConfirmWith gamepad`. For the main-menu path,
select Quit Game first and add `-FromMainMenu`. Finish shader preparation first.

## Settings and runtime integration

- `src/settings/pc_settings.cpp` validates and atomically saves the PC profile.
  Command-line and environment values override it. `lollipop_60fps` migrates to
  `lollipop_fps`.
- Native options load after the save. Apply uses the game's setters and save
  routine on the engine tick thread. `game_bridge` restores the caller's PPC
  context; guest calls must not hold runtime or registry locks.
- `game_menus` routes movie hooks. `native_quit_dialog` owns the native system
  window; `exit_flow` forwards UI input, blocks gameplay and waits for release.
  Cancelling preserves the settings draft and existing native pause state.
- The PC page hides and closes the original Options movie. Display changes have
  a 15-second rollback timer. Settings sync runs every 250 ms; quit updates run
  each frame.
- The main menu reuses the disabled Downloads slot. Its patched `MENU_PL.xxx`
  lives in the cache; the tagged Options command bypasses native Kismet routing.
- Prompt artwork is cached under `cache/keyboard-prompts/`. Rebinding requires a
  restart. Mixed input needs synchronous memory-export readback.
- Host LZX decoding falls back to the guest decoder for unsupported layouts or
  errors. CPU copy mirroring requires current source data and retains the GPU draw.

## Shader preparation and collection

`assets/shaders/pipelines.txt` stores pipeline state and shader fingerprints.
Programs come from the user's disc or executable, are validated before use, and
are cached under `cache/shaders/`. Damaged local caches are rebuilt. See
[coverage limits](COMPATIBILITY.md#shader-coverage).

Collect with a copied save and separate cache:

```powershell
./run.ps1 -UserDirectory out/user-collection -CacheDirectory out/cache-collection
```

Play the required scenes, close the game, then collect:

```powershell
./out/build/win-amd64-release/lollipop_shader_tool.exe collect `
    out/cache-collection out/pipeline-inventory.txt "Stage and situations exercised"
```

Reusing the output merges records. Review before replacing
`assets/shaders/pipelines.txt`, rebuild, and check normal rendering with a fresh
cache. Watch `pipeline_cache_misses` for combinations compiled during play.

`--d3d12_pipeline_collection=true --audio_mute=true` skips color/depth draws
while recording pipelines. Verify its results with normal rendering afterward.
The portable inventory excludes the engine-generated loading copy pipeline.
Stream `Swan_Event_PL` into an initialized final-stage world; direct map boot
is unsupported.

## Texture reload stress check

Use a copied save and reduced cache limits:

```powershell
./run.ps1 -UserDirectory out/texture-test-user -CacheDirectory out/texture-test-cache `
    --texture_cache_memory_limit_soft=64 --texture_cache_memory_limit_hard=128 `
    --texture_cache_memory_limit_soft_lifetime=1 --perf_log_csv=out/texture-test-frames.csv
```

Repeat prologue Restart and death/Continue. Confirm rising
`texture_cache_evictions`, resumed gameplay, and no access violation or GPU loss.
Repeat with default limits. Texture destruction must hold the shared-memory
critical region while removing watch handles. Mid-frame GPU waits must invalidate
cached texture bindings before eviction.

For phone load/unload checks, load a copied checkpoint 25 save normally and run:

```powershell
./tests/phone_transition_smoke.ps1 -GameProcessId 1234 -Directory out/phone-control `
    -FrameLog out/phone-frames.csv -GameLog out/phone-game.log
```

Inspect its captures. The script invokes `ce TelephoneIntro` and `ce TeleTest`;
these bypass story triggers and do not verify normal progression.

## Background control bridge

```powershell
./run.ps1 -UserDirectory out/user-test --lollipop_test_directory=out/test-control `
    --window_hidden=true --fullscreen=false --audio_mute=true --no-mnk_mode `
    --perf_log_csv=out/lab-frames.csv
```

From another PowerShell window:

```powershell
./scripts/test-control.ps1 -Command 'input 4096 0 0 0 0 150'
```

Supplying the bridge directory enables local file commands without desktop focus.
Injected input shares player 1 with real devices. Use `-Directory` with the control
script for a different bridge path.

| Command | Purpose |
| --- | --- |
| `get name` / `set name value` | Read or change live settings; restart-only writes are rejected |
| `input buttons lx ly rx ry duration_ms [lt rt]` | Inject controller input for up to 10 seconds |
| `input_device keyboard` / `input_device gamepad` | Select injected device |
| `input_state` | Read guest/UI buttons, triggers and hint device |
| `game_settings_state` | Read cached native options |
| `game_console command` / `game_console_state` | Queue an engine-thread console command or read its status |
| `ui_state` | List active dialog rectangles |
| `ui_mouse x y down` / `ui_key key down` | Send UI mouse or navigation-key events |
| `capture` | Save `capture.bmp` without SDK overlays |
| `gpu_capture_start` / `gpu_capture_stop` | Capture under RenderDoc |
| `quit` | Close the test game |
| `quit_state` / `quit_selection` | Read native quit state and selection: 1 Yes, 2 No |

Button masks are decimal Xbox bits; axes range from -32768 to 32767. Triggers
range from 0 to 255; omitting them releases both. The example presses A.
`input 0 0 0 0 0 150 0 255` pulls RT for Star Soul.

UI events use 1 for press and 0 for release; send both. Keys: `enter`, `escape`,
`backspace`, `space`, `tab`, `up`, `down`, `left`, `right`.

Console commands accept 240 printable ASCII characters. Poll
`game_console_state` until `complete` or `error` before sending another command;
completion does not prove the requested scene loaded. Commands use the local
player console, falling back to the world console during startup. Use copied
saves for console travel and `--cl` launch arguments.

## Frame and engine measurements

```powershell
./run.ps1 --perf_log_csv=out/frames.csv --lollipop_tick_trace_csv=out/ticks.csv
./scripts/measure-frames.ps1 -Path out/frames.csv -TargetFPS 60
```

Frame logs measure guest swap intervals, not monitor presentation or GPU duration.
Group tick records by engine object. `measure-frames.ps1` supports `-LastFrames`,
`-AfterFrame`, and timed windows with `-StartHostUs` / `-EndHostUs` in Windows
Stopwatch microseconds. Only complete intervals count.

With the bridge running in live gameplay:

```powershell
./scripts/benchmark-gameplay.ps1 -Path out/lab-frames.csv -OutputDirectory out/benchmark-new
```

Use a new output directory. The benchmark records 60 seconds of idle, camera,
movement and attacks. Inspect captures for menus, cutscenes or death screens;
keep the save, scene and settings consistent between runs.

Verify native pause before leaving combat unattended: Start may open a skip
prompt or be ignored during a finisher. Close the game before replaying a GPU
capture or profiling another GPU workload.
