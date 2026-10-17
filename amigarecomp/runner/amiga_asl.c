/* amiga_asl.c - asl.library (V40) in C, al posto di quella del Workbench.
 *
 * Quella vera vuole gadtools.library e BOOPSI (in ROM sul Kickstart 3.1).
 * Qui le richieste si allocano come asl (FileRequester V38: fr_File 4,
 * fr_Drawer 8, fr_NumArgs 32, fr_ArgList 36, fr_Pattern 52) con i valori
 * iniziali dei tag, e AslRequest apre la finestra Apri o Salva di Windows
 * nella cartella della richiesta (tradotta sul PC); il file scelto torna come
 * fr_Drawer (percorso Amiga, PC: fuori dai dati del gioco) e fr_File. Senza
 * finestra (prove) risponde AMIGA_ASL_FILE (un percorso Amiga), o annulla.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <commdlg.h>
#include "genesis_runtime.h"
#include "amiga_os.h"

#define ASL_TB                0x80080000u
#define ASLFR_TITLETEXT       (ASL_TB + 1)
#define ASLFR_INITIALFILE     (ASL_TB + 8)
#define ASLFR_INITIALDRAWER   (ASL_TB + 9)
#define ASLFR_INITIALPATTERN  (ASL_TB + 10)
#define ASLFR_FLAGS1          (ASL_TB + 20)
#define ASLFR_DOSAVEMODE      (ASL_TB + 44)
#define FRF_DOSAVEMODE        0x20
#define DIM_RICHIESTA         128
#define DIM_STRINGA           256
/* in fondo alla richiesta (spazio riservato): tipo, salvataggio, titolo */
#define R_TIPO   (DIM_RICHIESTA - 4)
#define R_SALVA  (DIM_RICHIESTA - 8)
#define R_TITOLO (DIM_RICHIESTA - 12)

void os_dos_percorso_host(const char *amiga, char *host, size_t n);     /* amiga_dos.c */
void os_dos_cartella_amiga(const char *host, char *amiga, size_t n);
void os_finestra_cattura(int si);                                     /* amiga_glue.c */
void *os_finestra_hwnd(void);

/* il valore di un tag in una lista semplice (TAG_MORE e TAG_SKIP compresi) */
static uint32_t tag(uint32_t lista, uint32_t t, uint32_t predefinito) {
    for (uint32_t p = lista; p;) {
        uint32_t k = R32(p);
        if (k == 0) break;
        if (k == 1) { p += 8; continue; }
        if (k == 2) { p = R32(p + 4); continue; }
        if (k == 3) { p += 8u * (R32(p + 4) + 1); continue; }
        if (k == t) return R32(p + 4);
        p += 8;
    }
    return predefinito;
}

static void copia_stringa(uint32_t dest, uint32_t src) {
    uint32_t i = 0;
    if (src) for (; i + 1 < DIM_STRINGA && R8(src + i); i++) W8(dest + i, R8(src + i));
    W8(dest + i, 0);
}
static void scrivi_stringa(uint32_t dest, const char *s) {
    uint32_t i = 0;
    for (; i + 1 < DIM_STRINGA && s[i]; i++) W8(dest + i, (uint8_t)s[i]);
    W8(dest + i, 0);
}

/* i tag di AllocAslRequest e di AslRequest cambiano la richiesta */
static void applica_tag(uint32_t r, uint32_t lista) {
    if (!lista) return;
    uint32_t file = R32(r + 4), cart = R32(r + 8), schema = R32(r + 52), v;
    if ((v = tag(lista, ASLFR_INITIALFILE, 0)) != 0) copia_stringa(file, v);
    if ((v = tag(lista, ASLFR_INITIALDRAWER, 0)) != 0) copia_stringa(cart, v);
    if ((v = tag(lista, ASLFR_INITIALPATTERN, 0)) != 0) copia_stringa(schema, v);
    if ((v = tag(lista, ASLFR_TITLETEXT, 0)) != 0) W32(r + R_TITOLO, v);
    uint32_t f1 = tag(lista, ASLFR_FLAGS1, 0xFFFFFFFFu);
    if (f1 != 0xFFFFFFFFu) W32(r + R_SALVA, (f1 & FRF_DOSAVEMODE) != 0);
    if ((v = tag(lista, ASLFR_DOSAVEMODE, 0xFFFFFFFFu)) != 0xFFFFFFFFu) W32(r + R_SALVA, v != 0);
}

