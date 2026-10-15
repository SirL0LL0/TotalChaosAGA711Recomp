/* amiga_dos.c - dos.library (V34) in C, sui file del PC.
 *
 * I percorsi Amiga diventano percorsi nella cartella dei dati del programma:
 * "volume:" e gli assign (os_dos_assegna) puntano a cartelle, il resto e'
 * relativo alla cartella corrente (CurrentDir). FileHandle e FileLock sono
 * strutture vere in memoria emulata (BPTR); a ciascuna corrisponde un file o
 * un percorso dell'host. LoadSeg carica gli eseguibili hunk in memoria con
 * le rilocazioni.
 */
#include "amiga_os.h"
#include "genesis_runtime.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define ERROR_OBJECT_IN_USE       202
#define ERROR_OBJECT_NOT_FOUND    205
#define ERROR_NO_MORE_ENTRIES     232
#define ERROR_SEEK_ERROR          219
#define ERROR_DISK_FULL           221

#define MODE_OLDFILE   1005
#define MODE_NEWFILE   1006
#define MODE_READWRITE 1004
#define FH_SIZE        44
#define FL_SIZE        20

static char s_dati[1024];                    /* la cartella dei dati */
static struct { char volume[32]; char cartella[1024]; } s_assign[16];
static int s_n_assign;
static uint32_t s_dosbase;

/* oggetti dell'host dietro FileHandle e FileLock */
typedef struct {
    uint32_t ind;                            /* la struttura in memoria */
    FILE *f;
    int console;                             /* Input()/Output(): niente file */
    char percorso[1024];
    DIR *dir;                                /* ExNext su un lock di cartella */
} oggetto_t;
static oggetto_t s_ogg[128];

static oggetto_t *ogg_nuovo(uint32_t ind) {
    for (int i = 0; i < 128; i++) if (!s_ogg[i].ind) { memset(&s_ogg[i], 0, sizeof s_ogg[i]); s_ogg[i].ind = ind; return &s_ogg[i]; }
    return NULL;
}
static oggetto_t *ogg_di(uint32_t ind) {
    if (!ind) return NULL;
    for (int i = 0; i < 128; i++) if (s_ogg[i].ind == ind) return &s_ogg[i];
    return NULL;
}

static void imposta_errore(uint32_t e) { if (os_task) W32(os_task + PR_RESULT2, e); }

/* la lista dei dispositivi di AmigaDOS 1.3, per chi la legge da solo (il file
 * requester di req.library ne fa i pulsanti dei volumi): DOSBase->dl_Root
 * (RootNode) -> rn_Info (BPTR DosInfo) -> di_DevInfo, una catena di DevInfo
 * (dvi_Next BPTR +0, dvi_Type +4: 1 assign, 2 volume, dvi_Name BSTR +40).
 * Il volume del gioco e un nodo per ogni assign; si rifa' a ogni assign */
