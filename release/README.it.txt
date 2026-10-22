TOTAL CHAOS AGA 7.11 - VERSIONE PER PC (ricompilazione statica)
================================================================

Total Chaos AGA 7.11 (Amiga, James Conwell e Team Chaos, 2008) ricompilato in un programma nativo per Windows,
con AmigaOS e i chip dell'Amiga riscritti in C, e la traduzione italiana completa.
Versione per PC: SirL0LL0, con Claude. https://github.com/SirL0LL0/TotalChaosAGA711Recomp
English version of this file: README.txt

REQUISITI
- Windows 10 o 11 a 64 bit, scheda video con OpenGL (GLSL 3.30 per il modello di schermo Lola Retro).
- Il pacchetto originale del gioco da Aminet (gratuito): TotalChaosAGAr6.lha
  https://aminet.net/package/game/strat/TotalChaosAGAr6
  Questo zip NON contiene file del gioco.

INSTALLAZIONE
1. Estrai questa cartella dove vuoi.
2. Avvia TotalChaosAGA711Recomp.exe.
3. Nel launcher trascina TotalChaosAGAr6.lha sulla finestra o usa Sfoglia, poi premi GIOCA: il gioco viene
   installato nella cartella "install" accanto al programma.

OPZIONI DEL LAUNCHER
- Lingua del gioco e del launcher (italiano o inglese, e le altre lingue del gioco).
- Modello di schermo: Nitido, Commodore 1084S, CRT Easymode, Scanline, Lola Retro 1084S (tutta la catena
  analogica).
- Filtro di scala del modello Nitido: pixel netti, sharp bilinear, bilineare, bicubico, Lanczos, Scale2x.
- Forma dei pixel: PAL o NTSC 4:3, pixel quadrati, 16:9, 16:10, riempi la finestra; scala della finestra;
  scala intera.
- Volume dell'audio.

FILE
- TotalChaosAGA711Recomp.exe  il gioco e il suo launcher
- SDL2.dll                    finestra, audio e ingressi
- tc_modifiche.bin            traduzione italiana e correzioni, applicate all'installazione
- assets\                     caratteri e immagini del launcher
- licenses\                   licenze dei componenti di terze parti

TERZE PARTI
- SDL2 (licenza zlib): licenses\SDL2-zlib.txt
- recomp-ui, il launcher (MIT): licenses\recomp-ui.txt
- Lola Retro, preset Commodore 1084S per Amiberry (MIT, Zoki, github.com/zoran123456/lola-retro):
  licenses\Lola-Retro-MIT.txt
- Carattere topaz di AROS (AROS Public License).
- Caratteri del launcher: Lato e Noto Sans Symbols 2 (SIL Open Font License), OpenMoji (CC BY-SA 4.0).

MANUALE DEL GIOCO
=================

BENVENUTO
---------
Total Chaos AGA è un gioco di strategia per un massimo di otto maghi, umani o
guidati dal computer. Ogni mago parte con una manciata di incantesimi a caso:
creature, illusioni, maledizioni, catastrofi naturali e poteri. Lo scopo è
restare l'ultimo mago vivo.

La battaglia si svolge su una scacchiera di 17 per 14 caselle. Una partita è
una serie di turni: in ogni turno ogni mago sceglie un incantesimo, lo lancia
e poi muove le creature che controlla.

Total Chaos AGA 7 è opera di James Conwell e del Team Chaos (2008), freeware
su Aminet. Questa versione per PC esegue il programma originale dell'Amiga,
ricompilato in codice nativo, con la traduzione italiana completata.


INIZIARE UNA PARTITA
--------------------

Impostazioni I: i maghi
Scegli quali degli otto maghi giocano (Attivo), quali guidi tu (Umano) e la
squadra di ciascuno: i maghi con la stessa icona di squadra partono alleati.
Clicca un nome per cambiarlo. Start porta alla schermata successiva.

Impostazioni II: il mondo
Numero di generatori (4 di partenza), incantesimi per mago (20), livello dei
maghi del computer (0-9: cambia solo gli incantesimi che ricevono), artefatti,
pergamene, cadaveri e PowerPoint sul campo, e il numero di turni (infiniti o
fino a 999). Qui si accendono anche Esplorazione, Impurità, Agente di lancio,
artefatti e pergamene nascosti, Texas Trash'em e l'aggressività e la
spietatezza del computer.

