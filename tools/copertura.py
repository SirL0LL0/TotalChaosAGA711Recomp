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
B = os.path.join(REPO, "build", os.environ.get("TC_BUILD", "cmake"))   # TC_BUILD=ramo: un'altra build
DATI = os.path.join(REPO, "dati_gioco", "DH0", "TotalChaos")


def bitmap(p, salta=0):
    b = open(p, "rb").read()[salta:]
    return {2 * (i * 8 + k) for i, x in enumerate(b) if x for k in range(8) if x >> k & 1}


def main():
    a = argparse.ArgumentParser()
    a.add_argument("-prove", default="", help="solo queste serie di build/ (es. prove,prove_mirati); vuoto = tutte")
    a.add_argument("-o", default=os.path.join(REPO, "build", "copertura.md"))
    arg = a.parse_args()

    siti_p = os.path.join(B, "copertura_siti.bin")
    if not os.path.exists(siti_p):
        raise SystemExit("manca build/cmake/copertura_siti.bin: ricompila")
    inizio = int.from_bytes(open(siti_p, "rb").read(4), "big")
    siti = bitmap(siti_p, 4)
    prog = {s for s in siti if s >= inizio}

    # tutti i file di copertura nella cartella del repo e sotto: le partite
    # giocate (copertura_giocatore.bin/.txt, anche di altre build o copie del
    # gioco) e le prove automatiche (copertura.bin con il loro err.txt);
    # -prove limita le prove a quelle serie di build/
    giocatore, eventi, n_gioc = set(), {}, 0
    prove, suoni_prove, n_prove = set(), set(), 0
    serie = {os.path.normcase(os.path.join(REPO, "build", s)) for s in arg.prove.split(",") if s}
    for r, ds, fs in os.walk(REPO):
        ds[:] = [d for d in ds if d not in (".git", "external")]
        if "copertura_giocatore.bin" in fs:
            giocatore |= bitmap(os.path.join(r, "copertura_giocatore.bin"))
            n_gioc += 1
            p = os.path.join(r, "copertura_giocatore.txt")
            for t in (x.rstrip("\n").split("\t") for x in open(p, encoding="latin-1")) if os.path.exists(p) else []:
                if len(t) == 3 and not t[0].startswith("#"):
                    e = eventi.setdefault(t[0], {})
                    e[t[1]] = e.get(t[1], 0) + int(t[2])
        if "copertura.bin" in fs and (not serie or os.path.normcase(os.path.dirname(r)) in serie):
            prove |= bitmap(os.path.join(r, "copertura.bin"))
            n_prove += 1
            if "err.txt" in fs:
                suoni_prove.update(re.findall(r"^\[suono\] q\d+ \S+ (.*)$",
                                              open(os.path.join(r, "err.txt"), errors="replace").read(), re.M))

    def riga(nome, insieme):
        return "| %s | %d/%d (%.1f%%) | %d/%d (%.1f%%) |" % (
            nome, len(insieme & prog), len(prog), 100.0 * len(insieme & prog) / max(1, len(prog)),
            len(insieme & (siti - prog)), len(siti - prog), 100.0 * len(insieme & (siti - prog)) / max(1, len(siti - prog)))

    out = ["# Copertura di Total Chaos ricompilato", "",
           "| fonte | programma | librerie |", "|---|---|---|",
           riga("partite giocate (--debug, %d file)" % n_gioc, giocatore),
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
