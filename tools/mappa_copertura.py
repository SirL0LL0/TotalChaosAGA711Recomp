"""mappa_copertura.py - cosa c'e' nei punti di chiamata mai eseguiti.

Dal C generato (build/cmake/generated) ogni funzione ricompilata con i suoi
punti di chiamata (recomp_push_return), le funzioni che chiama e i testi che
usa (indirizzi assoluti e d16(A4) che nell'immagine puntano a una stringa).
Con la copertura comune (game/copertura_base.bin) dice quali funzioni non
sono mai state eseguite, chi le chiama (se il chiamante e' coperto, manca solo
la condizione giusta) e di che parlano: messaggi di errore, incantesimi,
menu... Scrive build/copertura_mappa.md.

    python tools/mappa_copertura.py [-n 300]
"""
import argparse
import os
import re

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
B = os.path.join(REPO, "build", os.environ.get("TC_BUILD", "cmake"))
A4 = 0x4C8F68
PROGRAMMA = 0x23A000


def bitmap(p, salta=0):
    b = open(p, "rb").read()[salta:]
    return {2 * (i * 8 + k) for i, x in enumerate(b) if x for k in range(8) if x >> k & 1}


def stringa(imm, a):
    """il testo in a (almeno 4 caratteri stampabili e lo zero), o None"""
    if not 0 < a < len(imm) - 4:
        return None
    e = a
    while e < len(imm) and e - a < 120 and (32 <= imm[e] < 127 or imm[e] in (9, 10)):
        e += 1
    if e - a >= 4 and e < len(imm) and imm[e] == 0:
        return imm[a:e].decode("latin-1").replace("\n", " ").strip()
    return None


# parole -> argomento, per raggruppare le funzioni mai eseguite
ARGOMENTI = [
    ("errori", r"(?i)error|could not|couldn|can't|cannot|failed|corrupt|out of memory|not enough|unable|invalid|warning"),
    ("file e salvataggi", r"(?i)\.lha|save|load|file|dir|ram:|t:|progdir|#\?"),
    ("suoni e voci", r"(?i)sound|speech|narrator|sample|music|\.med|mod\."),
    ("grafica e schermi", r"(?i)screen|gfx|backdrop|palette|font|iff|ilbm|brush"),
    ("tutorial e script", r"(?i)tutorial|script|print|wait|pointto"),
    ("rete e partite registrate", r"(?i)record|replay|network|modem|serial"),
    ("debug e sviluppo", r"(?i)debug|test|developer|cheat|secret"),
]


