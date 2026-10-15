/* amiga_device.c - i device e le risorse di AmigaOS che i programmi aprono:
 *   timer.device   TR_ADDREQUEST (risposta allo scadere, tempo emulato),
 *                  TR_GETSYSTIME
 *   audio.device   allocazione dei canali, CMD_WRITE in coda per canale
 *                  (risposta a note finita), PERVOL, FINISH, FLUSH, LOCK...
 *   input.device   accettato, i comandi non fanno niente
 *   narrator.device  quello del disco se c'e' (devs/, ricompilato col gioco),
 *                  altrimenti i fonemi detti dalla voce di Windows (amiga_voce.c)
 *   ciaa/ciab.resource  AddICRVector: i gestori del timer CIA-B li chiama
 *                  os_device_quadro() al ritmo programmato nei registri
 */
#include "amiga_os.h"
#include "amiga_hw.h"
#include "amiga_paula.h"
#include "amiga_voce.h"
#include "amiga_debug.h"
#include "genesis_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IOF_QUICK 1
#define CMD_RESET 1
#define CMD_READ 2
#define CMD_WRITE 3
#define CMD_UPDATE 4
#define CMD_CLEAR 5
#define CMD_STOP 6
#define CMD_START 7
#define CMD_FLUSH 8
#define TR_ADDREQUEST 9
#define TR_GETSYSTIME 10
#define ADCMD_FREE 9
#define ADCMD_SETPREC 10
#define ADCMD_FINISH 11
#define ADCMD_PERVOL 12
#define ADCMD_LOCK 13
#define ADCMD_WAITCYCLE 14
#define ADCMD_ALLOCATE 32
#define IOERR_OPENFAIL (-1)

static uint32_t s_timer, s_audio, s_input, s_narratore, s_ciaa, s_ciab;

/* completa una richiesta: se era "veloce" basta il flag, altrimenti risposta */
static void completa(uint32_t io, int8_t errore) {
    W8(io + IO_ERROR, (uint8_t)errore);
    if (R8(io + IO_FLAGS) & IOF_QUICK) return;
    os_rispondi_msg(io);
}

/* la richiesta resta in sospeso (risposta dopo): niente IOF_QUICK e
 * ln_Type = NT_MESSAGE, come fa il device vero. Chi chiama BeginIO da solo
 * (il narrator con audio.device) riusa richieste gia' risposte, ancora
 * NT_REPLYMSG: senza questo WaitIO le darebbe per finite subito */
static void rimanda(uint32_t io) {
    W8(io + IO_FLAGS, R8(io + IO_FLAGS) & ~IOF_QUICK);
    W8(io + LN_TYPE, NT_MESSAGE);
}

/* ---------------------------------------------------------- timer.device */

typedef struct { uint32_t io; uint64_t scade; } attesa_t;
static attesa_t s_attese[64];
static int s_n_attese;

static void timer_beginio(void) {
    uint32_t io = A1;
    switch (R16(io + IO_COMMAND)) {
    case TR_ADDREQUEST: {
        uint64_t us = (uint64_t)R32(io + 32) * 1000000ull + R32(io + 36);
        rimanda(io);   /* risponde dopo */
        if (s_n_attese < 64) s_attese[s_n_attese++] = (attesa_t){ io, os_microsecondi() + us };
        else completa(io, 0);
        break;
    }
    case TR_GETSYSTIME: {
        /* l'ora del PC alla partenza, poi il tempo emulato (le differenze
         * restano quelle dei quadri, anche con Tab) */
        extern long os_secondi_amiga(void);
        static uint64_t base;
        if (!base) base = (uint64_t)os_secondi_amiga() * 1000000ull - os_microsecondi();
        uint64_t t = base + os_microsecondi();
        W32(io + 32, (uint32_t)(t / 1000000ull));
        W32(io + 36, (uint32_t)(t % 1000000ull));
        completa(io, 0);
        break;
    }
    default: completa(io, 0);
    }
}
static void timer_abortio(void) {
    for (int i = 0; i < s_n_attese; i++)
        if (s_attese[i].io == A1) {
            s_attese[i] = s_attese[--s_n_attese];
            W8(A1 + IO_ERROR, (uint8_t)-2);                  /* IOERR_ABORTED */
            os_rispondi_msg(A1);
            break;
        }
}

/* ---------------------------------------------------------- audio.device */

/* Ogni canale ha la sua coda di CMD_WRITE: la testa suona su Paula, le altre
 * aspettano. A ogni fine blocco (amiga_paula_fine_blocco) si conta un ciclo;
 * finiti i cicli (ioa_Cycles, 0 = per sempre) la richiesta e' fatta e la
 * prossima della coda entra subito, prima che Paula ricarichi: cosi' due
 * richieste in coda suonano senza buchi come sul vero audio.device. Le
 * risposte si mandano in os_device_quadro, fuori dalla generazione audio. */
