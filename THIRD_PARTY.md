# Credits and third-party notices

## Project

Lollipop Chainsaw Recomp by [kxrxh](https://github.com/kxrxh).
Original code uses the [BSD-3-Clause license](LICENSE). Source and binary
redistributions must retain the copyright notice, license terms and disclaimer.

## Runtime and tools

| Component | Use | License / notice |
| --- | --- | --- |
| [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) | Recompilation, runtime and Xenos graphics | [BSD notice](patches/LICENSE.rexglue) |
| [Xenia](https://github.com/xenia-project/xenia) | ReXGlue foundations | Included in the ReXGlue notice |
| [libmspack](https://github.com/kyz/libmspack) | LZX decoding, with a separate XMem padding mode in the SDK patch | [LGPL 2.1 notice](patches/LICENSE.libmspack) |
| [PM.RAZOR / SSX-RexGlue](https://github.com/pmrazor/SSX-RexGlue) | Adapted texture-copy bounds and scaled-wrapper fixes | [BSD notice](patches/SSX-RexGlue-LICENSE.txt) |
| [LLVM 20.1.8](https://github.com/llvm/llvm-project/releases/tag/llvmorg-20.1.8) | Bundled minimal C++ toolchain | [LLVM notice](launcher/assets/LICENSE.LLVM), [Clang notice](launcher/assets/LICENSE.Clang), [LLD notice](launcher/assets/LICENSE.LLD) |
| [extract-xiso](https://github.com/XboxDev/extract-xiso) | Disc extraction | [Upstream license](https://github.com/XboxDev/extract-xiso/blob/master/LICENSE.TXT) |
| [PowerShell](https://github.com/PowerShell/PowerShell) | Build, packaging and development scripts | [Upstream license](https://github.com/PowerShell/PowerShell/blob/master/LICENSE.txt) |
| [.NET 10 / WPF](https://github.com/dotnet/wpf) | Self-contained launcher runtime | [MIT license](launcher/assets/LICENSE.NET), [runtime notices](launcher/assets/NOTICES.NET) |
| [Markdig 1.6.0](https://github.com/xoofx/markdig) | Launcher Markdown parsing | [BSD-2-Clause notice](launcher/assets/LICENSE.Markdig) |
| [Microsoft C++ Build Tools](https://visualstudio.microsoft.com/downloads/) | C++ headers and x64 libraries, reused from an installed toolset or downloaded from official VSIX packages | [Microsoft terms](https://visualstudio.microsoft.com/license-terms/vs2022-ga-diagnosticbuildtools/) |
| [Windows SDK](https://www.nuget.org/packages/Microsoft.Windows.SDK.CPP/10.0.26100.7705) | Native headers, resource tools and x64 libraries, reused locally or downloaded as SDK NuGet packages | [SDK terms](https://aka.ms/WinSDKLicenseURL) |
| [Visual C++ Runtime](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files) | App-local runtime DLLs bundled with the SDK; older installations can also reuse a compatible installed runtime or download the official Microsoft runtime VSIX | Microsoft redistribution terms |

Tools and SDK dependencies retain their own licenses. Preserve their notices
when distributing them; the project license does not replace or restrict them.
The bundled SDK keeps its dependency notices under `licenses/`, with pinned source
revisions and the SDK patches used to produce it. It contains no game translations.

[Re-Cherry](https://github.com/MaxDeadBear/Re-Cherry) provided research leads;
its implementation was not copied.

## Game and artwork

Lollipop Chainsaw and Lollipop Chainsaw RePOP, their characters, logos and
game data belong to their respective rights holders. This is an unofficial
project and does not imply endorsement. Game code, game data, promotional artwork,
characters and trademarks are not covered by the project license.

The README banner is adapted from the supplied RePOP promotional artwork, with the
subtitle changed to RECOMP. The application icon follows the supplied game
logo's art style.
The launcher's title logo in `launcher/assets/title-logo.png` is adapted from the supplied
RECOMP banner logo. OpenAI image editing was used to isolate and upscale it with
a transparent background.

OpenAI image generation was used for:

- The settings panel and button textures, using the original menu as a style
  reference. These are not extracted game textures.
- The native Quit Game slot artwork in `assets/settings/quit-game.png`, matching
  the original main-menu style. Its animation and navigation come from the
  user's own disc assets at runtime.
- The launcher illustration in `launcher/assets/hero.png`, using the
  [original developer's gallery](https://www.grasshopper.co.jp/en/) and
  [official RePOP promotional artwork](https://repop.lollipopchainsaw.com/en/)
  as references. The character illustration is unofficial fan artwork.

The banner, application icon, launcher title logo and installer character illustration are excluded
from the project code license.

Disc images, extracted game data, the original executable and generated
translations of the game code are not included in the repository or source kit. Users supply
their own game copy and generate the translations locally.
