"""sostituisci_in_lha.py - rimpiazza un file dentro un archivio lha (i
tutorial: ChaosSavedGame compresso + TotalChaosTutorial, lo script). Le altre
voci si copiano cosi' come sono (anche -lh5-), quella nuova va -lh0- con
intestazione di livello 0, come le scrive amiga_lha.c.

    python tools/sostituisci_in_lha.py <archivio.lha> <nome nell'archivio> <file nuovo>
"""
import struct
import sys


def crc16(d):
    c = 0
    for b in d:
        c ^= b
        for _ in range(8):
            c = (c >> 1) ^ 0xA001 if c & 1 else c >> 1
    return c


def voci(a):
    """(nome, byte della voce intera) per le intestazioni di livello 0 e 1"""
    p = 0
    while p < len(a) and a[p]:
        lh = a[p] + 2
        livello = a[p + 20]
        if livello not in (0, 1):
            raise SystemExit("intestazione di livello %d non gestita" % livello)
        compr = struct.unpack_from("<I", a, p + 7)[0]
        nome = a[p + 22:p + 22 + a[p + 21]].decode("latin-1")
        yield nome, a[p:p + lh + compr]       # nel livello 1 compr comprende le estese
        p += lh + compr


def voce_lh0(nome, dati, ora):
    n = nome.encode("latin-1")
    h = bytearray(24 + len(n))
    h[0] = 22 + len(n)
    h[2:7] = b"-lh0-"
    struct.pack_into("<II", h, 7, len(dati), len(dati))
    h[15:19] = ora
    h[19] = 0x20
    h[21] = len(n)
    h[22:22 + len(n)] = n
    struct.pack_into("<H", h, 22 + len(n), crc16(dati))
    h[1] = sum(h[2:h[0] + 2]) & 0xFF
    return bytes(h) + dati


def main():
    arch, nome, nuovo = sys.argv[1:4]
    a = open(arch, "rb").read()
    dati = open(nuovo, "rb").read()
    out, trovato = bytearray(), False
    for n, v in voci(a):
        if n.split("\0")[0] == nome:          # dopo lo zero il commento Amiga: resta
            out += voce_lh0(n, dati, v[15:19])
            trovato = True
        else:
            out += v
    if not trovato:
        raise SystemExit("%s non c'e' in %s" % (nome, arch))
    open(arch, "wb").write(bytes(out) + b"\0")
    print("%s: %s sostituito (%d byte)" % (arch, nome, len(dati)))


if __name__ == "__main__":
    main()
