/* amiga_os.c - il nucleo di AmigaOS in C: memoria, librerie, liste, segnali,
 * messaggi, chiamate a codice 68000. Vedi amiga_os.h. */
#include "amiga_os.h"
#include "amiga_debug.h"
#include "genesis_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32_t os_sysbase;
uint32_t os_task;
int os_traccia;
void (*os_quadro)(void);

/* ======================================================== memoria (AllocMem) */

/* due zone, chip e fast; per ciascuna i blocchi liberi, ordinati e fusi */
typedef struct { uint32_t inizio, dim; } blocco_t;
typedef struct {
    uint32_t inizio, fine;
    blocco_t *liberi;
    int n, cap;
} zona_t;
static zona_t s_chip, s_fast, s_alta;          /* chip, fast bassa, fast a 32 bit */

static void zona_init(zona_t *z, uint32_t inizio, uint32_t fine) {
    z->inizio = inizio; z->fine = fine;
    z->cap = 64; z->n = 1;
    z->liberi = malloc(sizeof(blocco_t) * (size_t)z->cap);
    z->liberi[0] = (blocco_t){ inizio, fine - inizio };
}

static uint32_t zona_alloca(zona_t *z, uint32_t dim) {
    for (int i = 0; i < z->n; i++) {
        blocco_t *b = &z->liberi[i];
        if (b->dim < dim) continue;
        uint32_t a = b->inizio;
        b->inizio += dim; b->dim -= dim;
        if (!b->dim) { memmove(b, b + 1, sizeof *b * (size_t)(z->n - i - 1)); z->n--; }
        return a;
    }
    return 0;
}

static void zona_libera(zona_t *z, uint32_t a, uint32_t dim) {
    int i = 0;
    while (i < z->n && z->liberi[i].inizio < a) i++;
    /* si unisce al blocco prima e/o dopo */
    int prima = i > 0 && z->liberi[i - 1].inizio + z->liberi[i - 1].dim == a;
    int dopo = i < z->n && a + dim == z->liberi[i].inizio;
    if (prima && dopo) {
        z->liberi[i - 1].dim += dim + z->liberi[i].dim;
        memmove(&z->liberi[i], &z->liberi[i + 1], sizeof(blocco_t) * (size_t)(z->n - i - 1));
        z->n--;
    } else if (prima) z->liberi[i - 1].dim += dim;
    else if (dopo) { z->liberi[i].inizio = a; z->liberi[i].dim += dim; }
    else {
        if (z->n == z->cap) { z->cap *= 2; z->liberi = realloc(z->liberi, sizeof(blocco_t) * (size_t)z->cap); }
        memmove(&z->liberi[i + 1], &z->liberi[i], sizeof(blocco_t) * (size_t)(z->n - i));
        z->liberi[i] = (blocco_t){ a, dim };
        z->n++;
    }
}

static uint32_t zona_libera_tot(const zona_t *z, int piu_grande) {
    uint32_t t = 0;
    for (int i = 0; i < z->n; i++)
        t = piu_grande ? (z->liberi[i].dim > t ? z->liberi[i].dim : t) : t + z->liberi[i].dim;
    return t;
}

uint32_t os_alloca(uint32_t dim, uint32_t flag) {
    if (!dim) return 0;
    uint32_t d = (dim + 7) & ~7u;                      /* come exec: multipli di 8 */
    uint32_t a = 0;
    /* la fast a 32 bit prima, poi la bassa, poi la chip (come exec, che
     * preferisce la fast); MEMF_24BITDMA resta sotto i 16 MB */
    if (!(flag & (MEMF_CHIP | MEMF_24BITDMA)) && s_alta.fine) a = zona_alloca(&s_alta, d);
    if (!a && !(flag & MEMF_CHIP)) a = zona_alloca(&s_fast, d);
    if (!a && !(flag & MEMF_FAST)) a = zona_alloca(&s_chip, d);
    if (a && (flag & MEMF_CLEAR)) memset(g_amiga_mem + a, 0, d);
    return a;
}

