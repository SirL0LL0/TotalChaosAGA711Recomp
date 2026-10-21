/* amiga_grafica.c - graphics.library (V34) in C.
 *
 * Si disegna davvero sulle bitmap planari in memoria emulata (come il
 * blitter): il programma puo' leggerle e scriverle anche da solo. Una
 * RastPort con un Layer (la finestra) ha coordinate relative all'angolo del
 * layer ed e' ritagliata ai suoi bordi; senza layer si disegna sulla bitmap
 * intera. I font sono TextFont veri (quelli caricati da diskfont e topaz di
 * sistema); la palette sta nella ColorMap del ViewPort.
 */
#include "amiga_os.h"
#include "amiga_grafica.h"
#include "genesis_runtime.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const char *const os_nomi_graphics[N_GFX_V40];
uint32_t os_gfxbase;
static uint32_t s_topaz8;

/* ---------------------------------------------------------- strutture */
/* RastPort */
#define RP_LAYER 0
#define RP_BITMAP 4
#define RP_AREAPTRN 8
#define RP_TMPRAS 12
#define RP_AREAINFO 16
#define RP_MASK 24
#define RP_FGPEN 25
#define RP_BGPEN 26
#define RP_AOLPEN 27
#define RP_DRAWMODE 28
#define RP_AREAPTSZ 29
#define RP_FLAGS 32
#define RP_LINEPTRN 34
#define RP_CPX 36
#define RP_CPY 38
#define RP_FONT 52
#define RP_ALGOSTYLE 56
#define RP_TXFLAGS 57
#define RP_TXHEIGHT 58
#define RP_TXWIDTH 60
#define RP_TXBASELINE 62
#define RP_TXSPACING 64
#define RP_SIZE 100
/* BitMap */
#define BM_BYTESPERROW 0
#define BM_ROWS 2
#define BM_DEPTH 5
#define BM_PLANES 8
/* Layer */
#define LY_MINX 16
#define LY_MINY 18
#define LY_MAXX 20
#define LY_MAXY 22
/* TextFont */
#define TF_YSIZE 20
#define TF_STYLE 22
#define TF_FLAGS 23
#define TF_XSIZE 24
#define TF_BASELINE 26
#define TF_BOLDSMEAR 28
#define TF_ACCESSORS 30
#define TF_LOCHAR 32
#define TF_HICHAR 33
#define TF_CHARDATA 34
#define TF_MODULO 38
#define TF_CHARLOC 40
#define TF_CHARSPACE 44
#define TF_CHARKERN 48
#define FPF_PROPORTIONAL 0x20
#define FSF_UNDERLINED 1
#define FSF_BOLD 2
#define FSF_ITALIC 4
#define JAM1 0
#define JAM2 1
#define COMPLEMENT 2
#define INVERSVID 4

/* ---------------------------------------------------------- disegno di base */

typedef struct {
    uint32_t bm;
    int ox, oy;                    /* origine (angolo del layer) sulla bitmap */
    int x0, y0, x1, y1;            /* ritaglio, coordinate della bitmap, inclusi */
    int bpr, righe, prof;
    uint32_t piani[8];
    uint8_t maschera;
    /* le parti del layer coperte da altre finestre: li' si scrive (e si
     * legge) nel suo backing store invece che sulla bitmap */
    uint32_t ly;
    int n_cop;
    os_rett_t cop[OS_MAX_COPERTURE];
} ctx_t;

/* i piani che un pixel del backing store ha: quelli della bitmap e della maschera */
static inline uint8_t maschera_ombra(const ctx_t *c) {
    return (uint8_t)(c->maschera & ((1u << c->prof) - 1u));
}

static int ctx(uint32_t rp, ctx_t *c) {
    c->bm = R32(rp + RP_BITMAP);
    if (!c->bm) return 0;
    c->bpr = R16(c->bm + BM_BYTESPERROW);
    c->righe = R16(c->bm + BM_ROWS);
    c->prof = R8(c->bm + BM_DEPTH);
    if (c->prof > 8) c->prof = 8;
    for (int i = 0; i < c->prof; i++) c->piani[i] = R32(c->bm + BM_PLANES + 4 * i);
    c->maschera = R8(rp + RP_MASK);
    c->x0 = 0; c->y0 = 0; c->x1 = c->bpr * 8 - 1; c->y1 = c->righe - 1;
    c->ox = c->oy = 0;
    uint32_t ly = R32(rp + RP_LAYER);
    c->ly = ly;
    c->n_cop = 0;
    if (ly) {
        int mx = S16(ly + LY_MINX), my = S16(ly + LY_MINY), Mx = S16(ly + LY_MAXX), My = S16(ly + LY_MAXY);
        c->ox = mx; c->oy = my;
        if (mx > c->x0) c->x0 = mx;
        if (my > c->y0) c->y0 = my;
        if (Mx < c->x1) c->x1 = Mx;
        if (My < c->y1) c->y1 = My;
        c->n_cop = os_strato_coperture(ly, c->cop, OS_MAX_COPERTURE);
    }
    return 1;
}

/* un pixel coperto: va nel backing store (o si perde se non c'e') */
static void px_ombra(const ctx_t *c, int x, int y, int col, int xor_) {
    uint8_t *o = os_strato_ombra(c->ly, x, y);
    if (!o) return;
    uint8_t m = maschera_ombra(c);
    *o = xor_ ? (uint8_t)(*o ^ m) : (uint8_t)((*o & ~m) | (col & m));
}

/* pixel alla posizione della bitmap (gia' con l'origine), nel colore col */
static inline void px(const ctx_t *c, int x, int y, int col) {
    if (x < c->x0 || x > c->x1 || y < c->y0 || y > c->y1) return;
    if (c->n_cop && os_coperto(c->cop, c->n_cop, x, y)) { px_ombra(c, x, y, col, 0); return; }
    uint32_t off = (uint32_t)y * (uint32_t)c->bpr + (uint32_t)(x >> 3);
    uint8_t bit = (uint8_t)(0x80 >> (x & 7));
    for (int p = 0; p < c->prof; p++) {
        if (!(c->maschera >> p & 1) || !c->piani[p]) continue;
        os_guardia_c(c->piani[p] + off, "punto");
        uint8_t *b = g_amiga_mem + c->piani[p] + off;
        if (col >> p & 1) *b |= bit; else *b &= (uint8_t)~bit;
    }
}
static inline void px_xor(const ctx_t *c, int x, int y) {
    if (x < c->x0 || x > c->x1 || y < c->y0 || y > c->y1) return;
    if (c->n_cop && os_coperto(c->cop, c->n_cop, x, y)) { px_ombra(c, x, y, 0, 1); return; }
    uint32_t off = (uint32_t)y * (uint32_t)c->bpr + (uint32_t)(x >> 3);
    uint8_t bit = (uint8_t)(0x80 >> (x & 7));
    for (int p = 0; p < c->prof; p++)
        if ((c->maschera >> p & 1) && c->piani[p]) { os_guardia_c(c->piani[p] + off, "punto xor"); g_amiga_mem[c->piani[p] + off] ^= bit; }
}
static inline int leggi_px(const ctx_t *c, int x, int y) {
    if (x < 0 || y < 0 || x >= c->bpr * 8 || y >= c->righe) return 0;
    if (c->n_cop && os_coperto(c->cop, c->n_cop, x, y)) {
        uint8_t *o = os_strato_ombra(c->ly, x, y);
        if (o) return *o;
    }
    uint32_t off = (uint32_t)y * (uint32_t)c->bpr + (uint32_t)(x >> 3);
    uint8_t bit = (uint8_t)(0x80 >> (x & 7));
    int v = 0;
    for (int p = 0; p < c->prof; p++)
        if (c->piani[p] && (g_amiga_mem[c->piani[p] + off] & bit)) v |= 1 << p;
    return v;
}

/* un punto con la penna e il modo della RastPort; x, y relativi al layer */
static void punto_rp(uint32_t rp, const ctx_t *c, int x, int y) {
    uint8_t modo = R8(rp + RP_DRAWMODE);
    if (modo & COMPLEMENT) px_xor(c, x + c->ox, y + c->oy);
    else px(c, x + c->ox, y + c->oy, R8(rp + RP_FGPEN));
}

