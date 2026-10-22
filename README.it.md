# TotalChaosAGA711Recomp

[English](README.md) | **Italiano**

Ricompilazione statica di **Total Chaos AGA 7.11** (Amiga, James Conwell + Team Chaos, 2008) in un programma
nativo per Windows, con AmigaOS e i chip custom dell'Amiga riscritti in C, e la **traduzione italiana completa**.
Usa lo stesso ricompilatore e lo stesso runner di [Chaos25Recomp](https://github.com/SirL0LL0/Chaos25Recomp).

I file del gioco non sono inclusi: il gioco si installa dal pacchetto gratuito di Aminet
[TotalChaosAGAr6.lha](https://aminet.net/package/game/strat/TotalChaosAGAr6).
L'ultima versione si scarica dalle [Release](https://github.com/SirL0LL0/TotalChaosAGA711Recomp/releases).

## Caratteristiche

- **Programma nativo per PC**: il codice 68k del gioco e delle sue librerie (chunkyfonts, iff, req, z) è
  ricompilato in C; AmigaOS (exec, dos, intuition, graphics, layers, audio.device, narrator, asl, iffparse,
  diskfont...), Paula, i CIA e i chip custom sono riscritti nel runner. Musashi esegue il poco codice che il
  gioco costruisce mentre gira.
- **Installazione dal pacchetto di Aminet**: estrae l'archivio (lh0-lh7), decomprime i 1779 campioni Shorten
  come faceva l'Amiga e applica la traduzione italiana e le correzioni (`tc_modifiche.bin`).
- **Traduzione italiana**: cataloghi del gioco, aiuto, descrizioni degli incantesimi, tutorial, testi fissi del
  programma, requester dei file; la lingua si cambia dal menu del gioco e dal launcher.
- **Launcher** (recomp-ui): lingua, modello di schermo, filtro di scala, forma dei pixel, scala della finestra,
  audio.
- **Modelli di schermo**: pixel nitidi (con il filtro di scala a scelta: pixel netti, sharp bilinear,
  bilineare, bicubico, Lanczos, Scale2x), Commodore 1084S, CRT Easymode, scanline e **Lola Retro 1084S** (tutta
  la catena analogica: segnale composito, fascio, maschera a fessure, vetro, curvatura).
- **Forme dei pixel**: PAL e NTSC 4:3, pixel quadrati (multisync), 16:9, 16:10, riempi la finestra; scala
  intera o libera.
- **Suono**: Paula con i timer dei CIA, musica Protracker e OctaMED, effetti sonori che non vengono mai
  scartati, voce del narrator per i nomi pronunciati.
- **Correzioni ed extra**: niente limite di turni, Load riattivato, il mago "Yours" dei tutorial con il nome del
  giocatore 1, grafica PP20 del Vaso di Pandora decompressa, carattere topaz di AROS, percorsi lunghi di Windows.
- **Comandi del PC**: F12, o un clic sinistro tenendo premuto il tasto centrale, libera il mouse; il tasto
  centrale esamina, come sull'Amiga; Tab tenuto premuto è l'avanti veloce.

## Requisiti

Windows 10/11 a 64 bit con OpenGL (GLSL 3.30 per il modello di schermo Lola Retro).

## Compilazione

clang (target mingw), CMake + Ninja, Python 3, SDL2:

```
python tools/estrai_hdf.py TotalChaos711.hdf dati_gioco
python game/prepara_immagine.py dati_gioco/DH0/TotalChaos
cmake -S . -B build/cmake -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build/cmake
```

Sottomoduli: `external/m68k-recomp-core` e `external/recomp-ui` (ramo `amiga`), `external/musashi`.
I README dello zip della release si generano con `python tools/genera_readme_release.py` (dal manuale del
launcher, `game/tc_manuale.c`).

## Strumenti di prova

- `tools/prove_cpu.py`: partite intere di 8 maghi del computer senza finestra (`-variati` pesi a caso di
  incantesimi e creature, `-mirati` gli incantesimi mai sentiti, `-scimmia` un giocatore umano che clicca, preme
  tasti e sceglie menu a caso).
- `tools/prove_segreti.py`: test dei tempi degli sviluppatori (Alt+T), rigenerazione delle tile personalizzate,
  rami di errore delle preferenze, tutti i tutorial.
- `tools/copertura.py` e `tools/mappa_copertura.py`: copertura del codice di tutte le prove (unita in
  `game/copertura_base.*`, da cui parte ogni build) e mappa del codice mai eseguito.
- `--debug`: finestra di debug, registro degli eventi, dump con Shift+F11.

## Terze parti

- SDL2 (licenza zlib).
- Il launcher [recomp-ui](https://github.com/SirL0LL0/recomp-ui) (MIT).
- Modello di schermo **Lola Retro 1084S**: il preset Commodore 1084S di
  [Lola Retro](https://github.com/zoran123456/lola-retro) per Amiberry, (c) 2026 Zoki, licenza MIT
  (`terze_parti/lola-retro/LICENSE`).
- Carattere topaz di AROS (AROS Public License).
- Caratteri del launcher: Lato e Noto Sans Symbols 2 (SIL Open Font License), OpenMoji (CC BY-SA 4.0).
