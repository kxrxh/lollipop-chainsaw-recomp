# Development and local testing

## Source boundaries

The manifest records verified entrypoints and timing hooks. `src/` contains the
handwritten host; translated game code belongs in ignored `out/generated/`.
Keep disc images, extracted game assets, generated C++, saves, caches and GPU captures
out of Git. The SDK patch targets the commit pinned in [BUILDING.md](BUILDING.md).
Preserve upstream notices when modifying or redistributing it.

Host code uses `namespace lollipop`, with headers beside implementations:

- `app/`: application lifecycle; `main.cpp` only registers the application factory.
- `game/`: native settings bridge and timing hooks.
- `input/`: keyboard bindings, prompt artwork and live menu hints.
- `settings/`: the settings catalog, validation and persistence.
- `ui/`: settings rendering and interaction.
- `testing/`: the optional local input/control bridge.

Generated-code hook symbols remain global to preserve their ABI. Host headers
compile independently as part of the normal build. See [AGENTS.md](../AGENTS.md)
for project code and tooling conventions.

The asynchronous package worker calls the guest decompressor at `0x822590B0`.
For LZX jobs with a complete XMem frame sequence and accessible buffers, the
host decodes with the SDK's LZX implementation. It copies a successful result
through the guest address mapping and resumes at the worker's completion path.
Other formats, unsupported layouts and decoder errors use the original guest
call. The prompt-artwork builder checks its compressed output with the same
frame parser.

## Launch profile and controls

`run.ps1` targets 2560×1440 / 60 FPS with ROV. The default mixed-input
profile enables synchronous CPU memexport readback for live prompt artwork.
Explicit arguments override defaults without duplicate CLI options.
Normal launches prepare the D3D12 ROV shader inventory on a worker before starting
the guest. The loading screen reports package extraction, shader variants and
pipeline creation with measured counts. `async_shader_compilation=false` remains
the fallback for combinations outside the inventory; it preserves complete draws.
Explicit renderer diagnostics retain the SDK's original cache initialization.
Graphics → Frame rate offers 30, 60, 90 and 120 FPS, with a restart required.
`--lollipop_fps=90` or `--lollipop_fps=120` selects a limit at launch.
`--no-lollipop_60fps` remains a legacy alias for original timing;
`--resolution_scale=1` compares the original internal resolution.

The applied limit is captured at startup: the engine cap and guest vblank rate
change together after restarting. Original 30 FPS uses the native timing hooks
with a 60 Hz guest mode; 60/90/120 FPS use one vblank per frame at the selected
guest refresh rate. An explicit advanced `video_mode_refresh_rate` override is
preserved. VSync, display refresh and hardware performance can limit visible FPS.
Older profiles using `lollipop_60fps` migrate automatically to 30 or 60 FPS.

Keyboard/mouse defaults follow the action mappings found in RePOP's PC save:

| Action | Key |
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
| Recover / use lollipop | C (also 1) |
| Other D-pad directions: down, left, right | 2, 3, 4 |
| Reset camera | F |
| Help | Tab |
| Pause / cutscene skip dialog | Esc |
| Confirm menus / skip | Enter (also middle mouse) |
| Cancel menus | Backspace (also Space) |

Shift keeps movement active and routes left mouse to blaster fire without a
chainsaw swing. Losing focus clears held inputs; SDK dialogs release mouse
capture. F4 opens PC settings; F6 opens the SDK's rebindable input settings after closing PC settings. Command-line `--keybind_*`
overrides and saved settings take priority over the game's defaults.
Mouse movement supplies a stable right-stick sample across repeated input polls,
with the game's 20% camera deadzone compensated so slow movements still register.
The default mouse sensitivity is 18 (six times the previous default); adjust it
in PC settings or F6's input settings (the advanced range is 0.01 to 50).

