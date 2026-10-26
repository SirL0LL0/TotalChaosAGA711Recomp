/* amiga_debug.c - registrazione e finestra di debug, solo con --debug o
 * AMIGA_DEBUG (e nelle prove senza finestra). Gli agganci del gioco alle
 * chiamate (g_debug_gioco.agganci) funzionano sempre. */
#include "amiga_debug.h"
#include "amiga_hw.h"
#include "genesis_runtime.h"
#include "font_debug.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <direct.h>

uint8_t g_debug_osservati[0x100000 / 8];

/* ------------------------------------------------------------ anelli */

#define N_CHIAMATE 8192
#define N_SCRITTURE 4096
#define N_MESSAGGI 256

typedef struct { uint64_t quadro; uint32_t ritorno, a7; } chiamata_t;
typedef struct { uint64_t quadro; uint32_t indirizzo, vecchio, nuovo, dove; uint8_t taglia; } scrittura_t;
typedef struct { uint64_t quadro; int numero; char testo[256]; } messaggio_t;

static chiamata_t s_ch[N_CHIAMATE];
static scrittura_t s_scr[N_SCRITTURE];
static messaggio_t s_msg[N_MESSAGGI];
static uint64_t s_n_ch, s_n_scr, s_n_msg, s_quadro;
static uint32_t s_ultima_chiamata;
static FILE *s_log_msg;
static int s_attivo;

/* ------------------------------------------------------------ nomi */

typedef struct { uint32_t a; char nome[64]; } nome_t;
static nome_t *s_nomi;
static int s_n_nomi;

static int cmp_nome(const void *x, const void *y) {
    uint32_t a = ((const nome_t *)x)->a, b = ((const nome_t *)y)->a;
    return a < b ? -1 : a > b;
}

static void carica_nomi(const char *csv) {
    FILE *f = csv ? fopen(csv, "r") : NULL;
    if (!f) return;
    char riga[512];
    int cap = 0;
    while (fgets(riga, sizeof riga, f)) {
        if (riga[0] == '#' || riga[0] == '\n') continue;
        char *v = strchr(riga, ',');
        if (!v) continue;
        *v++ = 0;
        char *fine = strchr(v, ',');
        if (fine) *fine = 0;
        if (s_n_nomi == cap) {
            cap = cap ? cap * 2 : 256;
            s_nomi = realloc(s_nomi, (size_t)cap * sizeof *s_nomi);
        }
        s_nomi[s_n_nomi].a = (uint32_t)strtoul(riga, NULL, 16);
        snprintf(s_nomi[s_n_nomi].nome, sizeof s_nomi[0].nome, "%s", v);
        s_n_nomi++;
    }
    fclose(f);
    qsort(s_nomi, (size_t)s_n_nomi, sizeof *s_nomi, cmp_nome);
}

/* "funzione+$off" della funzione che contiene a (l'ultima che inizia prima) */
static const char *dove(uint32_t a, char *buf, size_t n) {
    int lo = 0, hi = s_n_nomi - 1, k = -1;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        if (s_nomi[m].a <= a) { k = m; lo = m + 1; } else hi = m - 1;
    }
    if (k >= 0 && a - s_nomi[k].a < 0x1000) snprintf(buf, n, "%s+$%X", s_nomi[k].nome, a - s_nomi[k].a);
    else snprintf(buf, n, "$%06X", a);
    return buf;
}

/* ------------------------------------------------------------ registrazione */

void amiga_debug_chiamata(uint32_t ritorno, uint32_t a7) {
    chiamata_t *c = &s_ch[s_n_ch++ % N_CHIAMATE];
    c->quadro = s_quadro; c->ritorno = ritorno; c->a7 = a7;
    s_ultima_chiamata = ritorno;
    for (int i = 0; i < g_debug_gioco.n_agganci; i++)
        if (g_debug_gioco.agganci[i].ritorno == ritorno) g_debug_gioco.agganci[i].fn(ritorno);
}

void amiga_debug_scrittura(uint32_t indirizzo, int taglia, uint32_t vecchio, uint32_t nuovo) {
    if (vecchio == nuovo) return;
    scrittura_t *s = &s_scr[s_n_scr++ % N_SCRITTURE];
    s->quadro = s_quadro; s->indirizzo = indirizzo; s->taglia = (uint8_t)taglia;
    s->vecchio = vecchio; s->nuovo = nuovo; s->dove = s_ultima_chiamata;
}

void amiga_debug_messaggio(int numero, const char *testo) {
    messaggio_t *m = &s_msg[s_n_msg++ % N_MESSAGGI];
    m->quadro = s_quadro; m->numero = numero;
    snprintf(m->testo, sizeof m->testo, "%s", testo);
    if (s_log_msg) { fprintf(s_log_msg, "q%llu #%d: %s\n", (unsigned long long)s_quadro, numero, testo); fflush(s_log_msg); }
}

/* ------------------------------------------------------------ copertura del giocatore */

debug_stat_t g_dstat;

