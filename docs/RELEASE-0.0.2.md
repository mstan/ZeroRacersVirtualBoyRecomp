Zero Racers Recompiled 0.0.2 adds an optional F-Zero-inspired color mod while
keeping the original red presentation as the default.

## Download and play

1. Download **ZeroRacersVirtualBoyRecomp-windows-x64.zip** and extract everything.
2. Run **ZeroRacersVirtualBoyRecomp.exe**, choose your own extracted Zero Racers
   Switch Online `.vb` cartridge, and press **Play**.
3. For color, open **Mods**, choose **Install .vbmod**, select the included
   **zero-racers-full-color-0.2.0.vbmod**, then enable **Wireframe color**.

Installing the mod alone leaves it disabled. Turn it off at any time to restore
native red. **Tunnel tone** offers cool silver and neutral silver.
The separate `.vbmod` download is the same package already included in the ZIP;
it requires the 0.0.2 executable. `SHA256SUMS.txt` covers both downloads.

Required ROM: 1,048,576 bytes, CRC32 `71553796`, SHA-256
`47421cd82dfd414d042d2f7f9db51e657d414d4ddfa46887efb544029fa11ce7`.
No ROM is included or patched.

## Changes

- Cool silver tunnels, distinct machine/NPC colors, and colored menus and HUDs.
- Dynamic counters, minimap, energy/boost indicators and give-up dialog retain
  consistent colors. Unmapped artwork uses white and geometry uses silver.
- Original line positions, thickness, black gaps, stereo geometry and native
  intensity are preserved. No added surfaces or effects.
- Shared launcher, controls, in-game settings, saves and interpreter fallback
  remain available. The production player build omits TCP debugging and CPU traces.
- SDL controller defaults support D-pad or left-stick steering, the right stick
  for the Virtual Boy's right D-pad, A/B, shoulders and Start/Select. Select your
  connected controller in the player selector; the mapping is ready to use.
  Custom bindings and deadzones are supported.

## Updating

Close the game, then extract the new ZIP into the existing installation. Keep
`vbrecomp.cfg`, `saves/` and `mods/`; no personal settings or saves are included
in the download. Existing mod preferences remain intact.

## Validation and scope

Player feedback approved the color preview as playable and ready for an initial
release. A focused 16-checkpoint comparison through frame 4,500, covering menus,
race start, NPC traffic, HUD changes, give-up and resume, preserves CPU state,
WRAM, VRAM, both native eye images, visible masks and maximum-channel intensity.
The prior release's independent-oracle comparisons cover the underlying game.

The color package remains experimental: every course and model variant has not
been individually reviewed. See `docs/MODS-AND-COLOR.md` for screenshots and
details. `build-info.json` records the exact source commits and artifact hashes.
