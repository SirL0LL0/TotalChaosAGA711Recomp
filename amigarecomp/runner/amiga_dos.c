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

/* Execute: una piccola shell per i comandi che i programmi lanciano da soli.
 * Virgolette, ">file" e ">>file" per l'uscita; comandi: cd, assign, list
 * (FILES, DIRS, QUICK, NOHEAD, LFORMAT con %n %s %l), execute (uno script,
 * riga per riga, con la sua cartella corrente), join (TO/AS), delete (anche
 * "#?" e "*"), makedir, copy (TO), echo; protect, resident, wait e gli altri
 * non fanno niente e riescono. Total Chaos con List si fa gli elenchi dei
 * suoni, dei terreni e delle voci, e con uno script li unisce. */
#define MAX_PAROLE 32
static int dividi_riga(char *riga, char *parole[MAX_PAROLE]) {
    int n = 0;
    char *p = riga;
    while (*p && n < MAX_PAROLE) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p || *p == ';') break;
        char *inizio = p, *w = p;
        /* una parola: le virgolette raggruppano (anche a meta': LFORMAT="%n") */
        int dentro = 0;
        while (*p && (dentro || (*p != ' ' && *p != '\t'))) {
            if (*p == '"') { dentro = !dentro; p++; continue; }
            if (*p == '*' && p[1] == 'n' && dentro) { *w++ = '\n'; p += 2; continue; }
            *w++ = *p++;
        }
        if (*p) p++;
        *w = 0;
        parole[n++] = inizio;
    }
    return n;
}

/* un nome dentro una cartella va bene per lo schema AmigaDOS piu' comune:
 * "#?" e "*" (tutto), "#?.est" (finale), altrimenti uguale */
static int corrisponde(const char *schema, const char *nome) {
    if (!schema[0] || !strcmp(schema, "#?") || !strcmp(schema, "*")) return 1;
    if (!strncmp(schema, "#?", 2)) {
        size_t ls = strlen(schema + 2), ln = strlen(nome);
        return ln >= ls && !strcasecmp(nome + ln - ls, schema + 2);
    }
    return !strcasecmp(schema, nome);
}

static void shell_riga(const char *riga_in, int profondita);

static void shell_list(char **parole, int n, FILE *out) {
    const char *dir = "", *formato = NULL;
    int solo_file = 0, solo_dir = 0, rapido = 0, senza_testa = 0;
    for (int i = 1; i < n; i++) {
        if (!strcasecmp(parole[i], "FILES")) solo_file = 1;
        else if (!strcasecmp(parole[i], "DIRS")) solo_dir = 1;
        else if (!strcasecmp(parole[i], "QUICK")) rapido = 1;
        else if (!strcasecmp(parole[i], "NOHEAD")) senza_testa = 1;
        else if (!strncasecmp(parole[i], "LFORMAT=", 8)) formato = parole[i] + 8;
        else if (!strcasecmp(parole[i], "LFORMAT") && i + 1 < n) formato = parole[++i];
        else if (!dir[0]) dir = parole[i];
    }
    char host[1100];
    percorso_host(dir, host, sizeof host);
    DIR *d = opendir(host);
    if (!d) return;
    if (!senza_testa && !formato) fprintf(out, "Directory \"%s\"\n", dir);
    struct dirent *e;
    int nf = 0, nd = 0;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char h[1400];
        snprintf(h, sizeof h, "%s/%s", host, e->d_name);
        struct stat st;
        if (stat(h, &st)) continue;
        int cartella = S_ISDIR(st.st_mode);
        if ((solo_file && cartella) || (solo_dir && !cartella)) continue;
        if (cartella) nd++; else nf++;
        if (formato) {
            for (const char *f = formato; *f; f++) {
                if (*f != '%') { fputc(*f, out); continue; }
                f++;
                if (*f == 'n' || *f == 'N') fputs(e->d_name, out);
                else if (*f == 's' || *f == 'S') fputs(e->d_name, out);
                else if (*f == 'p' || *f == 'P') fputs(dir, out);
                else if (*f == 'l' || *f == 'L') fprintf(out, "%ld", cartella ? 0L : (long)st.st_size);
                else if (*f == '%') fputc('%', out);
                else if (!*f) break;
            }
            fputc('\n', out);
        } else if (rapido) fprintf(out, "%s\n", e->d_name);
        else if (cartella) fprintf(out, "%-24s  Dir ----rwed\n", e->d_name);
        else fprintf(out, "%-24s %6ld ----rwed\n", e->d_name, (long)st.st_size);
    }
    closedir(d);
    if (!senza_testa && !formato) fprintf(out, "%d files - %d directories\n", nf, nd);
}