#define ADIOF_PERVOL 0x10
#define IOERR_ABORTED (-2)
#define CODA_AUDIO 32

typedef struct {
    uint32_t coda[CODA_AUDIO];
    int n;
    unsigned cicli;            /* cicli ancora da suonare della testa (0 = per sempre) */
    uint16_t per, vol;
    int fermo;                 /* CMD_STOP */
    uint32_t aspetta_ciclo;    /* ADCMD_WAITCYCLE in attesa */
    uint32_t blocco;           /* ADCMD_LOCK in attesa */
    uint16_t chiave;           /* ioa_AllocKey di chi lo ha allocato, 0 = libero */
    int8_t pri;                /* la sua priorita' (ln_Pri della richiesta) */
    uint32_t porta;            /* la porta di risposta della sua richiesta: il cliente */
    uint32_t richiesta;        /* la richiesta che lo ha allocato */
} canale_audio_t;

#define MN_REPLYPORT 14

#define LN_PRI 9
#define ADIOERR_NOALLOCATION (-4)
#define ADIOERR_ALLOCFAILED (-10)
#define ADIOERR_CHANNELSTOLEN (-11)

static canale_audio_t s_ca[4];
static uint16_t s_ultima_chiave;
static uint32_t s_da_rispondere[64];
static int s_n_da_rispondere;

static void rispondi_dopo(uint32_t io, int8_t errore) {
    W8(io + IO_ERROR, (uint8_t)errore);
    if (s_n_da_rispondere < 64) s_da_rispondere[s_n_da_rispondere++] = io;
}

/* mette su Paula la testa della coda del canale (o lo spegne se vuota) */
static void suona_testa(int c, int accendi) {
    canale_audio_t *ca = &s_ca[c];
    if (!ca->n) { amiga_paula_spegni(c); return; }
    uint32_t io = ca->coda[0];
    if ((R8(io + IO_FLAGS) & ADIOF_PERVOL) || !ca->per) { ca->per = R16(io + 42); ca->vol = R16(io + 44); }
    uint32_t len = R32(io + 38) / 2;
    if (!len) len = 1;
    if (len > 0xFFFF) len = 0xFFFF;
    ca->cicli = R16(io + 46);
    amiga_paula_imposta(c, R32(io + 34), (uint16_t)len, ca->per, ca->vol);
    if (accendi && !ca->fermo) amiga_paula_accendi(c);
}

static void togli_testa(int c) {
    canale_audio_t *ca = &s_ca[c];
    if (!ca->n) return;
    memmove(ca->coda, ca->coda + 1, (size_t)(ca->n - 1) * sizeof ca->coda[0]);
    ca->n--;
}

static void audio_fine_blocco(int c) {
    canale_audio_t *ca = &s_ca[c];
    if (ca->aspetta_ciclo) { rispondi_dopo(ca->aspetta_ciclo, 0); ca->aspetta_ciclo = 0; }
    if (!ca->n) return;                            /* canale suonato dai registri, non da noi */
    if (ca->cicli == 0) return;                    /* per sempre */
    if (--ca->cicli) return;
    rispondi_dopo(ca->coda[0], 0);
    togli_testa(c);
    suona_testa(c, 0);                             /* Paula ricarica i nuovi LC/LEN */
}

/* svuota la coda del canale rispondendo "interrotta" a tutte */
static void svuota(int c) {
    canale_audio_t *ca = &s_ca[c];
    for (int i = 0; i < ca->n; i++) rispondi_dopo(ca->coda[i], IOERR_ABORTED);
    ca->n = 0;
    if (ca->aspetta_ciclo) { rispondi_dopo(ca->aspetta_ciclo, 0); ca->aspetta_ciclo = 0; }
    amiga_paula_spegni(c);
}

void amiga_hw_audio_adesso(void);                   /* amiga_hw.c */

/* le note finite (risposte rimandate fuori dalla generazione dell'audio):
 * a ogni quadro e a ogni passo breve di un'attesa */
void os_device_risposte_audio(void) {
    for (int i = 0; i < s_n_da_rispondere; i++) os_rispondi_msg(s_da_rispondere[i]);
    s_n_da_rispondere = 0;
}

