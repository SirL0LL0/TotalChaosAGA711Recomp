"""carica_hunk.py - carica e riloca un eseguibile Amiga (hunk) a un indirizzo
e scrive la memoria piatta, per tools/dis68k.c.

Uso: python tools/carica_hunk.py <file> <base hex> <uscita.bin>
Stampa i segmenti (inizio, taglia, tipo).
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "game"))
from hunk_amiga import carica  # noqa: E402

file, base, uscita = sys.argv[1], int(sys.argv[2], 16), sys.argv[3]
seg = carica(open(file, "rb").read(), base)
with open(uscita, "wb") as f:
    f.write(bytes(seg.mem))
for a, t, k in zip(seg.inizio, seg.taglie, seg.tipi):
    print(f"${a:06X} {t:6d} {k}")
print(f"base ${seg.base:06X} fine ${seg.fine:06X}")
