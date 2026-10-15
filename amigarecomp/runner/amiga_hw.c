/* amiga_hw.c - chip custom, CIA-A e video di un A500 (OCS), quanto basta ai
 * giochi che prendono la macchina.
 *
 * Video: a fine quadro il copper (MOVE e WAIT) viene eseguito riga per riga
 * e i bitplane lowres producono 320x256 pixel nella finestra di
 * visualizzazione DIWSTRT/DIWSTOP. Niente blitter, niente sprite (per ora).
 * Tastiera: un codice per quadro nel registro seriale del CIA-A, come fa il
 * controller della tastiera dell'Amiga, con l'interrupt PORTS.
 */
#include "amiga_hw.h"
#include "amiga_paula.h"
#include <string.h>

#include "genesis_runtime.h"     /* g_cycle_accumulator: posizione del pennello */

uint8_t *g_amiga_mem;
uint32_t g_amiga_mem_size;
uint64_t g_amiga_letture_vhpos;     /* diagnostica: letture di VHPOSR */

static uint16_t s_custom[0x100];
static uint16_t s_dmacon, s_intena, s_intreq;
static uint32_t s_quadro[AMIGA_LARGHEZZA * AMIGA_ALTEZZA];
static int s_altezza_visibile = AMIGA_ALTEZZA_GIOCO;   /* righe della finestra video (DIWSTRT-DIWSTOP) */
static unsigned long s_quadri;

static uint8_t s_cia_icr_mask, s_cia_icr_dati, s_cia_sdr, s_cia_pra = 0xFF;
static uint8_t s_ciab[16];
static uint8_t s_tasti[64];
static int s_n_tasti;

static int s_fuoco1, s_fuoco2, s_su, s_giu, s_sx, s_dx, s_mx, s_my, s_destro;


/* istante attuale in cicli CPU dall'avvio: quadri completati + posizione nel quadro */
static uint64_t adesso(void) {
    return (uint64_t)s_quadri * AMIGA_CICLI_QUADRO + g_cycle_accumulator / g_amiga_cpu_x;
}

static void scrivi_custom(uint32_t off, uint16_t v) {
    off &= 0x1FE;
    if (off >= 0x0A0 && off < 0x0E0) {                /* audio: prima si genera fin qui */
        amiga_paula_avanza(adesso());
        amiga_paula_registro(off, v);
    }
    switch (off) {
    case 0x096: {
        uint16_t prima = s_dmacon;
        s_dmacon = (v & 0x8000) ? (uint16_t)(s_dmacon | (v & 0x7FFF)) : (uint16_t)(s_dmacon & ~v);
        if ((prima ^ s_dmacon) & 0x020F) {
            amiga_paula_avanza(adesso());
            amiga_paula_dmacon(prima, s_dmacon);
        }
        return;
    }
    case 0x09A: s_intena = (v & 0x8000) ? (uint16_t)(s_intena | (v & 0x7FFF)) : (uint16_t)(s_intena & ~v); return;
    case 0x09C: s_intreq = (v & 0x8000) ? (uint16_t)(s_intreq | (v & 0x7FFF)) : (uint16_t)(s_intreq & ~v); return;
    case 0x088: return;                  /* COPJMP1: il copper riparte da COP1LC a ogni quadro */
    }
    s_custom[off / 2] = v;
}

static uint16_t joydat(int su, int giu, int sx, int dx) {
    int b1 = dx, b9 = sx, b0 = giu ^ dx, b8 = su ^ sx;
    return (uint16_t)(b9 << 9 | b8 << 8 | b1 << 1 | b0);
}

static uint16_t leggi_custom(uint32_t off) {
    off &= 0x1FE;
    uint32_t t = g_cycle_accumulator / g_amiga_cpu_x;      /* tempo dei chip, non della CPU */
    uint32_t riga = t / AMIGA_CICLI_RIGA, h = (t % AMIGA_CICLI_RIGA) / 2;
    switch (off) {
    case 0x002: return s_dmacon;
    case 0x004: return (uint16_t)((riga >> 8) & 1);
    case 0x006: g_amiga_letture_vhpos++; return (uint16_t)((riga & 0xFF) << 8 | (h & 0xFF));
    case 0x00A: return (uint16_t)((s_my & 0xFF) << 8 | (s_mx & 0xFF));
    case 0x00C: return joydat(s_su, s_giu, s_sx, s_dx);
    case 0x016: return (uint16_t)(0xFF00 & ~(s_destro ? 0x0400 : 0));   /* POTGOR: DATLY = destro porta 0 */
    case 0x01C: return s_intena;
    case 0x01E: return s_intreq;
    }
    return s_custom[off / 2];
}

