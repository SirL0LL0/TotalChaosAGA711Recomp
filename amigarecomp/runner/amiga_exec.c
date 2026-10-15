/* amiga_exec.c - exec.library (V34) in C.
 *
 * Un solo task (il Process del programma): niente scheduler; Forbid,
 * Disable e i semafori sono contatori. Wait fa girare i quadri del runner
 * finche' arriva un segnale (os_attendi). Gli interrupt che il programma
 * aggiunge (AddIntServer, SetIntVector) li chiama il runner a ogni quadro
 * (os_exec_vblank).
 */
#include "amiga_os.h"
#include "genesis_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IOF_QUICK 1

/* ---------------------------------------------------- interrupt del programma */
typedef struct { uint32_t is; int livello; } server_t;
static server_t s_server[16];
static int s_n_server;

void os_exec_vblank(void) {
    /* INTB_VERTB: i server ricevono A1 = is_Data, A5 = is_Code, A6 = SysBase */
    for (int i = 0; i < s_n_server; i++) {
        if (s_server[i].livello != 5) continue;
        uint32_t is = s_server[i].is;
        M68KState s = g_cpu;
        g_cpu.A[1] = R32(is + IS_DATA);
        g_cpu.A[5] = R32(is + IS_CODE);
        g_cpu.A[6] = os_sysbase;
        g_cpu.A[0] = 0xDFF000u;
        os_chiama_68k(R32(is + IS_CODE));
        g_cpu = s;
    }
}

/* ---------------------------------------------------- funzioni */

static void e_niente(void) {}
static void e_disable(void) { W8(os_sysbase + 294, R8(os_sysbase + 294) + 1); }
static void e_enable(void)  { W8(os_sysbase + 294, R8(os_sysbase + 294) - 1); }
static void e_forbid(void)  { W8(os_sysbase + 295, R8(os_sysbase + 295) + 1); }
static void e_permit(void)  { W8(os_sysbase + 295, R8(os_sysbase + 295) - 1); }

static void e_supervisor(void) {
    /* A5 = routine da eseguire in modo supervisore, finisce con RTE: la pila
     * come dopo una trap (SR, poi l'indirizzo di ritorno sopra) */
    uint16_t sr = g_cpu.SR;
    uint32_t sp = g_cpu.A[7];
    g_cpu.A[7] -= 4; W32(g_cpu.A[7], 0x00000Cu);
    g_cpu.A[7] -= 2; W16(g_cpu.A[7], sr);
    g_cpu.SR |= SR_S;
    call_by_address(g_cpu.A[5]);
    g_rte_pending = 0;
    g_cpu.A[7] = sp;
    g_cpu.SR = sr;
}

/* InitStruct(initTable A1, memory A2, size D0) */
static void e_initstruct(void) {
    uint32_t t = A1, m = A2, n = D0;
    if (n) memset(g_amiga_mem + m, 0, (n + 1) & ~1u);
    uint32_t off = 0;
    for (;;) {
        uint8_t c = R8(t);
        if (!c) break;
        int dest = (c >> 6) & 3, size = (c >> 4) & 3, count = c & 15;
        t++;
        if (dest == 3) { off = R8(t) << 16 | R16(t + 1); t += 3; }
        else if (dest == 2) { off = R8(t); t++; }
        if (size == 3) break;
        int bytes = size == 0 ? 4 : size == 1 ? 2 : 1;
        if (bytes > 1 && (t & 1)) t++;
        for (int i = 0; i <= count; i++) {
            if (dest == 1) {                         /* ripete lo stesso valore */
                for (int k = 0; k < bytes; k++) W8(m + off + k, R8(t + k));
                off += bytes;
                if (i == count) t += bytes;
            } else {
                for (int k = 0; k < bytes; k++) W8(m + off + k, R8(t + k));
                off += bytes; t += bytes;
            }
        }
        if (t & 1) t++;
    }
}

/* MakeFunctions(target A0, functionArray A1, funcDispBase A2): JMP abs.l */
static uint32_t make_functions(uint32_t target, uint32_t arr, uint32_t disp) {
    uint32_t n = 0;
    if (disp) {
        for (;; n++) {
            int16_t o = S16(arr + 2 * n);
            if (o == -1) break;
            uint32_t v = target - 6 * (n + 1);
            W16(v, 0x4EF9); W32(v + 2, disp + (uint32_t)(int32_t)o);
        }
    } else {
        for (;; n++) {
            uint32_t f = R32(arr + 4 * n);
            if (f == 0xFFFFFFFFu) break;
            uint32_t v = target - 6 * (n + 1);
            W16(v, 0x4EF9); W32(v + 2, f);
        }
    }
    return n * 6;
}
static void e_makefunctions(void) { D0 = make_functions(A0, A1, A2); }

