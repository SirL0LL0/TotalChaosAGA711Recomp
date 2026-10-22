# TotalChaosAGA711Recomp

Static recompilation of **Total Chaos AGA 7.11** (Amiga, James Conwell + Team Chaos, 2008) to a native Windows
program, with AmigaOS and the Amiga custom chips reimplemented in C, and a **full Italian translation**.
Built on the same recompiler and runner as [Chaos25Recomp](https://github.com/SirL0LL0/Chaos25Recomp).

No game files are included: the game is installed from the free Aminet package
[TotalChaosAGAr6.lha](https://aminet.net/package/game/strat/TotalChaosAGAr6).
Download the latest build from [Releases](https://github.com/SirL0LL0/TotalChaosAGA711Recomp/releases).

## Features

- **Native PC program**: the 68k code of the game and of its libraries (chunkyfonts, iff, req, z) is recompiled
  to C; AmigaOS (exec, dos, intuition, graphics, layers, audio.device, narrator, asl, iffparse, diskfont...),
  Paula, CIA and the custom chips are reimplemented in the runner. Musashi runs the little code that is built at
  run time.
- **Installer from the Aminet package**: extracts the archive (lh0-lh7), decompresses the 1779 Shorten-packed
  samples as the Amiga did, and applies the Italian translation and the fixes (`tc_modifiche.bin`).
- **Italian translation**: game catalogs, help, spell descriptions, tutorials, fixed texts in the program, file
  requesters; language switchable from the game menu and the launcher.
- **Launcher** (recomp-ui): language, screen model, scaling filter, pixel shape, window scale, audio.
- **Screen models**: sharp pixels (with a choice of scaling filter: sharp pixels, sharp bilinear, bilinear,
  bicubic, Lanczos, Scale2x), Commodore 1084S, CRT Easymode, scanlines, and **Lola Retro 1084S** (the full
  analog chain: composite signal, beam, slot mask, glass, curvature).
- **Pixel shapes**: PAL and NTSC 4:3, square pixels (multisync), 16:9, 16:10, fill the window; integer or free
  scaling.
- **Sound**: Paula with CIA timers, Protracker and OctaMED music, sound effects that are never dropped, narrator
  speech for the spoken names.
- **Fixes and extras**: no turn limit, Load re-enabled, the "Yours" tutorial wizard renamed after player 1,
  Pandora's Box PP20 graphics decompressed, AROS topaz font, long Windows paths supported.
- **PC controls**: F12, or a left click while holding the middle button, releases the mouse; the middle button
  examines, as on the Amiga; hold Tab to fast forward.

## Requirements

64-bit Windows 10/11 with OpenGL (GLSL 3.30 for the Lola Retro screen model).

## Building

clang (mingw target), CMake + Ninja, Python 3, SDL2:

```
python tools/estrai_hdf.py TotalChaos711.hdf dati_gioco
python game/prepara_immagine.py dati_gioco/DH0/TotalChaos
cmake -S . -B build/cmake -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build/cmake
```

Submodules: `external/m68k-recomp-core` and `external/recomp-ui` (branch `amiga`), `external/musashi`.

## Testing tools

- `tools/prove_cpu.py`: whole games of 8 computer wizards without a window (`-variati` random spell and creature
  weights, `-mirati` the spells never heard, `-scimmia` a human player that clicks, types and picks menus at
  random).
- `tools/prove_segreti.py`: developer timing tests (Alt+T), custom tile regeneration, preference error paths,
  every tutorial.
- `tools/copertura.py` and `tools/mappa_copertura.py`: code coverage of all runs (merged into
  `game/copertura_base.*`, which every build starts from) and a map of the code never executed.
- `--debug`: a debug window, a log of events, Shift+F11 dumps.

## In italiano

Total Chaos AGA 7.11 ricompilato per PC, con traduzione italiana completa. Si installa dal pacchetto gratuito di
Aminet `TotalChaosAGAr6.lha`: avvia `TotalChaosAGA711Recomp.exe` e indicalo nel launcher. Dal launcher si
scelgono lingua, modello di schermo (anche Lola Retro 1084S), filtro, forma dei pixel e scala. F12, o un clic
sinistro tenendo premuto il tasto centrale, libera il mouse; il centrale da solo esamina; Tab tenuto è l'avanti
veloce.

## Third parties

- SDL2 (zlib license).
- [recomp-ui](https://github.com/SirL0LL0/recomp-ui) launcher (MIT).
- Screen model **Lola Retro 1084S**: the Commodore 1084S preset of [Lola Retro](https://github.com/zoran123456/lola-retro)
  for Amiberry, (c) 2026 Zoki, MIT license (`terze_parti/lola-retro/LICENSE`).
- AROS topaz font (AROS Public License).
- Launcher fonts: Lato and Noto Sans Symbols 2 (SIL Open Font License), OpenMoji (CC BY-SA 4.0).
