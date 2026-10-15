/* amiga_paula.c - l'audio dell'Amiga: Paula, 4 canali DMA di campioni a 8 bit.
 *
 * Per canale: AUDxLC (puntatore), AUDxLEN (lunghezza in parole), AUDxPER
 * (periodo in cicli del clock audio, 3.546.895 Hz PAL), AUDxVOL (0-64).
 * Quando il DMA del canale si accende, Paula copia LC e LEN nei contatori
 * interni e legge il campione byte per byte al ritmo del periodo; finita la
 * lunghezza ricarica LC/LEN (cosi' si fanno i loop) e alza l'interrupt del
 * canale (INTREQ bit 7+x). Canali 0 e 3 a sinistra, 1 e 2 a destra.
 *
 * Il tempo e' quello del gioco: a ogni scrittura su un registro audio si
 * generano prima i campioni fino al ciclo corrente, poi si applica la
 * scrittura; a fine quadro si completa il quadro. Uscita a 48 kHz stereo.
 */
#include "amiga_paula.h"
#include "amiga_hw.h"
#include <string.h>

#define CLOCK_AUDIO   3546895.0
#define CLOCK_CPU     7093790.0

typedef struct {
    uint32_t lc, ptr;        /* registro e contatore interno */
    uint16_t len, resto;     /* parole: registro e rimaste */
    uint16_t per, vol;
    int dma;                 /* DMA acceso */
    double fase;             /* frazione di campione gia' passata */
    int byte;                /* 0/1: quale byte della parola corrente */
    int8_t campione;
} canale_t;

static canale_t s_c[4];
static uint64_t s_cicli_fatti;          /* tempo del gioco gia' trasformato in audio */
static double s_resto_campioni;
static int16_t s_buf[48000 * 2];        /* fino a un secondo di stereo */
static int s_n;                          /* campioni stereo pronti */
static int s_volume = 100;               /* percento */
static uint16_t *s_intreq;

void amiga_paula_init(uint16_t *intreq) { s_intreq = intreq; memset(s_c, 0, sizeof s_c); }
void amiga_paula_volume(int percento) { s_volume = percento; }

static int8_t leggi(uint32_t a) {
    return a < g_amiga_mem_size ? (int8_t)g_amiga_mem[a] : 0;
}

static void avvia(canale_t *c) {
    c->ptr = c->lc & 0x1FFFFE;
    c->resto = c->len ? c->len : 0xFFFF;
    c->byte = 0;
    c->fase = 0;
    c->campione = leggi(c->ptr);
}

void (*amiga_paula_fine_blocco)(int canale);

/* passa al byte successivo; a fine blocco ricarica e chiede l'interrupt */
static void prossimo(canale_t *c, int n) {
    if (c->byte == 0) { c->byte = 1; c->campione = leggi(c->ptr + 1); return; }
    c->byte = 0;
    c->ptr += 2;
    if (--c->resto == 0) {
        if (amiga_paula_fine_blocco) {
            amiga_paula_fine_blocco(n);                 /* puo' cambiare i registri o spegnere */
            if (!c->dma) return;
        }
        c->ptr = c->lc & 0x1FFFFE;
        c->resto = c->len ? c->len : 0xFFFF;
        if (s_intreq) *s_intreq |= (uint16_t)(0x0080 << n);
    }
    c->campione = leggi(c->ptr);
}

static void genera(int quanti) {
    for (int k = 0; k < quanti && s_n < 48000; k++) {
        int sx = 0, dx = 0;
        for (int n = 0; n < 4; n++) {
            canale_t *c = &s_c[n];
            if (!c->dma || c->per < 64) continue;
            int v = c->campione * (c->vol > 64 ? 64 : c->vol);      /* -8192..8128 */
            if (n == 0 || n == 3) sx += v; else dx += v;
            c->fase += CLOCK_AUDIO / c->per / 48000.0;
            while (c->fase >= 1.0) { c->fase -= 1.0; prossimo(c, n); }
        }
        s_buf[s_n * 2]     = (int16_t)(sx * 2 * s_volume / 100);
        s_buf[s_n * 2 + 1] = (int16_t)(dx * 2 * s_volume / 100);
        s_n++;
    }
}