static void shell_execute(const char *script, int profondita) {
    char host[1100];
    percorso_host(script, host, sizeof host);
    FILE *f = fopen(host, "rb");
    if (!f) { fprintf(stderr, "[dos] Execute: script %s non trovato\n", script); return; }
    /* lo script gira con la sua cartella corrente (come un nuovo processo CLI) */
    uint32_t cd = R32(os_task + PR_CURRENTDIR);
    oggetto_t *o = ogg_di(BADDR(cd));
    uint32_t mia = nuovo_lock(o ? o->percorso : s_dati);
    W32(os_task + PR_CURRENTDIR, mia);
    char riga[512];
    while (fgets(riga, sizeof riga, f)) {
        riga[strcspn(riga, "\r\n")] = 0;
        if (riga[0] && riga[0] != '.' && riga[0] != ';') shell_riga(riga, profondita + 1);
    }
    fclose(f);
    uint32_t ora = R32(os_task + PR_CURRENTDIR);
    W32(os_task + PR_CURRENTDIR, cd);
    uint32_t d1 = D1;
    D1 = ora; d_unlock();
    if (ora != mia) { D1 = mia; d_unlock(); }
    D1 = d1;
}

static void shell_riga(const char *riga_in, int profondita) {
    char riga[1024];
    snprintf(riga, sizeof riga, "%s", riga_in);
    char *parole[MAX_PAROLE];
    int n = dividi_riga(riga, parole);
    if (!n || profondita > 8) return;
    /* redirezione: ">file", "> file", ">>file" */
    const char *uscita = NULL;
    int aggiungi = 0, m = 0;
    for (int i = 0; i < n; i++) {
        char *w = parole[i];
        if (w[0] == '>') {
            aggiungi = w[1] == '>';
            const char *nome = w + 1 + aggiungi;
            if (!*nome && i + 1 < n) nome = parole[++i];
            uscita = nome;
            continue;
        }
        parole[m++] = w;
    }
    n = m;
    if (!n) return;
    FILE *out = stderr;
    if (uscita && strcasecmp(uscita, "NIL:")) {
        char host[1100];
        percorso_host(uscita, host, sizeof host);
        out = fopen(host, aggiungi ? "ab" : "wb");
        if (!out) out = stderr;
    } else if (uscita) out = NULL;
    FILE *scarico = out ? out : fopen("NUL", "wb");
    const char *cmd = parole[0];
    const char *c0 = strrchr(cmd, '/') ? strrchr(cmd, '/') + 1 : strchr(cmd, ':') ? strchr(cmd, ':') + 1 : cmd;
    if (!strcasecmp(c0, "cd")) {
        if (n == 1) {
            uint32_t cd = os_task ? R32(os_task + PR_CURRENTDIR) : 0;
            oggetto_t *o = ogg_di(BADDR(cd));
            char s[1100];
            percorso_amiga(o ? o->percorso : s_dati, s, sizeof s);
            if (scarico) fprintf(scarico, "%s\n", s);
        } else {
            char host[1100];
            percorso_host(parole[1], host, sizeof host);
            size_t l = strlen(host);
            if (l && (host[l - 1] == '/' || host[l - 1] == '\\')) host[l - 1] = 0;
            uint32_t vecchio = R32(os_task + PR_CURRENTDIR);
            W32(os_task + PR_CURRENTDIR, nuovo_lock(host));
            uint32_t d1 = D1; D1 = vecchio; d_unlock(); D1 = d1;
        }
    } else if (!strcasecmp(c0, "assign") && n >= 2) {
        char vol[64], host[1100];
        snprintf(vol, sizeof vol, "%s", parole[1]);
        size_t l = strlen(vol);
        if (l && vol[l - 1] == ':') vol[l - 1] = 0;
        if (n >= 3) { percorso_host(parole[2], host, sizeof host); os_dos_assegna(vol, host); }
    } else if (!strcasecmp(c0, "list")) {
        if (scarico) shell_list(parole, n, scarico);
    } else if (!strcasecmp(c0, "execute") && n >= 2) {
        shell_execute(parole[1], profondita);
    } else if (!strcasecmp(c0, "join")) {
        const char *dest = NULL;
        for (int i = 1; i + 1 < n; i++)
            if (!strcasecmp(parole[i], "TO") || !strcasecmp(parole[i], "AS")) { dest = parole[i + 1]; n = i; break; }
        if (dest) {
            char host[1100];
            percorso_host(dest, host, sizeof host);
            FILE *d = fopen(host, "wb");
            for (int i = 1; d && i < n; i++) {
                char h[1100];
                percorso_host(parole[i], h, sizeof h);
                FILE *s = fopen(h, "rb");
                if (!s) continue;
                char buf[8192];
                size_t k;
                while ((k = fread(buf, 1, sizeof buf, s)) > 0) fwrite(buf, 1, k, d);
                fclose(s);
            }
            if (d) fclose(d);
        }
    } else if (!strcasecmp(c0, "delete")) {
        for (int i = 1; i < n; i++) {
            if (!strcasecmp(parole[i], "ALL") || !strcasecmp(parole[i], "QUIET") || !strcasecmp(parole[i], "FORCE")) continue;
            char host[1100];
            const char *schema = strrchr(parole[i], '/');
            if (schema && (strchr(schema, '#') || strchr(schema, '*'))) {
                char dir[512];
                snprintf(dir, sizeof dir, "%.*s", (int)(schema - parole[i]), parole[i]);
                percorso_host(dir, host, sizeof host);
                DIR *d = opendir(host);
                struct dirent *e;
                while (d && (e = readdir(d)) != NULL) {
                    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..") || !corrisponde(schema + 1, e->d_name)) continue;
                    char h[1400];
                    snprintf(h, sizeof h, "%s/%s", host, e->d_name);
                    remove(h);
                }
                if (d) closedir(d);
            } else {
                percorso_host(parole[i], host, sizeof host);
                if (remove(host)) rmdir(host);
            }
        }
    } else if (!strcasecmp(c0, "makedir")) {
        for (int i = 1; i < n; i++) { char host[1100]; percorso_host(parole[i], host, sizeof host); mkdir(host); }
    } else if (!strcasecmp(c0, "copy") && n >= 3) {
        const char *src = parole[1], *dst = parole[n - 1];
        char hs[1100], hd[1100];
        percorso_host(src, hs, sizeof hs);
        percorso_host(dst, hd, sizeof hd);
        struct stat st;
        if (!stat(hd, &st) && S_ISDIR(st.st_mode)) { strncat(hd, "/", sizeof hd - strlen(hd) - 1); strncat(hd, nome_finale(hs), sizeof hd - strlen(hd) - 1); }
        FILE *s = fopen(hs, "rb"), *d = s ? fopen(hd, "wb") : NULL;
        char buf[8192];
        size_t k;
        while (s && d && (k = fread(buf, 1, sizeof buf, s)) > 0) fwrite(buf, 1, k, d);
        if (s) fclose(s);
        if (d) fclose(d);
    } else if (!strcasecmp(c0, "sort") && n >= 3) {
        /* Sort FROM TO [CASE]: le righe in ordine, senza distinguere le
         * maiuscole se non c'e' CASE */
        int maiuscole = 0;
        const char *da = NULL, *a = NULL;
        for (int i = 1; i < n; i++) {
            if (!strcasecmp(parole[i], "CASE")) maiuscole = 1;
            else if (!strcasecmp(parole[i], "FROM") || !strcasecmp(parole[i], "TO")) continue;
            else if (!da) da = parole[i];
            else if (!a) a = parole[i];
        }
        char hd[1100], ha[1100];
        percorso_host(da, hd, sizeof hd);
        percorso_host(a ? a : da, ha, sizeof ha);
        FILE *s = fopen(hd, "rb");
        if (s) {
            static char righe[4096][256];
            int k = 0;
            while (k < 4096 && fgets(righe[k], sizeof righe[k], s)) { righe[k][strcspn(righe[k], "\r\n")] = 0; k++; }
            fclose(s);
            for (int i = 1; i < k; i++)                     /* inserzione: pochi nomi */
                for (int j = i; j > 0; j--) {
                    int c = maiuscole ? strcmp(righe[j - 1], righe[j]) : strcasecmp(righe[j - 1], righe[j]);
                    if (c <= 0) break;
                    char t[256];
                    memcpy(t, righe[j], sizeof t); memcpy(righe[j], righe[j - 1], sizeof t); memcpy(righe[j - 1], t, sizeof t);
                }
            FILE *d = fopen(ha, "wb");
            for (int i = 0; d && i < k; i++) fprintf(d, "%s\n", righe[i]);
            if (d) fclose(d);
        }
    } else if (!strcasecmp(c0, "echo")) {
        for (int i = 1; i < n && scarico; i++) fprintf(scarico, "%s%s", parole[i], i + 1 < n ? " " : "");
        if (scarico) fputc('\n', scarico);
    } else if (!strcasecmp(c0, "protect") || !strcasecmp(c0, "resident") || !strcasecmp(c0, "wait") ||
               !strcasecmp(c0, "stack") || !strcasecmp(c0, "failat") || !strcasecmp(c0, "setpatch")) {
        /* niente da fare sul PC */
    } else {
        fprintf(stderr, "[dos] Execute: comando \"%s\" non gestito\n", riga_in);
    }
    if (out && out != stderr) fclose(out);
    else if (!out && scarico) fclose(scarico);
}

