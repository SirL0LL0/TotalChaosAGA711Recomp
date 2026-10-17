"""gioca_da_solo.py - partite a 8 maghi del computer nella finestra, una dopo
l'altra, per la copertura: il gioco parte con --debug (le novita' vanno in
esame/novita.txt), i maghi diventano tutti del computer, le opzioni delle
impostazioni II e III si scelgono a caso (come tools/prove_cpu.py) e la
partita va con l'avanti veloce sempre acceso.

  - Esc nella finestra: la partita automatica si ferma e il controllo torna a
    te (la finestra resta aperta, puoi continuare a mano);
  - chiudere la finestra: si ferma tutto (anche lo script);
  - a fine partita il gioco si chiude da solo e ne parte un'altra con altre
    opzioni.

    python tools/gioca_da_solo.py [-exe build/cmake/TotalChaosAGA711Recomp.exe] [-partite 0]

(-partite 0 = senza fine). Il gioco deve essere gia' installato dal launcher.
"""
import argparse
import os
import random
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import prove_cpu as pc  # noqa: E402


def main():
    a = argparse.ArgumentParser()
    a.add_argument("-exe", default=pc.EXE)
    a.add_argument("-partite", type=int, default=0)
    arg = a.parse_args()
    exe = os.path.abspath(arg.exe)
    n = 0
    while not arg.partite or n < arg.partite:
        n += 1
        seme = random.randrange(1 << 30)
        ingressi, opzioni = pc.ingressi(random.Random(seme))
        f = os.path.join(tempfile.gettempdir(), "tc_gioca_da_solo.txt")
        open(f, "w").write(ingressi)
        print("partita %d (seme %d): %s" % (n, seme, ", ".join(opzioni) or "nessuna opzione"))
        sys.stdout.flush()
        env = dict(os.environ, AMIGA_INGRESSI="@" + f)
        p = subprocess.run([exe, "--debug", "--senza-launcher"], cwd=os.path.dirname(exe), env=env,
                           stdout=subprocess.PIPE, text=True, errors="replace")
        if "fine partita" not in (p.stdout or ""):
            print("finestra chiusa: fine")
            break
        print("  finita: ne parte un'altra")


if __name__ == "__main__":
    main()
