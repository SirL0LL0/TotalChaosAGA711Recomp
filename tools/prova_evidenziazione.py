"""prova_evidenziazione.py - menu 1 voce 6 (lo stile di evidenziazione):
si apre la schermata con i dieci riquadri e si sceglie il riquadro k, una
prova per riquadro in parallelo, senza finestra. Come prova_menu.py.

    python tools/prova_evidenziazione.py [-solo 0,3]
"""
import argparse
import concurrent.futures
import os
import queue
import re
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import prove_cpu as pc  # noqa: E402
import prova_menu as pm  # noqa: E402


def prova(k, cartella, exe, posti):
    posto = posti.get()
    try:
        d = os.path.join(cartella, "riquadro%d" % k)
        shutil.rmtree(d, ignore_errors=True)
        os.makedirs(os.path.join(d, "RAM"))
        _, _, xt, xv, yv, _ = next(v for v in pm.VOCI if v[0] == 1 and v[1] == 6)
        e = ["p320.256:1", "p569.14:700", "l3:705", "p600.15:850", "l3:855", "p592.14:1000", "l3:1005"]
        q = 1600
        e += ["p%d.4:%d" % (xt, q), "r60:%d" % q, "p%d.4:%d" % (xt, q + 10), "p%d.%d:%d" % (xv, yv, q + 20),
              "p%d.%d:%d" % (xv, yv, q + 30)]
        x = 16 + 64 * k
        e += ["p%d.83:%d" % (x, q + 200), "l3:%d" % (q + 202)]
        for t in range(q + 500, q + 2000, 300):
            e += ["p320.250:%d" % t, "l3:%d" % (t + 2)]
        env = dict(os.environ)
        env.update(AMIGA_QUADRI=str(q + 2000), AMIGA_INGRESSI=",".join(e), AMIGA_ORA="1792000000",
                   TC_RAM=os.path.join(d, "RAM"), TC_DATI=os.path.join(cartella, "dati%d" % posto),
                   AMIGA_TRACCIA_DA=str(q + 150), AMIGA_FOTO="%d,%d,%d" % (q + 150, q + 260, q + 1995))
        with open(os.path.join(d, "out.txt"), "w") as o, open(os.path.join(d, "err.txt"), "w") as er:
            try:
                codice = subprocess.run([exe], cwd=d, env=env, stdout=o, stderr=er, timeout=900).returncode
            except subprocess.TimeoutExpired:
                codice = "timeout"
        err = open(os.path.join(d, "err.txt"), errors="replace").read()
        out = open(os.path.join(d, "out.txt"), errors="replace").read()
        righe = [l for l in err.splitlines() if l.startswith(("[errore]", "[crash]")) or "DISPATCH MISS" in l]
        ultimo = re.findall(r"^\[os\] q(\d+) ", err, re.M)
        return k, codice, "quadri " in out, ultimo[-1] if ultimo else "-", righe[:4]
    finally:
        posti.put(posto)


def main():
    a = argparse.ArgumentParser()
    a.add_argument("-solo", default="")
    a.add_argument("-paralleli", type=int, default=4)
    arg = a.parse_args()
    cartella = os.path.join(pc.REPO, "build", "prove_evidenziazione")
    bin_ = os.path.join(cartella, "bin")
    os.makedirs(bin_, exist_ok=True)
    for f in os.listdir(os.path.dirname(pc.EXE)):
        if f.endswith((".exe", ".dll")):
            shutil.copy2(os.path.join(os.path.dirname(pc.EXE), f), bin_)
    exe = os.path.join(bin_, os.path.basename(pc.EXE))
    posti = queue.Queue()
    for i in range(arg.paralleli):
        pc.copia_dati(os.path.join(cartella, "dati%d" % i))
        posti.put(i)
    ks = [int(x) for x in arg.solo.split(",")] if arg.solo else list(range(10))
    with concurrent.futures.ThreadPoolExecutor(arg.paralleli) as ex:
        for k, codice, finito, ultimo, righe in ex.map(lambda k: prova(k, cartella, exe, posti), ks):
            print("riquadro %d: codice %s, %s, ultima chiamata q%s %s" % (
                k, codice, "arrivato in fondo" if finito else "USCITO PRIMA", ultimo, " | ".join(righe)))
            sys.stdout.flush()


if __name__ == "__main__":
    main()
