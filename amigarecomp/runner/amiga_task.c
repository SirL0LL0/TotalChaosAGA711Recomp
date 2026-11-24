/* amiga_task.c - i task che il programma crea con AddTask, su fiber di
 * Windows (su macOS e Linux un cambio di pila uguale, qui sotto).
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
#ifdef _WIN32
#include <windows.h>
#else
/* le tre chiamate delle fiber di Windows, con un cambio di pila scritto qui
 * (ucontext su macOS e' deprecato, e con i chip Apple il gioco andava in
 * crash nel primo task secondario): si salvano i registri che una funzione
 * deve conservare sulla pila di chi cede, si passa alla pila dell'altro e si
 * riprendono i suoi. Una fiber nuova parte da trampolino, che chiama corpo(arg). */
#ifdef __APPLE__
#define SIMBOLO(n) "_" #n
#else
#define SIMBOLO(n) #n
#endif
void amiga_cambia_pila(void **salva, void *nuova);
void amiga_trampolino(void);
#if defined(__aarch64__)
__asm__(
    ".text\n.p2align 2\n.globl " SIMBOLO(amiga_cambia_pila) "\n" SIMBOLO(amiga_cambia_pila) ":\n"
    "    sub sp, sp, #160\n"
    "    stp x19, x20, [sp, #0]\n    stp x21, x22, [sp, #16]\n    stp x23, x24, [sp, #32]\n"
    "    stp x25, x26, [sp, #48]\n    stp x27, x28, [sp, #64]\n    stp x29, x30, [sp, #80]\n"
    "    stp d8, d9, [sp, #96]\n    stp d10, d11, [sp, #112]\n    stp d12, d13, [sp, #128]\n    stp d14, d15, [sp, #144]\n"
    "    mov x9, sp\n    str x9, [x0]\n    mov sp, x1\n"
    "    ldp x19, x20, [sp, #0]\n    ldp x21, x22, [sp, #16]\n    ldp x23, x24, [sp, #32]\n"
    "    ldp x25, x26, [sp, #48]\n    ldp x27, x28, [sp, #64]\n    ldp x29, x30, [sp, #80]\n"
    "    ldp d8, d9, [sp, #96]\n    ldp d10, d11, [sp, #112]\n    ldp d12, d13, [sp, #128]\n    ldp d14, d15, [sp, #144]\n"
    "    add sp, sp, #160\n    ret\n"
    ".globl " SIMBOLO(amiga_trampolino) "\n" SIMBOLO(amiga_trampolino) ":\n"
    "    mov x0, x20\n    blr x19\n    brk #0\n");
#define REGISTRI 20                      /* parole salvate: x19-x30, d8-d15 */
#define POSTO_INGRESSO 11                /* x30: dove torna ret */
#define POSTO_CORPO 0                    /* x19 */
#define POSTO_ARG 1                      /* x20 */
#define SCARTO 0                         /* dopo add sp la pila e' la cima, allineata */
#elif defined(__x86_64__)
__asm__(
    ".text\n.globl " SIMBOLO(amiga_cambia_pila) "\n" SIMBOLO(amiga_cambia_pila) ":\n"
    "    pushq %rbp\n    pushq %rbx\n    pushq %r12\n    pushq %r13\n    pushq %r14\n    pushq %r15\n"
    "    movq %rsp, (%rdi)\n    movq %rsi, %rsp\n"
    "    popq %r15\n    popq %r14\n    popq %r13\n    popq %r12\n    popq %rbx\n    popq %rbp\n    ret\n"
    ".globl " SIMBOLO(amiga_trampolino) "\n" SIMBOLO(amiga_trampolino) ":\n"
    "    movq %r13, %rdi\n    callq *%r12\n    ud2\n");
#define REGISTRI 7                       /* r15 r14 r13 r12 rbx rbp, poi l'indirizzo di ritorno */
#define POSTO_INGRESSO 6
#define POSTO_CORPO 3                    /* r12 */
#define POSTO_ARG 2                      /* r13 */
#define SCARTO 16                        /* dopo il ret in trampolino rsp allineato a 16 */
#else
#error "cambio di pila: manca questo processore"
#endif

typedef struct { void *sp; } fiber_t;
static fiber_t *s_fiber_corrente;
static void *ConvertThreadToFiber(void *p) {
    (void)p;
    return s_fiber_corrente = calloc(1, sizeof(fiber_t));
}
static void *CreateFiber(size_t pila, void (*corpo)(void *), void *arg) {
    fiber_t *f = calloc(1, sizeof(fiber_t));
    char *base = malloc(pila);
    uintptr_t cima = ((uintptr_t)base + pila) & ~(uintptr_t)15;
    /* sotto la cima i registri che amiga_cambia_pila riprende */
    void **r = (void **)(cima - REGISTRI * sizeof(void *) - SCARTO);
    memset(r, 0, REGISTRI * sizeof(void *));
    r[POSTO_INGRESSO] = (void *)amiga_trampolino;
    r[POSTO_CORPO] = (void *)corpo;
    r[POSTO_ARG] = arg;
    f->sp = r;
    return f;
}
static void SwitchToFiber(void *a) {
    fiber_t *da = s_fiber_corrente;
    s_fiber_corrente = a;
    amiga_cambia_pila(&da->sp, ((fiber_t *)a)->sp);
}
#define __stdcall
#endif

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