void os_libera(uint32_t a, uint32_t dim) {
    if (!a || !dim) return;
    uint32_t d = (dim + 7) & ~7u;
    /* il blocco delle stringhe del requester (amiga_asl.c) non si libera mai:
     * chi lo fa e' il colpevole del titolo a caratteri casuali */
    extern uint32_t os_asl_buf, os_asl_buf_dim;
    if (os_asl_buf && a < os_asl_buf + os_asl_buf_dim && a + d > os_asl_buf) {
        fprintf(stderr, "[os] FreeMem $%08X (%u byte) sopra il blocco del requester $%08X: ignorato\n", a, dim, os_asl_buf);
        amiga_esame("[os] FreeMem $%08X (%u byte) sopra il blocco del requester: ignorato", a, dim);
        return;
    }
    if (a >= s_chip.inizio && a + d <= s_chip.fine) zona_libera(&s_chip, a, d);
    else if (a >= s_fast.inizio && a + d <= s_fast.fine) zona_libera(&s_fast, a, d);
    else if (s_alta.fine && a >= s_alta.inizio && a + d <= s_alta.fine) zona_libera(&s_alta, a, d);
    else fprintf(stderr, "[os] FreeMem di un blocco mai allocato: $%08X (%u byte)\n", a, dim);
}

uint32_t os_disponibile(uint32_t flag) {
    int g = (flag & MEMF_LARGEST) != 0;
    uint32_t c = zona_libera_tot(&s_chip, g), f = zona_libera_tot(&s_fast, g);
    if (s_alta.fine) {
        uint32_t h = zona_libera_tot(&s_alta, g);
        f = g ? (h > f ? h : f) : f + h;
    }
    if (flag & MEMF_CHIP) return c;
    if (flag & MEMF_FAST) return f;
    return g ? (c > f ? c : f) : c + f;
}

uint32_t os_stringa(const char *s) {
    size_t n = strlen(s) + 1;
    uint32_t a = os_alloca((uint32_t)n, MEMF_PUBLIC);
    memcpy(g_amiga_mem + a, s, n);
    return a;
}

void os_leggi_stringa(uint32_t a, char *buf, size_t n) {
    size_t i = 0;
    if (a) while (i + 1 < n && a + i < g_amiga_mem_size && g_amiga_mem[a + i]) { buf[i] = (char)g_amiga_mem[a + i]; i++; }
    buf[i] = 0;
}

/* ======================================================== liste */

void os_lista_init(uint32_t l) {
    W32(l + LH_HEAD, l + LH_TAIL);
    W32(l + LH_TAIL, 0);
    W32(l + LH_TAILPRED, l + LH_HEAD);
}

int os_lista_vuota(uint32_t l) { return R32(l + LH_TAILPRED) == l; }

void os_aggiungi_testa(uint32_t l, uint32_t n) {
    uint32_t primo = R32(l + LH_HEAD);
    W32(n + LN_SUCC, primo);
    W32(n + LN_PRED, l + LH_HEAD);
    W32(primo + LN_PRED, n);
    W32(l + LH_HEAD, n);
}

void os_aggiungi_coda(uint32_t l, uint32_t n) {
    uint32_t ultimo = R32(l + LH_TAILPRED);
    W32(n + LN_SUCC, l + LH_TAIL);
    W32(n + LN_PRED, ultimo);
    W32(ultimo + LN_SUCC, n);
    W32(l + LH_TAILPRED, n);
}

void os_togli(uint32_t n) {
    uint32_t s = R32(n + LN_SUCC), p = R32(n + LN_PRED);
    W32(p + LN_SUCC, s);
    W32(s + LN_PRED, p);
}

uint32_t os_togli_testa(uint32_t l) {
    uint32_t n = R32(l + LH_HEAD);
    if (!R32(n + LN_SUCC)) return 0;                 /* la coda finta: lista vuota */
    os_togli(n);
    return n;
}

void os_accoda_pri(uint32_t l, uint32_t n) {
    int8_t pri = (int8_t)R8(n + LN_PRI);
    uint32_t q = R32(l + LH_HEAD);
    while (R32(q + LN_SUCC) && (int8_t)R8(q + LN_PRI) >= pri) q = R32(q + LN_SUCC);
    /* davanti a q */
    uint32_t p = R32(q + LN_PRED);
    W32(n + LN_SUCC, q);
    W32(n + LN_PRED, p);
    W32(p + LN_SUCC, n);
    W32(q + LN_PRED, n);
}

uint32_t os_trova_nome(uint32_t l, const char *nome) {
    for (uint32_t n = R32(l + LH_HEAD); R32(n + LN_SUCC); n = R32(n + LN_SUCC)) {
        char b[128];
        os_leggi_stringa(R32(n + LN_NAME), b, sizeof b);
        if (!strcmp(b, nome)) return n;
    }
    return 0;
}