uint32_t os_make_library(uint32_t vettori, uint32_t struttura, uint32_t init, uint32_t dim, uint32_t seglist) {
    uint32_t n = 0;
    int rel = R16(vettori) == 0xFFFF;
    if (rel) while (S16(vettori + 2 + 2 * n) != -1) n++;
    else while (R32(vettori + 4 * n) != 0xFFFFFFFFu) n++;
    uint32_t neg = (n * 6 + 3) & ~3u;
    uint32_t mem = os_alloca(neg + dim, MEMF_PUBLIC | MEMF_CLEAR | MEMF_24BITDMA);   /* tabella dei salti sotto i 16 MB */
    if (!mem) return 0;
    uint32_t base = mem + neg;
    make_functions(base, rel ? vettori + 2 : vettori, rel ? vettori : 0);
    W16(base + LIB_NEGSIZE, neg);
    W16(base + LIB_POSSIZE, dim);
    if (struttura) {
        uint32_t a1 = A1, a2 = A2, d0 = D0;
        A1 = struttura; A2 = base; D0 = 0;
        e_initstruct();
        A1 = a1; A2 = a2; D0 = d0;
    }
    if (init) {
        M68KState s = g_cpu;
        D0 = base; A0 = seglist; A6 = os_sysbase;
        os_chiama_68k(init);
        base = D0;
        g_cpu = s;
    }
    return base;
}
static void e_makelibrary(void) { D0 = os_make_library(A0, A1, A2, D0, D1); }

static void e_alert(void) {
    fprintf(stderr, "[os] Alert $%08X\n", D7);
    if (D7 & 0x80000000u) exit(3);
}

static void e_setsr(void) {
    uint32_t vecchio = g_cpu.SR;
    g_cpu.SR = (uint16_t)((g_cpu.SR & ~D1) | (D0 & D1));
    D0 = vecchio;
}
static void e_superstate(void) { D0 = 0; g_cpu.SR |= SR_S; }
static void e_userstate(void) { g_cpu.SR &= ~SR_S; }

static void e_setintvector(void) {
    /* D0 = numero, A1 = Interrupt: lo trattiamo come un server (VERTB) */
    D0 = 0;
    if (s_n_server < 16) s_server[s_n_server++] = (server_t){ A1, (int)D1 };
}
static void e_addintserver(void) {
    if (s_n_server < 16) s_server[s_n_server++] = (server_t){ A1, (int)D0 };
}
static void e_remintserver(void) {
    for (int i = 0; i < s_n_server; i++)
        if (s_server[i].is == A1) { s_server[i] = s_server[--s_n_server]; break; }
}
static void e_cause(void) {
    M68KState s = g_cpu;
    g_cpu.A[1] = R32(s.A[1] + IS_DATA);
    g_cpu.A[5] = R32(s.A[1] + IS_CODE);
    g_cpu.A[6] = os_sysbase;
    os_chiama_68k(R32(s.A[1] + IS_CODE));
    g_cpu = s;
}

static void e_allocmem(void) { D0 = os_alloca(D0, D1); }
static void e_freemem(void) { os_libera(A1, D0); }
static void e_availmem(void) { D0 = os_disponibile(D1); }
static void e_allocabs(void) { D0 = 0; os_manca("exec.library", "AllocAbs", 204); }

/* AllocEntry(entry A0): MemList con ml_NumEntries a 14, voci (reqs, length) */
static void e_allocentry(void) {
    uint32_t ml = A0;
    uint16_t n = R16(ml + 14);
    uint32_t nuova = os_alloca(16 + 8u * n, MEMF_PUBLIC | MEMF_CLEAR);
    W16(nuova + 14, n);
    for (uint16_t i = 0; i < n; i++) {
        uint32_t req = R32(ml + 16 + 8u * i), len = R32(ml + 20 + 8u * i);
        uint32_t a = os_alloca(len, req);
        if (!a) { D0 = 0x80000000u | req; return; }
        W32(nuova + 16 + 8u * i, a);
        W32(nuova + 20 + 8u * i, len);
    }
    D0 = nuova;
}
static void e_freeentry(void) {
    uint32_t ml = A0;
    uint16_t n = R16(ml + 14);
    for (uint16_t i = 0; i < n; i++) os_libera(R32(ml + 16 + 8u * i), R32(ml + 20 + 8u * i));
    os_libera(ml, 16 + 8u * n);
}

