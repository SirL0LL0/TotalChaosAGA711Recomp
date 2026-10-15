/* amiga_iffparse.c - iffparse.library (V40) in C, quanto ne usa il gioco.
 *
 * In Total Chaos la chiama solo chunkyfonts.library (del gioco) per leggere i
 * suoi font a colori: AllocIFF, InitIFFasDOS, OpenIFF, StopChunks, ParseIFF
 * (IFFPARSE_SCAN), CurrentChunk, ReadChunkBytes, CloseIFF, FreeIFF. Prima era
 * la libreria del Workbench 3.1 ricompilata insieme al gioco: questa non ha
 * bisogno di file di sistema.
 *
 * All'OpenIFF si legge tutto il file (dalla posizione attuale) in C e lo si
 * scorre da li': FORM/LIST/CAT/PROP annidati, pezzi pari, una pila di
 * contesti. Ogni contesto ha in memoria emulata la sua ContextNode
 * (cn_ID, cn_Type, cn_Size, cn_Scan), che CurrentChunk restituisce.
 */
#include "amiga_os.h"
#include "amiga_debug.h"
#include "genesis_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* struct IFFHandle (parte pubblica) e ContextNode */
enum { IFF_STREAM = 0, IFF_FLAGS = 4, IFF_DEPTH = 8, IFF_SIZE = 64 };
enum { CN_ID = 8, CN_TYPE = 12, CN_SIZE = 16, CN_SCAN = 20, CN_DIM = 32 };
#define IFFF_RWBITS 1
#define IFFERR_EOF      (-1)
#define IFFERR_EOC      (-2)
#define IFFERR_NOSCOPE  (-3)
#define IFFERR_NOMEM    (-4)
#define IFFERR_READ     (-5)
#define IFFERR_MANGLED  (-8)
#define IFFERR_SYNTAX   (-9)
#define IFFERR_NOTIFF   (-10)
#define IFFPARSE_SCAN    0
#define IFFPARSE_STEP    1
#define IFFPARSE_RAWSTEP 2

#define ID(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))
#define ID_FORM ID('F', 'O', 'R', 'M')
#define ID_LIST ID('L', 'I', 'S', 'T')
#define ID_CAT  ID('C', 'A', 'T', ' ')
#define ID_PROP ID('P', 'R', 'O', 'P')

#define MAX_PILA 32
#define MAX_STOP 64
typedef struct {
    uint32_t id, tipo, dim, scan;   /* scan: byte gia' letti del contenuto */
    long inizio;                    /* il contenuto nel buffer */
    int contenitore;
    uint32_t nodo;                  /* la ContextNode in memoria emulata */
} contesto_t;

typedef struct {
    uint32_t iff;                   /* l'IFFHandle in memoria emulata */
    uint8_t *dati;
    long n, pos;
    contesto_t pila[MAX_PILA];
    int prof;
    int fermo;                      /* ParseIFF si e' fermato sull'ultimo contesto */
    int finito;                     /* ParseIFF ha gia' restituito IFFERR_EOF */
    uint32_t stop[MAX_STOP][2];     /* (tipo, id) */
    int n_stop;
} gestore_t;

#define MAX_GESTORI 16
static gestore_t s_g[MAX_GESTORI];

static gestore_t *gestore(uint32_t iff) {
    for (int i = 0; i < MAX_GESTORI; i++) if (s_g[i].iff == iff && iff) return &s_g[i];
    return NULL;
}

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

static void aggiorna_nodo(contesto_t *c) {
    W32(c->nodo + CN_ID, c->id);
    W32(c->nodo + CN_TYPE, c->tipo);
    W32(c->nodo + CN_SIZE, c->dim);
    W32(c->nodo + CN_SCAN, c->scan);
}

static void svuota(gestore_t *g) {
    for (int i = 0; i < MAX_PILA; i++) if (g->pila[i].nodo) os_libera(g->pila[i].nodo, CN_DIM);
    free(g->dati);
    uint32_t iff = g->iff;
    memset(g, 0, sizeof *g);
    g->iff = iff;
}

/* AllocIFF() */
static void p_allociff(void) {
    for (int i = 0; i < MAX_GESTORI; i++) {
        if (s_g[i].iff) continue;
        memset(&s_g[i], 0, sizeof s_g[i]);
        s_g[i].iff = os_alloca(IFF_SIZE, MEMF_PUBLIC | MEMF_CLEAR);
        D0 = s_g[i].iff;
        return;
    }
    D0 = 0;
}

