"""comandi_suoni.py - la lista dei JCsound/JCspeech per tutti i campioni del
gioco, per AMIGA_COMANDI (con AMIGA_SOLO_CARICA: si caricano soltanto).

    python tools/comandi_suoni.py > build/comandi_suoni.txt
"""
import os
import sys

BASE = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "dati_gioco", "DH0", "TotalChaos")
n = 0
for top in ("Sounds", "CreatureNameSpeech", "MiscellaneousSpeech"):
    for r, ds, fs in os.walk(os.path.join(BASE, top)):
        for f in sorted(fs):
            if f.lower().endswith(".txt") or "List" in f or f.upper().startswith(("MOD.", "MED.")) or f == "Player":
                continue
            rel = os.path.relpath(os.path.join(r, f), BASE).replace("\\", "/")
            cmd = "JCsound" if top == "Sounds" else "JCspeech"
            sys.stdout.buffer.write(('%s <>NIL: "%s" -2\n' % (cmd, rel)).encode("latin-1", "replace"))
            n += 1
print(n, file=sys.stderr)