uint8_t amiga_hw_read8(uint32_t a) {
    if (a >= 0xDFF000 && a < 0xDFF200) {
        uint16_t v = leggi_custom(a & ~1u);
        return (a & 1) ? (uint8_t)v : (uint8_t)(v >> 8);
    }
    if ((a & 0xFFF000) == 0xBFE000 && (a & 1)) {           /* CIA-A, registri dispari */
        switch ((a >> 8) & 15) {
        case 0x0: return (uint8_t)((s_cia_pra & 0x3F) | (s_fuoco1 ? 0 : 0x40) | (s_fuoco2 ? 0 : 0x80));
        case 0x8: return (uint8_t)s_quadri;                 /* TOD basso: contatore di quadri */
        case 0xC: return s_cia_sdr;
        case 0xD: { uint8_t r = s_cia_icr_dati; if (r) r |= 0x80; s_cia_icr_dati = 0; return r; }
        }
        return 0xFF;
    }
    if ((a & 0xFFF000) == 0xBFD000 && !(a & 1))             /* CIA-B, registri pari */
        return s_ciab[(a >> 8) & 15];
    return 0xFF;
}

/* CIA-B: solo i registri. Chi usa i suoi timer (un player di musica tramite
 * ciab.resource) li legge da qui per sapere a che ritmo chiamarlo */
unsigned amiga_hw_ciab_timer(int b) {
    return b ? (unsigned)(s_ciab[7] << 8 | s_ciab[6]) : (unsigned)(s_ciab[5] << 8 | s_ciab[4]);
}
int amiga_hw_ciab_avviato(int b) { return s_ciab[b ? 0xF : 0xE] & 1; }

uint16_t amiga_hw_read16(uint32_t a) {
    if (a >= 0xDFF000 && a < 0xDFF200) return leggi_custom(a);
    return (uint16_t)(amiga_hw_read8(a) << 8 | amiga_hw_read8(a + 1));
}

void amiga_hw_write8(uint32_t a, uint8_t v) {
    if (a >= 0xDFF000 && a < 0xDFF200) { scrivi_custom(a & ~1u, (uint16_t)(v << 8 | v)); return; }
    if ((a & 0xFFF000) == 0xBFE000 && (a & 1)) {
        switch ((a >> 8) & 15) {
        case 0x0: s_cia_pra = v; break;
        case 0xD: if (v & 0x80) s_cia_icr_mask |= v & 0x7F; else s_cia_icr_mask &= (uint8_t)~v; break;
        }
    }
    if ((a & 0xFFF000) == 0xBFD000 && !(a & 1)) s_ciab[(a >> 8) & 15] = v;
}

void amiga_hw_write16(uint32_t a, uint16_t v) {
    if (a >= 0xDFF000 && a < 0xDFF200) { scrivi_custom(a, v); return; }
    amiga_hw_write8(a, (uint8_t)(v >> 8));
    amiga_hw_write8(a + 1, (uint8_t)v);
}

int amiga_hw_livello_interrupt(void) {
    if (!(s_intena & 0x4000)) return 0;
    uint16_t att = s_intena & s_intreq & 0x3FFF;
    if (att & 0x2000) return 6;
    if (att & 0x1800) return 5;
    if (att & 0x0780) return 4;
    if (att & 0x0070) return 3;
    if (att & 0x0008) return 2;
    if (att & 0x0007) return 1;
    return 0;
}

/* Lo schermo lo compone Intuition (os_video): qui non si emula il copper.
 * Il gioco puo' lasciare in COP1LC un indirizzo qualsiasi, e leggerlo come
 * lista copper costava 20 ms a quadro e scriveva a caso nei registri. */

void amiga_hw_init(void) { amiga_paula_init(&s_intreq); }

int amiga_hw_audio(int16_t **campioni) { return amiga_paula_prendi(campioni); }

/* l'audio fino a questo istante del quadro (prima di cambiare i canali, e
 * nelle attese a passi brevi) */
void amiga_hw_audio_adesso(void) { amiga_paula_avanza(adesso()); }

void amiga_hw_fine_quadro(void) {
    amiga_paula_avanza((uint64_t)(s_quadri + 1) * AMIGA_CICLI_QUADRO);   /* audio del quadro finito */
    s_quadri++;
    if (s_n_tasti && (s_cia_icr_mask & 0x08)) {      /* un codice tastiera per quadro */
        uint8_t k = s_tasti[0];
        memmove(s_tasti, s_tasti + 1, (size_t)--s_n_tasti);
        s_cia_sdr = (uint8_t)~((k << 1) | (k >> 7));
        s_cia_icr_dati |= 0x08;
        s_intreq |= 0x0008;
    }
    s_intreq |= 0x0020;                                /* vertical blank */
}

const uint32_t *amiga_hw_framebuffer(void) { return s_quadro; }
int amiga_hw_altezza_visibile(void) { return s_altezza_visibile; }

void amiga_hw_tasto(uint8_t raw, int rilasciato) {
    if (s_n_tasti < (int)sizeof s_tasti) s_tasti[s_n_tasti++] = (uint8_t)(raw | (rilasciato ? 0x80 : 0));
}

void amiga_hw_joystick(int su, int giu, int sx, int dx, int fuoco) {
    s_su = su; s_giu = giu; s_sx = sx; s_dx = dx; s_fuoco2 = fuoco;
}

void amiga_hw_mouse(int dx, int dy, int sinistro, int destro) {
    s_mx += dx; s_my += dy; s_fuoco1 = sinistro; s_destro = destro;
}
