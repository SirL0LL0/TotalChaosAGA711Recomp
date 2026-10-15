/* amiga_shorten.c - decompressione Shorten (vedi amiga_shorten.h).
 *
 * Il flusso, dopo "ajkg" e il byte di versione, e' una sequenza di bit (dal
 * piu' significativo) con codici di Rice: q zeri, un 1, poi k bit. Intestazione
 * (tipo, canali, blocco, ordine LPC massimo, medie, byte da saltare), poi
 * comandi: un blocco di campioni con predittore fisso (DIFF0-3) o LPC
 * quantizzato (QLPC), blocco di zeri, nuova dimensione del blocco, shift,
 * dati da copiare tali e quali (VERBATIM), fine (QUIT). I campioni escono
 * a 8 bit (S8/U8) o a 16 (big o little endian), canali alternati.
 */
#include "amiga_shorten.h"
#include <stdlib.h>
#include <string.h>

enum { FN_DIFF0, FN_DIFF1, FN_DIFF2, FN_DIFF3, FN_QUIT, FN_BLOCKSIZE, FN_BITSHIFT, FN_QLPC, FN_ZERO, FN_VERBATIM };
enum { TYPE_AU1, TYPE_S8, TYPE_U8, TYPE_S16HL, TYPE_U16HL, TYPE_S16LH, TYPE_U16LH };
#define ULONGSIZE 2
#define NSKIPSIZE 1
#define LPCQSIZE 2
#define LPCQUANT 5
#define TYPESIZE 4
#define CHANSIZE 0
#define ENERGYSIZE 3
#define BITSHIFTSIZE 2
#define FNSIZE 2
#define VERBATIM_CKSIZE_SIZE 5
#define VERBATIM_BYTE_SIZE 8
#define NWRAP 3
#define MAX_CANALI 8
#define MAX_ORDINE 32
#define MAX_MEDIE 32
#define MAX_BLOCCO 65535

typedef struct { const uint8_t *d; size_t n, bit; int errore; } bits_t;

static unsigned leggi_bit(bits_t *b) {
    if (b->bit >= b->n * 8) { b->errore = 1; return 0; }
    unsigned v = b->d[b->bit >> 3] >> (7 - (b->bit & 7)) & 1;
    b->bit++;
    return v;
}
static uint32_t leggi_bits(bits_t *b, int k) {
    uint32_t v = 0;
    while (k-- > 0) v = v << 1 | leggi_bit(b);
    return v;
}
/* Rice senza segno: zeri fino a un 1, poi k bit */
static uint32_t rice(bits_t *b, int k) {
    uint32_t q = 0;
    while (!leggi_bit(b)) { if (b->errore || ++q > (1u << 24)) { b->errore = 1; return 0; } }
    return q << k | leggi_bits(b, k);
}
/* con segno: k+1 bit, l'ultimo e' il segno */
static int32_t rice_s(bits_t *b, int k) {
    uint32_t u = rice(b, k + 1);
    return (int32_t)(u >> 1) ^ -(int32_t)(u & 1);
}

typedef struct { uint8_t *d; size_t n, cap; int errore; } uscita_t;
static void metti(uscita_t *o, uint8_t v) {
    if (o->n == o->cap) {
        size_t c = o->cap ? o->cap * 2 : 1 << 16;
        uint8_t *nd = realloc(o->d, c);
        if (!nd) { o->errore = 1; return; }
        o->d = nd; o->cap = c;
    }
    o->d[o->n++] = v;
}

int shorten_e(const uint8_t *d, size_t n) { return n >= 5 && !memcmp(d, "ajkg", 4); }