static void e_insert(void) {
    /* Insert(list A0, node A1, pred A2): A2 = 0 in testa */
    if (!A2) { os_aggiungi_testa(A0, A1); return; }
    uint32_t succ = R32(A2 + LN_SUCC);
    if (!succ) { os_aggiungi_coda(A0, A1); return; }     /* A2 e' la coda finta */
    W32(A1 + LN_SUCC, succ); W32(A1 + LN_PRED, A2);
    W32(succ + LN_PRED, A1); W32(A2 + LN_SUCC, A1);
}
static void e_addhead(void) { os_aggiungi_testa(A0, A1); }
static void e_addtail(void) { os_aggiungi_coda(A0, A1); }
static void e_remove(void) { os_togli(A1); }
static void e_remhead(void) { D0 = os_togli_testa(A0); }
static void e_remtail(void) {
    uint32_t n = R32(A0 + LH_TAILPRED);
    if (n == A0) { D0 = 0; return; }
    os_togli(n);
    D0 = n;
}
static void e_enqueue(void) { os_accoda_pri(A0, A1); }
static void e_findname(void) {
    char nome[128];
    os_leggi_stringa(A1, nome, sizeof nome);
    /* A0 puo' essere una lista o un nodo da cui continuare */
    uint32_t l = A0;
    for (uint32_t n = R32(l + LN_SUCC); n && R32(n + LN_SUCC); n = R32(n + LN_SUCC)) {
        char b[128];
        os_leggi_stringa(R32(n + LN_NAME), b, sizeof b);
        if (!strcmp(b, nome)) { D0 = n; return; }
    }
    D0 = 0;
}

void os_task_aggiungi(uint32_t task, uint32_t pc, uint32_t finale);   /* amiga_task.c */
void os_task_togli(uint32_t task);
static void e_addtask(void) { os_task_aggiungi(A1, A2, A3); D0 = A1; }
static void e_remtask(void) { os_task_togli(A1 ? A1 : os_task); }
static void e_findtask(void) {
    if (!A1) { D0 = os_task; return; }
    char nome[128], b[128];
    os_leggi_stringa(A1, nome, sizeof nome);
    os_leggi_stringa(R32(os_task + LN_NAME), b, sizeof b);
    if (!strcmp(nome, b)) { D0 = os_task; return; }
    D0 = os_trova_nome(os_sysbase + EB_TASKREADY, nome);
    if (!D0) D0 = os_trova_nome(os_sysbase + EB_TASKWAIT, nome);
}
static void e_settaskpri(void) {
    uint32_t vecchia = (uint32_t)(int32_t)(int8_t)R8(A1 + LN_PRI);
    W8(A1 + LN_PRI, D0);
    D0 = vecchia;
}

static void e_setsignal(void) {
    uint32_t vecchi = R32(os_task + TC_SIGRECVD);
    W32(os_task + TC_SIGRECVD, (vecchi & ~D1) | (D0 & D1));
    D0 = vecchi;
}
static void e_wait(void) { D0 = os_attendi(D0); }
static void e_signal(void) { os_segnala(A1, D0); }
static void e_allocsignal(void) {
    uint32_t alloc = R32(os_task + TC_SIGALLOC);
    int8_t b = (int8_t)D0;
    if (b == -1) {
        for (b = 31; b >= 0 && (alloc >> b & 1); b--) {}
        if (b < 0) { D0 = (uint32_t)-1; return; }
    } else if (alloc >> b & 1) { D0 = (uint32_t)-1; return; }
    W32(os_task + TC_SIGALLOC, alloc | 1u << b);
    W32(os_task + TC_SIGRECVD, R32(os_task + TC_SIGRECVD) & ~(1u << b));
    D0 = (uint32_t)(int32_t)b;
}
static void e_freesignal(void) {
    int8_t b = (int8_t)D0;
    if (b >= 0) W32(os_task + TC_SIGALLOC, R32(os_task + TC_SIGALLOC) & ~(1u << b));
}

static void e_addport(void) {
    W8(A1 + LN_TYPE, NT_MSGPORT);
    os_lista_init(A1 + MP_MSGLIST);
    os_accoda_pri(os_sysbase + EB_PORTLIST, A1);
}
static void e_remport(void) { os_togli(A1); }
static void e_putmsg(void) { os_metti_msg(A0, A1); }
static void e_getmsg(void) { D0 = os_prendi_msg(A0); }
static void e_replymsg(void) { os_rispondi_msg(A1); }
static void e_waitport(void) {
    uint32_t p = A0;
    while (os_lista_vuota(p + MP_MSGLIST)) os_attendi(1u << R8(p + MP_SIGBIT));
    D0 = R32(p + MP_MSGLIST + LH_HEAD);
}
static void e_findport(void) {
    char nome[128];
    os_leggi_stringa(A1, nome, sizeof nome);
    D0 = os_trova_nome(os_sysbase + EB_PORTLIST, nome);
}

