# Credits and third-party notices

## Project

Lollipop Chainsaw Recomp by [kxrxh](https://github.com/kxrxh).
Original code uses the [Attribution License](LICENSE). Redistributed versions
and forks must preserve the author credit and link.

## Runtime and tools

| Component | Use | License / notice |
| --- | --- | --- |
| [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) | Recompilation, runtime and Xenos graphics | [BSD notice](patches/LICENSE.rexglue) |
| [Xenia](https://github.com/xenia-project/xenia) | ReXGlue foundations | Included in the ReXGlue notice |
| [libmspack](https://github.com/kyz/libmspack) | LZX decoding, with a separate XMem padding mode in the SDK patch | [LGPL 2.1 notice](patches/LICENSE.libmspack) |
| [PM.RAZOR / SSX-RexGlue](https://github.com/pmrazor/SSX-RexGlue) | Adapted texture-copy bounds and scaled-wrapper fixes | [BSD notice](patches/SSX-RexGlue-LICENSE.txt) |
| [LLVM](https://github.com/llvm/llvm-project) | C++ toolchain | [Upstream license](https://github.com/llvm/llvm-project/blob/llvmorg-20.1.8/LICENSE.TXT) |
| [extract-xiso](https://github.com/XboxDev/extract-xiso) | Disc extraction | [Upstream license](https://github.com/XboxDev/extract-xiso/blob/master/LICENSE.TXT) |
| [MinGit](https://github.com/git-for-windows/git) | Portable Git for setup | Notices included in the upstream archive |
| [PowerShell](https://github.com/PowerShell/PowerShell) | Build, packaging and development scripts | [Upstream license](https://github.com/PowerShell/PowerShell/blob/master/LICENSE.txt) |
| [Visual Studio Build Tools](https://visualstudio.microsoft.com/downloads/) and Visual C++ Redistributable | Windows compiler libraries, SDK and runtime | Microsoft installer license terms |

Tools and SDK dependencies retain their own licenses. Preserve their notices
when distributing them; the project license does not replace or restrict them.

[Re-Cherry](https://github.com/MaxDeadBear/Re-Cherry) provided research leads;
its implementation was not copied.

## Game and artwork

Lollipop Chainsaw and Lollipop Chainsaw RePOP, their characters, logos and
game data belong to their respective rights holders. This is an unofficial
project and does not imply endorsement.

The README banner is adapted from the supplied RePOP promotional artwork, with the
subtitle changed to RECOMP. The application icon follows the supplied game
logo's art style.

OpenAI image generation was used for:

- The settings panel and button textures, using the original menu as a style
  reference. These are not extracted game textures.
- The installer illustration and paper texture in `assets/installer/`, using the
  [original developer's gallery](https://www.grasshopper.co.jp/en/) and
  [official RePOP promotional artwork](https://repop.lollipopchainsaw.com/en/)
  as references. The character illustration is unofficial fan artwork.

The banner, application icon and installer character illustration are excluded
from the project code license.

Disc images, extracted game data, the original executable and generated
translations of the game code are not included in the repository or source kit. Users supply
their own game copy and generate the translations locally.
