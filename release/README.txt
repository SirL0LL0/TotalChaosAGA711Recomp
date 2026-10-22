TOTAL CHAOS AGA 7.11 - PC VERSION (static recompilation)
=========================================================

Total Chaos AGA 7.11 (Amiga, James Conwell and Team Chaos, 2008) recompiled into a native Windows program,
with AmigaOS and the Amiga chips rewritten in C, and a full Italian translation.
PC version: SirL0LL0, with Claude. https://github.com/SirL0LL0/TotalChaosAGA711Recomp
Italian version of this file: README.it.txt

REQUIREMENTS
- 64-bit Windows 10 or 11, a graphics card with OpenGL (GLSL 3.30 for the Lola Retro screen model).
- The original game package from Aminet (free): TotalChaosAGAr6.lha
  https://aminet.net/package/game/strat/TotalChaosAGAr6
  This zip does NOT contain any game file.

INSTALLATION
1. Extract this folder anywhere.
2. Start TotalChaosAGA711Recomp.exe.
3. In the launcher, drag TotalChaosAGAr6.lha onto the window or use Browse, then press PLAY: the game is
   installed in the "install" folder next to the program.

LAUNCHER OPTIONS
- Language of the game and of the launcher (English or Italian, and the other languages of the game).
- Screen model: Sharp, Commodore 1084S, CRT Easymode, Scanlines, Lola Retro 1084S (the whole analog chain).
- Scaling filter for the Sharp model: sharp pixels, sharp bilinear, bilinear, bicubic, Lanczos, Scale2x.
- Pixel shape: PAL or NTSC 4:3, square pixels, 16:9, 16:10, fill the window; window scale; integer scaling.
- Audio volume.

FILES
- TotalChaosAGA711Recomp.exe  the game and its launcher
- SDL2.dll                    window, audio and input
- tc_modifiche.bin            Italian translation and fixes, applied when installing
- assets\                     fonts and images of the launcher
- licenses\                   licenses of third-party components

THIRD PARTIES
- SDL2 (zlib license): licenses\SDL2-zlib.txt
- recomp-ui, the launcher (MIT): licenses\recomp-ui.txt
- Lola Retro, Commodore 1084S preset for Amiberry (MIT, Zoki, github.com/zoran123456/lola-retro):
  licenses\Lola-Retro-MIT.txt
- AROS topaz font (AROS Public License).
- Launcher fonts: Lato and Noto Sans Symbols 2 (SIL Open Font License), OpenMoji (CC BY-SA 4.0).

GAME MANUAL
===========

WELCOME
-------
Total Chaos AGA is a strategy game for up to eight wizards, human or computer
controlled. Each wizard starts with a handful of random spells: creatures,
illusions, curses, natural disasters and powers. The aim is to be the last
wizard alive.

The battle takes place on a board of 17 by 14 cells. A game is a series of
rounds; in every round each wizard chooses a spell, casts it, and then moves
the creatures it controls.

Total Chaos AGA 7 is the work of James Conwell and Team Chaos (2008), freeware
on Aminet. This PC version runs the original Amiga program, recompiled to
native code, with the Italian translation completed.


STARTING A GAME
---------------

Settings I: the wizards
Choose which of the eight wizards play (Active), which are controlled by you
(Human) and the team of each one: wizards with the same team icon start as
allies. Click a name to change it. Start takes you to the next screen.

Settings II: the world
Number of generators (default 4), spells per wizard (default 20), level of the
computer wizards (0-9: it only affects the spells they receive), artifacts,
scrolls, corpses and PowerPoints on the board, and the number of turns
(forever, or up to 999). Options such as Exploration, Impurities, Agent
casting, Hidden artifacts and scrolls, Texas Trash'em and the computer's
Aggressiveness and Ruthlessness are switched on here.

Settings III: the rules
Ground and air wars, dungeon wars, weak or strong wizards, power-ups and
capture options. Newbie mode (on at first) limits the spells to an easier set:
switch it off for the full repertoire. Start begins the game.


THE ROUND
---------