#define N_EVENTI 32768
typedef struct { char chiave[100]; uint64_t prima, sessione; } evento_t;
static evento_t *s_ev;                       /* tabella aperta: "tipo:nome" */
static int s_n_ev;
static char s_base[1024];                    /* cartella dell'eseguibile */

static uint32_t hash(const char *s) {
    uint32_t h = 2166136261u;
    for (; *s; s++) h = (h ^ (uint8_t)*s) * 16777619u;
    return h;
}

static evento_t *evento(const char *chiave) {
    if (!s_ev) return NULL;
    for (uint32_t i = hash(chiave) % N_EVENTI, n = 0; n < N_EVENTI; i = (i + 1) % N_EVENTI, n++) {
        if (!s_ev[i].chiave[0]) {
            if (s_n_ev >= N_EVENTI - 16) return NULL;
            snprintf(s_ev[i].chiave, sizeof s_ev[i].chiave, "%s", chiave);
            s_n_ev++;
            return &s_ev[i];
        }
        if (!strcmp(s_ev[i].chiave, chiave)) return &s_ev[i];
    }
    return NULL;
}

#define N_ULTIMI 16
static struct { uint64_t quadro; char testo[100]; int nuovo; } s_ultimi[N_ULTIMI];
static uint64_t s_n_ultimi;

static void percorso(char *p, size_t n, const char *nome);

/* esame/novita.txt: aperto alla prima riga, in coda */
static FILE *s_esame;
static int s_esame_righe, s_esame_nuovi, s_esame_attivo;

void amiga_esame(const char *formato, ...) {
    if (!s_esame_attivo) return;                 /* solo nelle partite con --debug */
    if (!s_esame) {
        char p[1100];
        percorso(p, sizeof p, "esame");
        _mkdir(p);
        percorso(p, sizeof p, "esame/novita.txt");
        s_esame = fopen(p, "a");
        if (!s_esame) return;
        time_t t = time(NULL);
        char d[32];
        strftime(d, sizeof d, "%Y-%m-%d %H:%M:%S", localtime(&t));
        fprintf(s_esame, "== sessione del %s\n", d);
    }
    if (s_esame_righe++ > 20000) return;         /* una sessione impazzita non riempie il disco */
    va_list a;
    va_start(a, formato);
    fprintf(s_esame, "q%-7llu ", (unsigned long long)s_quadro);
    vfprintf(s_esame, formato, a);
    va_end(a);
    fputc('\n', s_esame);
    fflush(s_esame);
}

void amiga_debug_evento_gioco(const char *tipo, const char *nome) {
    if (!s_ev) return;
    char k[100];
    snprintf(k, sizeof k, "%s\t%s", tipo, nome);
    evento_t *e = evento(k);
    if (!e) return;
    int nuovo = !e->prima && !e->sessione;       /* mai visto in nessuna sessione */
    e->sessione++;
    if (nuovo) { s_esame_nuovi++; amiga_esame("nuovo %s: %s", tipo, nome); }
    if (!strcmp(tipo, "tasto")) return;
    int u = (int)(s_n_ultimi++ % N_ULTIMI);
    s_ultimi[u].quadro = s_quadro;
    s_ultimi[u].nuovo = nuovo;
    snprintf(s_ultimi[u].testo, sizeof s_ultimi[u].testo, "%-6s %s", tipo, nome);
}

/* funzioni di sistema: un contatore per (libreria, voce); in tabella al salvataggio */
#define N_LIB 48
#define N_VOCI 512
static uint64_t s_api[N_LIB][N_VOCI], s_api_salvate[N_LIB][N_VOCI];
static const char *s_api_nome[N_LIB][N_VOCI], *s_api_lib[N_LIB];

void amiga_debug_api(int lib, int k, const char *libreria, const char *funzione) {
    g_dstat.os++;
    if (lib < 0 || lib >= N_LIB || k < 0 || k >= N_VOCI) return;
    if (!s_api[lib][k]++) { s_api_nome[lib][k] = funzione; s_api_lib[lib] = libreria; }
}

static uint8_t *s_cop;                       /* punti di chiamata di questa sessione */
static uint8_t *s_cop_prima, *s_siti;        /* sessioni precedenti; tutti i punti del programma */
static uint32_t s_cop_byte, s_inizio_programma, s_fine_programma;

static void percorso(char *p, size_t n, const char *nome) { snprintf(p, n, "%s%s", s_base, nome); }

/* il programma (non le librerie): da s_inizio_programma, fino a
 * s_fine_programma se c'e' (Chaos 2.5d: le librerie stanno dopo) */
static int nel_programma(uint32_t a) {
    return a >= s_inizio_programma && (!s_fine_programma || a < s_fine_programma);
}