/* ======================================================== segnali e messaggi */

void os_segnala(uint32_t task, uint32_t segnali) {
    if (!task) return;
    W32(task + TC_SIGRECVD, R32(task + TC_SIGRECVD) | segnali);
}

uint32_t os_task_attendi(uint32_t maschera);       /* amiga_task.c */
uint32_t os_attendi(uint32_t maschera) {
    if (!maschera) return 0;
    return os_task_attendi(maschera);
}

void os_metti_msg(uint32_t porta, uint32_t msg) {
    W8(msg + LN_TYPE, NT_MESSAGE);
    os_aggiungi_coda(porta + MP_MSGLIST, msg);
    if ((R8(porta + MP_FLAGS) & 3) == 0)                         /* PA_SIGNAL */
        os_segnala(R32(porta + MP_SIGTASK), 1u << R8(porta + MP_SIGBIT));
}

uint32_t os_prendi_msg(uint32_t porta) { return os_togli_testa(porta + MP_MSGLIST); }

void os_rispondi_msg(uint32_t msg) {
    uint32_t porta = R32(msg + MN_REPLYPORT);
    if (!porta) { W8(msg + LN_TYPE, NT_FREEMSG); return; }
    os_metti_msg(porta, msg);
    W8(msg + LN_TYPE, NT_REPLYMSG);
}

uint32_t os_crea_porta(void) {
    uint32_t p = os_alloca(MP_SIZE, MEMF_PUBLIC | MEMF_CLEAR);
    /* un segnale libero del task */
    uint32_t alloc = R32(os_task + TC_SIGALLOC);
    int b = 31;
    while (b >= 16 && (alloc >> b & 1)) b--;
    W32(os_task + TC_SIGALLOC, alloc | 1u << b);
    W8(p + LN_TYPE, NT_MSGPORT);
    W8(p + MP_SIGBIT, b);
    W32(p + MP_SIGTASK, os_task);
    os_lista_init(p + MP_MSGLIST);
    return p;
}

/* ======================================================== chiamate a 68000 */

#define RITORNO_FINTO 0x00000Cu       /* l'indirizzo di ritorno che si spinge: mai eseguito */

void os_chiama_68k(uint32_t ind) {
    recomp_push_return(RITORNO_FINTO);
    uint32_t slot = g_cpu.A[7];
    int salvato = g_split_sp_popped;
    g_split_sp_popped = 0;
    call_by_address(ind);
    g_split_sp_popped = salvato;
    recomp_pop_slot();
    if (g_rte_pending) g_rte_pending = 0;
    if (g_cpu.A[7] == slot) g_cpu.A[7] += 4;          /* RTS non eseguito dal C: lo togliamo noi */
}

void os_chiama_68k_isolato(uint32_t ind) {
    M68KState s = g_cpu;
    os_chiama_68k(ind);
    g_cpu = s;
}

/* ======================================================== librerie */

typedef struct {
    char nome[40];
    uint32_t base;                    /* 0 = non ancora creata (librerie del programma) */
    int n;                            /* voci della tabella dei salti */
    const os_libreria_c *c;           /* NULL = codice 68000 del programma */
    /* librerie del programma: dove stanno */
    uint32_t seglist, dati, vettori, struttura, init;
    /* sostituzioni in C delle funzioni del programma */
    os_funzione sost[64];
} libreria_t;

static libreria_t s_lib[48];
static int s_n_lib;

static libreria_t *lib_nome(const char *nome) {
    for (int i = 0; i < s_n_lib; i++) if (!strcmp(s_lib[i].nome, nome)) return &s_lib[i];
    return NULL;
}

void os_manca(const char *libreria, const char *funzione, int lvo) {
    static char visti[512][48];
    static int n;
    char k[48];
    snprintf(k, sizeof k, "%s%d", libreria, lvo);
    for (int i = 0; i < n; i++) if (!strcmp(visti[i], k)) return;
    if (n < 512) strcpy(visti[n++], k);
    fprintf(stderr, "[os] manca %s/%s (LVO -%d)\n", libreria, funzione ? funzione : "?", lvo);
    char m[96];
    snprintf(m, sizeof m, "%s/%s (LVO -%d)", libreria, funzione ? funzione : "?", lvo);
    amiga_debug_evento_gioco("manca", m);
}