Spell selection
The right of the screen shows your spells. Click one to choose it, or End Turn
(or Esc) to cast nothing. Point at a spell and press q to read about it, or c
to see every cell where it could be cast. A chosen spell is used up even if
you abort it.

Spell casting
The bottom line shows the spell, how many times it is cast and its range.
Click a target cell; range 0 spells just need a click. Most spells need line
of sight from your wizard. The right button or Backspace aborts. After each
wizard, generators may create creatures in empty adjacent cells.

Board update
The computer grows fire, floods, fungi and the other growths, moves tempests
and vortices, may collapse castles and magic woods (a wizard inside gets a
bonus spell) and wakes or destroys sleepers.

Movement
Click a creature, then the cells to move to: walkers go one cell at a time,
fliers jump straight to the target. Moving onto an enemy attacks it. Creatures
marked with a flashing E are engaged: they cannot move but can fight.
Backspace ends a creature's move, End Turn or Esc ends your turn. Wizards can
mount creatures, trees and castles by moving onto them; the D gadget
dismounts.


WINNING
-------
Kill every enemy wizard, kill or block every enemy generator (a generator
surrounded by creatures or objects cannot create anything) and kill the enemy
creatures still roaming.

When a wizard dies its creatures fall asleep: sleepers can be attacked and,
turn after turn, may crumble away. The rare Dead Revenge power instead leaves
a generator where the wizard fell. With a turn limit the game ends when the
turns run out and the scores decide. Points come from killing creatures and
wizards, from difficult spells and even from clearing corpses. Five Archonian
PowerPoints held at once win immediately.


MOUSE AND KEYS
--------------
- Left button: select, cast, move. Right button: abort a spell or a move; on
  the menu bar (during spell selection) it opens the menus.
- q: information about the spell or creature under the pointer (q again closes
  it).
- c: legal targets of the spell being cast. m: legal moves (Shift+m: full
  range even after moving). y: fliers' reach.
- r: range of the ranged weapon. s: cells that can really be shot. l: line of
  sight.
- f, h, o: what a creature can feel, hear and smell (Exploration mode).
- 1-8: highlight the creatures of that player; 9: the independent ones.
- Return: like the left button. Cursor keys: move the pointer one cell.
  Backspace: end a creature's move. Esc: end your turn.
- Home or Insert: the Amiga HELP key (context help).

Keys of this PC version
- Tab (hold): fast forward, useful when only computer wizards are playing.
- F12, or a left click while holding the middle button: release the mouse
  pointer (the middle button alone examines, like Q); click in the window to
  take it back.
- Esc during a game of computer wizards only: at the next change of phase the
  game asks whether to start a new game (tick: back to the settings; cross:
  carry on).


MENUS
-----
The menus are used during spell selection: keep the right button pressed on
the bar at the top of the screen.

Project
- About, New Game.
- Load: opens a saved game.
- Save: saves again over the last game you saved or loaded, without asking. If
  you have not saved or loaded anything yet it does nothing: use Save As.
- Save As: saves with a new name (in SavedGames).
- Save Preferences, Change Terrain, Change Backdrop, Show Game Settings, Quit.

Controls
Speech, sounds, combat sounds, music, summoning effects, scores every turn,
highlight style, end of turn confirmation, current scores, autosave, silent
special combat, talk delay, automatic display of moves and shots.

Language and Help
The game's language (also chosen in this launcher) and the tutorials.


THIS PC VERSION
---------------

Installation
The game files are not included. Download TotalChaosAGAr6.lha from Aminet
(game/strat), drag it onto this launcher or use Browse, then press PLAY. The
first time it is installed in the "install" folder next to the program (about
20 seconds): the archive is extracted, the compressed sounds and music are
unpacked as the game did on the Amiga, and the changes of this port (Load, no
turn limit, Italian) are applied. The very first start is a little slower
because the game builds its file lists.

Settings
Settings holds the window, screen model, aspect ratio and audio. The game
language is set here too: picking Italiano also switches this launcher to
Italian. "Reset all settings" puts back both the game preferences and the
launcher's.

Saves
Saved games are in install/TotalChaos/SavedGames, high scores in
install/TotalChaos/HighScores. Reinstalling (for a new version) keeps them.
