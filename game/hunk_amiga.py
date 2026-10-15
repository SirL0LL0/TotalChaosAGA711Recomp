"""hunk_amiga.py - LoadSeg in Python: carica un eseguibile hunk di AmigaDOS
a un indirizzo fisso, con le rilocazioni applicate.

Ogni segmento ha davanti 8 byte come in AmigaDOS: la lunghezza (segmento
piu' intestazione) e il BPTR al segmento successivo; il codice parte subito
dopo. Restituisce l'immagine dei segmenti, gli indirizzi d'inizio, le
lunghezze, i tipi e, per ogni rilocazione, dove sta e dove punta.

Con base_chip i hunk che chiedono memoria chip (bit 30 della taglia) vanno
li', uno dopo l'altro, come farebbe LoadSeg con AllocMem(MEMF_CHIP); gli
altri restano di seguito a base.
"""
import struct
from dataclasses import dataclass, field

HUNK_HEADER, HUNK_CODE, HUNK_DATA, HUNK_BSS = 0x3F3, 0x3E9, 0x3EA, 0x3EB
HUNK_RELOC32, HUNK_SYMBOL, HUNK_DEBUG, HUNK_END = 0x3EC, 0x3F0, 0x3F1, 0x3F2


@dataclass
class Segmenti:
    base: int                       # indirizzo dell'intestazione del primo segmento
    mem: bytearray                  # i segmenti non chip con le intestazioni, da base
    inizio: list                    # indirizzo del primo byte di ogni hunk
    taglie: list                    # lunghezza di ogni hunk (BSS compreso)
    tipi: list                      # "CODE" / "DATA" / "BSS"
    riloc: list = field(default_factory=list)   # (indirizzo della long, destinazione)
    chip: list = field(default_factory=list)    # True per i hunk in memoria chip
    base_chip: int = 0              # dove stanno i hunk chip (se ce ne sono)
    mem_chip: bytearray = field(default_factory=bytearray)

    @property
    def fine(self):
        return self.base + len(self.mem)

    @property
    def fine_chip(self):
        return self.base_chip + len(self.mem_chip)


def carica(dati, base, base_chip=None):
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
    grezze = [long() for _ in range(ultimo - primo + 1)]
    taglie = [(g & 0x3FFFFFFF) * 4 for g in grezze]
    chip = [base_chip is not None and (g >> 30) == 1 for g in grezze]
    inizio = []
    a, c = base, base_chip or 0
    for t, ch in zip(taglie, chip):
        if ch:
            inizio.append(c + 8)
            c = (c + 8 + t + 7) & ~7
        else:
            inizio.append(a + 8)
            a = (a + 8 + t + 7) & ~7
    seg = Segmenti(base, bytearray(a - base), inizio, taglie, [], chip=chip,
                   base_chip=base_chip or 0, mem_chip=bytearray(c - (base_chip or 0)))

    def zona(ind):                   # (bytearray, offset) di un indirizzo
        if base_chip is not None and seg.base_chip <= ind < seg.fine_chip:
            return seg.mem_chip, ind - seg.base_chip
        return seg.mem, ind - base
    for i, t in enumerate(taglie):
        m, p = zona(inizio[i] - 8)
        succ = (inizio[i + 1] - 8) >> 2 if i + 1 < len(taglie) else 0
        struct.pack_into(">II", m, p, t + 8, succ)
    h = -1
    while o < len(dati):
        t = long() & 0x3FFFFFFF
        if t in (HUNK_CODE, HUNK_DATA):
            h += 1
            n = long() * 4
            m, p = zona(inizio[h])
            m[p:p + n] = dati[o:o + n]
            o += n
            seg.tipi.append("CODE" if t == HUNK_CODE else "DATA")
        elif t == HUNK_BSS:
            h += 1
            long()
            seg.tipi.append("BSS")
        elif t == HUNK_RELOC32:
            while True:
                n = long()
                if not n:
                    break
                dest = long()
                for _ in range(n):
                    dove = inizio[h] + long()
                    m, p = zona(dove)
                    v = (struct.unpack_from(">I", m, p)[0] + inizio[dest]) & 0xFFFFFFFF
                    struct.pack_into(">I", m, p, v)
                    seg.riloc.append((dove, v))
        elif t == HUNK_END:
            pass
        elif t == HUNK_SYMBOL:
            while True:
                n = long()
                if not n:
                    break
                o += 4 * n + 4
        elif t == HUNK_DEBUG:
            n = long()                   # non "o += 4 * long()": o si leggerebbe prima di long()
            o += 4 * n
        else:
            raise ValueError(f"hunk ${t:X} non gestito")
    return seg