/* rettangolo pieno nel colore col (coordinate della bitmap, gia' ritagliate) */
static void riempi(const ctx_t *c, int x0, int y0, int x1, int y1, int col, int xor_) {
    if (x0 < c->x0) x0 = c->x0;
    if (y0 < c->y0) y0 = c->y0;
    if (x1 > c->x1) x1 = c->x1;
    if (y1 > c->y1) y1 = c->y1;
    if (x0 > x1 || y0 > y1) return;
    /* in parte coperto: pixel per pixel (le parti coperte nel backing store) */
    for (int i = 0; i < c->n_cop; i++) {
        const os_rett_t *r = &c->cop[i];
        if (r->x1 < x0 || r->x0 > x1 || r->y1 < y0 || r->y0 > y1) continue;
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                if (xor_) px_xor(c, x, y); else px(c, x, y, col);
            }
        return;
    }
    for (int p = 0; p < c->prof; p++) {
        if (!(c->maschera >> p & 1) || !c->piani[p]) continue;
        int uno = col >> p & 1;
        for (int y = y0; y <= y1; y++) {
            uint8_t *r = g_amiga_mem + c->piani[p] + (uint32_t)y * (uint32_t)c->bpr;
            for (int x = x0; x <= x1;) {
                uint8_t m;
                int b = x >> 3;
                if ((x & 7) == 0 && x + 7 <= x1) { m = 0xFF; x += 8; }
                else { m = (uint8_t)(0x80 >> (x & 7)); x++; }
                os_guardia_c(c->piani[p] + (uint32_t)y * (uint32_t)c->bpr + (uint32_t)b, "riempi");
                if (xor_) r[b] ^= m;
                else if (uno) r[b] |= m;
                else r[b] &= (uint8_t)~m;
            }
        }
    }
}

/* ---------------------------------------------------------- funzioni */

static void g_initbitmap(void) {
    /* InitBitMap(bm A0, depth D0, width D1, height D2) */
    uint32_t bm = A0;
    W16(bm + BM_BYTESPERROW, ((D1 + 15) >> 4) << 1);
    W16(bm + BM_ROWS, D2);
    W8(bm + 4, 0);
    W8(bm + BM_DEPTH, D0);
    W16(bm + 6, 0);
}

static void init_rastport(uint32_t rp) {
    memset(g_amiga_mem + rp, 0, RP_SIZE);
    W8(rp + RP_MASK, 0xFF);
    W8(rp + RP_FGPEN, 1);
    W8(rp + RP_DRAWMODE, JAM2);
    W16(rp + RP_LINEPTRN, 0xFFFF);
    W8(rp + 30, 15);                                   /* linpatcnt */
    W16(rp + 48, 1); W16(rp + 50, 1);                  /* PenWidth, PenHeight */
    W8(rp + RP_AOLPEN, 1);
    if (s_topaz8) {
        W32(rp + RP_FONT, s_topaz8);
        W16(rp + RP_TXHEIGHT, R16(s_topaz8 + TF_YSIZE));
        W16(rp + RP_TXWIDTH, R16(s_topaz8 + TF_XSIZE));
        W16(rp + RP_TXBASELINE, R16(s_topaz8 + TF_BASELINE));
    }
}
void os_init_rastport(uint32_t rp) { init_rastport(rp); }
static void g_initrastport(void) { init_rastport(A1); }

static void g_setapen(void) { W8(A1 + RP_FGPEN, D0); }
static void g_setbpen(void) { W8(A1 + RP_BGPEN, D0); }
static void g_setdrmd(void) { W8(A1 + RP_DRAWMODE, D0); }
static void g_move(void) { W16(A1 + RP_CPX, D0); W16(A1 + RP_CPY, D1); }

void os_linea(uint32_t rp, int xa, int ya, int xb, int yb) {
    ctx_t c;
    if (!ctx(rp, &c)) return;
    int dx = abs(xb - xa), dy = -abs(yb - ya), sx = xa < xb ? 1 : -1, sy = ya < yb ? 1 : -1, e = dx + dy;
    uint16_t motivo = R16(rp + RP_LINEPTRN);
    int k = 0;
    for (;;) {
        if (motivo == 0xFFFF || (motivo >> (15 - (k & 15)) & 1)) punto_rp(rp, &c, xa, ya);
        else if (R8(rp + RP_DRAWMODE) & JAM2) px(&c, xa + c.ox, ya + c.oy, R8(rp + RP_BGPEN));
        k++;
        if (xa == xb && ya == yb) break;
        int e2 = 2 * e;
        if (e2 >= dy) { e += dy; xa += sx; }
        if (e2 <= dx) { e += dx; ya += sy; }
    }
}
static void g_draw(void) {
    os_linea(A1, S16(A1 + RP_CPX), S16(A1 + RP_CPY), (int16_t)D0, (int16_t)D1);
    W16(A1 + RP_CPX, D0); W16(A1 + RP_CPY, D1);
}
static void g_polydraw(void) {
    /* PolyDraw(rp A1, count D0, array A0) */
    for (uint32_t i = 0; i < (D0 & 0xFFFF); i++) {
        int x = S16(A0 + 4 * i), y = S16(A0 + 4 * i + 2);
        os_linea(A1, S16(A1 + RP_CPX), S16(A1 + RP_CPY), x, y);
        W16(A1 + RP_CPX, x); W16(A1 + RP_CPY, y);
    }
}

void os_rettangolo(uint32_t rp, int x0, int y0, int x1, int y1) {
    ctx_t c;
    if (!ctx(rp, &c)) return;
    uint8_t modo = R8(rp + RP_DRAWMODE);
    riempi(&c, x0 + c.ox, y0 + c.oy, x1 + c.ox, y1 + c.oy, R8(rp + RP_FGPEN), modo & COMPLEMENT);
}
static void g_rectfill(void) { os_rettangolo(A1, (int16_t)D0, (int16_t)D1, (int16_t)D2, (int16_t)D3); }

static void g_writepixel(void) {
    ctx_t c;
    if (!ctx(A1, &c)) { D0 = (uint32_t)-1; return; }
    punto_rp(A1, &c, (int16_t)D0, (int16_t)D1);
    D0 = 0;
}
static void g_readpixel(void) {
    ctx_t c;
    if (!ctx(A1, &c)) { D0 = (uint32_t)-1; return; }
    int x = (int16_t)D0 + c.ox, y = (int16_t)D1 + c.oy;
    D0 = (x < c.x0 || x > c.x1 || y < c.y0 || y > c.y1) ? (uint32_t)-1 : (uint32_t)leggi_px(&c, x, y);
}

void os_setrast(uint32_t rp, int penna) {
    ctx_t c;
    if (!ctx(rp, &c)) return;
    riempi(&c, c.x0, c.y0, c.x1, c.y1, penna, 0);
}
static void g_setrast(void) { os_setrast(A1, (int)D0); }

/* ---------------------------------------------------------- testo */

static void imposta_font(uint32_t rp, uint32_t tf) {
    W32(rp + RP_FONT, tf);
    if (!tf) return;
    W16(rp + RP_TXHEIGHT, R16(tf + TF_YSIZE));
    W16(rp + RP_TXWIDTH, R16(tf + TF_XSIZE));
    W16(rp + RP_TXBASELINE, R16(tf + TF_BASELINE));
}
static void g_setfont(void) { imposta_font(A1, A0); }

static void g_askfont(void) {
    /* AskFont(rp A1, textAttr A0) */
    uint32_t tf = R32(A1 + RP_FONT);
    W32(A0, tf ? R32(tf + LN_NAME) : 0);
    W16(A0 + 4, R16(A1 + RP_TXHEIGHT));
    W8(A0 + 6, R8(A1 + RP_ALGOSTYLE) | (tf ? R8(tf + TF_STYLE) : 0));
    W8(A0 + 7, tf ? R8(tf + TF_FLAGS) : 0);
}

