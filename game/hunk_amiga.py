"""hunk_amiga.py - LoadSeg in Python: carica un eseguibile hunk di AmigaDOS
a un indirizzo fisso, con le rilocazioni applicate.

Ogni segmento ha davanti 8 byte come in AmigaDOS: la lunghezza (segmento
piu' intestazione) e il BPTR al segmento successivo; il codice parte subito
dopo. Restituisce l'immagine dei segmenti, gli indirizzi d'inizio, le
lunghezze, i tipi e, per ogni rilocazione, dove sta e dove punta.
"""
import struct
from dataclasses import dataclass, field

HUNK_HEADER, HUNK_CODE, HUNK_DATA, HUNK_BSS = 0x3F3, 0x3E9, 0x3EA, 0x3EB
HUNK_RELOC32, HUNK_SYMBOL, HUNK_DEBUG, HUNK_END = 0x3EC, 0x3F0, 0x3F1, 0x3F2


@dataclass
class Segmenti:
    base: int                       # indirizzo dell'intestazione del primo segmento
    mem: bytearray                  # i segmenti con le intestazioni, da base
    inizio: list                    # indirizzo del primo byte di ogni hunk
    taglie: list                    # lunghezza di ogni hunk (BSS compreso)
    tipi: list                      # "CODE" / "DATA" / "BSS"
    riloc: list = field(default_factory=list)   # (indirizzo della long, destinazione)

    @property
    def fine(self):
        return self.base + len(self.mem)


def carica(dati, base):
    o = 0

    def long():
        nonlocal o
        v = struct.unpack_from(">I", dati, o)[0]
        o += 4
        return v
    if long() != HUNK_HEADER:
        raise ValueError("non e' un eseguibile AmigaDOS (manca HUNK_HEADER)")
    while long():                    # nomi delle librerie residenti: nessuno
        pass
    long()
    primo, ultimo = long(), long()
    taglie = [(long() & 0x3FFFFFFF) * 4 for _ in range(ultimo - primo + 1)]
    inizio = []
    a = base
    for t in taglie:
        inizio.append(a + 8)
        a = (a + 8 + t + 7) & ~7
    mem = bytearray(a - base)
    for i, t in enumerate(taglie):
        p = inizio[i] - 8 - base
        succ = (inizio[i + 1] - 8) >> 2 if i + 1 < len(taglie) else 0
        struct.pack_into(">II", mem, p, t + 8, succ)
    seg = Segmenti(base, mem, inizio, taglie, [])
    h = -1
    while o < len(dati):
        t = long() & 0x3FFFFFFF
        if t in (HUNK_CODE, HUNK_DATA):
            h += 1
            n = long() * 4
            p = inizio[h] - base
            mem[p:p + n] = dati[o:o + n]
            o += n
            seg.tipi.append("CODE" if t == HUNK_CODE else "DATA")
        elif t == HUNK_BSS:
            h += 1
            long()
            seg.tipi.append("BSS")
        elif t == HUNK_RELOC32:
            while True:
                c = long()
                if not c:
                    break
                dest = long()
                for _ in range(c):
                    dove = inizio[h] + long()
                    p = dove - base
                    v = (struct.unpack_from(">I", mem, p)[0] + inizio[dest]) & 0xFFFFFFFF
                    struct.pack_into(">I", mem, p, v)
                    seg.riloc.append((dove, v))
        elif t == HUNK_END:
            pass
        elif t == HUNK_SYMBOL:
            while True:
                c = long()
                if not c:
                    break
                o += 4 * c + 4
        elif t == HUNK_DEBUG:
            n = long()                   # non "o += 4 * long()": o si leggerebbe prima di long()
            o += 4 * n
        else:
            raise ValueError(f"hunk ${t:X} non gestito")
    return seg
