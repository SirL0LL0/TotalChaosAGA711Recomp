"""prova_descrizioni.py - le descrizioni degli incantesimi (tasto q sopra
un'icona del pannello) una dopo l'altra, con traccia, audio in un WAV e una
foto per descrizione. Senza finestra, come prova_menu.py.

    python tools/prova_descrizioni.py [-quante 10] [-dopo 1600]
"""
import argparse
import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import prove_cpu as pc  # noqa: E402

ICONE = [(x, 70 + 33 * r) for r in range(10) for x in (582, 617)]


def main():
    a = argparse.ArgumentParser()
    a.add_argument("-quante", type=int, default=10)
    a.add_argument("-passo", type=int, default=250)
    a.add_argument("-ora", default="1792000000", help="seme: cambia le carte distribuite")
    a.add_argument("-giro", default="giro")
    arg = a.parse_args()
    cartella = os.path.join(pc.REPO, "build", "prova_descrizioni")
    bin_ = os.path.join(cartella, "bin")
    os.makedirs(bin_, exist_ok=True)
    for f in os.listdir(os.path.dirname(pc.EXE)):
        if f.endswith((".exe", ".dll")):
            shutil.copy2(os.path.join(os.path.dirname(pc.EXE), f), bin_)
    exe = os.path.join(bin_, os.path.basename(pc.EXE))
    pc.copia_dati(os.path.join(cartella, "dati"))
    d = os.path.join(cartella, arg.giro)
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(os.path.join(d, "RAM"))
    e = ["p320.256:1", "p569.14:700", "l3:705", "p600.15:850", "l3:855", "p592.14:1000", "l3:1005"]
    t = 1600
    foto = []
    for k in range(arg.quante):
        x, y = ICONE[k]
        e += ["p%d.%d:%d" % (x, y, t), "k10:%d" % (t + 5)]
        foto.append(t + 120)
        e += ["p320.250:%d" % (t + arg.passo - 60), "l3:%d" % (t + arg.passo - 58)]   # un clic chiude
        t += arg.passo
    env = dict(os.environ)
    env.update(AMIGA_QUADRI=str(t + 50), AMIGA_INGRESSI=",".join(e), AMIGA_ORA=arg.ora,
               TC_RAM=os.path.join(d, "RAM"), TC_DATI=os.path.join(cartella, "dati"),
               AMIGA_TRACCIA_DA="1580", AMIGA_WAV=os.path.join(d, "audio.wav"),
               AMIGA_FOTO=",".join(str(f) for f in foto))
    with open(os.path.join(d, "out.txt"), "w") as o, open(os.path.join(d, "err.txt"), "w") as er:
        p = subprocess.run([exe], cwd=d, env=env, stdout=o, stderr=er, timeout=1800)
    print("codice", p.returncode, "foto", foto)


if __name__ == "__main__":
    main()
