# Zero Racers wireframe color preview

The experimental full-color mod uses cool silver for the tunnel, blue/yellow/
green/red for the four normal machine models, and restrained HUD/menu colors.
Every original black gap, line position, thickness, overlap and stereo offset is
preserved. Native intensity, including dim lines and brightness-register fades,
remains the maximum RGB channel of each colored pixel. There are no added
surfaces, bloom or geometry. The palette is an enhancement, not a claim about
Nintendo's intended colors.

## Try it

Build this branch with `tools/build.ps1`; the normal build also creates
`build/mod-packages/zero-racers-full-color-0.1.0.vbmod`. The renderer requires the
new executable on this branch; the original 0.0.1 release cannot activate it.

```powershell
python .\tools\play-color.py
```

The helper installs the package, enables **Wireframe color** and opens recomp-ui
using a separate `build/color-profile` for settings, saves and mod selections.
It waits until the application closes. Supply `--rom` for a cartridge elsewhere.

Alternatively, install the `.vbmod` through the launcher's **Mods** page and
enable **Wireframe color**. **Tunnel tone** offers **Cool silver** and **Neutral
silver**. Turn the feature off in Mods (or the in-game settings) to restore native
red immediately. The ROM and saved game are never patched by the mod.

See the [launcher controls](color-mods.png).

![Cool silver tunnel and colored machines](color-ready.png)

![Machine selection](color-machines.png)

## How it follows the original drawing

Mario Tennis supplies the reference pattern: a game-owned, statically linked
`video.renderer` plugin, data-only `.vbmod` archive, editable named palette,
default-off feature and shared launcher controls. Its filled court/sky and
character material catalog are not used here.

Zero Racers draws its 3D wireframes directly into the framebuffer with V810
instructions. The renderer observes this pipeline without executing substitute
game logic:

| Stage | Verified cartridge location | Observation |
| --- | --- | --- |
| Model table | `FFF61080`, 16-byte records | Normal machine pairs 256/257, 258/259, 260/261, 262/263 share vertex data |
| Model edges | Store `FFF1403C`, descriptor in r6 | Attach the machine identity to the edge being copied |
| Procedural tunnel edges | Builders `FFF1420E` through `FFF1495F` | Attach the tunnel role |
| Edge list | WRAM `05004220`, 8-byte entries | Host-only parallel tags survive vertex transformation/clipping |
| Stereo projection | Stores `FFF15012`, `FFF15150`, edge pointer r25 | Propagate the edge tag to each eye's actual line command |
| Line commands | WRAM `05005230`, 32-byte entries | Track projected commands without reconstructing their geometry |
| Native rasterizer | Stores inside `FFF13AD2` through `FFF13CE6`, command pointer r17 | Attach the tag only to framebuffer pixels the CPU actually changes |

The opening tunnel end uses model 59; side sections observed at the start use
models 34/35. Their model-copy path receives the tunnel palette too. Other
unclassified models retain original ink. The four normal machine pairs use
consistent colors across selection, rotation and race-start camera motion.

The framework knows no Zero Racers addresses or colors. Its optional read-only
write observer returns an opaque presentation tag; framebuffer attribution is
updated alongside actual writes. Unchanged packed pixels keep their previous
owner, erased pixels lose attribution, and the native VIP overwrites attribution
when it draws. Both eyes and both framebuffer slots have separate metadata.

HUD/menu material masks use source atlas positions, CHR content hashes and
unflipped texel coordinates captured by the native renderer. Runtime coloring
does not use screen-coordinate bands or moving world indices. The authoring
tool uses known worlds only to label captured artwork. `materials.txt` contains
color IDs and lookup identities, not copied ROM bytes or original bitmap art.

## Scope and validation

This first pass covers the captured title, name entry, mode/machine/area/level
menus, and the Duct-A Beginner opening sequence through the starting HUD. Other
model variants, later courses and uncatalogued artwork conservatively retain
native red until mapped. The package is marked experimental for that reason.

A short 13-checkpoint route through frame 2400 preserves CPU state, WRAM, VRAM
and both raw eye images. Every presented pixel has the exact same visible/black
mask and maximum-channel intensity as the original. The focused framework source
test also checks packed writes, retained owners, erasure, both eyes and display
buffer lifetime. This is not an exhaustive gameplay or course validation.

To reproduce with your cartridge:

```powershell
python .\tools\color-capture.py --out .\validation\color-off --package .\build\mod-packages\zero-racers-full-color-0.1.0.vbmod
python .\tools\color-capture.py --out .\validation\color-on --package .\build\mod-packages\zero-racers-full-color-0.1.0.vbmod --enabled
python .\tools\validate-color.py .\validation\color-off .\validation\color-on
```

Captures are private owner-ROM data under ignored `validation/`. The commands own
and close their runtime instances. `tools/color-materials.py` rebuilds the source
material masks from `validation/color-sources`. Named colors live in
`mods/full-color/palette.txt`; rebuild the package after editing. Installed
versions are immutable, so use a fresh preview profile when changing development
package contents, and increment the package version before distributing an update.
