/* tc_tutorial.c - nei tutorial il mago del giocatore si chiama come il
 * giocatore 1 (Player1Name di Chaos.prefs), non "Yours" o "Player": quei nomi
 * sono scritti nei salvataggi dei tutorial (Tutorials/<n>.lha, che il gioco
 * estrae con c/lha in T:ChaosSavedGame). Appena estratto, nel campo dei nomi
 * (etichetta "N ", 4 zeri, poi un nome per riga) si sostituisce il primo.
 * Il salvataggio e' fatto di campi uno dopo l'altro senza lunghezze, quindi
 * un nome piu' lungo sposta solo quello che segue. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char s_prefs[1100];

void tc_tutorial_imposta(const char *dati) {
    snprintf(s_prefs, sizeof s_prefs, "%s/TotalChaos/Chaos.prefs", dati);
}

static int nome_giocatore1(char *nome, size_t n) {
    FILE *f = fopen(s_prefs, "rb");
    if (!f) return 0;
    char r[256];
    int ok = 0;
    while (fgets(r, sizeof r, f)) {
        if (strncmp(r, "Player1Name:", 12)) continue;
        char *p = r + 12;
        while (*p == ' ' || *p == '\t') p++;
        p[strcspn(p, "\r\n")] = 0;
        if (*p) { snprintf(nome, n, "%s", p); ok = 1; }
        break;
    }
    fclose(f);
    return ok;
}

void tc_tutorial_estratto(const char *archivio, const char *cartella) {
    if (!strstr(archivio, "utorial")) return;
    char nome[64];
    if (!nome_giocatore1(nome, sizeof nome)) return;
    char p[1200];
    snprintf(p, sizeof p, "%s/ChaosSavedGame", cartella);
    FILE *f = fopen(p, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *d = n > 0 ? malloc((size_t)n + 64) : NULL;
    size_t letti = d ? fread(d, 1, (size_t)n, f) : 0;
    fclose(f);
    if (!d || letti != (size_t)n) { free(d); return; }
    static const unsigned char etichetta[6] = { 'N', ' ', 0, 0, 0, 0 };
    for (long i = 0; i + 6 < n; i++) {
        if (memcmp(d + i, etichetta, 6)) continue;
        long a = i + 6, b = a;
        while (b < n && d[b] != '\n' && d[b] >= 0x20 && d[b] < 0x7F) b++;
        if (b >= n || d[b] != '\n') continue;            /* non e' una riga di testo */
        long l = b - a;
        if (!((l == 5 && !memcmp(d + a, "Yours", 5)) || (l == 6 && !memcmp(d + a, "Player", 6)))) break;
        size_t ln = strlen(nome);
        f = fopen(p, "wb");
        if (!f) break;
        fwrite(d, 1, (size_t)a, f);
        fwrite(nome, 1, ln, f);
        fwrite(d + b, 1, (size_t)(n - b), f);
        fclose(f);
        fprintf(stderr, "[tutorial] il mago del giocatore si chiama \"%s\"\n", nome);
        break;
    }
    free(d);
}