static void audio_beginio(void) {
    amiga_hw_audio_adesso();                        /* i canali cambiano adesso, non a inizio quadro */
    uint32_t io = A1;
    uint32_t unita = R32(io + IO_UNIT) & 15;
    uint16_t cmd = R16(io + IO_COMMAND);
    if (cmd == ADCMD_ALLOCATE) {
        /* ioa_Data: le combinazioni di canali richieste, ioa_Length quante.
         * Una combinazione va bene se i suoi canali sono liberi o di chi ha
         * priorita' piu' bassa (ln_Pri): a quello vengono rubati, le sue note
         * si interrompono e il suo ADCMD_LOCK risponde CHANNELSTOLEN. Cosi' il
         * narrator parla anche sopra la musica, come sull'Amiga */
        uint32_t dati = R32(io + 34), n = R32(io + 38);
        int8_t pri = (int8_t)R8(io + LN_PRI);
        if (os_traccia) {
            fprintf(stderr, " [ALLOCATE pri %d chiave %u, combinazioni", pri, R16(io + 32));
            for (uint32_t i = 0; i < n && i < 16; i++) fprintf(stderr, " %X", R8(dati + i) & 15);
            fprintf(stderr, "; canali:");
            for (int c = 0; c < 4; c++) fprintf(stderr, " %d=%u/%d", c, s_ca[c].chiave, s_ca[c].pri);
            fprintf(stderr, "]");
        }
        for (uint32_t i = 0; i < n; i++) {
            uint8_t m = R8(dati + i) & 15;
            int ok = 1;
            /* gia' suoi (stessa chiave e stessa richiesta): si riallocano. Una
             * richiesta diversa con la stessa chiave no: il NotePlayer, dopo
             * New Game, rialloca i quattro canali con quattro richieste che
             * hanno tutte la chiave rimasta dalla prima volta, e prima
             * prendevano tutte il canale 3 */
            uint16_t sua = R16(io + 32);
            for (int c = 0; c < 4; c++)
                if ((m >> c & 1) && s_ca[c].chiave && !(sua && s_ca[c].chiave == sua && s_ca[c].richiesta == io)
                    && s_ca[c].pri >= pri) ok = 0;
            if (!ok) continue;
            uint16_t chiave = R16(io + 32);
            if (!chiave) { if (!++s_ultima_chiave) s_ultima_chiave = 1; chiave = s_ultima_chiave; }
            for (int c = 0; c < 4; c++) {
                if (!(m >> c & 1)) continue;
                if (s_ca[c].chiave && s_ca[c].chiave != chiave) {
                    svuota(c);
                    if (s_ca[c].blocco) { rispondi_dopo(s_ca[c].blocco, ADIOERR_CHANNELSTOLEN); s_ca[c].blocco = 0; }
                    s_ca[c].per = 0;
                }
                s_ca[c].chiave = chiave;
                s_ca[c].pri = pri;
                s_ca[c].porta = R32(io + MN_REPLYPORT);
                s_ca[c].richiesta = io;
            }
            W32(io + IO_UNIT, m);
            W16(io + 32, chiave);                              /* ioa_AllocKey */
            completa(io, 0);
            return;
        }
        completa(io, ADIOERR_ALLOCFAILED);
        return;
    }
    /* gli altri comandi valgono sui canali liberi, su quelli della propria
     * chiave o dello stesso cliente (porta di risposta), e su quelli di chi
     * ha priorita' almeno pari: il NotePlayer del gioco usa le sue richieste
     * (chiavi diverse, tutte a 127) una al posto dell'altra, e libera tutto
     * con la prima. Musica ed effetti non toccano i canali del narrator (75),
     * che li ha presi a priorita' piu' bassa: un canale rubato e' suo */
    {
        uint16_t chiave = R16(io + 32);
        uint32_t porta = R32(io + MN_REPLYPORT);
        int8_t pri = (int8_t)R8(io + LN_PRI);
        uint32_t mie = 0;
        for (int c = 0; c < 4; c++) {
            if (!(unita >> c & 1)) continue;
            if (!s_ca[c].chiave || (chiave && s_ca[c].chiave == chiave) || (porta && s_ca[c].porta == porta)
                || s_ca[c].pri >= pri)
                mie |= 1u << c;
        }
        if (!mie) {
            if (os_traccia) fprintf(stderr, " [audio: comando %u senza canali (chiave %u, unita %X)]", cmd, chiave, unita);
            completa(io, ADIOERR_NOALLOCATION);
            return;
        }
        unita = mie;
    }
    switch (cmd) {
    case ADCMD_FREE:
        for (int c = 0; c < 4; c++)
            if (unita >> c & 1) {
                svuota(c);
                if (s_ca[c].blocco) { rispondi_dopo(s_ca[c].blocco, 0); s_ca[c].blocco = 0; }
                s_ca[c].chiave = 0;
            }
        completa(io, 0);
        return;
    case ADCMD_SETPREC:
        for (int c = 0; c < 4; c++)
            if (unita >> c & 1) s_ca[c].pri = (int8_t)R8(io + LN_PRI);
        completa(io, 0);
        return;
    case CMD_WRITE: {
        /* va sul primo canale dell'unita'; risponde quando ha finito di suonare */
        int c = 0;
        while (c < 4 && !(unita >> c & 1)) c++;
        if (os_traccia)
            fprintf(stderr, " [CMD_WRITE unita %X flag %02X dati $%06X lung %u per %u vol %u cicli %u coda %d]",
                    unita, R8(io + IO_FLAGS), R32(io + 34), R32(io + 38), R16(io + 42), R16(io + 44), R16(io + 46),
                    c < 4 ? s_ca[c].n : -1);
        if (c == 4 || s_ca[c].n >= CODA_AUDIO) { completa(io, -4); return; }   /* ADIOERR_NOALLOCATION */
        rimanda(io);
        W8(io + IO_ERROR, 0);
        s_ca[c].coda[s_ca[c].n++] = io;
        if (s_ca[c].n == 1) suona_testa(c, 1);
        return;
    }
    case ADCMD_PERVOL:
        for (int c = 0; c < 4; c++)
            if (unita >> c & 1) {
                s_ca[c].per = R16(io + 42); s_ca[c].vol = R16(io + 44);
                amiga_paula_per_vol(c, s_ca[c].per, s_ca[c].vol);
            }
        completa(io, 0);
        return;
    case ADCMD_FINISH:
        /* la nota corrente finisce alla fine del ciclo che sta suonando */
        for (int c = 0; c < 4; c++)
            if ((unita >> c & 1) && s_ca[c].n) s_ca[c].cicli = 1;
        completa(io, 0);
        return;
    case CMD_FLUSH:
    case CMD_RESET:
        for (int c = 0; c < 4; c++)
            if (unita >> c & 1) { svuota(c); s_ca[c].fermo = 0; }
        completa(io, 0);
        return;
    case CMD_STOP:
        for (int c = 0; c < 4; c++)
            if (unita >> c & 1) { s_ca[c].fermo = 1; amiga_paula_spegni(c); }
        completa(io, 0);
        return;
    case CMD_START:
        for (int c = 0; c < 4; c++)
            if ((unita >> c & 1) && s_ca[c].fermo) {
                s_ca[c].fermo = 0;
                if (s_ca[c].n) amiga_paula_accendi(c);
            }
        completa(io, 0);
        return;
    case ADCMD_WAITCYCLE: {
        int c = 0;
        while (c < 4 && !(unita >> c & 1)) c++;
        if (c == 4 || !s_ca[c].n || s_ca[c].aspetta_ciclo) { completa(io, 0); return; }
        rimanda(io);
        s_ca[c].aspetta_ciclo = io;
        return;
    }
    case ADCMD_LOCK: {
        /* resta in sospeso finche' i canali sono nostri */
        int c = 0;
        while (c < 4 && !(unita >> c & 1)) c++;
        rimanda(io);
        W8(io + IO_ERROR, 0);
        if (c < 4 && !s_ca[c].blocco) s_ca[c].blocco = io;
        else completa(io, 0);
        return;
    }
    default:
        completa(io, 0);
    }
}

