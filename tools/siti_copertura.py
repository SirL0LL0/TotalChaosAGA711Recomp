"""siti_copertura.py - tutti i punti di chiamata del C generato, per le
percentuali di copertura della finestra di debug.

I punti sono i recomp_push_return(0x...u) con indirizzo costante: la stessa
bitmap di AMIGA_COPERTURA (un bit per indirizzo pari sotto $400000),
preceduta da 4 byte con l'inizio del programma (sotto: le librerie).

    python tools/siti_copertura.py <cartella generated> <uscita.bin> [inizio programma]
"""
import os
import re
import sys

gen, uscita = sys.argv[1], sys.argv[2]
inizio = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0x23A000
b = bytearray(0x400000 // 16)
n = 0
for f in os.listdir(gen):
    if f.endswith(".c"):
        for m in re.findall(r"recomp_push_return\(0x([0-9A-Fa-f]+)u\)", open(os.path.join(gen, f)).read()):
            a = int(m, 16)
            if a < 0x400000:
                k = a >> 1
                n += not b[k >> 3] >> (k & 7) & 1
                b[k >> 3] |= 1 << (k & 7)
with open(uscita, "wb") as o:
    o.write(inizio.to_bytes(4, "big"))
    o.write(b)
print("punti di chiamata: %d" % n)
