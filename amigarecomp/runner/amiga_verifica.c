/* amiga_verifica.c - prova differenziale: codice ricompilato contro Musashi.
 *
 * AMIGA_VERIFICA=n (n = quante volte per funzione, di solito 1-3): a ogni
 * chiamata diretta a una funzione ricompilata (recomp_call_func) che non e'
 * ancora stata provata n volte,
 *   1. si fotografano registri e memoria;
 *   2. Musashi esegue la stessa funzione dalla stessa fotografia fino al suo
 *      ritorno (l'indirizzo di ritorno in pila e' sostituito per un momento
 *      da una sentinella);
 *   3. si rimette la fotografia, gira il codice ricompilato, e si
 *      confrontano D0-D7, A0-A7, i flag e tutta la memoria.
 * Se l'interprete esce dal codice del gioco (una chiamata al sistema, che e'
 * in C e avrebbe effetti propri), tocca l'hardware o non torna entro un
 * budget di istruzioni, il confronto per quella chiamata si lascia perdere.
 * Durante la prova il quadro non scatta (niente interrupt, task, audio).
 *
 * Le differenze vanno su stderr come "[verifica]": la prima istruzione
 * sbagliata e' in quella funzione o in una che chiama (le chiamate interne
 * si provano a loro volta, quindi conviene guardare la piu' profonda).
 */
#include "amiga_hw.h"
#include "genesis_runtime.h"
#include "m68k.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern uint32_t os_fine_programma;
extern uint64_t g_os_quadri;
int g_verifica_attiva;                         /* glue_check_vblank aspetta */

typedef void (*fn_t)(void);
extern int (*g_recomp_call_hook)(fn_t fn);
void recomp_call_func_direct(fn_t fn);
int game_dispatch_table_size(void);
uint32_t game_dispatch_table_addr(int i);
fn_t game_dispatch_table_fn(int i);

#define INIZIO_CODICE 0x200000u
#define SENTINELLA    0xF00000u                /* fuori dalla memoria: li' si legge NOP */
#define BUDGET        20000000u

typedef struct { fn_t fn; uint32_t addr; int volte, tentativi, sbagliata; } voce_t;
static voce_t *s_voci;
static int s_n_voci, s_volte = 1;
static uint8_t *s_foto, *s_risultato;
static unsigned s_ok, s_sbagliate, s_sistema, s_hw, s_budget;

/* ---------------------------------------------------------- Musashi */

static enum { CORRE, FINITA, SISTEMA, HW, BUDGET_FINITO } s_stato;
static unsigned s_istruzioni;

/* le ultime istruzioni di Musashi, per il rapporto: pc, D0 e flag prima di ognuna */
#define N_TRACCIA 48
static struct { uint32_t pc, d0; uint16_t sr; } s_traccia[N_TRACCIA];

static int memoria(uint32_t a) { return os_e_ram(a); }      /* mappa a 32 bit, amiga_os.h */
static void hw(void) { if (s_stato == CORRE) s_stato = HW; m68k_end_timeslice(); }

unsigned int m68k_read_memory_8(unsigned int a) {
    if (a >= SENTINELLA && a < SENTINELLA + 16) return (a & 1) ? 0x71 : 0x4E;     /* nop */
    if (!memoria(a)) { hw(); return 0; }
    return g_amiga_mem[a];
}
unsigned int m68k_read_memory_16(unsigned int a) {
    return m68k_read_memory_8(a) << 8 | m68k_read_memory_8(a + 1);
}
unsigned int m68k_read_memory_32(unsigned int a) {
    return m68k_read_memory_16(a) << 16 | m68k_read_memory_16(a + 2);
}
void m68k_write_memory_8(unsigned int a, unsigned int v) {
    if (!memoria(a)) { hw(); return; }
    g_amiga_mem[a] = (uint8_t)v;
}
void m68k_write_memory_16(unsigned int a, unsigned int v) {
    m68k_write_memory_8(a, v >> 8); m68k_write_memory_8(a + 1, v);
}
void m68k_write_memory_32(unsigned int a, unsigned int v) {
    m68k_write_memory_16(a, v >> 16); m68k_write_memory_16(a + 2, v);
}

void verifica_istruzione(unsigned int pc) {
    pc &= 0xFFFFFFu;
    if (s_stato != CORRE) { m68k_end_timeslice(); return; }
    if (pc == SENTINELLA) { s_stato = FINITA; m68k_end_timeslice(); return; }
    if (pc < INIZIO_CODICE || pc >= os_fine_programma) { s_stato = SISTEMA; m68k_end_timeslice(); return; }
    int t = s_istruzioni % N_TRACCIA;
    s_traccia[t].pc = pc;
    s_traccia[t].d0 = m68k_get_reg(NULL, M68K_REG_D0);
    s_traccia[t].sr = (uint16_t)m68k_get_reg(NULL, M68K_REG_SR);
    if (++s_istruzioni > BUDGET) { s_stato = BUDGET_FINITO; m68k_end_timeslice(); }
}

