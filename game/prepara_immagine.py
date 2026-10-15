"""prepara_immagine.py - l'immagine di Total Chaos AGA 7.11 per il ricompilatore.

Carica come farebbe LoadSeg l'eseguibile del gioco (TotalChaos/Chaos, gia'
con le nostre patch: niente limite di turni, Load riattivato, italiano), le
librerie che porta con se' in libs/ (chunkyfonts, iff, req, z) e quelle del
Workbench che usa (DH0:Libs, DH0:Devs/narrator.device): codice 68000 che
viene ricompilato col gioco e inizializzato dal runner quando il gioco le
apre (OpenLibrary). Da CARICA prima le librerie, poi il programma: il codice
deve stare sotto $400000. I hunk CHIP del programma (sprite, buffer audio)
vanno a CHIP, in memoria chip. Scrive:
  build/chaos_immagine.bin   la memoria da $000000 alla fine dell'ultimo
                             segmento: $0 pila, $4 ingresso (per il
                             ricompilatore; a runtime $4 torna ExecBase)
  build/chaos_semi.toml      [functions].extra: le destinazioni delle
                             rilocazioni che cadono nel codice, i vettori e
                             le routine di init delle librerie
  build/chaos_segmenti.h     dove stanno programma e librerie, per il runner

Uso: python game/prepara_immagine.py <cartella del gioco> [--uscita build]
     (la cartella e' quella con Chaos e libs/: dati_gioco/DH0/TotalChaos)
"""
import argparse
import os
import struct
import sys
import zlib

QUI = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, QUI)
from hunk_amiga import carica  # noqa: E402

CARICA = 0x200000          # da qui librerie e programma, di seguito
PILA = 0x1FF000            # cima della pila iniziale (sotto il programma)
CHIP = 0x001000            # i hunk CHIP del programma, in fondo alla chip RAM
PROGRAMMA = "Chaos"
# le librerie del gioco (TotalChaos/libs) e quelle del Workbench che usa
# (DH0:Libs): codice 68000 ricompilato col gioco, come in Chaos 2.5d. asl.library
# resta nell'immagine (toglierla sposterebbe il programma) ma non si usa: vuole
# gadtools e BOOPSI, il runner ha la sua in C (amiga_asl.c), registrata prima
LIBRERIE = ["libs/chunkyfonts.library", "libs/iff.library", "libs/req.library", "libs/z.library",
            "../Libs/diskfont.library", "../Libs/translator.library", "../Libs/locale.library",
            "../Libs/iffparse.library", "../Libs/asl.library",
            "../Libs/mathieeedoubbas.library", "../Libs/mathieeedoubtrans.library",
            "../Libs/mathieeesingtrans.library", "../Libs/mathtrans.library"]
# il narrator.device del Workbench (lo stesso 33.2 di Chaos 2.5d)
DEVICE = ["../Devs/narrator.device"]
RTF_AUTOINIT = 0x80
CORREZIONI = []



def l32(m, a):
    return struct.unpack_from(">I", m, a)[0]


def romtag(imm, seg):
    """la RomTag della libreria: $4AFC seguito dal puntatore a se stessa
    (None se non c'e': certe librerie vecchie partono con la routine d'init)"""
    for a, t, k in zip(seg.inizio, seg.taglie, seg.tipi):
        if k != "CODE":
            continue
        for p in range(a, a + t - 26, 2):
            if imm[p:p + 2] == b"\x4a\xfc" and l32(imm, p + 2) == p:
                return p
    return None


def tabella_makelibrary(imm, seg):
    """la tabella dei vettori e la routine d'init passate a MakeLibrary
    (jsr -$54(a6)) da una routine d'init: gli ultimi lea X(pc)/X.l in a0 e
    in a2 prima della chiamata. Restituisce (tabella, init) o (None, None)"""
    for a, t, k in zip(seg.inizio, seg.taglie, seg.tipi):
        if k != "CODE":
            continue
        tab = init = None
        for p in range(a, a + t - 4, 2):
            w = imm[p:p + 2]
            if w in (b"\x41\xfa", b"\x45\xfa"):                # lea d16(pc),a0 / a2
                v = p + 2 + struct.unpack_from(">h", imm, p + 2)[0]
            elif w in (b"\x41\xf9", b"\x45\xf9"):              # lea abs.l,a0 / a2
                v = l32(imm, p + 2)
            elif imm[p:p + 4] == b"\x4e\xae\xff\xac" and tab:  # jsr -$54(a6)
                return tab, init
            else:
                continue
            if w[1] in (0xfa, 0xf9) and w[0] == 0x41:
                tab = v
            else:
                init = v
    return None, None


