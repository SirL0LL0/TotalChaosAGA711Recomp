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
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
B = os.path.join(REPO, "build", os.environ.get("TC_BUILD", "cmake"))   # TC_BUILD=ramo: un'altra build
DATI = os.path.join(REPO, "dati_gioco", "DH0", "TotalChaos")


def bitmap(p, salta=0):
    b = open(p, "rb").read()[salta:]
    return {2 * (i * 8 + k) for i, x in enumerate(b) if x for k in range(8) if x >> k & 1}


def leggi_eventi(p, eventi):
    """tipo, nome, volte; tra file diversi il massimo (lo stesso evento puo'
    essere gia' passato da una copia all'altra: la somma lo conterebbe due volte)"""
    if not os.path.exists(p):
        return
    for x in open(p, encoding="latin-1"):
        t = x.rstrip("\n").split("\t")
        if len(t) == 3 and not t[0].startswith("#") and t[2].isdigit():
            e = eventi.setdefault(t[0], {})
            e[t[1]] = max(e.get(t[1], 0), int(t[2]))


def scrivi(cartella, nome, punti, eventi, byte):
    """bitmap (un bit ogni 2 byte di indirizzo) ed eventi nel formato di amiga_debug.c"""
    b = bytearray(byte)
    for a in punti:
        if a // 16 < byte:
            b[a // 16] |= 1 << (a // 2 % 8)
    open(os.path.join(cartella, nome + ".bin"), "wb").write(b)
    with open(os.path.join(cartella, nome + ".txt"), "w", encoding="latin-1", newline="\n") as f:
        f.write("# copertura delle partite giocate con --debug: tipo, nome, volte (tutte le sessioni)\n")
        for k in sorted(eventi):
            for n, v in sorted(eventi[k].items()):
                f.write("%s\t%s\t%d\n" % (k, n, v))


def solo_base(B):
    """-base: alla build, game/copertura_base.* (la copertura di tutti, nel
    repo) si unisce a quella accanto all'eseguibile, che il gioco legge con
    --debug e a cui aggiunge le partite nuove"""
    siti_p = os.path.join(B, "copertura_siti.bin")
    base = os.path.join(REPO, "game", "copertura_base.bin")
    if not os.path.exists(siti_p) or not os.path.exists(base):
        return
    byte = os.path.getsize(siti_p) - 4
    punti, eventi = bitmap(base), {}
    leggi_eventi(os.path.join(REPO, "game", "copertura_base.txt"), eventi)
    g = os.path.join(B, "copertura_giocatore.bin")
    if os.path.exists(g):
        punti |= bitmap(g)
    leggi_eventi(os.path.join(B, "copertura_giocatore.txt"), eventi)
    scrivi(B, "copertura_giocatore", punti, eventi, byte)


def main():
    if sys.argv[1:2] == ["-base"]:          # -base [cartella della build]
        return solo_base(sys.argv[2] if len(sys.argv) > 2 else B)
    a = argparse.ArgumentParser()
    a.add_argument("-prove", default="", help="solo queste serie di build/ (es. prove,prove_mirati); vuoto = tutte")
    a.add_argument("-o", default=os.path.join(REPO, "build", "copertura.md"))
    a.add_argument("-tieni", action="store_true", help="non cancellare i file di copertura gia' uniti")
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
    fonti = []                              # da cancellare a fine lavoro: ormai in game/copertura_base.*
    serie = {os.path.normcase(os.path.join(REPO, "build", s)) for s in arg.prove.split(",") if s}
    for r, ds, fs in os.walk(REPO):
        ds[:] = [d for d in ds if d not in (".git", "external")]
        for nome in ("copertura_giocatore", "copertura_base"):   # game/copertura_base.*: quella di tutti
            if nome + ".bin" in fs:
                giocatore |= bitmap(os.path.join(r, nome + ".bin"))
                n_gioc += 1
                leggi_eventi(os.path.join(r, nome + ".txt"), eventi)
                if nome == "copertura_giocatore" and os.path.normcase(r) != os.path.normcase(B):
                    fonti += [os.path.join(r, nome + x) for x in (".bin", ".txt")]
        if "copertura_serie.bin" in fs and (not serie or os.path.normcase(r) in serie):
            prove |= bitmap(os.path.join(r, "copertura_serie.bin"))   # una serie intera di prove_cpu.py
            p = os.path.join(r, "copertura_serie.txt")
            if os.path.exists(p):
                suoni_prove.update(x for x in open(p, encoding="utf-8").read().splitlines() if x)
            fonti += [os.path.join(r, "copertura_serie.bin"), p]
        if "copertura.bin" in fs and (not serie or os.path.normcase(os.path.dirname(r)) in serie):
            prove |= bitmap(os.path.join(r, "copertura.bin"))
            n_prove += 1
            fonti.append(os.path.join(r, "copertura.bin"))
        # i suoni delle prove: dagli err.txt, che restano (servono anche ad altro)
        if "err.txt" in fs and os.path.normcase(r).startswith(os.path.normcase(os.path.join(REPO, "build"))) and \
                (not serie or os.path.normcase(os.path.dirname(r)) in serie):
            suoni_prove.update(re.findall(r"^\[suono\] q\d+ \S+ (.*)$",
                                              open(os.path.join(r, "err.txt"), errors="replace").read(), re.M))

    def riga(nome, insieme):
        return "| %s | %d/%d (%.1f%%) | %d/%d (%.1f%%) |" % (
            nome, len(insieme & prog), len(prog), 100.0 * len(insieme & prog) / max(1, len(prog)),
            len(insieme & (siti - prog)), len(siti - prog), 100.0 * len(insieme & (siti - prog)) / max(1, len(siti - prog)))

    out = ["# Copertura di Total Chaos ricompilato", "",
           "| fonte | programma | librerie |", "|---|---|---|",
           riga("partite giocate e copertura gia' unita (%d file)" % n_gioc, giocatore),
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
    # tutto insieme in game/copertura_base.* (nel repo: chi fa la build parte da
    # qui) e accanto all'eseguibile (la finestra di --debug e le partite nuove);
    # i suoni delle prove come eventi "suono", cosi' restano anche senza i file
    for s in suoni_prove:
        eventi.setdefault("suono", {}).setdefault(s, 1)
    byte = os.path.getsize(siti_p) - 4
    for cartella, nome in ((os.path.join(REPO, "game"), "copertura_base"), (B, "copertura_giocatore")):
        scrivi(cartella, nome, (giocatore | prove) & siti, eventi, byte)
    # pulizia: i file appena uniti non servono piu' (i suoni delle prove
    # restano come eventi "suono" in game/copertura_base.txt)
    if not arg.tieni:
        tolti = 0
        for p in fonti:
            if os.path.isfile(p):
                os.remove(p)
                tolti += 1
        print("cancellati %d file di copertura gia' uniti" % tolti)
    print("\n".join(out[:12]))
    print("riepilogo completo in", arg.o)


if __name__ == "__main__":
    main()