static uint8_t *leggi_bitmap(const char *nome, uint32_t byte, uint32_t *testa) {
    char p[1100];
    percorso(p, sizeof p, nome);
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    uint8_t *b = calloc(1, byte);
    if (testa) {
        /* copertura_siti.bin: 4 byte con l'inizio del programma, o 8 con
         * inizio e fine (si capisce dalla lunghezza del file) */
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        uint8_t t[8] = { 0 };
        int k = n == (long)byte + 8 ? 8 : 4;
        if (fread(t, 1, (size_t)k, f) == (size_t)k) {
            *testa = (uint32_t)t[0] << 24 | t[1] << 16 | t[2] << 8 | t[3];
            if (k == 8) s_fine_programma = (uint32_t)t[4] << 24 | t[5] << 16 | t[6] << 8 | t[7];
        }
    }
    if (b && fread(b, 1, byte, f) == 0) { free(b); b = NULL; }
    fclose(f);
    return b;
}

void amiga_debug_copertura(uint8_t *bitmap, uint32_t byte) {
    s_cop = bitmap;
    s_cop_byte = byte;
    s_cop_prima = leggi_bitmap("copertura_giocatore.bin", byte, NULL);
    if (!s_cop_prima) s_cop_prima = calloc(1, byte);
    s_siti = leggi_bitmap("copertura_siti.bin", byte, &s_inizio_programma);
}

static void carica_eventi(void) {
    char p[1100], riga[512];
    percorso(p, sizeof p, "copertura_giocatore.txt");
    FILE *f = fopen(p, "r");
    if (!f) return;
    while (fgets(riga, sizeof riga, f)) {
        char *t1 = strchr(riga, '\t'), *t2 = t1 ? strchr(t1 + 1, '\t') : NULL;
        if (!t2 || riga[0] == '#') continue;
        *t2++ = 0;
        evento_t *e = evento(riga);
        if (e) e->prima += strtoull(t2, NULL, 10);
    }
    fclose(f);
}

void amiga_debug_salva(void) {
    if (!s_ev) return;
    for (int l = 0; l < N_LIB; l++)
        for (int k = 0; k < N_VOCI; k++) {
            uint64_t d = s_api[l][k] - s_api_salvate[l][k];
            if (!d) continue;
            char n[100];
            if (s_api_nome[l][k]) snprintf(n, sizeof n, "%s/%s", s_api_lib[l], s_api_nome[l][k]);
            else snprintf(n, sizeof n, "%s/LVO-%d", s_api_lib[l], 6 * (k + 1));
            char kk[100];
            snprintf(kk, sizeof kk, "api\t%s", n);
            evento_t *e = evento(kk);
            if (e) e->sessione += d;
            s_api_salvate[l][k] = s_api[l][k];
        }
    char p[1100], q[1100];
    percorso(p, sizeof p, "copertura_giocatore.txt.nuovo");
    percorso(q, sizeof q, "copertura_giocatore.txt");
    FILE *f = fopen(p, "w");
    if (f) {
        fprintf(f, "# copertura delle partite giocate con --debug: tipo, nome, volte (tutte le sessioni)\n");
        for (int i = 0; i < N_EVENTI; i++)
            if (s_ev[i].chiave[0])
                fprintf(f, "%s\t%llu\n", s_ev[i].chiave, (unsigned long long)(s_ev[i].prima + s_ev[i].sessione));
        fclose(f);
        remove(q);
        rename(p, q);
    }
    if (s_cop && s_cop_prima) {
        percorso(p, sizeof p, "copertura_giocatore.bin.nuovo");
        percorso(q, sizeof q, "copertura_giocatore.bin");
        f = fopen(p, "wb");
        if (f) {
            for (uint32_t i = 0; i < s_cop_byte; i++) fputc(s_cop[i] | s_cop_prima[i], f);
            fclose(f);
            remove(q);
            rename(p, q);
        }
    }
}

/* a fine sessione: quanti eventi nuovi e quanti punti del programma mai
 * raggiunti prima */
static void esame_fine(void) {
    uint32_t nuovi = 0;
    for (uint32_t i = 0; s_cop && s_cop_prima && i < s_cop_byte; i++) {
        uint8_t b = (uint8_t)(s_cop[i] & ~s_cop_prima[i]);
        while (b) { nuovi += b & 1; b >>= 1; }
    }
    amiga_esame("fine sessione: %d eventi nuovi, %u punti del programma raggiunti per la prima volta", s_esame_nuovi, nuovi);
}

/* ------------------------------------------------------------ valori */

static void valore(const debug_var_t *v, char *buf, size_t n) {
    uint32_t a = v->indirizzo;
    switch (v->formato) {
    case DV_U8:    snprintf(buf, n, "%u", m68k_read8(a)); break;
    case DV_U16:   snprintf(buf, n, "%u", m68k_read16(a)); break;
    case DV_U32:   snprintf(buf, n, "%u", m68k_read32(a)); break;
    case DV_HEX8:  snprintf(buf, n, "$%02X", m68k_read8(a)); break;
    case DV_HEX16: snprintf(buf, n, "$%04X", m68k_read16(a)); break;
    case DV_CIFRE8: {
        char c[9]; int i = 0;
        for (; i < 8; i++) { uint8_t d = m68k_read8(a + (uint32_t)i); c[i] = (char)(d <= 9 ? '0' + d : '?'); }
        c[8] = 0;
        const char *p = c; while (*p == '0' && p[1]) p++;
        snprintf(buf, n, "%s", p);
        break;
    }
    }
}