def vettori(imm, tab):
    """la tabella dei vettori di MakeLibrary: assoluti, o relativi se comincia
    con $FFFF; finisce con -1"""
    v = []
    if imm[tab:tab + 2] == b"\xff\xff":
        p = tab + 2
        while True:
            o = struct.unpack_from(">h", imm, p)[0]
            if o == -1:
                break
            v.append(tab + o)
            p += 2
    else:
        p = tab
        while l32(imm, p) != 0xFFFFFFFF:
            v.append(l32(imm, p))
            p += 4
    return v


def puntatori_pc(imm, codice):
    """le funzioni passate per indirizzo senza rilocazione: lea X(pc),An e
    pea X(pc) che puntano nel codice. Restano fuori le destinazioni che non
    possono essere l'inizio di una funzione: testi, zeri, opcode del gruppo
    0 (ori/btst/bchg...) o A/F-line, e quelle senza un RTS vicino"""
    def testo(t):
        n = 0
        while n < 8 and 32 <= imm[t + n] < 127:
            n += 1
        return (n >= 2 and imm[t + n] == 0) or n >= 4
    semi = set()
    for x, y in codice:
        for p in range(x, y - 4, 2):
            w = struct.unpack_from(">H", imm, p)[0]
            if (w & 0xF1FF) != 0x41FA and w != 0x487A:
                continue
            t = p + 2 + struct.unpack_from(">h", imm, p + 2)[0]
            if t % 2 or not any(a <= t < b for a, b in codice):
                continue
            op = struct.unpack_from(">H", imm, t)[0]
            # un prologo tipico basta (i corpi dei task non tornano mai):
            # movea.l $4.w,a6 / movem.l ...,-(a7) / link / move.l Dn,-(a7) /
            # spazio in pila: subq.w #n,a7 / suba.w #n,a7 / lea -n(a7),a7
            prologo = (op in (0x2C78, 0x48E7, 0x9EFC, 0x4FEF) or (op & 0xFFF8) == 0x4E50
                       or (op & 0xFFF8) == 0x2F00 or (op & 0xF1FF) == 0x514F)
            if not prologo:
                if op >> 12 in (0x0, 0xA, 0xF) or testo(t):
                    continue
                if not any(imm[q:q + 2] == b"\x4e\x75" for q in range(t, min(t + 512, y), 2)):
                    continue
            semi.add(t)
    return semi


def e_prologo(op):
    """il primo opcode tipico di una funzione compilata: movem.l ...,-(a7) /
    link / move.l Rn,-(a7) / movea.l $4.w,a6 / spazio in pila (subq.w #n,a7,
    suba.w #n,a7, lea -n(a7),a7). link An,#d e' $4E50-$4E57"""
    return (op in (0x2C78, 0x48E7, 0x9EFC, 0x4FEF) or (op & 0xFFF8) == 0x4E50
            or (op & 0xFFF0) == 0x2F00 or (op & 0xF1FF) == 0x514F)


def inizio_senza_prologo(op):
    """il primo opcode di una funzione SAS/C che non salva registri: pea/lea
    d16(pc), lea/movea.l/move.l/tst/clr assoluti, moveq, jsr assoluto, bsr.w,
    move.l #imm,-(a7)"""
    return (op in (0x487A, 0x4EB9, 0x6100, 0x2F3C, 0x2039, 0x4AB9, 0x4A79, 0x4A39,
                   0x42B9, 0x4279, 0x4239, 0x23FC, 0x33FC, 0x13FC)
            or (op & 0xF1FF) in (0x41F9, 0x41FA, 0x2079)
            or (op & 0xF100) == 0x7000)