static void d_execute(void) {
    char c[1024];
    os_leggi_stringa(D1, c, sizeof c);
    if (os_traccia) fprintf(stderr, "[dos] Execute(\"%s\")\n", c);
    shell_riga(c, 0);
    D0 = (uint32_t)-1;
}

static void d_exit(void) {
    fprintf(stderr, "[dos] Exit(%d)\n", (int)D1);
    exit((int)D1);
}

/* ---------------------------------------------------- tabella */

/* ---------------------------------------------------- V36 (Kickstart 2.0+) */

size_t os_formatta(uint32_t f, uint32_t *arg, char *out, size_t n);   /* amiga_exec.c */

static void d_filepart(void) {
    /* FilePart(path D1): dopo l'ultimo ':' o '/' */
    uint32_t p = D1, r = D1;
    if (!p) { D0 = 0; return; }
    for (uint8_t c; (c = R8(p)) != 0; p++) if (c == ':' || c == '/') r = p + 1;
    D0 = r;
}
static void d_pathpart(void) {
    /* PathPart(path D1): l'ultimo '/' (o dopo ':'), dove finisce la cartella */
    uint32_t p = D1, r = D1;
    if (!p) { D0 = 0; return; }
    for (uint8_t c; (c = R8(p)) != 0; p++) {
        if (c == ':') r = p + 1;
        else if (c == '/') r = p;
    }
    D0 = r;
}
static void d_addpart(void) {
    /* AddPart(dirname D1, filename D2, size D3) */
    char dir[512], file[256], out[800];
    os_leggi_stringa(D1, dir, sizeof dir);
    os_leggi_stringa(D2, file, sizeof file);
    size_t l = strlen(dir);
    if (strchr(file, ':') || !l) snprintf(out, sizeof out, "%s", file);
    else if (dir[l - 1] == ':' || dir[l - 1] == '/') snprintf(out, sizeof out, "%s%s", dir, file);
    else snprintf(out, sizeof out, "%s/%s", dir, file);
    if (strlen(out) + 1 > D3) { D0 = 0; return; }
    for (size_t i = 0; i <= strlen(out); i++) W8(D1 + (uint32_t)i, out[i]);
    D0 = 1;
}
static void d_setioerr(void) {
    uint32_t v = R32(os_task + PR_RESULT2);
    W32(os_task + PR_RESULT2, D1);
    D0 = v;
}
static void scrivi_stringa(uint32_t buf, uint32_t len, const char *s) {
    uint32_t i = 0;
    if (!buf || !len) return;
    for (; s[i] && i + 1 < len; i++) W8(buf + i, s[i]);
    W8(buf + i, 0);
}
static void d_fault(void) {
    /* Fault(code D1, header D2, buffer D3, len D4) */
    char h[128], s[300];
    h[0] = 0;
    if (D2) os_leggi_stringa(D2, h, sizeof h);
    snprintf(s, sizeof s, "%s%sError %d", h, h[0] ? ": " : "", (int32_t)D1);
    scrivi_stringa(D3, D4, s);
    D0 = (uint32_t)strlen(s);
}
static void d_printfault(void) {
    char h[128];
    h[0] = 0;
    if (D2) os_leggi_stringa(D2, h, sizeof h);
    fprintf(stderr, "%s%sError %d\n", h, h[0] ? ": " : "", (int32_t)D1);
    D0 = 1;
}
static void d_namefromlock(void) {
    oggetto_t *o = ogg_di(BADDR(D1));
    char s[1100];
    percorso_amiga(o ? o->percorso : s_dati, s, sizeof s);
    if (strlen(s) + 1 > D3) { imposta_errore(120); D0 = 0; return; }   /* ERROR_LINE_TOO_LONG */
    scrivi_stringa(D2, D3, s);
    D0 = 1;
}
static void d_examinefh(void) {
    oggetto_t *o = ogg_di(BADDR(D1));
    if (!o || o->console) { D0 = 0; return; }
    fflush(o->f);
    riempi_fib(D2, o->percorso, nome_finale(o->percorso));
    D0 = 1;
}
static void d_duplockfromfh(void) {
    oggetto_t *o = ogg_di(BADDR(D1));
    D0 = o && !o->console ? nuovo_lock(o->percorso) : 0;
}
static void d_parentoffh(void) {
    oggetto_t *o = ogg_di(BADDR(D1));
    if (!o || o->console) { D0 = 0; return; }
    char h[1024];
    snprintf(h, sizeof h, "%s", o->percorso);
    char *b = strrchr(h, '/'), *c = strrchr(h, '\\');
    if (c > b) b = c;
    if (b) *b = 0;
    D0 = nuovo_lock(h);
}
static void d_openfromlock(void) {
    oggetto_t *l = ogg_di(BADDR(D1));
    if (!l) { D0 = 0; return; }
    FILE *f = fopen(l->percorso, "r+b");
    if (!f) f = fopen(l->percorso, "rb");
    if (!f) { imposta_errore(ERROR_OBJECT_NOT_FOUND); D0 = 0; return; }
    char p[1024];
    snprintf(p, sizeof p, "%s", l->percorso);
    uint32_t d1 = D1;
    d_unlock();                                       /* il lock passa al file */
    D1 = d1;
    uint32_t fh = os_alloca(FH_SIZE, MEMF_PUBLIC | MEMF_CLEAR);
    oggetto_t *o = ogg_nuovo(fh);
    o->f = f;
    snprintf(o->percorso, sizeof o->percorso, "%s", p);
    D0 = MKBADDR(fh);
}
static void d_samelock(void) {
    oggetto_t *a = ogg_di(BADDR(D1)), *b = ogg_di(BADDR(D2));
    D0 = a && b && !strcasecmp(a->percorso, b->percorso) ? 0 : (uint32_t)-1;   /* LOCK_SAME / DIFFERENT */
}

