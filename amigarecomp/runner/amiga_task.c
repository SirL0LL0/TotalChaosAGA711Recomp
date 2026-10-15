/* amiga_task.c - i task che il programma crea con AddTask, su fiber di
 * Windows.
 *
 * Ogni task Amiga ha una fiber (una pila C sua) e il suo contesto: registri
 * 68000, pila ombra delle JSR, g_rte_pending, g_split_sp_popped, la catena
 * delle chiamate in coda del dispatcher. Il cambio e' cooperativo, come
 * conviene a codice ricompilato: quando un task aspetta (Wait) senza segnali
 * pronti si passa al prossimo task pronto; se nessuno lo e', il runner fa
 * girare un quadro (ingressi, interrupt, timer), che puo' svegliarne qualcuno.
 * Il programma principale e' il task 0 (la fiber del thread).
 */
#include "amiga_os.h"
#include "genesis_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

void *glue_pila_ombra_nuova(void);
void *glue_pila_ombra(void *nuova);
int  *glue_rte_pending(void);

#define MAX_TASK 16
typedef struct {
    uint32_t task;                /* la struttura Task in memoria emulata */
    void *fiber;
    M68KState cpu;
    void *pila_ombra;
    int rte_pending, split_sp;
    void *coda;                   /* recomp_tail_frame */
    uint32_t pc, finale;
    int vivo;
    uint64_t quadro;              /* aspetta il quadro (WaitTOF, Delay), 0 = no */
} contesto_t;

extern uint64_t g_os_quadri;

static contesto_t s_t[MAX_TASK];
static int s_n, s_corrente;

static contesto_t *ctx_di(uint32_t task) {
    for (int i = 0; i < s_n; i++) if (s_t[i].task == task && s_t[i].vivo) return &s_t[i];
    return NULL;
}

static void salva(contesto_t *c) {
    c->cpu = g_cpu;
    c->pila_ombra = glue_pila_ombra(NULL);
    c->rte_pending = *glue_rte_pending();
    c->split_sp = g_split_sp_popped;
    c->coda = recomp_tail_frame_get();
}
static void ripristina(contesto_t *c) {
    g_cpu = c->cpu;
    glue_pila_ombra(c->pila_ombra);
    *glue_rte_pending() = c->rte_pending;
    g_split_sp_popped = c->split_sp;
    recomp_tail_frame_set(c->coda);
    os_task = c->task;
    W32(os_sysbase + EB_THISTASK, c->task);
}

static void passa_a(int i) {
    if (i == s_corrente) return;
    contesto_t *da = &s_t[s_corrente];
    if (da->vivo) salva(da);
    s_corrente = i;
    ripristina(&s_t[i]);
    SwitchToFiber(s_t[i].fiber);
    /* qui si torna quando qualcuno ripassa a questo task: il contesto lo ha
     * gia' rimesso chi ha fatto il cambio */
}

/* un task e' pronto se ha ricevuto un segnale che aspetta, o se non aspetta */
static int pronto(contesto_t *c) {
    if (!c->vivo) return 0;
    if (c->quadro) return g_os_quadri >= c->quadro;
    uint8_t stato = R8(c->task + TC_STATE);
    if (stato != 4) return 1;                                   /* TS_WAIT = 4 */
    return (R32(c->task + TC_SIGRECVD) & R32(c->task + TC_SIGWAIT)) != 0;
}

static void __stdcall corpo_fiber(void *p) {
    contesto_t *c = (contesto_t *)p;
    /* registri: come exec, A7 = tc_SPReg; nessun argomento */
    memset(&g_cpu, 0, sizeof g_cpu);
    g_cpu.A[7] = R32(c->task + TC_SPREG);
    os_chiama_68k(c->pc);
    /* il task e' finito: la routine finale, poi via per sempre */
    if (c->finale) os_chiama_68k(c->finale);
    c->vivo = 0;
    os_togli(c->task);
    for (;;) {
        int i;
        for (i = 0; i < s_n && !(s_t[i].vivo && i != (int)(c - s_t)); i++) {}
        if (i < s_n) {
            s_corrente = i;
            ripristina(&s_t[i]);
            SwitchToFiber(s_t[i].fiber);
        }
    }
}

static void init_principale(void) {
    if (s_n) return;
    s_t[0].task = os_task;
    s_t[0].fiber = ConvertThreadToFiber(NULL);
    s_t[0].vivo = 1;
    s_n = 1;
    s_corrente = 0;
}