Comic-style keycaps replace the shared Xbox menu and animated QTE textures.
Hints follow keyboard/mouse and SDL gamepad events across guest users, including
unbound keyboard keys. XInput and test drivers fall back to meaningful state
changes. Held inputs and idle stick drift do not take over the hints. The
renderer refreshes resident textures, including hints already visible in an
open menu, without reloading the level.
Mixed keyboard/controller hints require current CPU copies of the game's GPU
icon transfers. Startup enables memory-export readback and disables delayed
readback for this mode. Explicit renderer overrides are preserved; disabling
readback or enabling `readback_memexport_fast` can leave native hints stale.
D3D12 mirrors the game's full-buffer copy shader into guest memory when the
source pages are current on the CPU. The GPU still executes the draw and
invalidates resident textures. Partial exports, other shaders and GPU-only
source pages use synchronous readback.
Prompts are rendered from the bindings at launch into `cache/keyboard-prompts`;
the original disc files are never edited. Restart after rebinding to refresh
the artwork. Prompts show the first binding; Enter/Backspace remain menu aliases.
Windows' built-in `makecab.exe` supplies LZX compression. The generated package
is decoded and checked when built. Later launches reuse the compressed package
after checking its format stamp and the rendered and compressed content hashes.
Cached Xbox TOCs receive the generated file sizes.
The shared right-trigger icon shows V; blaster fire also accepts Shift+LMB.
The general movement icon shows WASD; directional QTE icons use their bound keys.
Mouse actions use mouse silhouettes with the relevant button highlighted.
Comic ink outlines, bevel shading and halftone dots match the original UI.
Mouse highlights retain the original action colors: yellow chainsaw, blue
pom-pom and green low slash; the wheel uses a narrow ribbed silhouette.
Menu prompt textures render at least 128×128 while keeping their original display
size. Vertical QTE artwork maps the down arrow to S and the up arrow to W.
Stick rotation prompts show separate movement keys surrounded by circular arrows.
The live title field shows the bound keyboard pause/start key ("PRESS ESC" by
default) or the original localized "PRESS START" according to the current input.
Its Scaleform view is observed from the verified UGFxMoviePlayer load hook at
`0x82940384`, then updated each advance; no title reload is required.
The unload hook at `0x829411A8` removes the view before its owner is freed;
advancing a movie can itself close it, so ownership is checked afterward.
Controller-only mode retains the original title text.
Embedded controller diagrams or text in other artwork are not rewritten.

For controller-only play with original Xbox artwork, use `./run.ps1 --no-mnk_mode`.
SDL still supplies gamepad input. During cutscenes, Esc opens Skip/Cancel and
Enter confirms skipping. These controls preserve the original game's actions;
RePOP's automatic firing and automatic QTE gameplay changes are not included.

## Shader preparation and collection

`assets/shaders/pipelines.txt` contains pipeline state, shader fingerprints,
executable locations and masked changes for runtime variants. It contains no
complete shader programs. The host extracts shader containers, including their
individual XMem compression, from the user's packages and loaded executable.
The extracted library and graphics caches remain under `cache/shaders/`.
Package names, sizes and modification times invalidate the local scan index.
Every reconstructed shader is checked against its recorded fingerprint before use.
Invalid local cache files are rebuilt; cached pipelines with missing shader programs
are discarded. The bundled inventory remains strictly validated.

The inventory contains 1,916 pipelines collected across stages 00-06, boss maps,
final-stage variants and opening/ending/result scenes. The sweep checked 760
distinct streaming packages and 14,467 camera positions, sampling four directions
and waiting for three guest swaps per direction. Duplicate packages were reused.
Swan_Event_PL was streamed into an initialized final-stage world; it cannot be
booted directly with the collection game's stage initializer.

This covers the stage/asset sweep. A complete story playthrough and every outfit,
boss-phase and combat-effect permutation remain unverified. The engine generates
one byte-copy shader during native loading; collection excludes that pipeline
from the portable seed. Later launches also prepare it from the user's local
runtime cache. Record and merge any new pipeline combinations found during play.

The merged seed was also checked by preparing a fresh cache and replaying all 14
playable/boss map entries with normal rendering at default graphics settings.
That pass added no material pipelines; only the native startup copy shader was
absent from the portable seed. Test runs keep audio muted.

To extend coverage, play the relevant scenes with an isolated save and cache,
close the game so its records are stable, then merge that cache:

```powershell
./scripts/collect-pipelines.ps1 -CacheDirectory out/stage-test/cache `
    -OutputFile out/pipeline-inventory.txt -Coverage "Stage and situations actually exercised"