/* la base: tabella dei salti (JMP abs.l alla voce stessa: chi la legge vede
 * un salto, chi la chiama finisce in recomp_dispatch_ram_stub) e la Library */
static uint32_t costruisci_base(const char *nome, int n, int dim_base, int versione, uint8_t tipo) {
    uint32_t neg = (uint32_t)n * 6u;
    neg = (neg + 3) & ~3u;
    uint32_t pos = (uint32_t)(dim_base > LIB_SIZE ? dim_base : LIB_SIZE);
    uint32_t mem = os_alloca(neg + pos, MEMF_PUBLIC | MEMF_CLEAR | MEMF_24BITDMA);   /* tabella dei salti sotto i 16 MB */
    uint32_t base = mem + neg;
    for (int i = 0; i < n; i++) {
        uint32_t v = base - 6u * (uint32_t)(i + 1);
        W16(v, 0x4EF9);
        W32(v + 2, v);
    }
    W8(base + LN_TYPE, tipo);
    W32(base + LN_NAME, os_stringa(nome));
    W16(base + LIB_NEGSIZE, neg);
    W16(base + LIB_POSSIZE, pos);
    W16(base + LIB_VERSION, versione);
    return base;
}

/* amiga_nomi_v40.c: le funzioni da Kickstart 2.0 a 3.1 */
int os_voci_v40(const char *libreria);
const char *os_nome_v40(const char *libreria, int lvo);

uint32_t os_crea_libreria(const os_libreria_c *def, uint8_t tipo) {
    libreria_t *l = &s_lib[s_n_lib++];
    snprintf(l->nome, sizeof l->nome, "%s", def->nome);
    /* exec, dos, graphics, intuition: tabella dei salti lunga come la V40 e
     * versione 40 (Workbench 3.1); le voci oltre quelle in C le segnala
     * os_manca col loro nome */
    int v40 = os_voci_v40(def->nome);
    l->n = v40 > def->n ? v40 : def->n;
    l->c = def;
    l->base = costruisci_base(def->nome, l->n, def->dim_base, v40 ? 40 : def->versione, tipo);
    if (os_sysbase && tipo == NT_LIBRARY) os_aggiungi_coda(os_sysbase + EB_LIBLIST, l->base);
    if (os_sysbase && tipo == NT_DEVICE) os_aggiungi_coda(os_sysbase + EB_DEVICELIST, l->base);
    if (os_sysbase && tipo == NT_RESOURCE) os_aggiungi_coda(os_sysbase + EB_RESOURCELIST, l->base);
    return l->base;
}

uint32_t os_base(const char *nome) {
    libreria_t *l = lib_nome(nome);
    return l ? l->base : 0;
}

void os_libreria_disco(const char *nome, uint32_t seglist, uint32_t dati,
                       uint32_t vettori, uint32_t struttura, uint32_t init) {
    libreria_t *l = &s_lib[s_n_lib++];
    snprintf(l->nome, sizeof l->nome, "%s", nome);
    l->seglist = seglist; l->dati = dati; l->vettori = vettori;
    l->struttura = struttura; l->init = init;
}

void os_sostituisci(const char *libreria, int lvo, os_funzione f) {
    libreria_t *l = lib_nome(libreria);
    if (l && lvo / 6 - 1 < 64) l->sost[lvo / 6 - 1] = f;
}

/* MakeLibrary per le librerie del programma (exec la chiama anche dal C) */
uint32_t os_make_library(uint32_t vettori, uint32_t struttura, uint32_t init, uint32_t dim, uint32_t seglist);

/* "libs:req.library", "devs/narrator.device": conta solo il nome */
static const char *solo_nome(const char *nome) {
    const char *p = strrchr(nome, '/');
    if (!p) p = strrchr(nome, ':');
    return p ? p + 1 : nome;
}

/* prima apertura di una libreria o di un device del programma: come
 * InitResident. 0 se l'init fallisce */
/* la RomTag di una libreria del disco ($4AFC seguito dal puntatore a se
 * stessa), 0 se non c'e' */
static uint32_t cerca_romtag(uint32_t seglist) {
    for (uint32_t p = seglist; p < seglist + 0x40000u; p += 2)
        if (R16(p) == 0x4AFC && R32(p + 2) == p) return p;
    return 0;
}

