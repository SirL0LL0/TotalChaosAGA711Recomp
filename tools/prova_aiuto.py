"""prova_aiuto.py - menu 3 voce 1 ("Mi sono perso!") e poi i clic sugli
argomenti dell'aiuto come nella sessione del 16/10 in cui la grafica in alto
si e' rovinata; una foto dopo ogni clic. Senza finestra, come prova_menu.py.

    python tools/prova_aiuto.py
"""
import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import prove_cpu as pc  # noqa: E402
import prova_menu as pm  # noqa: E402

# (quadri dopo la scelta del menu, x, y del video) dalla sessione registrata
CLIC = [(29, 205, 24), (57, 205, 24), (78, 205, 24), (116, 185, 69), (132, 185, 69), (166, 172, 111),
        (184, 171, 115), (206, 160, 168), (220, 160, 168), (246, 156, 223), (263, 156, 224), (293, 156, 272),
        (311, 156, 272), (344, 159, 334)]


def main():
    cartella = os.path.join(pc.REPO, "build", "prova_aiuto")
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
    _, _, xt, xv, yv, _ = next(v for v in pm.VOCI if v[0] == 3 and v[1] == 1)
    e = ["p320.256:1", "p569.14:700", "l3:705", "p600.15:850", "l3:855", "p592.14:1000", "l3:1005"]
    q = 1600
    e += ["p%d.4:%d" % (xt, q), "r60:%d" % q, "p%d.4:%d" % (xt, q + 10), "p%d.%d:%d" % (xv, yv, q + 20),
          "p%d.%d:%d" % (xv, yv, q + 30)]
    s = q + 60
    foto = [s - 5]
    for t, x, y in CLIC:
        e += ["p%d.%d:%d" % (x, y, s + t - 2), "l3:%d" % (s + t)]
        foto.append(s + t + 15)
    fine = s + CLIC[-1][0] + 300
    foto.append(fine - 5)
    env = dict(os.environ)
    env.update(AMIGA_QUADRI=str(fine), AMIGA_INGRESSI=",".join(e), AMIGA_ORA="1792000000",
               TC_RAM=os.path.join(d, "RAM"), TC_DATI=os.path.join(cartella, "dati"),
               AMIGA_TRACCIA_DA=str(q - 20), AMIGA_FOTO=",".join(str(f) for f in foto))
    with open(os.path.join(d, "out.txt"), "w") as o, open(os.path.join(d, "err.txt"), "w") as er:
        p = subprocess.run([exe], cwd=d, env=env, stdout=o, stderr=er, timeout=900)
    print("codice", p.returncode, "foto", foto)


if __name__ == "__main__":
    main()