/* ------------------------------------------------------------ finestra */

#define DW 1280
#define DH 960
static SDL_Window *s_win;
static SDL_Renderer *s_ren;
static SDL_Texture *s_tex;
static uint32_t s_pix[DW * DH];
static int s_aperta;

static void testo(int x, int y, uint32_t col, const char *s) {
    for (; *s && x + FONT_DEBUG_L <= DW; s++, x += FONT_DEBUG_L) {
        unsigned c = (unsigned char)*s;
        if (c == 0xC2 && (unsigned char)s[1] == 0xA3) { c = 'L'; s++; }   /* sterlina in UTF-8 */
        else if (c < 32 || c > 126) c = '?';
        const uint8_t *g = font_debug[c - 32];
        for (int r = 0; r < FONT_DEBUG_A && y + r < DH; r++)
            for (int b = 0; b < 8; b++)
                if (g[r] & (0x80 >> b)) s_pix[(y + r) * DW + x + b] = col;
    }
}

static void apri(void) {
    if (s_win) { SDL_ShowWindow(s_win); s_aperta = 1; return; }
    s_win = SDL_CreateWindow("Debug", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, DW, DH, 0);
    s_ren = SDL_CreateRenderer(s_win, -1, 0);
    s_tex = SDL_CreateTexture(s_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, DW, DH);
    s_aperta = 1;
}

/* ---- misure al secondo (orologio vero) */
typedef struct {
    uint32_t ms;
    uint64_t quadro, quadri_cpu, attesa_us, vhpos;
    debug_stat_t st;
    uint64_t api[N_LIB][N_VOCI];
} campione_t;
static campione_t s_prima, s_ora;            /* gli ultimi due secondi */
static struct {
    double fps, carico, chiamate, os, interp, salti, cl, cs, cia, vhpos, cpu_pct;
    int cop_sessione, cop_totale, cop_nuovi, siti, siti_prog, cop_prog, cop_lib;
    int api_distinte, ev_tipo[8];
} s_m;
static const char *const s_tipi[8] = { "api", "suono", "file", "menu", "tasto", "schermo", "manca", NULL };

static int bit(const uint8_t *b, uint32_t i) { return b && (b[i >> 3] >> (i & 7) & 1); }

static void misura(void) {
    extern uint64_t g_quadri_cpu, g_tempo_attesa_us;
    uint32_t ms = SDL_GetTicks();
    if (s_ora.ms && ms - s_ora.ms < 1000) return;
    s_prima = s_ora;
    s_ora.ms = ms; s_ora.quadro = s_quadro; s_ora.quadri_cpu = g_quadri_cpu; s_ora.attesa_us = g_tempo_attesa_us;
    s_ora.vhpos = g_amiga_letture_vhpos; s_ora.st = g_dstat;
    memcpy(s_ora.api, s_api, sizeof s_api);
    if (!s_prima.ms) return;
    double sec = (ms - s_prima.ms) / 1000.0;
#define AL_SEC(x) ((double)(s_ora.x - s_prima.x) / sec)
    s_m.fps = AL_SEC(quadro);
    s_m.cpu_pct = s_ora.quadro > s_prima.quadro
        ? 100.0 * (double)(s_ora.quadri_cpu - s_prima.quadri_cpu) / (double)(s_ora.quadro - s_prima.quadro) : 0;
    s_m.carico = 100.0 - 100.0 * (double)(s_ora.attesa_us - s_prima.attesa_us) / (sec * 1e6);
    s_m.chiamate = AL_SEC(st.chiamate); s_m.os = AL_SEC(st.os); s_m.interp = AL_SEC(st.interpretate);
    s_m.salti = AL_SEC(st.salti); s_m.cl = AL_SEC(st.custom_l); s_m.cs = AL_SEC(st.custom_s);
    s_m.cia = AL_SEC(st.cia); s_m.vhpos = AL_SEC(vhpos);
#undef AL_SEC
    /* copertura: i punti di chiamata (un bit per indirizzo pari) */
    int ses = 0, tot = 0, nuovi = 0, siti = 0, siti_p = 0, cop_p = 0, cop_l = 0;
    for (uint32_t i = 0; s_cop && i < s_cop_byte * 8; i++) {
        int a = bit(s_cop, i), p = bit(s_cop_prima, i), s = s_siti ? bit(s_siti, i) : 1;
        if (!(a | p) && !s_siti) continue;
        if (s_siti && s) { siti++; if (nel_programma(2 * i)) siti_p++; }
        if ((a | p) && s) { tot++; if (nel_programma(2 * i)) cop_p++; else cop_l++; }
        ses += a; nuovi += a && !p;
    }
    s_m.cop_sessione = ses; s_m.cop_totale = tot; s_m.cop_nuovi = nuovi; s_m.siti = siti; s_m.siti_prog = siti_p;
    s_m.cop_prog = cop_p; s_m.cop_lib = cop_l;
    int n = 0;
    for (int l = 0; l < N_LIB; l++) for (int k = 0; k < N_VOCI; k++) n += s_api[l][k] != 0;
    s_m.api_distinte = n;
    memset(s_m.ev_tipo, 0, sizeof s_m.ev_tipo);
    for (int i = 0; s_ev && i < N_EVENTI; i++) {
        if (!s_ev[i].chiave[0]) continue;
        for (int t = 0; s_tipi[t]; t++) {
            size_t l = strlen(s_tipi[t]);
            if (!strncmp(s_ev[i].chiave, s_tipi[t], l) && s_ev[i].chiave[l] == '\t') { s_m.ev_tipo[t]++; break; }
        }
    }
    /* ogni 30 s su disco, cosi' anche un crash lascia la copertura */
    static uint32_t salvato;
    if (ms - salvato > 30000) { salvato = ms; amiga_debug_salva(); }
}

