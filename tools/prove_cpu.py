"""prove_cpu.py - partite intere a 8 maghi del computer, senza finestra.

Ogni partita e' un processo con un seme suo (AMIGA_ORA), le opzioni delle
impostazioni II e III accese o spente a caso, e finisce quando il gioco suona
"YouHaveWon" (AMIGA_FINE_VITTORIA: vittoria.bmp e uscita). Si registrano i
campioni suonati (AMIGA_LOG_SUONI), i DISPATCH MISS, le funzioni di sistema
che mancano e i crash; alla fine un riepilogo con la copertura: quanti
incantesimi e creature si sono sentiti rispetto a quelli che esistono.

    python tools/prove_cpu.py [-n 12] [-paralleli 4] [-quadri 400000] [-seme 1]
Risultati in build/prove/<n>/ (out.txt, err.txt, foto) e build/prove/riepilogo.md
"""
import argparse
import concurrent.futures
import json
import os
import random
import re
import shutil
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.environ.get("TC_EXE") or os.path.join(REPO, "build", "cmake", "TotalChaosAGA711Recomp.exe")   # TC_EXE: un'altra build
DATI = os.path.join(REPO, "dati_gioco", "DH0", "TotalChaos")

# le opzioni a interruttore: (schermata, x, y, nome)
OPZIONI_II = [(628, 104 + 32 * k) for k in range(9)] + [(12, 360)]
NOMI_II = ["Esplorazione", "Impurita", "Agente di lancio", "Artefatti nascosti", "Pergamene nascoste",
           "Texas Gettali", "Senza regole", "Aggressivita", "Uccidi gli umani", "Punti Archoniani"]
OPZIONI_III = [(12, 72 + 32 * k) for k in range(10)] + \
    [(628, y) for y in (72, 104, 168, 200, 232, 264, 296, 328)]          # 360: principiante, a parte
NOMI_III = ["Partenza distanziata", "Squadre bloccate", "Guerre di terra", "Libro guerre di terra",
            "Guerre aeree", "Libro guerre aeree", "Libro debole", "Prima i piu' deboli", "Prima i piu' forti",
            "Maghi forti", "1/2 superpotenziamenti", "1/3 superpotenziamenti", "Guerre in sotterranei",
            "Sotterraneo danneggiato", "Sotterraneo devastato", "Cattura creature", "Cattura incantesimi",
            "Guerre degli spazzini"]
LIVELLO = 9                  # Livello Computer (impostazioni II, campo numerico a 0,129)
PRINCIPIANTE = False         # Modalita' principiante (impostazioni III, 628,360: accesa all'inizio)
UMANO = False                # il primo mago resta umano (e passivo): per la schermata della sconfitta
TASTO_CIFRA = [0x0A, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09]   # codici Amiga di 0-9


def ingressi(rnd):
    """gli 8 maghi attivi e del computer, poi le opzioni a caso"""
    e = ["p320.256:1"]
    q = 540
    def clic(x, y):
        nonlocal q
        e.append("p%d.%d:%d" % (x, y, q))
        e.append("l3:%d" % (q + 5))
        q += 20
    for k in range(1, 7):                       # Attivo dei maghi 2-7 (il primo e l'ottavo lo sono gia')
        clic(100, 104 + 33 * k)
    if not UMANO:
        clic(164, 104)                          # il primo non e' umano
    clic(569, 14)                               # Start: impostazioni II
    q += 60
    if LIVELLO is not None:                     # il campo: clic, cancella, la cifra, Invio
        clic(10, 136)
        for k in [0x46] * 4 + [0x41] * 4 + [TASTO_CIFRA[LIVELLO], 0x44]:
            e.append("k%02X:%d" % (k, q))
            q += 8
        q += 10
    scelte_ii = [rnd.random() < 0.35 for _ in OPZIONI_II]
    for (x, y), s in zip(OPZIONI_II, scelte_ii):
        if s:
            clic(x, y)
    clic(600, 15)                               # Start: impostazioni III
    q += 60
    if not PRINCIPIANTE:
        clic(628, 360)
    scelte_iii = [rnd.random() < 0.3 for _ in OPZIONI_III]
    for (x, y), s in zip(OPZIONI_III, scelte_iii):
        if s:
            clic(x, y)
    clic(592, 14)                               # Start: la partita
    # un clic ogni 1000 quadri sul tabellone: la tabella dei punteggi di fine
    # turno aspetta un clic anche senza maghi umani; e sul segno di spunta in
    # basso a destra, per il "Vuoi veramente iniziare un nuovo gioco?" che il
    # gioco chiede quando non resta nessuno (la partita torna alle impostazioni)
    for t in range(q + 2000, 900000, 1000):
        e.append("p100.100:%d" % t)
        e.append("l3:%d" % (t + 2))
        e.append("p602.462:%d" % (t + 500))
        e.append("l3:%d" % (t + 502))
        if UMANO:                               # il mago umano non fa niente: END TURN chiude le sue fasi
            for dt in (250, 750):
                e.append("p610.410:%d" % (t + dt))
                e.append("l3:%d" % (t + dt + 2))
    nomi = [n for n, s in zip(NOMI_II, scelte_ii) if s] + [n for n, s in zip(NOMI_III, scelte_iii) if s]
    return ",".join(e), nomi


