"""prova_moduli.py - ogni modulo musicale del gioco, senza finestra.

Il gioco sceglie il modulo da Music/MusicCount.txt (il prossimo numero):
qui lo si scrive, si avvia il gioco per qualche migliaio di quadri (titolo e
impostazioni suonano il modulo) e si cercano crash, DISPATCH MISS e il codice
eseguito da Musashi (i moduli "custom" col player in 68000).

    python tools/prova_moduli.py [-paralleli 4] [-quadri 3000] [-solo 78,121]
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


def prova(n, cartella, posti, quadri):
    posto = posti.get()
    try:
        dati = os.path.join(cartella, "dati%d" % posto)
        open(os.path.join(dati, "TotalChaos", "Music", "MusicCount.txt"), "w").write("%d\n" % n)
        d = os.path.join(cartella, "m%03d" % n)
        shutil.rmtree(d, ignore_errors=True)
        os.makedirs(os.path.join(d, "RAM"))
        env = dict(os.environ)
        env.update(AMIGA_QUADRI=str(quadri), TC_RAM=os.path.join(d, "RAM"), TC_DATI=dati, AMIGA_TRACCIA_DA="1",
                   AMIGA_ORA="1792000000")
        with open(os.path.join(d, "out.txt"), "w") as o, open(os.path.join(d, "err.txt"), "w") as er:
            try:
                subprocess.run([pc.EXE], cwd=d, env=env, stdout=o, stderr=er, timeout=600)
            except subprocess.TimeoutExpired:
                pass
        err = open(os.path.join(d, "err.txt"), errors="replace").read()
        out = open(os.path.join(d, "out.txt"), errors="replace").read()
        mod = re.findall(r'Open\("(Music/[^"]*)"', err)
        esito = "crash" if ("[errore]" in err or "[crash]" in err) else ("ok" if "quadri " in out else "uscito")
        r = dict(n=n, moduli=sorted(set(m for m in mod if "Count" not in m)), esito=esito,
                 interprete="[interprete]" in err,
                 errore=" | ".join(l for l in err.splitlines() if l.startswith(("[errore]", "[crash]", "[DISPATCH"))))
        if esito == "ok":
            shutil.rmtree(d, ignore_errors=True)          # niente da guardare: si risparmia spazio
        return r
    finally:
        posti.put(posto)


def main():
    a = argparse.ArgumentParser()
    a.add_argument("-paralleli", type=int, default=4)
    a.add_argument("-quadri", type=int, default=3000)
    a.add_argument("-solo", default="")
    arg = a.parse_args()
    cartella = os.path.join(pc.REPO, "build", "prove_moduli")
    os.makedirs(cartella, exist_ok=True)
    bin_ = os.path.join(cartella, "bin")
    os.makedirs(bin_, exist_ok=True)
    for f in os.listdir(os.path.dirname(pc.EXE)):
        if (f.endswith((".exe", ".dll")) and "vecchio" not in f) or f == "chaos_immagine.bin":
            shutil.copy2(os.path.join(os.path.dirname(pc.EXE), f), bin_)
    pc.EXE = os.path.join(bin_, os.path.basename(pc.EXE))
    posti = queue.Queue()
    for i in range(arg.paralleli):
        pc.copia_dati(os.path.join(cartella, "dati%d" % i))
        posti.put(i)
    numeri = [int(x) for x in arg.solo.split(",")] if arg.solo else list(range(0, 180))
    with concurrent.futures.ThreadPoolExecutor(arg.paralleli) as ex:
        for r in ex.map(lambda n: prova(n, cartella, posti, arg.quadri), numeri):
            print("MusicCount %3d: %-6s %s%s %s" % (r["n"], r["esito"], " ".join(r["moduli"]),
                  " (Musashi)" if r["interprete"] else "", r["errore"]))
            sys.stdout.flush()


if __name__ == "__main__":
    main()