static int inizializza_disco(libreria_t *l) {
    if (l->base) return 1;
    M68KState s = g_cpu;
    uint32_t base;
    if (l->dati) {
        base = os_make_library(l->vettori, l->struttura, 0, l->dati, l->seglist);
        /* come InitResident: senza struttura d'init (iffparse 40) nome,
         * versione e IdString vengono dalla RomTag */
        uint32_t rt = l->struttura ? 0 : cerca_romtag(l->seglist);
        if (base && rt) {
            W8(base + LN_TYPE, R8(rt + 12));
            W32(base + LN_NAME, R32(rt + 14));
            W16(base + LIB_VERSION, R8(rt + 11));
            W32(base + LIB_IDSTRING, R32(rt + 18));
            W8(base + LIB_FLAGS, 0x06);                     /* LIBF_SUMUSED | LIBF_CHANGED */
        }
        if (l->init) {
            g_cpu.D[0] = base; g_cpu.A[0] = MKBADDR(l->seglist); g_cpu.A[6] = os_sysbase;
            os_chiama_68k(l->init);
            base = g_cpu.D[0];
        }
        if (base) os_aggiungi_coda(os_sysbase + EB_LIBLIST, base);
    } else {
        /* la routine fa MakeLibrary e AddLibrary (o AddDevice) da sola: la
         * base e' quella aggiunta alla lista (certe non la restituiscono in D0) */
        extern uint32_t os_ultima_libreria;
        os_ultima_libreria = 0;
        g_cpu.D[0] = 0; g_cpu.A[0] = MKBADDR(l->seglist); g_cpu.A[6] = os_sysbase;
        os_chiama_68k(l->init);
        base = os_ultima_libreria ? os_ultima_libreria : g_cpu.D[0];
    }
    g_cpu = s;
    if (!base) { fprintf(stderr, "[os] init di %s fallita\n", l->nome); return 0; }
    l->base = base;
    l->n = R16(base + LIB_NEGSIZE) / 6;
    return 1;
}

/* un device del programma (devs/), 0 se non c'e': lo inizializza alla prima
 * apertura e chiama il suo Open come exec (A6 device, A1 richiesta, D0
 * unita', D1 flag); *errore = io_Error */
uint32_t os_apri_device_disco(const char *nome, uint32_t unita, uint32_t io, uint32_t flag, int *errore) {
    nome = solo_nome(nome);
    libreria_t *l = NULL;
    for (int i = 0; i < s_n_lib; i++)
        if (!s_lib[i].c && !strcmp(s_lib[i].nome, nome)) { l = &s_lib[i]; break; }
    if (!l || !inizializza_disco(l)) return 0;
    W32(io + IO_DEVICE, l->base);
    W8(io + IO_ERROR, 0);
    M68KState s = g_cpu;
    g_cpu.A[6] = l->base; g_cpu.A[1] = io; g_cpu.D[0] = unita; g_cpu.D[1] = flag;
    os_chiama_68k(l->base - 6);
    g_cpu = s;
    *errore = (int8_t)R8(io + IO_ERROR);
    if (*errore) W32(io + IO_DEVICE, 0);
    return l->base;
}

uint32_t os_apri_libreria(const char *nome, uint32_t versione) {
    nome = solo_nome(nome);
    libreria_t *l = lib_nome(nome);
    if (!l) return 0;
    if (!l->c && !inizializza_disco(l)) return 0;
    if (R16(l->base + LIB_VERSION) < versione) return 0;
    if (!l->c) {
        /* Open della libreria: D0 = versione, A6 = base */
        M68KState s = g_cpu;
        g_cpu.D[0] = versione; g_cpu.A[6] = l->base;
        os_chiama_68k(l->base - 6);
        uint32_t r = g_cpu.D[0];
        g_cpu = s;
        return r;
    }
    W16(l->base + LIB_OPENCNT, R16(l->base + LIB_OPENCNT) + 1);
    return l->base;
}

/* 1 se addr sta nella tabella dei salti di una libreria (amiga_interprete.c:
 * li' Musashi si ferma e chiama la funzione nativa) */
int os_e_vettore_libreria(uint32_t addr) {
    for (int i = 0; i < s_n_lib; i++) {
        libreria_t *l = &s_lib[i];
        if (l->base && addr < l->base && addr >= l->base - 6u * (uint32_t)l->n) return 1;
    }
    return 0;
}

