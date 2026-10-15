"""estrai_hdf.py - estrae i file di un hardfile Amiga (RDB, partizioni OFS/FFS).

Uso: python tools/estrai_hdf.py <file.hdf> <cartella> [--partizione DH0]

Legge il Rigid Disk Block, poi per ogni partizione (o solo quella indicata)
percorre il filesystem dalla rootblock: directory (tabella hash e catene),
file (blocchi di dati dalle tabelle e dalle estensioni), date, protezioni e
commenti. Le date diventano quelle dei file; protezioni e commenti finiscono
in <cartella>/<partizione>.elenco.txt. Supporta DOS\\0-\\5 (OFS, FFS, INTL,
DIRCACHE: i blocchi della cache si ignorano). I link vengono solo elencati.
"""
import argparse
import datetime
import os
import struct
import sys

T_HEADER, T_DATA, T_LIST = 2, 8, 16
ST_ROOT, ST_USERDIR, ST_SOFTLINK, ST_LINKDIR = 1, 2, 3, 4
ST_FILE, ST_LINKFILE = -3, -4
EPOCA = datetime.datetime(1978, 1, 1)


class Partizione:
    def __init__(self, f, inizio, blocchi, bs, dostype):
        self.f, self.inizio, self.blocchi, self.bs, self.dostype = f, inizio, blocchi, bs, dostype
        self.ffs = dostype & 1
        self.n_hash = bs // 4 - 56

    def blocco(self, n):
        if not 0 <= n < self.blocchi:
            raise ValueError(f"blocco {n} fuori dalla partizione")
        self.f.seek((self.inizio + n) * self.bs)
        return self.f.read(self.bs)

    def L(self, b, o):
        return struct.unpack_from(">I", b, o)[0]

    def l(self, b, o):
        return struct.unpack_from(">i", b, o)[0]

    def nome(self, b):
        o = self.bs - 80
        return b[o + 1:o + 1 + min(b[o], 30)].decode("latin-1")

    def data(self, b, o):
        g, m, t = struct.unpack_from(">III", b, o)
        return EPOCA + datetime.timedelta(days=g, minutes=m, seconds=t / 50)

    def radice(self):
        return (2 + self.blocchi - 1) // 2

    def voci(self, b):
        for i in range(self.n_hash):
            n = self.L(b, 24 + 4 * i)
            visti = 0
            while n:
                h = self.blocco(n)
                yield n, h
                n = self.L(h, self.bs - 16)                  # hash_chain
                visti += 1
                if visti > 100000:
                    raise ValueError("catena hash infinita")

    def dati_file(self, h):
        dim = self.L(h, self.bs - 188)
        out = bytearray()
        blocco = h
        while True:
            alto = self.L(blocco, 8)                         # high_seq: quante voci usate
            for i in range(alto):
                n = self.L(blocco, self.bs - 204 - 4 * i)   # la tabella va all'indietro
                d = self.blocco(n)
                if self.ffs:
                    out += d
                else:                                        # OFS: 24 byte d'intestazione
                    out += d[24:24 + self.L(d, 12)]
            est = self.L(blocco, self.bs - 8)
            if not est:
                break
            blocco = self.blocco(est)
        return bytes(out[:dim])


def leggi_rdb(f):
    for i in range(16):
        f.seek(i * 512)
        b = f.read(512)
        if b[:4] == b"RDSK":
            break
    else:
        raise ValueError("RDSK non trovato: hardfile senza RDB")
    L = lambda b, o: struct.unpack_from(">I", b, o)[0]
    bs = L(b, 16)
    parti = []
    p = L(b, 28)
    while p != 0xFFFFFFFF:
        f.seek(p * bs)
        pb = f.read(bs)
        nome = pb[37:37 + pb[36]].decode("latin-1")
        de = pb[128:]
        teste, settori, basso, alto, dt = L(de, 12), L(de, 20), L(de, 36), L(de, 40), L(de, 64)
        dbs = L(de, 4) * 4
        per_cil = teste * settori
        parti.append((nome, basso * per_cil * dbs // bs, (alto - basso + 1) * per_cil, dbs, dt))
        p = L(pb, 16)
    return parti


def estrai(part, cartella, elenco):
    def giu(n, b, percorso):
        for num, h in part.voci(b):
            if part.l(h, 0) != T_HEADER:
                print(f"  blocco {num}: non e' un'intestazione, saltato", file=sys.stderr)
                continue
            tipo = part.l(h, part.bs - 4)
            nome = part.nome(h)
            dest = os.path.join(percorso, nome)
            rel = os.path.relpath(dest, cartella)
            prot = part.L(h, part.bs - 192)
            o = part.bs - 184
            commento = h[o + 1:o + 1 + min(h[o], 79)].decode("latin-1")
            quando = part.data(h, part.bs - 92)
            if tipo == ST_USERDIR:
                os.makedirs(dest, exist_ok=True)
                elenco.append(f"{rel}/\t{prot:08X}\t{quando:%Y-%m-%d %H:%M:%S}\t{commento}")
                giu(num, h, dest)
            elif tipo == ST_FILE:
                with open(dest, "wb") as u:
                    u.write(part.dati_file(h))
                t = quando.timestamp()
                os.utime(dest, (t, t))
                elenco.append(f"{rel}\t{prot:08X}\t{quando:%Y-%m-%d %H:%M:%S}\t{commento}")
            elif tipo == ST_SOFTLINK:
                elenco.append(f"{rel}\tSOFTLINK -> {h[24:part.bs - 200].split(b'\\0')[0].decode('latin-1')}")
            else:
                elenco.append(f"{rel}\tLINK (tipo {tipo}) -> blocco {part.L(h, part.bs - 44)}")
    r = part.blocco(part.radice())
    if part.l(r, part.bs - 4) != ST_ROOT:
        raise ValueError("rootblock non trovata")
    print(f"  volume \"{part.nome(r)}\"")
    os.makedirs(cartella, exist_ok=True)
    giu(part.radice(), r, cartella)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("hdf")
    ap.add_argument("cartella")
    ap.add_argument("--partizione")
    a = ap.parse_args()
    with open(a.hdf, "rb") as f:
        for nome, inizio, blocchi, bs, dt in leggi_rdb(f):
            tipo = struct.pack(">I", dt)
            print(f"{nome}: blocco {inizio}, {blocchi} blocchi da {bs}, {tipo[:3].decode()}\\{tipo[3]}")
            if a.partizione and a.partizione != nome:
                continue
            if tipo[:3] != b"DOS" or tipo[3] > 5:
                print("  filesystem non supportato, saltata")
                continue
            dest = os.path.join(a.cartella, nome)
            elenco = []
            estrai(Partizione(f, inizio, blocchi, bs, dt), dest, elenco)
            with open(os.path.join(a.cartella, nome + ".elenco.txt"), "w", encoding="utf-8") as e:
                e.write("# percorso\tprotezioni\tdata\tcommento\n" + "\n".join(sorted(elenco)) + "\n")
            print(f"  {sum(1 for x in elenco if not x.split(chr(9))[0].endswith('/'))} voci estratte in {dest}")


if __name__ == "__main__":
    main()
