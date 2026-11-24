/* amiga_percorsi.c - percorsi lunghi su Windows (amiga_percorsi.h): oltre i
 * 240 caratteri il percorso diventa assoluto, con le barre rovesciate e senza
 * "." e "..", e prende il prefisso \\?\ (con quello le funzioni di Windows,
 * anche ANSI, accettano fino a 32767 caratteri). Qui le funzioni vere: le
 * macro dell'header non valgono in questo file.
 *
 * Su macOS e Linux le stesse funzioni convertono i nomi: il gioco li scrive
 * in Latin-1 (l'alfabeto dell'Amiga, che Windows accetta cosi' com'e'), macOS
 * vuole UTF-8 (un terreno con la u con dieresi non si installava). Dentro il
 * runner i nomi restano Latin-1: si converte solo verso il sistema e da readdir. */
#ifdef _WIN32
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <direct.h>

#define CORTO 240
#define LUNGO 4096

/* il percorso da usare: p se e' corto, altrimenti \\?\ + assoluto normalizzato in b */
static const char *lungo(const char *p, char *b) {
    if (!p || strlen(p) < CORTO) return p;
    if (!strncmp(p, "\\\\?\\", 4)) return p;
    char t[LUNGO];
    int assoluto = (p[0] && p[1] == ':') || p[0] == '\\' || p[0] == '/';
    if (assoluto) snprintf(t, sizeof t, "%s", p);
    else {
        char cwd[LUNGO];
        if (!_getcwd(cwd, sizeof cwd)) return p;
        snprintf(t, sizeof t, "%s\\%s", cwd, p);
    }
    for (char *c = t; *c; c++) if (*c == '/') *c = '\\';
    /* i pezzi, senza "." e con ".." che toglie il precedente */
    char *pezzi[512];
    int n = 0;
    char unita[3] = "";
    char *s = t;
    if (t[0] && t[1] == ':') { unita[0] = t[0]; unita[1] = ':'; s = t + 2; }
    else if (t[0] == '\\' && t[1] == '\\') return p;          /* UNC: lasciato com'e' */
    for (char *tok = strtok(s, "\\"); tok && n < 512; tok = strtok(NULL, "\\")) {
        if (!strcmp(tok, ".") || !*tok) continue;
        if (!strcmp(tok, "..")) { if (n) n--; continue; }
        pezzi[n++] = tok;
    }
    if (!unita[0]) { unita[0] = (char)('A' + _getdrive() - 1); unita[1] = ':'; }
    size_t l = (size_t)snprintf(b, LUNGO, "\\\\?\\%s", unita);
    for (int i = 0; i < n && l < LUNGO; i++) l += (size_t)snprintf(b + l, LUNGO - l, "\\%s", pezzi[i]);
    return b;
}

FILE *lp_fopen(const char *p, const char *modo) { char b[LUNGO]; return fopen(lungo(p, b), modo); }
int lp_remove(const char *p) { char b[LUNGO]; return remove(lungo(p, b)); }
int lp_rename(const char *a, const char *z) { char b1[LUNGO], b2[LUNGO]; return rename(lungo(a, b1), lungo(z, b2)); }
int lp_stat(const char *p, struct stat *st) { char b[LUNGO]; return stat(lungo(p, b), st); }
DIR *lp_opendir(const char *p) { char b[LUNGO]; return opendir(lungo(p, b)); }
int lp_mkdir(const char *p) { char b[LUNGO]; return _mkdir(lungo(p, b)); }
#else
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define LUNGO 4096

/* lunghezza della sequenza UTF-8 valida che comincia in s, 0 se non lo e' */
static int utf8(const unsigned char *s) {
    int n = s[0] >= 0xF0 && s[0] < 0xF5 ? 4 : s[0] >= 0xE0 ? 3 : s[0] >= 0xC2 && s[0] < 0xE0 ? 2 : 0;
    for (int i = 1; i < n; i++) if ((s[i] & 0xC0) != 0x80) return 0;
    return n;
}

/* Latin-1 -> UTF-8; le sequenze gia' UTF-8 (la cartella dell'utente) restano */
static const char *sistema(const char *p, char *b) {
    if (!p) return p;
    const unsigned char *s = (const unsigned char *)p;
    size_t o = 0;
    int alto = 0;
    for (; *s && o + 4 < LUNGO; s++) {
        if (*s < 0x80) { b[o++] = (char)*s; continue; }
        alto = 1;
        int n = utf8(s);
        if (n) { memcpy(b + o, s, (size_t)n); o += (size_t)n; s += n - 1; continue; }
        b[o++] = (char)(0xC0 | *s >> 6);
        b[o++] = (char)(0x80 | (*s & 0x3F));
    }
    b[o] = 0;
    return alto ? b : p;
}

FILE *lp_fopen(const char *p, const char *modo) { char b[LUNGO]; return fopen(sistema(p, b), modo); }
int lp_remove(const char *p) { char b[LUNGO]; return remove(sistema(p, b)); }
int lp_rename(const char *a, const char *z) { char b1[LUNGO], b2[LUNGO]; return rename(sistema(a, b1), sistema(z, b2)); }
int lp_stat(const char *p, struct stat *st) { char b[LUNGO]; return stat(sistema(p, b), st); }
DIR *lp_opendir(const char *p) { char b[LUNGO]; return opendir(sistema(p, b)); }
int lp_mkdir(const char *p) { char b[LUNGO]; return mkdir(sistema(p, b), 0777); }
int lp_rmdir(const char *p) { char b[LUNGO]; return rmdir(sistema(p, b)); }

/* i nomi letti dalle cartelle tornano Latin-1 (quelli che ci stanno) */
struct dirent *lp_readdir(DIR *d) {
    static struct dirent copia;
    struct dirent *e = readdir(d);
    if (!e) return NULL;
    copia = *e;
    const unsigned char *s = (const unsigned char *)e->d_name;
    char *o = copia.d_name;
    for (; *s; s++) {
        int n = utf8(s);
        if (n == 2 && s[0] < 0xC4) { *o++ = (char)((s[0] & 0x1F) << 6 | (s[1] & 0x3F)); s++; }
        else if (n) { memcpy(o, s, (size_t)n); o += n; s += n - 1; }
        else *o++ = (char)*s;
    }
    *o = 0;
    return &copia;
}
#endif