/* CloseDevice: i canali ancora allocati con la sua chiave tornano liberi */
static void audio_close(void) {
    uint16_t chiave = R16(A1 + 32);
    for (int c = 0; c < 4; c++)
        if (chiave && s_ca[c].chiave == chiave) {
            svuota(c);
            if (s_ca[c].blocco) { rispondi_dopo(s_ca[c].blocco, 0); s_ca[c].blocco = 0; }
            s_ca[c].chiave = 0;
        }
}

static void audio_abortio(void) {
    uint32_t io = A1;
    for (int c = 0; c < 4; c++) {
        canale_audio_t *ca = &s_ca[c];
        for (int i = 0; i < ca->n; i++) {
            if (ca->coda[i] != io) continue;
            memmove(ca->coda + i, ca->coda + i + 1, (size_t)(ca->n - i - 1) * sizeof ca->coda[0]);
            ca->n--;
            if (i == 0) suona_testa(c, 0);                 /* la prossima, o silenzio */
            W8(io + IO_ERROR, (uint8_t)IOERR_ABORTED);
            os_rispondi_msg(io);
            return;
        }
        if (ca->aspetta_ciclo == io || ca->blocco == io) {
            if (ca->aspetta_ciclo == io) ca->aspetta_ciclo = 0; else ca->blocco = 0;
            W8(io + IO_ERROR, (uint8_t)IOERR_ABORTED);
            os_rispondi_msg(io);
            return;
        }
    }
}

/* ---------------------------------------------------------- narrator.device */

/* CMD_WRITE: io_Data i fonemi, io_Length quanti; narrator_rb: rate +48,
 * sex +54 (1 = voce femminile). Risponde quando la voce ha finito. */
static uint32_t s_narr_io;