static void barra(int x, int y, int largo, double pct, uint32_t col) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    int pieno = (int)(largo * pct / 100.0);
    for (int r = 2; r < FONT_DEBUG_A - 2; r++)
        for (int c = 0; c < largo; c++)
            s_pix[(y + r) * DW + x + c] = c < pieno ? col : 0xFF2A3440;
}

static void disegna(void) {
    const uint32_t BIANCO = 0xFFE8E8E8, GIALLO = 0xFFFFC040, CIANO = 0xFF60D0FF, GRIGIO = 0xFF8090A0,
                   VERDE = 0xFF60E070, ROSSO = 0xFFFF6060;
    char b[256], d[96];
    misura();
    for (int i = 0; i < DW * DH; i++) s_pix[i] = 0xFF101820;
    int L = FONT_DEBUG_A + 1, y = 6, X2 = 660;
    snprintf(b, sizeof b, "QUADRO %llu   %.1f fps (%.0f%% di 50)   F11 chiude, Shift+F11 dump",
             (unsigned long long)s_quadro, s_m.fps, s_m.fps * 2);
    testo(8, y, GIALLO, b); y += L + 6;

    /* ---- colonna sinistra */
    testo(8, y, CIANO, "ESECUZIONE"); y += L;
    snprintf(b, sizeof b, "carico del PC      %5.1f%%   (tempo non passato ad aspettare il quadro)", s_m.carico);
    testo(16, y, BIANCO, b); y += L;
    snprintf(b, sizeof b, "quadri per cicli   %5.1f%%   (il resto: il gioco aspetta, Wait/Delay)", s_m.cpu_pct);
    testo(16, y, BIANCO, b); y += L;
    extern int game_dispatch_table_size(void);
    snprintf(b, sizeof b, "chiamate ricompilate %10.0f/s   funzioni ricompilate %d", s_m.chiamate, game_dispatch_table_size());
    testo(16, y, BIANCO, b); y += L;
    snprintf(b, sizeof b, "funzioni di sistema  %10.0f/s   (AmigaOS in C, nativo)", s_m.os);
    testo(16, y, BIANCO, b); y += L;
    snprintf(b, sizeof b, "Musashi (interprete) %10.0f/s   salti indiretti %8.0f/s", s_m.interp, s_m.salti);
    testo(16, y, s_m.interp > 0 ? GIALLO : BIANCO, b); y += L + 6;

    testo(8, y, CIANO, "COPERTURA (questa sessione + le precedenti con --debug)"); y += L;
    if (s_siti && s_m.siti) {
        double pp = 100.0 * s_m.cop_prog / (s_m.siti_prog ? s_m.siti_prog : 1);
        double pl = 100.0 * s_m.cop_lib / ((s_m.siti - s_m.siti_prog) ? s_m.siti - s_m.siti_prog : 1);
        snprintf(b, sizeof b, "programma  %5d/%-5d %5.1f%%", s_m.cop_prog, s_m.siti_prog, pp);
        testo(16, y, BIANCO, b); barra(300, y, 300, pp, VERDE); y += L;
        snprintf(b, sizeof b, "librerie   %5d/%-5d %5.1f%%", s_m.cop_lib, s_m.siti - s_m.siti_prog, pl);
        testo(16, y, BIANCO, b); barra(300, y, 300, pl, VERDE); y += L;
    } else if (s_cop) {
        testo(16, y, GRIGIO, "(manca copertura_siti.bin accanto all'eseguibile: niente percentuali)"); y += L;
    } else {
        testo(16, y, GRIGIO, "(copertura spenta: si accende con --debug)"); y += L;
    }
    snprintf(b, sizeof b, "punti di chiamata: sessione %d, in tutto %d, nuovi oggi %d", s_m.cop_sessione,
             s_m.cop_totale, s_m.cop_nuovi);
    testo(16, y, s_m.cop_nuovi ? VERDE : BIANCO, b); y += L;
    snprintf(b, sizeof b, "visti in tutto: api %d  suoni %d  file %d  voci di menu %d  tasti %d",
             s_m.ev_tipo[0] > s_m.api_distinte ? s_m.ev_tipo[0] : s_m.api_distinte,
             s_m.ev_tipo[1], s_m.ev_tipo[2], s_m.ev_tipo[3], s_m.ev_tipo[4]);
    testo(16, y, BIANCO, b); y += L;
    snprintf(b, sizeof b, "funzioni di sistema usate in questa sessione %d; mancanti %d", s_m.api_distinte, s_m.ev_tipo[6]);
    testo(16, y, s_m.ev_tipo[6] ? ROSSO : BIANCO, b); y += L + 6;

    testo(8, y, CIANO, "HARDWARE"); y += L;
    snprintf(b, sizeof b, "chip custom: letture %8.0f/s  scritture %8.0f/s", s_m.cl, s_m.cs);
    testo(16, y, BIANCO, b); y += L;
    snprintf(b, sizeof b, "CIA: accessi %8.0f/s   VHPOSR letto %8.0f/s", s_m.cia, s_m.vhpos);
    testo(16, y, BIANCO, b); y += L;
    uint16_t dmacon, intena, intreq; uint8_t icr;
    extern void amiga_hw_stato(uint16_t *, uint16_t *, uint16_t *, uint8_t *);
    amiga_hw_stato(&dmacon, &intena, &intreq, &icr);
    snprintf(b, sizeof b, "DMACON $%04X  INTENA $%04X  INTREQ $%04X  ICR CIA-A $%02X", dmacon, intena, intreq, icr);
    testo(16, y, BIANCO, b); y += L;
    snprintf(b, sizeof b, "CIA-A timer A %5u %s  B %5u %s    CIA-B timer A %5u %s  B %5u %s",
             amiga_hw_ciaa_timer(0), amiga_hw_ciaa_avviato(0) ? "on " : "off", amiga_hw_ciaa_timer(1),
             amiga_hw_ciaa_avviato(1) ? "on " : "off", amiga_hw_ciab_timer(0), amiga_hw_ciab_avviato(0) ? "on " : "off",
             amiga_hw_ciab_timer(1), amiga_hw_ciab_avviato(1) ? "on " : "off");
    testo(16, y, BIANCO, b); y += L;
    extern void amiga_paula_stato(int, uint32_t *, uint16_t *, uint16_t *, uint16_t *);
    extern int amiga_paula_acceso(int);
    for (int n = 0; n < 4; n++) {
        uint32_t lc; uint16_t len, per, vol;
        amiga_paula_stato(n, &lc, &len, &per, &vol);
        int on = amiga_paula_acceso(n);
        snprintf(b, sizeof b, "Paula %d %s  $%06X  %5u parole  periodo %5u (%5.0f Hz)  vol %2u", n, on ? "ON " : "off",
                 lc, len, per, per ? 3546895.0 / per : 0.0, vol);
        testo(16, y, on ? VERDE : GRIGIO, b);
        if (on) barra(560, y, 80, vol * 100.0 / 64, VERDE);
        y += L;
    }
    y += 6;

    testo(8, y, CIANO, "MEMORIA E FILE"); y += L;
    extern uint32_t os_disponibile(uint32_t flag);
    snprintf(b, sizeof b, "chip libera %6u KB   fast libera %7u KB", os_disponibile(2) >> 10, os_disponibile(4) >> 10);
    testo(16, y, BIANCO, b); y += L;
    snprintf(b, sizeof b, "file aperti %llu   effetti/voci %llu (saltati per canali occupati %llu)",
             (unsigned long long)g_dstat.file, (unsigned long long)g_dstat.suoni, (unsigned long long)g_dstat.suoni_saltati);
    testo(16, y, g_dstat.suoni_saltati ? GIALLO : BIANCO, b); y += L + 6;

    testo(8, y, CIANO, "ULTIME CHIAMATE (chi chiama, dal punto di ritorno)"); y += L;
    for (int i = 0; y < DH - L && (uint64_t)i < s_n_ch; i++) {
        chiamata_t *c = &s_ch[(s_n_ch - 1 - (uint64_t)i) % N_CHIAMATE];
        snprintf(b, sizeof b, "q%-7llu %-40s A7=$%06X", (unsigned long long)c->quadro, dove(c->ritorno, d, sizeof d), c->a7);
        testo(16, y, i ? GRIGIO : BIANCO, b); y += L;
    }

    /* ---- colonna destra */
    y = 6 + L + 6;
    testo(X2, y, CIANO, "FUNZIONI DI SISTEMA PIU' USATE (ultimo secondo)"); y += L;
    {
        int scelte[16][2], n = 0;
        uint64_t usate[16];
        for (int l = 0; l < N_LIB; l++)
            for (int k = 0; k < N_VOCI; k++) {
                uint64_t v = s_ora.api[l][k] - s_prima.api[l][k];
                if (!v) continue;
                if (n == 16 && v <= usate[15]) continue;     /* piena: entra solo chi supera l'ultima */
                int j = n < 16 ? n++ : 15;
                while (j > 0 && usate[j - 1] < v) { usate[j] = usate[j - 1]; scelte[j][0] = scelte[j - 1][0]; scelte[j][1] = scelte[j - 1][1]; j--; }
                usate[j] = v; scelte[j][0] = l; scelte[j][1] = k;
            }
        for (int i = 0; i < n; i++) {
            int l = scelte[i][0], k = scelte[i][1];
            snprintf(b, sizeof b, "%7llu/s  %s/%s  (%llu)", (unsigned long long)usate[i], s_api_lib[l] ? s_api_lib[l] : "?",
                     s_api_nome[l][k] ? s_api_nome[l][k] : "?", (unsigned long long)s_api[l][k]);
            testo(X2 + 8, y, BIANCO, b); y += L;
        }
        y += (16 - n) * L;
    }
    y += 6;
    testo(X2, y, CIANO, "ULTIMI EVENTI (verde = mai visto prima)"); y += L;
    for (int i = 0; i < N_ULTIMI && (uint64_t)i < s_n_ultimi; i++) {
        int u = (int)((s_n_ultimi - 1 - (uint64_t)i) % N_ULTIMI);
        snprintf(b, sizeof b, "q%-7llu %.70s", (unsigned long long)s_ultimi[u].quadro, s_ultimi[u].testo);
        testo(X2 + 8, y, s_ultimi[u].nuovo ? VERDE : (i ? GRIGIO : BIANCO), b); y += L;
    }
    y += 6;
    if (g_debug_gioco.n_var) {
        testo(X2, y, CIANO, "VARIABILI"); y += L;
        for (int i = 0; i < g_debug_gioco.n_var && y < DH - L; i++) {
            char v[32];
            valore(&g_debug_gioco.var[i], v, sizeof v);
            snprintf(b, sizeof b, "%-26s %-12s $%05X", g_debug_gioco.var[i].nome, v, g_debug_gioco.var[i].indirizzo);
            testo(X2 + 8, y, BIANCO, b); y += L;
        }
        y += 6;
    }
    testo(X2, y, CIANO, "ULTIMI MESSAGGI DEL GIOCO"); y += L;
    for (int i = 0; i < 6 && (uint64_t)i < s_n_msg && y < DH - L; i++) {
        messaggio_t *m = &s_msg[(s_n_msg - 1 - (uint64_t)i) % N_MESSAGGI];
        snprintf(b, sizeof b, "q%-6llu #%-3d %.60s", (unsigned long long)m->quadro, m->numero, m->testo);
        testo(X2 + 8, y, i ? GRIGIO : BIANCO, b); y += L;
    }
    if (!s_ren) return;
    SDL_UpdateTexture(s_tex, NULL, s_pix, DW * 4);
    SDL_RenderClear(s_ren);
    SDL_RenderCopy(s_ren, s_tex, NULL, NULL);
    SDL_RenderPresent(s_ren);
}