static void a_allocaslrequest(void) {
    /* AllocAslRequest(tipo D0, tag A0): 0 file, 1 font, 2 modo video */
    uint32_t r = os_alloca(DIM_RICHIESTA + 3 * DIM_STRINGA, MEMF_PUBLIC | MEMF_CLEAR);
    if (!r) { D0 = 0; return; }
    W32(r + R_TIPO, D0);
    if (D0 == 0) {
        uint32_t file = r + DIM_RICHIESTA, cart = file + DIM_STRINGA, schema = cart + DIM_STRINGA;
        W32(r + 4, file); W32(r + 8, cart); W32(r + 52, schema);
        applica_tag(r, A0);
    }
    D0 = r;
}
static void a_freeaslrequest(void) {
    if (A0) os_libera(A0, DIM_RICHIESTA + 3 * DIM_STRINGA);
}
static void a_allocfilerequest(void) { D0 = 0; A0 = 0; a_allocaslrequest(); }

/* la scelta: percorso PC completo -> fr_Drawer (Amiga) e fr_File */
static void risposta(uint32_t r, const char *host) {
    char dir[1100], amiga[1100];
    snprintf(dir, sizeof dir, "%s", host);
    char *b = strrchr(dir, '\\'), *c = strrchr(dir, '/');
    if (c > b) b = c;
    char file[300];
    snprintf(file, sizeof file, "%s", b ? b + 1 : dir);
    if (b) *b = 0; else dir[0] = 0;
    os_dos_cartella_amiga(dir, amiga, sizeof amiga);
    scrivi_stringa(R32(r + 8), amiga);
    scrivi_stringa(R32(r + 4), file);
}

/* Il requester del gioco: Total Chaos tiene una ReqFileRequester di
 * req.library a $44F7CE (Title +2, Dir +6, File +10, PathName +14, Window
 * +18, Flags +30) e la passa allo stub $2C0CCE (pea struttura; jsr), che
 * chiama FileRequest: D0 != 0 = scelto. Le richieste asl (terreni, sfondi,
 * tutorial) passano di li', come Load/Save: stessa interfaccia, gia'
 * nella cartella che il gioco chiede. Prima e dopo la struttura si salva e
 * si rimette com'era */
#define TC_REQ       0x44F7CEu
#define TC_REQ_STUB  0x2C0CCEu
#define TC_REQ_DIM   0x300u
#define TC_A4        0x4C8F68u
#define TC_REQ_HIDE  120u                      /* char Hide[32]: schema dei file nascosti */
#define TC_REQ_SHOW  152u                      /* char Show[32]: schema dei file mostrati */
static int s_req_asl;                          /* la richiesta viene da AslRequest (terreni...) */

static void scrivi_schema(uint32_t a, const char *s) {
    uint32_t i = 0;
    for (; i < 31 && s[i]; i++) W8(a + i, (uint8_t)s[i]);
    for (; i < 32; i++) W8(a + i, 0);
}

/* a ogni quadro (amiga_glue.c): appena il gioco ha preparato il suo
 * requester, una volta sola, Show = "#?.lha" (Apri/Salva: solo le partite).
 * Dopo resta quello che il giocatore scrive (req.library lo ricopia) */
void os_asl_quadro(void) {
    static int fatto;
    if (fatto || R16(TC_REQ_STUB) != 0x206F || !R32(TC_REQ + 6)) return;
    if (!R8(TC_REQ + TC_REQ_SHOW)) scrivi_schema(TC_REQ + TC_REQ_SHOW, "#?.lha");
    fatto = 1;
}
static int req_del_gioco(uint32_t r, const char *titolo, const char *cart, const char *file, int salva) {
    if (R32(TC_REQ + 6) == 0 || R16(TC_REQ_STUB) != 0x206F) return -1;   /* struttura non pronta o non e' Total Chaos */
    static uint8_t copia[TC_REQ_DIM];
    memcpy(copia, g_amiga_mem + TC_REQ, TC_REQ_DIM);
    uint32_t buf = os_alloca(3 * DIM_STRINGA + 512, MEMF_PUBLIC | MEMF_CLEAR);
    if (!buf) return -1;
    uint32_t bt = buf, bd = buf + DIM_STRINGA, bf = buf + 2 * DIM_STRINGA, bp = buf + 3 * DIM_STRINGA;
    scrivi_stringa(bt, titolo); scrivi_stringa(bd, cart); scrivi_stringa(bf, file);
    W32(TC_REQ + 2, bt); W32(TC_REQ + 6, bd); W32(TC_REQ + 10, bf); W32(TC_REQ + 14, bp);
    W32(TC_REQ + 30, salva ? 0x8C4u : 0x44u);
    /* i filtri per cartella: i terreni senza gli elenchi che il gioco si
     * crea (#?List.txt), i tutorial solo archivi, le texture del fondo
     * (Gfx/BottomTextures) solo i file con BT nel nome (Battle Terrain) */
    scrivi_schema(TC_REQ + TC_REQ_HIDE, strstr(cart, "Terrain") || strstr(cart, "terrain") ? "#?List.txt" : "");
    scrivi_schema(TC_REQ + TC_REQ_SHOW, strstr(cart, "Tutorial") ? "#?.lha" :
                                        strstr(cart, "BottomTextures") ? "#?BT#?" : "");
    M68KState s = g_cpu;
    g_cpu.A[4] = TC_A4;
    g_cpu.A[7] -= 4;
    W32(g_cpu.A[7], TC_REQ);
    s_req_asl = 1;                                 /* non e' Apri/Salva: non conta per "Salva" */
    os_chiama_68k(TC_REQ_STUB);
    s_req_asl = 0;
    uint32_t esito = g_cpu.D[0];
    g_cpu = s;
    if (esito) {
        char d[DIM_STRINGA], f[DIM_STRINGA];
        os_leggi_stringa(bd, d, sizeof d);
        os_leggi_stringa(bf, f, sizeof f);
        scrivi_stringa(R32(r + 8), d);
        scrivi_stringa(R32(r + 4), f);
    }
    memcpy(g_amiga_mem + TC_REQ, copia, TC_REQ_DIM);
    os_libera(buf, 3 * DIM_STRINGA + 512);
    return esito != 0;
}