/* larghezza di un carattere nel font (con spaziatura e kern) */
static int larghezza_car(uint32_t tf, uint8_t ch) {
    uint8_t lo = R8(tf + TF_LOCHAR), hi = R8(tf + TF_HICHAR);
    int i = (ch >= lo && ch <= hi) ? ch - lo : hi - lo + 1;      /* il glifo "altro" in fondo */
    uint32_t sp = R32(tf + TF_CHARSPACE), kr = R32(tf + TF_CHARKERN);
    int w = (R8(tf + TF_FLAGS) & FPF_PROPORTIONAL) && sp ? S16(sp + 2 * (uint32_t)i) : S16(tf + TF_XSIZE);
    if (kr) w += S16(kr + 2 * (uint32_t)i);
    return w;
}

int os_lunghezza_testo(uint32_t rp, uint32_t s, int n) {
    uint32_t tf = R32(rp + RP_FONT);
    if (!tf) return n * 8;
    int w = 0;
    for (int i = 0; i < n; i++) w += larghezza_car(tf, R8(s + (uint32_t)i)) + S16(rp + RP_TXSPACING);
    if (R8(rp + RP_ALGOSTYLE) & FSF_BOLD) w += n;
    return w;
}
static void g_textlength(void) { D0 = (uint32_t)os_lunghezza_testo(A1, A0, (int)(D0 & 0xFFFF)); }

void os_testo(uint32_t rp, uint32_t s, int n) {
    ctx_t c;
    uint32_t tf = R32(rp + RP_FONT);
    if (!tf || !ctx(rp, &c)) return;
    int x = S16(rp + RP_CPX), y = S16(rp + RP_CPY) - S16(tf + TF_BASELINE);
    int alto = R16(tf + TF_YSIZE), mod = R16(tf + TF_MODULO);
    uint8_t lo = R8(tf + TF_LOCHAR), hi = R8(tf + TF_HICHAR);
    uint32_t dati = R32(tf + TF_CHARDATA), loc = R32(tf + TF_CHARLOC);
    uint32_t sp = R32(tf + TF_CHARSPACE), kr = R32(tf + TF_CHARKERN);
    int prop = (R8(tf + TF_FLAGS) & FPF_PROPORTIONAL) && sp;
    uint8_t modo = R8(rp + RP_DRAWMODE), stile = R8(rp + RP_ALGOSTYLE);
    int fg = R8(rp + RP_FGPEN), bg = R8(rp + RP_BGPEN);
    if (modo & INVERSVID) { int t = fg; fg = bg; bg = t; }
    for (int k = 0; k < n; k++) {
        uint8_t ch = R8(s + (uint32_t)k);
        int i = (ch >= lo && ch <= hi) ? ch - lo : hi - lo + 1;
        uint32_t l = R32(loc + 4 * (uint32_t)i);
        int bit0 = (int)(l >> 16), lar = (int)(l & 0xFFFF);
        int avanza = prop ? S16(sp + 2 * (uint32_t)i) : S16(tf + TF_XSIZE);
        int kern = kr ? S16(kr + 2 * (uint32_t)i) : 0;
        int cx = x + kern;
        /* lo sfondo della cella in JAM2 */
        if ((modo & 3) == JAM2)
            riempi(&c, x + c.ox, y + c.oy, x + avanza + c.ox - 1 + (stile & FSF_BOLD ? 1 : 0),
                   y + alto - 1 + c.oy, bg, 0);
        for (int r = 0; r < alto; r++) {
            int incl = (stile & FSF_ITALIC) ? (alto - 1 - r) / 2 : 0;
            for (int b = 0; b < lar; b++) {
                int bb = bit0 + b;
                int on = R8(dati + (uint32_t)(r * mod) + (uint32_t)(bb >> 3)) >> (7 - (bb & 7)) & 1;
                if (!on) continue;
                for (int g = 0; g <= ((stile & FSF_BOLD) ? 1 : 0); g++) {
                    int X = cx + b + g + incl + c.ox, Y = y + r + c.oy;
                    if (modo & COMPLEMENT) px_xor(&c, X, Y); else px(&c, X, Y, fg);
                }
            }
        }
        if (stile & FSF_UNDERLINED)
            for (int b = 0; b < avanza; b++) {
                int X = x + b + c.ox, Y = y + S16(tf + TF_BASELINE) + 1 + c.oy;
                if (modo & COMPLEMENT) px_xor(&c, X, Y); else px(&c, X, Y, fg);
            }
        x += avanza + S16(rp + RP_TXSPACING) + ((stile & FSF_BOLD) ? 1 : 0);
    }
    W16(rp + RP_CPX, x);
}
static void g_text(void) { os_testo(A1, A0, (int)(D0 & 0xFFFF)); }

static void g_setsoftstyle(void) {
    /* SetSoftStyle(rp A1, style D0, enable D1): restituisce lo stile */
    uint8_t st = (uint8_t)((R8(A1 + RP_ALGOSTYLE) & ~D1) | (D0 & D1));
    W8(A1 + RP_ALGOSTYLE, st);
    D0 = st;
}
static void g_asksoftstyle(void) { D0 = FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED; }

/* i font in memoria: GfxBase->TextFonts (diskfont ci aggiunge i suoi) */
uint32_t os_apri_font(uint32_t ta) {
    char nome[64];
    os_leggi_stringa(R32(ta), nome, sizeof nome);
    uint16_t alto = R16(ta + 4);
    uint32_t migliore = 0;
    for (uint32_t n = R32(os_gfxbase + 140); R32(n + LN_SUCC); n = R32(n + LN_SUCC)) {
        char b[64];
        os_leggi_stringa(R32(n + LN_NAME), b, sizeof b);
        if (strcasecmp(b, nome)) continue;
        if (R16(n + TF_YSIZE) == alto) { migliore = n; break; }
    }
    if (migliore) W16(migliore + TF_ACCESSORS, R16(migliore + TF_ACCESSORS) + 1);
    return migliore;
}
static void g_openfont(void) {
    D0 = os_apri_font(A0);
    if (os_traccia) {
        char nome[64];
        os_leggi_stringa(R32(A0), nome, sizeof nome);
        fprintf(stderr, "[gfx] OpenFont(\"%s\", %u) = $%06X\n", nome, R16(A0 + 4), D0);
    }
}
static void g_closefont(void) {
    if (A1 && R16(A1 + TF_ACCESSORS)) W16(A1 + TF_ACCESSORS, R16(A1 + TF_ACCESSORS) - 1);
}
static void g_addfont(void) {
    W8(A1 + LN_TYPE, 12);                                   /* NT_FONT */
    os_aggiungi_coda(os_gfxbase + 140, A1);
}
static void g_remfont(void) { os_togli(A1); }

/* ---------------------------------------------------------- blit */

/* minterm su un byte: sorgente B, destinazione C (come BltBitMap: A = maschera) */
static inline uint8_t minterm(uint8_t mt, uint8_t b, uint8_t c) {
    uint8_t r = 0;
    if (mt & 0x80) r |= b & c;
    if (mt & 0x40) r |= b & ~c;
    if (mt & 0x20) r |= ~b & c;
    if (mt & 0x10) r |= ~b & ~c;
    return r;
}

/* i layer della prossima os_blit (ClipBlit, BltBitMapRastPort, ScrollRaster):
 * le parti coperte della destinazione si scrivono nel suo backing store, quelle
 * della sorgente si leggono dal suo. NULL = bitmap senza layer */
static const ctx_t *s_blit_d, *s_blit_s;

static inline int bit_ombra(const ctx_t *c, int x, int y, int p) {
    uint8_t *o = os_strato_ombra(c->ly, x, y);
    return o ? (*o >> p & 1) : 0;
}

