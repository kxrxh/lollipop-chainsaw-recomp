# Lollipop Chainsaw Recomp

<p align="center">
  <img src="assets/banner.png" alt="Lollipop Chainsaw Recomp" width="100%">
</p>

The original Xbox 360 game, recompiled for Windows. Requires your own game copy;
game files are not included.

## Getting started

Requires Windows x64, internet for setup, and an ISO of the
[supported Xbox 360 disc](docs/BUILDING.md#supported-game).

Build the launcher from PowerShell:

```powershell
./launcher/build.ps1
```

Open `out/launcher/Lollipop Chainsaw Launcher.exe`:

1. **Set up game** → choose your ISO and an install folder.
2. **Install game** → wait for the build to finish.
3. **Play**. Next time, open `Play Lollipop Chainsaw.exe` in the install folder.

Display and graphics: launcher **Game settings**. Audio and controls: in-game **Options**.
Back up the install folder's `user/` directory to keep your saves.

## Launcher

![Launcher overview with the game ready to play](docs/screenshots/launcher-home.png)

<details>
<summary>Graphics settings and updates</summary>

![Launcher display and graphics settings](docs/screenshots/launcher-settings.png)

![Launcher versions and Markdown release notes](docs/screenshots/launcher-updates.png)

</details>

## Known issues

A full playthrough is unverified. Loading/cutscene stalls and D3D12 crashes remain.
See [compatibility and troubleshooting](docs/COMPATIBILITY.md).

## Docs

[Build guide](docs/BUILDING.md) ·
[Update / repair](docs/BUILDING.md#updating-or-removing-an-installation) ·
[Development](docs/DEVELOPMENT.md) · [Changelog](CHANGELOG.md)

## Credits

By [kxrxh](https://github.com/kxrxh). Built with
[ReXGlue](https://github.com/rexglue/rexglue-sdk) and Xenia.
[BSD-3-Clause](LICENSE) · [Credits and artwork notices](THIRD_PARTY.md).