Impostazioni III: le regole
Guerre di terra e aeree, sotterranei, maghi deboli o forti, superpotenziamenti
e cattura. La modalità principiante (accesa all'inizio) limita gli incantesimi
a un insieme più semplice: spegnila per averli tutti. Start fa partire la
partita.


IL TURNO
--------

Scelta dell'incantesimo
A destra ci sono i tuoi incantesimi. Clicca quello che vuoi, oppure Fine turno
(o Esc) per non lanciare niente. Punta un incantesimo e premi q per leggerne
la descrizione, o c per vedere tutte le caselle dove si può lanciare. Un
incantesimo scelto è consumato anche se lo annulli.

Lancio
La riga in basso mostra l'incantesimo, quante volte si lancia e la portata.
Clicca la casella bersaglio; con portata 0 basta un clic. Quasi tutti gli
incantesimi richiedono che il mago veda il bersaglio. Il tasto destro o
Backspace annullano. Dopo ogni mago i generatori possono creare creature nelle
caselle libere accanto.

Aggiornamento del campo
Il computer fa crescere fuoco, inondazioni, funghi e le altre crescite, sposta
tempeste e vortici, può far crollare castelli e boschi magici (il mago dentro
riceve un incantesimo in premio) e sveglia o distrugge chi dorme.

Movimento
Clicca una creatura, poi le caselle dove andare: chi cammina va una casella
alla volta, chi vola salta subito al bersaglio. Muoversi su un nemico lo
attacca. Le creature con la E lampeggiante sono ingaggiate: non si muovono ma
possono combattere. Backspace chiude la mossa di una creatura, Fine turno o
Esc il tuo turno. I maghi salgono su creature, alberi e castelli muovendosi
sopra; il pulsante D li fa scendere.


COME SI VINCE
-------------
Uccidi tutti i maghi nemici, distruggi o blocca tutti i loro generatori (un
generatore circondato da creature o oggetti non può creare niente) e uccidi le
creature nemiche che girano ancora.

Quando un mago muore le sue creature si addormentano: chi dorme può essere
attaccato e, turno dopo turno, può sgretolarsi. Il raro potere Dead Revenge
invece lascia un generatore dove il mago è caduto. Con un limite di turni la
partita finisce allo scadere e decidono i punteggi. I punti vengono dalle
uccisioni di creature e maghi, dagli incantesimi difficili e persino dal
ripulire i cadaveri. Cinque PowerPoint Archoniani tenuti insieme danno la
vittoria subito.


MOUSE E TASTI
-------------
- Tasto sinistro: scegliere, lanciare, muovere. Tasto destro: annullare un
  incantesimo o una mossa; sulla barra in alto (nella scelta degli
  incantesimi) apre i menu.
- q: informazioni sull'incantesimo o la creatura sotto il puntatore (q di
  nuovo le chiude).
- c: bersagli possibili dell'incantesimo da lanciare. m: mosse possibili
  (Shift+m: tutta la portata anche dopo aver mosso). y: dove arriva chi vola.
- r: portata dell'arma a distanza. s: caselle che si possono davvero colpire.
  l: linea di vista.
- f, h, o: cosa una creatura sente al tatto, all'udito e all'olfatto (modalità
  Esplorazione).
- 1-8: evidenzia le creature di quel giocatore; 9: quelle indipendenti.
- Invio: come il tasto sinistro. Frecce: spostano il puntatore di una casella.
  Backspace: chiude la mossa di una creatura. Esc: chiude il tuo turno.
- Home o Ins: il tasto HELP dell'Amiga (aiuto sul momento).

Tasti di questa versione per PC
- Tab (tenuto premuto): avanti veloce, comodo quando giocano solo i maghi del
  computer.
- F12, o un clic sinistro tenendo premuto il tasto centrale: liberano il
  puntatore del mouse (il centrale da solo esamina, come Q); un clic nella
  finestra lo riprende.
- Esc in una partita di soli maghi del computer: al cambio di fase successivo
  il gioco chiede se iniziare un nuovo gioco (spunta: si torna alle
  impostazioni; croce: si continua).


I MENU
------
I menu si usano durante la scelta degli incantesimi: tieni premuto il tasto
destro sulla barra in alto.

Progetto
- Informazioni, Nuovo gioco.
- Apri: carica una partita salvata.
- Salva: salva di nuovo sull'ultima partita salvata o aperta, senza chiedere.
  Se non hai ancora salvato né aperto niente non fa nulla: usa Salva come.
- Salva come: salva con un nome nuovo (in SavedGames).
- Salva preferenze, Cambia terreno, Cambia lo sfondo, Mostra le impostazioni,
  Chiudi.

Controlli
Voce, suoni, suoni dei combattimenti, musica, effetti delle evocazioni,
punteggi a ogni turno, stile di evidenziazione, conferma di fine turno,
risultati attuali, salvataggio automatico, combattimento speciale silenzioso,
pausa del discorso, mosse e tiri mostrati in automatico.

Lingua e Aiuto
La lingua del gioco (si sceglie anche in questo launcher) e i tutorial.


QUESTA VERSIONE PER PC
----------------------

Installazione
I file del gioco non sono inclusi. Scarica TotalChaosAGAr6.lha da Aminet
(game/strat), trascinalo su questo launcher o usa Sfoglia, poi premi GIOCA. La
prima volta viene installato nella cartella "install" accanto al programma
(una ventina di secondi): l'archivio si estrae, i suoni e le musiche compressi
si decomprimono come faceva il gioco sull'Amiga e si applicano le modifiche di
questa versione (Apri, niente limite di turni, italiano). Il primissimo avvio
è un po' più lento perché il gioco si prepara gli elenchi dei file.

Impostazioni
In Impostazioni ci sono la finestra, il modello di schermo, la forma dei pixel
e l'audio. Qui si sceglie anche la lingua del gioco: con l'italiano anche
questo launcher passa in italiano. "Ripristina tutte le impostazioni" rimette
come all'inizio sia le preferenze del gioco sia quelle del launcher.

Salvataggi
Le partite salvate sono in install/TotalChaos/SavedGames, i record in
install/TotalChaos/HighScores. Reinstallando (per una versione nuova) restano.