static void narratore_beginio(void) {
    uint32_t io = A1;
    switch (R16(io + IO_COMMAND)) {
    case CMD_WRITE: {
        static char fonemi[1024];
        uint32_t dati = R32(io + 40), n = R32(io + 36);
        if (n >= sizeof fonemi) n = sizeof fonemi - 1;
        for (uint32_t i = 0; i < n; i++) fonemi[i] = (char)R8(dati + i);
        fonemi[n] = 0;
        W32(io + 32, n);                                     /* io_Actual */
        if (s_narr_io) { completa(s_narr_io, 0); s_narr_io = 0; }
        if (voce_parla(fonemi, (int)n, R16(io + 48), R16(io + 54) == 1)) {
            rimanda(io);
            W8(io + IO_ERROR, 0);
            s_narr_io = io;
        } else completa(io, 0);
        return;
    }
    case CMD_FLUSH:
    case CMD_RESET:
        if (s_narr_io) { voce_ferma(); completa(s_narr_io, IOERR_ABORTED); s_narr_io = 0; }
        completa(io, 0);
        return;
    default:
        completa(io, 0);                                     /* CMD_READ (bocche) e il resto */
    }
}
static void narratore_abortio(void) {
    if (A1 != s_narr_io) return;
    voce_ferma();
    W8(A1 + IO_ERROR, (uint8_t)IOERR_ABORTED);
    os_rispondi_msg(A1);
    s_narr_io = 0;
}

/* ---------------------------------------------------------- input.device */

static void niente_beginio(void) { completa(A1, 0); }
static void niente_abortio(void) {}
static void device_close(void) {}

/* ---------------------------------------------------------- risorse CIA */

typedef struct { uint32_t is; int bit; } icr_t;
static icr_t s_icr_b[5], s_icr_a[5];
static uint8_t s_abilitati_b = 0x1F;

static void ciab_addicrvector(void) {
    int b = (int)D0 & 7;
    if (b < 5 && s_icr_b[b].is) { D0 = s_icr_b[b].is; return; }      /* gia' preso */
    if (b < 5) s_icr_b[b] = (icr_t){ A1, b };
    D0 = 0;
}
static void ciab_remicrvector(void) { int b = (int)D0 & 7; if (b < 5) s_icr_b[b].is = 0; }
static void ciab_ableicr(void) {
    uint8_t m = (uint8_t)D0, vecchio = s_abilitati_b;
    if (m & 0x80) s_abilitati_b |= m & 0x7F; else s_abilitati_b &= (uint8_t)~m;
    D0 = vecchio;
}
static void ciab_seticr(void) { D0 = 0; }
static void ciaa_addicrvector(void) {
    int b = (int)D0 & 7;
    if (b < 5 && s_icr_a[b].is) { D0 = s_icr_a[b].is; return; }
    if (b < 5) s_icr_a[b] = (icr_t){ A1, b };
    if (b < 5) amiga_hw_ciaa_ableicr((uint8_t)(0x80 | 1u << b));   /* come la risorsa: si abilita */
    D0 = 0;
}
static void ciaa_remicrvector(void) { int b = (int)D0 & 7; if (b < 5) s_icr_a[b].is = 0; }
static void ciaa_ableicr(void) { D0 = amiga_hw_ciaa_ableicr((uint8_t)D0); }

static void chiama_icr(uint32_t is) {
    M68KState s = g_cpu;
    g_cpu.A[1] = R32(is + IS_DATA);
    g_cpu.A[5] = R32(is + IS_CODE);
    g_cpu.A[6] = os_sysbase;
    os_chiama_68k(R32(is + IS_CODE));
    g_cpu = s;
}

/* ---------------------------------------------------------- a ogni quadro */

/* ---------------------------------------------------------- suoni dal C */

/* I comandi JCsound e JCspeech di Total Chaos (amiga_dos.c) suonano un
 * campione 8SVX con audio.device: qui lo stesso senza un processo. Si chiede
 * una combinazione di canali (ADCMD_ALLOCATE, che rispetta la priorita' della
 * musica), si manda un CMD_WRITE per canale (a pezzi da 128 KB) e a ogni
 * quadro si guarda se sono finiti tutti: allora ADCMD_FREE e via la memoria.
 * Le richieste non hanno porta di risposta: finite diventano NT_FREEMSG */
#define IO_DEVICE 20
#define IOA_SIZE 68
#define NT_FREEMSG_ 6
#define MAX_SUONI_C 8
#define MAX_PEZZI 32

typedef struct {
    int vivo;
    uint32_t alloc, comb;          /* la richiesta di allocazione e le combinazioni */
    uint32_t pezzi[MAX_PEZZI];
    int n;
    uint32_t mem, lung;            /* il campione in chip */
} suono_c_t;
static suono_c_t s_sc[MAX_SUONI_C];

static void audio_c(uint32_t io) {
    M68KState s = g_cpu;
    g_cpu.A[1] = io;
    audio_beginio();
    g_cpu = s;
}

