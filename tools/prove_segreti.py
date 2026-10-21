"""prove_segreti.py - le parti del gioco che non si raggiungono giocando
normalmente (tools/mappa_copertura.py le ha trovate), senza finestra, con
la copertura unita dopo ogni scenario (tools/copertura.py):

  tempi      Alt+T: i "Timing Tests" degli sviluppatori (T:TotalChaosTimingTests.txt)
  tile       Gfx/CustomSpellGfx presente e gfx/Dat vecchi: il gioco rigenera
             le tile personalizzate (Gfx/Dat/Custom*) e le pergamene
  email      le chiavi delle partite via e-mail in Chaos.prefs (lette e riscritte)
  prefs_errate  ogni SI/NO di Chaos.prefs sbagliato: i 62 rami "ERROR: Bad ..."

    python tools/prove_segreti.py [scenari...]      (senza: tutti)

I dati sono una copia per scenario in build/prove_segreti/<scenario>.
"""
import datetime
import os
import shutil
import subprocess
import sys

QUI = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(QUI)
sys.path.insert(0, QUI)
import prove_cpu as pc  # noqa: E402


def gfx(dati):
    t = os.path.join(dati, "TotalChaos")
    return os.path.join(t, next(d for d in os.listdir(t) if d.lower() == "gfx"))


def prepara_tile(dati):
    g = gfx(dati)
    os.makedirs(os.path.join(g, "CustomSpellGfx"), exist_ok=True)
    dat = os.path.join(g, next(d for d in os.listdir(g) if d.lower() == "dat"))
    vecchio = datetime.datetime(2000, 1, 1).timestamp()
    for f in os.listdir(dat):
        p = os.path.join(dat, f)
        if f.lower().startswith("custom"):
            os.remove(p)                               # si rifanno
        else:
            os.utime(p, (vecchio, vecchio))


def cambia_prefs(dati, cambia):
    """Chaos.prefs con cambia(chiave, valore) -> nuovo valore (None: com'era)"""
    p = os.path.join(dati, "TotalChaos", "Chaos.prefs")
    righe = []
    for r in open(p, encoding="latin-1").read().splitlines():
        k, s, v = r.partition(":")
        n = cambia(k, v.strip()) if s else None
        righe.append(r if n is None else "%s:\t%s" % (k, n))
    open(p, "w", encoding="latin-1", newline="\n").write("\n".join(righe) + "\n")


def prepara_email(dati):
    """le partite via e-mail: il gioco legge e riscrive le chiavi (non spedisce)"""
    nuove = {"AutomaticallyMailCompletedTurns": "YES", "EmailServerType": "POP3",
             "EmailServerName": "mail.example.com", "Player2Alive": "YES", "Player2Human": "YES"}
    nuove.update({"Player%dEmail" % i: "giocatore%d@example.com" % i for i in range(1, 9)})
    cambia_prefs(dati, lambda k, v: nuove.get(k))


def prepara_prefs_errate(dati):
    """ogni SI/NO con un valore sbagliato: i rami "ERROR: Bad ... specification" """
    cambia_prefs(dati, lambda k, v: "FORSE" if v in ("YES", "NO", "SMTP", "POP3") else None)


SCENARI = {
    "tempi": (None, ["k64+14", "w40000", "f"]),
    "tile": (prepara_tile, ["w3000", "f"]),
    "email": (prepara_email, ["w3000", "f"]),
    "prefs_errate": (prepara_prefs_errate, ["w3000", "f"]),
}


def main():
    scelti = sys.argv[1:] or list(SCENARI)
    for nome in scelti:
        prepara, passi = SCENARI[nome]
        dati = os.path.join(REPO, "build", "prove_segreti", nome)
        if os.path.isdir(dati):
            shutil.rmtree(dati)
        pc.copia_dati(dati)
        if prepara:
            prepara(dati)
        env = dict(os.environ, TC_SEQUENZA_DATI=dati)
        print("==", nome)
        subprocess.run([sys.executable, "-I", os.path.join(QUI, "prova_sequenza.py")] + passi, env=env, check=True)
        subprocess.run([sys.executable, "-I", os.path.join(QUI, "copertura.py")], check=True,
                       stdout=subprocess.DEVNULL)
    subprocess.run([sys.executable, "-I", os.path.join(QUI, "copertura.py"), "-tieni"])


if __name__ == "__main__":
    main()