static void musashi_init(void) {
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68020);
    s_stato = FINITA;                          /* il reset legge i vettori senza contare */
    m68k_pulse_reset();
}

/* ---------------------------------------------------------- confronto */

static int per_fn(const void *a, const void *b) {
    uintptr_t x = (uintptr_t)((const voce_t *)a)->fn, y = (uintptr_t)((const voce_t *)b)->fn;
    return x < y ? -1 : x > y;
}
static voce_t *trova(fn_t fn) {
    int lo = 0, hi = s_n_voci;
    while (lo < hi) {
        int m = (lo + hi) / 2;
        if ((uintptr_t)s_voci[m].fn < (uintptr_t)fn) lo = m + 1; else hi = m;
    }
    return lo < s_n_voci && s_voci[lo].fn == fn ? &s_voci[lo] : NULL;
}

static void riassunto(void) {
    fprintf(stderr, "[verifica] riassunto: %u giuste, %u SBAGLIATE; non confrontate: %u sistema, %u hardware, %u troppo lunghe\n",
            s_ok, s_sbagliate, s_sistema, s_hw, s_budget);
}

static void riporta(voce_t *v, const M68KState *prima, const M68KState *mus) {
    static const char *const flag = "CVZNX";
    fprintf(stderr, "[verifica] $%06X SBAGLIATA (quadro %llu)\n", v->addr, (unsigned long long)g_os_quadri);
    for (int i = 0; i < 8; i++) {
        if (mus->D[i] != g_cpu.D[i])
            fprintf(stderr, "  D%d: musashi %08X, ricompilato %08X (prima %08X)\n", i, mus->D[i], g_cpu.D[i], prima->D[i]);
        if (mus->A[i] != g_cpu.A[i])
            fprintf(stderr, "  A%d: musashi %08X, ricompilato %08X (prima %08X)\n", i, mus->A[i], g_cpu.A[i], prima->A[i]);
    }
    uint16_t df = (uint16_t)((mus->SR ^ g_cpu.SR) & 0x1F);
    if (df) {
        fprintf(stderr, "  flag diversi:");
        for (int b = 0; b < 5; b++) if (df >> b & 1) fprintf(stderr, " %c(m%d r%d)", flag[b], mus->SR >> b & 1, g_cpu.SR >> b & 1);
        fprintf(stderr, "\n");
    }
    int n = 0;
    for (uint32_t a = 0; a < g_amiga_mem_size && n < 8; a++) {
        if (g_amiga_mem[a] == s_risultato[a]) continue;
        uint32_t b = a;
        while (b < g_amiga_mem_size && g_amiga_mem[b] != s_risultato[b] && b - a < 64) b++;
        fprintf(stderr, "  memoria $%06X-$%06X:", a, b - 1);
        for (uint32_t k = a; k < b && k < a + 8; k++) fprintf(stderr, " m%02X/r%02X", s_risultato[k], g_amiga_mem[k]);
        fprintf(stderr, "\n");
        a = b;
        n++;
    }
    fprintf(stderr, "  registri all'ingresso: D0=%08X D1=%08X A0=%06X A1=%06X A4=%06X A7=%06X\n",
            prima->D[0], prima->D[1], prima->A[0], prima->A[1], prima->A[4], prima->A[7]);
    fprintf(stderr, "  musashi, %u istruzioni; le ultime (pc D0 CVZNX prima dell'istruzione):\n", s_istruzioni);
    unsigned da = s_istruzioni > N_TRACCIA ? s_istruzioni - N_TRACCIA : 0;
    for (unsigned k = da; k < s_istruzioni; k++) {
        int t = k % N_TRACCIA;
        fprintf(stderr, "    $%06X D0=%08X %c%c%c%c%c\n", s_traccia[t].pc, s_traccia[t].d0,
                "-C"[s_traccia[t].sr & 1], "-V"[s_traccia[t].sr >> 1 & 1], "-Z"[s_traccia[t].sr >> 2 & 1],
                "-N"[s_traccia[t].sr >> 3 & 1], "-X"[s_traccia[t].sr >> 4 & 1]);
    }
    fflush(stderr);
}