static uint32_t s_root, s_dosinfo;
static struct { char nome[32]; uint32_t nodo; } s_nodi[24];
static int s_n_nodi;
static char s_volume[32];
static uint32_t nodo_dos(const char *nome, uint32_t tipo) {
    for (int i = 0; i < s_n_nodi; i++) if (!strcasecmp(s_nodi[i].nome, nome)) return s_nodi[i].nodo;
    if (s_n_nodi >= 24) return 0;
    uint32_t n = os_alloca(44, MEMF_PUBLIC | MEMF_CLEAR);
    size_t l = strlen(nome);
    if (l > 30) l = 30;
    uint32_t b = os_alloca((uint32_t)l + 2, MEMF_PUBLIC | MEMF_CLEAR);
    W8(b, (uint8_t)l);
    for (size_t i = 0; i < l; i++) W8(b + 1 + (uint32_t)i, (uint8_t)nome[i]);
    W32(n + 4, tipo);
    W32(n + 40, MKBADDR(b));
    snprintf(s_nodi[s_n_nodi].nome, 32, "%s", nome);
    s_nodi[s_n_nodi++].nodo = n;
    return n;
}
static void aggiorna_doslist(void) {
    if (!s_dosbase) return;
    if (!s_root) {
        s_root = os_alloca(56, MEMF_PUBLIC | MEMF_CLEAR);
        s_dosinfo = os_alloca(24, MEMF_PUBLIC | MEMF_CLEAR);
        W32(s_root + 24, MKBADDR(s_dosinfo));                   /* rn_Info */
        W32(s_dosbase + 34, s_root);                             /* dl_Root */
    }
    uint32_t primo = 0, ultimo = 0;
    for (int i = -1; i < s_n_assign; i++) {
        const char *nome = i < 0 ? s_volume : s_assign[i].volume;
        if (!nome[0] || (i >= 0 && !strcasecmp(nome, s_volume))) continue;
        uint32_t n = nodo_dos(nome, i < 0 ? 2u : 1u);
        if (!n) continue;
        W32(n, 0);
        if (ultimo) W32(ultimo, MKBADDR(n)); else primo = n;
        ultimo = n;
    }
    W32(s_dosinfo + 4, primo ? MKBADDR(primo) : 0);             /* di_DevInfo */
}

void os_dos_assegna(const char *volume, const char *cartella) {
    for (int i = 0; i < s_n_assign; i++)
        if (!strcasecmp(s_assign[i].volume, volume)) { snprintf(s_assign[i].cartella, 1024, "%s", cartella); return; }
    if (s_n_assign < 16) {
        snprintf(s_assign[s_n_assign].volume, 32, "%s", volume);
        snprintf(s_assign[s_n_assign].cartella, 1024, "%s", cartella);
        s_n_assign++;
    }
    aggiorna_doslist();
}

/* percorso Amiga -> percorso dell'host; "/" all'inizio o dopo un nome sale
 * di una cartella come in AmigaDOS */
static void percorso_host(const char *amiga, char *out, size_t n) {
    char base[1024];
    const char *resto = amiga;
    const char *due = strchr(amiga, ':');
    if (due) {
        char vol[64];
        size_t lv = (size_t)(due - amiga);
        if (lv >= sizeof vol) lv = sizeof vol - 1;
        memcpy(vol, amiga, lv); vol[lv] = 0;
        snprintf(base, sizeof base, "%s", s_dati);
        for (int i = 0; i < s_n_assign; i++)
            if (!strcasecmp(s_assign[i].volume, vol)) { snprintf(base, sizeof base, "%s", s_assign[i].cartella); break; }
        resto = due + 1;
    } else {
        /* relativo alla cartella corrente */
        uint32_t cd = os_task ? R32(os_task + PR_CURRENTDIR) : 0;
        oggetto_t *o = ogg_di(BADDR(cd));
        snprintf(base, sizeof base, "%s", o ? o->percorso : s_dati);
    }
    snprintf(out, n, "%s", base);
    while (*resto) {
        if (*resto == '/') {                         /* risale */
            char *b = strrchr(out, '/');
            char *c = strrchr(out, '\\');
            if (c > b) b = c;
            if (b) *b = 0;
            resto++;
            continue;
        }
        const char *fine = strchr(resto, '/');
        size_t l = fine ? (size_t)(fine - resto) : strlen(resto);
        size_t lo = strlen(out);
        if (lo + l + 2 < n) {
            out[lo] = '/';
            memcpy(out + lo + 1, resto, l);
            out[lo + 1 + l] = 0;
        }
        resto += l;
        if (*resto == '/') resto++;
    }
}

/* ---------------------------------------------------- file */