uint32_t os_ultima_libreria;                  /* l'ultima AddLibrary (init senza risultato) */
static void e_addlibrary(void) {
    os_aggiungi_coda(os_sysbase + EB_LIBLIST, A1);
    os_ultima_libreria = A1;
}
static void e_oldopenlibrary(void) {
    char nome[128];
    os_leggi_stringa(A1, nome, sizeof nome);
    D0 = os_apri_libreria(nome, 0);
    if (os_traccia || !D0) fprintf(stderr, "[os] OpenLibrary(\"%s\") = $%06X\n", nome, D0);
}
static void e_openlibrary(void) {
    char nome[128];
    os_leggi_stringa(A1, nome, sizeof nome);
    D0 = os_apri_libreria(nome, D0);
    if (os_traccia || !D0) fprintf(stderr, "[os] OpenLibrary(\"%s\") = $%06X\n", nome, D0);
}
static void e_closelibrary(void) {
    if (A1) W16(A1 + LIB_OPENCNT, R16(A1 + LIB_OPENCNT) - 1);
}
static void e_setfunction(void) {
    /* SetFunction(library A1, funcOffset A0.W, newFunction D0): nella tabella
     * dei salti; il dispatcher segue il JMP se non punta piu' a se stesso */
    uint32_t v = A1 + (uint32_t)(int32_t)(int16_t)A0;
    uint32_t vecchio = R32(v + 2);
    W16(v, 0x4EF9);
    W32(v + 2, D0);
    D0 = vecchio;
}

/* ---------------------------------------------------- device */

/* i device in C hanno BeginIO (-30) e AbortIO (-36) nella loro tabella */
static void chiama_device(uint32_t io, int lvo) {
    uint32_t dev = R32(io + IO_DEVICE);
    if (!dev) return;
    M68KState s = g_cpu;
    g_cpu.A[1] = io; g_cpu.A[6] = dev;
    os_chiama_68k(dev - (uint32_t)lvo);
    g_cpu = s;
}

uint32_t os_apri_device(const char *nome, uint32_t unita, uint32_t io, uint32_t flag);   /* amiga_device.c */
static void e_opendevice(void) {
    char nome[128];
    os_leggi_stringa(A0, nome, sizeof nome);
    D0 = os_apri_device(nome, D0, A1, D1);
    if (os_traccia || D0) fprintf(stderr, "[os] OpenDevice(\"%s\", %u) = %d\n", nome, D0, (int)(int8_t)D0);
}
static void e_closedevice(void) { chiama_device(A1, 12); }

/* AMIGA_TRACCIA_OS: il testo di un CMD_WRITE (i fonemi per il narrator) */
static void traccia_scrittura(uint32_t io) {
    if (!os_traccia || R16(io + IO_COMMAND) != 3) return;
    uint32_t dati = R32(io + 40), n = R32(io + 36);
    if (n > 200) return;
    fprintf(stderr, " [CMD_WRITE \"");
    for (uint32_t i = 0; i < n; i++) {
        uint8_t c = R8(dati + i);
        if (c < 32 || c > 126) return;
        fputc(c, stderr);
    }
    fprintf(stderr, "\"]");
}

static void e_sendio(void) {
    traccia_scrittura(A1);
    W8(A1 + IO_FLAGS, 0);
    W8(A1 + LN_TYPE, NT_MESSAGE);
    chiama_device(A1, 30);
}
static int io_finito(uint32_t io) {
    return (R8(io + IO_FLAGS) & IOF_QUICK) || R8(io + LN_TYPE) == NT_REPLYMSG;
}
static void e_waitio(void) {
    uint32_t io = A1;
    while (!io_finito(io)) {
        uint32_t porta = R32(io + MN_REPLYPORT);
        os_attendi(porta ? 1u << R8(porta + MP_SIGBIT) : 0x100u);
    }
    if (R8(io + LN_TYPE) == NT_REPLYMSG) os_togli(io);
    D0 = (uint32_t)(int32_t)(int8_t)R8(io + IO_ERROR);
}
static void e_doio(void) {
    uint32_t io = A1;
    traccia_scrittura(io);
    W8(io + IO_FLAGS, IOF_QUICK);
    W8(io + LN_TYPE, NT_MESSAGE);
    chiama_device(io, 30);
    A1 = io;
    e_waitio();
}
static void e_checkio(void) { D0 = io_finito(A1) ? A1 : 0; }
static void e_abortio(void) { chiama_device(A1, 36); D0 = 0; }

uint32_t os_apri_risorsa(const char *nome);   /* amiga_device.c */
static void e_openresource(void) {
    char nome[128];
    os_leggi_stringa(A1, nome, sizeof nome);
    D0 = os_apri_risorsa(nome);
    if (os_traccia || !D0) fprintf(stderr, "[os] OpenResource(\"%s\") = $%06X\n", nome, D0);
}

/* RawDoFmt(format A0, data A1, putChProc A2, putChData A3) */
static void metti(uint32_t proc, uint32_t *dati, char c) {
    M68KState s = g_cpu;
    g_cpu.D[0] = (uint8_t)c;
    g_cpu.A[3] = *dati;
    os_chiama_68k(proc);
    *dati = g_cpu.A[3];
    g_cpu = s;
}
/* il formato di RawDoFmt in una stringa C (per dos: VPrintf, VFPrintf,
 * Fault); *arg avanza sui dati. Restituisce la lunghezza */
