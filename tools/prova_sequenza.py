"""prova_sequenza.py - una sequenza di azioni nella scelta degli incantesimi,
senza finestra, per rifare un difetto visto giocando:

    m0.4      la voce 4 del menu 0 (prova_menu.VOCI)
    c585,478  un clic sinistro in quel punto del video 640 x 512
    w100      aspetta 100 quadri
    f         una foto (foto_<quadro>.bmp)

    python tools/prova_sequenza.py m0.4 w150 c585,478 w100 m0.8 w150 f
"""
import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import prove_cpu as pc  # noqa: E402
import prova_menu as pm  # noqa: E402


def main():
    cartella = os.path.join(pc.REPO, "build", "prova_sequenza")
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
    e = ["p320.256:1", "p569.14:700", "l3:705", "p600.15:850", "l3:855", "p592.14:1000", "l3:1005"]
    t = 1600
    foto = []
    for a in sys.argv[1:]:
        if a[0] == "m":
            mm, kk = (int(x) for x in a[1:].split("."))
            _, _, xt, xv, yv, _ = next(v for v in pm.VOCI if v[0] == mm and v[1] == kk)
            e += ["p%d.4:%d" % (xt, t), "r60:%d" % t, "p%d.4:%d" % (xt, t + 10), "p%d.%d:%d" % (xv, yv, t + 20),
                  "p%d.%d:%d" % (xv, yv, t + 30)]
            t += 70
        elif a[0] == "c":
            x, y = (int(v) for v in a[1:].split(","))
            e += ["p%d.%d:%d" % (x, y, t), "l4:%d" % (t + 2)]
            t += 10
        elif a[0] == "w":
            t += int(a[1:])
        elif a[0] == "f":
            foto.append(t)
            t += 2
    env = dict(os.environ)
    env.update(AMIGA_QUADRI=str(t + 20), AMIGA_INGRESSI=",".join(e), AMIGA_ORA="1792000000",
               TC_RAM=os.path.join(d, "RAM"), TC_DATI=os.path.join(cartella, "dati"),
               AMIGA_TRACCIA_DA="1580", AMIGA_FOTO=",".join(str(f) for f in foto) or "1")
    with open(os.path.join(d, "out.txt"), "w") as o, open(os.path.join(d, "err.txt"), "w") as er:
        p = subprocess.run([exe], cwd=d, env=env, stdout=o, stderr=er, timeout=1800)
    print("codice", p.returncode, "foto", foto, "in", d)


if __name__ == "__main__":
    main()
