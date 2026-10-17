"""analisi_mod.py - cosa usa un modulo Protracker: firma, canali, tempi (Fxx
sotto 32 = velocita', da 32 in su = BPM sul timer del CIA), effetti, campioni
con loop. Per capire quali moduli mettono alla prova Paula e i CIA.

    python tools/analisi_mod.py <cartella Music o file> [-tutti]
"""
import collections
import os
import struct
import sys

NOMI = {0: "arpeggio", 1: "porta su", 2: "porta giu", 3: "portamento", 4: "vibrato", 5: "porta+vol", 6: "vib+vol",
        7: "tremolo", 8: "pan", 9: "offset", 0xA: "volume slide", 0xB: "salto", 0xC: "volume", 0xD: "break",
        0xE: "esteso", 0xF: "velocita'"}


def analizza(p):
    d = open(p, "rb").read()
    if len(d) < 1084:
        return None
    firma = d[1080:1084]
    canali = {b"M.K.": 4, b"M!K!": 4, b"FLT4": 4, b"4CHN": 4, b"6CHN": 6, b"8CHN": 8, b"FLT8": 8}.get(firma)
    if not canali:
        return {"file": os.path.basename(p), "firma": repr(firma), "non": "non e' un Protracker a 31 campioni"}
    lung = d[950]
    ordine = d[952:952 + 128]
    npat = max(ordine[:lung]) + 1
    eff = collections.Counter()
    estesi = collections.Counter()
    tempi, velocita = set(), set()
    for pat in range(npat):
        for r in range(64):
            for c in range(canali):
                o = 1084 + pat * 64 * 4 * canali + (r * canali + c) * 4
                if o + 4 > len(d):
                    continue
                b = d[o:o + 4]
                e, x = b[2] & 15, b[3]
                if e == 0 and x == 0:
                    continue
                eff[e] += 1
                if e == 0xE:
                    estesi[x >> 4] += 1
                if e == 0xF:
                    (tempi if x >= 32 else velocita).add(x)
    loop = 0
    for i in range(31):
        o = 20 + i * 30
        rl = struct.unpack(">H", d[o + 28:o + 30])[0]
        if rl > 1:
            loop += 1
    return {"file": os.path.basename(p), "firma": firma.decode(), "canali": canali, "posizioni": lung,
            "pattern": npat, "bpm": sorted(tempi), "velocita": sorted(velocita), "campioni con loop": loop,
            "effetti": {NOMI.get(k, k): v for k, v in sorted(eff.items())},
            "estesi E": {"E%X" % k: v for k, v in sorted(estesi.items())}}


def main():
    p = sys.argv[1]
    files = [os.path.join(p, f) for f in sorted(os.listdir(p)) if f.lower().startswith("mod.")] if os.path.isdir(p) else [p]
    for f in files:
        a = analizza(f)
        if not a:
            continue
        if "-tutti" in sys.argv or len(files) == 1:
            print(a)
        else:
            print("%-20s %s ch%s bpm %s vel %s E:%s" % (a["file"], a["firma"], a.get("canali"), a.get("bpm"),
                                                     a.get("velocita"), ",".join(a.get("estesi E", {}))))


if __name__ == "__main__":
    main()
