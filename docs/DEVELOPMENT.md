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
They are generated from the bindings at launch into `cache/keyboard-prompts`;
the original disc files are never edited. Restart after rebinding to refresh
the artwork. Prompts show the first binding; Enter/Backspace remain menu aliases.
Windows' built-in `makecab.exe` supplies LZX compression. The generated package
is decoded again before use, and cached Xbox TOCs receive the generated file sizes.
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

Verification includes 14 CTests and 18 live D3D12 behavior cases: controller, mouse
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
./scripts/benchmark-gameplay.ps1 -Path ../LollipopChainsaw-private/lab-frames.csv -OutputDirectory ../LollipopChainsaw-private/benchmark-new
```

The 60-second sequence records idle, camera, movement and attack phases, with
JSON summaries and captures. Inspect the images: tutorials, cutscenes and death
screens are not sustained combat measurements. CSV boundaries are approximate
by up to 60 frames. Verify the native pause menu before leaving combat unattended;
Start can open a cutscene skip prompt or be rejected during a finisher.

Stop the game before replaying RenderDoc captures or profiling another GPU
workload. Keep diagnostic output private and report timing-source limits.
