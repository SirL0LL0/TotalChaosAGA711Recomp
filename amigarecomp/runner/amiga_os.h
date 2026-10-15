/* amiga_os.h - AmigaOS (Kickstart 1.3, V34) riscritto in C per i programmi
 * ricompilati che lo usano invece di programmare l'hardware.
 *
 * Il programma chiama le librerie come sull'Amiga: jsr -LVO(a6). Le basi delle
 * librerie stanno in memoria emulata, con la tabella dei salti davanti; il C
 * generato salta all'indirizzo base-LVO, che non e' codice ricompilato, e il
 * dispatcher chiede a recomp_dispatch_ram_stub(): qui si riconosce la
 * libreria e la funzione e la si esegue in C, con gli argomenti nei registri
 * di g_cpu e il risultato in D0, come nell'ABI delle librerie Amiga.
 *
 * Le librerie che il programma porta con se' (libs/) sono invece codice 68000
 * ricompilato con lui: OpenLibrary le inizializza come exec (MakeLibrary,
 * routine d'init, Open) e la loro tabella dei salti contiene JMP veri.
 *
 * Le strutture (Task, MsgPort, Screen, RastPort...) stanno in memoria emulata
 * con gli offset del Kickstart 1.3: il programma le legge e le scrive
 * direttamente.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>

/* ---- memoria emulata --------------------------------------------------- */
extern uint8_t *g_amiga_mem;
extern uint32_t g_amiga_mem_size;
uint8_t  m68k_read8(uint32_t a);
uint16_t m68k_read16(uint32_t a);
uint32_t m68k_read32(uint32_t a);
void     m68k_write8(uint32_t a, uint8_t v);
void     m68k_write16(uint32_t a, uint16_t v);
void     m68k_write32(uint32_t a, uint32_t v);
#define R8(a)     m68k_read8((uint32_t)(a))
#define R16(a)    m68k_read16((uint32_t)(a))
#define R32(a)    m68k_read32((uint32_t)(a))
#define W8(a, v)  m68k_write8((uint32_t)(a), (uint8_t)(v))
#define W16(a, v) m68k_write16((uint32_t)(a), (uint16_t)(v))
#define W32(a, v) m68k_write32((uint32_t)(a), (uint32_t)(v))
#define S16(a)    ((int16_t)R16(a))
#define BADDR(b)  ((uint32_t)(b) << 2)          /* BPTR -> indirizzo */
#define MKBADDR(a) ((uint32_t)(a) >> 2)

/* registri del 68000 (g_cpu, genesis_runtime.h) */
#define D0 g_cpu.D[0]
#define D1 g_cpu.D[1]
#define D2 g_cpu.D[2]
#define D3 g_cpu.D[3]
#define D4 g_cpu.D[4]
#define D5 g_cpu.D[5]
#define D6 g_cpu.D[6]
#define D7 g_cpu.D[7]
#define A0 g_cpu.A[0]
#define A1 g_cpu.A[1]
#define A2 g_cpu.A[2]
#define A3 g_cpu.A[3]
#define A4 g_cpu.A[4]
#define A5 g_cpu.A[5]
#define A6 g_cpu.A[6]

/* mappa: vettori e strutture di sistema in basso, pila sotto il programma */
#define OS_MEM          0x800000u     /* 8 MB: chip fino a OS_FINE_CHIP, poi fast */
#define OS_INIZIO_CHIP  0x001000u
#define OS_FINE_CHIP    0x1E0000u
#define OS_PILA_BASSO   0x1E0000u     /* pila del programma: 124 KB */
#define OS_PILA_ALTO    0x1FF000u

/* AllocMem */
#define MEMF_PUBLIC  0x00001u
#define MEMF_CHIP    0x00002u
#define MEMF_FAST    0x00004u
#define MEMF_CLEAR   0x10000u
#define MEMF_LARGEST 0x20000u

/* ---- strutture di exec: offset ----------------------------------------- */
enum {
    LN_SUCC = 0, LN_PRED = 4, LN_TYPE = 8, LN_PRI = 9, LN_NAME = 10, LN_SIZE = 14,
    LH_HEAD = 0, LH_TAIL = 4, LH_TAILPRED = 8, LH_TYPE = 12, LH_SIZE = 14,
    LIB_FLAGS = 14, LIB_NEGSIZE = 16, LIB_POSSIZE = 18, LIB_VERSION = 20,
    LIB_REVISION = 22, LIB_IDSTRING = 24, LIB_SUM = 28, LIB_OPENCNT = 32, LIB_SIZE = 34,
    MP_FLAGS = 14, MP_SIGBIT = 15, MP_SIGTASK = 16, MP_MSGLIST = 20, MP_SIZE = 34,
    MN_REPLYPORT = 14, MN_LENGTH = 18, MN_SIZE = 20,
    IS_DATA = 14, IS_CODE = 18,
    IO_DEVICE = 20, IO_UNIT = 24, IO_COMMAND = 28, IO_FLAGS = 30, IO_ERROR = 31,
    IO_ACTUAL = 32, IO_LENGTH = 36, IO_DATA = 40, IO_OFFSET = 44,
    TC_FLAGS = 14, TC_STATE = 15, TC_IDNESTCNT = 16, TC_TDNESTCNT = 17,
    TC_SIGALLOC = 18, TC_SIGWAIT = 22, TC_SIGRECVD = 26, TC_SIGEXCEPT = 30,
    TC_SPREG = 54, TC_SPLOWER = 58, TC_SPUPPER = 62, TC_MEMENTRY = 74,
    TC_USERDATA = 88, TC_SIZE = 92,
    PR_MSGPORT = 92, PR_SEGLIST = 128, PR_STACKSIZE = 132, PR_TASKNUM = 140,
    PR_STACKBASE = 144, PR_RESULT2 = 148, PR_CURRENTDIR = 152, PR_CIS = 156,
    PR_COS = 160, PR_CONSOLETASK = 164, PR_FILESYSTEMTASK = 168, PR_CLI = 172,
    PR_WINDOWPTR = 184, PR_SIZE = 188,
    CLI_COMMANDNAME = 16, CLI_DEFAULTSTACK = 52, CLI_SIZE = 64,
    /* ExecBase */
    EB_THISTASK = 276, EB_ATTNFLAGS = 296, EB_MEMLIST = 322, EB_RESOURCELIST = 336,
    EB_DEVICELIST = 350, EB_INTRLIST = 364, EB_LIBLIST = 378, EB_PORTLIST = 392,
    EB_TASKREADY = 406, EB_TASKWAIT = 420, EB_VBLANKFREQ = 530, EB_POWERFREQ = 531,
    EB_SEMAPHORELIST = 532, EB_SIZE = 632,
};
enum { NT_TASK = 1, NT_INTERRUPT = 2, NT_DEVICE = 3, NT_MSGPORT = 4, NT_MESSAGE = 5,
       NT_FREEMSG = 6, NT_REPLYMSG = 7, NT_RESOURCE = 8, NT_LIBRARY = 9, NT_PROCESS = 13,
       NT_SEMAPHORE = 15 };

