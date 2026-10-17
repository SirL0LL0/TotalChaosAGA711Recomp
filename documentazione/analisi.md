# Total Chaos AGA 7.11: analisi per il port

Analisi del 15/10/2026, fatta sull'hardfile `..\TotalChaos711.hdf` (quello con le nostre personalizzazioni: niente
limite di turni, Load riattivato, traduzione italiana, campioni gia' decompressi). Estratto con
`tools/estrai_hdf.py` in `dati_gioco/DH0` (fuori dal repository).

## L'hardfile
- RDB, una partizione `DH0`, FFS internazionale (`DOS\3`), volume "TotalChaos", 256 MB; 7757 voci.
- Sistema minimo Workbench 3.1: `C:` (SetPatch, IPrefs, Assign...), `Libs:` (asl, diskfont, iffparse, locale,
  mathieee*, 680x0/68040/68060, mmu, rexxsyslib, translator, commodities), `Devs:` (narrator.device,
  Monitors/PAL e DblPAL), `Locale:`.
- `S:Startup-Sequence`: SetPatch, assegnazioni in RAM:, monitor PAL e DblPAL, `C:ChaosCheck` (requisiti),
  `CD SYS:TotalChaos`, `C:VideoSel` (scelta video: con DblPAL esce con WARN), poi `Chaos -p -t` (DblPAL) o
  `Chaos -t`. ChaosCheck e VideoSel sono nostri (sorgenti in `..\..\Documenti\ChaosCheck_src`, `VideoSel_src`).
- Il gioco sta in `TotalChaos/`: `Chaos` (eseguibile), `Chaos.prefs`, `*.prb`, `HighScores`, e i dati:

| Cartella | File | Dimensione | Contenuto |
|---|---|---|---|
| Music | 176 | 47,6 MB | moduli Protracker (`Mod.ChaosTheme*`) |
| Terrain | 441 | 52,8 MB | terreni e sfondi |
| Sounds | 488 | 11,9 MB | effetti (8SVX) |
| gfx | 123 | 7,4 MB | grafica |
| Screens | 16 | 4,4 MB | schermate (IFF e memoria video grezza) |
| CreatureNameSpeech, MiscellaneousSpeech | | | voci campionate (8SVX) |
| Catalogs | 11 | 0,2 MB | testi per locale.library (italiano compreso) |
| fonts, ChunkyFonts | 42 + 12 | 0,8 MB | font Amiga e font "chunky" |
| libs | 4 | 124 KB | chunkyfonts, iff, req, z |
| c | 22 | 0,7 MB | strumenti di sviluppo dell'autore (non servono al gioco) |

## L'eseguibile `Chaos`
- 1.294.336 byte, 12 hunk. Rispetto a `Chaos.orig` (nella copia `..\TotalChaosAGAr7`) il codice e' piu' lungo di
  772 byte: e' la routine di Load aggiunta da `..\..\Documenti\Patch_src\patch_totalchaos711.py`.

| Hunk | Tipo | Byte | Memoria | Note |
|---|---|---|---|---|
| 0 | CODE | 582.384 | qualsiasi | 19.645 rilocazioni, simboli della parte assembly |
| 1, 2 | DATA | 300 + 300 | CHIP | sprite (`BlankSprite`, `HighlightSprite1-4`) |
| 3 | DATA | 20.308 | qualsiasi | tabelle (`_Vblank_Counter`, `Mult640Table`, `JCSinTable`...) |
| 4 | DATA | 528.476 | qualsiasi | dati del programma C (16.019 rilocazioni) |
| 5 | BSS | 1.547.400 | qualsiasi | |
| 6 | DATA | 4.100 | qualsiasi | |
| 7, 9, 10 | BSS | 6.404 + 2.048 + 4 | CHIP | buffer per i chip custom |
| 8 | DATA | 8 | CHIP | |
| 11 | DATA | 11.620 (+BSS) | qualsiasi | |

- Compilatore SAS/C (nomi con `@`, argomenti nei registri) piu' moduli in assembly: P2C (chunky-to-planar),
  disegno delle caselle e delle ombre, divisioni e distanze, `MinimigTest`.