static uint32_t richiesta_c(uint32_t alloc, uint16_t cmd, uint32_t unita) {
    uint32_t io = os_alloca(IOA_SIZE, MEMF_PUBLIC | MEMF_CLEAR);
    if (!io) return 0;
    W8(io + LN_PRI, R8(alloc + LN_PRI));
    W16(io + 18, IOA_SIZE);                                  /* mn_Length */
    W32(io + IO_DEVICE, s_audio);
    W32(io + IO_UNIT, unita);
    W16(io + IO_COMMAND, cmd);
    W16(io + 32, R16(alloc + 32));                           /* ioa_AllocKey */
    return io;
}

static void suono_c_fine(suono_c_t *s) {
    uint32_t nostri = 0;                       /* solo i canali ancora suoi (non rubati) */
    for (int c = 0; s->alloc && c < 4; c++)
        if ((R32(s->alloc + IO_UNIT) >> c & 1) && s_ca[c].chiave == R16(s->alloc + 32)) nostri |= 1u << c;
    if (nostri) {
        uint32_t lib = richiesta_c(s->alloc, ADCMD_FREE, nostri);
        if (lib) { W8(lib + IO_FLAGS, IOF_QUICK); audio_c(lib); os_libera(lib, IOA_SIZE); }
    }
    for (int i = 0; i < s->n; i++) os_libera(s->pezzi[i], IOA_SIZE);
    if (s->alloc) os_libera(s->alloc, IOA_SIZE);
    if (s->comb) os_libera(s->comb, 4);
    if (s->mem) os_libera(s->mem, s->lung);
    memset(s, 0, sizeof *s);
}

/* prova a suonare lung byte da mem (in chip: se riesce lo libera lui alla
 * fine, se no resta del chiamante) sui canali di una delle combinazioni
 * comb[0..nc-1]; per in tick di Paula, vol 0-64. Restituisce un numero per
 * os_audio_c_finito, -1 se non ci sono canali liberi adesso */
int os_audio_c_prova(uint32_t mem, uint32_t lung, uint16_t per, uint16_t vol, const uint8_t *comb, int nc, int8_t pri) {
    int k = 0;
    while (k < MAX_SUONI_C && s_sc[k].vivo) k++;
    if (k == MAX_SUONI_C || !s_audio || nc > 4) return -1;
    suono_c_t *s = &s_sc[k];
    memset(s, 0, sizeof *s);
    s->vivo = 1; s->mem = mem; s->lung = lung;
    s->comb = os_alloca(4, MEMF_PUBLIC | MEMF_CLEAR);
    s->alloc = os_alloca(IOA_SIZE, MEMF_PUBLIC | MEMF_CLEAR);
    if (!s->comb || !s->alloc) { s->mem = 0; suono_c_fine(s); return -1; }
    for (int i = 0; i < nc; i++) W8(s->comb + i, comb[i]);
    W8(s->alloc + LN_PRI, (uint8_t)pri);
    W16(s->alloc + 18, IOA_SIZE);
    W32(s->alloc + IO_DEVICE, s_audio);
    W16(s->alloc + IO_COMMAND, ADCMD_ALLOCATE);
    W8(s->alloc + IO_FLAGS, IOF_QUICK);
    W32(s->alloc + 34, s->comb);
    W32(s->alloc + 38, (uint32_t)nc);
    audio_c(s->alloc);
    if ((int8_t)R8(s->alloc + IO_ERROR)) { W32(s->alloc + IO_UNIT, 0); s->mem = 0; suono_c_fine(s); return -1; }
    uint32_t unita = R32(s->alloc + IO_UNIT);
    for (int c = 0; c < 4; c++) {
        if (!(unita >> c & 1)) continue;
        for (uint32_t off = 0; off < lung && s->n < MAX_PEZZI; off += 0x1FFFE) {
            uint32_t io = richiesta_c(s->alloc, CMD_WRITE, 1u << c);
            if (!io) break;
            uint32_t l = lung - off > 0x1FFFE ? 0x1FFFE : lung - off;
            W8(io + IO_FLAGS, ADIOF_PERVOL);
            W32(io + 34, mem + off);
            W32(io + 38, l & ~1u);
            W16(io + 42, per);
            W16(io + 44, vol);
            W16(io + 46, 1);
            s->pezzi[s->n++] = io;
            audio_c(io);
        }
    }
    return k;
}

static int suono_c_finito(suono_c_t *s) {
    for (int i = 0; i < s->n; i++) {
        uint8_t t = R8(s->pezzi[i] + LN_TYPE);
        if (t != NT_FREEMSG_ && !(R8(s->pezzi[i] + IO_FLAGS) & IOF_QUICK)) return 0;
    }
    return 1;
}

/* 1 se il suono k e' finito (o non c'e') */
int os_audio_c_finito(int k) {
    if (k < 0 || k >= MAX_SUONI_C || !s_sc[k].vivo) return 1;
    os_device_risposte_audio();
    if (!suono_c_finito(&s_sc[k])) return 0;
    suono_c_fine(&s_sc[k]);
    return 1;
}

