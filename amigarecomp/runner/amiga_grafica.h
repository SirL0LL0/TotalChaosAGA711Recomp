/* amiga_grafica.h - le primitive di graphics.library usate anche da
 * Intuition (amiga_intuition.c). Coordinate relative alla RastPort. */
#pragma once
#include <stdint.h>

extern uint32_t os_gfxbase;
uint32_t os_topaz8(void);
void     os_init_rastport(uint32_t rp);
void     os_linea(uint32_t rp, int xa, int ya, int xb, int yb);
void     os_rettangolo(uint32_t rp, int x0, int y0, int x1, int y1);
void     os_testo(uint32_t rp, uint32_t stringa, int n);       /* alla posizione corrente */
int      os_lunghezza_testo(uint32_t rp, uint32_t stringa, int n);
uint32_t os_apri_font(uint32_t textattr);
uint32_t os_colormap(uint32_t n);
/* colori a 24 bit (0xRRGGBB) nella ColorMap V39 (ColorTable + LowColorBits) */
void     os_cm_colore24(uint32_t cm, uint32_t i, uint32_t rgb);
uint32_t os_cm_rgb24(uint32_t cm, uint32_t i);
void     os_loadrgb32_cm(uint32_t cm, uint32_t tabella);   /* formato di LoadRGB32 */
/* copia un rettangolo fra due bitmap con minterm e maschera dei piani; il
 * ritaglio (cx0..cx1, cy0..cy1, inclusi) e' sulla destinazione, cx1 < 0 = niente */
void     os_blit(uint32_t sbm, int sx, int sy, uint32_t dbm, int dx, int dy, int w, int h,
                 uint8_t minterm, uint8_t maschera, int cx0, int cy0, int cx1, int cy1);

/* i layer delle finestre (amiga_intuition.c): i rettangoli (coordinate della
 * bitmap, inclusi) coperti da finestre davanti a quella del layer, e il suo
 * backing store (SMART_REFRESH: un byte per pixel, il colore; NULL se la
 * finestra non ne ha, come SIMPLE_REFRESH: quel che si disegna sotto si perde) */
typedef struct { int x0, y0, x1, y1; } os_rett_t;
#define OS_MAX_COPERTURE 8
int      os_strato_coperture(uint32_t ly, os_rett_t *r, int max);
uint8_t *os_strato_ombra(uint32_t ly, int x, int y);
static inline int os_coperto(const os_rett_t *r, int n, int x, int y) {
    for (int i = 0; i < n; i++)
        if (x >= r[i].x0 && x <= r[i].x1 && y >= r[i].y0 && y <= r[i].y1) return 1;
    return 0;
}

/* AMIGA_GUARDIA anche per le scritture del runner (amiga_glue.c) */
void os_guardia_c(uint32_t a, const char *chi);

/* sprite hardware presi con GetSprite (SimpleSprite) */
extern uint32_t os_sprite[8];
