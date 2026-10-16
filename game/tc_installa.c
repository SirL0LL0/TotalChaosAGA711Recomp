/* tc_installa.c - installa il gioco dal pacchetto di Aminet.
 *
 *   game/strat/TotalChaosAGAr6.lha (95.779.935 byte, CRC32 793901A5)
 *
 * 1. si estrae l'archivio (amiga_lha.c), la cartella TotalChaosAGAr7/ diventa
 *    <dati>/TotalChaos/;
 * 2. i file compressi con Shorten (voci, effetti, moduli: firma "ajkg") si
 *    decomprimono, come faceva il gioco al primo avvio con
 *    c/UnshortenEverything020;
 * 3. si applicano le nostre modifiche (tc_modifiche.bin, fatto da
 *    tools/crea_modifiche.py): il programma con le patch (Load, turni) e la
 *    traduzione italiana. Per i file del gioco il pacchetto contiene solo le
 *    differenze (copia da qui, aggiungi questi byte), mai i file originali.
 *
 * Formato di tc_modifiche.bin: "TCMOD1\n", poi voci fino a 'F':
 *   'N' percorso, u32 n, n byte                 file nuovo (nostro)
 *   'D' percorso, u32 crc dell'originale, u32 n, u32 crc del risultato,
 *       operazioni: 1 u32 da u32 quanti (copia dall'originale),
 *                   2 u32 quanti, byte (aggiunge), 0 fine
 *   'X' percorso                                file da togliere
 * percorso = u16 lunghezza + byte (Latin-1, separatori '/'); numeri big endian.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <direct.h>
#include <errno.h>

#include "amiga_lha.h"
#include "amiga_shorten.h"
#include "tc_installa.h"

uint32_t tc_crc32(const uint8_t *d, size_t n);            /* tc_immagine.c */

#define PREFISSO "TotalChaosAGAr7/"

typedef struct {
    const char *dati;
    tc_progresso_fn progresso;
    void *ctx;
    int fatti, totale, errori, decompressi;
    char err[600];
} stato_t;

static void crea_cartelle(const char *percorso) {
    char p[1200];
    snprintf(p, sizeof p, "%s", percorso);
    for (char *c = p + 1; *c; c++) {
        if (*c != '/' && *c != '\\') continue;
        char s = *c;
        *c = 0;
        _mkdir(p);
        *c = s;
    }
}

static int scrivi_file(const char *percorso, const uint8_t *d, size_t n) {
    crea_cartelle(percorso);
    FILE *f = fopen(percorso, "wb");
    if (!f) return -1;
    size_t s = fwrite(d, 1, n, f);
    return fclose(f) == 0 && s == n ? 0 : -1;
}

static uint8_t *leggi_file(const char *p, size_t *n) {
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long l = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc(l > 0 ? (size_t)l : 1);
    if (d && l > 0 && fread(d, 1, (size_t)l, f) != (size_t)l) { free(d); d = NULL; }
    fclose(f);
    *n = d ? (size_t)l : 0;
    return d;
}

/* un file dell'archivio: dentro TotalChaosAGAr7/ -> <dati>/TotalChaos/ */
static int dall_archivio(void *ctx, const char *perc, const uint8_t *dati, size_t n) {
    stato_t *s = ctx;
    s->fatti++;
    if (s->progresso && s->fatti % 64 == 0) s->progresso(s->ctx, s->fatti, s->totale, "estrazione");
    if (strncmp(perc, PREFISSO, strlen(PREFISSO))) return 0;          /* l'icona della cartella */
    char dest[1200];
    const char *rel = perc + strlen(PREFISSO);
    snprintf(dest, sizeof dest, "%s/TotalChaos/%s", s->dati, rel);
    /* le preferenze originali, per "Ripristina le impostazioni del gioco" */
    if (!strcmp(rel, "Chaos.prefs")) {
        char orig[1300];
        snprintf(orig, sizeof orig, "%s.originale", dest);
        scrivi_file(orig, dati, n);
    }
    /* reinstallando (modifiche nuove) restano le cose del giocatore */
    if (!strcmp(rel, "Chaos.prefs") || !strcmp(rel, "HighScores") || !strncmp(rel, "SavedGames/", 11) ||
        !strncmp(rel, "RecordedGames/", 14)) {
        FILE *c = fopen(dest, "rb");
        if (c) { fclose(c); return 0; }
    }
    uint8_t *u = NULL;
    size_t nu = 0;
    if (shorten_e(dati, n)) {
        if (!shorten_decomprimi(dati, n, &u, &nu)) {
            snprintf(s->err, sizeof s->err, "Non riesco a decomprimere %s.", perc);
            s->errori++;
            return -1;
        }
        s->decompressi++;
    }
    int r = scrivi_file(dest, u ? u : dati, u ? nu : n);
    free(u);
    if (r) { snprintf(s->err, sizeof s->err, "Non riesco a scrivere %s.", dest); s->errori++; }
    return r;
}

