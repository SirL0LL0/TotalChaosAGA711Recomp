"""prova_menu.py - ogni voce dei menu, una prova per voce, senza finestra.

Partita normale (James Conwell umano contro Magnus del computer); nella
scelta degli incantesimi si apre il menu col tasto destro, si va sulla voce
e si lascia. Poi la prova va avanti 2500 quadri con qualche clic al centro
dello schermo (per chiudere schermate e richieste) e si guarda: crash, DISPATCH
MISS, funzioni che mancano, e se il gioco e' ancora vivo (Execute/suoni e
chiamate dopo la scelta). Foto in build/prove_menu/<menu>_<voce>/.

    python tools/prova_menu.py [-paralleli 4] [-solo 1.3,2.5]
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

# (menu, voce, x del titolo, x della voce, y della voce, nome)
BARRA = 19                   # la barra dei menu: ChaosPearl 16 + 2, piu' 1
VOCI = []
for k, n in enumerate(["Informazioni", "Nuovo Gioco", "Apri", "Salva", "Salva come", None, None,
                       "Salva preferenze", "Cambia Terreno", "Cambia lo Sfondo", "Mostra impostazioni", "Chiudi"]):
    if n:
        VOCI.append((0, k, 20, 40, BARRA + 13 * k + 6, n))
for k, n in enumerate(["Voce", "Suono", "Suoni dei combattimenti", "Musica", "Mostra evocazioni",
                       "Punteggi ad ogni turno", "Stile di evidenziazione", "Conferma fine turno",
                       "Risultati attuali", "AutoSalvataggio", "Combattimento speciale e silenzioso",
                       "Pausa discorso", "Mostra mosse in automatico", "Mostra tiri in automatico"]):
    VOCI.append((1, k, 80, 100, BARRA + 13 * k + 6, n))
for k, n in enumerate(["English", "Francais", "Magyar", "Deutsch", "Espanol", "Italiano", "Polski", "Norsk",
                       "Svenska", "Suomi", "Portugues", "Brazillianese", "Ellinika"]):
    VOCI.append((2, k + 1, 170, 160, BARRA + 13 * k + 6, n))
for k, n in enumerate(["Mi sono perso", "Tutorial"]):
    VOCI.append((3, k + 1, 250, 250, BARRA + 13 * k + 6, n))


def ingressi(xt, xv, yv):
    e = ["p320.256:1", "p569.14:700", "l3:705", "p600.15:850", "l3:855", "p592.14:1000", "l3:1005"]
    q = 1600                                    # scelta degli incantesimi del mago umano
    e += ["p%d.4:%d" % (xt, q), "r60:%d" % q, "p%d.4:%d" % (xt, q + 15), "p%d.%d:%d" % (xv, yv, q + 30),
          "p%d.%d:%d" % (xv, yv, q + 45)]
    for t in range(q + 300, q + 2500, 400):     # clic al centro: chiude schermate e richieste
        e += ["p320.250:%d" % t, "l3:%d" % (t + 2)]
    return ",".join(e), q + 60


def prova(voce, cartella, posti):
    m, k, xt, xv, yv, nome = voce
    posto = posti.get()
    try:
        d = os.path.join(cartella, "%d_%02d" % (m, k))
        shutil.rmtree(d, ignore_errors=True)
        os.makedirs(os.path.join(d, "RAM"))
        ing, scelta = ingressi(xt, xv, yv)
        env = dict(os.environ)
        env.update(AMIGA_QUADRI=str(scelta + 2500), AMIGA_INGRESSI=ing, AMIGA_ORA="1792000000",
                   TC_RAM=os.path.join(d, "RAM"), TC_DATI=os.path.join(cartella, "dati%d" % posto),
                   AMIGA_TRACCIA_DA=str(scelta - 100), AMIGA_FOTO="%d,%d,%d,%d" % (scelta - 20, scelta + 100, scelta + 800, scelta + 2400))
        with open(os.path.join(d, "out.txt"), "w") as o, open(os.path.join(d, "err.txt"), "w") as er:
            try:
                p = subprocess.run([pc.EXE], cwd=d, env=env, stdout=o, stderr=er, timeout=900)
                codice = p.returncode
            except subprocess.TimeoutExpired:
                codice = "timeout"
        err = open(os.path.join(d, "err.txt"), errors="replace").read()
        out = open(os.path.join(d, "out.txt"), errors="replace").read()
        pick = re.findall(r"menu chiuso: menu (-?\d+) voce (-?\d+) -> MENUPICK \$([0-9A-F]+)", err)
        scelto = any(int(a) == m and int(b) == k for a, b, _ in pick)
        # vivo: chiamate al sistema del task principale negli ultimi 500 quadri
        ultimi = [int(x) for x in re.findall(r"^\[os\] q(\d+) T8000FB8", err, re.M)[-50:]]
        vivo = bool(ultimi) and max(ultimi) > scelta + 2000
        esito = "crash" if ("[errore]" in err or "[crash]" in err) else (
            "uscito" if "quadri " not in out else ("ok" if vivo else "fermo?"))
        return dict(menu=m, voce=k, nome=nome, scelto=scelto, esito=esito, codice=codice,
                    ultimo=max(ultimi) if ultimi else 0,
                    miss=sorted(set(re.findall(r"DISPATCH MISS\] \$([0-9A-F]+)", err))),
                    manca=sorted(set(re.findall(r"manca ([\w.]+/\w+)", err))),
                    errore="\n".join(l for l in err.splitlines() if l.startswith(("[errore]", "[crash]")))[:600])
    finally:
        posti.put(posto)


def main():
    a = argparse.ArgumentParser()
    a.add_argument("-paralleli", type=int, default=4)
    a.add_argument("-solo", default="")
    arg = a.parse_args()
    cartella = os.path.join(pc.REPO, "build", "prove_menu")
    os.makedirs(cartella, exist_ok=True)
    bin_ = os.path.join(cartella, "bin")
    os.makedirs(bin_, exist_ok=True)
    for f in os.listdir(os.path.dirname(pc.EXE)):
        if f.endswith((".exe", ".dll")) or f == "chaos_immagine.bin":
            shutil.copy2(os.path.join(os.path.dirname(pc.EXE), f), bin_)
    pc.EXE = os.path.join(bin_, os.path.basename(pc.EXE))
    posti = queue.Queue()
    for i in range(arg.paralleli):
        pc.copia_dati(os.path.join(cartella, "dati%d" % i))
        posti.put(i)
    voci = VOCI
    if arg.solo:
        scelte = {tuple(int(x) for x in s.split(".")) for s in arg.solo.split(",")}
        voci = [v for v in VOCI if (v[0], v[1]) in scelte]
    ris = []
    with concurrent.futures.ThreadPoolExecutor(arg.paralleli) as ex:
        for r in ex.map(lambda v: prova(v, cartella, posti), voci):
            ris.append(r)
            print("menu %d voce %2d %-36s scelta %s  %s (ultima chiamata q%d) %s %s %s" % (
                r["menu"], r["voce"], r["nome"], "si" if r["scelto"] else "NO", r["esito"], r["ultimo"],
                " ".join(r["miss"]), " ".join(r["manca"]), r["errore"].replace("\n", " | ")))
            sys.stdout.flush()


if __name__ == "__main__":
    main()
