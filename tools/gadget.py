"""gadget.py - i gadget di una finestra in un'immagine della memoria.

(memoria_fine.bin: AMIGA_QUADRI con AMIGA_MEMORIA_FINE=1)
Uso: python tools/gadget.py build/cmake/memoria_fine.bin <finestra hex>
Gadget: Next 0, Left 4, Top 6, Width 8, Height 10, Flags 12, Activation 14,
GadgetType 16, GadgetRender 18, SelectRender 22, GadgetText 26, SpecialInfo 34, GadgetID 38
"""
import struct
import sys

m = open(sys.argv[1], "rb").read()
w = int(sys.argv[2], 16)
L = lambda a: struct.unpack_from(">I", m, a)[0]
H = lambda a: struct.unpack_from(">H", m, a)[0]
S = lambda a: struct.unpack_from(">h", m, a)[0]
print(f"finestra ${w:06X}: {S(w + 4)},{S(w + 6)} {S(w + 8)}x{S(w + 10)}")
g = L(w + 62)
n = 0
while g and n < 64:
    fl, act, tipo = H(g + 12), H(g + 14), H(g + 16)
    rel = "".join(c for c, b in (("B", 8), ("R", 0x10), ("W", 0x20), ("H", 0x40)) if fl & b)
    testo = ""
    it = L(g + 26)
    if it and L(it + 12):
        s = L(it + 12)
        testo = m[s:m.index(0, s)][:20].decode("latin-1")
    print(f"  ${g:06X} id {H(g + 38):3d} {S(g + 4):4d},{S(g + 6):4d} {S(g + 8):4d}x{S(g + 10):<4d} flag ${fl:04X} rel {rel:4s} "
          f"att ${act:04X} tipo ${tipo:04X} render ${L(g + 18):06X} '{testo}'")
    g = L(g)
    n += 1