def sembra_codice(imm, q, y):
    """scarta testo e tabelle: niente 4 byte stampabili in fila all'inizio e
    un RTS entro 256 byte"""
    if all(0x20 <= b < 0x7F for b in imm[q:q + 4]):
        return False
    for r in range(q + 2, min(q + 256, y - 2), 2):
        if imm[r:r + 2] == b"\x4e\x75":
            return True
    return False


def dopo_rts(imm, codice):
    """le funzioni chiamate solo per puntatore calcolato: il codice compilato
    mette le funzioni una dopo l'altra, quindi ogni prologo subito dopo un RTS
    (saltando i nop di allineamento) e' l'inizio di una funzione; anche quelle
    senza prologo, se l'inizio e' tipico e quel che segue sembra codice"""
    semi = set()
    for x, y in codice:
        for p in range(x, y - 4, 2):
            if imm[p:p + 2] != b"\x4e\x75":
                continue
            q = p + 2
            while q < y - 2 and imm[q:q + 2] == b"\x4e\x71":
                q += 2
            if q >= y - 2:
                continue
            op = struct.unpack_from(">H", imm, q)[0]
            if e_prologo(op) or (inizio_senza_prologo(op) and sembra_codice(imm, q, y)):
                semi.add(q)
    return semi


def tabelle_jmp(imm, codice):
    """le tabelle di salti fatte di jmp d16(pc) uno dopo l'altro (le usano le
    routine assembler, per esempio i player di musica): le destinazioni sono
    funzioni chiamate per indirizzo calcolato. Le voci vuote sono "rts; nop"
    o "moveq #n,d0; rts" (i player di Total Chaos a $2BC3E4 e $2BE0B0): una
    tabella e' una fila di almeno tre voci da 4 byte con almeno due jmp, e
    sono ingressi anche le voci e quella subito dopo (l'ultima voce puo'
    essere una routine intera)"""
    def voce(p):
        if imm[p:p + 2] == b"\x4e\xfa":
            return "jmp"
        if imm[p:p + 4] == b"\x4e\x75\x4e\x71":
            return "rts"
        if imm[p] == 0x70 and imm[p + 2:p + 4] == b"\x4e\x75":
            return "moveq"
        return None
    semi = set()
    for x, y in codice:
        p = x
        while p < y - 8:
            q, jmp = p, 0
            while q < y - 4 and voce(q):
                jmp += voce(q) == "jmp"
                q += 4
            if (q - p) // 4 >= 3 and jmp >= 2:
                semi.add(q)
                for r in range(p, q, 4):
                    semi.add(r)
                    if voce(r) == "jmp":
                        t = r + 2 + struct.unpack_from(">h", imm, r + 2)[0]
                        if any(a <= t < b for a, b in codice) and t % 2 == 0:
                            semi.add(t)
                p = q
            else:
                p += 2
    return semi


def switch_sasc(imm, codice):
    """gli switch di SAS/C: move.w (d8,pc,Xn.w),Dn ; jmp (d8,pc,Xn.w) e
    subito dopo la tabella di offset a 16 bit (z.library a $2096EC). Il
    ricompilatore non sa quante voci ha: si leggono fino a dove comincia la
    prima destinazione (il codice dei casi sta dopo la tabella)"""
    semi = set()
    for x, y in codice:
        for p in range(x, y - 8, 2):
            w = struct.unpack_from(">H", imm, p)[0]
            if (w & 0xF1FF) != 0x303B or struct.unpack_from(">H", imm, p + 4)[0] != 0x4EFB:
                continue
            tab = p + 2 + struct.unpack_from(">b", imm, p + 3)[0]
            base = p + 6 + struct.unpack_from(">b", imm, p + 7)[0]
            if not (x <= tab < y) or tab < p + 8:
                continue
            fine = y
            q = tab
            while q < fine and q < tab + 2 * 512:
                t = base + struct.unpack_from(">h", imm, q)[0]
                if not (x <= t < y) or t % 2:
                    break
                semi.add(t)
                fine = min(fine, t)
                q += 2
    return semi