/* I/O a caratteri e con buffer (sul FILE dell'host) */
static void d_fgetc(void) {
    oggetto_t *o = ogg_di(BADDR(D1));
    int c = o && o->f ? fgetc(o->f) : EOF;
    D0 = c == EOF ? (uint32_t)-1 : (uint32_t)c;
}
static void d_fputc(void) {
    oggetto_t *o = ogg_di(BADDR(D1));
    if (!o) { D0 = (uint32_t)-1; return; }
    if (o->console) fputc((int)(D2 & 0xFF), stderr); else fputc((int)(D2 & 0xFF), o->f);
    D0 = D2 & 0xFF;
}
static void d_ungetc(void) {
    oggetto_t *o = ogg_di(BADDR(D1));
    if (!o || !o->f) { D0 = 0; return; }
    if ((int32_t)D2 == -1) { fseek(o->f, -1, SEEK_CUR); D0 = 1; return; }
    D0 = ungetc((int)(D2 & 0xFF), o->f) != EOF;
}
static void d_fgets(void) {
    /* FGets(fh D1, buf D2, len D3): fino a '\n' compreso, 0 a fine file */
    oggetto_t *o = ogg_di(BADDR(D1));
    if (!o || !o->f || D3 < 2) { D0 = 0; return; }
    uint32_t i = 0;
    int c = EOF;
    while (i + 1 < D3 && (c = fgetc(o->f)) != EOF) {
        W8(D2 + i++, c);
        if (c == '\n') break;
    }
    W8(D2 + i, 0);
    D0 = i ? D2 : 0;
}
static void d_fputs(void) {
    oggetto_t *o = ogg_di(BADDR(D1));
    char s[2048];
    os_leggi_stringa(D2, s, sizeof s);
    if (!o) { D0 = (uint32_t)-1; return; }
    fputs(s, o->console ? stderr : o->f);
    D0 = 0;
}
static void d_fread(void) {
    /* FRead(fh D1, buf D2, blocklen D3, blocks D4): blocchi letti */
    oggetto_t *o = ogg_di(BADDR(D1));
    if (!o || !o->f || !D3) { D0 = 0; return; }
    if (D2 + D3 * D4 > g_amiga_mem_size) { D0 = 0; return; }
    D0 = (uint32_t)fread(g_amiga_mem + D2, D3, D4, o->f);
}
static void d_fwrite(void) {
    oggetto_t *o = ogg_di(BADDR(D1));
    if (!o || !D3) { D0 = 0; return; }
    if (o->console) { fwrite(g_amiga_mem + D2, D3, D4, stderr); D0 = D4; return; }
    D0 = (uint32_t)fwrite(g_amiga_mem + D2, D3, D4, o->f);
}
static void d_flush(void) {
    oggetto_t *o = ogg_di(BADDR(D1));
    if (o && o->f) fflush(o->f);
    D0 = 1;
}
static void d_setvbuf(void) { D0 = 0; }
static void d_vfprintf(void) {
    /* VFPrintf(fh D1, format D2, argv D3) */
    oggetto_t *o = ogg_di(BADDR(D1));
    char s[4096];
    uint32_t arg = D3;
    size_t n = os_formatta(D2, &arg, s, sizeof s);
    if (!o) { D0 = (uint32_t)-1; return; }
    fwrite(s, 1, n, o->console ? stderr : o->f);
    D0 = (uint32_t)n;
}
static void d_vprintf(void) {
    /* VPrintf(format D1, argv D2) su Output() */
    uint32_t formato = D1, argv = D2;
    D1 = s_output; D2 = formato; D3 = argv;
    d_vfprintf();
}
static void d_putstr(void) {
    char s[2048];
    os_leggi_stringa(D1, s, sizeof s);
    fputs(s, stderr);
    D0 = 0;
}
static void d_writechars(void) {
    fwrite(g_amiga_mem + D1, 1, D2, stderr);
    D0 = D2;
}

