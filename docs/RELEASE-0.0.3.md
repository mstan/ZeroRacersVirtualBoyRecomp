Zero Racers Recompiled 0.0.3 bundles color mod 0.2.1, with warm yellow repair
lanes and animated repair beams. The tunnel keeps the default cool silver;
the duplicate silver selector has been removed. Original red remains the
default, and the color mod is opt-in.

## Download and play

1. Download **ZeroRacersVirtualBoyRecomp-windows-x64.zip** and extract everything.
2. Run **ZeroRacersVirtualBoyRecomp.exe**, choose your own extracted Zero Racers
   Switch Online `.vb` cartridge, and press **Play**.
3. For color, open **Mods**, choose **Install .vbmod**, select the included
   **zero-racers-full-color-0.2.1.vbmod**, then enable **Wireframe color**.

The separate `.vbmod` download is the same package included in the ZIP.
Installing it alone leaves color disabled. Disable **Wireframe color** at any
time to restore native red. `SHA256SUMS.txt` covers both downloads.

Required ROM: 1,048,576 bytes, CRC32 `71553796`, SHA-256
`47421cd82dfd414d042d2f7f9db51e657d414d4ddfa46887efb544029fa11ce7`.
No ROM is included or patched.

## Included features

- Silver tunnels, distinct machine/NPC colors, colored menus and HUDs, and
  yellow repair lanes and beams.
- Native line positions, thickness, black gaps, stereo geometry and brightness
  are preserved.
- recomp-ui launcher, ROM selection, persistent settings, saves, in-game
  settings, and interpreter fallback.
- SDL controller support with D-pad or left-stick steering, right-stick input
  for the Virtual Boy's right D-pad, face buttons, shoulders and Start/Select.
  Select your controller in the launcher; defaults, remapping and deadzones
  are available.

## Updating

Close the game and extract the new ZIP into the existing installation. Keep
`vbrecomp.cfg`, `keybinds.ini`, `saves/` and `mods/`. Install the new 0.2.1 mod
package through **Mods** to update an existing color installation. Existing
preferences are preserved; no personal settings or saves are bundled.

## Validation

The focused repair comparison covers the opening and two native repair-beam
animation phases in both eyes. CPU state, WRAM, VRAM, native images, visible
masks and brightness match with color off/on. The native repair flag activates
and damage decreases during the captured repair effect. Earlier menu, driving,
controller and independent-oracle checks cover the existing functionality.

The color package remains experimental; every course and model variant has
not been individually reviewed. See `docs/MODS-AND-COLOR.md` for screenshots
and comparison reports. `build-info.json` identifies source commits and hashes.