size_t os_formatta(uint32_t f, uint32_t *arg, char *out, size_t n) {
    size_t k = 0;
    char c;
#define METTI(x) do { if (k + 1 < n) out[k++] = (x); } while (0)
    while ((c = (char)R8(f++)) != 0) {
        if (c != '%') { METTI(c); continue; }
        int sx = 0, zeri = 0, lungo = 0, larg = 0, prec = -1;
        if (R8(f) == '-') { sx = 1; f++; }
        if (R8(f) == '0') { zeri = 1; f++; }
        while (R8(f) >= '0' && R8(f) <= '9') larg = larg * 10 + (R8(f++) - '0');
        if (R8(f) == '.') { f++; prec = 0; while (R8(f) >= '0' && R8(f) <= '9') prec = prec * 10 + (R8(f++) - '0'); }
        if (R8(f) == 'l') { lungo = 1; f++; }
        char t = (char)R8(f++), buf[300];
        buf[0] = 0;
        switch (t) {
        case 'd': case 'D': case 'u': case 'U': case 'x': case 'X': {
            uint32_t v;
            if (lungo) { v = R32(*arg); *arg += 4; } else { v = R16(*arg); *arg += 2; }
            if (t == 'd' || t == 'D') snprintf(buf, sizeof buf, "%d", lungo ? (int32_t)v : (int16_t)v);
            else if (t == 'u' || t == 'U') snprintf(buf, sizeof buf, "%u", v);
            else snprintf(buf, sizeof buf, t == 'x' ? "%x" : "%X", v);
            break;
        }
        case 'c': { uint32_t v; if (lungo) { v = R32(*arg); *arg += 4; } else { v = R16(*arg); *arg += 2; }
                    buf[0] = (char)v; buf[1] = 0; break; }
        case 's': { uint32_t p = R32(*arg); *arg += 4; os_leggi_stringa(p, buf, sizeof buf); break; }
        case 'b': { uint32_t p = BADDR(R32(*arg)); *arg += 4; uint8_t m = R8(p);
                    for (int i = 0; i < m && i < 255; i++) buf[i] = (char)R8(p + 1 + i); buf[m] = 0; break; }
        case '%': strcpy(buf, "%"); break;
        default: buf[0] = t; buf[1] = 0;
        }
        if (prec >= 0 && t == 's' && (int)strlen(buf) > prec) buf[prec] = 0;
        int m = (int)strlen(buf);
        if (!sx) for (int i = m; i < larg; i++) METTI(zeri ? '0' : ' ');
        for (int i = 0; i < m; i++) METTI(buf[i]);
        if (sx) for (int i = m; i < larg; i++) METTI(' ');
    }
#undef METTI
    out[k < n ? k : n - 1] = 0;
    return k;
}

static void e_rawdofmt(void) {
    uint32_t f = A0, arg = A1, proc = A2, dati = A3;
    char c;
    while ((c = (char)R8(f++)) != 0) {
        if (c != '%') { metti(proc, &dati, c); continue; }
        int sx = 0, zeri = 0, lungo = 0, larg = 0, prec = -1;
        if (R8(f) == '-') { sx = 1; f++; }
        if (R8(f) == '0') { zeri = 1; f++; }
        while (R8(f) >= '0' && R8(f) <= '9') larg = larg * 10 + (R8(f++) - '0');
        if (R8(f) == '.') { f++; prec = 0; while (R8(f) >= '0' && R8(f) <= '9') prec = prec * 10 + (R8(f++) - '0'); }
        if (R8(f) == 'l') { lungo = 1; f++; }
        char t = (char)R8(f++), buf[300];
        buf[0] = 0;
        switch (t) {
        case 'd': case 'D': case 'u': case 'U': case 'x': case 'X': {
            uint32_t v;
            if (lungo) { v = R32(arg); arg += 4; } else { v = R16(arg); arg += 2; }
            if (t == 'd' || t == 'D') snprintf(buf, sizeof buf, "%d", lungo ? (int32_t)v : (int16_t)v);
            else if (t == 'u' || t == 'U') snprintf(buf, sizeof buf, "%u", v);
            else snprintf(buf, sizeof buf, t == 'x' ? "%x" : "%X", v);
            break;
        }
        case 'c': { uint32_t v; if (lungo) { v = R32(arg); arg += 4; } else { v = R16(arg); arg += 2; }
                    buf[0] = (char)v; buf[1] = 0; break; }
        case 's': { uint32_t p = R32(arg); arg += 4; os_leggi_stringa(p, buf, sizeof buf); break; }
        case 'b': { uint32_t p = BADDR(R32(arg)); arg += 4; uint8_t n = R8(p);
                    for (int i = 0; i < n && i < 255; i++) buf[i] = (char)R8(p + 1 + i); buf[n] = 0; break; }
        case '%': strcpy(buf, "%"); break;
        default: buf[0] = t; buf[1] = 0;
        }
        if (prec >= 0 && t == 's' && (int)strlen(buf) > prec) buf[prec] = 0;
        int n = (int)strlen(buf);
        if (!sx) for (int i = n; i < larg; i++) metti(proc, &dati, zeri ? '0' : ' ');
        for (int i = 0; i < n; i++) metti(proc, &dati, buf[i]);
        if (sx) for (int i = n; i < larg; i++) metti(proc, &dati, ' ');
    }
    metti(proc, &dati, 0);
    D0 = arg;
}

