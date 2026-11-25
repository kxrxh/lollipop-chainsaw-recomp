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

`./build.ps1` updates generated code, builds changed sources and runs CTest.
To test an existing build:

```powershell
. ./scripts/build-tools.ps1
$tools = Get-BuildTools
& $tools.CTest --test-dir out/build/win-amd64-release --output-on-failure
```

Pass `-LlvmDirectory` to `Get-BuildTools` if LLVM is not on PATH. Headers compile
independently. Development builds enable `BUILD_TESTING` by default and build the
full test suite, header checks and shader tool. Player installations configure
`BUILD_TESTING=OFF`, build `lollipop`, `lollipop_shader_tool` and `lollipop_dlc_tool`,
and skip CTest. Launcher payloads omit
test sources.

The standalone launcher checks include synthetic DLC metadata, nested ZIP/folder
imports, duplicate detection, and safe archive paths. To exercise the native reader
and review/install UI with your own two costume ZIPs, run:

```powershell
./out/build/win-amd64-release/lollipop_setup_test.exe --costume-preview `
    out/costume-import-check out/build/win-amd64-release 'path/to/Ash Outfit.zip' 'path/to/Costumes.zip'
```

This creates an isolated installation, captures the normal and minimum-size UI,
and checks installation locks, duplicate imports, and save preservation. Use a new
output directory for each run. DLC packages and captures stay outside Git.

Launcher builds use `out/build/game-compile/` and the stable source copy in
`out/source/`. Unchanged sources and manifests retain their timestamps. ReXGlue
skips unchanged translations; Ninja reuses objects and precompiled headers. Fixed
SDK/compiler archive timestamps keep package cache paths stable. CMake starts fresh
only when migrating an older source path. Automatic regeneration is disabled for
launcher builds to avoid loops from archived timestamps.

Runtime files are staged in `out/build/game-staging/` and checked before replacing
the playable build. Recovery records in `out/launcher/` protect directory swaps.
The version marker is published with the build and restored on rollback. Older
installations show an unknown version until rebuilt.

The launcher embeds `VERSION` and `launcher/VERSION`; release notes come from GitHub releases. A background
startup check compares the latest stable tag with the bundled recomp version.
Newer releases show an Updates badge and a notice that can be dismissed for the
session. Failed checks stay on the Updates page. Self-update selects the latest
release's `Lollipop-Chainsaw-Launcher.exe`, checks its official
URL, size and SHA-256, and downloads it through the resumable package cache. Its
file version prevents launcher downgrades independently of the recomp version.
A copy of the current launcher waits for the old process to exit, replaces its
EXE atomically and restarts it. State stays beside the original EXE.

Tests cover source-path migration, future-dated configuration, object/PCH reuse, changed
headers, reproducible packages, invalid downloads, locked files and updater restart.

`launcher/Services/GameSettings.cs` edits `user/pc-settings.toml`. It preserves
unrelated settings and comments, migrates the legacy FPS key, rejects malformed
profiles and detects external changes. Writes are atomic and use the installation
lock. Settings cannot be saved while the game is running. Tests pass a launcher
profile to the C++ loader to check compatibility and native setting imports.

The SDK-style launcher targets .NET 10 WPF, with the SDK pinned by
`launcher/global.json` and NuGet versions recorded in each project's lockfile.
`launcher/build.ps1 -Tests -OutputFile out/launcher/launcher-tests.exe` publishes
the standalone check executable; pass the checkout path and optional native
`lollipop_settings_test.exe` path when running it. CMake builds and runs this
project as part of the normal development checks.

Release notes use Markdig and native WPF document elements. Web links open in the
browser; HTML stays inert and image alt text is shown without fetching remote images.
The game source kit contains native runtime sources and required resources only.

Use an isolated save directory for game checks; copy existing saves there if needed.

## Settings and runtime integration

- D3D12 ROV uses descriptor tables by default. Cache-pressure replays of the Zed
  encounter reproduced NVIDIA GPU timeouts with bindless resources enabled.
  `--gpu.d3d12.bindless=true` overrides this default for diagnostics. The shader
  startup check accepts `-Bindless` to verify that override with cold and warm caches.
- `src/settings/pc_settings.cpp` validates and atomically saves the PC profile.
  Command-line and environment values override it. `lollipop_60fps` migrates to
  `lollipop_fps`.
- `lollipop_shadow_resolution` keeps native character shadows at `0` (default), or
  raises their resolution cap and texel density to `256`, `512` or `1024` live.
  Startup captures the native baseline. The engine tick queues live settings to
  the render thread. Shadow projection uses the allocated target dimensions
  while the selected quality limits each light's viewport.
  Original restores the baseline, and native reloads cannot compound the density.
- Native options load after the save. Apply uses the game's setters and save
  routine on the engine tick thread. `game_bridge` restores the caller's PPC
  context; guest calls must not hold runtime or registry locks.
- `game_menus` routes movie hooks. `native_quit_dialog` owns the native system
  window; `exit_flow` forwards UI input, blocks gameplay and waits for release.
  Cancelling preserves the settings draft and existing native pause state.
- The SDK keeps consumed buttons and triggers blocked until UI input confirms release,
  including when ImGui temporarily mutes the guest driver's state.
- The PC page hides and closes the original Options movie. Display changes have
  a 15-second rollback timer. Settings sync runs every 250 ms; quit updates run
  each frame.
- Render resolution, frame rate, FXAA and color dithering apply live. Resolution
  changes wait for a completed GPU frame, drain submissions and replace the
  scale-dependent render targets, texture cache and shader pipelines while keeping
  shared GPU memory and the guest running. A scale change can briefly compile shaders.
- F6 (`bind_cheats`) opens a separate Cheats window in `ui/cheats_menu.cpp`. It owns
  its input blocker and pause flag, preserving settings drafts and native pause.
  Cheat flags default off and are excluded from the PC profile. The engine-thread update in
  `game/cheats.cpp` validates the current Juliet pawn, restores the controller's
  previous god-mode flag when disabled, and uses native medal and Star Soul setters.
  Native save behavior is unchanged: disabling a cheat does not remove awarded
  medals or purchases. Scripted deaths and mission failures can still apply.
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

Run `./tests/shader_startup_test.ps1 -GameDirectory <disc> -BuildDirectory <build>`
to check automatic preparation with a new cache and isolated saves, then a warm
relaunch. It checks that preparation runs only once per launch and captures a
guest frame after the handoff. Evidence stays under a unique `out/` directory.

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

To search across playthroughs, close the game and collect
`cache/shaders/shareable/565707D0.rov.d3d12.xpso` from each recording. This
file contains pipeline descriptions and shader fingerprints. Compare one or more
recordings with the current inventory from PowerShell 7:

```powershell
./out/build/win-amd64-release/lollipop_shader_tool.exe discover assets/shaders/pipelines.txt `
    'out/recordings/stage 2.xpso' 'out/recordings/stage 6.xpso' > out/discovery.txt
```