- **Codice 68000**: nella parte decodificata non ci sono istruzioni FPU, `mul/div.l`, bitfield, `move16`. Il 68060
  consigliato serve solo per la velocita' (P2C a 640x512x256, animazioni, Protracker). Il ricompilatore va bene
  cosi' com'e'.
- Librerie aperte: dos, exec, graphics, intuition, diskfont, asl, iffparse, keymap, locale (cataloghi),
  translator, req, reqtools, iff, chunkyfonts, z, xpkmaster/compression (facoltative); device: audio, narrator,
  timer, input; risorse: ciaa, ciab.

### Hardware diretto (da emulare, come in Rock Star Ate My Hamster)
- **Paula**: player Protracker proprio ("1 line DMA wait"): DMACON `$DFF096`, registri audio da `$DFF0A0`,
  INTENA/INTREQ `$DFF09A/$09C` con gli interrupt audio (`$0780`), attesa di una riga leggendo VHPOSR `$DFF007`.
- **CIA**: timer di CIA-B (`$BFD400/$BFD600`, `$BFDE00/$BFDF00`) per il tempo del Protracker; filtro audio
  (`$BFE001` bit 1); porta parallela (`$BFE101`, `$BFD000`: adattatore joystick 3-4).
- **Video**: BEAMCON0 `$DFF1DC` (`@ntsc`/`@pal`), BPLCON1 `$DFF102`, sprite per la casella evidenziata, "beam
  tracking" (attese sulla posizione del pennello) per animazioni senza sfarfallio.
- Schermo AGA 640x512 a 256 colori (8 bitplane, palette a 24 bit): il gioco disegna in chunky e converte con
  il suo P2C nella memoria chip.

## Cosa serve al runner (rispetto a Chaos25Recomp)
1. **AGA**: 8 bitplane, LoadRGB32/SetRGB32, modi HIRES|LACE e DblPAL, sprite AGA; P2C scrive direttamente nei
   bitplane.
2. **Hardware**: Paula a DMA con interrupt audio, CIA-B timer, VHPOSR/VPOSR coerenti col tempo emulato (da RAMH:
   `amiga_hw.c`, `amiga_paula.c`).
3. **Librerie di sistema 3.x**: locale.library (cataloghi), asl.library (requester), iffparse; le librerie del
   disco (`Libs:`) sono codice 68000 come req/translator in Chaos 2.5d e si possono ricompilare insieme al gioco.
4. **Memoria**: 2 MB chip (hunk CHIP, campioni, bitplane) piu' 32 MB e oltre di fast; `prepara_immagine.py` per
   ora mette tutto di seguito a `$200000`.
5. **Voce**: `Devs:narrator.device`, `Libs:translator.library` e `libs/req.library` sono identici (MD5) a quelli
   di Chaos 2.5d, che girano gia' ricompilati nel suo runner; in piu' campioni 8SVX per i nomi delle creature.
6. **File**: molti caricamenti da disco (moduli da 300 KB, terreni), anche compressi (z.library, xpk).

## Stato
- [x] Estrazione dell'hardfile (`tools/estrai_hdf.py`).
- [x] Struttura del progetto da Chaos25Recomp (runner, ricompilatore, sottomoduli, strumenti).
- [x] `game/prepara_immagine.py`: programma, librerie del gioco e del Workbench (diskfont, translator,
  locale, iffparse, asl, mathieee*, narrator.device). Prima le librerie, poi il programma: il profilo
  genesis di m68k-recomp-core vede codice solo sotto $400000.
- [x] Hunk CHIP a $1000 (`hunk_amiga.carica(..., base_chip)`), la chip libera dopo (`os_inizio_chip`).
- [x] `game/game.toml`, `game/tc_spec.c`, CMakeLists: il gioco ricompilato (circa 7400 funzioni) gira.
- [x] Memoria a 32 bit: 2 MB chip, fast bassa fino a $A00000, 128 MB di fast da $08000000.
- [x] Librerie 3.x nel runner: exec/dos/graphics/intuition con la tabella V40 (nomi in
  `amiga_nomi_v40.c`) e le funzioni che servono; utility, keymap, layers e asl in C.
- [x] AGA: ColorMap V39 a 24 bit, LoadRGB32/SetRGB32/GetRGB32, schermi a 8 piani, interleaved, HAM8,
  OpenScreenTagList/OpenWindowTagList. La schermata del titolo (HAM8) e la configurazione si vedono.