static void d_open(void) {
    char nome[256], host[1100];
    os_leggi_stringa(D1, nome, sizeof nome);
    uint32_t modo = D2;
    if (!strcasecmp(nome, "*") || !strncasecmp(nome, "CON:", 4) || !strncasecmp(nome, "RAW:", 4) ||
        !strcasecmp(nome, "NIL:")) {
        uint32_t fh = os_alloca(FH_SIZE, MEMF_PUBLIC | MEMF_CLEAR);
        oggetto_t *o = ogg_nuovo(fh);
        o->console = 1;
        W32(fh + 4, 0xFFFFFFFFu);                    /* fh_Type: interattivo */
        D0 = MKBADDR(fh);
        return;
    }
    percorso_host(nome, host, sizeof host);
    FILE *f = fopen(host, modo == MODE_NEWFILE ? "w+b" : modo == MODE_READWRITE ? "r+b" : "rb");
    if (!f && modo == MODE_READWRITE) f = fopen(host, "w+b");
    if (os_traccia) fprintf(stderr, "[dos] Open(\"%s\", %u) -> %s %s\n", nome, modo, host, f ? "ok" : "NO");
    if (!f) { imposta_errore(ERROR_OBJECT_NOT_FOUND); D0 = 0; return; }
    uint32_t fh = os_alloca(FH_SIZE, MEMF_PUBLIC | MEMF_CLEAR);
    oggetto_t *o = ogg_nuovo(fh);
    o->f = f;
    snprintf(o->percorso, sizeof o->percorso, "%s", host);
    D0 = MKBADDR(fh);
}

static void d_close(void) {
    uint32_t fh = BADDR(D1);
    oggetto_t *o = ogg_di(fh);
    if (!o) { D0 = 0; return; }
    if (o->f) fclose(o->f);
    o->ind = 0;
    os_libera(fh, FH_SIZE);
    D0 = 1;
}

static void d_read(void) {
    oggetto_t *o = ogg_di(BADDR(D1));
    if (!o || o->console) { D0 = o ? 0 : (uint32_t)-1; return; }
    if (D2 + D3 > g_amiga_mem_size) { D0 = (uint32_t)-1; return; }
    D0 = (uint32_t)fread(g_amiga_mem + D2, 1, D3, o->f);
}

static void d_write(void) {
    oggetto_t *o = ogg_di(BADDR(D1));
    if (!o) { D0 = (uint32_t)-1; return; }
    if (o->console) {
        fwrite(g_amiga_mem + D2, 1, D3, stderr);
        D0 = D3;
        return;
    }
    D0 = (uint32_t)fwrite(g_amiga_mem + D2, 1, D3, o->f);
    if (D0 != D3) imposta_errore(ERROR_DISK_FULL);
}

static uint32_t s_input, s_output;
static void d_input(void) { D0 = s_input; }
static void d_output(void) { D0 = s_output; }

static void d_seek(void) {
    oggetto_t *o = ogg_di(BADDR(D1));
    if (!o || !o->f) { D0 = (uint32_t)-1; return; }
    long vecchia = ftell(o->f);
    int da = (int32_t)D3 == -1 ? SEEK_SET : (int32_t)D3 == 0 ? SEEK_CUR : SEEK_END;
    if (fseek(o->f, (long)(int32_t)D2, da)) { imposta_errore(ERROR_SEEK_ERROR); D0 = (uint32_t)-1; return; }
    D0 = (uint32_t)vecchia;
}

static void d_deletefile(void) {
    char nome[256], host[1100];
    os_leggi_stringa(D1, nome, sizeof nome);
    percorso_host(nome, host, sizeof host);
    D0 = remove(host) == 0 || rmdir(host) == 0;
    if (!D0) imposta_errore(ERROR_OBJECT_NOT_FOUND);
}

static void d_rename(void) {
    char a[256], b[256], ha[1100], hb[1100];
    os_leggi_stringa(D1, a, sizeof a);
    os_leggi_stringa(D2, b, sizeof b);
    percorso_host(a, ha, sizeof ha);
    percorso_host(b, hb, sizeof hb);
    D0 = rename(ha, hb) == 0;
}

/* ---------------------------------------------------- lock */

static uint32_t nuovo_lock(const char *host) {
    uint32_t fl = os_alloca(FL_SIZE, MEMF_PUBLIC | MEMF_CLEAR);
    oggetto_t *o = ogg_nuovo(fl);
    snprintf(o->percorso, sizeof o->percorso, "%s", host);
    W32(fl + 8, (uint32_t)-2);                       /* fl_Access: SHARED_LOCK */
    return MKBADDR(fl);
}