/* copia un rettangolo fra bitmap, piano per piano, con minterm e maschera */
void os_blit(uint32_t sbm, int sx, int sy, uint32_t dbm, int dx, int dy, int w, int h,
             uint8_t mt, uint8_t maschera, int cx0, int cy0, int cx1, int cy1) {
    const ctx_t *lc_d = s_blit_d && s_blit_d->n_cop ? s_blit_d : NULL;
    const ctx_t *lc_s = s_blit_s && s_blit_s->n_cop ? s_blit_s : NULL;
    s_blit_d = s_blit_s = NULL;
    if (w <= 0 || h <= 0) return;
    int sbpr = R16(sbm), srighe = R16(sbm + 2), sprof = R8(sbm + 5);
    int dbpr = R16(dbm), drighe = R16(dbm + 2), dprof = R8(dbm + 5);
    /* ritaglio della destinazione (layer o bitmap) */
    int x0 = 0, y0 = 0, x1 = dbpr * 8 - 1, y1 = drighe - 1;
    if (cx1 >= 0) {
        if (cx0 > x0) x0 = cx0;
        if (cy0 > y0) y0 = cy0;
        if (cx1 < x1) x1 = cx1;
        if (cy1 < y1) y1 = cy1;
    }
    if (dx < x0) { sx += x0 - dx; w -= x0 - dx; dx = x0; }
    if (dy < y0) { sy += y0 - dy; h -= y0 - dy; dy = y0; }
    if (dx + w - 1 > x1) w = x1 - dx + 1;
    if (dy + h - 1 > y1) h = y1 - dy + 1;
    if (sx < 0) { dx -= sx; w += sx; sx = 0; }
    if (sy < 0) { dy -= sy; h += sy; sy = 0; }
    if (sx + w > sbpr * 8) w = sbpr * 8 - sx;
    if (sy + h > srighe) h = srighe - sy;
    if (w <= 0 || h <= 0) return;
    /* stessa bitmap e sovrapposti: si copia nel verso giusto */
    int giu = !(sbm == dbm && dy > sy);
    for (int p = 0; p < dprof && p < 8; p++) {
        if (!(maschera >> p & 1)) continue;
        uint32_t dp = R32(dbm + 8 + 4 * p);
        uint32_t spn = p < sprof ? R32(sbm + 8 + 4 * p) : 0;
        if (!dp) continue;
        for (int k = 0; k < h; k++) {
            int r = giu ? k : h - 1 - k;
            int destra = sbm == dbm && dy == sy && dx > sx;
            for (int q = 0; q < w; q++) {
                int cidx = destra ? w - 1 - q : q;
                int xs = sx + cidx, xd = dx + cidx;
                uint8_t b = 0;
                if (lc_s && os_coperto(lc_s->cop, lc_s->n_cop, xs, sy + r) && os_strato_ombra(lc_s->ly, xs, sy + r))
                    b = bit_ombra(lc_s, xs, sy + r, p) ? 0xFF : 0;
                else if (spn && spn != 0xFFFFFFFFu)
                    b = (g_amiga_mem[spn + (uint32_t)(sy + r) * (uint32_t)sbpr + (uint32_t)(xs >> 3)] >> (7 - (xs & 7))) & 1 ? 0xFF : 0;
                else if (spn == 0xFFFFFFFFu) b = 0xFF;           /* piano tutto a uno */
                if (lc_d && os_coperto(lc_d->cop, lc_d->n_cop, xd, dy + r)) {
                    uint8_t *o = os_strato_ombra(lc_d->ly, xd, dy + r);
                    if (o) {
                        uint8_t c = (*o >> p & 1) ? 0xFF : 0;
                        if (minterm(mt, b, c) & 1) *o |= (uint8_t)(1u << p); else *o &= (uint8_t)~(1u << p);
                    }
                    continue;
                }
                uint8_t *d = g_amiga_mem + dp + (uint32_t)(dy + r) * (uint32_t)dbpr + (uint32_t)(xd >> 3);
                uint8_t m = (uint8_t)(0x80 >> (xd & 7));
                os_guardia_c((uint32_t)(d - g_amiga_mem), "blit");
                uint8_t c = *d & m ? 0xFF : 0;
                uint8_t v = minterm(mt, b, c);
                if (v & 1) *d |= m; else *d &= (uint8_t)~m;
            }
        }
    }
}

static void g_bltbitmap(void) {
    /* BltBitMap(src A0, sx D0, sy D1, dst A1, dx D2, dy D3, w D4, h D5, minterm D6, mask D7, tmp A2) */
    os_blit(A0, (int16_t)D0, (int16_t)D1, A1, (int16_t)D2, (int16_t)D3, (int16_t)D4, (int16_t)D5,
            (uint8_t)D6, (uint8_t)D7, 0, 0, -1, -1);
    D0 = 8;
}
static void g_clipblit(void) {
    /* ClipBlit(srcrp A0, sx D0, sy D1, dstrp A1, dx D2, dy D3, w D4, h D5, minterm D6) */
    ctx_t s, d;
    if (!ctx(A0, &s) || !ctx(A1, &d)) return;
    s_blit_s = &s; s_blit_d = &d;
    os_blit(s.bm, (int16_t)D0 + s.ox, (int16_t)D1 + s.oy, d.bm, (int16_t)D2 + d.ox, (int16_t)D3 + d.oy,
            (int16_t)D4, (int16_t)D5, (uint8_t)D6, d.maschera, d.x0, d.y0, d.x1, d.y1);
}
static void g_bltbitmaprastport(void) {
    /* BltBitMapRastPort(src A0, sx D0, sy D1, dstrp A1, dx D2, dy D3, w D4, h D5, minterm D6) */
    ctx_t d;
    if (!ctx(A1, &d)) return;
    s_blit_d = &d;
    os_blit(A0, (int16_t)D0, (int16_t)D1, d.bm, (int16_t)D2 + d.ox, (int16_t)D3 + d.oy,
            (int16_t)D4, (int16_t)D5, (uint8_t)D6, d.maschera, d.x0, d.y0, d.x1, d.y1);
}
static void g_bltmaskbitmaprastport(void) {
    /* BltMaskBitMapRastPort(src A0, sx D0, sy D1, dstrp A1, dx D2, dy D3,
     * w D4, h D5, minterm D6, maschera A2): dove la maschera (un piano grande
     * come quelli della sorgente) ha 1 si copia il pixel della sorgente, il
     * resto della destinazione resta (il minterm tipico, $E0) */
    ctx_t c;
    if (!ctx(A1, &c)) return;
    uint32_t bm = A0, mask = A2;
    int bpr = R16(bm), prof = R8(bm + 5);
    int bpr_m = (R8(bm + 4) & 1) && prof ? bpr / prof : bpr;      /* interleaved: la maschera e' un piano */
    uint32_t piani[8];
    for (int p = 0; p < prof && p < 8; p++) piani[p] = R32(bm + 8 + 4u * p);
    int sx = (int16_t)D0, sy = (int16_t)D1, dx = (int16_t)D2 + c.ox, dy = (int16_t)D3 + c.oy;
    for (int r = 0; r < (int16_t)D5; r++)
        for (int b = 0; b < (int16_t)D4; b++) {
            int x = sx + b, y = sy + r;
            uint8_t bit = (uint8_t)(0x80 >> (x & 7));
            if (mask && !(R8(mask + (uint32_t)(y * bpr_m) + (uint32_t)(x >> 3)) & bit)) continue;
            int v = 0;
            for (int p = 0; p < prof && p < 8; p++)
                if (piani[p] && (R8(piani[p] + (uint32_t)(y * bpr) + (uint32_t)(x >> 3)) & bit)) v |= 1 << p;
            px(&c, dx + b, dy + r, v);
        }
}
static void g_bltclear(void) {
    /* BltClear(mem A1, count D0, flags D1): bit 1 = D0 e' righe<<16|byte */
    uint32_t n = (D1 & 2) ? (D0 >> 16) * (D0 & 0xFFFF) : D0;
    if (A1 + n <= g_amiga_mem_size) memset(g_amiga_mem + A1, 0, n);
}
static void g_blttemplate(void) {
    /* BltTemplate(source A0, srcX D0, srcMod D1, rp A1, dstX D2, dstY D3, w D4, h D5):
     * dove la sagoma ha 1 si disegna con la penna (JAM1/JAM2/COMPLEMENT) */
    ctx_t c;
    if (!ctx(A1, &c)) return;
    uint8_t modo = R8(A1 + RP_DRAWMODE);
    for (int r = 0; r < (int16_t)D5; r++)
        for (int b = 0; b < (int16_t)D4; b++) {
            int bb = (int)D0 + b;
            int on = R8(A0 + (uint32_t)r * (D1 & 0xFFFF) + (uint32_t)(bb >> 3)) >> (7 - (bb & 7)) & 1;
            int X = (int16_t)D2 + b + c.ox, Y = (int16_t)D3 + r + c.oy;
            if (on) { if (modo & COMPLEMENT) px_xor(&c, X, Y); else px(&c, X, Y, R8(A1 + RP_FGPEN)); }
            else if ((modo & 3) == JAM2) px(&c, X, Y, R8(A1 + RP_BGPEN));
        }
}
static void g_scrollraster(void) {
    /* ScrollRaster(rp A1, dx D0, dy D1, xmin D2, ymin D3, xmax D4, ymax D5):
     * sposta il contenuto di (-dx,-dy) e pulisce con BgPen */
    ctx_t c;
    if (!ctx(A1, &c)) return;
    int dx = (int16_t)D0, dy = (int16_t)D1;
    int x0 = (int16_t)D2 + c.ox, y0 = (int16_t)D3 + c.oy, x1 = (int16_t)D4 + c.ox, y1 = (int16_t)D5 + c.oy;
    int w = x1 - x0 + 1, h = y1 - y0 + 1;
    ctx_t cl = c;
    cl.x0 = x0 > c.x0 ? x0 : c.x0; cl.y0 = y0 > c.y0 ? y0 : c.y0;
    cl.x1 = x1 < c.x1 ? x1 : c.x1; cl.y1 = y1 < c.y1 ? y1 : c.y1;
    s_blit_s = &c; s_blit_d = &c;
    os_blit(c.bm, x0 + dx, y0 + dy, c.bm, x0, y0, w, h, 0xC0, c.maschera, cl.x0, cl.y0, cl.x1, cl.y1);
    int bg = R8(A1 + RP_BGPEN);
    if (dx > 0) riempi(&cl, x1 - dx + 1, y0, x1, y1, bg, 0);
    if (dx < 0) riempi(&cl, x0, y0, x0 - dx - 1, y1, bg, 0);
    if (dy > 0) riempi(&cl, x0, y1 - dy + 1, x1, y1, bg, 0);
    if (dy < 0) riempi(&cl, x0, y0, x1, y0 - dy - 1, bg, 0);
}