/* il dispatcher del C generato: un salto a un indirizzo che non e' una
 * funzione ricompilata. 1 = gestito */
int recomp_dispatch_ram_stub(uint32_t addr) {
    addr &= 0xFFFFFFu;
    for (int i = 0; i < s_n_lib; i++) {
        libreria_t *l = &s_lib[i];
        if (!l->base || addr >= l->base || addr < l->base - 6u * (uint32_t)l->n) continue;
        uint32_t d = l->base - addr;
        if (d % 6) continue;
        int k = (int)(d / 6) - 1;
        if (l->c) {
            os_funzione f = k < l->c->n ? l->c->funzioni[k] : NULL;
            const char *nome = l->c->nomi && k < l->c->n ? l->c->nomi[k] : NULL;
            if (!nome) nome = os_nome_v40(l->nome, (int)d);
            amiga_debug_api(i, k, l->nome, nome);
            if (os_traccia) {
                extern uint64_t g_os_quadri;
                fprintf(stderr, "[os] q%llu T%06X %s/%s da $%06X D0=%08X D1=%08X D2=%08X D3=%08X A0=%06X A1=%06X A4=%06X A7=%06X",
                        (unsigned long long)g_os_quadri, os_task, l->nome, nome ? nome : "?", R32(g_cpu.A[7]) - 4,
                        g_cpu.D[0], g_cpu.D[1], g_cpu.D[2], g_cpu.D[3], g_cpu.A[0], g_cpu.A[1], g_cpu.A[4], g_cpu.A[7]);
            }
            if (f) {
                f();
                extern void os_guardia_req(const char *dopo);
                static char dopo[80];
                snprintf(dopo, sizeof dopo, "%s/%s", l->nome, nome ? nome : "?");
                os_guardia_req(dopo);
            } else {
                os_manca(l->nome, nome, (int)d);
                if (k == 0) g_cpu.D[0] = l->base;              /* Open */
                else g_cpu.D[0] = 0;
            }
            if (os_traccia) fprintf(stderr, " -> %08X\n", g_cpu.D[0]);
            return 1;
        }
        /* le librerie in 68000 (del gioco o del disco): contate anche loro */
        extern uint64_t g_os_quadri;
        amiga_debug_api(i, k, l->nome, os_nome_v40(l->nome, (int)d));
        if (os_traccia) {
            const char *n = os_nome_v40(l->nome, (int)d);
            fprintf(stderr, "[os68k] q%llu %s/%s (LVO -%u) da $%06X D0=%08X D1=%08X A0=%06X A1=%06X A2=%06X\n",
                    (unsigned long long)g_os_quadri, l->nome, n ? n : "?", d, R32(g_cpu.A[7]),
                    g_cpu.D[0], g_cpu.D[1], g_cpu.A[0], g_cpu.A[1], g_cpu.A[2]);
        }
        if (k < 64 && l->sost[k]) { l->sost[k](); return 1; }
        /* libreria del programma: il JMP della sua tabella */
        if (R16(addr) == 0x4EF9) {
            call_by_address(R32(addr + 2));
            return 1;
        }
        return 0;
    }
    /* piccoli stub, nelle tabelle di salti dei programmi o costruiti in RAM:
     *   jmp abs.l / jmp d16(pc)    si va alla destinazione
     *   rts                        niente
     *   moveq #n,d0 + rts          D0 = n */
    if (addr + 6 <= g_amiga_mem_size) {
        uint16_t w = R16(addr);
        if (w == 0x4EF9 && R32(addr + 2) != addr) { call_by_address(R32(addr + 2)); return 1; }
        if (w == 0x4EFA) { call_by_address(addr + 2 + (uint32_t)(int32_t)S16(addr + 2)); return 1; }
        if (w == 0x4E75) return 1;
        if ((w & 0xF100) == 0x7000 && R16(addr + 2) == 0x4E75) {
            int32_t n = (int8_t)w;
            g_cpu.D[(w >> 9) & 7] = (uint32_t)n;
            g_cpu.SR &= ~0x0Fu;                     /* come MOVEQ: N e Z, V e C a zero */
            if (!n) g_cpu.SR |= 1u << 2;
            if (n < 0) g_cpu.SR |= 1u << 3;
            return 1;
        }
    }
    return 0;
}

