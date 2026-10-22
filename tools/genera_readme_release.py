"""genera_readme_release.py - the README files of the release zip, in English
(README.txt) and Italian (README.it.txt): a short header (requirements,
installation, PC version, files, licenses) followed by the game manual of the
launcher (game/tc_manuale.c), so the zip and the launcher always say the same.

    python tools/genera_readme_release.py [output folder]     (default: release/)
"""
import os
import re
import sys
import textwrap

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LARGHEZZA = 78

TESTA = {
    "en": """TOTAL CHAOS AGA 7.11 - PC VERSION (static recompilation)
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
- assets\\                     fonts and images of the launcher
- licenses\\                   licenses of third-party components

THIRD PARTIES
- SDL2 (zlib license): licenses\\SDL2-zlib.txt
- recomp-ui, the launcher (MIT): licenses\\recomp-ui.txt
- Lola Retro, Commodore 1084S preset for Amiberry (MIT, Zoki, github.com/zoran123456/lola-retro):
  licenses\\Lola-Retro-MIT.txt
- AROS topaz font (AROS Public License).
- Launcher fonts: Lato and Noto Sans Symbols 2 (SIL Open Font License), OpenMoji (CC BY-SA 4.0).

GAME MANUAL
===========
""",
    "it": """TOTAL CHAOS AGA 7.11 - VERSIONE PER PC (ricompilazione statica)
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
- assets\\                     caratteri e immagini del launcher
- licenses\\                   licenze dei componenti di terze parti

TERZE PARTI
- SDL2 (licenza zlib): licenses\\SDL2-zlib.txt
- recomp-ui, il launcher (MIT): licenses\\recomp-ui.txt
- Lola Retro, preset Commodore 1084S per Amiberry (MIT, Zoki, github.com/zoran123456/lola-retro):
  licenses\\Lola-Retro-MIT.txt
- Carattere topaz di AROS (AROS Public License).
- Caratteri del launcher: Lato e Noto Sans Symbols 2 (SIL Open Font License), OpenMoji (CC BY-SA 4.0).

MANUALE DEL GIOCO
=================
""",
}


def stringa_c(s):
    """il contenuto di un letterale C (senza le virgolette)"""
    return s.replace('\\"', '"').replace("\\n", "\n").replace("\\\\", "\\")


def pagine(sorgente, nome):
    i = sorgente.index("RecompLauncherCManualPage %s[] = {" % nome)
    blocco = sorgente[i:sorgente.index("\n};", i)]
    lett = r'"(?:[^"\\]|\\.)*"'
    for m in re.finditer(r"\{\s*(%s)\s*,\s*((?:%s\s*)+)\}" % (lett, lett), blocco):
        titolo = stringa_c(m.group(1)[1:-1])
        testo = "".join(stringa_c(x[1:-1]) for x in re.findall(lett, m.group(2)))
        yield titolo, testo


def impagina(titolo, testo):
    out = [titolo.upper(), "-" * len(titolo)]
    for riga in testo.split("\n"):
        if riga.startswith("## "):
            out += ["", riga[3:]]
        elif riga.startswith("- "):
            out += textwrap.wrap(riga, LARGHEZZA, subsequent_indent="  ") or [""]
        else:
            out += textwrap.wrap(riga, LARGHEZZA) or [""]
    return "\n".join(out).replace("\n\n\n", "\n\n")


def main():
    dest = sys.argv[1] if len(sys.argv) > 1 else os.path.join(REPO, "release")
    os.makedirs(dest, exist_ok=True)
    sorgente = open(os.path.join(REPO, "game", "tc_manuale.c"), encoding="utf-8").read()
    for lingua, nome, file_ in (("en", "s_en", "README.txt"), ("it", "s_it", "README.it.txt")):
        corpo = "\n\n\n".join(impagina(t, x) for t, x in pagine(sorgente, nome))
        testo = TESTA[lingua] + "\n" + corpo + "\n"
        # CRLF: si legge col Blocco note di qualunque Windows
        open(os.path.join(dest, file_), "w", encoding="utf-8-sig", newline="\r\n").write(testo)
        print(os.path.join(dest, file_), len(testo.splitlines()), "righe")


if __name__ == "__main__":
    main()
