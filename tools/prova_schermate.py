"""prova_schermate.py - tutte le immagini IFF ILBM del gioco (schermate,
sfondi, pannelli): intestazione, palette e corpo ByteRun1 decompresso fino
in fondo, con il numero di byte giusto per larghezza x altezza x piani.

    python tools/prova_schermate.py
"""
import os
import struct
import sys

BASE = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "dati_gioco", "DH0", "TotalChaos")


def prova(p):
    d = open(p, "rb").read()
    if d[:4] != b"FORM" or d[8:12] != b"ILBM":
        return None
    i, bmhd, body, cmap, camg = 12, None, None, None, 0
    while i + 8 <= len(d):
        k, l = d[i:i + 4], struct.unpack(">I", d[i + 4:i + 8])[0]
        c = d[i + 8:i + 8 + l]
        if k == b"BMHD": bmhd = c
        elif k == b"BODY": body = c
        elif k == b"CMAP": cmap = c
        elif k == b"CAMG": camg = struct.unpack(">I", c[:4])[0]
        i += 8 + l + (l & 1)
    if not bmhd or body is None:
        return "manca BMHD o BODY"
    w, h, _, _, piani, mask, comp = struct.unpack(">HHhhBBB", bmhd[:11])
    bpr = (w + 15) // 16 * 2
    attesi = bpr * h * (piani + (1 if mask == 1 else 0))
    if comp == 0:
        n = len(body)
    elif comp == 1:
        n, j = 0, 0
        while j < len(body) and n < attesi:
            b = body[j]; j += 1
            if b < 128:
                n += b + 1; j += b + 1
            elif b > 128:
                n += 257 - b; j += 1
    else:
        return "compressione %d" % comp
    modo = "HAM" if camg & 0x800 else ("EHB" if camg & 0x80 else "")
    stato = "ok" if n >= attesi else "CORTO (%d su %d byte)" % (n, attesi)
    return "%dx%dx%d %s palette %d colori %s" % (w, h, piani, modo, len(cmap or b"") // 3, stato)


n = male = 0
for top in ("gfx", "Screens"):
    for r, ds, fs in os.walk(os.path.join(BASE, top)):
        for f in sorted(fs):
            p = os.path.join(r, f)
            esito = prova(p)
            if esito is None:
                continue
            n += 1
            if "ok" not in esito:
                male += 1
            if "ok" not in esito or "Screens" in p:
                print("%-60s %s" % (os.path.relpath(p, BASE), esito))
print("immagini ILBM: %d, con problemi: %d" % (n, male), file=sys.stderr)