/* ---------------------------------------------------------- aree (poligoni) */

/* AreaInfo: VctrTbl 0, VctrPtr 4, FlagTbl 8, FlagPtr 12, Count 16, MaxCount 18, FirstX 20, FirstY 22 */
static void g_initarea(void) {
    W32(A0 + 0, A1); W32(A0 + 4, A1);
    W32(A0 + 8, A1 + 4 * D0); W32(A0 + 12, A1 + 4 * D0);
    W16(A0 + 16, 0); W16(A0 + 18, D0);
}
static void area_punto(uint32_t ai, int x, int y) {
    uint16_t n = R16(ai + 16);
    if (n >= R16(ai + 18)) return;
    uint32_t v = R32(ai) + 4u * n;
    W16(v, x); W16(v + 2, y);
    W16(ai + 16, n + 1);
}
static void g_areamove(void) {
    uint32_t ai = R32(A1 + RP_AREAINFO);
    if (!ai) { D0 = (uint32_t)-1; return; }
    area_punto(ai, (int16_t)D0, (int16_t)D1);
    W16(ai + 20, D0); W16(ai + 22, D1);
    D0 = 0;
}
static void g_areadraw(void) {
    uint32_t ai = R32(A1 + RP_AREAINFO);
    if (!ai) { D0 = (uint32_t)-1; return; }
    area_punto(ai, (int16_t)D0, (int16_t)D1);
    D0 = 0;
}
static void g_areaend(void) {
    /* riempimento pari-dispari riga per riga, poi il contorno con AOlPen */
    uint32_t ai = R32(A1 + RP_AREAINFO);
    ctx_t c;
    if (!ai || !ctx(A1, &c)) { D0 = (uint32_t)-1; return; }
    int n = R16(ai + 16);
    uint32_t v = R32(ai);
    int ymin = 32767, ymax = -32768;
    for (int i = 0; i < n; i++) { int y = S16(v + 4u * i + 2); if (y < ymin) ymin = y; if (y > ymax) ymax = y; }
    int fg = R8(A1 + RP_FGPEN);
    for (int y = ymin; y <= ymax; y++) {
        int xs[64], k = 0;
        for (int i = 0; i < n && k < 64; i++) {
            int xa = S16(v + 4u * i), ya = S16(v + 4u * i + 2);
            int j = (i + 1) % n;
            int xb = S16(v + 4u * j), yb = S16(v + 4u * j + 2);
            if ((ya <= y && yb > y) || (yb <= y && ya > y))
                xs[k++] = xa + (y - ya) * (xb - xa) / (yb - ya);
        }
        for (int a = 0; a < k; a++) for (int b = a + 1; b < k; b++) if (xs[b] < xs[a]) { int t = xs[a]; xs[a] = xs[b]; xs[b] = t; }
        for (int a = 0; a + 1 < k; a += 2) riempi(&c, xs[a] + c.ox, y + c.oy, xs[a + 1] + c.ox, y + c.oy, fg, 0);
    }
    if (R16(A1 + RP_FLAGS) & 8) {                               /* AREAOUTLINE */
        uint8_t fgp = R8(A1 + RP_FGPEN);
        W8(A1 + RP_FGPEN, R8(A1 + RP_AOLPEN));
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            os_linea(A1, S16(v + 4u * i), S16(v + 4u * i + 2), S16(v + 4u * j), S16(v + 4u * j + 2));
        }
        W8(A1 + RP_FGPEN, fgp);
    }
    W16(ai + 16, 0);
    D0 = 0;
}
static void g_drawellipse(void) {
    /* DrawEllipse(rp A1, cx D0, cy D1, a D2, b D3) */
    int cx = (int16_t)D0, cy = (int16_t)D1, a = (int16_t)D2, b = (int16_t)D3;
    ctx_t c;
    if (!ctx(A1, &c)) return;
    for (int i = 0; i < 360; i++) {
        double t = i * 3.14159265358979 / 180.0;
        punto_rp(A1, &c, cx + (int)(a * cos(t)), cy + (int)(b * sin(t)));
    }
}
static void g_initmprast(void) { W32(A0, A1); W32(A0 + 4, D0); D0 = A0; }   /* InitTmpRas */

/* ---------------------------------------------------------- colori */

/* ColorMap V39: Flags 0, Type 1 (2 = V39), Count 2, ColorTable 4 (i 4 bit
 * alti di R, G, B in una UWORD), cm_vpe 8, LowColorBits 12 (i 4 bit bassi,
 * stessa forma). Le funzioni a 12 bit scrivono il nibble anche nei bit bassi
 * (come l'AGA: $F diventa $FF), quelle a 32 bit li tengono separati */
#define CM_LOWCOLORBITS 12
uint32_t os_colormap(uint32_t n) {
    uint32_t cm = os_alloca(52, MEMF_PUBLIC | MEMF_CLEAR);
    uint32_t t = os_alloca(2 * n, MEMF_PUBLIC | MEMF_CLEAR);
    uint32_t b = os_alloca(2 * n, MEMF_PUBLIC | MEMF_CLEAR);
    W8(cm + 1, 2);
    W16(cm + 2, n);
    W32(cm + 4, t);
    W32(cm + CM_LOWCOLORBITS, b);
    return cm;
}
/* un colore a 24 bit (0xRRGGBB) nella ColorMap */
void os_cm_colore24(uint32_t cm, uint32_t i, uint32_t rgb) {
    if (!cm || i >= R16(cm + 2)) return;
    uint32_t r = rgb >> 16 & 0xFF, g = rgb >> 8 & 0xFF, b = rgb & 0xFF;
    W16(R32(cm + 4) + 2 * i, (r >> 4) << 8 | (g >> 4) << 4 | (b >> 4));
    if (R8(cm + 1) >= 2 && R32(cm + CM_LOWCOLORBITS))
        W16(R32(cm + CM_LOWCOLORBITS) + 2 * i, (r & 15) << 8 | (g & 15) << 4 | (b & 15));
}
/* e letto: 0xRRGGBB */
uint32_t os_cm_rgb24(uint32_t cm, uint32_t i) {
    if (!cm || i >= R16(cm + 2)) return 0;
    uint32_t a = R16(R32(cm + 4) + 2 * i);
    uint32_t b = (R8(cm + 1) >= 2 && R32(cm + CM_LOWCOLORBITS)) ? R16(R32(cm + CM_LOWCOLORBITS) + 2 * i) : a;
    return ((a >> 8 & 15) << 4 | (b >> 8 & 15)) << 16 | ((a >> 4 & 15) << 4 | (b >> 4 & 15)) << 8 |
           ((a & 15) << 4 | (b & 15));
}
static void colore12(uint32_t cm, uint32_t i, uint32_t c) {
    uint32_t r = c >> 8 & 15, g = c >> 4 & 15, b = c & 15;
    os_cm_colore24(cm, i, (r * 17) << 16 | (g * 17) << 8 | (b * 17));
}
static void g_getcolormap(void) { D0 = os_colormap(D0); }
static void g_freecolormap(void) {}
void os_loadrgb4(uint32_t vp, uint32_t colori, int quanti) {
    uint32_t cm = R32(vp + 4);
    if (!cm) return;
    uint16_t n = R16(cm + 2);
    for (uint32_t i = 0; i < (uint32_t)quanti && i < n; i++) colore12(cm, i, R16(colori + 2 * i));
}
/* LoadRGB4(vp A0, colors A1, count D0) */
/* con un requester del gioco aperto sullo schermo del tabellone (amiga_asl.c)
 * i cambi di tavolozza del gioco (il ciclo dei colori) aspettano */
