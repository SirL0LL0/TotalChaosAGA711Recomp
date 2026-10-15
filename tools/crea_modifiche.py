"""crea_modifiche.py - tc_modifiche.bin: le nostre modifiche al pacchetto di Aminet.

Confronta il gioco installato dall'originale (TotalChaosAGAr6.lha, con
"--installa-originale") con la versione nostra (dati_gioco/DH0/TotalChaos) e
scrive, per i file nostri, le differenze nel formato di game/tc_installa.c:
per un file che c'e' nel pacchetto solo "copia da qui" e "aggiungi questi
byte" (il file originale non entra mai), per un file nuovo il file intero.

Cosa e' nostro (il resto lo rigenera il gioco: elenchi *List.txt,
TalkListsDir, HighScores, gfx/dat, preferenze, salvataggi):
  Chaos                          le patch (Load, turni) + italiano
  Catalogs/                      testi del gioco
  Help/Italiano/, SpellDescriptions/Italiano/, Tutorials/Italiano/,
  SpellDescriptions/ItalianoSpellnames.txt
e VirginityHandler va tolto: sull'Amiga lo cancellava il gioco dopo aver
decompresso tutto al primo avvio (qui lo fa l'installazione).

    python tools/crea_modifiche.py <installato dall'originale> [<nostro>] [-o build/tc_modifiche.bin]
"""
import argparse
import os
import struct
import zlib

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NOSTRI = ("chaos", "catalogs/", "help/italiano/", "spelldescriptions/italiano/", "tutorials/italiano/",
          "spelldescriptions/italianospellnames.txt")
DA_TOGLIERE = ["VirginityHandler"]
MIN = 8                       # copia solo da 8 byte in su


def elenco(r):
    out = {}
    for d, _, fs in os.walk(r):
        for f in fs:
            p = os.path.join(d, f)
            rel = os.path.relpath(p, r).replace("\\", "/")
            out[rel.lower()] = (rel, p)
    return out


def differenze(sorgente, dest):
    """operazioni (1, da, quanti) / (2, byte) che fanno dest da sorgente"""
    indice = {}
    for i in range(0, len(sorgente) - MIN + 1):
        indice.setdefault(sorgente[i:i + MIN], i)
    ops, lett, i = [], bytearray(), 0
    while i < len(dest):
        j = indice.get(dest[i:i + MIN]) if i + MIN <= len(dest) else None
        if j is None:
            lett.append(dest[i])
            i += 1
            continue
        k = MIN
        while i + k < len(dest) and j + k < len(sorgente) and dest[i + k] == sorgente[j + k]:
            k += 1
        if lett:
            ops.append((2, bytes(lett)))
            lett = bytearray()
        ops.append((1, j, k))
        i += k
    if lett:
        ops.append((2, bytes(lett)))
    return ops


def percorso(rel):
    b = rel.encode("latin-1")
    return struct.pack(">H", len(b)) + b


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("originale")
    ap.add_argument("nostro", nargs="?", default=os.path.join(REPO, "dati_gioco", "DH0", "TotalChaos"))
    ap.add_argument("-o", default=os.path.join(REPO, "build", "tc_modifiche.bin"))
    arg = ap.parse_args()
    a = elenco(os.path.join(arg.originale, "TotalChaos"))
    b = elenco(arg.nostro)
    out = bytearray(b"TCMOD1\n")
    conta = {"N": 0, "D": 0, "X": 0, "uguali": 0}
    aggiunti = copiati = nuovi = 0
    for k in sorted(b):
        if not (k == "chaos" or k.startswith(NOSTRI[1:])):
            continue
        rel, pb = b[k]
        dest = open(pb, "rb").read()
        if k in a:
            src = open(a[k][1], "rb").read()
            if src == dest:
                conta["uguali"] += 1
                continue
            ops = differenze(src, dest)
            out += b"D" + percorso(a[k][0]) + struct.pack(">III", zlib.crc32(src), len(dest), zlib.crc32(dest))
            for op in ops:
                if op[0] == 1:
                    out += struct.pack(">BII", 1, op[1], op[2])
                    copiati += op[2]
                else:
                    out += struct.pack(">BI", 2, len(op[1])) + op[1]
                    aggiunti += len(op[1])
            out += b"\0"
            conta["D"] += 1
        else:
            out += b"N" + percorso(rel) + struct.pack(">I", len(dest)) + dest
            nuovi += len(dest)
            conta["N"] += 1
    for rel in DA_TOGLIERE:
        if rel.lower() in a and rel.lower() not in b:
            out += b"X" + percorso(a[rel.lower()][0])
            conta["X"] += 1
    out += b"F"
    with open(arg.o, "wb") as f:
        f.write(out)
    print("%s: %d byte; file modificati %d (copiati dall'originale %d byte, aggiunti %d), nuovi %d (%d byte), "
          "tolti %d, gia' uguali %d" % (arg.o, len(out), conta["D"], copiati, aggiunti, conta["N"], nuovi,
                                         conta["X"], conta["uguali"]))


if __name__ == "__main__":
    main()