/* i suoni lanciati con "run" che non hanno trovato canali: come una
 * ADCMD_ALLOCATE senza NOWAIT aspettano che se ne liberino (un po' di quadri,
 * poi si lascia perdere: un effetto in ritardo di secondi non serve). Succede
 * con gli effetti lunghi ripetuti di continuo (CastingSounds/Abduction): le
 * ripetizioni si accavallano e qualcuna salta */
#define MAX_ATTESA 8
typedef struct { uint32_t mem, lung; uint16_t per, vol; uint8_t comb[4]; int nc; int8_t pri; int quadri; char nome[48]; } attesa_c_t;
static attesa_c_t s_attesa_c[MAX_ATTESA];
static int s_n_attesa_c;

void os_audio_c_suona_dopo(uint32_t mem, uint32_t lung, uint16_t per, uint16_t vol, const uint8_t *comb, int nc,
                           int8_t pri, int quadri, const char *nome) {
    if (os_audio_c_prova(mem, lung, per, vol, comb, nc, pri) >= 0) return;
    if (s_n_attesa_c == MAX_ATTESA || nc > 4) {
        g_dstat.suoni_saltati++;
        fprintf(stderr, "[audio] \"%s\" saltato: canali occupati e %d effetti gia' in coda\n", nome, MAX_ATTESA);
        os_libera(mem, lung);
        return;
    }
    attesa_c_t *a = &s_attesa_c[s_n_attesa_c++];
    a->mem = mem; a->lung = lung; a->per = per; a->vol = vol; a->nc = nc; a->pri = pri; a->quadri = quadri;
    snprintf(a->nome, sizeof a->nome, "%s", nome);
    memcpy(a->comb, comb, (size_t)nc);
}

static void suoni_c_quadro(void) {
    for (int k = 0; k < MAX_SUONI_C; k++)
        if (s_sc[k].vivo && suono_c_finito(&s_sc[k])) suono_c_fine(&s_sc[k]);
    for (int i = 0; i < s_n_attesa_c;) {
        attesa_c_t *a = &s_attesa_c[i];
        if (os_audio_c_prova(a->mem, a->lung, a->per, a->vol, a->comb, a->nc, a->pri) < 0 && --a->quadri > 0) { i++; continue; }
        if (a->quadri <= 0) {
            g_dstat.suoni_saltati++;
            fprintf(stderr, "[audio] \"%s\" saltato: i canali erano ancora occupati dopo 2 s\n", a->nome);
            os_libera(a->mem, a->lung);
        }
        s_attesa_c[i] = s_attesa_c[--s_n_attesa_c];
    }
}

void os_device_quadro(void) {
    /* audio.device: le note finite nel quadro passato */
    os_device_risposte_audio();
    suoni_c_quadro();
    /* narrator.device: la frase detta */
    if (s_narr_io && voce_finita()) { completa(s_narr_io, 0); s_narr_io = 0; }
    /* timer.device: le richieste scadute */
    uint64_t ora = os_microsecondi();
    for (int i = 0; i < s_n_attese;) {
        if (s_attese[i].scade <= ora) {
            uint32_t io = s_attese[i].io;
            s_attese[i] = s_attese[--s_n_attese];
            completa(io, 0);
        } else i++;
    }
    /* CIA-B: il timer A o B con un gestore, al ritmo dei registri: un quadro
     * PAL sono 709379 / 50 tick E-clock */
    static double resto[2];
    for (int t = 0; t < 2; t++) {
        if (!s_icr_b[t].is || !(s_abilitati_b >> t & 1)) continue;
        unsigned latch = amiga_hw_ciab_timer(t);
        if (!latch || !amiga_hw_ciab_avviato(t)) continue;
        resto[t] += 709379.0 / 50.0 / (double)latch;
        while (resto[t] >= 1.0) { resto[t] -= 1.0; chiama_icr(s_icr_b[t].is); }
    }
    /* CIA-A: lo stesso, abilitato dalla maschera dell'ICR (il Protracker di
     * Total Chaos scrive da se' timer A, CRA e ICR) */
    static double resto_a[2];
    for (int t = 0; t < 2; t++) {
        if (!s_icr_a[t].is || !amiga_hw_ciaa_abilitato(t)) continue;
        unsigned latch = amiga_hw_ciaa_timer(t);
        if (!latch || !amiga_hw_ciaa_avviato(t)) continue;
        resto_a[t] += 709379.0 / 50.0 / (double)latch;
        while (resto_a[t] >= 1.0) { resto_a[t] -= 1.0; chiama_icr(s_icr_a[t].is); }
    }
}

/* ---------------------------------------------------------- apertura */

static const char *const s_nomi_dev[6] = { "Open", "Close", "Expunge", "Reserved", "BeginIO", "AbortIO" };
static const char *const s_nomi_cia[4] = { "AddICRVector", "RemICRVector", "AbleICR", "SetICR" };

