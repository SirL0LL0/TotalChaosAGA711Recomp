/* amiga_lha.c - archivi LHA per il comando "lha" della shell (amiga_dos.c).
 *
 * Total Chaos salva le partite con "c/lha -I -aq -v9 a <file>.lha
 * T:ChaosSavedGame" e le riapre con "c/lha -I -qa x <file> t:". Qui:
 *   lettura   intestazioni di livello 0, 1 e 2; metodi -lh0- (copia) e
 *             -lh5-/-lh6-/-lh7- (LZ77 + Huffman statico a blocchi, come il
 *             decodificatore ar002 di Haruhiko Okumura), per i salvataggi
 *             fatti sull'Amiga
 *   scrittura -lh0- con intestazione di livello 0: il lha dell'Amiga la legge
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "amiga_lha.h"

/* ---------------------------------------------------------- CRC-16 (ARC) */

static uint16_t crc16(const uint8_t *p, size_t n) {
    uint16_t c = 0;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) c = (c & 1) ? (uint16_t)((c >> 1) ^ 0xA001) : (uint16_t)(c >> 1);
    }
    return c;
}

/* ---------------------------------------------------------- decodifica lh5/6/7 */

#define NC 510                       /* 256 caratteri + 256 lunghezze - 3 + 1 */
#define NT 19
#define NPMAX 17
#define CBIT 9
#define TBIT 5

typedef struct {
    const uint8_t *in;
    size_t n_in, pos;
    uint16_t bitbuf;
    uint8_t sub;
    int conta;
    int np, pbit;
    uint16_t left[2 * NC - 1], right[2 * NC - 1];
    uint8_t c_len[NC], pt_len[NPMAX > NT ? NPMAX : NT];
    uint16_t c_table[4096], pt_table[256];
    unsigned blocco;
    int errore;
} dec_t;

static void riempi(dec_t *d, int n) {
    while (n > d->conta) {
        n -= d->conta;
        d->bitbuf = (uint16_t)((d->bitbuf << d->conta) + (d->sub >> (8 - d->conta)));
        d->sub = d->pos < d->n_in ? d->in[d->pos++] : 0;
        d->conta = 8;
    }
    d->conta -= n;
    d->bitbuf = (uint16_t)((d->bitbuf << n) + (d->sub >> (8 - n)));
    d->sub = (uint8_t)(d->sub << n);
}
static unsigned bit(dec_t *d, int n) {
    unsigned x = d->bitbuf >> (16 - n);
    riempi(d, n);
    return x;
}

static void fa_tabella(dec_t *d, int nchar, const uint8_t *lun, int tbit, uint16_t *tab) {
    unsigned count[17] = {0}, peso[17], inizio[18];
    for (int i = 0; i < nchar; i++) if (lun[i] <= 16) count[lun[i]]++;
    inizio[1] = 0;
    for (int i = 1; i <= 16; i++) inizio[i + 1] = inizio[i] + (count[i] << (16 - i));
    if ((inizio[17] & 0xFFFF) != 0) { d->errore = 1; return; }
    int salto = 16 - tbit;
    for (int i = 1; i <= tbit; i++) { inizio[i] >>= salto; peso[i] = 1u << (tbit - i); }
    for (int i = tbit + 1; i <= 16; i++) peso[i] = 1u << (16 - i);
    for (unsigned i = inizio[tbit + 1] >> salto; i < (1u << tbit); i++) tab[i] = 0;   /* codici che mancano */
    int libero = nchar;
    unsigned maschera = 1u << (15 - tbit);
    for (int ch = 0; ch < nchar; ch++) {
        int l = lun[ch];
        if (l == 0) continue;
        unsigned prossimo = inizio[l] + peso[l];
        if (l <= tbit) {
            for (unsigned j = inizio[l]; j < prossimo && j < (1u << tbit); j++) tab[j] = (uint16_t)ch;
        } else {
            unsigned k = inizio[l];
            uint16_t *p = &tab[k >> salto];
            int r = l - tbit;
            while (r-- != 0) {
                if (*p == 0) {
                    if (libero >= 2 * NC - 1) { d->errore = 1; return; }
                    d->right[libero] = d->left[libero] = 0;
                    *p = (uint16_t)libero++;
                }
                p = (k & maschera) ? &d->right[*p] : &d->left[*p];
                k <<= 1;
            }
            *p = (uint16_t)ch;
        }
        inizio[l] = prossimo;
    }
}