/* ---- le nostre modifiche */
typedef struct { const uint8_t *d; size_t n, o; int errore; } lettore_t;
static uint32_t u32(lettore_t *l) {
    if (l->o + 4 > l->n) { l->errore = 1; return 0; }
    l->o += 4;
    return (uint32_t)l->d[l->o - 4] << 24 | l->d[l->o - 3] << 16 | l->d[l->o - 2] << 8 | l->d[l->o - 1];
}
static int percorso(lettore_t *l, const char *dati, char *out, size_t no) {
    if (l->o + 2 > l->n) { l->errore = 1; return 0; }
    size_t k = (size_t)(l->d[l->o] << 8 | l->d[l->o + 1]);
    l->o += 2;
    if (l->o + k > l->n || k > 500) { l->errore = 1; return 0; }
    snprintf(out, no, "%s/TotalChaos/%.*s", dati, (int)k, (const char *)l->d + l->o);
    l->o += k;
    return 1;
}

static int applica_modifiche(const char *file, stato_t *s) {
    size_t n;
    uint8_t *m = leggi_file(file, &n);
    if (!m) { snprintf(s->err, sizeof s->err, "Manca %s (le modifiche del port).", file); return 0; }
    lettore_t l = { m, n, 7, 0 };
    if (n < 7 || memcmp(m, "TCMOD1\n", 7)) { free(m); snprintf(s->err, sizeof s->err, "%s non e' valido.", file); return 0; }
    int ok = 1;
    while (ok && l.o < n && !l.errore) {
        char tipo = (char)m[l.o++];
        if (tipo == 'F') break;
        char p[1200];
        if (!percorso(&l, s->dati, p, sizeof p)) break;
        if (tipo == 'N') {
            uint32_t k = u32(&l);
            if (l.o + k > n) { l.errore = 1; break; }
            if (scrivi_file(p, m + l.o, k)) { snprintf(s->err, sizeof s->err, "Non riesco a scrivere %s.", p); ok = 0; }
            l.o += k;
        } else if (tipo == 'X') {
            remove(p);
        } else if (tipo == 'D') {
            uint32_t crc_orig = u32(&l), lung = u32(&l), crc_fine = u32(&l);
            size_t no;
            uint8_t *orig = leggi_file(p, &no);
            if (!orig || tc_crc32(orig, no) != crc_orig) {
                snprintf(s->err, sizeof s->err, "%s non e' quello del pacchetto di Aminet: non posso modificarlo.", p);
                free(orig);
                ok = 0;
                break;
            }
            uint8_t *out = malloc(lung ? lung : 1);
            size_t k = 0;
            for (;;) {
                if (l.o >= n) { l.errore = 1; break; }
                uint8_t op = m[l.o++];
                if (op == 0) break;
                if (op == 1) {
                    uint32_t da = u32(&l), q = u32(&l);
                    if ((size_t)da + q > no || k + q > lung) { l.errore = 1; break; }
                    memcpy(out + k, orig + da, q);
                    k += q;
                } else if (op == 2) {
                    uint32_t q = u32(&l);
                    if (l.o + q > n || k + q > lung) { l.errore = 1; break; }
                    memcpy(out + k, m + l.o, q);
                    l.o += q;
                    k += q;
                } else { l.errore = 1; break; }
            }
            if (!l.errore && (k != lung || tc_crc32(out, lung) != crc_fine)) {
                snprintf(s->err, sizeof s->err, "Modifica di %s non riuscita.", p);
                ok = 0;
            } else if (!l.errore && scrivi_file(p, out, lung)) {
                snprintf(s->err, sizeof s->err, "Non riesco a scrivere %s.", p);
                ok = 0;
            }
            free(orig);
            free(out);
        } else l.errore = 1;
    }
    if (l.errore && ok) { snprintf(s->err, sizeof s->err, "%s e' rovinato.", file); ok = 0; }
    free(m);
    return ok;
}

int tc_installa(const char *lha, const char *dati, const char *modifiche, tc_progresso_fn progresso, void *ctx,
                char *err, size_t ne) {
    stato_t s = { dati, progresso, ctx, 0, 7624, 0, 0, "" };
    /* il pacchetto giusto: dimensione e CRC */
    size_t n;
    uint8_t *a = leggi_file(lha, &n);
    if (!a) { snprintf(err, ne, "Non riesco a leggere %s.", lha); return 0; }
    uint32_t crc = tc_crc32(a, n);
    free(a);
    if (n != TC_LHA_DIMENSIONE || crc != TC_LHA_CRC) {
        snprintf(err, ne, "%s non e' TotalChaosAGAr6.lha di Aminet (game/strat): %zu byte, CRC %08X; "
                 "attesi %u byte, CRC %08X.", lha, n, crc, TC_LHA_DIMENSIONE, TC_LHA_CRC);
        return 0;
    }
    if (progresso) progresso(ctx, 0, s.totale, "estrazione");
    int k = lha_estrai_tutto(lha, &s, dall_archivio);
    if (k < 0 || s.errori) { snprintf(err, ne, "%s", s.err[0] ? s.err : "Estrazione non riuscita."); return 0; }
    if (progresso) progresso(ctx, s.totale, s.totale, "modifiche");
    if (modifiche && !applica_modifiche(modifiche, &s)) { snprintf(err, ne, "%s", s.err); return 0; }
    snprintf(err, ne, "Installati %d file (%d decompressi) in %s.", k, s.decompressi, dati);
    return 1;
}
