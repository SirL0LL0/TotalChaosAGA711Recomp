"""genera_lola.py - amigarecomp/runner/amiga_lola_dati.h dal preset Lola Retro
1084S per Amiberry (terze_parti/lola-retro, MIT, di Zoki: github.com/
zoran123456/lola-retro): i passaggi del .glslp e i sorgenti GLSL dentro
l'eseguibile, cosi' non servono file accanto.

    python tools/genera_lola.py
"""
import os

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOLA = os.path.join(REPO, "terze_parti", "lola-retro")
OUT = os.path.join(REPO, "amigarecomp", "runner", "amiga_lola_dati.h")
SCALE = {"source": 0, "viewport": 1, "absolute": 2}


def c_stringa(testo):
    righe = []
    for r in testo.replace("\r\n", "\n").split("\n"):
        r = r.replace("\\", "\\\\").replace('"', '\\"')
        righe.append('    "%s\\n"' % r)
    return "\n".join(righe)


def main():
    preset = {}
    for r in open(os.path.join(LOLA, "amiga-1084s.glslp"), encoding="utf-8"):
        r = r.split("#")[0].strip()
        if "=" in r:
            k, v = (x.strip() for x in r.split("=", 1))
            preset[k] = v.strip('"')
    n = int(preset["shaders"])
    file_ = []
    passi = []
    for i in range(n):
        f = preset["shader%d" % i]
        if f not in file_:
            file_.append(f)
        g = lambda k, d="": preset.get("%s%d" % (k, i), d)
        passi.append("    { %d, \"%s\", %d, %d, %d, %sf, %d, %sf, %d }," % (
            file_.index(f), g("alias"), g("filter_linear", "false") == "true",
            g("wrap_mode") == "clamp_to_border", SCALE[g("scale_type_x", "source")], "%r" % float(g("scale_x", "1.0")),
            SCALE[g("scale_type_y", "source")], "%r" % float(g("scale_y", "1.0")), g("float_framebuffer", "false") == "true"))
    out = ["/* generato da tools/genera_lola.py: Lola Retro, Commodore 1084S per Amiberry",
           " * (terze_parti/lola-retro, licenza MIT, Copyright (c) 2026 Zoki). Non modificare. */",
           "#pragma once", ""]
    for k, f in enumerate(file_):
        testo = open(os.path.join(LOLA, *f.split("/")), encoding="utf-8").read()
        out += ["/* %s */" % f, "static const char LOLA_SRC%d[] =" % k, c_stringa(testo) + ";", ""]
    out += ["static const char *const LOLA_SORGENTI[] = { %s };" % ", ".join("LOLA_SRC%d" % k for k in range(len(file_))),
            "/* sorgente, alias, lineare, bordo nero, scala x (0 sorgente, 1 viewport, 2 assoluta), valore,",
            " * scala y, valore, framebuffer float */",
            "typedef struct { int src; const char *alias; int lineare, bordo, tx; float sx; int ty; float sy; int fl; } lola_passo_t;",
            "static const lola_passo_t LOLA_PASSI[%d] = {" % n] + passi + ["};", "#define LOLA_N_PASSI %d" % n, ""]
    open(OUT, "w", encoding="utf-8", newline="\n").write("\n".join(out))
    print(OUT, n, "passaggi,", len(file_), "sorgenti")


if __name__ == "__main__":
    main()
