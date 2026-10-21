# TotalChaosAGA711Recomp

Static recompilation of **Total Chaos AGA 7.11** (Amiga, James Conwell + Team Chaos, 2008) to a native PC
program, with AmigaOS and the Amiga custom chips reimplemented in C. Built on the same recompiler and runner as
[Chaos25Recomp](https://github.com/SirL0LL0/Chaos25Recomp).

**Status:** early. The hard disk image is extracted and analysed, and the memory image for the recompiler is
prepared. Nothing runs yet. See `documentazione/analisi.md` (Italian).

No game files are included: you need your own copy of the game.

```
python tools/estrai_hdf.py TotalChaos711.hdf dati_gioco
python game/prepara_immagine.py dati_gioco/DH0/TotalChaos
```

## Terze parti

- Modello schermo **Lola Retro 1084S**: preset Commodore 1084S di [Lola Retro](https://github.com/zoran123456/lola-retro) per Amiberry, (c) 2026 Zoki, licenza MIT (`terze_parti/lola-retro/LICENSE`).