static void e_getcc(void) { D0 = g_cpu.SR & 0xFF; }
static void e_typeofmem(void) {
    D0 = A1 < OS_FINE_CHIP ? (MEMF_CHIP | MEMF_PUBLIC) : os_e_ram(A1) ? (MEMF_FAST | MEMF_PUBLIC) : 0;
}

/* semafori: SignalSemaphore = Node, ss_NestCount 14, ss_WaitQueue 16,
 * ss_MultipleLink 28, ss_Owner 40, ss_QueueCount 44 */
static void e_initsemaphore(void) {
    memset(g_amiga_mem + A0 + LN_SIZE, 0, 46 - LN_SIZE);
    W8(A0 + LN_TYPE, NT_SEMAPHORE);
    os_lista_init(A0 + 16);
    W16(A0 + 44, 0xFFFF);
}
/* un semaforo di un altro task fa aspettare: chi lo chiede cede il posto un
 * quadro alla volta finche' non e' libero (il task di animazione del gioco
 * disegna senza layer solo col semaforo in mano; il programma lo tiene
 * mentre mostra le schermate di fine turno) */
void os_task_attendi_quadro(void);
static int semaforo_libero(uint32_t s) {
    uint32_t o = R32(s + 40);
    return !o || o == os_task || !R16(s + 14);
}
static void prendi_semaforo(uint32_t s) {
    W16(s + 14, R16(s + 14) + 1);
    W16(s + 44, R16(s + 44) + 1);
    W32(s + 40, os_task);
}
static void e_obtainsemaphore(void) {
    uint32_t s = A0;
    M68KState salvato = g_cpu;
    while (!semaforo_libero(s)) os_task_attendi_quadro();
    g_cpu = salvato;
    prendi_semaforo(s);
}
static void e_releasesemaphore(void) {
    if (!R16(A0 + 14)) return;
    W16(A0 + 14, R16(A0 + 14) - 1);
    W16(A0 + 44, R16(A0 + 44) - 1);
    if (!R16(A0 + 14)) W32(A0 + 40, 0);
}
static void e_attemptsemaphore(void) {
    if (semaforo_libero(A0)) { prendi_semaforo(A0); D0 = 1; } else D0 = 0;
}
static void e_addsemaphore(void) {
    uint32_t a0 = A0;
    A0 = A1;
    e_initsemaphore();
    A0 = a0;
    os_accoda_pri(os_sysbase + EB_SEMAPHORELIST, A1);
}
static void e_remsemaphore(void) { os_togli(A1); }
static void e_findsemaphore(void) {
    char nome[128];
    os_leggi_stringa(A1, nome, sizeof nome);
    D0 = os_trova_nome(os_sysbase + EB_SEMAPHORELIST, nome);
}

static void e_copymem(void) {
    if (D0) memmove(g_amiga_mem + A1, g_amiga_mem + A0, D0);
}

/* ---- Kickstart 2.0-3.1 (V36-V39) ---- */

static void e_createiorequest(void) {
    /* CreateIORequest(porta A0, dim D0) */
    if (!A0 || D0 < MN_SIZE) { D0 = 0; return; }
    uint32_t io = os_alloca(D0, MEMF_PUBLIC | MEMF_CLEAR);
    if (io) {
        W8(io + LN_TYPE, NT_REPLYMSG);
        W32(io + MN_REPLYPORT, A0);
        W16(io + MN_LENGTH, D0);
    }
    D0 = io;
}
static void e_deleteiorequest(void) { if (A0) os_libera(A0, R16(A0 + MN_LENGTH)); }

static void e_createmsgport(void) { D0 = os_crea_porta(); }
static void e_deletemsgport(void) {
    uint32_t p = A0;
    if (!p) return;
    uint32_t d0 = D0;
    D0 = (uint32_t)(int32_t)(int8_t)R8(p + MP_SIGBIT);
    e_freesignal();
    D0 = d0;
    os_libera(p, MP_SIZE);
}