/* la finestra in un BMP (AMIGA_DEBUG_FOTO=<quadro> nelle prove, Shift+F11) */
static void foto_finestra(const char *file) {
    SDL_Surface *s = SDL_CreateRGBSurfaceFrom(s_pix, DW, DH, 32, DW * 4, 0xFF0000, 0xFF00, 0xFF, 0);
    if (s) { SDL_SaveBMP(s, file); SDL_FreeSurface(s); }
}
static long s_foto = -1;

/* ------------------------------------------------------------ interfaccia */

void amiga_debug_init(int attivo, int apri_finestra) {
    s_attivo = attivo;
    if (!attivo) return;
    carica_nomi(g_debug_gioco.annotazioni);
    for (int i = 0; i < g_debug_gioco.n_var; i++) {
        uint32_t a = g_debug_gioco.var[i].indirizzo, n = 1;
        switch (g_debug_gioco.var[i].formato) {
        case DV_U16: case DV_HEX16: n = 2; break;
        case DV_U32: n = 4; break;
        case DV_CIFRE8: n = 8; break;
        default: break;
        }
        for (uint32_t k = 0; k < n && a + k < 0x100000u; k++)
            g_debug_osservati[(a + k) >> 3] |= (uint8_t)(1u << ((a + k) & 7));
    }
    if (getenv("AMIGA_DEBUG_FOTO")) {               /* prove: la finestra fotografata a quel quadro */
        s_foto = atol(getenv("AMIGA_DEBUG_FOTO"));
        s_ev = calloc(N_EVENTI, sizeof *s_ev);
        atexit(amiga_debug_salva);                    /* copertura_giocatore.* nella cartella della prova */
    }
    if (apri_finestra) {
        /* partite giocate con --debug: la copertura si somma a quella delle
         * sessioni prima (copertura_giocatore.*) e si salva ogni 30 s e all'uscita */
        char *qui = SDL_GetBasePath();
        snprintf(s_base, sizeof s_base, "%s", qui ? qui : "");
        SDL_free(qui);
        s_ev = calloc(N_EVENTI, sizeof *s_ev);
        carica_eventi();
        atexit(amiga_debug_salva);
        s_esame_attivo = 1;
        atexit(esame_fine);
        apri();
    }
    /* i messaggi del gioco: con --debug tra le cose da esaminare, nelle prove
     * nella loro cartella */
    char p[1100];
    if (s_esame_attivo) { percorso(p, sizeof p, "esame"); _mkdir(p); percorso(p, sizeof p, "esame/messaggi.txt"); }
    else snprintf(p, sizeof p, "messaggi.log");
    s_log_msg = fopen(p, "a");
    if (s_log_msg) fprintf(s_log_msg, "---- avvio %ld\n", (long)time(NULL));
}

