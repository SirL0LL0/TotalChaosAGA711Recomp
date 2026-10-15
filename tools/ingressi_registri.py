"""ingressi_registri.py - le funzioni di libreria SAS/C con due ingressi.

Certe routine della libreria (stcl_d, stcl_o, ...) cominciano leggendo gli
argomenti dalla pila (move.l n(a7),Dn / movea.l n(a7),An) e subito dopo hanno
l'ingresso "da registri" (@stcl_d), dove saltano le chiamate con gli
argomenti gia' nei registri. Se il ricompilatore conosce solo uno dei due,
l'altro arriva come salto a codice non tradotto. Qui si stampano entrambi
gli ingressi di ogni routine cosi', da mettere in game/funzioni_scoperte.toml.

Uso: python tools/ingressi_registri.py build/chaos_immagine.bin build/c2/generated/chaos_dispatch.c
"""
import re
import struct
import sys

imm = open(sys.argv[1], "rb").read()
noti = set()
if len(sys.argv) > 2:
    noti = {int(x, 16) for x in re.findall(r"\{ 0x([0-9A-F]+)u, func_", open(sys.argv[2]).read())}


def legge_pila(p):
    """move.l d16(a7),Dn (0x202F|n<<9) o movea.l d16(a7),An (0x206F|n<<9)"""
    w = struct.unpack_from(">H", imm, p)[0]
    return (w & 0xF1FF) in (0x202F, 0x206F)


for p in range(0x200000, len(imm) - 16, 2):
    if not legge_pila(p):
        continue
    # una funzione: comincia dopo un RTS o un NOP di allineamento
    if imm[p - 2:p] not in (b"\x4e\x75", b"\x4e\x71", b"\x00\x00"):
        continue
    q = p
    while q < p + 16 and legge_pila(q):
        q += 4
    if q == p + 4:
        continue                      # un solo argomento: niente da dire
    # gli argomenti che restano in pila scendono al posto di quelli letti
    # (move.l d16(a7),d16(a7)): fanno parte dell'ingresso da pila
    while struct.unpack_from(">H", imm, q)[0] == 0x2F6F:
        q += 6
    w = struct.unpack_from(">H", imm, q)[0]
    if w in (0x4E75, 0x4E71):
        continue
    mancano = [a for a in (p, q) if a not in noti]
    if mancano:
        print(f"  0x{p:06X}, 0x{q:06X},   # pila / registri" + ("" if not noti else
              "   mancano: " + " ".join(f"${a:06X}" for a in mancano)))