def main():
    a = argparse.ArgumentParser()
    a.add_argument("-n", type=int, default=300, help="quante funzioni mai eseguite elencare")
    a.add_argument("-o", default=os.path.join(REPO, "build", "copertura_mappa.md"))
    arg = a.parse_args()
    imm = open(os.path.join(REPO, "build", "chaos_immagine.bin"), "rb").read()
    coperti = bitmap(os.path.join(REPO, "game", "copertura_base.bin"))
    gb = os.path.join(B, "copertura_giocatore.bin")
    if os.path.exists(gb):
        coperti |= bitmap(gb)

    funz = {}                                   # indirizzo -> dati
    gen = os.path.join(B, "generated")
    for f in sorted(os.listdir(gen)):
        if not f.endswith(".c"):
            continue
        cur = None
        for r in open(os.path.join(gen, f), encoding="latin-1"):
            m = re.match(r"void func_([0-9A-F]+)\(void\) \{", r)
            if m:
                cur = funz.setdefault(int(m.group(1), 16), {"siti": set(), "chiama": set(), "testi": [], "istr": 0})
                continue
            if cur is None:
                continue
            if r.startswith("}"):
                cur = None
                continue
            if "/* $" in r:
                cur["istr"] += 1
            for x in re.findall(r"recomp_push_return\(0x([0-9A-Fa-f]+)u\)", r):
                cur["siti"].add(int(x, 16))
            for x in re.findall(r"recomp_call_func\(func_([0-9A-F]+)\)", r):
                cur["chiama"].add(int(x, 16))
            for x in re.findall(r"A\[4\] \+ \(int32_t\)(-?\d+)\)", r):
                s = stringa(imm, A4 + int(x))
                if s and s not in cur["testi"]:
                    cur["testi"].append(s)
            for x in re.findall(r"\b0x([0-9A-Fa-f]{5,8})u\b", r):
                s = stringa(imm, int(x, 16))
                if s and s not in cur["testi"]:
                    cur["testi"].append(s)

    chiamanti = {}
    for k, v in funz.items():
        for c in v["chiama"]:
            chiamanti.setdefault(c, set()).add(k)

    def eseguita(k):
        return bool(funz[k]["siti"] & coperti)

    def argomento(v):
        t = " ".join(v["testi"])
        for nome, rx in ARGOMENTI:
            if re.search(rx, t):
                return nome
        return "altro" if t else "senza testi"

    mai = [k for k, v in funz.items() if v["siti"] and not eseguita(k)]
    parz = [k for k, v in funz.items() if v["siti"] and eseguita(k) and not v["siti"] <= coperti]
    siti_mai = sum(len(funz[k]["siti"]) for k in mai)
    siti_parz = sum(len(funz[k]["siti"] - coperti) for k in parz)
    # una funzione mai eseguita il cui chiamante e' stato eseguito: manca solo
    # la condizione (il ramo) giusta; con chiamanti mai eseguiti dipende da loro
    porte = [k for k in mai if any(c in funz and eseguita(c) for c in chiamanti.get(k, ()))]
    orfane = [k for k in mai if not chiamanti.get(k)]

    per_arg = {}
    for k in mai:
        g = argomento(funz[k])
        n, s = per_arg.get(g, (0, 0))
        per_arg[g] = (n + 1, s + len(funz[k]["siti"]))

    out = ["# Mappa dei punti mai eseguiti", "",
           "- punti coperti: %d; mancanti: %d in %d funzioni mai eseguite, %d nelle %d funzioni eseguite a meta'"
           % (len(coperti), siti_mai + siti_parz, len(mai), siti_parz, len(parz)),
           "- funzioni mai eseguite con un chiamante eseguito (manca la condizione giusta): %d (%d punti)"
           % (len(porte), sum(len(funz[k]["siti"]) for k in porte)),
           "- funzioni mai eseguite senza chiamate dirette (puntatori, tabelle, vettori): %d (%d punti)"
           % (len(orfane), sum(len(funz[k]["siti"]) for k in orfane)),
           "- programma: %d funzioni mai eseguite, librerie: %d"
           % (sum(k >= PROGRAMMA for k in mai), sum(k < PROGRAMMA for k in mai)),
           "", "## Per argomento (dai testi che usano)", "", "| argomento | funzioni | punti |", "|---|---|---|"]
    out += ["| %s | %d | %d |" % (g, n, s) for g, (n, s) in sorted(per_arg.items(), key=lambda x: -x[1][1])]
    out += ["", "## Le funzioni mai eseguite piu' grandi", "",
            "| funzione | punti | istr. | chiamata da (eseguite *) | argomento | testi |", "|---|---|---|---|---|---|"]
    for k in sorted(mai, key=lambda k: -len(funz[k]["siti"]))[:arg.n]:
        v = funz[k]
        ch = sorted(chiamanti.get(k, ()))
        chs = ", ".join("$%06X%s" % (c, "*" if c in funz and eseguita(c) else "") for c in ch[:4])
        if len(ch) > 4:
            chs += " +%d" % (len(ch) - 4)
        testi = " / ".join(t[:50] for t in v["testi"][:4]).replace("|", "/")
        out.append("| $%06X%s | %d | %d | %s | %s | %s |" % (k, "" if k >= PROGRAMMA else " (lib)", len(v["siti"]),
                                                       v["istr"], chs or "-", argomento(v), testi))
    open(arg.o, "w", encoding="utf-8").write("\n".join(out) + "\n")
    print("\n".join(out[:20]))
    print("mappa completa in", arg.o)


if __name__ == "__main__":
    main()