def copia_dati(dest):
    """i dati per un processo: il gioco riscrive i suoi elenchi (*List.txt,
    TalkListsDir) nella cartella dei dati, e due partite insieme si
    rovinavano i file a vicenda. I file piccoli si copiano, i grandi (musica,
    terreni) sono collegamenti fissi: nessuno li scrive"""
    src = os.environ.get("TC_DATI_ORIGINE") or os.path.join(REPO, "dati_gioco", "DH0")   # o un gioco installato
    if os.path.isdir(dest):
        return
    for r, ds, fs in os.walk(src):
        rel = os.path.relpath(r, src)
        os.makedirs(os.path.join(dest, rel), exist_ok=True)
        for f in fs:
            a, b = os.path.join(r, f), os.path.join(dest, rel, f)
            if os.path.getsize(a) < (1 << 20):
                shutil.copy2(a, b)
            else:
                try:
                    os.link(a, b)
                except OSError:
                    shutil.copy2(a, b)


_posti = None
VARIATI = False


def una_partita(n, seme, quadri, cartella, limite):
    posto = _posti.get()
    try:
        return _una_partita(n, seme, quadri, cartella, limite, posto)
    finally:
        _posti.put(posto)


def _una_partita(n, seme, quadri, cartella, limite, posto):
    rnd = random.Random(seme)
    ing, opzioni = ingressi(rnd)
    if VARIATI:                                # i pesi di questa partita nella copia dei dati del posto
        incantesimi_mirati(os.path.join(cartella, "dati%d" % posto), random.Random(seme * 7 + 1))
    d = os.path.join(cartella, "%03d" % n)
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(os.path.join(d, "RAM"))
    env = dict(os.environ)
    env.update(AMIGA_QUADRI=str(quadri), AMIGA_INGRESSI=ing, AMIGA_ORA=str(1792000000 + seme),
               AMIGA_FINE_VITTORIA="1", AMIGA_LOG_SUONI="1", TC_RAM=os.path.join(d, "RAM"), AMIGA_COPERTURA="1",
               TC_DATI=os.path.join(cartella, "dati%d" % posto),
               AMIGA_FOTO="20000-%d/20000" % quadri)
    t0 = time.time()
    with open(os.path.join(d, "out.txt"), "w") as o, open(os.path.join(d, "err.txt"), "w") as er:
        try:
            p = subprocess.run([EXE], cwd=d, env=env, stdout=o, stderr=er, timeout=limite)
            codice = p.returncode
        except subprocess.TimeoutExpired:
            codice = "timeout"
    sec = time.time() - t0
    out = open(os.path.join(d, "out.txt"), errors="replace").read()
    err = open(os.path.join(d, "err.txt"), errors="replace").read()
    m = re.search(r"fine partita \((.*)\) al quadro (\d+)", out)
    if m:
        esito = m.group(1)
    elif "[errore]" in err or "[crash]" in err or (codice not in (0, "timeout")):
        esito = "crash"                         # anche un'uscita con codice d'errore senza messaggio
    elif codice == "timeout":
        esito = "tempo scaduto"
    else:
        esito = "senza fine"
    q = re.search(r"quadri (\d+)", out)
    return {
        "n": n, "seme": seme, "opzioni": opzioni, "esito": esito, "codice": codice, "secondi": round(sec),
        "quadro_vittoria": int(m.group(2)) if m else None, "quadri": int(q.group(1)) if q else None,
        "suoni": sorted(set(re.findall(r"^\[suono\] q\d+ \S+ (.*)$", err, re.M))),
        "miss": sorted(set(re.findall(r"DISPATCH MISS\] \$([0-9A-F]+)", err))),
        "manca": sorted(set(re.findall(r"manca ([\w.]+/\w+)", err))),
        "errore": "\n".join(l for l in err.splitlines() if l.startswith(("[errore]", "[crash]", "[DISPATCH")))[:2000],
    }