- [x] Musica: il Protracker del gioco gira sul timer A del CIA-A (ciaa.resource) e scrive Paula.
- [x] Video solo degli schermi visibili (2,8 ms a quadro); 10 schermi aperti (MAX_SCHERMI 16): prima gli
  ultimi due (pulsanti "?" ed "END TURN" del pannello) restavano fuori e ScreenToFront rompeva l'elenco.
- [x] RawDoFmt con argomenti posizionali (`%1$s: %2$s x%3$ld` dei cataloghi).
- [x] Gadget stringa che non cancellano le etichette accanto (impostazioni II); funzioni V39 di graphics
  (GetAPen, SetABPenDrMd, AllocBitMap, WriteChunkyPixels, ObtainBestPen...).
- [x] Menu "nuovo aspetto": penne della barra da SA_Pens (BARDETAILPEN/BARBLOCKPEN).
- [x] Effetti e voci: `JCsound`/`JCspeech` lanciati con Execute, fatti in C (8SVX, TEXTLINK, audio.device).
- [x] Switch SAS/C da 757 voci a $23CD1A (incantesimi come Subversion): prima uscivano dalla tabella.
- [x] Partita a 8 maghi del computer: 20000 quadri senza errori (il pannello destro e' vuoto per i maghi del
  computer anche sull'Amiga: il manuale dice che si riempie solo per gli umani).
- [x] Video DblPAL ("-p -t" come la Startup-Sequence): 512 righe vere, finestra riempita in proporzione,
  mouse sullo stesso rettangolo del disegno.
- [x] Load/Save: requester di req.library, poi `c/lha a|x` fatto da amiga_lha.c (si apre anche un
  salvataggio -lh5- fatto sull'Amiga). asl.library con la finestra Apri/Salva di Windows.
- [x] Puntatore 2x2 anche su schermi LACE/DblPAL; sprite di graphics (riquadro di selezione) disegnati;
  il gadget UNDO compare dopo una mossa e annulla.
- [x] Prove a 8 maghi del computer (`tools/prove_cpu.py`, 16/10): 64 partite con opzioni a caso, nessun
  crash; finiscono con "YOU HAVE WON" o, se non resta nessuno, con "nuovo gioco?" e il ritorno alle
  impostazioni. Copertura delle prove: circa 40% dei punti di chiamata del programma (il resto e' la parte
  dei giocatori umani, editor, registrazioni, lingue), 100 suoni di lancio su 153, tutte le 29 opzioni.
- [x] Partite giocate a mano con `--debug` (15-16/10, main e branch `pacchetto-aminet`): 6351 punti di
  chiamata del programma su 13312 (47,7%), nessuna funzione di sistema mancante, tutte le voci di menu
  usate almeno una volta. Il branch (librerie del Workbench in C) da solo: 3524 punti, 303 su 989 delle
  librerie del gioco.
- [x] Branch `pacchetto-aminet`: installazione dal pacchetto di Aminet in `<exe>/install` dal launcher;
  diskfont/iffparse/translator in C. Corretti sul branch (16/10): nome del font in dfh_Name (voci del menu
  Controls rovinate), stato dei canali audio condiviso tra audio.device e DMACON.
- [x] Partite giocate fino al 17/10: 8252 punti del programma su 13312 (62,0%; con le prove 62,3%), 382 su
  991 delle librerie, 212 funzioni di sistema usate e nessuna mancante, 53 voci di menu, 82 suoni di lancio
  su 153, 311 nomi su 976, quasi tutti i tasti.
- [x] Chiusura in Load (16/10): due funzioni di req.library ($206298, $2062A6) passate come puntatori per
  scorrere l'elenco dei file; aggiunte a funzioni_scoperte.toml.
- [x] Barre di scorrimento, filtri dei requester, tick dei timer CIA distribuiti nel quadro (moduli MED a
  piu' di 50 tick al secondo), frasi fisse in italiano (17/10).
- [ ] MENUVERIFY.
- [ ] Riga gialla doppia sotto la barra dei menu e "schermo nero" che ogni tanto compare (segnalati il 17/10,
  non riprodotti: serve una foto Shift+F11).
- [ ] Filtro dei terreni (quali file sono compatibili: da chiedere all'utente).
