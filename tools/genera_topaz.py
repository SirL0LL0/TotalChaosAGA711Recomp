"""genera_topaz.py - amiga_topaz.h, il font 8x8 di sistema del runner (al
posto di topaz 8, che sta nella ROM Kickstart), da font8x8_basic.h di Daniel
Hepper (pubblico dominio, https://github.com/dhepper/font8x8).

Uso: python tools/genera_topaz.py <font8x8_basic.h> amigarecomp/runner/amiga_topaz.h
"""
import re
import sys

testo = open(sys.argv[1], encoding="latin-1").read()
righe = re.findall(r"\{\s*((?:0x[0-9A-Fa-f]{2},?\s*){8})\}", testo)[:128]
glifi = [[int(v, 16) for v in re.findall(r"0x[0-9A-Fa-f]{2}", r)] for r in righe]


def rovescia(b):
    """font8x8: il bit 0 e' il pixel a sinistra; sull'Amiga e' il bit 7"""
    return int(f"{b:08b}"[::-1], 2)


altro = [0x7E, 0x42, 0x42, 0x42, 0x42, 0x42, 0x7E, 0x00]      # riquadro: caratteri senza disegno
with open(sys.argv[2], "w", encoding="ascii") as f:
    f.write("/* amiga_topaz.h - GENERATO da tools/genera_topaz.py: font 8x8 di sistema,\n"
            " * da font8x8_basic.h di Daniel Hepper (pubblico dominio). */\n#pragma once\n#include <stdint.h>\n")
    f.write("static const uint8_t amiga_topaz_dati[128][8] = {\n")
    for c, g in enumerate(glifi):
        f.write("    {" + ",".join(f"0x{rovescia(b):02x}" for b in g) + f"}}, /* {c} */\n")
    f.write("};\n")
    f.write("static const uint8_t amiga_topaz_altro[8] = {" + ",".join(f"0x{b:02x}" for b in altro) + "};\n")
    f.write("static inline uint8_t amiga_topaz_glifo(int c, int riga) {\n"
            "    return c >= 32 && c < 127 ? amiga_topaz_dati[c][riga] : amiga_topaz_altro[riga];\n}\n")
print(len(glifi), "glifi")