def _norm(s):
    return re.sub(r"[^a-z0-9]", "", s.lower())


def incantesimi_mirati(dest, rnd=None):
    """-variati (rnd): ogni incantesimo e creatura con un peso a caso (0, 2, 10
    o 100; anche quelli che le tabelle originali tengono a 0), diverso a ogni
    partita: mazzi sempre diversi, rami del codice diversi.
    -mirati: le tabelle di probabilita' (*.prb) della copia dei dati con gli
    incantesimi mai sentiti (build/copertura.md) a 100 e gli altri a 3, cosi' i
    maghi del computer li ricevono e li lanciano. Le righe con * le ignora il
    gioco; il segno (negativo = solo meditando) resta"""
    rep = os.path.join(REPO, "build", "copertura.md")
    m = re.search(r"^- incantesimi mai sentiti: (.*)$", open(rep, encoding="utf-8").read(), re.M) \
        if os.path.exists(rep) else None
    mai = {_norm(x) for x in m.group(1).split(", ")} if m and not rnd else set()
    src = os.environ.get("TC_DATI_ORIGINE") or os.path.join(REPO, "dati_gioco", "DH0")
    trovati = set()
    for f in os.listdir(os.path.join(src, "TotalChaos")):
        if not f.lower().endswith(".prb"):
            continue
        righe = []
        for r in open(os.path.join(src, "TotalChaos", f), encoding="latin-1").read().splitlines():
            v = re.match(r"^(-?\d+)(\s*#\s*(.*?))\s*$", r)
            if v and not v.group(3).endswith("*"):
                n = _norm(v.group(3))
                segno = -1 if int(v.group(1)) < 0 else 1
                if rnd:
                    p = rnd.choice((0, 0, 0, 2, 10, 100))
                else:
                    p = 100 if n in mai else (3 if int(v.group(1)) else 0)
                trovati |= {n} & mai
                r = "%d%s" % (segno * p, v.group(2))
            righe.append(r)
        open(os.path.join(dest, "TotalChaos", f), "w", encoding="latin-1", newline="\n").write("\n".join(righe) + "\n")
    return len(trovati), len(mai)


def nomi_cartella(sotto):
    p = os.path.join(DATI, sotto)
    return sorted(f for f in os.listdir(p)) if os.path.isdir(p) else []