/* FreeIFF(iff A0) */
static void p_freeiff(void) {
    gestore_t *g = gestore(A0);
    if (!g) return;
    svuota(g);
    os_libera(g->iff, IFF_SIZE);
    g->iff = 0;
}

/* InitIFFasDOS(iff A0) e InitIFF(iff A0, flags D0, hook A1): il flusso e' un file DOS */
static void p_initiffasdos(void) { W32(A0 + IFF_FLAGS, R32(A0 + IFF_FLAGS) & IFFF_RWBITS); }
static void p_initiff(void) {
    os_manca("iffparse.library", "InitIFF (hook personale)", 228);
    W32(A0 + IFF_FLAGS, D0 & IFFF_RWBITS);
}

/* OpenIFF(iff A0, modo D0): in lettura carica tutto il resto del file */
static void p_openiff(void) {
    gestore_t *g = gestore(A0);
    if (!g) { D0 = (uint32_t)IFFERR_NOMEM; return; }
    if (D0 & IFFF_RWBITS) {                         /* scrittura: il gioco non la usa */
        os_manca("iffparse.library", "OpenIFF in scrittura", 36);
        D0 = (uint32_t)IFFERR_NOSCOPE;
        return;
    }
    svuota(g);
    FILE *f = os_dos_file(R32(A0 + IFF_STREAM));
    if (!f) { D0 = (uint32_t)IFFERR_READ; return; }
    long da = ftell(f);
    fseek(f, 0, SEEK_END);
    long fine = ftell(f);
    fseek(f, da, SEEK_SET);
    g->n = fine > da ? fine - da : 0;
    g->dati = malloc((size_t)g->n + 1);
    if (!g->dati || fread(g->dati, 1, (size_t)g->n, f) != (size_t)g->n) { D0 = (uint32_t)IFFERR_READ; return; }
    for (int i = 0; i < MAX_PILA; i++) g->pila[i].nodo = os_alloca(CN_DIM, MEMF_PUBLIC | MEMF_CLEAR);
    W32(A0 + IFF_FLAGS, R32(A0 + IFF_FLAGS) & ~IFFF_RWBITS);
    W32(A0 + IFF_DEPTH, 0);
    D0 = 0;
}

/* CloseIFF(iff A0) */
static void p_closeiff(void) {
    gestore_t *g = gestore(A0);
    if (g) svuota(g);
}

/* StopChunk(iff A0, tipo D0, id D1) e StopChunks(iff A0, coppie A1, quante D0) */
static void stop_aggiungi(gestore_t *g, uint32_t tipo, uint32_t id) {
    if (g->n_stop < MAX_STOP) { g->stop[g->n_stop][0] = tipo; g->stop[g->n_stop][1] = id; g->n_stop++; }
}
static void p_stopchunk(void) {
    gestore_t *g = gestore(A0);
    if (g) stop_aggiungi(g, D0, D1);
    D0 = 0;
}
static void p_stopchunks(void) {
    gestore_t *g = gestore(A0);
    for (uint32_t i = 0; g && i < D0; i++) stop_aggiungi(g, R32(A1 + 8 * i), R32(A1 + 8 * i + 4));
    D0 = 0;
}
static int e_stop(gestore_t *g, uint32_t tipo, uint32_t id) {
    for (int i = 0; i < g->n_stop; i++) if (g->stop[i][0] == tipo && g->stop[i][1] == id) return 1;
    return 0;
}

static int contenitore(uint32_t id) { return id == ID_FORM || id == ID_LIST || id == ID_CAT || id == ID_PROP; }

/* un passo: il prossimo contesto (entrando) o la fine di quello in cima.
 * 1 = entrato in un contesto, 0 = uscito da uno, <0 errore/fine */
