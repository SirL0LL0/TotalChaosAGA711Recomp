"""prova_requester.py - apre un requester di file da un menu (0.9 Cambia lo
Sfondo: le texture, l'elenco piu' lungo) e lo usa: frecce giu' oltre la fine,
frecce su oltre l'inizio, trascinamento della manopola, clic sul contenitore.
Una foto dopo ogni gruppo. Senza finestra, come prova_menu.py.

    python tools/prova_requester.py [-voce 0.9]
"""
import argparse
import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import prove_cpu as pc  # noqa: E402
import prova_menu as pm  # noqa: E402

BARRA_X = 323              # la barra dell'elenco dei file
SU_Y, GIU_Y = 76, 195      # le frecce


def main():
    a = argparse.ArgumentParser()
    a.add_argument("-voce", default="0.9")
    arg = a.parse_args()
    cartella = os.path.join(pc.REPO, "build", "prova_requester")
    bin_ = os.path.join(cartella, "bin")
    os.makedirs(bin_, exist_ok=True)
    for f in os.listdir(os.path.dirname(pc.EXE)):
        if f.endswith((".exe", ".dll")):
            shutil.copy2(os.path.join(os.path.dirname(pc.EXE), f), bin_)
    exe = os.path.join(bin_, os.path.basename(pc.EXE))
    pc.copia_dati(os.path.join(cartella, "dati"))
    d = os.path.join(cartella, "giro")
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(os.path.join(d, "RAM"))
    m, k = (int(x) for x in arg.voce.split("."))
    _, _, xt, xv, yv, _ = next(v for v in pm.VOCI if v[0] == m and v[1] == k)
    e = ["p320.256:1", "p569.14:700", "l3:705", "p600.15:850", "l3:855", "p592.14:1000", "l3:1005"]
    q = 1600
    e += ["p%d.4:%d" % (xt, q), "r60:%d" % q, "p%d.4:%d" % (xt, q + 10), "p%d.%d:%d" % (xv, yv, q + 20),
          "p%d.%d:%d" % (xv, yv, q + 30)]
    t = q + 200
    foto = [t - 5]
    for _ in range(80):                                  # giu' oltre la fine
        e += ["p%d.%d:%d" % (BARRA_X, GIU_Y, t), "l3:%d" % (t + 1)]
        t += 8
    foto.append(t + 10)
    t += 20
    for _ in range(80):                                  # su oltre l'inizio
        e += ["p%d.%d:%d" % (BARRA_X, SU_Y, t), "l3:%d" % (t + 1)]
        t += 8
    foto.append(t + 10)
    t += 20
    # trascinamento: si prende la manopola in cima e la si porta a meta'
    e += ["p%d.%d:%d" % (BARRA_X, 88, t), "l40:%d" % (t + 1)]
    for i in range(1, 9):
        e += ["p%d.%d:%d" % (BARRA_X, 88 + 6 * i, t + 2 + 4 * i)]
    foto.append(t + 45)
    foto.append(t + 80)                                  # dopo il rilascio: l'elenco segue?
    t += 100
    e += ["p%d.%d:%d" % (BARRA_X, GIU_Y, t), "l120:%d" % (t + 1)]   # freccia giu' tenuta
    foto.append(t + 60)
    foto.append(t + 130)
    t += 150
    e += ["p%d.%d:%d" % (BARRA_X, SU_Y, t), "l200:%d" % (t + 1)]   # freccia su tenuta
    foto.append(t + 100)
    foto.append(t + 210)
    t += 230
    e += ["p%d.%d:%d" % (BARRA_X, 185, t), "l3:%d" % (t + 1)]  # clic sul contenitore sotto: una pagina
    foto.append(t + 15)
    fine = t + 40
    env = dict(os.environ)
    env.update(AMIGA_QUADRI=str(fine), AMIGA_INGRESSI=",".join(e), AMIGA_ORA="1792000000",
               TC_RAM=os.path.join(d, "RAM"), TC_DATI=os.path.join(cartella, "dati"),
               AMIGA_TRACCIA_DA=str(q), AMIGA_FOTO=",".join(str(f) for f in foto))
    with open(os.path.join(d, "out.txt"), "w") as o, open(os.path.join(d, "err.txt"), "w") as er:
        p = subprocess.run([exe], cwd=d, env=env, stdout=o, stderr=er, timeout=900)
    print("codice", p.returncode, "foto", foto)


if __name__ == "__main__":
    main()