/* variabili d'ambiente: file in ENV: */
static void percorso_var(const char *nome, char *host, size_t n) {
    char p[300];
    snprintf(p, sizeof p, "ENV:%s", nome);
    percorso_host(p, host, n);
}
static void d_getvar(void) {
    /* GetVar(name D1, buffer D2, size D3, flags D4): lunghezza o -1 */
    char nome[256], host[1100];
    os_leggi_stringa(D1, nome, sizeof nome);
    percorso_var(nome, host, sizeof host);
    FILE *f = fopen(host, "rb");
    if (!f) { imposta_errore(ERROR_OBJECT_NOT_FOUND); D0 = (uint32_t)-1; return; }
    uint32_t i = 0;
    int c;
    while (i + 1 < D3 && (c = fgetc(f)) != EOF && c != '\n') W8(D2 + i++, c);
    fclose(f);
    if (D3) W8(D2 + i, 0);
    D0 = i;
}
static void d_setvar(void) {
    /* SetVar(name D1, buffer D2, size D3, flags D4) */
    char nome[256], host[1100];
    os_leggi_stringa(D1, nome, sizeof nome);
    percorso_var(nome, host, sizeof host);
    FILE *f = fopen(host, "wb");
    if (!f) { D0 = 0; return; }
    uint32_t n = (int32_t)D3 < 0 ? 0 : D3;
    if ((int32_t)D3 < 0) while (R8(D2 + n)) n++;
    fwrite(g_amiga_mem + D2, 1, n, f);
    fclose(f);
    D0 = 1;
}
static void d_deletevar(void) {
    char nome[256], host[1100];
    os_leggi_stringa(D1, nome, sizeof nome);
    percorso_var(nome, host, sizeof host);
    D0 = remove(host) == 0;
}