void os_task_aggiungi(uint32_t task, uint32_t pc, uint32_t finale) {
    init_principale();
    if (s_n >= MAX_TASK) { fprintf(stderr, "[os] troppi task\n"); return; }
    contesto_t *c = &s_t[s_n++];
    memset(c, 0, sizeof *c);
    c->task = task; c->pc = pc; c->finale = finale; c->vivo = 1;
    c->pila_ombra = glue_pila_ombra_nuova();
    c->fiber = CreateFiber(1 << 20, corpo_fiber, c);
    W8(task + LN_TYPE, NT_TASK);
    W8(task + TC_STATE, 3);                                    /* TS_READY */
    if (!R32(task + TC_SIGALLOC)) W32(task + TC_SIGALLOC, 0xFFFF);
    os_accoda_pri(os_sysbase + EB_TASKREADY, task);
    char nome[64];
    os_leggi_stringa(R32(task + LN_NAME), nome, sizeof nome);
    if (os_traccia) fprintf(stderr, "[os] AddTask(\"%s\") ingresso $%06X pila $%06X\n", nome, pc, R32(task + TC_SPREG));
    /* exec passa subito al nuovo task se ha priorita' piu' alta: qui lo si
     * fa partire sempre, fino al suo primo Wait */
    W8(os_task + TC_STATE, 3);
    passa_a(s_n - 1);
    W8(os_task + TC_STATE, 2);
}

void os_task_togli(uint32_t task) {
    init_principale();
    if (task == s_t[0].task) {
        fprintf(stderr, "[os] RemTask del programma: fine\n");
        exit(0);
    }
    contesto_t *c = ctx_di(task);
    if (!c) { os_togli(task); return; }
    os_togli(task);
    if (c == &s_t[s_corrente]) {
        /* si toglie da solo: non torna piu' */
        c->vivo = 0;
        for (int i = 0; i < s_n; i++) if (s_t[i].vivo) { s_corrente = i; ripristina(&s_t[i]); SwitchToFiber(s_t[i].fiber); }
    }
    c->vivo = 0;
}

/* Wait del task corrente: si cede agli altri task pronti, altrimenti un quadro */
int os_attesa_breve(void);                                          /* amiga_glue.c */

uint32_t os_task_attendi(uint32_t maschera) {
    uint32_t t = os_task;
    for (;;) {
        uint32_t r = R32(t + TC_SIGRECVD) & maschera;
        if (r) {
            W32(t + TC_SIGRECVD, R32(t + TC_SIGRECVD) & ~r);
            W8(t + TC_STATE, 2);                                    /* TS_RUN */
            return r;
        }
        W32(t + TC_SIGWAIT, maschera);
        W8(t + TC_STATE, 4);                                        /* TS_WAIT */
        int altro = -1;
        for (int k = 1; k <= s_n; k++) {
            int i = (s_corrente + k) % (s_n ? s_n : 1);
            if (s_n && i != s_corrente && pronto(&s_t[i]) && (i == 0 || !s_t[i].quadro)) { altro = i; break; }
        }
        if (altro >= 0) passa_a(altro);
        else if (s_corrente != 0 && s_n) passa_a(0);                /* il principale fa i quadri */
        else if (os_attesa_breve()) {}                              /* un pezzo di quadro (amiga_glue.c) */
        else if (os_quadro) os_quadro();
    }
}

/* il task in esecuzione non e' il programma principale */
int os_task_secondario(void) { return s_n && s_corrente != 0; }

/* aspetta il prossimo quadro (WaitTOF, un tick di Delay, il quadro di CPU
 * finito): il principale lo fa girare, un task secondario cede il posto al
 * principale, che lo rimette in moto al quadro dopo (os_task_quadro) */
void os_task_attendi_quadro(void) {
    if (!os_task_secondario()) { if (os_quadro) os_quadro(); return; }
    contesto_t *c = &s_t[s_corrente];
    c->quadro = g_os_quadri + 1;
    passa_a(0);
    c->quadro = 0;
}

/* a ogni quadro (fuori dai Wait) i task pronti girano fino al loro Wait */
void os_task_quadro(void) {
    if (s_n < 2 || s_corrente != 0) return;
    for (int i = 1; i < s_n; i++) {
        if (pronto(&s_t[i])) {
            uint32_t t = os_task;
            W8(t + TC_STATE, 3);
            passa_a(i);
            W8(t + TC_STATE, 2);
        }
    }
}
