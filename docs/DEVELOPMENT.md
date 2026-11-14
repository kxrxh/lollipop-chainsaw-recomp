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
| `launcher/App.xaml`, `launcher/Views/`, `launcher/Styles/` | WPF startup, compiled XAML and shared theme |
| `launcher/Presentation/` | Window interaction, graphics settings controls and Markdown rendering |
| `launcher/Services/`, `launcher/Infrastructure/`, `launcher/Models/` | Installation, releases, persistence, build tools and data |
| `launcher/assets/` | Launcher artwork, shared app icon and dependency notices |
| `launcher/scripts/`, `launcher/tests/` | Source-kit packaging, pinned SDK bootstrap and launcher checks |
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
independently. Development builds enable `BUILD_TESTING` by default and build the
full test suite, header checks and shader tool. Player installations configure
`BUILD_TESTING=OFF`, build only `lollipop`, and skip CTest. Launcher payloads omit
test sources.

Apply launcher build / Repair builds in `out/build/game-compile/`, stages runtime files in
`out/build/game-staging/`, and publishes after the build and required runtime-file checks pass. Recovery data
in `out/launcher/` protects interrupted directory swaps.
The installed version marker travels with the staged build, so rollback restores
the matching version. Older installations without a marker show an unknown version
until rebuilt. The launcher embeds `VERSION`, `launcher/VERSION` and `CHANGELOG.md`; update checks
compare stable release tags with its bundled version and leave installed files alone.

`launcher/Services/GameSettings.cs` edits the same `user/pc-settings.toml` profile read by
the game. It preserves unrelated settings and comments, migrates the legacy FPS
key, rejects malformed profiles, detects external changes and publishes writes
atomically under the installation lock. Settings cannot be saved while the game
is running. The launcher test feeds a saved profile to the C++ settings loader to
check compatibility and native setting import behavior.

The SDK-style launcher targets .NET 10 WPF, with the SDK pinned by
`launcher/global.json` and NuGet versions recorded in each project's lockfile.
`launcher/build.ps1 -Tests -OutputFile out/launcher/launcher-tests.exe` publishes
the standalone check executable; pass the checkout path and optional native
`lollipop_settings_test.exe` path when running it. CMake builds and runs this
project as part of the normal development checks.

Release notes use Markdig and native WPF document elements. Web links open in the
browser; HTML stays inert and image alt text is shown without fetching remote images.
The game source kit contains native runtime sources and required resources only.

Use an isolated save directory for game checks. Copy existing saves there when
progress is needed.

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
  Chunked copy exports track only the bytes written by that draw, including vertex
  offsets and export rebasing, to avoid readback waits during streaming.
- Windows prefetches the executable code before starting the guest to reduce
  first-use instruction paging; this does not execute the game or alter saves.

## Shader preparation and collection

`assets/shaders/pipelines.txt` stores pipeline state and shader fingerprints.
Programs come from the user's disc or executable, except the engine loading copy
shader reconstructed by a fingerprint-checked generator. They are validated before
use and cached under `cache/shaders/`. Damaged local caches are rebuilt. See
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
The portable inventory includes the engine-generated loading copy pipeline.
Stream `Swan_Event_PL` into an initialized final-stage world; direct map boot
is unsupported.

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

Verify native pause before leaving combat unattended: Start may open a skip
prompt or be ignored during a finisher. Close the game before replaying a GPU
capture or profiling another GPU workload.
