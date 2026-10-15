"""riproduci.py - una partita giocata a mano con --debug, rifatta senza finestra.

Il registro Chaos_debug.log ha gli ingressi ("[ingresso] q<quadro> mouse <n>
giu/su a x,y del video", "... tasto <nome> giu/su (raw $xx)"). Qui diventano
la lista di AMIGA_INGRESSI: il mouse va dove era a ogni clic, i tasti del
mouse restano premuti quanti quadri li ha tenuti il giocatore. Con la stessa
ora di partenza (AMIGA_ORA, usata anche nella partita vera) il gioco rifa'
esattamente la stessa partita, e la si puo' fermare a un quadro qualsiasi.

Uso: python tools/riproduci.py build/cmake/Chaos_debug.log build/cmake/ingressi.txt
     poi AMIGA_INGRESSI=@build/cmake/ingressi.txt (tools/partita.ps1 -ingressi @...)
"""
import re
import sys

testo = open(sys.argv[1], encoding="latin-1").read()
uscita = []
giu = {}                                            # tasto del mouse -> (quadro, x, y)
for m in re.finditer(r"\[ingresso\] q(\d+) (mouse (\d+) (giu|su) a (-?\d+),(-?\d+)|tasto .*? (giu|su) \(raw \$([0-9A-F]{2})\))", testo):
    q = int(m.group(1))
    if m.group(3):
        tasto, verso, x, y = int(m.group(3)), m.group(4), int(m.group(5)), int(m.group(6))
        if verso == "giu":
            giu[tasto] = (q, x, y)
        elif tasto in giu:
            q0, x0, y0 = giu.pop(tasto)
            lettera = {1: "l", 3: "r"}.get(tasto)
            if lettera:
                uscita.append((q0, f"p{x0}.{y0}:{q0}"))
                uscita.append((q0, f"{lettera}{max(1, q - q0)}:{q0}"))
                if (x, y) != (x0, y0):               # trascinato: i menu scelgono al rilascio
                    uscita.append((q - 1, f"p{x}.{y}:{q - 1}"))
    elif m.group(7) == "giu" and m.group(8) != "FF":
        uscita.append((q, f"k{m.group(8)}:{q}"))
# i movimenti del mouse (registrati da --debug a ogni quadro in cui cambia)
for m in re.finditer(r"\[ingresso\] q(\d+) mouse a (-?\d+),(-?\d+)", testo):
    q = int(m.group(1))
    uscita.append((q, f"p{m.group(2)}.{m.group(3)}:{q}"))
uscita = [v for _, v in sorted(uscita, key=lambda t: t[0])]
open(sys.argv[2], "w").write("\n".join(uscita) + "\n")
print(f"{len(uscita)} ingressi in {sys.argv[2]}")