static void d_lock(void) {
    char nome[256], host[1100];
    struct stat st;
    os_leggi_stringa(D1, nome, sizeof nome);
    percorso_host(nome, host, sizeof host);
    if (stat(host, &st)) {
        if (os_traccia) fprintf(stderr, "[dos] Lock(\"%s\") -> %s NO\n", nome, host);
        imposta_errore(ERROR_OBJECT_NOT_FOUND); D0 = 0; return;
    }
    D0 = nuovo_lock(host);
}

static void d_unlock(void) {
    uint32_t fl = BADDR(D1);
    oggetto_t *o = ogg_di(fl);
    if (!o) return;
    if (o->dir) closedir(o->dir);
    o->ind = 0;
    os_libera(fl, FL_SIZE);
}

static void d_duplock(void) {
    oggetto_t *o = ogg_di(BADDR(D1));
    D0 = nuovo_lock(o ? o->percorso : s_dati);
}

/* FileInfoBlock: DiskKey 0, DirEntryType 4, FileName[108] 8, Protection 116,
 * EntryType 120, Size 124, NumBlocks 128, Date 132, Comment[80] 144 */
static void riempi_fib(uint32_t fib, const char *host, const char *nome) {
    struct stat st;
    memset(g_amiga_mem + fib, 0, 260);
    if (stat(host, &st)) return;
    int dir = S_ISDIR(st.st_mode);
    W32(fib + 4, dir ? 2 : (uint32_t)-3);
    W32(fib + 120, dir ? 2 : (uint32_t)-3);
    size_t l = strlen(nome);
    if (l > 106) l = 106;
    memcpy(g_amiga_mem + fib + 8, nome, l);          /* fib_FileName: stringa C */
    g_amiga_mem[fib + 8 + l] = 0;
    W32(fib + 124, dir ? 0 : (uint32_t)st.st_size);
    W32(fib + 128, (uint32_t)((st.st_size + 487) / 488));
    /* DateStamp: giorni dal 1/1/1978, minuti, tick */
    long s = (long)st.st_mtime - 252460800L;
    if (s < 0) s = 0;
    W32(fib + 132, (uint32_t)(s / 86400));
    W32(fib + 136, (uint32_t)(s % 86400 / 60));
    W32(fib + 140, (uint32_t)(s % 60 * 50));
}

static const char *nome_finale(const char *host) {
    const char *b = strrchr(host, '/');
    const char *c = strrchr(host, '\\');
    if (c > b) b = c;
    return b ? b + 1 : host;
}

static void d_examine(void) {
    oggetto_t *o = ogg_di(BADDR(D1));
    if (!o) { D0 = 0; return; }
    riempi_fib(D2, o->percorso, nome_finale(o->percorso));
    if (o->dir) { closedir(o->dir); o->dir = NULL; }
    D0 = 1;
}