/* ReqFileRequester (req.library, LVO -84) del gioco: qui passa ogni richiesta
 * di file del gioco (Apri, Salva, Salva come, terreni...).
 * "Salva" (Progetto, quarta voce) e "Salva come" chiamano la stessa routine
 * con la stessa finestra: qui "Salva" diventa quello dei programmi, senza
 * finestra, sull'ultimo file salvato o aperto; se non ce n'e' ancora uno non
 * fa niente. Si riconosce dall'ultima voce di menu scelta (per posizione:
 * vale in tutte le lingue) */
static char s_ultima_cart[DIM_STRINGA], s_ultimo_file[DIM_STRINGA];

static void r_filerequester(void) {
    extern uint16_t os_intuition_ultimo_menupick;
    uint32_t r = A0;
    uint16_t mp = os_intuition_ultimo_menupick;          /* menu | voce << 5 | sottovoce << 11 */
    int salva_semplice = !s_req_asl && (R32(r + 30) & 0x800) && (mp & 0x1F) == 0 && ((mp >> 5) & 0x3F) == 3;
    os_intuition_ultimo_menupick = 0xFFFF;     /* vale una volta sola */
    if (salva_semplice) {
        if (!s_ultimo_file[0]) { D0 = 0; return; }          /* niente da cui salvare: come annullato */
        char p[2 * DIM_STRINGA + 2];
        size_t l = strlen(s_ultima_cart);
        snprintf(p, sizeof p, "%s%s%s", s_ultima_cart, l && s_ultima_cart[l - 1] != ':' && s_ultima_cart[l - 1] != '/' ? "/" : "",
                 s_ultimo_file);
        if (R32(r + 6)) scrivi_stringa(R32(r + 6), s_ultima_cart);
        if (R32(r + 10)) scrivi_stringa(R32(r + 10), s_ultimo_file);
        if (R32(r + 14)) scrivi_stringa(R32(r + 14), p);
        fprintf(stderr, "[req] Salva senza finestra su \"%s\"\n", p);
        D0 = 1;
        return;
    }
    uint32_t v = os_base("req.library") - 84;
    if (R16(v) == 0x4EF9) call_by_address(R32(v + 2));
    /* Apri, Salva, Salva come riusciti: il file per il prossimo "Salva" */
    if (D0 && !s_req_asl && R32(r + 10)) {
        os_leggi_stringa(R32(r + 6), s_ultima_cart, sizeof s_ultima_cart);
        os_leggi_stringa(R32(r + 10), s_ultimo_file, sizeof s_ultimo_file);
    }
}
void os_req_aggancia(void) { os_sostituisci("req.library", 84, r_filerequester); }