/* AllocVec: la taglia nei 4 byte davanti */
static void e_allocvec(void) {
    uint32_t a = os_alloca(D0 + 4, D1);
    if (a) { W32(a, D0 + 4); a += 4; }
    D0 = a;
}
static void e_freevec(void) { if (A1) os_libera(A1 - 4, R32(A1 - 4)); }

/* pool: una lista dei blocchi (succ, taglia) cosi' DeletePool li libera
 * tutti; la testa: flag (0), primo blocco (4) */
static void e_createpool(void) {
    uint32_t p = os_alloca(8, MEMF_PUBLIC | MEMF_CLEAR);
    if (p) W32(p, D0);
    D0 = p;
}
static void e_allocpooled(void) {
    /* AllocPooled(pool A0, dim D0) */
    if (!A0 || !D0) { D0 = 0; return; }
    uint32_t b = os_alloca(D0 + 8, R32(A0));
    if (!b) { D0 = 0; return; }
    W32(b, R32(A0 + 4)); W32(b + 4, D0 + 8);
    W32(A0 + 4, b);
    D0 = b + 8;
}
static void e_freepooled(void) {
    /* FreePooled(pool A0, memoria A1, dim D0) */
    if (!A0 || !A1) return;
    uint32_t b = A1 - 8, prec = A0 + 4;
    while (R32(prec) && R32(prec) != b) prec = R32(prec);
    if (!R32(prec)) return;
    W32(prec, R32(b));
    os_libera(b, R32(b + 4));
}
static void e_deletepool(void) {
    if (!A0) return;
    for (uint32_t b = R32(A0 + 4), succ; b; b = succ) { succ = R32(b); os_libera(b, R32(b + 4)); }
    os_libera(A0, 8);
}

/* StackSwap(StackSwapStruct A0): stk_Lower 0, stk_Upper 4, stk_Pointer 8.
 * Come exec, l'indirizzo di ritorno passa sulla pila nuova: il codice
 * generato dopo la chiamata fa A7 += 4 e si trova a stk_Pointer */
static void e_stackswap(void) {
    uint32_t s = A0, ret = R32(g_cpu.A[7]);
    uint32_t basso = R32(os_task + TC_SPLOWER), alto = R32(os_task + TC_SPUPPER);
    W32(os_task + TC_SPLOWER, R32(s)); W32(os_task + TC_SPUPPER, R32(s + 4));
    W32(s, basso); W32(s + 4, alto);
    uint32_t nuova = R32(s + 8);
    W32(s + 8, g_cpu.A[7] + 4);
    g_cpu.A[7] = nuova - 4;
    W32(g_cpu.A[7], ret);
}

static void e_coldreboot(void) {
    fprintf(stderr, "[os] ColdReboot: il programma vuole riavviare l'Amiga, esco\n");
    exit(0);
}

static void e_addresource(void) { os_aggiungi_coda(os_sysbase + EB_RESOURCELIST, A1); }
static void e_adddevice(void) { os_aggiungi_coda(os_sysbase + EB_DEVICELIST, A1); os_ultima_libreria = A1; }

/* ---------------------------------------------------- tabella */

