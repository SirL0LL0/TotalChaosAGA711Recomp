"""genera_topaz.py - amiga_topaz.h, il font 8x8 di sistema del runner al
posto di topaz 8 (che sta nella ROM Kickstart e non si puo' distribuire): il
topaz 8 di AROS, ridisegnato per AROS e open source (AROS Public License,
https://github.com/aros-development-team/AROS, rom/graphics/default_font.c).
Caratteri 32-255 (Latin-1: anche le lettere accentate), il 256esimo e' il
glifo per i caratteri che non ci sono.

Uso: python tools/genera_topaz.py <default_font.c di AROS> amigarecomp/runner/amiga_topaz.h
"""
import re
import sys

testo = open(sys.argv[1], encoding="latin-1").read()
# i #define dei glifi alternativi (l'euro in A4): si usa il ramo _EURO, il primo
macro = {}
for nome, val in re.findall(r"#define\s+(A4_\d)\s+(0x[0-9A-Fa-f]+)", testo):
    macro.setdefault(nome, int(val, 16))
blocco = re.search(r"topaz8_chardata\[\]\s*=\s*\{(.*?)\};", testo, re.S).group(1)
dati = [macro[t] if t in macro else int(t, 16) for t in re.findall(r"0x[0-9A-Fa-f]+|A4_\d", blocco)]
loc_blocco = re.search(r"topaz8_charloc\[\]\s*=\s*\{(.*?)\};", testo, re.S).group(1)
loc = [int(v, 16) for v in re.findall(r"0x[0-9A-Fa-f]+", loc_blocco)]
modulo = int(re.search(r"(\d+),\s*/\*\s*Modulo", testo).group(1))
lo, hi = 32, 255
assert len(dati) == modulo * 8, (len(dati), modulo)
assert len(loc) == hi - lo + 2, len(loc)


def glifo(k):
    """le 8 righe del carattere k-esimo (da LoChar), allineate a sinistra"""
    off, larg = loc[k] >> 16, loc[k] & 0xFFFF
    righe = []
    for r in range(8):
        v = 0
        for b in range(min(larg, 8)):
            bit = off + b
            if dati[r * modulo + bit // 8] >> (7 - bit % 8) & 1:
                v |= 0x80 >> b
        righe.append(v)
    return righe


with open(sys.argv[2], "w", encoding="ascii") as f:
    f.write("/* amiga_topaz.h - GENERATO da tools/genera_topaz.py: il font 8x8 di sistema,\n"
            " * topaz 8 di AROS (rom/graphics/default_font.c, Copyright (C) 1995-2026 The\n"
            " * AROS Development Team, AROS Public License). */\n#pragma once\n#include <stdint.h>\n")
    f.write("static const uint8_t amiga_topaz_dati[256][8] = {\n")
    for c in range(256):
        g = glifo(c - lo) if lo <= c <= hi else [0] * 8
        f.write("    {" + ",".join("0x%02x" % b for b in g) + "}, /* %d */\n" % c)
    f.write("};\n")
    f.write("static const uint8_t amiga_topaz_altro[8] = {" + ",".join("0x%02x" % b for b in glifo(hi - lo + 1)) + "};\n")
    f.write("static inline uint8_t amiga_topaz_glifo(int c, int riga) {\n"
            "    return c >= 32 && c <= 255 ? amiga_topaz_dati[c][riga] : amiga_topaz_altro[riga];\n}\n")
print("glifi", hi - lo + 1, "modulo", modulo)