int shorten_decomprimi(const uint8_t *dati, size_t n, uint8_t **uscita, size_t *n_uscita) {
    *uscita = NULL; *n_uscita = 0;
    if (!shorten_e(dati, n)) return 0;
    int versione = dati[4];
    if (versione > 3) return 0;
    bits_t b = { dati + 5, n - 5, 0, 0 };
#define UINT(k) (versione ? rice(&b, (int)rice(&b, ULONGSIZE)) : rice(&b, (k)))
    int tipo = (int)UINT(TYPESIZE);
    int canali = (int)UINT(CHANSIZE);
    int blocco = (int)UINT(8);                      /* log2(256) */
    int ordine_max = (int)UINT(LPCQSIZE);
    int medie = (int)UINT(0);
    int salta = (int)UINT(NSKIPSIZE);
    if (b.errore || canali < 1 || canali > MAX_CANALI || blocco < 1 || blocco > MAX_BLOCCO ||
        ordine_max > MAX_ORDINE || medie > MAX_MEDIE)
        return 0;
    if (tipo != TYPE_S8 && tipo != TYPE_U8 && (tipo < TYPE_S16HL || tipo > TYPE_U16LH)) return 0;
    for (int i = 0; i < salta; i++) leggi_bits(&b, 8);
    int nwrap = ordine_max > NWRAP ? ordine_max : NWRAP;
    int lpcqoffset = versione > 1 ? (1 << LPCQUANT) : 0;
    int32_t media0 = tipo == TYPE_U8 ? 0x80 : (tipo == TYPE_U16HL || tipo == TYPE_U16LH) ? 0x8000 : 0;
    int32_t *mem[MAX_CANALI], offset[MAX_CANALI][MAX_MEDIE > 0 ? MAX_MEDIE : 1];
    for (int c = 0; c < canali; c++) {
        mem[c] = calloc((size_t)(MAX_BLOCCO + nwrap), sizeof(int32_t));
        for (int i = 0; i < (medie ? medie : 1); i++) offset[c][i] = media0;
    }
    int bitshift = 0, canale = 0, ok = 1;
    uscita_t o = { 0 };
    int32_t *blk[MAX_CANALI];                        /* campioni del blocco, canale per canale */
    for (;;) {
        if (b.errore || o.errore) { ok = 0; break; }
        int cmd = (int)rice(&b, FNSIZE);
        if (cmd == FN_QUIT) break;
        if (cmd == FN_BLOCKSIZE) {
            uint32_t nb = UINT(31 - __builtin_clz((unsigned)blocco));
            if (!nb || nb > MAX_BLOCCO) { ok = 0; break; }
            blocco = (int)nb;
            continue;
        }
        if (cmd == FN_BITSHIFT) { bitshift = (int)rice(&b, BITSHIFTSIZE); if (bitshift > 31) { ok = 0; break; } continue; }
        if (cmd == FN_VERBATIM) {
            int len = (int)rice(&b, VERBATIM_CKSIZE_SIZE);
            for (int i = 0; i < len; i++) metti(&o, (uint8_t)rice(&b, VERBATIM_BYTE_SIZE));
            continue;
        }
        if (cmd > FN_VERBATIM) { ok = 0; break; }
        /* un blocco audio del canale corrente */
        int32_t *d = mem[canale] + nwrap;
        int res = 0;
        if (cmd != FN_ZERO) {
            res = (int)rice(&b, ENERGYSIZE);
            if (versione == 0) res--;
        }
        int32_t coffset;
        if (!medie) coffset = offset[canale][0];
        else {
            int32_t somma = versione < 2 ? 0 : medie / 2;
            for (int i = 0; i < medie; i++) somma += offset[canale][i];
            coffset = somma / medie;
            if (versione >= 2) coffset = bitshift == 0 ? coffset : coffset >> (bitshift - 1) >> 1;
        }
        if (cmd == FN_ZERO) {
            for (int i = 0; i < blocco; i++) d[i] = 0;
        } else {
            int32_t coeff[MAX_ORDINE];
            int ordine, qshift, init;
            if (cmd == FN_QLPC) {
                ordine = (int)rice(&b, LPCQSIZE);
                if (ordine > ordine_max) { ok = 0; break; }
                for (int i = 0; i < ordine; i++) coeff[i] = rice_s(&b, LPCQUANT);
                qshift = LPCQUANT;
                if (coffset) for (int i = -ordine; i < 0; i++) d[i] -= coffset;
                init = lpcqoffset;              /* anche con ordine 0, come lo shorten originale */
            } else {
                static const int32_t fissi[4][3] = { { 0 }, { 1 }, { 2, -1 }, { 3, -3, 1 } };
                ordine = cmd;
                for (int i = 0; i < ordine; i++) coeff[i] = fissi[cmd][i];
                qshift = 0;
                init = ordine ? 0 : coffset;
            }
            for (int i = 0; i < blocco; i++) {
                int64_t s = init;
                for (int j = 0; j < ordine; j++) s += (int64_t)coeff[j] * d[i - j - 1];
                d[i] = rice_s(&b, res) + (int32_t)(s >> qshift);
            }
            if (cmd == FN_QLPC && coffset) for (int i = 0; i < blocco; i++) d[i] += coffset;
        }
        /* la media per i prossimi blocchi */
        if (medie > 0) {
            int64_t somma = versione < 2 ? 0 : blocco / 2;
            for (int i = 0; i < blocco; i++) somma += d[i];
            for (int i = 1; i < medie; i++) offset[canale][i - 1] = offset[canale][i];
            offset[canale][medie - 1] = versione < 2 ? (int32_t)(somma / blocco)
                                                     : (int32_t)((somma / blocco) * ((int64_t)1 << bitshift));
        }
        blk[canale] = d;
        if (++canale == canali) {
            /* tutti i canali del blocco: in uscita, alternati */
            for (int i = 0; i < blocco; i++)
                for (int c = 0; c < canali; c++) {
                    int32_t v = blk[c][i] << bitshift;
                    switch (tipo) {
                    case TYPE_S8: case TYPE_U8: metti(&o, (uint8_t)v); break;
                    case TYPE_S16HL: case TYPE_U16HL: metti(&o, (uint8_t)(v >> 8)); metti(&o, (uint8_t)v); break;
                    default: metti(&o, (uint8_t)v); metti(&o, (uint8_t)(v >> 8)); break;
                    }
                }
            for (int c = 0; c < canali; c++)          /* la coda per il predittore */
                for (int i = -nwrap; i < 0; i++) blk[c][i] = blk[c][i + blocco];
            canale = 0;
        }
    }
#undef UINT
    for (int c = 0; c < canali; c++) free(mem[c]);
    if (!ok) { free(o.d); return 0; }
    *uscita = o.d; *n_uscita = o.n;
    return 1;
}
