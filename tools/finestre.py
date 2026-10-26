"""finestre.py - schermi e finestre di Intuition in un'immagine della memoria
(memoria_fine.bin di AMIGA_QUADRI con AMIGA_MEMORIA_FINE=1, errore_memoria.bin): per ogni finestra
indicata, il suo schermo e la lista delle finestre di quello schermo (dalla
piu' davanti), con posizione, flag, RastPort, layer e bitmap.

Uso: python tools/finestre.py build/cmake/memoria_fine.bin <finestra hex> [...]
"""
import struct
import sys

m = open(sys.argv[1], "rb").read()
L = lambda a: struct.unpack_from(">I", m, a)[0]
S = lambda a: struct.unpack_from(">h", m, a)[0]
visti = set()
for w0 in [int(x, 16) for x in sys.argv[2:]]:
    sc = L(w0 + 46)
    if sc in visti:
        continue
    visti.add(sc)
    print(f"schermo ${sc:06X}: {S(sc + 12)}x{S(sc + 14)}, bitmap della RastPort ${L(sc + 84 + 4):06X}")
    w = L(sc + 4)
    while w:
        rp, ly = L(w + 50), L(w + 124)
        print(f"  finestra ${w:06X} {S(w + 4)},{S(w + 6)} {S(w + 8)}x{S(w + 10)} flag ${L(w + 24):08X} "
              f"rp ${rp:06X} (layer ${L(rp):06X} bitmap ${L(rp + 4):06X}) layer ${ly:06X} -> finestra ${L(ly + 40):06X}")
        w = L(w)