static void a_aslrequest(void) {
    /* AslRequest(richiesta A0, tag A1): 1 = scelto, 0 = annullato */
    uint32_t r = A0;
    if (!r || R32(r + R_TIPO) != 0) { D0 = 0; return; }
    applica_tag(r, A1);
    char cart[DIM_STRINGA], file[DIM_STRINGA], titolo[128] = "Total Chaos";
    os_leggi_stringa(R32(r + 8), cart, sizeof cart);
    os_leggi_stringa(R32(r + 4), file, sizeof file);
    if (R32(r + R_TITOLO)) os_leggi_stringa(R32(r + R_TITOLO), titolo, sizeof titolo);
    int salva = R32(r + R_SALVA) != 0;
    if (!getenv("AMIGA_ASL_FILE")) {
        int k = req_del_gioco(r, titolo, cart, file, salva);
        if (os_traccia) fprintf(stderr, "[asl] AslRequest \"%s\" cartella \"%s\": requester del gioco -> %d\n", titolo, cart, k);
        if (k >= 0) { D0 = (uint32_t)k; return; }
    }
    if (getenv("AMIGA_QUADRI")) {
        /* senza finestra: AMIGA_ASL_FILE (un percorso Amiga) o annullato */
        const char *prova = getenv("AMIGA_ASL_FILE");
        fprintf(stderr, "[asl] AslRequest \"%s\" (%s) cartella \"%s\" file \"%s\" -> %s\n", titolo,
                salva ? "salva" : "apri", cart, file, prova ? prova : "annullato");
        if (!prova) { D0 = 0; return; }
        char h[1100];
        os_dos_percorso_host(prova, h, sizeof h);
        risposta(r, h);
        D0 = 1;
        return;
    }
    char hdir[1100], scelto[MAX_PATH * 4] = "";
    os_dos_percorso_host(cart, hdir, sizeof hdir);
    for (char *p = hdir; *p; p++) if (*p == '/') *p = '\\';
    snprintf(scelto, sizeof scelto, "%s", file);
    OPENFILENAMEA o;
    memset(&o, 0, sizeof o);
    o.lStructSize = sizeof o;
    o.hwndOwner = (HWND)os_finestra_hwnd();
    /* il filtro: lo schema della richiesta ("#?.lha" -> "*.lha"), se c'e';
     * terreni, sfondi e tutorial non hanno estensione: tutti i file */
    char schema[DIM_STRINGA], filtro[600];
    os_leggi_stringa(R32(r + 52), schema, sizeof schema);
    char *pt = strstr(schema, "#?");
    size_t lf;
    if (schema[0] && pt) {
        char w[DIM_STRINGA];
        snprintf(w, sizeof w, "%.*s*%s", (int)(pt - schema), schema, pt + 2);
        lf = (size_t)snprintf(filtro, sizeof filtro, "%s", w) + 1;
        lf += (size_t)snprintf(filtro + lf, sizeof filtro - lf, "%s", w) + 1;
    } else if (salva || strstr(titolo, "ave") || strstr(titolo, "alva") || strstr(file, ".lha")) {
        lf = (size_t)snprintf(filtro, sizeof filtro, "Partite salvate (*.lha)") + 1;
        lf += (size_t)snprintf(filtro + lf, sizeof filtro - lf, "*.lha") + 1;
    } else lf = 0;
    lf += (size_t)snprintf(filtro + lf, sizeof filtro - lf, "Tutti i file") + 1;
    lf += (size_t)snprintf(filtro + lf, sizeof filtro - lf, "*.*") + 1;
    filtro[lf] = 0;
    o.lpstrFilter = filtro;
    o.lpstrFile = scelto;
    o.nMaxFile = sizeof scelto;
    o.lpstrInitialDir = hdir;
    o.lpstrTitle = titolo;
    o.Flags = OFN_NOCHANGEDIR | OFN_HIDEREADONLY | (salva ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    os_finestra_cattura(0);                      /* il mouse di Windows per la finestra */
    BOOL ok = salva ? GetSaveFileNameA(&o) : GetOpenFileNameA(&o);
    os_finestra_cattura(1);
    if (!ok) { D0 = 0; return; }
    risposta(r, scelto);
    D0 = 1;
}

#define F(lvo, fn) [(lvo) / 6 - 1] = fn
#define N(lvo, nome) [(lvo) / 6 - 1] = nome
static const os_funzione s_fn[12] = {
    F(30, a_allocfilerequest), F(36, a_freeaslrequest), F(42, a_aslrequest),
    F(48, a_allocaslrequest), F(54, a_freeaslrequest), F(60, a_aslrequest),
};
static const char *const s_nomi[12] = {
    N(6, "Open"), N(12, "Close"), N(18, "Expunge"), N(24, "Reserved"),
    N(30, "AllocFileRequest"), N(36, "FreeFileRequest"), N(42, "RequestFile"),
    N(48, "AllocAslRequest"), N(54, "FreeAslRequest"), N(60, "AslRequest"),
    N(66, "AbortAslRequest"), N(72, "ActivateAslRequest"),
};

void os_asl_init(void) {
    static const os_libreria_c def = { "asl.library", 40, 12, 34, s_nomi, s_fn };
    os_crea_libreria(&def, NT_LIBRARY);
}
