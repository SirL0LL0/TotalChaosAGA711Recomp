/* tc_manuale.c - il manuale nel launcher (pagina Manuale), in inglese e in
 * italiano: ricavato dalla guida del gioco (Documentation/English/
 * TotalChaosAGA_7.guide), dal readme del pacchetto e da come funziona questa
 * versione per PC. Markup del launcher: "## " sottotitolo, "- " voce
 * d'elenco, riga vuota = nuovo paragrafo. Testi in UTF-8. */
#include <string.h>

#if defined(RECOMP_LAUNCHER)
#include "recomp_launcher.h"

static const RecompLauncherCManualPage s_en[] = {
    { "Welcome",
      "Total Chaos AGA is a strategy game for up to eight wizards, human or computer controlled. "
      "Each wizard starts with a handful of random spells: creatures, illusions, curses, natural "
      "disasters and powers. The aim is to be the last wizard alive.\n"
      "\n"
      "The battle takes place on a board of 17 by 14 cells. A game is a series of rounds; in every "
      "round each wizard chooses a spell, casts it, and then moves the creatures it controls.\n"
      "\n"
      "Total Chaos AGA 7 is the work of James Conwell and Team Chaos (2008), freeware on Aminet. "
      "This PC version runs the original Amiga program, recompiled to native code, with the "
      "Italian translation completed." },
    { "Starting a game",
      "## Settings I: the wizards\n"
      "Choose which of the eight wizards play (Active), which are controlled by you (Human) and the "
      "team of each one: wizards with the same team icon start as allies. Click a name to change it. "
      "Start takes you to the next screen.\n"
      "\n"
      "## Settings II: the world\n"
      "Number of generators (default 4), spells per wizard (default 20), level of the computer "
      "wizards (0-9: it only affects the spells they receive), artifacts, scrolls, corpses and "
      "PowerPoints on the board, and the number of turns (forever, or up to 999). Options such as "
      "Exploration, Impurities, Agent casting, Hidden artifacts and scrolls, Texas Trash'em and the "
      "computer's Aggressiveness and Ruthlessness are switched on here.\n"
      "\n"
      "## Settings III: the rules\n"
      "Ground and air wars, dungeon wars, weak or strong wizards, power-ups and capture options. "
      "Newbie mode (on at first) limits the spells to an easier set: switch it off for the full "
      "repertoire. Start begins the game." },
    { "The round",
      "## Spell selection\n"
      "The right of the screen shows your spells. Click one to choose it, or End Turn (or Esc) to "
      "cast nothing. Point at a spell and press q to read about it, or c to see every cell where it "
      "could be cast. A chosen spell is used up even if you abort it.\n"
      "\n"
      "## Spell casting\n"
      "The bottom line shows the spell, how many times it is cast and its range. Click a target "
      "cell; range 0 spells just need a click. Most spells need line of sight from your wizard. "
      "The right button or Backspace aborts. After each wizard, generators may create creatures "
      "in empty adjacent cells.\n"
      "\n"
      "## Board update\n"
      "The computer grows fire, floods, fungi and the other growths, moves tempests and vortices, "
      "may collapse castles and magic woods (a wizard inside gets a bonus spell) and wakes or "
      "destroys sleepers.\n"
      "\n"
      "## Movement\n"
      "Click a creature, then the cells to move to: walkers go one cell at a time, fliers jump "
      "straight to the target. Moving onto an enemy attacks it. Creatures marked with a flashing E "
      "are engaged: they cannot move but can fight. Backspace ends a creature's move, End Turn or "
      "Esc ends your turn. Wizards can mount creatures, trees and castles by moving onto them; the "
      "D gadget dismounts." },
    { "Winning",
      "Kill every enemy wizard, kill or block every enemy generator (a generator surrounded by "
      "creatures or objects cannot create anything) and kill the enemy creatures still roaming.\n"
      "\n"
      "When a wizard dies its creatures fall asleep: sleepers can be attacked and, turn after turn, "
      "may crumble away. The rare Dead Revenge power instead leaves a generator where the wizard "
      "fell. With a turn limit the game ends when the turns run out and the scores decide. Points come from "
      "killing creatures and wizards, from difficult spells and even from clearing corpses. Five "
      "Archonian PowerPoints held at once win immediately." },
    { "Mouse and keys",
      "- Left button: select, cast, move. Right button: abort a spell or a move; on the menu bar "
      "(during spell selection) it opens the menus.\n"
      "- q: information about the spell or creature under the pointer (q again closes it).\n"
      "- c: legal targets of the spell being cast. m: legal moves (Shift+m: full range even after "
      "moving). y: fliers' reach.\n"
      "- r: range of the ranged weapon. s: cells that can really be shot. l: line of sight.\n"
      "- f, h, o: what a creature can feel, hear and smell (Exploration mode).\n"
      "- 1-8: highlight the creatures of that player; 9: the independent ones.\n"
      "- Return: like the left button. Cursor keys: move the pointer one cell. Backspace: end a "
      "creature's move. Esc: end your turn.\n"
      "- Home or Insert: the Amiga HELP key (context help).\n"
      "\n"
      "## Keys of this PC version\n"
      "- Tab (hold): fast forward, useful when only computer wizards are playing.\n"
      "- F12 or Ctrl+Alt: release the mouse pointer (the middle button examines, like Q); click in the window to take it back.\n"
      "- Esc during a game of computer wizards only: at the next change of phase the game asks "
      "whether to start a new game (tick: back to the settings; cross: carry on)." },
    { "Menus",
      "The menus are used during spell selection: keep the right button pressed on the bar at the "
      "top of the screen.\n"
      "\n"
      "## Project\n"
      "- About, New Game.\n"
      "- Load: opens a saved game.\n"
      "- Save: saves again over the last game you saved or loaded, without asking. If you have not "
      "saved or loaded anything yet it does nothing: use Save As.\n"
      "- Save As: saves with a new name (in SavedGames).\n"
      "- Save Preferences, Change Terrain, Change Backdrop, Show Game Settings, Quit.\n"
      "\n"
      "## Controls\n"
      "Speech, sounds, combat sounds, music, summoning effects, scores every turn, highlight style, "
      "end of turn confirmation, current scores, autosave, silent special combat, talk delay, "
      "automatic display of moves and shots.\n"
      "\n"
      "## Language and Help\n"
      "The game's language (also chosen in this launcher) and the tutorials." },
    { "This PC version",
      "## Installation\n"
      "The game files are not included. Download TotalChaosAGAr6.lha from Aminet (game/strat), "
      "drag it onto this launcher or use Browse, then press PLAY. The first time it is installed "
      "in the \"install\" folder next to the program (about 20 seconds): the archive is extracted, "
      "the compressed sounds and music are unpacked as the game did on the Amiga, and the changes "
      "of this port (Load, no turn limit, Italian) are applied. The very first start is a little "
      "slower because the game builds its file lists.\n"
      "\n"
      "## Settings\n"
      "Settings holds the window, screen model, aspect ratio and audio. The game language is set "
      "here too: picking Italiano also switches this launcher to Italian. \"Reset all settings\" "
      "puts back both the game preferences and the launcher's.\n"
      "\n"
      "## Saves\n"
      "Saved games are in install/TotalChaos/SavedGames, high scores in install/TotalChaos/HighScores. "
      "Reinstalling (for a new version) keeps them." },
};

