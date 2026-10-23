# Zero Racers 0.0.5

- F7 opens twelve save-state slots with thumbnails and timestamps. Arrows
  select, S saves, Enter loads, and Escape cancels.
- F8 opens the rewind filmstrip. Left/Right scrub, Enter resumes at the
  selected point, and Escape cancels without changing the live game.
- Controller shortcuts: Select+R for states, Select+L for rewind; A selects
  or loads, X saves, and B cancels. Menus pause execution and audio.
- Rewind defaults to 32 snapshots every six frames, configurable in Settings.
  F5/F6 remain quick save/load. The default view shows one eye.
- Full machine, both-eye framebuffer, audio synthesis and color-provenance
  state is captured. Native cartridge progress is protected separately.

Snapshots are specific to the current build; older snapshots may be rejected.
Keep `vbrecomp.cfg`, `saves/` and `mods/` when updating. No ROM is included.
The optional color package remains off by default. Netplay is not included.

Gameplay and the F7/F8 UI were approved by the owner. Automated qualification
covered both eyes independently, full machine state, stereo PCM, current-build
save/load, rewind, native-save protection, transactional invalid-input rejection,
and stock/color UI behavior. See the framework's qualification ledger for the
precise reference and performance scope. Earlier serializer CPU measurements
do not quantify the new UI's capture overhead or interactive cadence.