void amiga_debug_quadro(uint64_t quadro) {
    s_quadro = quadro;
    /* 10 aggiornamenti al secondo veri, anche con l'avanti veloce (prima ogni
     * 5 quadri: col veloce la finestra di debug frenava il gioco) */
    static uint32_t ultimo;
    uint32_t ora = SDL_GetTicks();
    if (s_aperta && ora - ultimo >= 100) { ultimo = ora; disegna(); }
    else if (s_ev && quadro % 50 == 0) misura();     /* finestra chiusa: il salvataggio ogni 30 s */
    if (s_foto >= 0 && (long)quadro == s_foto) { disegna(); foto_finestra("debug_finestra.bmp"); }
}

int amiga_debug_evento(const SDL_Event *ev) {
    if (!s_attivo) return 0;
    if (ev->type == SDL_KEYDOWN && ev->key.keysym.sym == SDLK_F11 && !ev->key.repeat) {
        if (ev->key.keysym.mod & KMOD_SHIFT) {
            amiga_debug_dump("debug_dump.txt");
        } else if (s_aperta) {
            SDL_HideWindow(s_win); s_aperta = 0;
        } else apri();
        return 1;
    }
    if (ev->type == SDL_WINDOWEVENT && s_win && ev->window.windowID == SDL_GetWindowID(s_win)) {
        if (ev->window.event == SDL_WINDOWEVENT_CLOSE) { SDL_HideWindow(s_win); s_aperta = 0; }
        return 1;
    }
    return 0;
}

