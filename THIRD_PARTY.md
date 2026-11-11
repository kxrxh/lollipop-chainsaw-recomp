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
| [PM.RAZOR / SSX-RexGlue](https://github.com/pmrazor/SSX-RexGlue) | Texture-copy bounds and scaled-wrapper fixes adapted in the SDK patch | [BSD notice](patches/SSX-RexGlue-LICENSE.txt) |
| [LLVM](https://github.com/llvm/llvm-project) | Local build toolchain | [Upstream license](https://github.com/llvm/llvm-project/blob/llvmorg-20.1.8/LICENSE.TXT) |
| [extract-xiso](https://github.com/XboxDev/extract-xiso) | Optional local disc-image extraction | [Upstream license](https://github.com/XboxDev/extract-xiso/blob/master/LICENSE.TXT) |

Tools and SDK dependencies are obtained and built separately. Preserve their
own license notices if distributing them. The project attribution license
does not replace or restrict the upstream licenses of those components.

[Re-Cherry](https://github.com/MaxDeadBear/Re-Cherry) was consulted for research
leads; its project implementation was not copied.

## Game and artwork

Lollipop Chainsaw and Lollipop Chainsaw RePOP, their characters, logos and
game data belong to their respective rights holders. The README banner is
adapted from the supplied RePOP promotional artwork, with the subtitle
changed to RECOMP; that artwork is excluded from the project code license.
This is an unofficial project and does not imply endorsement.

The LC application icon follows the supplied game logo's art style and is
also excluded from the project code license.

The repository and source kit contain no disc images, extracted game data,
original executable, or generated translations of the game's code. Those
files are produced locally from the user's own game copy.