void amiga_paula_avanza(uint64_t cicli) {
    if (cicli <= s_cicli_fatti) return;
    double campioni = (double)(cicli - s_cicli_fatti) * 48000.0 / CLOCK_CPU + s_resto_campioni;
    int n = (int)campioni;
    s_resto_campioni = campioni - n;
    s_cicli_fatti = cicli;
    genera(n);
}

void amiga_paula_registro(uint32_t off, uint16_t v) {
    int n = (int)((off - 0xA0) >> 4);
    if (n < 0 || n > 3) return;
    canale_t *c = &s_c[n];
    switch (off & 0x0F) {
    case 0x0: c->lc = (c->lc & 0x0000FFFF) | (uint32_t)v << 16; break;
    case 0x2: c->lc = (c->lc & 0xFFFF0000) | (v & 0xFFFE); break;
    case 0x4: c->len = v; break;
    case 0x6: c->per = v; break;
    case 0x8: c->vol = v & 0x7F; break;
    }
}

/* AMIGA_AUDIO_LOG=file: ogni avvio di nota (diagnostica) */
#include <stdio.h>
#include <stdlib.h>
static FILE *s_log;
static int s_log_pronto;

void amiga_paula_dmacon(uint16_t prima, uint16_t dopo) {
    if (!s_log_pronto) { s_log_pronto = 1; if (getenv("AMIGA_AUDIO_LOG")) s_log = fopen(getenv("AMIGA_AUDIO_LOG"), "w"); }
    if (!(dopo & 0x0200)) dopo = 0;                   /* senza DMAEN tutto fermo */
    if (!(prima & 0x0200)) prima = 0;
    for (int n = 0; n < 4; n++) {
        int era = (prima >> n) & 1, ora = (dopo >> n) & 1;
        if (ora && !era) {
            s_c[n].dma = 1; avvia(&s_c[n]);
            if (s_log) fprintf(s_log, "%llu %d %06X %u %u %u\n", (unsigned long long)s_cicli_fatti, n,
                               s_c[n].lc, s_c[n].len, s_c[n].per, s_c[n].vol);
        }
        else if (!ora && era) s_c[n].dma = 0;
    }
}

void amiga_paula_imposta(int n, uint32_t lc, uint16_t len, uint16_t per, uint16_t vol) {
    if (n < 0 || n > 3) return;
    s_c[n].lc = lc & 0xFFFFFE; s_c[n].len = len; s_c[n].per = per; s_c[n].vol = vol & 0x7F;
}
void amiga_paula_per_vol(int n, uint16_t per, uint16_t vol) {
    if (n < 0 || n > 3) return;
    s_c[n].per = per; s_c[n].vol = vol & 0x7F;
}
void amiga_paula_accendi(int n) { if (n >= 0 && n < 4 && !s_c[n].dma) { s_c[n].dma = 1; avvia(&s_c[n]); } }
void amiga_paula_spegni(int n) { if (n >= 0 && n < 4) s_c[n].dma = 0; }
int  amiga_paula_acceso(int n) { return n >= 0 && n < 4 && s_c[n].dma; }
/* per la finestra di debug */
void amiga_paula_stato(int n, uint32_t *lc, uint16_t *len, uint16_t *per, uint16_t *vol) {
    *lc = s_c[n & 3].lc; *len = s_c[n & 3].len; *per = s_c[n & 3].per; *vol = s_c[n & 3].vol;
}

int amiga_paula_prendi(int16_t **campioni) {
    *campioni = s_buf;
    int n = s_n;
    s_n = 0;
    return n;
}