#define N_EXEC 122                         /* fino a StackSwap (-732) */
static const char *const s_nomi[N_EXEC] = {
    "Open", "Close", "Expunge", "Reserved", "Supervisor", "ExitIntr", "Schedule", "Reschedule",
    "Switch", "Dispatch", "Exception", "InitCode", "InitStruct", "MakeLibrary", "MakeFunctions",
    "FindResident", "InitResident", "Alert", "Debug", "Disable", "Enable", "Forbid", "Permit",
    "SetSR", "SuperState", "UserState", "SetIntVector", "AddIntServer", "RemIntServer", "Cause",
    "Allocate", "Deallocate", "AllocMem", "AllocAbs", "FreeMem", "AvailMem", "AllocEntry",
    "FreeEntry", "Insert", "AddHead", "AddTail", "Remove", "RemHead", "RemTail", "Enqueue",
    "FindName", "AddTask", "RemTask", "FindTask", "SetTaskPri", "SetSignal", "SetExcept", "Wait",
    "Signal", "AllocSignal", "FreeSignal", "AllocTrap", "FreeTrap", "AddPort", "RemPort",
    "PutMsg", "GetMsg", "ReplyMsg", "WaitPort", "FindPort", "AddLibrary", "RemLibrary",
    "OldOpenLibrary", "CloseLibrary", "SetFunction", "SumLibrary", "AddDevice", "RemDevice",
    "OpenDevice", "CloseDevice", "DoIO", "SendIO", "CheckIO", "WaitIO", "AbortIO", "AddResource",
    "RemResource", "OpenResource", "RawIOInit", "RawMayGetChar", "RawPutChar", "RawDoFmt",
    "GetCC", "TypeOfMem", "Procure", "Vacate", "OpenLibrary", "InitSemaphore", "ObtainSemaphore",
    "ReleaseSemaphore", "AttemptSemaphore", "ObtainSemaphoreList", "ReleaseSemaphoreList",
    "FindSemaphore", "AddSemaphore", "RemSemaphore", "SumKickData", "AddMemList", "CopyMem",
    "CopyMemQuick", "CacheClearU", "CacheClearE", "CacheControl", "CreateIORequest",
    "DeleteIORequest", "CreateMsgPort", "DeleteMsgPort", "ObtainSemaphoreShared", "AllocVec",
    "FreeVec", "CreatePool", "DeletePool", "AllocPooled", "FreePooled", "AttemptSemaphoreShared",
    "ColdReboot", "StackSwap",
};
#define F(lvo, fn) [(lvo) / 6 - 1] = fn
static const os_funzione s_fn[N_EXEC] = {
    F(30, e_supervisor), F(78, e_initstruct), F(84, e_makelibrary), F(90, e_makefunctions),
    F(108, e_alert), F(114, e_niente), F(120, e_disable), F(126, e_enable), F(132, e_forbid),
    F(138, e_permit), F(144, e_setsr), F(150, e_superstate), F(156, e_userstate),
    F(162, e_setintvector), F(168, e_addintserver), F(174, e_remintserver), F(180, e_cause),
    F(198, e_allocmem), F(204, e_allocabs), F(210, e_freemem), F(216, e_availmem),
    F(222, e_allocentry), F(228, e_freeentry), F(234, e_insert), F(240, e_addhead),
    F(246, e_addtail), F(252, e_remove), F(258, e_remhead), F(264, e_remtail), F(270, e_enqueue),
    F(276, e_findname), F(282, e_addtask), F(288, e_remtask), F(294, e_findtask),
    F(300, e_settaskpri), F(306, e_setsignal), F(318, e_wait), F(324, e_signal),
    F(330, e_allocsignal), F(336, e_freesignal), F(354, e_addport), F(360, e_remport),
    F(366, e_putmsg), F(372, e_getmsg), F(378, e_replymsg), F(384, e_waitport), F(390, e_findport),
    F(396, e_addlibrary), F(408, e_oldopenlibrary), F(414, e_closelibrary), F(420, e_setfunction),
    F(426, e_niente), F(432, e_adddevice), F(444, e_opendevice), F(450, e_closedevice),
    F(456, e_doio), F(462, e_sendio), F(468, e_checkio), F(474, e_waitio), F(480, e_abortio),
    F(486, e_addresource), F(498, e_openresource), F(522, e_rawdofmt), F(528, e_getcc),
    F(534, e_typeofmem), F(552, e_openlibrary), F(558, e_initsemaphore),
    F(564, e_obtainsemaphore), F(570, e_releasesemaphore), F(576, e_attemptsemaphore),
    F(594, e_findsemaphore), F(600, e_addsemaphore), F(606, e_remsemaphore), F(624, e_copymem),
    F(630, e_copymem), F(636, e_niente), F(642, e_niente), F(648, e_niente),
    F(654, e_createiorequest), F(660, e_deleteiorequest), F(666, e_createmsgport),
    F(672, e_deletemsgport), F(678, e_obtainsemaphore), F(684, e_allocvec), F(690, e_freevec),
    F(696, e_createpool), F(702, e_deletepool), F(708, e_allocpooled), F(714, e_freepooled),
    F(720, e_attemptsemaphore), F(726, e_coldreboot), F(732, e_stackswap),
};

void os_exec_init(void) {
    static const os_libreria_c def = { "exec.library", 34, N_EXEC, EB_SIZE, s_nomi, s_fn };
    uint32_t eb = os_crea_libreria(&def, NT_LIBRARY);
    os_sysbase = eb;
    W16(eb + LIB_REVISION, 2);
    W16(eb + 34, 34);                                 /* SoftVer */
    W32(eb + 62, OS_FINE_CHIP);                       /* MaxLocMem */
    W32(eb + 78, OS_MEM);                             /* MaxExtMem */
    W16(eb + EB_ATTNFLAGS, 0x0003);                   /* AFF_68010 | AFF_68020 */
    W8(eb + EB_VBLANKFREQ, 50);
    W8(eb + EB_POWERFREQ, 50);
    const int liste[] = { EB_MEMLIST, EB_RESOURCELIST, EB_DEVICELIST, EB_INTRLIST, EB_LIBLIST,
                          EB_PORTLIST, EB_TASKREADY, EB_TASKWAIT, EB_SEMAPHORELIST };
    for (size_t i = 0; i < sizeof liste / sizeof liste[0]; i++) os_lista_init(eb + (uint32_t)liste[i]);
    os_aggiungi_coda(eb + EB_LIBLIST, eb);
}