/* la CLI e il programma */
static void d_cli(void) { D0 = os_task ? BADDR(R32(os_task + PR_CLI)) : 0; }
static void d_getprogramdir(void) {
    /* la cartella del programma: quella corrente all'avvio (PROGDIR:) */
    static uint32_t lock;
    if (!lock) {
        char h[1100];
        percorso_host("PROGDIR:", h, sizeof h);
        lock = nuovo_lock(h);
    }
    D0 = lock;
}
static void d_getprogramname(void) {
    uint32_t cli = os_task ? BADDR(R32(os_task + PR_CLI)) : 0;
    uint32_t b = cli ? BADDR(R32(cli + CLI_COMMANDNAME)) : 0;
    char s[256] = "";
    if (b) { uint8_t n = R8(b); for (int i = 0; i < n; i++) s[i] = (char)R8(b + 1 + (uint32_t)i); s[n] = 0; }
    scrivi_stringa(D1, D2, s);
    D0 = 1;
}
static void d_getcurrentdirname(void) {
    oggetto_t *o = ogg_di(BADDR(R32(os_task + PR_CURRENTDIR)));
    char s[1100];
    percorso_amiga(o ? o->percorso : s_dati, s, sizeof s);
    scrivi_stringa(D1, D2, s);
    D0 = 1;
}
static void d_vero(void) { D0 = 1; }
static void d_selectinput(void) { D0 = s_input; s_input = D1; }
static void d_selectoutput(void) { D0 = s_output; s_output = D1; }
static void d_checksignal(void) {
    D0 = R32(os_task + TC_SIGRECVD) & D1;
    W32(os_task + TC_SIGRECVD, R32(os_task + TC_SIGRECVD) & ~D0);
}
static void d_strtolong(void) {
    /* StrToLong(string D1, value D2): caratteri letti, -1 se nessuna cifra */
    char s[64];
    os_leggi_stringa(D1, s, sizeof s);
    char *fine;
    const char *p = s;
    while (*p == ' ' || *p == '\t') p++;
    long v = strtol(p, &fine, 10);
    if (fine == p) { D0 = (uint32_t)-1; return; }
    W32(D2, (uint32_t)v);
    D0 = (uint32_t)(fine - s);
}
static void d_comparedates(void) {
    /* CompareDates(date1 D1, date2 D2): <0 se la prima e' dopo */
    for (int i = 0; i < 3; i++) {
        int32_t a = (int32_t)R32(D1 + 4u * i), b = (int32_t)R32(D2 + 4u * i);
        if (a != b) { D0 = (uint32_t)(b - a); return; }
    }
    D0 = 0;
}
static void d_allocdosobject(void) {
    /* AllocDosObject(type D1, tags D2): DOS_FILEHANDLE 0, DOS_FIB 2,
     * DOS_RDARGS 5 (gli altri: niente) */
    static const uint32_t dim[] = { FH_SIZE, 0, 260, 0, 0, 32 };
    if (D1 > 5 || !dim[D1]) { os_manca("dos.library", "AllocDosObject (tipo)", 228); D0 = 0; return; }
    D0 = os_alloca(dim[D1], MEMF_PUBLIC | MEMF_CLEAR);
}
static void d_freedosobject(void) {
    static const uint32_t dim[] = { FH_SIZE, 0, 260, 0, 0, 32 };
    if (D1 <= 5 && dim[D1] && D2) os_libera(D2, dim[D1]);
}
static void d_isfilesystem(void) { D0 = 1; }

