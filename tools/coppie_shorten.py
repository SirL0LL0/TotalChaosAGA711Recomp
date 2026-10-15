"""coppie_shorten.py - i file compressi con Shorten del pacchetto e i loro
gemelli decompressi (stesso percorso, maiuscole a parte), per prova_shorten.

    python tools/coppie_shorten.py <cartella lha> <cartella hdf>
"""
import os
import sys

a, b = sys.argv[1], sys.argv[2]
mappa = {}
for d, _, fs in os.walk(b):
    for f in fs:
        p = os.path.join(d, f)
        mappa[os.path.relpath(p, b).lower()] = p
for d, _, fs in os.walk(a):
    for f in fs:
        p = os.path.join(d, f)
        with open(p, "rb") as h:
            if h.read(4) != b"ajkg":
                continue
        q = mappa.get(os.path.relpath(p, a).lower())
        if q:
            sys.stdout.write("%s\t%s\n" % (p, q))