def semi_device(imm, seg, tabelle=True):
    """i punti d'ingresso di un device in assembler che il ricompilatore non
    trova seguendo le chiamate: le destinazioni delle tabelle di puntatori
    (almeno due rilocazioni di fila a 4 byte, come la tabella dei comandi) e
    i lea/pea abs.l che puntano a una funzione (l'ingresso del suo task,
    passato ad AddTask in A2). Restano fuori testi e tabelle di dati"""
    codice = [(x, x + t) for x, t, k in zip(seg.inizio, seg.taglie, seg.tipi) if k == "CODE"]
    pos = {p for p, _ in seg.riloc}
    semi = set()
    for p, v in seg.riloc:
        if v % 2 or not any(x <= v < y for x, y in codice):
            continue
        op = struct.unpack_from(">H", imm, v)[0]
        tabella = tabelle and (p - 4 in pos or p + 4 in pos) and imm[p - 2:p] not in (b"\x4e\xb9", b"\x4e\xf9")
        lea = (struct.unpack_from(">H", imm, p - 2)[0] & 0xF1FF) == 0x41F9 or imm[p - 2:p] == b"\x48\x79"
        # una routine passata per indirizzo che non comincia con un prologo
        # (la putChProc di RawDoFmt in req.library): subito dopo un RTS
        dopo_rts = imm[v - 2:v] == b"\x4e\x75" or (imm[v - 2:v] == b"\x4e\x71" and imm[v - 4:v - 2] == b"\x4e\x75")
        # ma non tabelle (cominciano con un byte zero) ne' testi ("%2d\0")
        n_testo = 0
        while n_testo < 8 and 32 <= imm[v + n_testo] < 127:
            n_testo += 1
        if imm[v] == 0 or op >> 12 in (0xA, 0xF) or (n_testo >= 2 and imm[v + n_testo] == 0):
            dopo_rts = False
        if (tabella and op >> 12 not in (0x0, 0xA, 0xF)) or (lea and (e_prologo(op) or dopo_rts)):
            semi.add(v)
    # le funzioni compilate chiamate solo per puntatore (z.library: inflate,
    # $20BC36): un prologo vero subito dopo un RTS (senza le euristiche piu'
    # larghe di dopo_rts, che nelle librerie prenderebbero dati)
    def fine_salto(p):
        """dove finisce un'istruzione che non prosegue, se comincia a p: rts,
        bra.b, bra.w, jmp abs.l (la funzione dopo non e' raggiunta cadendo)"""
        w = struct.unpack_from(">H", imm, p)[0]
        if w == 0x4E75:
            return p + 2
        if w == 0x6000:
            return p + 4
        if (w >> 8) == 0x60 and (w & 0xFF) not in (0, 0xFF):
            return p + 2
        if w == 0x4EF9:
            return p + 6
        return None
    for x, y in codice:
        for p in range(x, y - 6, 2):
            q = fine_salto(p)
            if q is None:
                continue
            while q < y - 2 and imm[q:q + 2] == b"\x4e\x71":
                q += 2
            # niente sembra_codice: le funzioni lunghe (inflate) non hanno un
            # RTS entro 256 byte; basta che l'inizio non sia testo
            if (q < y - 2 and e_prologo(struct.unpack_from(">H", imm, q)[0])
                    and not all(0x20 <= b < 0x7F for b in imm[q:q + 4])):
                semi.add(q)
    semi |= switch_sasc(imm, codice)
    # l'init delle librerie Commodore (iffparse, asl): bsr a una routine che
    # apre una libreria e torna togliendo a mano il proprio ritorno
    # ("movea.l (a7)+,a0 ; tst.l d0 ; beq ... ; jmp (a0)"): i punti di ritorno
    # di quei bsr sono ingressi
    for x, y in codice:
        for p in range(x, y - 4, 2):
            w = struct.unpack_from(">H", imm, p)[0]
            if w == 0x6100:
                t, lung = p + 2 + struct.unpack_from(">h", imm, p + 2)[0], 4
            elif (w >> 8) == 0x61 and (w & 0xFF) not in (0, 0xFF):
                t, lung = p + 2 + struct.unpack_from(">b", imm, p + 1)[0], 2
            else:
                continue
            if x <= t < y - 16 and b"\x20\x5f\x4a\x80\x67" in imm[t:t + 16] and b"\x4e\xd0" in imm[t:t + 20]:
                semi.add(p + lung)
    # i lea d16(pc),An verso una routine (z.library: le funzioni di
    # allocazione passate per puntatore, $20E7E6): stessa cautela, la
    # destinazione comincia subito dopo un RTS (o RTS + NOP)
    for x, y in codice:
        for p in range(x, y - 4, 2):
            if (struct.unpack_from(">H", imm, p)[0] & 0xF1FF) != 0x41FA:
                continue
            v = p + 2 + struct.unpack_from(">h", imm, p + 2)[0]
            if v % 2 or not (x <= v < y) or v == p + 2:
                continue
            op = struct.unpack_from(">H", imm, v)[0]
            dopo_rts = imm[v - 2:v] == b"\x4e\x75" or (imm[v - 2:v] == b"\x4e\x71" and imm[v - 4:v - 2] == b"\x4e\x75")
            if dopo_rts and imm[v] != 0 and op >> 12 not in (0xA, 0xF) and not (32 <= imm[v] < 127 and 32 <= imm[v + 1] < 127):
                semi.add(v)
    return semi


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("data", help="la cartella del gioco (con Chaos e libs/)")
    ap.add_argument("--uscita", default=os.path.join(QUI, "..", "build"))
    arg = ap.parse_args()
    os.makedirs(arg.uscita, exist_ok=True)

    pezzi = []                                  # (nome, segmenti)
    a = CARICA
    altri = [(os.path.basename(n), os.path.join(arg.data, n)) for n in LIBRERIE + DEVICE]
    for nome, file in altri:
        if not os.path.exists(file):
            raise FileNotFoundError(file)
    # prima le librerie, poi il programma: il ricompilatore (profilo genesis di
    # m68k-recomp-core) considera codice solo sotto $400000, e il programma
    # con il suo BSS da 1,5 MB arriva oltre; cosi' tutto il codice sta sotto
    for nome, file in altri + [(PROGRAMMA, os.path.join(arg.data, PROGRAMMA))]:
        # i hunk CHIP del programma (sprite, buffer audio) in memoria chip
        seg = carica(open(file, "rb").read(), a, CHIP if nome == PROGRAMMA else None)
        pezzi.append((nome, seg))
        a = (seg.fine + 0xFFF) & ~0xFFF
    pezzi.insert(0, pezzi.pop())                # il programma davanti, come prima
    fine = max(seg.fine for _, seg in pezzi)
    prog = pezzi[0][1]
    for nome, seg in pezzi:
        for x, t, k in zip(seg.inizio, seg.taglie, seg.tipi):
            if k == "CODE" and x + t > 0x400000:
                raise ValueError(f"{nome}: codice oltre $400000 (${x + t:06X}), il ricompilatore non lo vede")
    fine_chip = (prog.fine_chip + 0xFFF) & ~0xFFF
    assert fine_chip <= PILA - 0x20000, "hunk chip troppo grandi"
    imm = bytearray(fine)
    for _, seg in pezzi:
        imm[seg.base:seg.fine] = seg.mem
    imm[prog.base_chip:prog.fine_chip] = prog.mem_chip
    print(f"{PROGRAMMA}: hunk chip " + ", ".join(f"{i} a ${x:06X}" for i, x in enumerate(prog.inizio)
                                                if prog.chip[i]) + f"; memoria chip libera da ${fine_chip:06X}")
    struct.pack_into(">II", imm, 0, PILA, pezzi[0][1].inizio[0])
    for a, prima, dopo in CORREZIONI:
        if imm[a:a + len(prima)] != prima:
            raise ValueError(f"correzione a ${a:06X}: trovati {imm[a:a + len(prima)].hex()}, attesi {prima.hex()}")
        imm[a:a + len(dopo)] = dopo

    semi = set()
    librerie = []
    for nome, seg in pezzi:
        if nome == PROGRAMMA:
            # i puntatori a funzione del programma: le rilocazioni nel codice.
            # Una funzione non comincia con $0000 (ori.b #0,d0): quelle
            # destinazioni sono tabelle di puntatori dentro il hunk di codice
            codice = [(x, x + t) for x, t, k in zip(seg.inizio, seg.taglie, seg.tipi) if k == "CODE"]
            semi |= {v for _, v in seg.riloc
                     if any(x <= v < y for x, y in codice) and v % 2 == 0 and imm[v:v + 2] != b"\0\0"}
            semi |= puntatori_pc(imm, codice)
            semi |= tabelle_jmp(imm, codice)
            semi |= dopo_rts(imm, codice)
            semi |= switch_sasc(imm, codice)
            continue
        # le librerie: solo vettori e init (nel loro codice ci sono anche dati
        # con rilocazioni, immagini e testi, che non vanno decodificati)
        rt = romtag(imm, seg)
        flag, versione = (imm[rt + 10], imm[rt + 11]) if rt else (0, 0)
        init = l32(imm, rt + 22) if rt else seg.inizio[0]
        if flag & RTF_AUTOINIT:
            dati, tab, struttura, routine = (l32(imm, init + 4 * i) for i in range(4))
            vv = vettori(imm, tab)
            print(f"{nome}: v{versione}, {len(vv)} vettori, base di {dati} byte, init ${routine:06X}")
        else:
            # la routine d'init chiama MakeLibrary da sola e restituisce la base
            # (rt_Init, o l'inizio del codice se non c'e' RomTag)
            dati, struttura, routine = 0, 0, init
            tab, init_ml = tabella_makelibrary(imm, seg)
            if tab is None:
                raise ValueError(f"{nome}: tabella dei vettori di MakeLibrary non trovata")
            vv = vettori(imm, tab)
            if init_ml:                         # la routine che MakeLibrary chiama
                semi.add(init_ml)
            print(f"{nome}: v{versione}, {len(vv)} vettori, init senza RTF_AUTOINIT a ${routine:06X}")
        semi |= set(vv)
        if routine:
            semi.add(routine)
        # i device: tabelle di puntatori e lea; le librerie solo i lea (nel loro
        # codice ci sono tabelle di dati con rilocazioni)
        sd = semi_device(imm, seg, tabelle=nome.endswith(".device")) - set(vv)
        if sd:
            print(f"{nome}: {len(sd)} punti d'ingresso da tabelle e lea: " + " ".join(f"${a:06X}" for a in sorted(sd)))
        semi |= sd
        librerie.append((nome, seg, versione, dati, tab, struttura, routine, 0))

    with open(os.path.join(arg.uscita, "chaos_immagine.bin"), "wb") as f:
        f.write(imm)
    with open(os.path.join(arg.uscita, "chaos_semi.toml"), "w") as f:
        f.write("# generato da game/prepara_immagine.py\n[functions]\nextra = [\n")
        for v in sorted(semi):
            f.write(f"  0x{v:06X},\n")
        f.write("]\n")

    prog = pezzi[0][1]
    with open(os.path.join(arg.uscita, "chaos_segmenti.h"), "w") as f:
        f.write("/* generato da game/prepara_immagine.py */\n#pragma once\n")
        f.write(f"#define CHAOS_CARICA       0x{prog.base:06X}u   /* primo segmento del programma */\n")
        f.write(f"#define CHAOS_FINE         0x{fine:06X}u   /* fine dell'immagine */\n")
        f.write(f"#define CHAOS_FINE_CHIP    0x{fine_chip:06X}u   /* dopo i hunk chip: AllocMem(CHIP) da qui */\n")
        f.write(f"#define CHAOS_PILA         0x{PILA:06X}u\n")
        f.write(f"#define CHAOS_INGRESSO     0x{prog.inizio[0]:06X}u\n")
        f.write(f"#define CHAOS_IMMAGINE_CRC 0x{zlib.crc32(imm):08X}u\n")
        f.write("/* le librerie del gioco (libs/), gia' in memoria: OpenLibrary le inizializza */\n")
        f.write("static const struct { const char *nome; unsigned versione, seglist, dati, vettori, "
                "struttura, init; } chaos_librerie[] = {\n")
        for nome, seg, ver, dati, tab, strutt, routine, _ in librerie:
            f.write(f"    {{ \"{nome}\", {ver}, 0x{seg.base:06X}u, {dati}, 0x{tab:06X}u, "
                    f"0x{strutt:06X}u, 0x{routine:06X}u }},\n")
        f.write("};\n")
    print(f"immagine ${fine:06X} byte, {len(semi)} semi")


if __name__ == "__main__":
    main()