void amiga_debug_dump(const char *file) {
    FILE *f = fopen(file, "w");
    if (!f) return;
    char d[96], v[32];
    fprintf(f, "# debug dump al quadro %llu\n\n[variabili]\n", (unsigned long long)s_quadro);
    for (int i = 0; i < g_debug_gioco.n_var; i++) {
        valore(&g_debug_gioco.var[i], v, sizeof v);
        fprintf(f, "%-26s $%05X = %s\n", g_debug_gioco.var[i].nome, g_debug_gioco.var[i].indirizzo, v);
    }
    fprintf(f, "\n[cpu]\nD0-D7");
    for (int i = 0; i < 8; i++) fprintf(f, " %08X", g_cpu.D[i]);
    fprintf(f, "\nA0-A7");
    for (int i = 0; i < 8; i++) fprintf(f, " %08X", g_cpu.A[i]);
    fprintf(f, "\nSR %04X\n\n[messaggi] (dal piu' vecchio)\n", g_cpu.SR);
    uint64_t da = s_n_msg > N_MESSAGGI ? s_n_msg - N_MESSAGGI : 0;
    for (uint64_t i = da; i < s_n_msg; i++) {
        messaggio_t *m = &s_msg[i % N_MESSAGGI];
        fprintf(f, "q%llu #%d: %s\n", (unsigned long long)m->quadro, m->numero, m->testo);
    }
    fprintf(f, "\n[scritture] (dalla piu' vecchia)\n");
    da = s_n_scr > N_SCRITTURE ? s_n_scr - N_SCRITTURE : 0;
    for (uint64_t i = da; i < s_n_scr; i++) {
        scrittura_t *s = &s_scr[i % N_SCRITTURE];
        fprintf(f, "q%llu $%05X %u -> %u  dopo %s\n", (unsigned long long)s->quadro, s->indirizzo,
                s->vecchio, s->nuovo, dove(s->dove, d, sizeof d));
    }
    fprintf(f, "\n[chiamate] (dalla piu' vecchia)\n");
    da = s_n_ch > N_CHIAMATE ? s_n_ch - N_CHIAMATE : 0;
    for (uint64_t i = da; i < s_n_ch; i++) {
        chiamata_t *c = &s_ch[i % N_CHIAMATE];
        fprintf(f, "q%llu %s A7=$%06X\n", (unsigned long long)c->quadro, dove(c->ritorno, d, sizeof d), c->a7);
    }
    fclose(f);
    if (s_aperta) foto_finestra("debug_finestra.bmp");
    FILE *m = fopen("debug_memoria.bin", "wb");          /* tutta la memoria, per analisi */
    if (m) { fwrite(g_amiga_mem, 1, g_amiga_mem_size, m); fclose(m); }
}