def main():
    a = argparse.ArgumentParser()
    a.add_argument("-n", type=int, default=12)
    a.add_argument("-paralleli", type=int, default=4)
    a.add_argument("-quadri", type=int, default=400000)
    a.add_argument("-seme", type=int, default=1)
    a.add_argument("-limite", type=int, default=3600, help="secondi per partita")
    a.add_argument("-solo", default="", help="solo queste partite (es. 3,19)")
    a.add_argument("-livello", type=int, default=9, help="Livello Computer (0-9)")
    a.add_argument("-principiante", action="store_true", help="lascia accesa la modalita' principiante")
    a.add_argument("-minuti", type=float, default=0, help="partite nuove finche' non passano tanti minuti")
    a.add_argument("-umano", action="store_true", help="il primo mago resta umano e passivo (sconfitta)")
    a.add_argument("-cartella", default="prove", help="sotto build/: risultati e dati di questa serie")
    a.add_argument("-variati", action="store_true",
                   help="pesi a caso degli incantesimi e delle creature, diversi a ogni partita")
    a.add_argument("-mirati", action="store_true",
                   help="gli incantesimi mai sentiti (build/copertura.md) diventano i piu' frequenti")
    arg = a.parse_args()
    global LIVELLO, PRINCIPIANTE, UMANO, VARIATI
    VARIATI = arg.variati
    LIVELLO, PRINCIPIANTE, UMANO = arg.livello, arg.principiante, arg.umano
    cartella = os.path.join(REPO, "build", arg.cartella)
    os.makedirs(cartella, exist_ok=True)
    # una copia dell'eseguibile e dell'immagine: intanto si puo' ricompilare
    global EXE
    bin_ = os.path.join(cartella, "bin")
    os.makedirs(bin_, exist_ok=True)
    for f in os.listdir(os.path.dirname(EXE)):
        if f.endswith((".exe", ".dll")) or f == "chaos_immagine.bin":
            shutil.copy2(os.path.join(os.path.dirname(EXE), f), bin_)
    EXE = os.path.join(bin_, os.path.basename(EXE))
    global _posti
    import queue
    _posti = queue.Queue()
    for k in range(arg.paralleli):
        copia_dati(os.path.join(cartella, "dati%d" % k))
        if arg.mirati:
            print("dati%d: incantesimi mai sentiti in tabella %d su %d" %
                  ((k,) + incantesimi_mirati(os.path.join(cartella, "dati%d" % k))))
        _posti.put(k)
    risultati = []
    inizio = time.time()
    with concurrent.futures.ThreadPoolExecutor(arg.paralleli) as ex:
        indici = [int(x) for x in arg.solo.split(",")] if arg.solo else list(range(arg.n))
        prossima = max(indici) + 1 if indici else 0
        fut = {ex.submit(una_partita, i, arg.seme * 1000 + i, arg.quadri, cartella, arg.limite) for i in indici}
        while fut:
            fatte, fut = concurrent.futures.wait(fut, return_when=concurrent.futures.FIRST_COMPLETED)
            for f in fatte:
                r = f.result()
                risultati.append(r)
                print("partita %3d seme %d: %s (%s quadri, %d s) %s" % (r["n"], r["seme"], r["esito"],
                      r["quadro_vittoria"] or r["quadri"], r["secondi"], (r["errore"].splitlines() or [""])[0]))
                sys.stdout.flush()
                # -minuti: un'altra partita al posto di quella finita, finche' c'e' tempo
                if arg.minuti and time.time() - inizio < arg.minuti * 60:
                    fut.add(ex.submit(una_partita, prossima, arg.seme * 1000 + prossima, arg.quadri, cartella, arg.limite))
                    prossima += 1
    risultati.sort(key=lambda r: r["n"])
    json.dump(risultati, open(os.path.join(cartella, "risultati.json"), "w"), indent=1)

    # copertura: incantesimi (Sounds/CastingSounds) e nomi (CreatureNameSpeech) sentiti
    sentiti = set()
    for r in risultati:
        sentiti.update(r["suoni"])
    lanci = {os.path.basename(s) for s in sentiti if "CastingSounds/" in s}
    nomi = {s.split("/")[2] for s in sentiti if s.startswith("CreatureNameSpeech/") and s.count("/") >= 3}
    tutti_lanci = nomi_cartella(os.path.join("Sounds", "CastingSounds"))
    tutti_nomi = nomi_cartella(os.path.join("CreatureNameSpeech", "English"))
    # copertura del codice: i punti di chiamata (recomp_push_return con un
    # indirizzo costante nel C generato) eseguiti in almeno una partita
    siti = set()
    gen = os.path.join(REPO, "build", "cmake", "generated")
    for f in os.listdir(gen):
        if f.endswith(".c"):
            siti.update(int(m, 16) for m in re.findall(r"recomp_push_return\(0x([0-9A-Fa-f]+)u\)", open(os.path.join(gen, f)).read()))
    unione = set()
    nuovi = []
    for r in risultati:
        p = os.path.join(cartella, "%03d" % r["n"], "copertura.bin")
        if not os.path.exists(p):
            continue
        b = open(p, "rb").read()
        presi = {2 * (i * 8 + k) for i, x in enumerate(b) if x for k in range(8) if x >> k & 1}
        nuovi.append(len(presi - unione))
        unione |= presi
    # la serie intera in copertura_serie.bin/.txt (unita alle volte prima):
    # tools/copertura.py la conta anche se le cartelle delle partite spariscono
    ps = os.path.join(cartella, "copertura_serie.bin")
    b = bytearray(open(ps, "rb").read()) if os.path.exists(ps) else bytearray()
    for a in unione:
        if a // 16 >= len(b):
            b.extend(bytes(a // 16 + 1 - len(b)))
        b[a // 16] |= 1 << (a // 2 % 8)
    open(ps, "wb").write(b)
    pt = os.path.join(cartella, "copertura_serie.txt")
    vecchi = set(open(pt, encoding="utf-8").read().splitlines()) if os.path.exists(pt) else set()
    open(pt, "w", encoding="utf-8").write("\n".join(sorted(vecchi | sentiti)) + "\n")
    prog = 0x23A000
    siti_p = {s for s in siti if s >= prog}
    righe_cop = ["- punti di chiamata eseguiti: %d su %d (%.1f%%); programma %d su %d (%.1f%%), librerie %d su %d" % (
        len(unione & siti), len(siti), 100.0 * len(unione & siti) / max(1, len(siti)),
        len(unione & siti_p), len(siti_p), 100.0 * len(unione & siti_p) / max(1, len(siti_p)),
        len((unione & siti) - siti_p), len(siti - siti_p)),
        "- punti nuovi partita per partita: " + " ".join(map(str, nuovi))]
    opz = {}
    for r in risultati:
        for o in r["opzioni"]:
            opz[o] = opz.get(o, 0) + 1
    righe = ["# Prove a 8 maghi del computer", "",
             "| n | seme | esito | quadri | s | opzioni |", "|---|---|---|---|---|---|"]
    for r in risultati:
        righe.append("| %d | %d | %s | %s | %d | %s |" % (r["n"], r["seme"], r["esito"],
                     r["quadro_vittoria"] or r["quadri"], r["secondi"], ", ".join(r["opzioni"])))
    vinte = sum(r["esito"] == "vittoria" for r in risultati)
    finite = sum(r["esito"] in ("vittoria", "ritorno alle impostazioni") for r in risultati)
    righe += ["", "Partite finite con la vittoria: %d su %d (finite in tutto: %d, crash: %d)" % (
              vinte, len(risultati), finite, sum(r["esito"] == "crash" for r in risultati)), "",
              "## Copertura",
              "- suoni di lancio sentiti: %d su %d" % (len(lanci & set(tutti_lanci)), len(tutti_lanci)),
              "- nomi di creature/incantesimi detti: %d su %d" % (len(nomi & set(tutti_nomi)), len(tutti_nomi)),
              "- opzioni provate: %d su %d (%s)" % (len(opz), len(NOMI_II) + len(NOMI_III),
                                                   ", ".join("%s %d" % kv for kv in sorted(opz.items()))),
              ] + righe_cop + [
              "- mai sentiti (lancio): " + ", ".join(sorted(set(tutti_lanci) - lanci)),
              "", "## Problemi"]
    for r in risultati:
        for k in ("miss", "manca"):
            if r[k]:
                righe.append("- partita %d %s: %s" % (r["n"], k, " ".join(r[k])))
        if r["esito"] == "crash":
            righe.append("- partita %d crash:\n```\n%s\n```" % (r["n"], r["errore"]))
    open(os.path.join(cartella, "riepilogo.md"), "w", encoding="utf-8").write("\n".join(righe) + "\n")
    print("\n".join(righe[-12:]))


if __name__ == "__main__":
    main()
