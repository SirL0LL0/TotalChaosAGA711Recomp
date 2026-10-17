/* tc_pp20.c - i file compressi con PowerPacker ("PP20") del pacchetto: Total
 * Chaos non li sa caricare (Sounds/MovementMods/Pandora's Box restava muto
 * anche sull'Amiga), qui si decomprimono una volta sul disco, all'avvio e
 * dopo l'installazione.
 *
 * Il formato: "PP20", 4 byte con le lunghezze degli scostamenti, i dati, e in
 * fondo 3 byte con la lunghezza decompressa e 1 con i bit da saltare. I bit
 * si leggono all'indietro dalla fine, e anche l'uscita si scrive dalla fine
 * all'inizio: letterali (1 bit 0, poi quanti a gruppi di 2 bit, poi 8 bit
 * ciascuno) e copie (2 bit per la lunghezza dello scostamento, poi lo
 * scostamento e quanti byte copiare da li'). */
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { const uint8_t *inizio, *p; uint32_t buf; int bit; } bit_t;

static int leggi(bit_t *b, int n, uint32_t *v) {
    while (b->bit < n) {
        if (b->p <= b->inizio) return 0;
        b->buf |= (uint32_t)*--b->p << b->bit;
        b->bit += 8;
    }
    uint32_t x = 0;
    for (int i = 0; i < n; i++) { x = x << 1 | (b->buf & 1); b->buf >>= 1; }
    b->bit -= n;
    *v = x;
    return 1;
}

/* dati (n byte, file intero) -> uscita (malloc), 0 se non e' PP20 o e' rotto */
static uint8_t *decomprimi(const uint8_t *d, size_t n, uint32_t *lun) {
    if (n < 16 || memcmp(d, "PP20", 4)) return NULL;
    const uint8_t *lens = d + 4;
    uint32_t tot = (uint32_t)d[n - 4] << 16 | (uint32_t)d[n - 3] << 8 | d[n - 2];
    if (!tot || tot > (64u << 20)) return NULL;
    uint8_t *out = malloc(tot), *fine = out + tot, *o = fine;
    if (!out) return NULL;
    bit_t b = { d + 8, d + n - 4, 0, 0 };
    uint32_t x, scritti = 0;
    if (!leggi(&b, d[n - 1], &x)) goto rotto;
    while (scritti < tot) {
        if (!leggi(&b, 1, &x)) goto rotto;
        if (x == 0) {                                         /* letterali */
            uint32_t quanti = 1;
            do { if (!leggi(&b, 2, &x)) goto rotto; quanti += x; } while (x == 3);
            while (quanti--) {
                if (!leggi(&b, 8, &x) || o <= out) goto rotto;
                *--o = (uint8_t)x; scritti++;
            }
            if (scritti == tot) break;
        }
        uint32_t sel, bits, scost;
        if (!leggi(&b, 2, &sel)) goto rotto;                  /* una copia */
        bits = lens[sel];
        uint32_t quanti = sel + 2;
        if (sel == 3) {
            if (!leggi(&b, 1, &x)) goto rotto;
            if (x == 0) bits = 7;
            if (!leggi(&b, (int)bits, &scost)) goto rotto;
            do { if (!leggi(&b, 3, &x)) goto rotto; quanti += x; } while (x == 7);
        } else if (!leggi(&b, (int)bits, &scost)) goto rotto;
        if (o + scost >= fine) goto rotto;
        while (quanti--) {
            if (o <= out) goto rotto;
            uint8_t c = o[scost];
            *--o = c; scritti++;
        }
    }
    *lun = tot;
    return out;
rotto:
    free(out);
    return NULL;
}

/* il file al suo posto, decompresso; 1 se l'ha fatto */
int tc_pp20_file(const char *percorso) {
    FILE *f = fopen(percorso, "rb");
    if (!f) return 0;
    uint8_t testa[4];
    if (fread(testa, 1, 4, f) != 4 || memcmp(testa, "PP20", 4)) { fclose(f); return 0; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = n > 0 ? malloc((size_t)n) : NULL;
    size_t letti = d ? fread(d, 1, (size_t)n, f) : 0;
    fclose(f);
    uint32_t lun = 0;
    uint8_t *u = d && letti == (size_t)n ? decomprimi(d, (size_t)n, &lun) : NULL;
    free(d);
    if (!u) { fprintf(stderr, "[pp20] %s: non riesco a decomprimerlo\n", percorso); return 0; }
    f = fopen(percorso, "wb");
    int ok = f && fwrite(u, 1, lun, f) == lun;
    if (f) fclose(f);
    free(u);
    fprintf(stderr, "[pp20] %s: decompresso (%u byte)%s\n", percorso, lun, ok ? "" : " ma non scritto");
    return ok;
}

/* tutti i PP20 di una cartella (non le sottocartelle) */
void tc_pp20_cartella(const char *cartella) {
    DIR *dir = opendir(cartella);
    if (!dir) return;
    struct dirent *e;
    while ((e = readdir(dir)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char p[1200];
        snprintf(p, sizeof p, "%s/%s", cartella, e->d_name);
        tc_pp20_file(p);
    }
    closedir(dir);
}