static const RecompLauncherCManualPage s_it[] = {
    { "Benvenuto",
      "Total Chaos AGA è un gioco di strategia per un massimo di otto maghi, umani o guidati dal "
      "computer. Ogni mago parte con una manciata di incantesimi a caso: creature, illusioni, "
      "maledizioni, catastrofi naturali e poteri. Lo scopo è restare l'ultimo mago vivo.\n"
      "\n"
      "La battaglia si svolge su una scacchiera di 17 per 14 caselle. Una partita è una serie di "
      "turni: in ogni turno ogni mago sceglie un incantesimo, lo lancia e poi muove le creature "
      "che controlla.\n"
      "\n"
      "Total Chaos AGA 7 è opera di James Conwell e del Team Chaos (2008), freeware su Aminet. "
      "Questa versione per PC esegue il programma originale dell'Amiga, ricompilato in codice "
      "nativo, con la traduzione italiana completata." },
    { "Iniziare una partita",
      "## Impostazioni I: i maghi\n"
      "Scegli quali degli otto maghi giocano (Attivo), quali guidi tu (Umano) e la squadra di "
      "ciascuno: i maghi con la stessa icona di squadra partono alleati. Clicca un nome per "
      "cambiarlo. Start porta alla schermata successiva.\n"
      "\n"
      "## Impostazioni II: il mondo\n"
      "Numero di generatori (4 di partenza), incantesimi per mago (20), livello dei maghi del "
      "computer (0-9: cambia solo gli incantesimi che ricevono), artefatti, pergamene, cadaveri e "
      "PowerPoint sul campo, e il numero di turni (infiniti o fino a 999). Qui si accendono anche "
      "Esplorazione, Impurità, Agente di lancio, artefatti e pergamene nascosti, Texas Trash'em e "
      "l'aggressività e la spietatezza del computer.\n"
      "\n"
      "## Impostazioni III: le regole\n"
      "Guerre di terra e aeree, sotterranei, maghi deboli o forti, superpotenziamenti e cattura. "
      "La modalità principiante (accesa all'inizio) limita gli incantesimi a un insieme più "
      "semplice: spegnila per averli tutti. Start fa partire la partita." },
    { "Il turno",
      "## Scelta dell'incantesimo\n"
      "A destra ci sono i tuoi incantesimi. Clicca quello che vuoi, oppure Fine turno (o Esc) per "
      "non lanciare niente. Punta un incantesimo e premi q per leggerne la descrizione, o c per "
      "vedere tutte le caselle dove si può lanciare. Un incantesimo scelto è consumato anche se "
      "lo annulli.\n"
      "\n"
      "## Lancio\n"
      "La riga in basso mostra l'incantesimo, quante volte si lancia e la portata. Clicca la "
      "casella bersaglio; con portata 0 basta un clic. Quasi tutti gli incantesimi richiedono che "
      "il mago veda il bersaglio. Il tasto destro o Backspace annullano. Dopo ogni mago i "
      "generatori possono creare creature nelle caselle libere accanto.\n"
      "\n"
      "## Aggiornamento del campo\n"
      "Il computer fa crescere fuoco, inondazioni, funghi e le altre crescite, sposta tempeste e "
      "vortici, può far crollare castelli e boschi magici (il mago dentro riceve un incantesimo "
      "in premio) e sveglia o distrugge chi dorme.\n"
      "\n"
      "## Movimento\n"
      "Clicca una creatura, poi le caselle dove andare: chi cammina va una casella alla volta, chi "
      "vola salta subito al bersaglio. Muoversi su un nemico lo attacca. Le creature con la E "
      "lampeggiante sono ingaggiate: non si muovono ma possono combattere. Backspace chiude la "
      "mossa di una creatura, Fine turno o Esc il tuo turno. I maghi salgono su creature, alberi e "
      "castelli muovendosi sopra; il pulsante D li fa scendere." },
    { "Come si vince",
      "Uccidi tutti i maghi nemici, distruggi o blocca tutti i loro generatori (un generatore "
      "circondato da creature o oggetti non può creare niente) e uccidi le creature nemiche che "
      "girano ancora.\n"
      "\n"
      "Quando un mago muore le sue creature si addormentano: chi dorme può essere attaccato e, "
      "turno dopo turno, può sgretolarsi. Il raro potere Dead Revenge invece lascia un generatore "
      "dove il mago è caduto. Con un limite di turni la partita finisce allo scadere e decidono i punteggi. "
      "I punti vengono dalle uccisioni di creature e maghi, dagli incantesimi difficili e persino "
      "dal ripulire i cadaveri. Cinque PowerPoint Archoniani tenuti insieme danno la vittoria "
      "subito." },
    { "Mouse e tasti",
      "- Tasto sinistro: scegliere, lanciare, muovere. Tasto destro: annullare un incantesimo o una "
      "mossa; sulla barra in alto (nella scelta degli incantesimi) apre i menu.\n"
      "- q: informazioni sull'incantesimo o la creatura sotto il puntatore (q di nuovo le chiude).\n"
      "- c: bersagli possibili dell'incantesimo da lanciare. m: mosse possibili (Shift+m: tutta la "
      "portata anche dopo aver mosso). y: dove arriva chi vola.\n"
      "- r: portata dell'arma a distanza. s: caselle che si possono davvero colpire. l: linea di "
      "vista.\n"
      "- f, h, o: cosa una creatura sente al tatto, all'udito e all'olfatto (modalità "
      "Esplorazione).\n"
      "- 1-8: evidenzia le creature di quel giocatore; 9: quelle indipendenti.\n"
      "- Invio: come il tasto sinistro. Frecce: spostano il puntatore di una casella. Backspace: "
      "chiude la mossa di una creatura. Esc: chiude il tuo turno.\n"
      "- Home o Ins: il tasto HELP dell'Amiga (aiuto sul momento).\n"
      "\n"
      "## Tasti di questa versione per PC\n"
      "- Tab (tenuto premuto): avanti veloce, comodo quando giocano solo i maghi del computer.\n"
      "- F12 o Ctrl+Alt: liberano il puntatore del mouse (il tasto centrale esamina, come Q); un clic nella finestra lo "
      "riprende.\n"
      "- Esc in una partita di soli maghi del computer: al cambio di fase successivo il gioco "
      "chiede se iniziare un nuovo gioco (spunta: si torna alle impostazioni; croce: si "
      "continua)." },
    { "I menu",
      "I menu si usano durante la scelta degli incantesimi: tieni premuto il tasto destro sulla "
      "barra in alto.\n"
      "\n"
      "## Progetto\n"
      "- Informazioni, Nuovo gioco.\n"
      "- Apri: carica una partita salvata.\n"
      "- Salva: salva di nuovo sull'ultima partita salvata o aperta, senza chiedere. Se non hai "
      "ancora salvato né aperto niente non fa nulla: usa Salva come.\n"
      "- Salva come: salva con un nome nuovo (in SavedGames).\n"
      "- Salva preferenze, Cambia terreno, Cambia lo sfondo, Mostra le impostazioni, Chiudi.\n"
      "\n"
      "## Controlli\n"
      "Voce, suoni, suoni dei combattimenti, musica, effetti delle evocazioni, punteggi a ogni "
      "turno, stile di evidenziazione, conferma di fine turno, risultati attuali, salvataggio "
      "automatico, combattimento speciale silenzioso, pausa del discorso, mosse e tiri mostrati "
      "in automatico.\n"
      "\n"
      "## Lingua e Aiuto\n"
      "La lingua del gioco (si sceglie anche in questo launcher) e i tutorial." },
    { "Questa versione per PC",
      "## Installazione\n"
      "I file del gioco non sono inclusi. Scarica TotalChaosAGAr6.lha da Aminet (game/strat), "
      "trascinalo su questo launcher o usa Sfoglia, poi premi GIOCA. La prima volta viene "
      "installato nella cartella \"install\" accanto al programma (una ventina di secondi): "
      "l'archivio si estrae, i suoni e le musiche compressi si decomprimono come faceva il gioco "
      "sull'Amiga e si applicano le modifiche di questa versione (Apri, niente limite di turni, "
      "italiano). Il primissimo avvio è un po' più lento perché il gioco si prepara gli elenchi "
      "dei file.\n"
      "\n"
      "## Impostazioni\n"
      "In Impostazioni ci sono la finestra, il modello di schermo, la forma dei pixel e l'audio. "
      "Qui si sceglie anche la lingua del gioco: con l'italiano anche questo launcher passa in "
      "italiano. \"Ripristina tutte le impostazioni\" rimette come all'inizio sia le preferenze del "
      "gioco sia quelle del launcher.\n"
      "\n"
      "## Salvataggi\n"
      "Le partite salvate sono in install/TotalChaos/SavedGames, i record in "
      "install/TotalChaos/HighScores. Reinstallando (per una versione nuova) restano." },
};

int tc_manuale(const char *lingua, const RecompLauncherCManualPage **pagine) {
    if (lingua && !strncmp(lingua, "it", 2)) {
        *pagine = s_it;
        return (int)(sizeof s_it / sizeof s_it[0]);
    }
    *pagine = s_en;
    return (int)(sizeof s_en / sizeof s_en[0]);
}
#endif
