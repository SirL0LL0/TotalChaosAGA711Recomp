/* prova_shorten.c - amiga_shorten.c contro i file decompressi dall'Amiga.
 *
 * Legge da stdin righe "compresso<TAB>atteso" e dice quanti sono uguali.
 *   clang -O2 -I amigarecomp/runner tools/prova_shorten.c amigarecomp/runner/amiga_shorten.c -o build/prova_shorten.exe
 *   python tools/coppie_shorten.py <cartella lha> <cartella hdf> | build/prova_shorten.exe
 */
#include "amiga_shorten.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *leggi(const char *p, size_t *n) {
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long l = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc((size_t)l + 1);
    *n = fread(d, 1, (size_t)l, f);
    fclose(f);
    return d;
}

int main(void) {
    char riga[2400];
    int uguali = 0, diversi = 0, errori = 0;
    while (fgets(riga, sizeof riga, stdin)) {
        riga[strcspn(riga, "\r\n")] = 0;
        char *tab = strchr(riga, '\t');
        if (!tab) continue;
        *tab = 0;
        size_t nc, na, nu;
        uint8_t *c = leggi(riga, &nc), *a = leggi(tab + 1, &na), *u;
        if (!c || !a) { errori++; free(c); free(a); continue; }
        if (!shorten_decomprimi(c, nc, &u, &nu)) {
            if (errori++ < 10) printf("ERRORE  %s\n", riga);
        } else if (nu == na && !memcmp(u, a, na)) uguali++;
        else {
            size_t k = 0;
            while (k < nu && k < na && u[k] == a[k]) k++;
            if (diversi++ < 10) printf("DIVERSO %s: %zu byte contro %zu, primo diverso a %zu\n", riga, nu, na, k);
            if (diversi == 1 && getenv("SALVA")) {          /* il primo, per guardarlo */
                FILE *s = fopen(getenv("SALVA"), "wb");
                if (s) { fwrite(u, 1, nu, s); fclose(s); }
            }
        }
        free(c); free(a);
        if (u) free(u);
    }
    printf("uguali %d, diversi %d, errori %d\n", uguali, diversi, errori);
    return diversi || errori;
}