int os_tavolozza_ferma;
static void g_loadrgb4(void) { if (!os_tavolozza_ferma) os_loadrgb4(A0, A1, (int)(D0 & 0xFFFF)); }
static void g_setrgb4(void) {
    /* SetRGB4(vp A0, n D0, r D1, g D2, b D3) */
    if (os_tavolozza_ferma) return;
    colore12(R32(A0 + 4), D0 & 0xFFFF, (D1 & 15) << 8 | (D2 & 15) << 4 | (D3 & 15));
}
static void g_setrgb4cm(void) {
    colore12(A0, D0 & 0xFFFF, (D1 & 15) << 8 | (D2 & 15) << 4 | (D3 & 15));
}
/* V39: colori a 32 bit per componente (allineati a sinistra: conta il byte alto) */
static void g_setrgb32(void) {
    /* SetRGB32(vp A0, n D0, r D1, g D2, b D3) */
    if (os_tavolozza_ferma) return;
    os_cm_colore24(R32(A0 + 4), D0, (D1 >> 24) << 16 | (D2 >> 24) << 8 | (D3 >> 24));
}
static void g_setrgb32cm(void) {
    os_cm_colore24(A0, D0, (D1 >> 24) << 16 | (D2 >> 24) << 8 | (D3 >> 24));
}
/* LoadRGB32(vp A0, table A1): (UWORD n, UWORD primo, n * 3 ULONG) ripetuti,
 * finiti da n = 0 */
void os_loadrgb32_cm(uint32_t cm, uint32_t t) {
    for (int giri = 0; t && giri < 64; giri++) {
        uint32_t n = R16(t), primo = R16(t + 2);
        if (!n) break;
        t += 4;
        for (uint32_t i = 0; i < n; i++, t += 12)
            os_cm_colore24(cm, primo + i, (R32(t) >> 24) << 16 | (R32(t + 4) >> 24) << 8 | (R32(t + 8) >> 24));
    }
}
static void g_loadrgb32(void) { if (A0 && A1 && !os_tavolozza_ferma) os_loadrgb32_cm(R32(A0 + 4), A1); }
static void g_getrgb32(void) {
    /* GetRGB32(cm A0, primo D0, quanti D1, table A1): 3 ULONG per colore */
    for (uint32_t i = 0; i < D1; i++) {
        uint32_t c = os_cm_rgb24(A0, D0 + i);
        W32(A1 + 12 * i, (c >> 16 & 0xFF) * 0x01010101u);
        W32(A1 + 12 * i + 4, (c >> 8 & 0xFF) * 0x01010101u);
        W32(A1 + 12 * i + 8, (c & 0xFF) * 0x01010101u);
    }
}
static void g_getrgb4(void) {
    uint32_t cm = A0;
    D0 = (!cm || (D0 & 0xFFFF) >= R16(cm + 2)) ? (uint32_t)-1 : R16(R32(cm + 4) + 2 * (D0 & 0xFFFF));
}

/* ---------------------------------------------------------- copper utente */
/* UCopList: Next 0, FirstCopList 4, CopList 8; CopList: Next 0, _CopList 4,
 * _ViewPort 8, CopIns 12, CopPtr 16, CopLStart 20, CopSStart 24, Count 28,
 * MaxCount 30. Le istruzioni si tengono in una tabella nostra (CopIns):
 * per ogni CWait/CMove 6 byte: OpCode.W (0 MOVE, 1 WAIT), due parole. */
#define UCL_CI 4
static void g_ucopperlistinit(void) {
    /* UCopperListInit(ucl A0, n D0) */
    uint32_t cl = os_alloca(36, MEMF_PUBLIC | MEMF_CLEAR);
    uint32_t ins = os_alloca(6 * (D0 + 2), MEMF_PUBLIC | MEMF_CLEAR);
    W32(cl + 12, ins); W32(cl + 16, ins);
    W16(cl + 30, D0 + 1);
    W32(A0 + 4, cl); W32(A0 + 8, cl);
    D0 = cl;
}
static void copper_ins(uint32_t ucl, int op, int a, int b) {
    uint32_t cl = R32(ucl + 8);
    if (!cl) return;
    uint16_t n = R16(cl + 28);
    if (n >= R16(cl + 30)) return;
    uint32_t p = R32(cl + 12) + 6u * n;
    W16(p, op); W16(p + 2, a); W16(p + 4, b);
}
static void g_cwait(void) { copper_ins(A1, 1, (int16_t)D0, (int16_t)D1); }  /* CWait(ucl A1, v D0, h D1) */
static void g_cmove(void) { copper_ins(A1, 0, (int)(D0 & 0x1FE), (int)(D1 & 0xFFFF)); }  /* CMove(ucl A1, reg D0, val D1) */
static void g_cbump(void) {
    uint32_t cl = R32(A1 + 8);
    if (cl) W16(cl + 28, R16(cl + 28) + 1);
}
static void g_freevportcoplists(void) {}

/* ---------------------------------------------------------- vari */

void os_task_attendi_quadro(void);                 /* amiga_task.c */
static void g_waittof(void) { os_task_attendi_quadro(); }
static void g_waitblit(void) {}
static void g_niente(void) {}
static void g_vbeampos(void) { D0 = 0; }
static void g_allocraster(void) {
    D0 = os_alloca(((D0 + 15) >> 4) * 2 * D1, MEMF_CHIP | MEMF_CLEAR);
}
static void g_freeraster(void) { os_libera(A0, ((D0 + 15) >> 4) * 2 * D1); }
static void g_initvport(void) { memset(g_amiga_mem + A0, 0, 40); }
static void g_initview(void) { memset(g_amiga_mem + A1, 0, 18); }
static void g_scrollvport(void) {}
static void g_lockl(void) {}

/* sprite hardware (GetSprite/ChangeSprite/MoveSprite): si ricordano per il video */
uint32_t os_sprite[8];
int os_sprite_x[8], os_sprite_y[8];
static void g_getsprite(void) {
    /* GetSprite(sprite A0, n D0): SimpleSprite posctldata 0, height 4, x 6, y 8, num 10 */
    int n = (int16_t)D0;
    if (n < 0) for (n = 2; n < 8 && os_sprite[n]; n++) {}
    if (n >= 8 || os_sprite[n]) { D0 = (uint32_t)-1; return; }
    os_sprite[n] = A0;
    W16(A0 + 10, n);
    D0 = (uint32_t)n;
}
static void g_freesprite(void) { if ((D0 & 7) == D0) os_sprite[D0] = 0; }
static void g_changesprite(void) {
    /* ChangeSprite(vp A0, sprite A1, newdata A2) */
    W32(A1, A2);
}
static void g_movesprite(void) {
    /* MoveSprite(vp A0, sprite A1, x D0, y D1) */
    W16(A1 + 6, D0); W16(A1 + 8, D1);
}

/* ---------------------------------------------------------- V36-V40 */