/* ======================================================== avvio */

/* l'inizio della chip libera: dopo i hunk CHIP del programma, se li ha
 * (il gioco lo imposta prima di os_init) */
uint32_t os_inizio_chip = OS_INIZIO_CHIP;

void os_init(void) {
    zona_init(&s_chip, os_inizio_chip, OS_FINE_CHIP);
    /* la fast comincia dopo il programma e le sue librerie (gia' in memoria) */
    extern uint32_t os_fine_programma;
    zona_init(&s_fast, (os_fine_programma + 0xFFFu) & ~0xFFFu, OS_MEM - 0x10000u);
    if (g_amiga_mem_size >= OS_MEM_TOT) zona_init(&s_alta, OS_FAST_ALTA, OS_FAST_ALTA + OS_FAST_DIM);
    os_traccia = getenv("AMIGA_TRACCIA_OS") != NULL;
    os_exec_init();                                  /* ExecBase, poi il resto */
    W32(4, os_sysbase);
}

void os_prepara_avvio(uint32_t seglist, const char *argomenti, const char *nome_comando) {
    /* il Process: task + porta + campi di AmigaDOS; la CLI che lo ha lanciato */
    uint32_t pr = os_alloca(PR_SIZE, MEMF_PUBLIC | MEMF_CLEAR);
    fprintf(stderr, "[os] processo del programma T%06X\n", pr);   /* per le prove (tools/prova_menu.py) */
    W8(pr + LN_TYPE, NT_PROCESS);
    W32(pr + LN_NAME, os_stringa(nome_comando));
    W8(pr + TC_STATE, 2);                            /* TS_RUN */
    W32(pr + TC_SIGALLOC, 0x0000FFFFu);              /* i segnali di sistema */
    W32(pr + TC_SPLOWER, OS_PILA_BASSO);
    W32(pr + TC_SPUPPER, OS_PILA_ALTO);
    os_lista_init(pr + TC_MEMENTRY);
    W8(pr + PR_MSGPORT + LN_TYPE, NT_MSGPORT);
    W8(pr + PR_MSGPORT + MP_SIGBIT, 8);              /* SIGB_DOS */
    W32(pr + PR_MSGPORT + MP_SIGTASK, pr);
    os_lista_init(pr + PR_MSGPORT + MP_MSGLIST);
    W32(pr + PR_SEGLIST, 0);
    W32(pr + PR_STACKSIZE, OS_PILA_ALTO - OS_PILA_BASSO);
    W32(pr + PR_TASKNUM, 1);
    uint32_t cli = os_alloca(CLI_SIZE, MEMF_PUBLIC | MEMF_CLEAR);
    /* cli_CommandName: BSTR (lunghezza + caratteri) */
    size_t ln = strlen(nome_comando);
    uint32_t bstr = os_alloca((uint32_t)ln + 2, MEMF_PUBLIC | MEMF_CLEAR);
    W8(bstr, ln);
    memcpy(g_amiga_mem + bstr + 1, nome_comando, ln);
    W32(cli + CLI_COMMANDNAME, MKBADDR(bstr));
    W32(cli + CLI_DEFAULTSTACK, (OS_PILA_ALTO - OS_PILA_BASSO) / 4);
    W32(cli + 60, seglist);                          /* cli_Module */
    W32(pr + PR_CLI, MKBADDR(cli));
    os_task = pr;
    W32(os_sysbase + EB_THISTASK, pr);

    /* argomenti: A0 = la riga (finita da LF), D0 = lunghezza */
    size_t la = strlen(argomenti);
    uint32_t arg = os_alloca((uint32_t)la + 2, MEMF_PUBLIC | MEMF_CLEAR);
    memcpy(g_amiga_mem + arg, argomenti, la);
    g_amiga_mem[arg + la] = '\n';
    g_cpu.A[0] = arg;
    g_cpu.D[0] = (uint32_t)la + 1;
    /* come la CLI: sopra l'indirizzo di ritorno c'e' la dimensione della pila */
    g_cpu.A[7] = OS_PILA_ALTO - 4;
    W32(OS_PILA_ALTO - 4, OS_PILA_ALTO - OS_PILA_BASSO);
    g_cpu.SR = 0;
}

uint64_t os_microsecondi(void) {
    extern uint64_t g_os_quadri;
    return g_os_quadri * 20000ull;
}