uint32_t os_apri_device_disco(const char *nome, uint32_t unita, uint32_t io, uint32_t flag, int *errore);

uint32_t os_apri_device(const char *nome, uint32_t unita, uint32_t io, uint32_t flag) {
    /* il narrator.device del disco (68000 ricompilato col gioco): la voce
     * vera dell'Amiga, sintetizzata da lui e suonata con audio.device.
     * AMIGA_NARRATORE=sapi usa invece la voce di Windows (amiga_voce.c) */
    const char *nr = getenv("AMIGA_NARRATORE");
    if (strstr(nome, "narrator.device") && !(nr && !strcmp(nr, "sapi"))) {
        int errore = 0;
        if (os_apri_device_disco(nome, unita, io, flag, &errore)) return (uint32_t)errore & 0xFF;
    }
    uint32_t dev = 0;
    if (!strcmp(nome, "timer.device")) dev = s_timer;
    else if (!strcmp(nome, "audio.device")) dev = s_audio;
    else if (!strcmp(nome, "input.device") || !strcmp(nome, "console.device") ||
             !strcmp(nome, "keyboard.device") || !strcmp(nome, "gameport.device")) dev = s_input;
    else if (!strcmp(nome, "narrator.device")) dev = s_narratore;
    if (!dev) {
        W8(io + IO_ERROR, (uint8_t)IOERR_OPENFAIL);
        return (uint32_t)IOERR_OPENFAIL & 0xFF;
    }
    W32(io + IO_DEVICE, dev);
    W32(io + IO_UNIT, unita);
    W8(io + IO_ERROR, 0);
    W16(dev + LIB_OPENCNT, R16(dev + LIB_OPENCNT) + 1);
    /* audio: OpenDevice con ioa_Length > 0 alloca anche i canali */
    if (dev == s_audio && R32(io + 38)) {
        M68KState s = g_cpu;
        W16(io + IO_COMMAND, ADCMD_ALLOCATE);
        W8(io + IO_FLAGS, IOF_QUICK);
        g_cpu.A[1] = io;
        audio_beginio();
        g_cpu = s;
        return (uint32_t)(int32_t)(int8_t)R8(io + IO_ERROR) & 0xFF;
    }
    return 0;
}

uint32_t os_apri_risorsa(const char *nome) {
    if (!strcmp(nome, "ciaa.resource")) return s_ciaa;
    if (!strcmp(nome, "ciab.resource")) return s_ciab;
    return 0;
}

void os_device_init(void) {
    static const os_funzione f_timer[6] = { NULL, device_close, NULL, NULL, timer_beginio, timer_abortio };
    static const os_funzione f_audio[6] = { NULL, audio_close, NULL, NULL, audio_beginio, audio_abortio };
    amiga_paula_fine_blocco = audio_fine_blocco;
    static const os_funzione f_input[6] = { NULL, device_close, NULL, NULL, niente_beginio, niente_abortio };
    static const os_libreria_c d_timer = { "timer.device", 34, 6, LIB_SIZE, s_nomi_dev, f_timer };
    static const os_libreria_c d_audio = { "audio.device", 34, 6, LIB_SIZE, s_nomi_dev, f_audio };
    static const os_libreria_c d_input = { "input.device", 34, 6, LIB_SIZE, s_nomi_dev, f_input };
    s_timer = os_crea_libreria(&d_timer, NT_DEVICE);
    s_audio = os_crea_libreria(&d_audio, NT_DEVICE);
    s_input = os_crea_libreria(&d_input, NT_DEVICE);
    /* narrator.device: i fonemi li dice la voce di Windows (amiga_voce.c) */
    static const os_funzione f_narr[6] = { NULL, device_close, NULL, NULL, narratore_beginio, narratore_abortio };
    static const os_libreria_c d_narr = { "narrator.device", 34, 6, LIB_SIZE, s_nomi_dev, f_narr };
    s_narratore = os_crea_libreria(&d_narr, NT_DEVICE);
    /* le risorse CIA: la tabella parte da -6 (niente Open/Close) */
    static const os_funzione f_ciaa[4] = { ciaa_addicrvector, ciaa_remicrvector, ciaa_ableicr, ciab_seticr };
    static const os_funzione f_ciab[4] = { ciab_addicrvector, ciab_remicrvector, ciab_ableicr, ciab_seticr };
    static const os_libreria_c r_ciaa = { "ciaa.resource", 34, 4, LIB_SIZE, s_nomi_cia, f_ciaa };
    static const os_libreria_c r_ciab = { "ciab.resource", 34, 4, LIB_SIZE, s_nomi_cia, f_ciab };
    s_ciaa = os_crea_libreria(&r_ciaa, NT_RESOURCE);
    s_ciab = os_crea_libreria(&r_ciab, NT_RESOURCE);
}