static int aggancio(fn_t fn) {
    if (g_verifica_attiva) return 0;
    voce_t *v = trova(fn);
    if (!v || v->volte >= s_volte) return 0;
    v->volte++;

    /* 1. fotografia */
    M68KState prima = g_cpu;
    uint32_t a7 = g_cpu.A[7] & 0xFFFFFFu;
    if (a7 + 4 > g_amiga_mem_size) return 0;
    memcpy(s_foto, g_amiga_mem, g_amiga_mem_size);
    uint32_t ritorno = (uint32_t)(g_amiga_mem[a7] << 24 | g_amiga_mem[a7 + 1] << 16 |
                                  g_amiga_mem[a7 + 2] << 8 | g_amiga_mem[a7 + 3]);

    /* 2. Musashi, con la sentinella al posto dell'indirizzo di ritorno */
    g_amiga_mem[a7] = (uint8_t)(SENTINELLA >> 24); g_amiga_mem[a7 + 1] = (uint8_t)(SENTINELLA >> 16);
    g_amiga_mem[a7 + 2] = (uint8_t)(SENTINELLA >> 8); g_amiga_mem[a7 + 3] = (uint8_t)SENTINELLA;
    m68k_set_reg(M68K_REG_SR, g_cpu.SR & 0x1F);               /* modo utente */
    for (int i = 0; i < 8; i++) {
        m68k_set_reg((m68k_register_t)(M68K_REG_D0 + i), g_cpu.D[i]);
        m68k_set_reg((m68k_register_t)(M68K_REG_A0 + i), g_cpu.A[i]);
    }
    m68k_set_reg(M68K_REG_PC, v->addr);
    s_stato = CORRE;
    s_istruzioni = 0;
    while (s_stato == CORRE) m68k_execute(100000);
    M68KState mus = g_cpu;
    for (int i = 0; i < 8; i++) {
        mus.D[i] = m68k_get_reg(NULL, (m68k_register_t)(M68K_REG_D0 + i));
        mus.A[i] = m68k_get_reg(NULL, (m68k_register_t)(M68K_REG_A0 + i));
    }
    mus.SR = (uint16_t)m68k_get_reg(NULL, M68K_REG_SR);
    int esito = s_stato;
    if (esito == FINITA) {
        g_amiga_mem[a7] = (uint8_t)(ritorno >> 24); g_amiga_mem[a7 + 1] = (uint8_t)(ritorno >> 16);
        g_amiga_mem[a7 + 2] = (uint8_t)(ritorno >> 8); g_amiga_mem[a7 + 3] = (uint8_t)ritorno;
        memcpy(s_risultato, g_amiga_mem, g_amiga_mem_size);
    }

    /* 3. indietro alla fotografia, e la chiamata vera */
    memcpy(g_amiga_mem, s_foto, g_amiga_mem_size);
    g_cpu = prima;
    if (esito != FINITA) {
        if (esito == SISTEMA) s_sistema++; else if (esito == HW) s_hw++; else s_budget++;
        /* una chiamata al sistema puo' dipendere dagli argomenti: si riprova
         * qualche volta; hardware o troppo lunga: inutile */
        if (esito == SISTEMA && ++v->tentativi < 4) v->volte--;
        else v->volte = s_volte;
        recomp_call_func_direct(fn);
        return 1;
    }
    g_verifica_attiva = 1;
    recomp_call_func_direct(fn);
    g_verifica_attiva = 0;
    /* l'RTS ricompilato lascia l'indirizzo di ritorno in pila: lo toglie il
     * chiamante (A7 += 4 se non e' gia' oltre lo slot); qui si fa lo stesso */
    if ((g_cpu.A[7] & 0xFFFFFFu) != a7 + 4) mus.A[7] -= 4;
    int uguali = memcmp(g_amiga_mem, s_risultato, g_amiga_mem_size) == 0 && !((mus.SR ^ g_cpu.SR) & 0x1F);
    for (int i = 0; i < 8; i++) uguali &= mus.D[i] == g_cpu.D[i] && mus.A[i] == g_cpu.A[i];
    if (uguali) s_ok++;
    else {
        s_sbagliate++;
        if (!v->sbagliata) riporta(v, &prima, &mus);
        v->sbagliata = 1;
    }
    glue_check_vblank();                           /* il quadro rimandato */
    return 1;
}

void amiga_verifica_init(void) {
    const char *e = getenv("AMIGA_VERIFICA");
    if (!e || atoi(e) <= 0) return;
    s_volte = atoi(e);
    s_n_voci = game_dispatch_table_size();
    s_voci = calloc((size_t)s_n_voci, sizeof *s_voci);
    s_foto = malloc(g_amiga_mem_size);
    s_risultato = malloc(g_amiga_mem_size);
    if (!s_voci || !s_foto || !s_risultato) return;
    for (int i = 0; i < s_n_voci; i++) { s_voci[i].fn = game_dispatch_table_fn(i); s_voci[i].addr = game_dispatch_table_addr(i); }
    qsort(s_voci, (size_t)s_n_voci, sizeof *s_voci, per_fn);
    musashi_init();
    g_recomp_call_hook = aggancio;
    atexit(riassunto);
    fprintf(stderr, "[verifica] attiva: %d funzioni, %d prove ciascuna, contro Musashi 68020\n", s_n_voci, s_volte);
}
