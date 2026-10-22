/* amiga_percorsi.c - percorsi lunghi su Windows (amiga_percorsi.h): oltre i
 * 240 caratteri il percorso diventa assoluto, con le barre rovesciate e senza
 * "." e "..", e prende il prefisso \\?\ (con quello le funzioni di Windows,
 * anche ANSI, accettano fino a 32767 caratteri). Qui le funzioni vere: le
 * macro dell'header non valgono in questo file. */
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
#endif