static int passo(gestore_t *g) {
    if (g->prof == 0) {
        if (g->finito || g->pos + 12 > g->n) return IFFERR_EOF;
        uint32_t id = be32(g->dati + g->pos);
        if (!contenitore(id) || id == ID_PROP) return IFFERR_NOTIFF;
        contesto_t *c = &g->pila[g->prof++];
        c->id = id; c->dim = be32(g->dati + g->pos + 4); c->tipo = be32(g->dati + g->pos + 8);
        c->inizio = g->pos + 8; c->scan = 4; c->contenitore = 1;
        g->pos += 12;
        return 1;
    }
    contesto_t *t = &g->pila[g->prof - 1];
    if (!t->contenitore || t->scan + 8 > t->dim) {
        /* fine del contesto: si salta il resto (e il byte di riempimento) */
        long dopo = t->inizio + (long)t->dim + (t->dim & 1);
        g->prof--;
        if (g->prof) {
            contesto_t *p = &g->pila[g->prof - 1];
            p->scan = (uint32_t)(dopo - p->inizio);
            if (p->scan > p->dim) p->scan = p->dim;
        } else g->finito = 1;
        g->pos = dopo;
        return 0;
    }
    long a = t->inizio + (long)t->scan;
    if (a + 8 > g->n) return IFFERR_MANGLED;
    contesto_t *c = &g->pila[g->prof];
    if (g->prof + 1 >= MAX_PILA) return IFFERR_SYNTAX;
    c->id = be32(g->dati + a);
    c->dim = be32(g->dati + a + 4);
    c->inizio = a + 8;
    c->scan = 0;
    c->contenitore = contenitore(c->id);
    c->tipo = t->tipo;
    if (c->contenitore) {
        if (a + 12 > g->n) return IFFERR_MANGLED;
        c->tipo = be32(g->dati + a + 8);
        c->scan = 4;
    }
    if (c->inizio + (long)c->dim > g->n) c->dim = (uint32_t)(g->n - c->inizio);   /* file troncato */
    g->prof++;
    g->pos = c->inizio + c->scan;
    return 1;
}

/* ParseIFF(iff A0, controllo D0) */
static void p_parseiff(void) {
    gestore_t *g = gestore(A0);
    if (!g || !g->dati) { D0 = (uint32_t)IFFERR_NOSCOPE; return; }
    uint32_t ctl = D0;
    int r;
    if (g->fermo) {                                  /* il pezzo dove ci eravamo fermati: si esce */
        g->fermo = 0;
        if (g->prof && !g->pila[g->prof - 1].contenitore) passo(g);
    }
    for (;;) {
        r = passo(g);
        if (r < 0) break;
        W32(A0 + IFF_DEPTH, (uint32_t)g->prof);
        if (r == 1) {
            contesto_t *c = &g->pila[g->prof - 1];
            aggiorna_nodo(c);
            if (ctl != IFFPARSE_SCAN) { g->fermo = !c->contenitore; r = 0; break; }
            if (!c->contenitore && e_stop(g, c->tipo, c->id)) { g->fermo = 1; r = 0; break; }
        } else if (ctl != IFFPARSE_SCAN) { r = IFFERR_EOC; break; }
    }
    D0 = (uint32_t)r;
}

/* CurrentChunk(iff A0) / ParentChunk(contesto A0) */
static void p_currentchunk(void) {
    gestore_t *g = gestore(A0);
    if (!g || !g->prof) { D0 = 0; return; }
    aggiorna_nodo(&g->pila[g->prof - 1]);
    D0 = g->pila[g->prof - 1].nodo;
}
static void p_parentchunk(void) {
    for (int k = 0; k < MAX_GESTORI; k++)
        for (int i = 1; i < s_g[k].prof; i++)
            if (s_g[k].pila[i].nodo == A0) { aggiorna_nodo(&s_g[k].pila[i - 1]); D0 = s_g[k].pila[i - 1].nodo; return; }
    D0 = 0;
}