/* penne e modo (V39) */
static void g_getapen(void) { D0 = R8(A0 + RP_FGPEN); }
static void g_getbpen(void) { D0 = R8(A0 + RP_BGPEN); }
static void g_getdrmd(void) { D0 = R8(A0 + RP_DRAWMODE); }
static void g_getoutlinepen(void) { D0 = R8(A0 + RP_AOLPEN); }
static void g_setabpendrmd(void) {
    /* SetABPenDrMd(rp A1, apen D0, bpen D1, drawmode D2) */
    W8(A1 + RP_FGPEN, D0); W8(A1 + RP_BGPEN, D1); W8(A1 + RP_DRAWMODE, D2);
}
static void g_setoutlinepen(void) {
    /* SetOutlinePen(rp A0, pen D0): restituisce la vecchia; accende AREAOUTLINE */
    uint32_t v = R8(A0 + RP_AOLPEN);
    W8(A0 + RP_AOLPEN, D0);
    W16(A0 + RP_FLAGS, R16(A0 + RP_FLAGS) | 0x0008);
    D0 = v;
}
static void g_setwritemask(void) { W8(A0 + RP_MASK, D0); D0 = 1; }
static void g_setmaxpen(void) {}
static void g_setchiprev(void) { D0 = 0x0F; }            /* SETCHIPREV_BEST: AGA */

/* penne condivise: la ColorMap ha cm_Count a +2; si cerca il colore piu' vicino */
static uint32_t colore_vicino(uint32_t cm, uint32_t r, uint32_t g, uint32_t b, int massimo) {
    int n = cm ? R16(cm + 2) : 0, meglio = 0;
    if (massimo >= 0 && massimo + 1 < n) n = massimo + 1;
    long d0 = 0x7FFFFFFF;
    for (int i = 0; i < n; i++) {
        uint32_t c = os_cm_rgb24(cm, (uint32_t)i);
        long dr = (long)(c >> 16 & 0xFF) - (long)(r >> 24), dg = (long)(c >> 8 & 0xFF) - (long)(g >> 24),
             db = (long)(c & 0xFF) - (long)(b >> 24), d = dr * dr + dg * dg + db * db;
        if (d < d0) { d0 = d; meglio = i; }
    }
    return (uint32_t)meglio;
}
static void g_obtainbestpena(void) { D0 = colore_vicino(A0, D1, D2, D3, -1); }  /* (cm A0, r D1, g D2, b D3, tags A1) */
static void g_findcolor(void) { D0 = colore_vicino(A3, D1, D2, D3, (int32_t)D4); }  /* (cm A3, r, g, b, maxpen D4) */
static void g_obtainpen(void) {
    /* ObtainPen(cm A0, n D0, r D1, g D2, b D3, flags D4): -1 = nessuna libera;
     * PENF_NO_SETCOLOR (2) lascia il colore com'e' */
    int32_t n = (int32_t)D0;
    if (n < 0) { D0 = (uint32_t)-1; return; }
    if (!(D4 & 2))
        os_cm_colore24(A0, (uint32_t)n, (D1 >> 24) << 16 | (D2 >> 24) << 8 | (D3 >> 24));
    D0 = (uint32_t)n;
}
static void g_releasepen(void) {}

/* AllocBitMap(larg D0, alt D1, prof D2, flags D3, amica A0): piani separati
 * in chip, sempre puliti. FreeBitMap(bm A0). GetBitMapAttr(bm A0, attr D1) */
static void g_allocbitmap(void) {
    uint32_t bpr = ((D0 + 15) >> 4) << 1, righe = D1, prof = D2 > 8 ? 8 : D2;
    uint32_t bm = os_alloca(40, MEMF_PUBLIC | MEMF_CLEAR);
    if (!bm) { D0 = 0; return; }
    W16(bm + BM_BYTESPERROW, bpr); W16(bm + BM_ROWS, righe); W8(bm + BM_DEPTH, prof);
    for (uint32_t p = 0; p < prof; p++) {
        uint32_t pl = os_alloca(bpr * righe, MEMF_CHIP | MEMF_CLEAR);
        if (!pl) { D0 = 0; return; }
        W32(bm + BM_PLANES + 4 * p, pl);
    }
    D0 = bm;
}
static void g_freebitmap(void) {
    uint32_t bm = A0;
    if (!bm) return;
    uint32_t sz = (uint32_t)R16(bm + BM_BYTESPERROW) * R16(bm + BM_ROWS);
    for (int p = 0; p < R8(bm + BM_DEPTH) && p < 8; p++) {
        uint32_t pl = R32(bm + BM_PLANES + 4 * p);
        if (pl && pl != 0xFFFFFFFF) os_libera(pl, sz);
    }
    os_libera(bm, 40);
}
static void g_getbitmapattr(void) {
    uint32_t bm = A0;
    switch (D1) {
    case 0: D0 = R16(bm + BM_ROWS); break;                    /* BMA_HEIGHT */
    case 4: D0 = R8(bm + BM_DEPTH); break;                     /* BMA_DEPTH */
    case 8: D0 = (uint32_t)R16(bm + BM_BYTESPERROW) * 8; break; /* BMA_WIDTH */
    case 12: D0 = R8(bm + 4) | 8; break;                      /* BMA_FLAGS: BMF_STANDARD */
    default: D0 = 0;
    }
}

/* pixel a byte (chunky): un byte per pixel, coordinate del layer */
static void scrivi_chunky(uint32_t rp, int x0, int y0, int x1, int y1, uint32_t src, uint32_t mod) {
    ctx_t c;
    if (!ctx(rp, &c)) return;
    for (int y = y0; y <= y1; y++, src += mod)
        for (int x = x0; x <= x1; x++)
            px(&c, x + c.ox, y + c.oy, R8(src + (uint32_t)(x - x0)));
}
static uint32_t leggi_chunky(uint32_t rp, int x0, int y0, int x1, int y1, uint32_t dst, uint32_t mod) {
    ctx_t c;
    if (!ctx(rp, &c)) return 0;
    uint32_t n = 0;
    for (int y = y0; y <= y1; y++, dst += mod)
        for (int x = x0; x <= x1; x++, n++)
            W8(dst + (uint32_t)(x - x0), (uint8_t)leggi_px(&c, x + c.ox, y + c.oy));
    return n;
}
static void g_writechunkypixels(void) {
    /* WriteChunkyPixels(rp A0, xs D0, ys D1, xe D2, ye D3, array A2, bpr D4) */
    scrivi_chunky(A0, (int16_t)D0, (int16_t)D1, (int16_t)D2, (int16_t)D3, A2, D4);
}
static void g_writepixelarray8(void) {
    /* WritePixelArray8(rp A0, xs D0, ys D1, xe D2, ye D3, array A2, temprp A1):
     * righe di larghezza arrotondata a 16 */
    int l = (int16_t)D2 - (int16_t)D0 + 1;
    scrivi_chunky(A0, (int16_t)D0, (int16_t)D1, (int16_t)D2, (int16_t)D3, A2, (uint32_t)((l + 15) & ~15));
    D0 = (uint32_t)(l * ((int16_t)D3 - (int16_t)D1 + 1));
}
static void g_writepixelline8(void) {
    /* WritePixelLine8(rp A0, x D0, y D1, n D2, array A2, temprp A1) */
    scrivi_chunky(A0, (int16_t)D0, (int16_t)D1, (int16_t)D0 + (int)D2 - 1, (int16_t)D1, A2, 0);
    D0 = D2;
}
static void g_readpixelarray8(void) {
    int l = (int16_t)D2 - (int16_t)D0 + 1;
    D0 = leggi_chunky(A0, (int16_t)D0, (int16_t)D1, (int16_t)D2, (int16_t)D3, A2, (uint32_t)((l + 15) & ~15));
}
static void g_readpixelline8(void) {
    D0 = leggi_chunky(A0, (int16_t)D0, (int16_t)D1, (int16_t)D0 + (int)D2 - 1, (int16_t)D1, A2, 0);
}
static void g_eraserect(void) {
    /* EraseRect(rp A1, xmin D0, ymin D1, xmax D2, ymax D3): colore 0 */
    ctx_t c;
    if (!ctx(A1, &c)) return;
    riempi(&c, (int16_t)D0 + c.ox, (int16_t)D1 + c.oy, (int16_t)D2 + c.ox, (int16_t)D3 + c.oy, 0, 0);
}
static void g_getvpmodeid(void) { D0 = 0x00029004; }    /* PAL HIRES LACE */
static void g_modenotavailable(void) { D0 = 0; }
static void g_bestmodeida(void) { D0 = 0x00029004; }