#define N_DOS 33
/* le funzioni di dos partono da -30 (Open); -6..-24 sono Open/Close/Expunge
 * della libreria, gestite dal nucleo */
#define F(lvo, fn) [(lvo) / 6 - 1] = fn
#define N_DOS_V40 166                  /* fino a SetOwner (-996); i nomi V36+ in amiga_nomi_v40.c */
static const os_funzione s_fn[N_DOS_V40] = {
    F(30, d_open), F(36, d_close), F(42, d_read), F(48, d_write), F(54, d_input), F(60, d_output),
    F(66, d_seek), F(72, d_deletefile), F(78, d_rename), F(84, d_lock), F(90, d_unlock),
    F(96, d_duplock), F(102, d_examine), F(108, d_exnext), F(120, d_createdir),
    F(126, d_currentdir), F(132, d_ioerr), F(144, d_exit), F(150, d_loadseg), F(156, d_unloadseg),
    F(186, d_setprotection), F(192, d_datestamp), F(198, d_delay), F(210, d_parentdir), F(216, d_isinteractive),
    F(222, d_execute),
    /* V36 */
    F(228, d_allocdosobject), F(234, d_freedosobject), F(294, d_selectinput), F(300, d_selectoutput),
    F(306, d_fgetc), F(312, d_fputc), F(318, d_ungetc), F(324, d_fread), F(330, d_fwrite),
    F(336, d_fgets), F(342, d_fputs), F(354, d_vfprintf), F(360, d_flush), F(366, d_setvbuf),
    F(372, d_duplockfromfh), F(378, d_openfromlock), F(384, d_parentoffh), F(390, d_examinefh),
    F(402, d_namefromlock), F(420, d_samelock), F(462, d_setioerr), F(468, d_fault),
    F(474, d_printfault), F(492, d_cli), F(558, d_vero), F(564, d_getcurrentdirname),
    F(570, d_vero), F(576, d_getprogramname), F(600, d_getprogramdir), F(708, d_isfilesystem),
    F(738, d_comparedates), F(792, d_checksignal), F(816, d_strtolong), F(870, d_filepart),
    F(876, d_pathpart), F(882, d_addpart), F(900, d_setvar), F(906, d_getvar), F(912, d_deletevar),
    F(942, d_writechars), F(948, d_putstr), F(954, d_vprintf),
};
static const char *const s_nomi_tutti[N_DOS_V40] = {
    "Open*", "Close*", "Expunge", "Reserved", "Open", "Close", "Read", "Write", "Input", "Output",
    "Seek", "DeleteFile", "Rename", "Lock", "UnLock", "DupLock", "Examine", "ExNext", "Info",
    "CreateDir", "CurrentDir", "IoErr", "CreateProc", "Exit", "LoadSeg", "UnLoadSeg", "GetPacket",
    "QueuePacket", "DeviceProc", "SetComment", "SetProtection", "DateStamp", "Delay",
    "WaitForChar", "ParentDir", "IsInteractive", "Execute",
};

void os_dos_init(const char *cartella) {
    snprintf(s_dati, sizeof s_dati, "%s", cartella);
    for (char *p = s_dati; *p; p++) if (*p == '\\') *p = '/';
    size_t l = strlen(s_dati);
    if (l && s_dati[l - 1] == '/') s_dati[l - 1] = 0;
    static const os_libreria_c def = { "dos.library", 40, N_DOS_V40, 70, s_nomi_tutti, s_fn };
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

/* la cartella corrente del Process, se non e' la radice dei dati (Total
 * Chaos parte da "CD SYS:TotalChaos") */
void os_dos_cd(const char *cartella_host) {
    W32(os_task + PR_CURRENTDIR, nuovo_lock(cartella_host));
}