static void d_exnext(void) {
    oggetto_t *o = ogg_di(BADDR(D1));
    if (!o) { D0 = 0; return; }
    if (!o->dir) o->dir = opendir(o->percorso);
    struct dirent *e;
    while (o->dir && (e = readdir(o->dir)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char h[1400];
        snprintf(h, sizeof h, "%s/%s", o->percorso, e->d_name);
        riempi_fib(D2, h, e->d_name);
        D0 = 1;
        return;
    }
    imposta_errore(ERROR_NO_MORE_ENTRIES);
    D0 = 0;
}

static void d_createdir(void) {
    char nome[256], host[1100];
    os_leggi_stringa(D1, nome, sizeof nome);
    percorso_host(nome, host, sizeof host);
    mkdir(host);
    D0 = nuovo_lock(host);
}

static void d_currentdir(void) {
    D0 = R32(os_task + PR_CURRENTDIR);
    W32(os_task + PR_CURRENTDIR, D1);
}

static void d_ioerr(void) { D0 = R32(os_task + PR_RESULT2); }

static void d_parentdir(void) {
    oggetto_t *o = ogg_di(BADDR(D1));
    char h[1024];
    snprintf(h, sizeof h, "%s", o ? o->percorso : s_dati);
    char *b = strrchr(h, '/'), *c = strrchr(h, '\\');
    if (c > b) b = c;
    if (!b || !strcmp(h, s_dati)) { D0 = 0; return; }
    *b = 0;
    D0 = nuovo_lock(h);
}

/* ---------------------------------------------------- LoadSeg */

static uint32_t lungo(const uint8_t *d, size_t *o) {
    uint32_t v = (uint32_t)d[*o] << 24 | d[*o + 1] << 16 | d[*o + 2] << 8 | d[*o + 3];
    *o += 4;
    return v;
}

/* l'eseguibile hunk in memoria: ogni segmento = lunghezza, BPTR al
 * successivo, dati; restituisce il BPTR del primo (0 = errore) */
static uint32_t carica_hunk(const uint8_t *d, size_t n) {
    size_t o = 0;
    if (n < 24 || lungo(d, &o) != 0x3F3) return 0;
    while (lungo(d, &o)) {}
    lungo(d, &o);
    uint32_t primo = lungo(d, &o), ultimo = lungo(d, &o);
    uint32_t nh = ultimo - primo + 1;
    if (nh > 64) return 0;
    uint32_t seg[64], dim[64];
    for (uint32_t i = 0; i < nh; i++) {
        uint32_t t = lungo(d, &o);
        dim[i] = (t & 0x3FFFFFFFu) * 4;
        uint32_t flag = (t >> 30) == 1 ? MEMF_CHIP : 0;
        seg[i] = os_alloca(dim[i] + 8, flag | MEMF_PUBLIC | MEMF_CLEAR);
        if (!seg[i]) return 0;
        W32(seg[i], dim[i] + 8);
    }
    for (uint32_t i = 0; i < nh; i++) W32(seg[i] + 4, i + 1 < nh ? MKBADDR(seg[i + 1] + 4) : 0);
    int h = -1;
    while (o + 4 <= n) {
        uint32_t t = lungo(d, &o) & 0x3FFFFFFFu;
        if (t == 0x3E9 || t == 0x3EA) {
            h++;
            uint32_t l = lungo(d, &o) * 4;
            memcpy(g_amiga_mem + seg[h] + 8, d + o, l);
            o += l;
        } else if (t == 0x3EB) { h++; lungo(d, &o); }
        else if (t == 0x3EC) {
            for (;;) {
                uint32_t c = lungo(d, &o);
                if (!c) break;
                uint32_t dest = lungo(d, &o);
                for (uint32_t k = 0; k < c; k++) {
                    uint32_t a = seg[h] + 8 + lungo(d, &o);
                    W32(a, R32(a) + seg[dest] + 8);
                }
            }
        } else if (t == 0x3F2) {}
        else if (t == 0x3F0) { for (;;) { uint32_t c = lungo(d, &o); if (!c) break; o += 4 * c + 4; } }
        else if (t == 0x3F1) o += 4 * lungo(d, &o);
        else break;
    }
    return MKBADDR(seg[0] + 4);
}

uint32_t os_dos_carica_segmenti(const char *nome) {
    char host[1100];
    percorso_host(nome, host, sizeof host);
    FILE *f = fopen(host, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc((size_t)n);
    size_t letti = fread(d, 1, (size_t)n, f);
    fclose(f);
    uint32_t s = letti == (size_t)n ? carica_hunk(d, (size_t)n) : 0;
    free(d);
    return s;
}

static void d_loadseg(void) {
    char nome[256];
    os_leggi_stringa(D1, nome, sizeof nome);
    D0 = os_dos_carica_segmenti(nome);
    if (os_traccia || !D0) fprintf(stderr, "[dos] LoadSeg(\"%s\") = $%06X\n", nome, BADDR(D0));
    if (!D0) imposta_errore(ERROR_OBJECT_NOT_FOUND);
}

static void d_unloadseg(void) {
    uint32_t s = D1;
    while (s) {
        uint32_t a = BADDR(s) - 4;
        uint32_t succ = R32(a + 4);
        os_libera(a, R32(a));
        s = succ;
    }
}

/* ---------------------------------------------------- tempo */

/* AMIGA_ORA=<secondi Unix>: l'ora di partenza fissa, poi avanza con i quadri
 * emulati. Il gioco ne ricava il seme casuale (maghi, mappa): con l'ora fissa
 * una prova con AMIGA_INGRESSI va sempre allo stesso modo */
static void d_datestamp(void) {
    time_t t = time(NULL);
    const char *ora = getenv("AMIGA_ORA");
    if (ora) t = (time_t)strtoll(ora, NULL, 10) + (time_t)(os_microsecondi() / 1000000u);
    struct tm g = *gmtime(&t);
    g.tm_isdst = -1;
    long fuso = (long)(t - mktime(&g));               /* ora locale meno UTC */
    long s = (long)t - 252460800L + fuso;
    W32(D1, (uint32_t)(s / 86400));
    W32(D1 + 4, (uint32_t)(s % 86400 / 60));
    W32(D1 + 8, (uint32_t)(s % 60 * 50));
    D0 = D1;
}

void os_task_attendi_quadro(void);                 /* amiga_task.c */
/* SetProtection(nome D1, bit D2): i bit di protezione di AmigaDOS non hanno un
 * equivalente sui file dell'host; il gioco li imposta dopo un salvataggio */
static void d_setprotection(void) { D0 = (uint32_t)-1; }

static void d_delay(void) {
    uint32_t n = D1;                                  /* tick di 1/50 s */
    for (uint32_t i = 0; i < n; i++) os_task_attendi_quadro();
}

static void d_isinteractive(void) {
    oggetto_t *o = ogg_di(BADDR(D1));
    D0 = o && o->console ? (uint32_t)-1 : 0;
}

/* il nome Amiga della cartella dei dati (il volume del disco originale) */
void os_dos_volume(const char *nome) { snprintf(s_volume, sizeof s_volume, "%s", nome); aggiorna_doslist(); }

/* il percorso Amiga di una cartella dell'host dentro i dati: "Chaos:fonts" */
static void percorso_amiga(const char *host, char *out, size_t n) {
    size_t l = strlen(s_dati);
    const char *resto = !strncasecmp(host, s_dati, l) ? host + l : "";
    while (*resto == '/' || *resto == '\\') resto++;
    snprintf(out, n, "%s:%s", s_volume, resto);
    for (char *p = out; *p; p++) if (*p == '\\') *p = '/';
}

/* Execute: solo i comandi della CLI che i programmi usano per sapere dove
 * sono o per farsi un assign. "cd" scrive la cartella corrente; ">file"
 * manda l'uscita in un file. */
static void d_execute(void) {
    char c[512], uscita[1024] = "", file[256] = "";
    os_leggi_stringa(D1, c, sizeof c);
    char *red = strchr(c, '>');
    if (red) {
        char *p = red + 1;
        while (*p == ' ') p++;
        size_t i = 0;
        while (*p && *p != ' ' && i + 1 < sizeof file) file[i++] = *p++;
        file[i] = 0;
        *red = 0;
    }
    char cmd[64] = "", arg1[256] = "", arg2[256] = "";
    for (char *p = c; *p; p++) if (*p == '"') *p = ' ';      /* virgolette: niente spazi nei nomi qui */
    sscanf(c, "%63s %255s %255s", cmd, arg1, arg2);
    if (!strcasecmp(cmd, "cd") && !arg1[0]) {
        uint32_t cd = os_task ? R32(os_task + PR_CURRENTDIR) : 0;
        oggetto_t *o = ogg_di(BADDR(cd));
        percorso_amiga(o ? o->percorso : s_dati, uscita, sizeof uscita);
        strcat(uscita, "\n");
    } else if (!strcasecmp(cmd, "assign") && arg1[0]) {
        char host[1100];
        size_t l = strlen(arg1);
        if (l && arg1[l - 1] == ':') arg1[l - 1] = 0;
        percorso_host(arg2, host, sizeof host);
        os_dos_assegna(arg1, host);
    } else {
        fprintf(stderr, "[dos] Execute(\"%s\") non gestito\n", c);
    }
    if (file[0]) {
        char host[1100];
        percorso_host(file, host, sizeof host);
        FILE *f = fopen(host, "wb");
        if (f) { fputs(uscita, f); fclose(f); }
    }
    if (os_traccia) fprintf(stderr, "[dos] Execute(\"%s\") -> \"%s\"\n", c, uscita);
    D0 = (uint32_t)-1;
}

static void d_exit(void) {
    fprintf(stderr, "[dos] Exit(%d)\n", (int)D1);
    exit((int)D1);
}

/* ---------------------------------------------------- tabella */

#define N_DOS 33
/* le funzioni di dos partono da -30 (Open); -6..-24 sono Open/Close/Expunge
 * della libreria, gestite dal nucleo */
#define F(lvo, fn) [(lvo) / 6 - 1] = fn
static const os_funzione s_fn[N_DOS + 5] = {
    F(30, d_open), F(36, d_close), F(42, d_read), F(48, d_write), F(54, d_input), F(60, d_output),
    F(66, d_seek), F(72, d_deletefile), F(78, d_rename), F(84, d_lock), F(90, d_unlock),
    F(96, d_duplock), F(102, d_examine), F(108, d_exnext), F(120, d_createdir),
    F(126, d_currentdir), F(132, d_ioerr), F(144, d_exit), F(150, d_loadseg), F(156, d_unloadseg),
    F(186, d_setprotection), F(192, d_datestamp), F(198, d_delay), F(210, d_parentdir), F(216, d_isinteractive),
    F(222, d_execute),
};
static const char *const s_nomi_tutti[N_DOS + 5] = {
    "Open*", "Close*", "Expunge", "Reserved", "Open", "Close", "Read", "Write", "Input", "Output",
    "Seek", "DeleteFile", "Rename", "Lock", "UnLock", "DupLock", "Examine", "ExNext", "Info",
    "CreateDir", "CurrentDir", "IoErr", "CreateProc", "Exit", "LoadSeg", "UnLoadSeg", "GetPacket",
    "QueuePacket", "DeviceProc", "SetComment", "SetProtection", "DateStamp", "Delay",
    "WaitForChar", "ParentDir", "IsInteractive", "Execute", "?",
};

void os_dos_init(const char *cartella) {
    snprintf(s_dati, sizeof s_dati, "%s", cartella);
    for (char *p = s_dati; *p; p++) if (*p == '\\') *p = '/';
    size_t l = strlen(s_dati);
    if (l && s_dati[l - 1] == '/') s_dati[l - 1] = 0;
    static const os_libreria_c def = { "dos.library", 34, N_DOS + 5, 70, s_nomi_tutti, s_fn };
    s_dosbase = os_crea_libreria(&def, NT_LIBRARY);
    if (!s_volume[0]) snprintf(s_volume, sizeof s_volume, "DH0");
    aggiorna_doslist();
    /* Input() e Output(): la console */
    uint32_t in = os_alloca(FH_SIZE, MEMF_PUBLIC | MEMF_CLEAR);
    uint32_t out = os_alloca(FH_SIZE, MEMF_PUBLIC | MEMF_CLEAR);
    ogg_nuovo(in)->console = 1;
    ogg_nuovo(out)->console = 1;
    s_input = MKBADDR(in);
    s_output = MKBADDR(out);
}

/* il Process prende console e cartella corrente (dopo os_prepara_avvio) */
void os_dos_processo(void) {
    W32(os_task + PR_CIS, s_input);
    W32(os_task + PR_COS, s_output);
    W32(os_task + PR_CURRENTDIR, nuovo_lock(s_dati));
}