static void g_weightamatch(void) {
    /* WeighTAMatch(richiesto A0, candidato A1, tag A2): 32767 = uguale.
     * TextAttr: ta_YSize 4, ta_Style 6, ta_Flags 7. Pesa la differenza di
     * altezza e di stile: diskfont sceglie il candidato col peso maggiore */
    int dy = (int)R16(A0 + 4) - (int)R16(A1 + 4);
    if (dy < 0) dy = -dy;
    int peso = 32767 - dy * 1024;
    if ((R8(A0 + 6) ^ R8(A1 + 6)) & 0x0F) peso -= 256;          /* stile */
    if ((R8(A0 + 7) ^ R8(A1 + 7)) & 0x01) peso -= 64;           /* FPF_ROMFONT */
    D0 = (uint32_t)(peso < 0 ? 0 : peso);
}

/* ExtendFont(font A0, tag A1): la TextFontExtension (tfe_MatchWord $F00D 0,
 * tfe_BackPtr 4, tfe_OrigReplyPort 8, tfe_Tags 12, 24 byte) prende il posto
 * di mn_ReplyPort (tf_Extension, +14). 1 = fatto (anche se c'era gia') */
static void g_extendfont(void) {
    uint32_t tf = A0, e = R32(tf + 14);
    if (e && R16(e) == 0xF00D) { D0 = 1; return; }
    uint32_t n = os_alloca(24, MEMF_PUBLIC | MEMF_CLEAR);
    if (!n) { D0 = 0; return; }
    W16(n, 0xF00D);
    W32(n + 4, tf);
    W32(n + 8, e);
    W32(n + 12, A1);
    W32(tf + 14, n);
    D0 = 1;
}
static void g_stripfont(void) {
    uint32_t tf = A0, e = R32(tf + 14);
    if (!e || R16(e) != 0xF00D) return;
    W32(tf + 14, R32(e + 8));
    os_libera(e, 24);
}

/* ---------------------------------------------------------- tabella */

#define F(lvo, fn) [(lvo) / 6 - 1] = fn
static const os_funzione s_fn[N_GFX_V40] = {
    F(30, g_bltbitmap), F(36, g_blttemplate), F(54, g_textlength), F(60, g_text),
    F(66, g_setfont), F(72, g_openfont), F(78, g_closefont), F(84, g_asksoftstyle),
    F(90, g_setsoftstyle), F(180, g_drawellipse), F(192, g_loadrgb4), F(198, g_initrastport),
    F(204, g_initvport), F(210, g_niente), F(216, g_niente), F(222, g_niente), F(228, g_waitblit),
    F(234, g_setrast), F(240, g_move), F(246, g_draw), F(252, g_areamove), F(258, g_areadraw),
    F(264, g_areaend), F(270, g_waittof), F(282, g_initarea), F(288, g_setrgb4), F(300, g_bltclear),
    F(306, g_rectfill), F(318, g_readpixel), F(324, g_writepixel), F(336, g_polydraw),
    F(342, g_setapen), F(348, g_setbpen), F(354, g_setdrmd), F(360, g_initview), F(366, g_cbump),
    F(372, g_cmove), F(378, g_cwait), F(384, g_vbeampos), F(390, g_initbitmap),
    F(396, g_scrollraster), F(402, g_waittof), F(408, g_getsprite), F(414, g_freesprite),
    F(420, g_changesprite), F(426, g_movesprite), F(432, g_lockl), F(438, g_lockl),
    F(456, g_niente), F(462, g_niente), F(468, g_initmprast), F(474, g_askfont), F(480, g_addfont),
    F(486, g_remfont), F(492, g_allocraster), F(498, g_freeraster), F(540, g_freevportcoplists),
    F(546, g_niente), F(552, g_clipblit), F(564, g_niente), F(570, g_getcolormap),
    F(576, g_freecolormap), F(582, g_getrgb4), F(588, g_scrollvport), F(594, g_ucopperlistinit),
    F(606, g_bltbitmaprastport),
    /* V36-V40 */
    F(630, g_setrgb4cm), F(636, g_bltmaskbitmaprastport), F(804, g_weightamatch), F(816, g_extendfont), F(822, g_stripfont),
    F(852, g_setrgb32), F(882, g_loadrgb32), F(900, g_getrgb32), F(996, g_setrgb32cm),
    F(768, g_readpixelline8), F(774, g_writepixelline8), F(780, g_readpixelarray8),
    F(786, g_writepixelarray8), F(792, g_getvpmodeid), F(798, g_modenotavailable), F(810, g_eraserect),
    F(840, g_obtainbestpena), F(858, g_getapen), F(864, g_getbpen), F(870, g_getdrmd),
    F(876, g_getoutlinepen), F(888, g_setchiprev), F(894, g_setabpendrmd), F(918, g_allocbitmap),
    F(924, g_freebitmap), F(948, g_releasepen), F(954, g_obtainpen), F(960, g_getbitmapattr),
    F(978, g_setoutlinepen), F(984, g_setwritemask), F(990, g_setmaxpen), F(1008, g_findcolor),
    F(1050, g_bestmodeida), F(1056, g_writechunkypixels),
};

/* topaz 8 di sistema: un font 8x8 nostro (il disegno di topaz e' della ROM) */
#include "amiga_topaz.h"
static uint32_t crea_topaz(void) {
    uint32_t tf = os_alloca(52, MEMF_PUBLIC | MEMF_CLEAR);
    int n = 256 - 32 + 1;                                  /* 32..255 e il glifo "altro" */
    uint32_t dati = os_alloca((uint32_t)n * 8, MEMF_PUBLIC | MEMF_CLEAR);
    uint32_t loc = os_alloca((uint32_t)n * 4, MEMF_PUBLIC | MEMF_CLEAR);
    int mod = n;                                           /* un byte per carattere per riga */
    for (int c = 0; c < n; c++) {
        int ch = c + 32;
        for (int r = 0; r < 8; r++) W8(dati + (uint32_t)(r * mod + c), amiga_topaz_glifo(ch < 256 ? ch : 0x7F, r));
        W32(loc + 4u * c, (uint32_t)(c * 8) << 16 | 8);
    }
    W8(tf + LN_TYPE, 12);
    W32(tf + LN_NAME, os_stringa("topaz.font"));
    W16(tf + TF_YSIZE, 8); W8(tf + TF_FLAGS, 0x01 | 0x40);  /* ROMFONT, DESIGNED */
    W16(tf + TF_XSIZE, 8); W16(tf + TF_BASELINE, 6); W16(tf + TF_BOLDSMEAR, 1);
    W8(tf + TF_LOCHAR, 32); W8(tf + TF_HICHAR, 255);
    W32(tf + TF_CHARDATA, dati); W16(tf + TF_MODULO, mod); W32(tf + TF_CHARLOC, loc);
    return tf;
}

void os_grafica_init(void) {
    static const os_libreria_c def = { "graphics.library", 40, N_GFX_V40, 0x230, os_nomi_graphics, s_fn };
    os_gfxbase = os_crea_libreria(&def, NT_LIBRARY);
    os_lista_init(os_gfxbase + 140);                    /* TextFonts */
    W16(os_gfxbase + 206, 4);                           /* DisplayFlags: PAL */
    W16(os_gfxbase + 212, 312);                         /* MaxDisplayRow */
    W16(os_gfxbase + 214, 452);                         /* MaxDisplayColumn */
    W16(os_gfxbase + 216, 256);                         /* NormalDisplayRows */
    W16(os_gfxbase + 218, 640);                         /* NormalDisplayColumns */
    W16(os_gfxbase + 220, 1280);                        /* NormalDPMX */
    W16(os_gfxbase + 222, 1024);                        /* NormalDPMY */
    W16(os_gfxbase + 232, 64);                          /* MicrosPerLine */
    W16(os_gfxbase + 234, 113);                         /* MinDisplayColumn */
    s_topaz8 = crea_topaz();
    os_aggiungi_coda(os_gfxbase + 140, s_topaz8);
    W32(os_gfxbase + 154, s_topaz8);                    /* DefaultFont */
}

uint32_t os_topaz8(void) { return s_topaz8; }