The report counts unique pipelines across all inputs. Plain hex lines are new
combinations of vertex shader, pixel shader and fixed-function state already
represented in the inventory, including the shader modification bits. Append
those lines to a copy of the inventory for review. Commented records identify
new shader forms or render states for a full `collect` run. A tester can share
the small `.xpso` alone for this comparison. Verify additions by replaying the
scene with a fresh game cache and normal rendering.

If the tester exported a [full text report](COMPATIBILITY.md#shader-coverage),
combine it with the current inventory in a scratch file. This retains new shader
recipes as well as pipeline states. Review the additions before rebuilding.

```powershell
Get-Content assets/shaders/pipelines.txt, out/stage-pipelines.txt |
    Select-Object -Unique | Set-Content -Encoding utf8NoBOM out/candidate.txt
```

Prioritize effects that appear briefly, including boss transformations, explosions
and combat moves. Streamed scenery alone will miss their draws. Record the scene
and the `pipeline_cache_misses` and `frame_time_us` counters together. Replay
the same segment using the updated inventory and a fresh pipeline cache. A
second run with the learned cache only measures a returning player's experience.
Long frames with zero misses warrant separate investigation.

Measure frame times with `--d3d12_debug=false --gpu_debug_markers=false` and
keep screenshot or GPU captures outside the measured interval. Repeat the same
route with diagnostics enabled only as a separate comparison: D3D12 diagnostics
can introduce long frames even when the release configuration stays smooth.

The disc scan finds shader programs. A D3D12 pipeline also depends on the
vertex and pixel shader modifications, depth/stencil and blend state, and render
targets used by a particular draw. Unleashed Recompiled supplements its recorded
states by preparing pipelines from models and particle materials as assets load.
For a similar UE3 implementation, measure how many actual misses a material-load
predictor anticipates on a separate playthrough, and how much extra compilation
it schedules. [Unleashed Recompiled renderer](https://github.com/hedge-dev/UnleashedRecomp/blob/main/UnleashedRecomp/gpu/video.cpp)

Forza Motorsport 4 orders scripted races by the additional shaders their tracks
reference. Forza Horizon 1: ReImagined measures its prebuilt list on routes
excluded from capture. Apply both ideas here: select scenes with different
materials and effects, reserve unseen encounters, and count new pipelines over
the entire session, including loading and transitions. ReImagined also has a
Vulkan implementation that reads queued draws using a copy of the GPU registers
and starts creating their pipelines early. A D3D12 experiment should measure
the time spent predicting as well as the time saved at the draw.
[Motorsport 4 scripted warm-up](https://github.com/i2kiselev/fm4-static-recomp/blob/160ef51f716ebc6f90692d368d68f3a820fcecfe/tools/warm-up.ps1)
· [Horizon held-out measurements](https://github.com/FH1-ReImagined/ForzaHorizon1-ReImagined/blob/46d5ed62866af68700d15c61748df8c29ad0248d/docs/shader_prebuild.md#wider-coverage-276)
· [Horizon lookahead implementation](https://github.com/FH1-ReImagined/ForzaHorizon1-ReImagined/blob/46d5ed62866af68700d15c61748df8c29ad0248d/patches/rexglue-sdk/0270-vulkan-pipeline-lookahead.patch)

Saints Reborn reloads pipelines learned in earlier sessions. It separately
prefetches world chunks toward the camera's predicted position. For a slow
frame with zero pipeline misses, time package reads, decompression and texture
uploads separately before choosing what to prefetch.
[Saints Reborn cache startup](https://github.com/whompay/SaintsReborn/blob/6c6e07eab365c83554e0777b48688425995caa4a/project/src/main.cpp)
· [Saints Reborn chunk prefetch](https://github.com/whompay/SaintsReborn/blob/6c6e07eab365c83554e0777b48688425995caa4a/project/src/render_fixes.cpp)

Zelda 64: Recompiled uses RT64, which can draw with a general-purpose N64 shader
while background threads compile specialized versions. Applying that approach to
the Xbox 360 renderer would require a fallback that also reproduces guest shader
execution and memory exports. [Zelda 64: Recompiled](https://github.com/Zelda64Recomp/Zelda64Recomp)
and [RT64 shader cache](https://github.com/rt64/rt64/blob/main/src/render/rt64_raster_shader_cache.cpp)

For automated stage checks, resume a checkpoint through the title screen so the
game loads Juliet's costume. From gameplay, `game_console RemoteEvent START_PL_STAGE02`
uses the game's Stage 2 transition inside `START_PL`. Confirm the target stage
loaded and capture Juliet in gameplay. Opening the stage package directly can
leave her pawn using `SK_CH_Main_Juliet_dummy` and miss character pipelines.

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
| `game_cheats_state` | Read cheat readiness or a native integration error |
| `presentation_state` | Read the presenter's active dithering state |
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

Run `./tests/live_settings_test.ps1 -GameDirectory <disc> -BuildDirectory <build>`
to verify 720p / 1440p / 4K switching, frame-rate changes, FXAA, dithering, shadow quality and
display rollback with Cheats open in one D3D12 process. Saves and captures stay in a new
`out/` directory.
Use `-SaveDirectory <save-folder>` to test a copy of an existing save.

```powershell
./run.ps1 --perf_log_csv=out/frames.csv --lollipop_tick_trace_csv=out/ticks.csv
./scripts/measure-frames.ps1 -Path out/frames.csv -TargetFPS 60
```

Frame logs measure guest swap intervals, not monitor presentation or GPU duration.
Hidden PC settings, achievement toasts and the F3 debug overlay update with guest
frames without extra host presents, so RTSS / MSI Afterburner tracks the game's frame
rate during normal play. Settings, the console and loading screens repaint independently.
Group tick records by engine object. `measure-frames.ps1` supports `-LastFrames`,
`-AfterFrame`, and timed windows with `-StartHostUs` / `-EndHostUs` in Windows
Stopwatch microseconds. Only complete intervals count.

Verify native pause before leaving combat unattended: Start may open a skip
prompt or be ignored during a finisher. Close the game before replaying a GPU
capture or profiling another GPU workload.
