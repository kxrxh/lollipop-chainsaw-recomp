# Lollipop Chainsaw Recomp

<p align="center">
  <img src="assets/banner.png" alt="Lollipop Chainsaw Recomp" width="100%">
</p>

Play the original Xbox 360 version of Lollipop Chainsaw on Windows, using your
own copy of the game. Game files are not included.

Keyboard, mouse and controllers are supported. Display, graphics, audio and
control settings are available in Options, or by pressing F4.

## Getting started

You need 64-bit Windows, an internet connection for setup, and an ISO of the
[supported Xbox 360 disc](docs/BUILDING.md#supported-game).

From the repository folder, run this in PowerShell to build the installer:

```powershell
./installer/build.ps1
```

Then open `out/installer/Lollipop Chainsaw Setup.exe`.

1. Choose your ISO and an install folder.
2. Select Install game. Setup downloads the tools it needs and builds the game
   on your PC. The first installation can take a while.
3. Select Play Lollipop Chainsaw. For later launches, open
   `Play Lollipop Chainsaw.exe` in the install folder.

If setup is interrupted after importing the disc, open it again and choose the
same folder, then select Update / Repair. The imported files are reused.
Your saves are in the install folder's `user/` directory. Keep that directory
when moving or reinstalling the game.

To apply a newer version, close the game, open the new setup, choose your
existing install folder and select Update / Repair. You do not need the ISO
again. Your saves and settings are kept.

## Known issues

The project is still in development. Menus and early gameplay have been tested,
but a full playthrough has not been verified. Some loading and cutscene
transitions can stall, and a rare graphics crash has been seen after Continue.

See the [compatibility notes](docs/COMPATIBILITY.md) for details.

## Development

The [build guide](docs/BUILDING.md) covers requirements and manual builds.
Installer source is in [installer/](installer/). The
[development guide](docs/DEVELOPMENT.md) covers the game code and testing tools.
Recent changes are in the [changelog](CHANGELOG.md).

## Credits

Lollipop Chainsaw Recomp by [kxrxh](https://github.com/kxrxh).

Built with [ReXGlue](https://github.com/rexglue/rexglue-sdk) and its Xenia
foundations, with fixes adapted from PM.RAZOR's SSX work. See
[THIRD_PARTY.md](THIRD_PARTY.md) for credits and artwork notices, and
[LICENSE](LICENSE) for the project license. This is an unofficial project.
