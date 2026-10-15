# TotalChaosAGA711Recomp — Total Chaos AGA 7.11 (Amiga, 2008) ricompilato per PC

Note per Claude, iniziate il 15/10/2026. Rispondi in italiano. Commit qui in italiano, brevi, senza prefissi; nel
sottomodulo `m68k-recomp-core` in inglese, come gli altri commit di quel ramo.

## Cos'e'
Port per ricompilazione statica di Total Chaos AGA 7.11 (James Conwell + Team Chaos), nella versione con le nostre
personalizzazioni (niente limite di turni, Load riattivato, italiano). Si parte da **Chaos25Recomp**
(`..\..\ChaosV2.5d\Recomp`, SirL0LL0/Chaos25Recomp): stesso ricompilatore, stesso runner con AmigaOS in C, stessi
strumenti. Leggi il suo CLAUDE.md: meccanismi (narrator, audio.device, layer, semafori, recomp_rts, DosList,
riproduzione delle partite) e lezioni valgono anche qui. Il runner viene da li' e prima ancora da Rock Star Ate My
Hamster Recomp, che emula Paula, CIA e chip custom: servono, perche' questo gioco tocca l'hardware.

- Repository: SirL0LL0/TotalChaosAGA711Recomp, branch `main` (da creare su GitHub: il token di `gh` non e' valido,
  i push vanno col gestore delle credenziali di Windows).
- Sottomoduli: `external/m68k-recomp-core` e `external/recomp-ui` (fork SirL0LL0, branch `amiga`),
  `external/musashi` (kstenerud/Musashi 313ebf1). Stessi commit di Chaos25Recomp.
- `dati_gioco/` e i file del gioco sono ignorati: mai nel repository.

## Dati
- Fonte: `..\TotalChaos711.hdf` (RDB, DH0 FFS, Workbench 3.1 minimo + `TotalChaos/`). E' la versione buona:
  la copia `..\TotalChaosAGAr7` e' piu' vecchia (campioni compressi, liste diverse).
- `python tools/estrai_hdf.py ..\TotalChaos711.hdf dati_gioco` -> `dati_gioco/DH0` e `dati_gioco/DH0.elenco.txt`
  (protezioni, date, commenti).
- Le patch del nostro eseguibile e la traduzione: `..\..\Documenti\Patch_src\patch_totalchaos711.py` e
  `TotalChaos711_italiano/`; riepilogo di tutte le versioni in `..\..\Documenti\Riepilogo_versioni_Chaos.md`.

## Struttura
- `amigarecomp/` — ricompilatore e runner (copiati da Chaos25Recomp al commit 678af20).
- `game/` — `prepara_immagine.py` (programma `Chaos` + `libs/` chunkyfonts, iff, req, z), `hunk_amiga.py`.
- `tools/` — `estrai_hdf.py` piu' gli strumenti di Chaos25Recomp (dis68k, partita.ps1, riproduci.py, finestre.py...),
  da adattare quando ci sara' l'eseguibile.
- `documentazione/analisi.md` — **l'analisi del gioco**: hunk, hardware usato, librerie, cosa manca al runner.