```

The first launch must finish shader preparation before collecting, because the
collector needs the local disc library and executable-location metadata. Reuse
the output file to merge more runs; existing entries and coverage notes survive.
Review the inventory before replacing `assets/shaders/pipelines.txt`, rebuild,
then test it with a fresh graphics cache containing no recorded `.xsh` or `.xpso`
files. Keep raw caches, executable dumps and captures out of Git. Frame CSV
`pipeline_cache_misses` identifies combinations still being compiled during play.

The SDK's shader-precompile patch adds progress callbacks, waits for every queued
pipeline before completing, and enables its temporary startup worker pool. Its
XMem decoder keeps ordinary CAB LZX padding behavior separate. Synthetic tests
cover odd uncompressed blocks, compressed shader containers, malformed input,
cache reuse, inventory merging and reconstruction without a runtime shader cache.

For an automated collection run, `--d3d12_pipeline_collection=true --audio_mute=true`
records the engine's selected pipelines without issuing color/depth draws. Memory
exports still execute, and occlusion queries report visible samples. This diagnostic
mode produces no game image; verify the resulting inventory with normal rendering.
A controlled school-scene comparison produced the same 656 pipeline descriptions
with normal rendering and collection mode, starting from separate fresh caches.

The opt-in bridge can queue native console commands on the engine thread. For
example, `game_console open STAGE02_PL?game=AshGame.ASH_GameInfo` travels to the
stadium stage. Poll `game_console_state` until `complete` before another command.
Completion means the native command returned; check the resulting scene as well.
Commands accept at most 240 printable ASCII characters on one line. Use copied
saves for collection, including when starting a map with the `--cl` guest argument.

## PC settings

The panel and controls use embedded PNG artwork in `assets/settings/`, with
brush-lettered title art and live labels/values. Control borders retain their
original proportions as buttons and selection strips resize. The panel and
three-strip control atlas are embedded in the Windows executable.

Settings control hints reuse the local game's button textures and existing
keyboard artwork. Glyphs are fitted by their visible bounds. UI controller
polling bypasses SDL's gameplay capture gate while preserving window focus and
guest input blocking. D-pad and left-stick navigation support held directions.
The caption shows one short hint or status at a time; restart notices appear
only for the selected setting or pending saved changes.
All live text, including the caption, uses the same bold font. Left/right moves
between the bottom actions and confirmation choices; A selects, B/Start backs
out, and LB/RB changes pages. The opt-in test bridge preserves real controllers
alongside injected input instead of replacing them with test-only devices.

Selecting **Options** in the original title or pause menu opens one unified
settings screen. The original Options movie is hidden and runs its own close
action, so Back returns to the parent menu. **F4** remains a shortcut and pauses
gameplay. Closing settings preserves an existing native pause. Unsaved-change
and display-confirmation dialogs keep gameplay paused; held closing keys must
be released before gameplay input resumes.

Five pages cover Display, Graphics, Audio, Camera and Controls. Alongside the PC
rendering and input controls, the menu includes every original option: music
and effects volume, subtitles, both camera axes, blaster aiming and vibration.
Original values are read and changed through the game's own option functions.
Render-resolution labels assume the normal 720p guest video mode. Borderless
fullscreen uses the desktop resolution; window resolution controls windowed size.

Mouse, keyboard arrows/Enter/Esc and controller D-pad/A/B are supported.
Tab or LB/RB changes pages. Defaults stages the current page's recommended
values; Apply commits all pages. Leaving with unsaved changes asks before
discarding. Window/display changes get a 15-second Keep/Revert preview. Graphics
options requiring a restart are labeled, with a reminder after saving.

Player-facing preferences are written atomically to
`<user_data_root>/pc-settings.toml`. Invalid or duplicate-key profiles are ignored
as a whole; explicit command-line/environment values win. Older profiles import
missing original options after native save loading rather than resetting them.
Apply also calls the original options-save routine. Disc assets are unchanged.
Key binding edits remain in the existing F6 advanced settings/config path.

Verification includes CTest regression checks and 18 live D3D12 behavior cases: controller, mouse
and keyboard navigation, native option read-back, repeated original Options
entry/Back, staged defaults, Apply/Discard/Cancel, unwritable-profile recovery,
display Keep/Revert/timeout, resized mouse targets, input-release blocking and
restart persistence. Controller-only mode is tested with injected pad input.
Full-game, physical-controller and multi-monitor coverage remain unverified.

The settings pause hook augments the false return of `UWorld::IsPaused` at
`0x82582FE0`; native true returns are unchanged. Timing tests cover repeated open/
close and preservation of the native result. Live prologue checks confirmed that
game/audio clocks stop while real time continues, resume after closing, remain
stopped during discard confirmation/held Enter, and leave the native pause menu
in place when settings closes over it.

## Frame and engine measurements

```powershell
./run.ps1 --perf_log_csv=out/frames.csv --lollipop_tick_trace_csv=out/ticks.csv
./scripts/measure-frames.ps1 -Path out/frames.csv -TargetFPS 60
```

CSV allows concurrent readers. `-AfterFrame <cursor>` selects samples after an
earlier `TotalFrames`; `-LastFrames <count>` bounds the interval. Results include
percentiles, waits, pipeline/texture misses, texture-cache memory/evictions and
skipped no-effect draws. Missing older counters are reported as unavailable.
Timestamped logs also support `-StartHostUs` and `-EndHostUs` in Windows
Stopwatch microseconds. Only intervals fully inside the window count.
`OverBudgetPercent` uses the exact requested frame budget, including small
deviations around the cap. Prompt matching time includes sampling and exact
comparison. Hash bytes count only the sampled bytes. Memexport fence time
is the portion of D3D12 fence waiting inside synchronous memory-export readback.

The optional trace records host time, engine object and unmodified delta at
`0x829D0CF8`, before Tick at `0x8253A7E8`. Group by object before interpreting
cadence. The hook does not clamp the game's delta.

## Background control bridge

Use a separate **copy** of the save directory for automation:

```powershell
./run.ps1 -UserDirectory out/user-perf-lab --lollipop_test_directory=out/test-control --window_hidden=true --fullscreen=false --audio_mute=true --no-mnk_mode --perf_log_csv=out/lab-frames.csv
```

The bridge is absent unless its directory is supplied. Injected input shares
player 1 with physical controllers and keyboard emulation. No desktop focus
or network service is used.

Send commands with `./scripts/test-control.ps1 '<command>'`:

| Command | Behavior |
| --- | --- |
| `get name` | Read a setting |
| `set name value` | Set a validated live setting; restart-only settings are rejected |
| `input buttons lx ly rx ry duration_ms` | Controller state, up to 10 seconds; releases automatically |
| `input_device keyboard` / `input_device gamepad` | Route future test commands to one of two separate devices |
| `input_state` | Compare guest and SDK UI button snapshots |
| `game_settings_state` | Read the cached native audio, subtitle, camera, aiming and vibration values |
| `game_console command` | Queue a native console command for the engine thread |
| `game_console_state` | Read native command status: idle, pending, running, complete or error |
| `ui_state` | List SDK dialog rectangles |
| `ui_mouse x y down` | Send SDK UI mouse down/up without moving the desktop pointer |
| `ui_key enter\|escape\|backspace\|tab\|up\|down\|left\|right down` | Send UI navigation key down/up (0 or 1) without desktop input |
| `capture` | Save native output to `capture.bmp` in the control directory |
| `gpu_capture_start` / `gpu_capture_stop` | Capture when launched under RenderDoc |
| `quit` | Close the test process |

Button masks are decimal Xbox bits; stick axes are signed 16-bit values.
`input 4096 0 0 0 0 150` presses A. Send both down and up for UI mouse clicks.
Guest captures exclude SDK overlays.

After visually confirming live gameplay:

```powershell
./scripts/benchmark-gameplay.ps1 -Path out/lab-frames.csv -OutputDirectory out/benchmark-new
```

Run the sequence after restarting with the desired settings. Its 60 seconds
of input cover idle, camera, movement and attacks. Each result records the
renderer and input flags, a frame-budget summary and a screenshot. The phase
starts on acknowledgement of its first input and ends after the last hold.
Analysis waits for the CSV to reach that timestamp before summarizing it.
Check screenshots for tutorials, cutscenes and death screens before interpreting
combat measurements. Verify the native pause menu before leaving combat unattended;
Start can open a cutscene skip prompt or be rejected during a finisher.

Stop the game before replaying RenderDoc captures or profiling another GPU
workload. Keep diagnostic output private and report timing-source limits.