static void leggi_pt(dec_t *d, int nn, int nbit, int speciale) {
    int n = (int)bit(d, nbit);
    if (n == 0) {
        unsigned c = bit(d, nbit);
        memset(d->pt_len, 0, (size_t)nn);
        for (int i = 0; i < 256; i++) d->pt_table[i] = (uint16_t)c;
        return;
    }
    int i = 0;
    while (i < n && i < nn) {
        int c = d->bitbuf >> 13;
        if (c == 7) {
            unsigned m = 1u << 12;
            while (m & d->bitbuf) { m >>= 1; c++; }
        }
        riempi(d, c < 7 ? 3 : c - 3);
        d->pt_len[i++] = (uint8_t)c;
        if (i == speciale) {
            int z = (int)bit(d, 2);
            while (--z >= 0 && i < nn) d->pt_len[i++] = 0;
        }
    }
    while (i < nn) d->pt_len[i++] = 0;
    fa_tabella(d, nn, d->pt_len, 8, d->pt_table);
}

static void leggi_c(dec_t *d) {
    int n = (int)bit(d, CBIT);
    if (n == 0) {
        unsigned c = bit(d, CBIT);
        memset(d->c_len, 0, NC);
        for (int i = 0; i < 4096; i++) d->c_table[i] = (uint16_t)c;
        return;
    }
    int i = 0;
    while (i < n && i < NC) {
        int c = d->pt_table[d->bitbuf >> 8];
        if (c >= NT) {
            unsigned m = 1u << 7;
            do { c = (d->bitbuf & m) ? d->right[c] : d->left[c]; m >>= 1; } while (c >= NT && m);
        }
        riempi(d, d->pt_len[c]);
        if (c <= 2) {
            if (c == 0) c = 1;
            else if (c == 1) c = (int)bit(d, 4) + 3;
            else c = (int)bit(d, CBIT) + 20;
            while (--c >= 0 && i < NC) d->c_len[i++] = 0;
        } else d->c_len[i++] = (uint8_t)(c - 2);
    }
    while (i < NC) d->c_len[i++] = 0;
    fa_tabella(d, NC, d->c_len, 12, d->c_table);
}

static unsigned decodifica_c(dec_t *d) {
    if (d->blocco == 0) {
        d->blocco = bit(d, 16);
        leggi_pt(d, NT, TBIT, 3);
        leggi_c(d);
        leggi_pt(d, d->np, d->pbit, -1);
    }
    d->blocco--;
    unsigned j = d->c_table[d->bitbuf >> 4];
    if (j < NC) riempi(d, d->c_len[j]);
    else {
        riempi(d, 12);
        unsigned m = 1u << 15;
        do { j = (d->bitbuf & m) ? d->right[j] : d->left[j]; m >>= 1; } while (j >= NC && m);
        riempi(d, d->c_len[j] - 12);
    }
    return j;
}

static unsigned decodifica_p(dec_t *d) {
    unsigned j = d->pt_table[d->bitbuf >> 8];
    if ((int)j < d->np) riempi(d, d->pt_len[j]);
    else {
        riempi(d, 8);                      /* consumati gli 8 bit della tabella: il prossimo e' in cima */
        unsigned m = 1u << 15;
        do { j = (d->bitbuf & m) ? d->right[j] : d->left[j]; m >>= 1; } while ((int)j >= d->np && m);
        riempi(d, d->pt_len[j] - 8);
    }
    if (j != 0) j = (1u << (j - 1)) + bit(d, (int)j - 1);
    return j;
}

