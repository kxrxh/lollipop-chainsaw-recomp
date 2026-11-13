# Lollipop Chainsaw Recomp

<p align="center">
  <img src="assets/banner.png" alt="Lollipop Chainsaw Recomp" width="100%">
</p>

Play the original Xbox 360 version of Lollipop Chainsaw on Windows, using your
own copy of the game. Game files are not included.

Keyboard, mouse and controllers are supported. Display, graphics, audio and
control settings are available through the game's Options menu.
Select Quit Game on the main menu to exit, then confirm.

## Getting started

You need 64-bit Windows, an internet connection for setup, and an ISO of the
[supported Xbox 360 disc](docs/BUILDING.md#supported-game).

From the repository folder, run this in PowerShell to build the launcher:

```powershell
./launcher/build.ps1
```

Then open `out/launcher/Lollipop Chainsaw Launcher.exe`.

1. Select Set up game, then choose your ISO and an install folder in Manage game.
2. Select Install game. The launcher downloads the tools it needs and builds the game
   on your PC. The first installation can take a while.
3. Select Play. For later launches, open
   `Play Lollipop Chainsaw.exe` in the install folder.

For an easier first run, choose Difficulty, then Easy on the game's title menu
before selecting Game Start.

If installation is interrupted after importing the disc, reopen the launcher,
select Locate game and choose the same folder, then select Repair game.
The imported files are reused.
Your saves are in the install folder's `user/` directory. Keep that directory
when moving or reinstalling the game.

The bundled recomp version is visible in the corner. Updates tracks launcher, bundled recomp and installed game
versions separately. Manage game provides shader preparation and shortcuts to
the game and saves folders. Game settings are in the game's Options menu.
The launcher's Game settings page configures display mode, window resolution,
VSync, aspect ratio, render resolution, anti-aliasing, texture filtering,
frame rate and dithering. Changes are saved with Save settings or before Play
and apply on the next launch. Audio and control preferences are preserved.
Help & support opens an in-app guide; close it with Got it, the close button or Escape.

Open Updates to read build notes and check for a stable GitHub release.
To apply a newer version, close the game, download and open its launcher, select
Locate game and choose your existing install folder. In Manage game, select
Apply launcher build / Repair. You do not need the ISO again. Your saves and
settings are kept. Update checks need internet; launching an installed game does not.

## Launcher

![Launcher overview with the game ready to play](docs/screenshots/launcher-home.png)

<details>
<summary>Graphics settings and updates</summary>

![Launcher display and graphics settings](docs/screenshots/launcher-settings.png)

![Launcher versions and Markdown release notes](docs/screenshots/launcher-updates.png)

</details>

## Known issues

The project is still in development. Menus and early gameplay have been tested,
but a full playthrough has not been verified. Some loading and cutscene
transitions can stall. Graphics crashes have occurred during checkpoint reloads
and a later prologue transition.

See the [compatibility notes](docs/COMPATIBILITY.md) for details.

## Development

The [build guide](docs/BUILDING.md) covers requirements and manual builds.
Launcher source is in [launcher/](launcher/). The
[development guide](docs/DEVELOPMENT.md) covers the game code and testing tools.
Recent changes are in the [changelog](CHANGELOG.md).

## Credits

Lollipop Chainsaw Recomp by [kxrxh](https://github.com/kxrxh).

Built with [ReXGlue](https://github.com/rexglue/rexglue-sdk) and its Xenia
foundations, with fixes adapted from PM.RAZOR's SSX work. See
[THIRD_PARTY.md](THIRD_PARTY.md) for credits and artwork notices, and
[LICENSE](LICENSE) for the BSD-3-Clause project license. This is an unofficial project.