/* ReadChunkBytes(iff A0, buf A1, n D0) e ReadChunkRecords(iff A0, buf A1, dim D0, quanti D1) */
static int32_t leggi(gestore_t *g, uint32_t buf, uint32_t n) {
    if (!g || !g->prof) return IFFERR_EOC;
    contesto_t *c = &g->pila[g->prof - 1];
    uint32_t resta = c->dim > c->scan ? c->dim - c->scan : 0;
    if (n > resta) n = resta;
    if (buf + n > g_amiga_mem_size) return IFFERR_READ;
    memcpy(g_amiga_mem + buf, g->dati + c->inizio + c->scan, n);
    c->scan += n;
    aggiorna_nodo(c);
    return (int32_t)n;
}
static void p_readchunkbytes(void) { D0 = (uint32_t)leggi(gestore(A0), A1, D0); }
static void p_readchunkrecords(void) {
    gestore_t *g = gestore(A0);
    uint32_t dim = D0, quanti = D1;
    if (!g || !g->prof || !dim) { D0 = (uint32_t)IFFERR_EOC; return; }
    contesto_t *c = &g->pila[g->prof - 1];
    uint32_t possibili = (c->dim - c->scan) / dim;
    if (quanti > possibili) quanti = possibili;
    int32_t r = leggi(g, A1, quanti * dim);
    D0 = r < 0 ? (uint32_t)r : quanti;
}

/* GoodID(id D0), GoodType(tipo D0): 4 caratteri stampabili, niente spazi in testa */
static int buono(uint32_t id) {
    for (int i = 0; i < 4; i++) { uint8_t c = (uint8_t)(id >> (24 - 8 * i)); if (c < 0x20 || c > 0x7E) return 0; }
    return (id >> 24) != ' ';
}
static void p_goodid(void) { D0 = buono(D0) ? 1 : 0; }
static void p_goodtype(void) {
    int ok = buono(D0);
    for (int i = 0; i < 4 && ok; i++) { uint8_t c = (uint8_t)(D0 >> (24 - 8 * i)); if (c >= 'a' && c <= 'z') ok = 0; }
    D0 = ok ? 1 : 0;
}
/* IDtoStr(id D0, buf A0) */
static void p_idtostr(void) {
    for (int i = 0; i < 4; i++) W8(A0 + i, (uint8_t)(D0 >> (24 - 8 * i)));
    W8(A0 + 4, 0);
    D0 = A0;
}

#define F(lvo, fn) [(lvo) / 6 - 1] = fn
#define N(lvo, nome) [(lvo) / 6 - 1] = nome
#define N_IFF 45
static const os_funzione s_fn[N_IFF] = {
    F(30, p_allociff), F(36, p_openiff), F(42, p_parseiff), F(48, p_closeiff), F(54, p_freeiff),
    F(60, p_readchunkbytes), F(72, p_readchunkrecords), F(126, p_stopchunk), F(132, p_stopchunks),
    F(174, p_currentchunk), F(180, p_parentchunk), F(228, p_initiff), F(234, p_initiffasdos),
    F(258, p_goodid), F(264, p_goodtype), F(270, p_idtostr),
};
static const char *const s_nomi[N_IFF] = {
    N(6, "Open"), N(12, "Close"), N(18, "Expunge"), N(24, "Reserved"), N(30, "AllocIFF"), N(36, "OpenIFF"),
    N(42, "ParseIFF"), N(48, "CloseIFF"), N(54, "FreeIFF"), N(60, "ReadChunkBytes"), N(66, "WriteChunkBytes"),
    N(72, "ReadChunkRecords"), N(78, "WriteChunkRecords"), N(84, "PushChunk"), N(90, "PopChunk"),
    N(96, "Reserved"), N(102, "EntryHandler"), N(108, "ExitHandler"), N(114, "PropChunk"),
    N(120, "PropChunks"), N(126, "StopChunk"), N(132, "StopChunks"), N(138, "CollectionChunk"),
    N(144, "CollectionChunks"), N(150, "StopOnExit"), N(156, "FindProp"), N(162, "FindCollection"),
    N(168, "FindPropContext"), N(174, "CurrentChunk"), N(180, "ParentChunk"), N(186, "AllocLocalItem"),
    N(192, "LocalItemData"), N(198, "SetLocalItemPurge"), N(204, "FreeLocalItem"), N(210, "FindLocalItem"),
    N(216, "StoreLocalItem"), N(222, "StoreItemInContext"), N(228, "InitIFF"), N(234, "InitIFFasDOS"),
    N(240, "InitIFFasClip"), N(246, "OpenClipboard"), N(252, "CloseClipboard"), N(258, "GoodID"),
    N(264, "GoodType"), N(270, "IDtoStr"),
};

void os_iffparse_init(void) {
    static const os_libreria_c def = { "iffparse.library", 40, N_IFF, 34, s_nomi, s_fn };
    os_crea_libreria(&def, NT_LIBRARY);
}
