"""prova_musica.py - accende e spegne la musica (menu 1 voce 3) tante volte
durante la scelta degli incantesimi, senza finestra, e guarda se il gioco
resta vivo e se i canali audio si liberano. Come prova_menu.py.

    python tools/prova_musica.py [-volte 12] [-passo 40] [-voce 3]
"""
import argparse
import os
import re
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import prove_cpu as pc  # noqa: E402
import prova_menu as pm  # noqa: E402


def main():
    a = argparse.ArgumentParser()
    a.add_argument("-volte", type=int, default=12)
    a.add_argument("-passo", type=int, default=40)
    a.add_argument("-voce", type=int, default=3)
    a.add_argument("-dopo", type=int, default=1500)
    a.add_argument("-voci", default="", help="una sequenza di voci al posto della musica: 1.0,1.1,2.5")
    a.add_argument("-prima", default="", help="una voce scelta prima, per esempio 0.0 (Informazioni: il tuono)")
    arg = a.parse_args()
    cartella = os.path.join(pc.REPO, "build", "prove_musica")
    bin_ = os.path.join(cartella, "bin")
    os.makedirs(bin_, exist_ok=True)
    for f in os.listdir(os.path.dirname(pc.EXE)):
        if f.endswith((".exe", ".dll")) or f == "chaos_immagine.bin":     # l'immagine: le build di main
            shutil.copy2(os.path.join(os.path.dirname(pc.EXE), f), bin_)
    exe = os.path.join(bin_, os.path.basename(pc.EXE))
    pc.copia_dati(os.path.join(cartella, "dati"))
    d = os.path.join(cartella, "giro")
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(os.path.join(d, "RAM"))
    m, k, xt, xv, yv, nome = next(v for v in pm.VOCI if v[0] == 1 and v[1] == arg.voce)
    e = ["p320.256:1", "p569.14:700", "l3:705", "p600.15:850", "l3:855", "p592.14:1000", "l3:1005"]
    q = 1600
    if arg.prima:
        pm_, pk = (int(x) for x in arg.prima.split("."))
        _, _, pxt, pxv, pyv, _ = next(v for v in pm.VOCI if v[0] == pm_ and v[1] == pk)
        e += ["p%d.4:%d" % (pxt, q), "r60:%d" % q, "p%d.4:%d" % (pxt, q + 10), "p%d.%d:%d" % (pxv, pyv, q + 20),
              "p%d.%d:%d" % (pxv, pyv, q + 30)]
        q += 100
    seq = [tuple(int(x) for x in s.split(".")) for s in arg.voci.split(",")] if arg.voci else [(1, arg.voce)] * arg.volte
    arg.volte = len(seq)
    for i, (sm, sk) in enumerate(seq):
        _, _, xt, xv, yv, _ = next(v for v in pm.VOCI if v[0] == sm and v[1] == sk)
        t = q + i * arg.passo
        e += ["p%d.4:%d" % (xt, t), "r60:%d" % t, "p%d.4:%d" % (xt, t + 10), "p%d.%d:%d" % (xv, yv, t + 20),
              "p%d.%d:%d" % (xv, yv, t + 30)]
    fine = q + arg.volte * arg.passo
    for t in range(fine + 100, fine + arg.dopo, 300):
        e += ["p320.250:%d" % t, "l3:%d" % (t + 2)]
    env = dict(os.environ)
    env.update(AMIGA_QUADRI=str(fine + arg.dopo), AMIGA_INGRESSI=",".join(e), AMIGA_ORA="1792000000",
               TC_RAM=os.path.join(d, "RAM"), TC_DATI=os.path.join(cartella, "dati"),
               AMIGA_TRACCIA_DA=str(q - 150), AMIGA_WAV=os.path.join(d, "audio.wav"), AMIGA_FOTO="%d,%d,%d" % (q + 25, fine, fine + arg.dopo - 5))
    with open(os.path.join(d, "out.txt"), "w") as o, open(os.path.join(d, "err.txt"), "w") as er:
        try:
            p = subprocess.run([exe], cwd=d, env=env, stdout=o, stderr=er, timeout=900)
            codice = p.returncode
        except subprocess.TimeoutExpired:
            codice = "timeout"
    err = open(os.path.join(d, "err.txt"), errors="replace").read()
    out = open(os.path.join(d, "out.txt"), errors="replace").read()
    pick = re.findall(r"menu chiuso: menu (-?\d+) voce (-?\d+)", err)
    print("%s: codice %s, scelte %d/%d, uscita %s" % (arg.voci or nome, codice, sum(1 for a_, b in pick if int(b) >= 0),
                                                     arg.volte, out.strip().splitlines()[-1:] if out.strip() else "-"))
    # quanto suona, secondo per secondo (dal quadro 0): un effetto che non
    # finisce resta acceso fino in fondo
    w = os.path.join(d, "audio.wav")
    if os.path.exists(w):
        import array
        import wave
        with wave.open(w) as f:
            s = array.array("h", f.readframes(f.getnframes()))
        sec = [max(abs(x) for x in s[i:i + 96000:7]) if s[i:i + 96000] else 0 for i in range(0, len(s), 96000)]
        print("picco per secondo:", " ".join("%d" % (v // 100) for v in sec))
    for l in err.splitlines():
        if l.startswith(("[errore]", "[crash]")) or "DISPATCH MISS" in l or "manca " in l:
            print("  ", l[:300])


if __name__ == "__main__":
    main()