## Build
```
python tools/estrai_hdf.py ..\TotalChaos711.hdf dati_gioco
python game/prepara_immagine.py dati_gioco\DH0\TotalChaos
cmake -S . -B build/cmake -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build/cmake
```
Prove senza finestra: `tools\partita.ps1 -quadri 1500 [-traccia] [-wav x.wav] [-foto "300-3000/300"] [-limite s]`
(i dati li cerca in `TC_DATI`, `dati` accanto all'exe o `dati_gioco\DH0` del repository). CPU: 40 volte un
68000 (68060 a 50 MHz), `AMIGA_CPU` per cambiarla. `TC_ARGOMENTI` per la riga di comando (predefinita `-p -t`).
Partite intere a 8 maghi del computer: `python tools/prove_cpu.py -n 12 -paralleli 4` (opzioni a caso, fine su
`YouHaveWon.HAM8` con `AMIGA_FINE_VITTORIA`, suoni con `AMIGA_LOG_SUONI`, punti di chiamata eseguiti con
`AMIGA_COPERTURA`; dati copiati per processo in `build/prove/dati<k>` perche' il gioco riscrive i suoi elenchi;
riepilogo in `build/prove/riepilogo.md`). Una partita: da 60000 a 430000 quadri, 30-170 s. Nella finestra Tab
tenuto = avanti veloce. `AMIGA_ASL_FILE` risponde all'asl senza finestra; `TC_RAM` sposta RAM:.

## Come si trovano le funzioni che mancano
Il gioco si ferma su `[DISPATCH MISS] $xxxxxx`: guardarlo con
`..\..\ChaosV2.5d\Recomp\build\dis68k.exe build\chaos_immagine.bin 0 <da> <a> 68020`. `prepara_immagine.py`
trova da solo: tabelle jmp/rts-nop dei player, switch SAS/C, prologhi dopo un salto (librerie), lea d16(pc)
dopo un RTS, i ritorni dei bsr all'"apri libreria o fallisci" delle librerie Commodore. Il resto va in
`game/funzioni_scoperte.toml` (con `tools/ingressi_registri.py` per le routine SAS/C a due ingressi).
Gli indirizzi dipendono dall'ordine delle librerie: non togliere librerie dall'immagine (asl c'e' ma non si usa).
Le funzioni di sistema mancanti escono come `[os] manca libreria/Nome (LVO -n)`.
Toolchain come Chaos25Recomp: clang (mingw) + CMake + Ninja in
`C:\Users\lollo\AppData\Local\retcomm\toolchains\cmake-clang-v1\latest\bin` (aggiungila al PATH). SDL2 da
`..\..\..\RAMH traduzione\sdl\SDL2-2.32.2`. Una sola build: `build\cmake`.

## Stato (15/10)
Il gioco ricompilato parte: schermata del titolo HAM8, caricamento, configurazione "Impostazioni Chaos I" a 256
colori in italiano, musica (Protracker sul timer A del CIA-A). Dettagli e cose da fare in
`documentazione/analisi.md` (Stato). Memoria a 32 bit con 128 MB di fast da $08000000 (amiga_hw.h).
Execute e' una piccola shell (List, Sort, Join, Execute di script, Delete, Copy, run...): il gioco si fa gli
elenchi dei file cosi'. Load/Save: il requester e' quello di req.library (ricompilata), poi il gioco usa
`c/lha a` / `c/lha x` con T:ChaosSavedGame: la shell li fa con amiga_lha.c (legge lh0/lh5/lh6/lh7 dei
salvataggi Amiga, scrive lh0). asl.library e' in C con la finestra Apri/Salva di Windows.
Gli sprite di graphics (GetSprite/MoveSprite: il riquadro di selezione) si disegnano in os_video, 2x2 come il
puntatore; Undo e' un gadget immagine (AddGadget) a 611,31 che compare dopo una mossa.
Effetti e voci: il gioco lancia `JCsound`/`JCspeech` (in `TotalChaos/c`) con Execute; la shell li fa in C
(`shell_jcsound` in amiga_dos.c: 8SVX, file TEXTLINK, canali da audio.device con `os_audio_c_suona` in
amiga_device.c). Senza "run" il gioco aspetta la fine della voce e poi butta i messaggi arrivati intanto (anche
un MENUPICK): e' cosi' anche sull'Amiga. Argomenti predefiniti "-p -t" (DblPAL). Il gioco apre 10 schermi
(MAX_SCHERMI 16). Le stringhe dei cataloghi hanno argomenti posizionali (%1$s): `os_formatta` li capisce.
Le prove senza finestra non sono del tutto ripetibili (l'attesa dipende dal tempo vero): una scelta di menu
puo' cadere in un quadro diverso.

## Attenzione
Il 14/10 un agente OpenCode ha cancellato file da C:\ : cancella solo con `Remove-Item -LiteralPath` su un percorso
completo verificato. Bash non riesce a fare fork: usa PowerShell e `python -I`. Negli script PowerShell niente
nomi come `rd(`: `rd` e' l'alias di Remove-Item e il comando viene bloccato.
