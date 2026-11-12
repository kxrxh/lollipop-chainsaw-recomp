# Credits and third-party notices

## Project

Lollipop Chainsaw Recomp by [kxrxh](https://github.com/kxrxh).
Original project code is covered by the [Attribution License](LICENSE).
Redistributed versions and forks must preserve the author credit and link.

## Runtime and tools

| Component | Use | License / notice |
| --- | --- | --- |
| [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) | Static recompilation, host runtime and Xenos graphics | [BSD notice](patches/LICENSE.rexglue) |
| [Xenia](https://github.com/xenia-project/xenia) | Foundations used by ReXGlue | Included in the ReXGlue notice |
| [libmspack](https://github.com/kyz/libmspack) | SDK LZX decoder, with a separate XMem padding mode in the shader-preparation patch | [LGPL 2.1 notice](patches/LICENSE.libmspack) |
| [PM.RAZOR / SSX-RexGlue](https://github.com/pmrazor/SSX-RexGlue) | Texture-copy bounds and scaled-wrapper fixes adapted in the SDK patch | [BSD notice](patches/SSX-RexGlue-LICENSE.txt) |
| [LLVM](https://github.com/llvm/llvm-project) | Local build toolchain | [Upstream license](https://github.com/llvm/llvm-project/blob/llvmorg-20.1.8/LICENSE.TXT) |
| [extract-xiso](https://github.com/XboxDev/extract-xiso) | Local disc-image extraction | [Upstream license](https://github.com/XboxDev/extract-xiso/blob/master/LICENSE.TXT) |
| [MinGit](https://github.com/git-for-windows/git) | Portable Git downloaded during setup | Licenses and notices accompany the upstream archive |
| [PowerShell](https://github.com/PowerShell/PowerShell) | Development and packaging scripts; not required by the standalone installer | [Upstream license](https://github.com/PowerShell/PowerShell/blob/master/LICENSE.txt) |
| [Visual Studio Build Tools](https://visualstudio.microsoft.com/downloads/) and Visual C++ Redistributable | Microsoft Windows compiler libraries, SDK and runtime | Microsoft installers and their applicable license terms |

Tools and SDK dependencies are obtained and built separately. Preserve their
own license notices if distributing them. The project attribution license
does not replace or restrict the upstream licenses of those components.

[Re-Cherry](https://github.com/MaxDeadBear/Re-Cherry) was consulted for research
leads; its project implementation was not copied.

## Game and artwork

Lollipop Chainsaw and Lollipop Chainsaw RePOP, their characters, logos and
game data belong to their respective rights holders. The banner used in the README is
adapted from the supplied RePOP promotional artwork, with the subtitle
changed to RECOMP; that artwork is excluded from the project code license.
This is an unofficial project and does not imply endorsement.

The LC application icon follows the supplied game logo's art style and is
also excluded from the project code license.

The settings panel and button textures were generated with OpenAI image
generation using the original menu as a style reference. They are not extracted
game textures.

The installer illustration and paper texture in `assets/installer/` were generated
with OpenAI image generation. Visual references were the [original developer's
gallery](https://www.grasshopper.co.jp/en/) and [official RePOP promotional
artwork](https://repop.lollipopchainsaw.com/en/). The character illustration is
unofficial fan artwork and is excluded from the project code license.

The repository and source kit contain no disc images, extracted game data,
original executable, or generated translations of the game's code. Those
files are produced locally from the user's own game copy.
