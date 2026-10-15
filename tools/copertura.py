"""copertura.py - quanto del gioco e' stato eseguito: partite giocate con
--debug (build/cmake/copertura_giocatore.*) piu' le prove automatiche
(build/<serie>/*/copertura.bin e i [suono] dei loro err.txt).

    python tools/copertura.py [-prove prove,prove_menu] [-o build/copertura.md]

Scrive un riepilogo: punti di chiamata del programma e delle librerie,
incantesimi mai sentiti, voci di menu e tasti mai usati, funzioni di sistema
che mancano al runner.
"""
import argparse
import glob
import os
import re

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
B = os.path.join(REPO, "build", "cmake")
DATI = os.path.join(REPO, "dati_gioco", "DH0", "TotalChaos")


def bitmap(p, salta=0):
    b = open(p, "rb").read()[salta:]
    return {2 * (i * 8 + k) for i, x in enumerate(b) if x for k in range(8) if x >> k & 1}


def main():
    a = argparse.ArgumentParser()
    a.add_argument("-prove", default="prove,prove_menu,prove_moduli,prove_umano")
    a.add_argument("-o", default=os.path.join(REPO, "build", "copertura.md"))
    arg = a.parse_args()

    siti_p = os.path.join(B, "copertura_siti.bin")
    if not os.path.exists(siti_p):
        raise SystemExit("manca build/cmake/copertura_siti.bin: ricompila")
    inizio = int.from_bytes(open(siti_p, "rb").read(4), "big")
    siti = bitmap(siti_p, 4)
    prog = {s for s in siti if s >= inizio}

    giocatore = bitmap(os.path.join(B, "copertura_giocatore.bin")) if os.path.exists(
        os.path.join(B, "copertura_giocatore.bin")) else set()
    eventi = {}
    p = os.path.join(B, "copertura_giocatore.txt")
    if os.path.exists(p):
        for r in open(p, encoding="latin-1"):
            if r.startswith("#"):
                continue
            t = r.rstrip("\n").split("\t")
            if len(t) == 3:
                eventi.setdefault(t[0], {})[t[1]] = int(t[2])
    prove = set()
    suoni_prove = set()
    n_prove = 0
    for serie in arg.prove.split(","):
        for c in glob.glob(os.path.join(REPO, "build", serie, "*", "copertura.bin")):
            prove |= bitmap(c)
            n_prove += 1
        for e in glob.glob(os.path.join(REPO, "build", serie, "*", "err.txt")):
            suoni_prove.update(re.findall(r"^\[suono\] q\d+ \S+ (.*)$", open(e, errors="replace").read(), re.M))

    def riga(nome, insieme):
        return "| %s | %d/%d (%.1f%%) | %d/%d (%.1f%%) |" % (
            nome, len(insieme & prog), len(prog), 100.0 * len(insieme & prog) / max(1, len(prog)),
            len(insieme & (siti - prog)), len(siti - prog), 100.0 * len(insieme & (siti - prog)) / max(1, len(siti - prog)))

    out = ["# Copertura di Total Chaos ricompilato", "",
           "| fonte | programma | librerie |", "|---|---|---|",
           riga("partite giocate (--debug)", giocatore),
           riga("prove automatiche (%d processi)" % n_prove, prove),
           riga("**insieme**", giocatore | prove),
           "", "Punti eseguiti solo giocando: %d; solo nelle prove: %d." % (
               len((giocatore - prove) & siti), len((prove - giocatore) & siti))]

    suoni = set(eventi.get("suono", {})) | suoni_prove
    lanci = {os.path.basename(s) for s in suoni if "CastingSounds/" in s}
    tutti = sorted(os.listdir(os.path.join(DATI, "Sounds", "CastingSounds")))
    nomi = {s.split("/")[2] for s in suoni if s.startswith("CreatureNameSpeech/") and s.count("/") >= 3}
    tutti_nomi = sorted(os.listdir(os.path.join(DATI, "CreatureNameSpeech", "English")))
    out += ["", "## Suoni",
            "- incantesimi sentiti: %d su %d" % (len(lanci & set(tutti)), len(tutti)),
            "- nomi detti: %d su %d" % (len(nomi & set(tutti_nomi)), len(tutti_nomi)),
            "- incantesimi mai sentiti: " + ", ".join(x for x in tutti if x not in lanci)]

    menu = eventi.get("menu", {})
    out += ["", "## Menu (dalle partite giocate)"] + \
           ["- %s: %d volte" % (k, v) for k, v in sorted(menu.items())] if menu else ["", "## Menu", "- nessuna voce scelta giocando"]
    tasti = eventi.get("tasto", {})
    out += ["", "## Tasti premuti giocando", ", ".join("%s (%d)" % kv for kv in sorted(tasti.items())) or "nessuno"]
    manca = eventi.get("manca", {})
    out += ["", "## Funzioni di sistema che mancano al runner"] + (["- " + k for k in sorted(manca)] or ["- nessuna"])
    api = eventi.get("api", {})
    out += ["", "## Funzioni di sistema usate giocando: %d" % len(api)] + \
           ["- %s: %d" % kv for kv in sorted(api.items(), key=lambda kv: -kv[1])]
    open(arg.o, "w", encoding="utf-8").write("\n".join(out) + "\n")
    print("\n".join(out[:12]))
    print("riepilogo completo in", arg.o)


if __name__ == "__main__":
    main()
