First Windows x64 release of Zero Racers Recompiled, with the shared Virtual
Boy recomp-ui launcher and original red graphics.

## Download and play

1. Download **ZeroRacersVirtualBoyRecomp-windows-x64.zip** and extract everything.
2. Run **ZeroRacersVirtualBoyRecomp.exe**, select your own extracted Zero Racers
   Switch Online `.vb` cartridge, and press **Play**.
3. Configure controls and display/audio preferences in the launcher. Escape
   opens settings during play.

Required ROM: 1,048,576 bytes, CRC32 `71553796`, SHA-256
`47421cd82dfd414d042d2f7f9db51e657d414d4ddfa46887efb544029fa11ce7`.
No ROM is included. `SHA256SUMS.txt` verifies the download.

## Included

- Native V810-to-C recompilation with interpreter fallback for uncovered code.
- Virtual Boy theme, Zero Racers thumbnail, persistent settings, controller
  configuration, in-game settings, mod management and save persistence.
- Original red rendering with stereo display support.
- Player build without TCP debug tools; developer source provides TCP control,
  an independent Beetle oracle, and repeatable comparison tools.

Color enhancement is a separate follow-up; no color mod is included.

## Validation

- 39-checkpoint driving route through frame 4,000 matches the independent
  oracle across CPU, memory, devices, both eyes, presentation and audio.
- 41-checkpoint menu/start route through frame 2,400 has zero pixel differences
  in both eyes; 16 host presentation captures also match.
- Determinism, interpreter equivalence, deliberate fault detection, TCP,
  saves, keyboard/in-game UI and Mario's Tennis regression checks pass.

These are the tested routes, not a claim that every course and mode is covered.
The ZIP's `build-info.json` records source commits and binary hashes. See the
README for clean source setup and developer tools.