/* ---- librerie ----------------------------------------------------------- */
typedef void (*os_funzione)(void);

/* una libreria in C: nomi[i] e funzioni[i] per LVO = -6 * (i + 1); le prime
 * quattro (Open, Close, Expunge, riservata) le gestisce il nucleo se NULL */
typedef struct {
    const char *nome;
    int versione;
    int n;                              /* voci della tabella dei salti */
    int dim_base;                       /* parte positiva della base */
    const char *const *nomi;            /* per i messaggi di debug, puo' avere NULL */
    const os_funzione *funzioni;
} os_libreria_c;

/* crea la base in memoria (tabella dei salti + Library) e la registra */
uint32_t os_crea_libreria(const os_libreria_c *def, uint8_t tipo);
/* sostituisce in C la funzione LVO di una libreria del programma (libs/) */
void os_sostituisci(const char *libreria, int lvo, os_funzione f);
/* una libreria del programma gia' in memoria (game/prepara_immagine.py) */
void os_libreria_disco(const char *nome, uint32_t seglist, uint32_t dati,
                       uint32_t vettori, uint32_t struttura, uint32_t init);
uint32_t os_apri_libreria(const char *nome, uint32_t versione);
uint32_t os_base(const char *nome);    /* base di una libreria gia' creata, 0 = no */

/* ---- nucleo ------------------------------------------------------------- */
extern uint32_t os_sysbase;            /* ExecBase */
extern uint32_t os_task;               /* il task in esecuzione (Process del programma) */

void     os_init(void);
/* prepara il Process della CLI e i registri per partire dall'ingresso del
 * programma con gli argomenti della riga di comando */
void     os_prepara_avvio(uint32_t seglist, const char *argomenti, const char *nome_comando);

uint32_t os_alloca(uint32_t dim, uint32_t flag);   /* AllocMem; 0 = memoria finita */
void     os_libera(uint32_t ind, uint32_t dim);
uint32_t os_disponibile(uint32_t flag);
uint32_t os_stringa(const char *s);                /* copia una stringa C in memoria (allocata) */
void     os_leggi_stringa(uint32_t ind, char *buf, size_t n);

/* liste in memoria emulata */
void     os_lista_init(uint32_t l);
void     os_aggiungi_coda(uint32_t l, uint32_t n);
void     os_aggiungi_testa(uint32_t l, uint32_t n);
void     os_togli(uint32_t n);
uint32_t os_togli_testa(uint32_t l);
void     os_accoda_pri(uint32_t l, uint32_t n);    /* Enqueue */
uint32_t os_trova_nome(uint32_t l, const char *nome);
int      os_lista_vuota(uint32_t l);

/* segnali e messaggi */
void     os_segnala(uint32_t task, uint32_t segnali);
uint32_t os_attendi(uint32_t maschera);            /* Wait: fa girare i quadri */
void     os_metti_msg(uint32_t porta, uint32_t msg);
uint32_t os_prendi_msg(uint32_t porta);
void     os_rispondi_msg(uint32_t msg);
uint32_t os_crea_porta(void);                       /* porta con un segnale del task */

/* chiamare codice 68000 (gestori di interrupt, routine d'init): i registri
 * sono quelli di g_cpu al momento della chiamata */
void     os_chiama_68k(uint32_t ind);
/* lo stesso salvando e ripristinando tutti i registri (interrupt) */
void     os_chiama_68k_isolato(uint32_t ind);

/* il runner: un quadro (ingressi, interrupt, video, 50 Hz) */
extern void (*os_quadro)(void);
uint64_t os_microsecondi(void);                      /* tempo emulato */

/* le funzioni che mancano: si scrivono una volta su stderr */
void     os_manca(const char *libreria, const char *funzione, int lvo);
extern int os_traccia;                               /* AMIGA_TRACCIA_OS=1: ogni chiamata */

/* librerie in C (amiga_exec.c, amiga_dos.c, ...) */
void     os_exec_init(void);
void     os_dos_init(const char *cartella_dati);
void     os_dos_assegna(const char *volume, const char *cartella);   /* "ChaosGame" -> cartella */
void     os_dos_volume(const char *nome);    /* nome Amiga della cartella dei dati ("Chaos") */
uint32_t os_dos_carica_segmenti(const char *percorso_amiga);          /* LoadSeg; BPTR */
void     os_grafica_init(void);
void     os_intuition_init(void);
void     os_device_init(void);