/* dicbit 13 (lh5), 15 (lh6), 16 (lh7) */
static int lh_decodifica(const uint8_t *in, size_t n_in, uint8_t *out, size_t n_out, int dicbit) {
    dec_t *d = calloc(1, sizeof *d);
    if (!d) return 0;
    d->in = in; d->n_in = n_in;
    d->np = dicbit + 1;
    d->pbit = dicbit <= 13 ? 4 : 5;
    riempi(d, 16);
    size_t k = 0;
    while (k < n_out && !d->errore) {
        unsigned c = decodifica_c(d);
        if (c <= 255) { out[k++] = (uint8_t)c; continue; }
        size_t l = c - 253;
        size_t dist = decodifica_p(d) + 1;
        if (dist > k) { d->errore = 1; break; }
        for (size_t i = 0; i < l && k < n_out; i++, k++) out[k] = out[k - dist];
    }
    int ok = !d->errore && k == n_out;
    free(d);
    return ok;
}

/* ---------------------------------------------------------- archivi */

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static uint8_t *leggi_file(const char *nome, size_t *n) {
    FILE *f = fopen(nome, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long l = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = l >= 0 ? malloc((size_t)l + 1) : NULL;
    if (d && fread(d, 1, (size_t)l, f) != (size_t)l) { free(d); d = NULL; }
    fclose(f);
    if (d) *n = (size_t)l;
    return d;
}

/* il nome con la cartella: estensione 2 (cartella, separatori 0xFF) + estensione 1 (nome) */
static void ext_nome(const uint8_t *e, size_t l, char *nome, size_t nn, char *cart, size_t nc) {
    if (l < 1) return;
    char *dst = e[0] == 1 ? nome : e[0] == 2 ? cart : NULL;
    size_t cap = e[0] == 1 ? nn : nc;
    if (!dst) return;
    size_t k = l - 1 < cap - 1 ? l - 1 : cap - 1;
    memcpy(dst, e + 1, k);
    dst[k] = 0;
}

int lha_estrai_tutto(const char *archivio, void *ctx,
                     int (*scrivi)(void *ctx, const char *percorso, const uint8_t *dati, size_t n)) {
    size_t n;
    uint8_t *a = leggi_file(archivio, &n);
    if (!a) return -1;
    int file = 0;
    size_t p = 0;
    while (p + 22 <= n && a[p] != 0) {
        int livello = a[p + 20];
        char metodo[6];
        memcpy(metodo, a + p + 2, 5); metodo[5] = 0;
        uint32_t compr = le32(a + p + 7), orig = le32(a + p + 11);
        char nome[512] = "", cart[512] = "";
        size_t dati;
        uint16_t crc = 0;
        if (livello == 0 || livello == 1) {
            size_t h = (size_t)a[p] + 2;
            int ln = a[p + 21];
            if (p + 22 + (size_t)ln + 2 > n) break;
            memcpy(nome, a + p + 22, (size_t)ln < sizeof nome ? (size_t)ln : sizeof nome - 1);
            nome[(size_t)ln < sizeof nome ? (size_t)ln : sizeof nome - 1] = 0;
            crc = le16(a + p + 22 + ln);
            dati = p + h;
            if (livello == 1) {                         /* intestazioni estese: contano nei byte compressi */
                size_t e = le16(a + p + h - 2);
                while (e && dati + e <= n) {
                    ext_nome(a + dati, e - 2, nome, sizeof nome, cart, sizeof cart);
                    compr -= (uint32_t)e;
                    dati += e;
                    e = le16(a + dati - 2);
                }
            }
        } else if (livello == 2) {
            size_t h = le16(a + p);
            crc = le16(a + p + 21);
            size_t e = p + 24;
            while (e + 3 <= p + h) {
                size_t l = le16(a + e);
                if (!l) break;
                ext_nome(a + e + 2, l - 2, nome, sizeof nome, cart, sizeof cart);
                e += l;
            }
            dati = p + h;
        } else break;
        if (dati + compr > n) break;
        /* il percorso con '/': cartella + nome; 0xFF e '\\' sono separatori */
        char perc[1100];
        snprintf(perc, sizeof perc, "%s%s", cart, nome);
        for (char *c = perc; *c; c++) if ((uint8_t)*c == 0xFF || *c == '\\') *c = '/';
        size_t lp = strlen(perc);
        int cartella = !strcmp(metodo, "-lhd-") || (lp && perc[lp - 1] == '/');
        if (cartella) { p = dati + compr; continue; }
        uint8_t *out = malloc(orig ? orig : 1);
        int ok = 0;
        if (out) {
            if (!strcmp(metodo, "-lh0-") || !strcmp(metodo, "-lz4-")) { memcpy(out, a + dati, orig <= compr ? orig : compr); ok = orig <= compr; }
            else if (!strcmp(metodo, "-lh5-")) ok = lh_decodifica(a + dati, compr, out, orig, 13);
            else if (!strcmp(metodo, "-lh6-")) ok = lh_decodifica(a + dati, compr, out, orig, 15);
            else if (!strcmp(metodo, "-lh7-")) ok = lh_decodifica(a + dati, compr, out, orig, 16);
            if (ok && crc16(out, orig) != crc) {
                fprintf(stderr, "[lha] %s: CRC sbagliato in %s\n", archivio, perc);
                ok = 0;
            }
            if (ok && scrivi(ctx, perc, out, orig) == 0) file++;
            else fprintf(stderr, "[lha] %s: %s (%s) non estratto\n", archivio, perc, metodo);
            free(out);
        }
        p = dati + compr;
    }
    free(a);
    return file;
}

/* la vecchia interfaccia (shell "lha x"): solo il nome, senza cartelle */
typedef struct { const char *cartella; int (*scrivi)(const char *, const char *, const uint8_t *, size_t); } vecchio_t;
static int scrivi_vecchio(void *ctx, const char *perc, const uint8_t *dati, size_t n) {
    vecchio_t *v = ctx;
    const char *b = strrchr(perc, '/');
    return v->scrivi(v->cartella, b ? b + 1 : perc, dati, n);
}
int lha_estrai(const char *archivio, const char *cartella, int (*scrivi)(const char *cartella, const char *nome,
               const uint8_t *dati, size_t n)) {
    vecchio_t v = { cartella, scrivi };
    return lha_estrai_tutto(archivio, &v, scrivi_vecchio);
}

/* un archivio nuovo con un solo file, -lh0-, intestazione di livello 0 */
int lha_crea(const char *archivio, const char *nome_nel_archivio, const uint8_t *dati, size_t n) {
    size_t ln = strlen(nome_nel_archivio);
    if (ln > 200) ln = 200;
    uint8_t h[256];
    h[0] = (uint8_t)(22 + ln);
    memcpy(h + 2, "-lh0-", 5);
    uint32_t v[2] = { (uint32_t)n, (uint32_t)n };
    for (int k = 0; k < 2; k++) for (int b = 0; b < 4; b++) h[7 + 4 * k + b] = (uint8_t)(v[k] >> (8 * b));
    uint32_t ora = (uint32_t)(46u << 25 | 1u << 21 | 1u << 16);   /* 1/1/2026 in formato MS-DOS */
    for (int b = 0; b < 4; b++) h[15 + b] = (uint8_t)(ora >> (8 * b));
    h[19] = 0x20;
    h[20] = 0;
    h[21] = (uint8_t)ln;
    memcpy(h + 22, nome_nel_archivio, ln);
    uint16_t c = crc16(dati, n);
    h[22 + ln] = (uint8_t)c; h[23 + ln] = (uint8_t)(c >> 8);
    uint8_t s = 0;
    for (size_t i = 2; i < (size_t)h[0] + 2; i++) s = (uint8_t)(s + h[i]);
    h[1] = s;
    FILE *f = fopen(archivio, "wb");
    if (!f) return -1;
    fwrite(h, 1, (size_t)h[0] + 2, f);
    fwrite(dati, 1, n, f);
    fputc(0, f);
    fclose(f);
    return 0;
}
