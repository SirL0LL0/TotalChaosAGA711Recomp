"""prova_lha.py - l'estrattore LHA del runner contro i file estratti da 7-Zip.

    python tools/prova_lha.py <archivio.lha> <cartella estratta da 7-Zip>
"""
import os
import subprocess
import sys
import zlib

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
arch, cart = sys.argv[1], sys.argv[2]


def chiave(p):
    """nome confrontabile: minuscole, '/' e niente caratteri non ASCII (7-Zip e
    l'Amiga li scrivono diversi)"""
    return "".join(c for c in p.replace("\\", "/").lower() if ord(c) < 128)


mio = subprocess.run([os.path.join(REPO, "build", "prova_lha.exe"), arch], capture_output=True)
nostri = {}
for r in mio.stdout.decode("latin-1").splitlines():
    nome, n, crc = r.split("\t")
    nostri[chiave(nome)] = (int(n), int(crc, 16))
print(mio.stderr.decode(errors="replace").strip().splitlines()[-1])
loro = {}
for d, _, fs in os.walk(cart):
    for f in fs:
        p = os.path.join(d, f)
        b = open(p, "rb").read()
        loro[chiave(os.path.relpath(p, cart))] = (len(b), zlib.crc32(b))
uguali = sum(1 for k, v in loro.items() if nostri.get(k) == v)
print("7-Zip: %d file; nostri: %d; uguali: %d" % (len(loro), len(nostri), uguali))
for k in sorted(set(loro) - set(nostri))[:10]:
    print("  manca da noi:", k)
for k in sorted(set(nostri) - set(loro))[:10]:
    print("  solo da noi:", k)
for k in sorted(k for k in loro if k in nostri and nostri[k] != loro[k])[:10]:
    print("  diverso:", k, loro[k], nostri[k])
