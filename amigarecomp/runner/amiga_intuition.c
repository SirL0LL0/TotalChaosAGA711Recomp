/* amiga_intuition.c - intuition.library (V34) in C.
 *
 * Schermi e finestre sono strutture vere in memoria emulata (Screen, Window,
 * RastPort, Layer, BitMap); i disegni vanno sulle bitmap con le primitive di
 * graphics (amiga_grafica.c). A ogni quadro:
 *   - gli ingressi del PC (mouse in coordinate del video, tasti) diventano
 *     messaggi IDCMP per la finestra attiva (o per quella sotto il mouse),
 *     gadget e menu compresi;
 *   - gli schermi si compongono nel video: dal piu' lontano al piu' vicino,
 *     ognuno dalla sua riga TopEdge in giu', con la palette della ColorMap e
 *     i cambi di colore della copper list utente; sopra, il puntatore.
 * Il video e' una griglia 640 x 512 (hires interlacciato): uno schermo lores
 * raddoppia i pixel in orizzontale, uno non interlacciato le righe.
 */
#include "amiga_os.h"
#include "amiga_grafica.h"
#include "genesis_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const char *const os_nomi_intuition[N_INTUI_V40];
static uint32_t s_ibase;

/* ---------------------------------------------------------- strutture */
/* Screen */
#define SC_NEXTSCREEN 0
#define SC_FIRSTWINDOW 4
#define SC_LEFTEDGE 8
#define SC_TOPEDGE 10
#define SC_WIDTH 12
#define SC_HEIGHT 14
#define SC_MOUSEY 16
#define SC_MOUSEX 18
#define SC_FLAGS 20
#define SC_TITLE 22
#define SC_DEFAULTTITLE 26
#define SC_BARHEIGHT 30
#define SC_FONT 40
#define SC_VIEWPORT 44
#define SC_RASTPORT 84
#define SC_BITMAP 184
#define SC_LAYERINFO 224
#define SC_FIRSTGADGET 326
#define SC_DETAILPEN 330
#define SC_BLOCKPEN 331
#define SC_SIZE 346
/* ViewPort (dentro lo Screen) */
#define VP_COLORMAP 4
#define VP_UCOPINS 20
#define VP_DWIDTH 24
#define VP_DHEIGHT 26
#define VP_DXOFFSET 28
#define VP_DYOFFSET 30
#define VP_MODES 32
#define VP_RASINFO 36
/* Window */
#define WD_NEXTWINDOW 0
#define WD_LEFTEDGE 4
#define WD_TOPEDGE 6
#define WD_WIDTH 8
#define WD_HEIGHT 10
#define WD_MOUSEY 12
#define WD_MOUSEX 14
#define WD_MINWIDTH 16
#define WD_FLAGS 24
#define WD_MENUSTRIP 28
#define WD_TITLE 32
#define WD_FIRSTREQUEST 36
#define WD_REQCOUNT 44
#define WD_WSCREEN 46
#define WD_RPORT 50
#define WD_BORDERLEFT 54
#define WD_BORDERTOP 55
#define WD_BORDERRIGHT 56
#define WD_BORDERBOTTOM 57
#define WD_BORDERRPORT 58
#define WD_FIRSTGADGET 62
#define WD_POINTER 74
#define WD_PTRHEIGHT 78
#define WD_PTRWIDTH 79
#define WD_XOFFSET 80
#define WD_YOFFSET 81
#define WD_IDCMPFLAGS 82
#define WD_USERPORT 86
#define WD_WINDOWPORT 90
#define WD_MESSAGEKEY 94
#define WD_DETAILPEN 98
#define WD_BLOCKPEN 99
#define WD_CHECKMARK 100
#define WD_SCREENTITLE 104
#define WD_GZZMOUSEX 108
#define WD_GZZMOUSEY 110
#define WD_GZZWIDTH 112
#define WD_GZZHEIGHT 114
#define WD_USERDATA 120
#define WD_WLAYER 124
#define WD_IFONT 128
#define WD_SIZE 132
/* IntuiMessage */
#define IM_CLASS 20
#define IM_CODE 24
#define IM_QUALIFIER 26
#define IM_IADDRESS 28
#define IM_MOUSEX 32
#define IM_MOUSEY 34
#define IM_SECONDS 36
#define IM_MICROS 40
#define IM_IDCMPWINDOW 44
#define IM_SIZE 52
/* Gadget */
#define GG_NEXT 0
#define GG_LEFT 4
#define GG_TOP 6
#define GG_WIDTH 8
#define GG_HEIGHT 10
#define GG_FLAGS 12
#define GG_ACTIVATION 14
#define GG_TYPE 16
#define GG_RENDER 18
#define GG_SELECTRENDER 22
#define GG_TEXT 26
#define GG_SPECIALINFO 34
#define GG_ID 38
/* Menu / MenuItem */
#define MU_NEXT 0
#define MU_LEFT 4
#define MU_TOP 6
#define MU_WIDTH 8
#define MU_HEIGHT 10
#define MU_FLAGS 12
#define MU_NAME 14
#define MU_FIRSTITEM 18
#define MI_NEXT 0
#define MI_LEFT 4
#define MI_TOP 6
#define MI_WIDTH 8
#define MI_HEIGHT 10
#define MI_FLAGS 12
#define MI_MUTUALEXCLUDE 14
#define MI_ITEMFILL 18
#define MI_SELECTFILL 22
#define MI_COMMAND 26
#define MI_SUBITEM 28
#define MI_NEXTSELECT 32

/* IDCMP */
#define NEWSIZE 0x2
#define REFRESHWINDOW 0x4
#define MOUSEBUTTONS 0x8
#define MOUSEMOVE 0x10
#define GADGETDOWN 0x20
#define GADGETUP 0x40
#define MENUPICK 0x100
#define CLOSEWINDOW 0x200
#define RAWKEY 0x400
#define ACTIVEWINDOW 0x40000
#define INACTIVEWINDOW 0x80000
#define DELTAMOVE 0x100000
#define VANILLAKEY 0x200000
#define INTUITICKS 0x400000
#define SELECTDOWN 0x68
#define SELECTUP 0xE8
#define MENUDOWN 0x69
#define MENUUP 0xE9
#define MENUNULL 0xFFFF
/* flag delle finestre */
#define WFLG_SIZEGADGET 0x1
#define WFLG_DRAGBAR 0x2
#define WFLG_DEPTHGADGET 0x4
#define WFLG_CLOSEGADGET 0x8
#define WFLG_BACKDROP 0x100
#define WFLG_REPORTMOUSE 0x200
#define WFLG_GIMMEZEROZERO 0x400
#define WFLG_BORDERLESS 0x800
#define WFLG_ACTIVATE 0x1000
#define WFLG_WINDOWACTIVE 0x2000
#define WFLG_MENUSTATE 0x8000
#define WFLG_RMBTRAP 0x10000
/* schermi */
#define SHOWTITLE 0x10
#define CUSTOMBITMAP 0x40
#define SCREENBEHIND 0x80
#define SCREENQUIET 0x100
#define HIRES 0x8000
#define LACE 0x4
#define EXTRA_HALFBRITE 0x80
#define HAM 0x800
/* gadget */
#define GFLG_GADGHCOMP 0
#define GFLG_GADGHBOX 1
#define GFLG_GADGHIMAGE 2
#define GFLG_GADGHNONE 3
#define GFLG_GADGIMAGE 4
#define GFLG_RELBOTTOM 8
#define GFLG_RELRIGHT 0x10
#define GFLG_RELWIDTH 0x20
#define GFLG_RELHEIGHT 0x40
#define GFLG_SELECTED 0x80
#define GFLG_DISABLED 0x100
#define GACT_RELVERIFY 1
#define GACT_IMMEDIATE 2
#define GACT_ENDGADGET 4
#define GACT_FOLLOWMOUSE 8
#define GACT_TOGGLESELECT 0x100
#define GTYP_BOOLGADGET 1
#define GTYP_PROPGADGET 3
#define GTYP_STRGADGET 4
#define GTYP_REQGADGET 0x1000

/* ---------------------------------------------------------- stato */

#define MAX_SCHERMI 16                    /* Total Chaos ne apre 10 */
#define MAX_FINESTRE 32
static uint32_t s_schermi[MAX_SCHERMI];        /* dal davanti al fondo */
static int s_n_schermi;

/* le penne di DrawInfo degli schermi aperti con OpenScreenTagList (SA_Pens):
 * BARDETAILPEN 9, BARBLOCKPEN 10, BARTRIMPEN 11 servono ai menu "nuovo
 * aspetto" (WFLG_NEWLOOKMENUS) */
#define N_PENNE 12
typedef struct { uint32_t sc; uint16_t p[N_PENNE]; } penne_t;
static penne_t s_penne[2 * MAX_SCHERMI];
static penne_t *penne_di(uint32_t sc, int nuovo) {
    for (int i = 0; i < 2 * MAX_SCHERMI; i++) if (s_penne[i].sc == sc) return &s_penne[i];
    if (!nuovo) return NULL;
    for (int i = 0; i < 2 * MAX_SCHERMI; i++) {
        int usato = 0;
        for (int k = 0; k < s_n_schermi; k++) usato |= s_schermi[k] == s_penne[i].sc;
        if (!s_penne[i].sc || !usato) { s_penne[i].sc = sc; return &s_penne[i]; }
    }
    return NULL;
}
static uint32_t s_finestre[MAX_FINESTRE];
static int s_n_finestre;
static uint32_t s_attiva;                      /* finestra attiva */

static int s_mx = 320, s_my = 256;             /* mouse nel video (640 x 512) */
static int s_sx, s_dx;                          /* tasti del mouse */
static uint16_t s_qualificatori;
static uint32_t s_gadget_attivo;               /* gadget premuto (o di testo attivo) */
static uint32_t s_gadget_finestra;
static int s_menu_aperto;
static uint32_t s_menu_finestra;
static int s_menu_sel, s_voce_sel, s_sotto_sel;
static uint32_t s_puntatore;                    /* SetPointer: dati sprite (0 = freccia) */
static int s_ptr_alto = 16, s_ptr_largo = 16, s_ptr_dx, s_ptr_dy;

static uint32_t screen_davanti(void) { return s_n_schermi ? s_schermi[0] : 0; }

static void aggiorna_ibase(void) {
    uint32_t primo = screen_davanti();
    W32(s_ibase + 60, primo);                                    /* FirstScreen */
    W32(s_ibase + 56, primo);                                    /* ActiveScreen */
    W32(s_ibase + 52, s_attiva);                                 /* ActiveWindow */
    for (int i = 0; i < s_n_schermi; i++)
        W32(s_schermi[i] + SC_NEXTSCREEN, i + 1 < s_n_schermi ? s_schermi[i + 1] : 0);
}

static int sch_hires(uint32_t sc) { return (R16(sc + SC_VIEWPORT + VP_MODES) & HIRES) != 0; }
static int sch_lace(uint32_t sc) { return (R16(sc + SC_VIEWPORT + VP_MODES) & LACE) != 0; }

/* le righe vere del video: 512 se lo schermo davanti e' interlacciato (o
 * DblPAL, che il gioco apre con LACE), 256 se le sue righe sono doppie */
int os_video_righe_vere(void) {
    uint32_t sc = screen_davanti();
    return sc && sch_lace(sc) ? 512 : 256;
}

/* mouse nelle coordinate di uno schermo */
static int mouse_x(uint32_t sc) { return (sch_hires(sc) ? s_mx : s_mx / 2) - S16(sc + SC_LEFTEDGE); }
static int mouse_y(uint32_t sc) { return (sch_lace(sc) ? s_my : s_my / 2) - S16(sc + SC_TOPEDGE); }

/* lo schermo sotto il mouse: il primo (dal davanti) la cui riga TopEdge e' sopra */
static uint32_t schermo_sotto_mouse(void) {
    for (int i = 0; i < s_n_schermi; i++) {
        uint32_t sc = s_schermi[i];
        int top = S16(sc + SC_TOPEDGE) * (sch_lace(sc) ? 1 : 2);
        if (s_my >= top) return sc;
    }
    return 0;
}

/* ---------------------------------------------------------- disegno: immagini, testi, bordi */

/* DrawImage: Image = LeftEdge 0, TopEdge 2, Width 4, Height 6, Depth 8,
 * ImageData 10, PlanePick 14, PlaneOnOff 15, NextImage 16 */
static void disegna_immagine(uint32_t rp, uint32_t im, int x, int y) {
    for (; im; im = R32(im + 16)) {
        int ix = x + S16(im), iy = y + S16(im + 2);
        int w = R16(im + 4), h = R16(im + 6), d = R16(im + 8);
        uint8_t pick = R8(im + 14), onoff = R8(im + 15);
        uint32_t dati = R32(im + 10);
        uint32_t bm = R32(rp + 4);
        uint32_t ly = R32(rp + 0);
        if (!bm) continue;
        int ox = 0, oy = 0, cx0 = 0, cy0 = 0, cx1 = -1, cy1 = -1;
        os_rett_t cop[OS_MAX_COPERTURE];
        int n_cop = 0;
        if (ly) {
            ox = S16(ly + 16); oy = S16(ly + 18);
            cx0 = ox; cy0 = oy; cx1 = S16(ly + 20); cy1 = S16(ly + 22);
            n_cop = os_strato_coperture(ly, cop, OS_MAX_COPERTURE);
        }
        int bpr = R16(bm), righe = R16(bm + 2), prof = R8(bm + 5);
        if (cx1 < 0) { cx1 = bpr * 8 - 1; cy1 = righe - 1; }
        int parole = (w + 15) / 16;
        uint8_t maschera = R8(rp + 24);
        int piano_img = 0;
        for (int p = 0; p < prof && p < 8; p++) {
            uint32_t dp = R32(bm + 8 + 4 * p);
            if (!dp || !(maschera >> p & 1)) { if (pick >> p & 1) piano_img++; continue; }
            int usa = (pick >> p & 1) && piano_img < d && dati;
            uint32_t sorg = usa ? dati + (uint32_t)(piano_img * h * parole * 2) : 0;
            if (pick >> p & 1) piano_img++;
            for (int r = 0; r < h; r++) {
                int Y = iy + oy + r;
                if (Y < cy0 || Y > cy1) continue;
                for (int b = 0; b < w; b++) {
                    int X = ix + ox + b;
                    if (X < cx0 || X > cx1) continue;
                    int on = usa ? R8(sorg + (uint32_t)(r * parole * 2 + (b >> 3))) >> (7 - (b & 7)) & 1
                                 : (onoff >> p & 1);
                    if (n_cop && os_coperto(cop, n_cop, X, Y)) {          /* nel backing store */
                        uint8_t *o = os_strato_ombra(ly, X, Y);
                        if (o) { if (on) *o |= (uint8_t)(1u << p); else *o &= (uint8_t)~(1u << p); }
                        continue;
                    }
                    uint8_t *q = g_amiga_mem + dp + (uint32_t)Y * (uint32_t)bpr + (uint32_t)(X >> 3);
                    os_guardia_c((uint32_t)(q - g_amiga_mem), "DrawImage");
                    uint8_t m = (uint8_t)(0x80 >> (X & 7));
                    if (on) *q |= m; else *q &= (uint8_t)~m;
                }
            }
        }
    }
}

/* IntuiText: FrontPen 0, BackPen 1, DrawMode 2, LeftEdge 4, TopEdge 6,
 * ITextFont 8, IText 12, NextText 16 */
static void stampa_itext(uint32_t rp, uint32_t it, int x, int y) {
    uint8_t fg = R8(rp + 25), bg = R8(rp + 26), dm = R8(rp + 28);
    uint32_t font = R32(rp + 52);
    for (; it; it = R32(it + 16)) {
        W8(rp + 25, R8(it)); W8(rp + 26, R8(it + 1)); W8(rp + 28, R8(it + 2));
        uint32_t ta = R32(it + 8);
        uint32_t tf = ta ? os_apri_font(ta) : 0;
        if (tf) { W32(rp + 52, tf); W16(rp + 58, R16(tf + 20)); W16(rp + 62, R16(tf + 26)); }
        uint32_t s = R32(it + 12);
        int n = 0;
        while (R8(s + (uint32_t)n)) n++;
        uint32_t f = R32(rp + 52);
        W16(rp + 36, x + S16(it + 4));
        W16(rp + 38, y + S16(it + 6) + (f ? S16(f + 26) : 6));
        os_testo(rp, s, n);
        if (tf) { W32(rp + 52, font); if (font) { W16(rp + 58, R16(font + 20)); W16(rp + 62, R16(font + 26)); } }
    }
    W8(rp + 25, fg); W8(rp + 26, bg); W8(rp + 28, dm);
}

/* Border: LeftEdge 0, TopEdge 2, FrontPen 4, BackPen 5, DrawMode 6, Count 7, XY 8, NextBorder 12 */
static void disegna_bordo(uint32_t rp, uint32_t b, int x, int y) {
    uint8_t fg = R8(rp + 25), dm = R8(rp + 28);
    for (; b; b = R32(b + 12)) {
        W8(rp + 25, R8(b + 4)); W8(rp + 28, R8(b + 6));
        int n = R8(b + 7);
        uint32_t xy = R32(b + 8);
        int bx = x + S16(b), by = y + S16(b + 2);
        for (int i = 0; i + 1 < n; i++)
            os_linea(rp, bx + S16(xy + 4u * i), by + S16(xy + 4u * i + 2),
                     bx + S16(xy + 4u * i + 4), by + S16(xy + 4u * i + 6));
        if (n == 1) os_linea(rp, bx + S16(xy), by + S16(xy + 2), bx + S16(xy), by + S16(xy + 2));
    }
    W8(rp + 25, fg); W8(rp + 28, dm);
}

static void i_drawimage(void) { disegna_immagine(A0, A1, (int16_t)D0, (int16_t)D1); }
static void i_printitext(void) { stampa_itext(A0, A1, (int16_t)D0, (int16_t)D1); }
static void i_drawborder(void) { disegna_bordo(A0, A1, (int16_t)D0, (int16_t)D1); }
static void i_intuitextlength(void) {
    uint32_t it = A0, ta = R32(it + 8);
    uint32_t tf = ta ? os_apri_font(ta) : os_topaz8();
    uint32_t s = R32(it + 12);
    int n = 0;
    while (R8(s + (uint32_t)n)) n++;
    /* una RastPort finta col font del testo */
    static uint32_t rp;
    if (!rp) rp = os_alloca(100, MEMF_PUBLIC | MEMF_CLEAR);
    os_init_rastport(rp);
    if (tf) W32(rp + 52, tf);
    D0 = (uint32_t)os_lunghezza_testo(rp, s, n);
}

/* ---------------------------------------------------------- schermi */

static void imposta_palette_base(uint32_t cm, int n) {
    static const uint16_t base[32] = {
        0x0AAA, 0x0000, 0x0FFF, 0x068B, 0x0F00, 0x00F0, 0x000F, 0x0FF0,
        0x0F0F, 0x00FF, 0x0888, 0x0444, 0x0F80, 0x08F0, 0x080F, 0x0FFF,
        0x0000, 0x0E44, 0x0000, 0x0EEC, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 };
    for (int i = 0; i < n && i < 32; i++) {
        uint32_t c = base[i];
        os_cm_colore24(cm, (uint32_t)i, ((c >> 8 & 15) * 17) << 16 | ((c >> 4 & 15) * 17) << 8 | (c & 15) * 17);
    }
}

/* il TextAttr del font di sistema, topaz 8 (in memoria emulata, uno solo) */
static uint32_t ta_topaz(void) {
    static uint32_t ta;
    if (!ta) {
        ta = os_alloca(8, MEMF_PUBLIC | MEMF_CLEAR);
        W32(ta, os_stringa("topaz.font"));
        W16(ta + 4, 8);
        W8(ta + 7, 1);                                               /* FPF_ROMFONT */
    }
    return ta;
}

static int s_ns_interleaved;             /* OpenScreenTagList con SA_Interleaved */

static void i_openscreen(void) {
    /* NewScreen: LeftEdge 0, TopEdge 2, Width 4, Height 6, Depth 8, DetailPen 10,
     * BlockPen 11, ViewModes 12, Type 14, Font 16, DefaultTitle 20, Gadgets 24,
     * CustomBitMap 28 */
    uint32_t ns = A0;
    int w = S16(ns + 4), h = S16(ns + 6), d = S16(ns + 8);
    uint16_t modi = R16(ns + 12), tipo = R16(ns + 14);
    if (w <= 0) w = (modi & HIRES) ? 640 : 320;
    if (h <= 0) h = (modi & LACE) ? 512 : 256;
    uint32_t sc = os_alloca(SC_SIZE, MEMF_PUBLIC | MEMF_CLEAR);
    W16(sc + SC_LEFTEDGE, S16(ns)); W16(sc + SC_TOPEDGE, S16(ns + 2));
    W16(sc + SC_WIDTH, w); W16(sc + SC_HEIGHT, h);
    W16(sc + SC_FLAGS, tipo);
    W32(sc + SC_TITLE, R32(ns + 20)); W32(sc + SC_DEFAULTTITLE, R32(ns + 20));
    W8(sc + SC_DETAILPEN, R8(ns + 10)); W8(sc + SC_BLOCKPEN, R8(ns + 11));
    W8(sc + SC_BARHEIGHT, 10);
    W8(sc + 35, 11); W8(sc + 36, 4); W8(sc + 37, 4); W8(sc + 38, 2);   /* WBor* */
    /* Font: quello di NewScreen o, se manca, quello predefinito (topaz 8), come
     * Intuition: req.library ne legge ta_YSize per dimensionare i pulsanti */
    W32(sc + SC_FONT, R32(ns + 16) ? R32(ns + 16) : ta_topaz());
    /* bitmap: quella del programma (CUSTOMBITMAP: come Intuition se ne copia
     * la struttura, ma RastPort e RasInfo puntano alla sua, che il programma
     * puo' riempire o cambiare dopo) o una nuova */
    uint32_t bm = sc + SC_BITMAP, bm_vera = bm;
    if ((tipo & CUSTOMBITMAP) && R32(ns + 28)) {
        bm_vera = R32(ns + 28);
        memcpy(g_amiga_mem + bm, g_amiga_mem + bm_vera, 40);
    } else if (s_ns_interleaved) {
        /* SA_Interleaved: le righe dei piani una dopo l'altra, in un blocco
         * solo (BytesPerRow = riga * profondita') */
        uint32_t riga = (uint32_t)(((w + 15) >> 4) << 1);
        uint32_t blocco = os_alloca(riga * (uint32_t)d * (uint32_t)h, MEMF_CHIP | MEMF_CLEAR);
        if (!blocco) { D0 = 0; return; }
        W16(bm, riga * (uint32_t)d); W16(bm + 2, h); W8(bm + 5, d); W8(bm + 4, 1);   /* Flags: BMF_INTERLEAVED */
        for (int p = 0; p < d; p++) W32(bm + 8 + 4u * p, blocco + riga * (uint32_t)p);
    } else {
        W16(bm, ((w + 15) >> 4) << 1); W16(bm + 2, h); W8(bm + 5, d);
        for (int p = 0; p < d; p++) {
            uint32_t pl = os_alloca((uint32_t)(((w + 15) >> 4) << 1) * (uint32_t)h, MEMF_CHIP | MEMF_CLEAR);
            if (!pl) { D0 = 0; return; }
            W32(bm + 8 + 4u * p, pl);
        }
    }
    /* ViewPort con la ColorMap: 32 colori, o 2^profondita' fino a 256 (AGA) */
    uint32_t vp = sc + SC_VIEWPORT;
    uint32_t cm = os_colormap(d > 5 ? 1u << (d > 8 ? 8 : d) : 32);
    imposta_palette_base(cm, 32);
    W32(vp + VP_COLORMAP, cm);
    W16(vp + VP_DWIDTH, w); W16(vp + VP_DHEIGHT, h);
    W16(vp + VP_MODES, modi);
    /* RasInfo: Next 0, BitMap 4, RxOffset 8, RyOffset 10 (il video legge da qui) */
    uint32_t ri = os_alloca(12, MEMF_PUBLIC | MEMF_CLEAR);
    W32(ri + 4, bm_vera);
    W32(vp + VP_RASINFO, ri);
    /* RastPort dello schermo */
    uint32_t rp = sc + SC_RASTPORT;
    os_init_rastport(rp);
    W32(rp + 4, bm_vera);
    if (R32(ns + 16)) {
        uint32_t tf = os_apri_font(R32(ns + 16));
        if (tf) { W32(rp + 52, tf); W16(rp + 58, R16(tf + 20)); W16(rp + 62, R16(tf + 26)); }
    }
    /* davanti a tutti, o dietro se SCREENBEHIND */
    if (s_n_schermi < MAX_SCHERMI) {
        if (tipo & SCREENBEHIND) s_schermi[s_n_schermi++] = sc;
        else {
            memmove(s_schermi + 1, s_schermi, sizeof s_schermi[0] * (size_t)s_n_schermi);
            s_schermi[0] = sc;
            s_n_schermi++;
        }
    }
    aggiorna_ibase();
    if (os_traccia) fprintf(stderr, "[intui] OpenScreen %dx%dx%d modi $%04X tipo $%04X = $%06X\n", w, h, d, modi, tipo, sc);
    D0 = sc;
}

static void togli_schermo(uint32_t sc) {
    for (int i = 0; i < s_n_schermi; i++)
        if (s_schermi[i] == sc) {
            memmove(s_schermi + i, s_schermi + i + 1, sizeof s_schermi[0] * (size_t)(s_n_schermi - i - 1));
            s_n_schermi--;
            break;
        }
}

static void i_closescreen(void) {
    uint32_t sc = A0;
    togli_schermo(sc);
    if (!(R16(sc + SC_FLAGS) & CUSTOMBITMAP)) {
        uint32_t bm = sc + SC_BITMAP;
        if (R8(bm + 4) & 1) os_libera(R32(bm + 8), (uint32_t)R16(bm) * R16(bm + 2));   /* interleaved */
        else for (int p = 0; p < R8(bm + 5); p++) os_libera(R32(bm + 8 + 4u * p), (uint32_t)R16(bm) * R16(bm + 2));
    }
    aggiorna_ibase();
    D0 = 1;
}

static int e_schermo(uint32_t sc) {
    for (int i = 0; i < s_n_schermi; i++) if (s_schermi[i] == sc) return 1;
    return 0;
}
static void i_screentofront(void) {
    if (!e_schermo(A0)) return;                 /* non e' uno schermo aperto */
    togli_schermo(A0);
    memmove(s_schermi + 1, s_schermi, sizeof s_schermi[0] * (size_t)s_n_schermi);
    s_schermi[0] = A0; s_n_schermi++;
    aggiorna_ibase();
}
static void i_screentoback(void) {
    if (!e_schermo(A0)) return;
    togli_schermo(A0);
    s_schermi[s_n_schermi++] = A0;
    aggiorna_ibase();
}
static void i_movescreen(void) {
    /* MoveScreen(screen A0, dx D0, dy D1): in verticale soltanto. Sull'Amiga
     * ogni spostamento rifa' la copper list (RethinkDisplay) e si vede dal
     * quadro dopo: chi scorre uno schermo con un ciclo di MoveScreen (i titoli
     * di Chaos salgono dal basso, 2 righe a passo) va un passo per quadro.
     * Qui si aspetta il quadro dopo lo spostamento, come quella sincronia */
    int y = S16(A0 + SC_TOPEDGE) + (int16_t)D1;
    if (y < 0) y = 0;
    W16(A0 + SC_TOPEDGE, y);
    if ((int16_t)D1) {
        extern void os_task_attendi_quadro(void);
        M68KState s = g_cpu;
        os_task_attendi_quadro();
        g_cpu = s;
    }
}
static void i_showtitle(void) {}
static void i_niente(void) {}
static void i_viewaddress(void) { D0 = s_ibase + 34; }
static void i_viewportaddress(void) { D0 = R32(A0 + WD_WSCREEN) + SC_VIEWPORT; }

/* ---------------------------------------------------------- finestre */

static void disegna_cornice(uint32_t w);

/* ---- i layer: come layers.library, in piccolo.
 * La lista dello schermo (SC_FIRSTWINDOW) e' l'ordine di profondita': la
 * testa sta davanti; le finestre BACKDROP stanno dietro alle altre. Una
 * finestra SMART_REFRESH ha un backing store (un byte per pixel, il colore):
 * quel che disegna dove e' coperta va li', e torna sullo schermo quando chi la
 * copriva si chiude. Una SIMPLE_REFRESH non ce l'ha: quando torna scoperta la
 * parte si pulisce e lei riceve REFRESHWINDOW. Una finestra nuova parte pulita. */
#define WFLG_SIMPLE_REFRESH 0x40
#define WFLG_SUPER_BITMAP 0x80
#define WFLG_BACKDROP 0x100

static void manda(uint32_t w, uint32_t classe, uint16_t codice, uint32_t iaddr);

typedef struct { uint32_t w; int x, y, lar, alt; uint8_t *pix; } ombra_t;
static ombra_t s_ombre[MAX_FINESTRE];
static int s_n_ombre;

static ombra_t *ombra_di(uint32_t w) {
    for (int i = 0; i < s_n_ombre; i++) if (s_ombre[i].w == w) return &s_ombre[i];
    return NULL;
}

/* le finestre dello schermo dalla piu' davanti; quante */
static int finestre_schermo(uint32_t sc, uint32_t *v, int max) {
    int n = 0;
    for (uint32_t w = R32(sc + SC_FIRSTWINDOW); w && n < max; w = R32(w + WD_NEXTWINDOW)) v[n++] = w;
    return n;
}

static int dentro_finestra(uint32_t w, int x, int y) {
    int wx = S16(w + WD_LEFTEDGE), wy = S16(w + WD_TOPEDGE);
    return x >= wx && y >= wy && x < wx + S16(w + WD_WIDTH) && y < wy + S16(w + WD_HEIGHT);
}

int os_strato_coperture(uint32_t ly, os_rett_t *r, int max) {
    uint32_t w = R32(ly + 40);
    if (!w) return 0;
    uint32_t sc = R32(w + WD_WSCREEN), v[MAX_FINESTRE];
    int n = finestre_schermo(sc, v, MAX_FINESTRE), k = 0;
    int x0 = S16(w + WD_LEFTEDGE), y0 = S16(w + WD_TOPEDGE);
    int x1 = x0 + S16(w + WD_WIDTH) - 1, y1 = y0 + S16(w + WD_HEIGHT) - 1;
    for (int i = 0; i < n && v[i] != w && k < max; i++) {
        int a0 = S16(v[i] + WD_LEFTEDGE), b0 = S16(v[i] + WD_TOPEDGE);
        int a1 = a0 + S16(v[i] + WD_WIDTH) - 1, b1 = b0 + S16(v[i] + WD_HEIGHT) - 1;
        if (a1 < x0 || a0 > x1 || b1 < y0 || b0 > y1) continue;
        r[k++] = (os_rett_t){ a0, b0, a1, b1 };
    }
    return k;
}

uint8_t *os_strato_ombra(uint32_t ly, int x, int y) {
    ombra_t *o = ombra_di(R32(ly + 40));
    if (!o || !o->pix) return NULL;
    x -= o->x; y -= o->y;
    if (x < 0 || y < 0 || x >= o->lar || y >= o->alt) return NULL;
    return &o->pix[(size_t)y * (size_t)o->lar + (size_t)x];
}

/* un pixel della bitmap dello schermo, tutti i piani */
static int pixel_bm(uint32_t bm, int x, int y) {
    int bpr = R16(bm), prof = R8(bm + 5), v = 0;
    if (x < 0 || y < 0 || x >= bpr * 8 || y >= R16(bm + 2)) return 0;
    uint32_t off = (uint32_t)y * (uint32_t)bpr + (uint32_t)(x >> 3);
    for (int p = 0; p < prof && p < 8; p++) {
        uint32_t pl = R32(bm + 8 + 4u * p);
        if (pl && (g_amiga_mem[pl + off] & (0x80 >> (x & 7)))) v |= 1 << p;
    }
    return v;
}
static void scrivi_pixel_bm(uint32_t bm, int x, int y, int col) {
    int bpr = R16(bm), prof = R8(bm + 5);
    if (x < 0 || y < 0 || x >= bpr * 8 || y >= R16(bm + 2)) return;
    uint32_t off = (uint32_t)y * (uint32_t)bpr + (uint32_t)(x >> 3);
    uint8_t m = (uint8_t)(0x80 >> (x & 7));
    for (int p = 0; p < prof && p < 8; p++) {
        uint32_t pl = R32(bm + 8 + 4u * p);
        if (!pl) continue;
        os_guardia_c(pl + off, "layer");
        if (col >> p & 1) g_amiga_mem[pl + off] |= m; else g_amiga_mem[pl + off] &= (uint8_t)~m;
    }
}

/* la finestra piu' davanti che contiene il punto, fra v[0..n); -1 = nessuna */
static int davanti_a(const uint32_t *v, int n, int x, int y) {
    for (int i = 0; i < n; i++) if (dentro_finestra(v[i], x, y)) return i;
    return -1;
}

/* la finestra nuova w (gia' nella lista) copre le altre: quel che si vedeva
 * delle SMART_REFRESH va nel loro backing store; la sua parte visibile si pulisce */
static void strati_apri(uint32_t w) {
    uint32_t sc = R32(w + WD_WSCREEN), bm = R32(sc + SC_RASTPORT + 4), v[MAX_FINESTRE];
    int n = finestre_schermo(sc, v, MAX_FINESTRE), iw = 0;
    while (iw < n && v[iw] != w) iw++;
    if (!bm) return;
    int x0 = S16(w + WD_LEFTEDGE), y0 = S16(w + WD_TOPEDGE);
    for (int y = y0; y < y0 + S16(w + WD_HEIGHT); y++)
        for (int x = x0; x < x0 + S16(w + WD_WIDTH); x++) {
            int f = davanti_a(v, n, x, y);
            if (f != iw) continue;                      /* coperta da una finestra davanti */
            int g = -1;                                 /* chi si vedeva prima */
            for (int i = iw + 1; i < n && g < 0; i++) if (dentro_finestra(v[i], x, y)) g = i;
            if (g >= 0) {
                ombra_t *o = ombra_di(v[g]);
                if (o && o->pix) o->pix[(size_t)(y - o->y) * (size_t)o->lar + (size_t)(x - o->x)] = (uint8_t)pixel_bm(bm, x, y);
            }
            scrivi_pixel_bm(bm, x, y, 0);
        }
}

/* la finestra w esce dalla lista (v, n: la lista di prima, iw il suo posto):
 * dove si vedeva, torna chi le stava dietro */
static void strati_chiudi(uint32_t w, const uint32_t *v, int n, int iw) {
    uint32_t sc = R32(w + WD_WSCREEN), bm = R32(sc + SC_RASTPORT + 4);
    if (!bm) return;
    uint32_t da_rinfrescare[MAX_FINESTRE];
    int n_r = 0;
    int x0 = S16(w + WD_LEFTEDGE), y0 = S16(w + WD_TOPEDGE);
    for (int y = y0; y < y0 + S16(w + WD_HEIGHT); y++)
        for (int x = x0; x < x0 + S16(w + WD_WIDTH); x++) {
            if (davanti_a(v, n, x, y) != iw) continue;  /* w non si vedeva qui */
            int g = -1;
            for (int i = iw + 1; i < n && g < 0; i++) if (dentro_finestra(v[i], x, y)) g = i;
            int col = 0;
            if (g >= 0) {
                ombra_t *o = ombra_di(v[g]);
                if (o && o->pix) col = o->pix[(size_t)(y - o->y) * (size_t)o->lar + (size_t)(x - o->x)];
                else {
                    int gia = 0;
                    for (int k = 0; k < n_r; k++) gia |= da_rinfrescare[k] == v[g];
                    if (!gia && n_r < MAX_FINESTRE) da_rinfrescare[n_r++] = v[g];
                }
            }
            scrivi_pixel_bm(bm, x, y, col);
        }
    for (int k = 0; k < n_r; k++) manda(da_rinfrescare[k], REFRESHWINDOW, 0, 0);
}

static void i_openwindow(void) {
    /* NewWindow: LeftEdge 0, TopEdge 2, Width 4, Height 6, DetailPen 8, BlockPen 9,
     * IDCMPFlags 10, Flags 14, FirstGadget 18, CheckMark 22, Title 26, Screen 30,
     * BitMap 34, MinWidth 38, MinHeight 40, MaxWidth 42, MaxHeight 44, Type 46 */
    uint32_t nw = A0;
    uint32_t sc = R32(nw + 30);
    if (!sc) sc = screen_davanti();
    if (!sc) { D0 = 0; return; }
    uint32_t w = os_alloca(WD_SIZE, MEMF_PUBLIC | MEMF_CLEAR);
    int x = S16(nw), y = S16(nw + 2), lar = S16(nw + 4), alt = S16(nw + 6);
    if (lar <= 0) lar = S16(sc + SC_WIDTH) - x;
    if (alt <= 0) alt = S16(sc + SC_HEIGHT) - y;
    uint32_t flag = R32(nw + 14);
    W16(w + WD_LEFTEDGE, x); W16(w + WD_TOPEDGE, y);
    W16(w + WD_WIDTH, lar); W16(w + WD_HEIGHT, alt);
    W16(w + WD_MINWIDTH, S16(nw + 38)); W16(w + WD_MINWIDTH + 2, S16(nw + 40));
    W16(w + WD_MINWIDTH + 4, S16(nw + 42)); W16(w + WD_MINWIDTH + 6, S16(nw + 44));
    W32(w + WD_FLAGS, flag);
    W32(w + WD_TITLE, R32(nw + 26));
    W32(w + WD_WSCREEN, sc);
    W8(w + WD_DETAILPEN, R8(nw + 8)); W8(w + WD_BLOCKPEN, R8(nw + 9));
    W32(w + WD_FIRSTGADGET, R32(nw + 18));
    W32(w + WD_CHECKMARK, R32(nw + 22));
    if (!(flag & WFLG_BORDERLESS)) {
        W8(w + WD_BORDERLEFT, 4); W8(w + WD_BORDERRIGHT, 4);
        W8(w + WD_BORDERTOP, R32(nw + 26) || (flag & (WFLG_DRAGBAR | WFLG_CLOSEGADGET | WFLG_DEPTHGADGET)) ? 11 : 2);
        W8(w + WD_BORDERBOTTOM, 2);
    }
    /* la RastPort con il suo Layer (i limiti della finestra sullo schermo) */
    uint32_t ly = os_alloca(160, MEMF_PUBLIC | MEMF_CLEAR);
    W16(ly + 16, x); W16(ly + 18, y); W16(ly + 20, x + lar - 1); W16(ly + 22, y + alt - 1);
    W32(ly + 40, w);
    uint32_t rp = os_alloca(100, MEMF_PUBLIC | MEMF_CLEAR);
    os_init_rastport(rp);
    W32(rp + 0, ly);
    W32(rp + 4, R32(sc + SC_RASTPORT + 4));                     /* la bitmap dello schermo */
    uint32_t tf = R32(sc + SC_RASTPORT + 52);
    if (tf) { W32(rp + 52, tf); W16(rp + 58, R16(tf + 20)); W16(rp + 62, R16(tf + 26)); }
    W32(ly + 12, rp);
    W32(w + WD_RPORT, rp);
    W32(w + WD_BORDERRPORT, rp);
    W32(w + WD_WLAYER, ly);
    W32(w + WD_IFONT, tf);
    W16(w + WD_GZZWIDTH, lar - R8(w + WD_BORDERLEFT) - R8(w + WD_BORDERRIGHT));
    W16(w + WD_GZZHEIGHT, alt - R8(w + WD_BORDERTOP) - R8(w + WD_BORDERBOTTOM));
    /* le porte: UserPort del programma (se ha IDCMP), WindowPort per le risposte */
    uint32_t idcmp = R32(nw + 10);
    W32(w + WD_IDCMPFLAGS, idcmp);
    if (idcmp) W32(w + WD_USERPORT, os_crea_porta());
    uint32_t wp = os_alloca(MP_SIZE, MEMF_PUBLIC | MEMF_CLEAR);
    W8(wp + LN_TYPE, NT_MSGPORT);
    W8(wp + MP_FLAGS, 2);                                        /* PA_IGNORE */
    os_lista_init(wp + MP_MSGLIST);
    W32(w + WD_WINDOWPORT, wp);
    /* nella lista dello schermo: davanti a tutte, o, se BACKDROP, davanti
     * alle altre BACKDROP ma dietro alle finestre normali */
    {
        uint32_t q = sc + SC_FIRSTWINDOW;
        if (flag & WFLG_BACKDROP)
            while (R32(q) && !(R32(R32(q) + WD_FLAGS) & WFLG_BACKDROP)) q = R32(q) + WD_NEXTWINDOW;
        W32(w + WD_NEXTWINDOW, R32(q));
        W32(q, w);
    }
    if (s_n_finestre < MAX_FINESTRE) s_finestre[s_n_finestre++] = w;
    /* il backing store di una SMART_REFRESH, poi copre chi sta dietro */
    if (!(flag & (WFLG_SIMPLE_REFRESH | WFLG_SUPER_BITMAP)) && s_n_ombre < MAX_FINESTRE && lar > 0 && alt > 0) {
        ombra_t *o = &s_ombre[s_n_ombre++];
        *o = (ombra_t){ w, x, y, lar, alt, calloc((size_t)lar * (size_t)alt, 1) };
    }
    strati_apri(w);
    if ((flag & WFLG_ACTIVATE) || !s_attiva) {
        uint32_t vecchia = s_attiva;
        if (vecchia) W32(vecchia + WD_FLAGS, R32(vecchia + WD_FLAGS) & ~WFLG_WINDOWACTIVE);
        s_attiva = w;
        W32(w + WD_FLAGS, R32(w + WD_FLAGS) | WFLG_WINDOWACTIVE);
        if (vecchia) disegna_cornice(vecchia);                       /* la sua barra ora e' spenta */
    }
    aggiorna_ibase();
    disegna_cornice(w);
    /* i gadget della finestra */
    {
        M68KState s = g_cpu;
        extern void os_disegna_gadget(uint32_t w, uint32_t g, int n);
        os_disegna_gadget(w, R32(w + WD_FIRSTGADGET), -1);
        g_cpu = s;
    }
    if (os_traccia) fprintf(stderr, "[intui] OpenWindow %d,%d %dx%d flag $%08X idcmp $%08X = $%06X\n",
                            x, y, lar, alt, flag, idcmp, w);
    D0 = w;
}

static void i_closewindow(void) {
    uint32_t w = A0, sc = R32(w + WD_WSCREEN);
    /* fuori dalla lista dello schermo; dove si vedeva torna chi stava dietro */
    uint32_t prima[MAX_FINESTRE];
    int n_prima = finestre_schermo(sc, prima, MAX_FINESTRE), iw = 0;
    while (iw < n_prima && prima[iw] != w) iw++;
    for (uint32_t q = sc + SC_FIRSTWINDOW, n = R32(q); n; q = n + WD_NEXTWINDOW, n = R32(q))
        if (n == w) { W32(q, R32(w + WD_NEXTWINDOW)); break; }
    if (iw < n_prima) strati_chiudi(w, prima, n_prima, iw);
    for (int i = 0; i < s_n_ombre; i++)
        if (s_ombre[i].w == w) { free(s_ombre[i].pix); s_ombre[i] = s_ombre[--s_n_ombre]; break; }
    for (int i = 0; i < s_n_finestre; i++)
        if (s_finestre[i] == w) { s_finestre[i] = s_finestre[--s_n_finestre]; break; }
    if (s_attiva == w) s_attiva = s_n_finestre ? s_finestre[s_n_finestre - 1] : 0;
    if (s_gadget_finestra == w) { s_gadget_attivo = 0; s_gadget_finestra = 0; }
    if (s_menu_finestra == w) { s_menu_aperto = 0; s_menu_finestra = 0; }
    /* i messaggi non ancora presi spariscono con la porta */
    uint32_t up = R32(w + WD_USERPORT);
    if (up) {
        uint32_t m;
        while ((m = os_prendi_msg(up)) != 0) os_libera(m, IM_SIZE);
        uint32_t alloc = R32(os_task + TC_SIGALLOC);
        W32(os_task + TC_SIGALLOC, alloc & ~(1u << R8(up + MP_SIGBIT)));
    }
    aggiorna_ibase();
}

static void i_modifyidcmp(void) {
    uint32_t w = A0;
    W32(w + WD_IDCMPFLAGS, D0);
    if (D0 && !R32(w + WD_USERPORT)) W32(w + WD_USERPORT, os_crea_porta());
}
static void i_activatewindow(void) {
    uint32_t vecchia = s_attiva;
    if (vecchia == A0) return;
    if (vecchia) W32(vecchia + WD_FLAGS, R32(vecchia + WD_FLAGS) & ~WFLG_WINDOWACTIVE);
    s_attiva = A0;
    W32(A0 + WD_FLAGS, R32(A0 + WD_FLAGS) | WFLG_WINDOWACTIVE);
    aggiorna_ibase();
    M68KState s = g_cpu;
    if (vecchia) disegna_cornice(vecchia);
    disegna_cornice(s_attiva);
    g_cpu = s;
}
static void i_movewindow(void) {
    uint32_t w = A0, ly = R32(w + WD_WLAYER);
    int dx = (int16_t)D0, dy = (int16_t)D1;
    W16(w + WD_LEFTEDGE, S16(w + WD_LEFTEDGE) + dx); W16(w + WD_TOPEDGE, S16(w + WD_TOPEDGE) + dy);
    W16(ly + 16, S16(ly + 16) + dx); W16(ly + 18, S16(ly + 18) + dy);
    W16(ly + 20, S16(ly + 20) + dx); W16(ly + 22, S16(ly + 22) + dy);
}
static void i_sizewindow(void) {
    uint32_t w = A0, ly = R32(w + WD_WLAYER);
    W16(w + WD_WIDTH, S16(w + WD_WIDTH) + (int16_t)D0); W16(w + WD_HEIGHT, S16(w + WD_HEIGHT) + (int16_t)D1);
    W16(ly + 20, S16(ly + 20) + (int16_t)D0); W16(ly + 22, S16(ly + 22) + (int16_t)D1);
}
static void i_setwindowtitles(void) {
    if (A1 != 0xFFFFFFFFu) W32(A0 + WD_TITLE, A1);
    if (A2 != 0xFFFFFFFFu) W32(A0 + WD_SCREENTITLE, A2);
    disegna_cornice(A0);
}
static void i_windowlimits(void) { D0 = 1; }
static void i_reportmouse(void) {
    /* ReportMouse(flag D0, window A0) */
    uint32_t f = R32(A0 + WD_FLAGS);
    W32(A0 + WD_FLAGS, D0 ? f | WFLG_REPORTMOUSE : f & ~WFLG_REPORTMOUSE);
}

/* la cornice di una finestra non BORDERLESS, come Intuition 1.3: la barra
 * del titolo piena di BlockPen con il titolo in DetailPen se la finestra e'
 * attiva (al contrario se no), il bordo, il gadget di chiusura a sinistra e
 * i due di profondita' a destra. Penne 255 = quelle dello schermo */
static void disegna_cornice(uint32_t w) {
    uint32_t flag = R32(w + WD_FLAGS);
    if (flag & WFLG_BORDERLESS) return;
    uint32_t rp = R32(w + WD_RPORT), sc = R32(w + WD_WSCREEN);
    int lar = S16(w + WD_WIDTH), alt = S16(w + WD_HEIGHT);
    uint8_t fg = R8(rp + 25), bg = R8(rp + 26), dm = R8(rp + 28);
    int dp = R8(w + WD_DETAILPEN), bp = R8(w + WD_BLOCKPEN);
    if (dp == 0xFF) dp = sc ? R8(sc + SC_DETAILPEN) : 0;
    if (bp == 0xFF) bp = sc ? R8(sc + SC_BLOCKPEN) : 1;
    int attiva = (flag & WFLG_WINDOWACTIVE) != 0;
    int fondo = attiva ? bp : dp, segno = attiva ? dp : bp;
    W8(rp + 28, 1);                                                  /* JAM2 */
    int top = R8(w + WD_BORDERTOP);
    /* il bordo */
    W8(rp + 25, (uint8_t)bp);
    os_linea(rp, 0, 0, lar - 1, 0);
    os_linea(rp, 0, 0, 0, alt - 1);
    os_linea(rp, lar - 1, 0, lar - 1, alt - 1);
    os_linea(rp, 0, alt - 1, lar - 1, alt - 1);
    if (top > 2) {
        /* la barra del titolo */
        W8(rp + 25, (uint8_t)fondo);
        os_rettangolo(rp, 1, 1, lar - 2, top - 2);
        W8(rp + 25, (uint8_t)bp);
        os_linea(rp, 0, top - 1, lar - 1, top - 1);
        int x_titolo = 4, x_destra = lar - 1;
        if (flag & WFLG_CLOSEGADGET) {                               /* chiusura: un quadratino */
            W8(rp + 25, (uint8_t)segno);
            os_linea(rp, 19, 1, 19, top - 2);
            os_rettangolo(rp, 7, 3, 12, top - 4);
            W8(rp + 25, (uint8_t)fondo);
            os_rettangolo(rp, 9, 5, 10, top - 6 > 5 ? top - 6 : 5);
            x_titolo = 24;
        }
        /* i gadget di profondita' (WFLG_DEPTHGADGET) non si disegnano: le
         * finestre del gioco non si riordinano */
        uint32_t t = R32(w + WD_TITLE);
        if (t) {
            int n = 0;
            while (R8(t + (uint32_t)n)) n++;
            while (n > 0 && x_titolo + os_lunghezza_testo(rp, t, n) > x_destra - 2) n--;
            W8(rp + 25, (uint8_t)segno); W8(rp + 26, (uint8_t)fondo);
            W16(rp + 36, x_titolo); W16(rp + 38, R16(rp + 62) + 1);
            os_testo(rp, t, n);
        }
    }
    W8(rp + 25, fg); W8(rp + 26, bg); W8(rp + 28, dm);
}

/* ---------------------------------------------------------- messaggi IDCMP */

static void libera_risposte(uint32_t w) {
    uint32_t wp = R32(w + WD_WINDOWPORT), m;
    while ((m = os_prendi_msg(wp)) != 0) os_libera(m, IM_SIZE);
}

static void manda(uint32_t w, uint32_t classe, uint16_t codice, uint32_t iaddr) {
    if (!w || !(R32(w + WD_IDCMPFLAGS) & classe)) return;
    uint32_t up = R32(w + WD_USERPORT);
    if (!up) return;
    libera_risposte(w);
    /* MOUSEMOVE: uno solo in coda alla volta, come Intuition */
    if (classe == MOUSEMOVE || classe == INTUITICKS)
        for (uint32_t n = R32(up + MP_MSGLIST + LH_HEAD); R32(n + LN_SUCC); n = R32(n + LN_SUCC))
            if (R32(n + IM_CLASS) == classe) {
                W16(n + IM_MOUSEX, mouse_x(R32(w + WD_WSCREEN)) - S16(w + WD_LEFTEDGE));
                W16(n + IM_MOUSEY, mouse_y(R32(w + WD_WSCREEN)) - S16(w + WD_TOPEDGE));
                return;
            }
    uint32_t m = os_alloca(IM_SIZE, MEMF_PUBLIC | MEMF_CLEAR);
    if (!m) return;
    W16(m + MN_LENGTH, IM_SIZE);
    W32(m + MN_REPLYPORT, R32(w + WD_WINDOWPORT));
    W32(m + IM_CLASS, classe);
    W16(m + IM_CODE, codice);
    W16(m + IM_QUALIFIER, s_qualificatori);
    W32(m + IM_IADDRESS, iaddr);
    uint32_t sc = R32(w + WD_WSCREEN);
    W16(m + IM_MOUSEX, mouse_x(sc) - S16(w + WD_LEFTEDGE));
    W16(m + IM_MOUSEY, mouse_y(sc) - S16(w + WD_TOPEDGE));
    uint64_t us = os_microsecondi();
    W32(m + IM_SECONDS, (uint32_t)(us / 1000000u));
    W32(m + IM_MICROS, (uint32_t)(us % 1000000u));
    W32(m + IM_IDCMPWINDOW, w);
    os_metti_msg(up, m);
    if (os_traccia && classe != INTUITICKS)
        fprintf(stderr, "[idcmp] finestra $%06X classe $%05X codice $%04X qual $%04X ind $%06X mouse %d,%d\n",
                w, classe, codice, s_qualificatori, iaddr, S16(m + IM_MOUSEX), S16(m + IM_MOUSEY));
}

/* ---------------------------------------------------------- gadget */

static void gadget_rettangolo(uint32_t w, uint32_t g, int *x, int *y, int *lar, int *alt) {
    uint16_t f = R16(g + GG_FLAGS);
    int W = S16(w + WD_WIDTH), H = S16(w + WD_HEIGHT);
    *x = S16(g + GG_LEFT) + ((f & GFLG_RELRIGHT) ? W - 1 : 0);
    *y = S16(g + GG_TOP) + ((f & GFLG_RELBOTTOM) ? H - 1 : 0);
    *lar = S16(g + GG_WIDTH) + ((f & GFLG_RELWIDTH) ? W : 0);
    *alt = S16(g + GG_HEIGHT) + ((f & GFLG_RELHEIGHT) ? H : 0);
    /* gadget di un requester: relativi al requester */
    if (R16(g + GG_TYPE) & GTYP_REQGADGET) {
        uint32_t rq = R32(w + WD_FIRSTREQUEST);
        if (rq) { *x += S16(rq + 4); *y += S16(rq + 6); }
    }
}

static void disegna_prop(uint32_t w, uint32_t g) {
    uint32_t pi = R32(g + GG_SPECIALINFO), rp = R32(w + WD_RPORT);
    if (!pi) return;
    int x, y, lar, alt;
    gadget_rettangolo(w, g, &x, &y, &lar, &alt);
    uint16_t f = R16(pi);
    uint8_t fg = R8(rp + 25), dm = R8(rp + 28);
    W8(rp + 28, 1);
    if (!(f & 8)) {                                         /* bordo se non PROPBORDERLESS */
        W8(rp + 25, 1);
        os_linea(rp, x, y, x + lar - 1, y); os_linea(rp, x, y + alt - 1, x + lar - 1, y + alt - 1);
        os_linea(rp, x, y, x, y + alt - 1); os_linea(rp, x + lar - 1, y, x + lar - 1, y + alt - 1);
        x += 2; y += 2; lar -= 4; alt -= 4;
    }
    W8(rp + 25, 0);
    os_rettangolo(rp, x, y, x + lar - 1, y + alt - 1);
    int kw = (f & 2) ? (int)((uint32_t)lar * R16(pi + 6) / 0xFFFF) : lar;
    int kh = (f & 4) ? (int)((uint32_t)alt * R16(pi + 8) / 0xFFFF) : alt;
    if (kw < 4) kw = 4;
    if (kh < 3) kh = 3;
    int kx = x + (int)((uint32_t)(lar - kw) * R16(pi + 2) / 0xFFFF);
    int ky = y + (int)((uint32_t)(alt - kh) * R16(pi + 4) / 0xFFFF);
    uint32_t img = R32(g + GG_RENDER);
    if ((f & 1) || !img) { W8(rp + 25, 1); os_rettangolo(rp, kx, ky, kx + kw - 1, ky + kh - 1); }
    else disegna_immagine(rp, img, kx, ky);
    W16(pi + 10, kw); W16(pi + 12, kh);
    W8(rp + 25, fg); W8(rp + 28, dm);
}

static void disegna_stringa(uint32_t w, uint32_t g, int attivo) {
    uint32_t si = R32(g + GG_SPECIALINFO), rp = R32(w + WD_RPORT);
    if (!si) return;
    int x, y, lar, alt;
    gadget_rettangolo(w, g, &x, &y, &lar, &alt);
    uint8_t fg = R8(rp + 25), bg = R8(rp + 26), dm = R8(rp + 28);
    uint32_t tf = R32(rp + 52);
    /* si cancella solo dove puo' stare il testo (MaxChars-1 caratteri, alto
     * quanto il font), non tutto il riquadro: Total Chaos mette gadget
     * numerici larghi 80 pixel sopra le etichette che scrive prima */
    int fl = tf ? R16(tf + 24) : 8, fa = tf ? R16(tf + 20) : 8;
    int cl = (R16(si + 10) > 1 ? R16(si + 10) - 1 : 1) * fl;
    if (cl > lar) cl = lar;
    if (fa > alt) fa = alt;
    W8(rp + 28, 1); W8(rp + 25, 0);
    os_rettangolo(rp, x, y, x + cl - 1, y + fa - 1);
    uint32_t buf = R32(si);
    int n = 0;
    while (R8(buf + (uint32_t)n) && n < R16(si + 10)) n++;
    W8(rp + 25, 1); W8(rp + 26, 0);
    W16(rp + 36, x); W16(rp + 38, y + (tf ? S16(tf + 26) : 6));
    os_testo(rp, buf, n);
    if (attivo) {                                            /* il cursore */
        int pos = R16(si + 8);
        int cx = x + os_lunghezza_testo(rp, buf, pos < n ? pos : n);
        W8(rp + 28, 2);
        os_rettangolo(rp, cx, y, cx + 7, y + (tf ? R16(tf + 20) : 8) - 1);
    }
    W8(rp + 25, fg); W8(rp + 26, bg); W8(rp + 28, dm);
}

/* disegna n gadget (n < 0: tutti) a partire da g */
void os_disegna_gadget(uint32_t w, uint32_t g, int n) {
    uint32_t rp = R32(w + WD_RPORT);
    for (; g && n != 0; g = R32(g + GG_NEXT), n--) {
        uint16_t tipo = R16(g + GG_TYPE) & 7, f = R16(g + GG_FLAGS);
        int x, y, lar, alt;
        gadget_rettangolo(w, g, &x, &y, &lar, &alt);
        if (tipo == GTYP_PROPGADGET) { disegna_prop(w, g); continue; }
        if (tipo == GTYP_STRGADGET) disegna_stringa(w, g, g == s_gadget_attivo);
        uint32_t r = R32(g + GG_RENDER);
        if ((f & GFLG_SELECTED) && (f & 3) == GFLG_GADGHIMAGE && R32(g + GG_SELECTRENDER))
            r = R32(g + GG_SELECTRENDER);
        if (r) {
            if (f & GFLG_GADGIMAGE) disegna_immagine(rp, r, x, y);
            else disegna_bordo(rp, r, x, y);
        }
        if (R32(g + GG_TEXT)) stampa_itext(rp, R32(g + GG_TEXT), x, y);
        if ((f & GFLG_SELECTED) && (f & 3) == GFLG_GADGHCOMP && tipo != GTYP_STRGADGET) {
            uint8_t dm = R8(rp + 28);
            W8(rp + 28, 2);
            os_rettangolo(rp, x, y, x + lar - 1, y + alt - 1);
            W8(rp + 28, dm);
        }
    }
}

/* numero di gadget delle *GList: -1 tutta la lista; anche 0 (Kickstart 1.3
 * conta con un dbra, e Chaos passa 0 per aggiungere tutta la lista) */
static int quanti_gadget(uint32_t d) { int n = (int16_t)d; return n <= 0 ? -1 : n; }

static void i_refreshglist(void) { os_disegna_gadget(A1, A0, quanti_gadget(D0)); }   /* RefreshGList(g A0, w A1, req A2, n D0) */
static void i_refreshgadgets(void) { os_disegna_gadget(A1, A0, -1); }
static void i_addglist(void) {
    /* AddGList(w A0, g A1, pos D0, n D1, req A2): restituisce la posizione */
    uint32_t w = A0, g = A1;
    int n = quanti_gadget(D1), pos = (int16_t)D0;
    uint32_t ultimo = g;
    for (int k = 1; (n < 0 || k < n) && R32(ultimo + GG_NEXT); k++) ultimo = R32(ultimo + GG_NEXT);
    uint32_t prima = w + WD_FIRSTGADGET;
    int i = 0;
    while (R32(prima) && (pos < 0 || i < pos)) { prima = R32(prima) + GG_NEXT; i++; }
    W32(ultimo + GG_NEXT, R32(prima));
    W32(prima, g);
    D0 = (uint32_t)i;
}
static void i_addgadget(void) { D1 = 1; i_addglist(); }
static void i_removeglist(void) {
    /* RemoveGList(w A0, g A1, n D0) */
    uint32_t w = A0, g = A1;
    int n = quanti_gadget(D0);
    uint32_t prima = w + WD_FIRSTGADGET;
    int i = 0;
    while (R32(prima) && R32(prima) != g) { prima = R32(prima) + GG_NEXT; i++; }
    if (!R32(prima)) { D0 = (uint32_t)-1; return; }
    uint32_t ultimo = g;
    for (int k = 1; (n < 0 || k < n) && R32(ultimo + GG_NEXT); k++) ultimo = R32(ultimo + GG_NEXT);
    W32(prima, R32(ultimo + GG_NEXT));
    W32(ultimo + GG_NEXT, 0);
    if (s_gadget_attivo == g) s_gadget_attivo = 0;
    D0 = (uint32_t)i;
}
static void i_removegadget(void) { D0 = 1; i_removeglist(); }
static void i_ongadget(void) { W16(A0 + GG_FLAGS, R16(A0 + GG_FLAGS) & ~GFLG_DISABLED); os_disegna_gadget(A1, A0, 1); }
static void i_offgadget(void) { W16(A0 + GG_FLAGS, R16(A0 + GG_FLAGS) | GFLG_DISABLED); }
static void i_modifyprop(void) {
    /* ModifyProp(g A0, w A1, req A2, flags D0, hpot D1, vpot D2, hbody D3, vbody D4) */
    uint32_t pi = R32(A0 + GG_SPECIALINFO);
    if (!pi) return;
    W16(pi, (R16(pi) & 0xFF00) | (D0 & 0xFF));
    W16(pi + 2, D1); W16(pi + 4, D2); W16(pi + 6, D3); W16(pi + 8, D4);
    if (A1) disegna_prop(A1, A0);
}
static void i_activategadget(void) {
    /* ActivateGadget(g A0, w A1, req A2) */
    if ((R16(A0 + GG_TYPE) & 7) != GTYP_STRGADGET) { D0 = 0; return; }
    s_gadget_attivo = A0; s_gadget_finestra = A1;
    disegna_stringa(A1, A0, 1);
    D0 = 1;
}

/* il gadget sotto il mouse in una finestra */
static uint32_t gadget_sotto(uint32_t w, int mx, int my) {
    for (uint32_t g = R32(w + WD_FIRSTGADGET); g; g = R32(g + GG_NEXT)) {
        if (R16(g + GG_FLAGS) & GFLG_DISABLED) continue;
        int x, y, lar, alt;
        gadget_rettangolo(w, g, &x, &y, &lar, &alt);
        if (mx >= x && mx < x + lar && my >= y && my < y + alt) return g;
    }
    return 0;
}

/* un gadget proporzionale segue il mouse */
static void trascina_prop(uint32_t w, uint32_t g, int mx, int my) {
    uint32_t pi = R32(g + GG_SPECIALINFO);
    if (!pi) return;
    int x, y, lar, alt;
    gadget_rettangolo(w, g, &x, &y, &lar, &alt);
    uint16_t f = R16(pi);
    if (!(f & 8)) { x += 2; y += 2; lar -= 4; alt -= 4; }
    int kw = R16(pi + 10), kh = R16(pi + 12);
    if (f & 2) {
        int r = lar - kw;
        int p = r > 0 ? (mx - x - kw / 2) * 0xFFFF / r : 0;
        W16(pi + 2, p < 0 ? 0 : p > 0xFFFF ? 0xFFFF : p);
    }
    if (f & 4) {
        int r = alt - kh;
        int p = r > 0 ? (my - y - kh / 2) * 0xFFFF / r : 0;
        W16(pi + 4, p < 0 ? 0 : p > 0xFFFF ? 0xFFFF : p);
    }
    disegna_prop(w, g);
}

/* un tasto in un gadget di testo; 1 = finito (RELVERIFY) */
static int tasto_stringa(uint32_t w, uint32_t g, int ascii) {
    uint32_t si = R32(g + GG_SPECIALINFO);
    uint32_t buf = R32(si);
    int max = R16(si + 10), pos = R16(si + 8);
    int n = 0;
    while (R8(buf + (uint32_t)n) && n < max) n++;
    if (ascii == 13) return 1;
    if (ascii == 8) {                                         /* backspace */
        if (pos > 0) {
            memmove(g_amiga_mem + buf + pos - 1, g_amiga_mem + buf + pos, (size_t)(n - pos + 1));
            pos--;
        }
    } else if (ascii == 127) {
        if (pos < n) memmove(g_amiga_mem + buf + pos, g_amiga_mem + buf + pos + 1, (size_t)(n - pos));
    } else if (ascii == -1) { if (pos > 0) pos--; }           /* freccia sinistra */
    else if (ascii == -2) { if (pos < n) pos++; }             /* freccia destra */
    else if (ascii >= 32 && n + 1 < max) {
        memmove(g_amiga_mem + buf + pos + 1, g_amiga_mem + buf + pos, (size_t)(n - pos + 1));
        W8(buf + (uint32_t)pos, ascii);
        pos++;
    }
    W16(si + 8, pos);
    n = 0;
    while (R8(buf + (uint32_t)n) && n < max) n++;
    W16(si + 16, n);                                            /* NumChars */
    if (R16(g + GG_ACTIVATION) & 0x800) W32(si + 28, (uint32_t)atol((const char *)g_amiga_mem + buf));   /* LONGINT */
    disegna_stringa(w, g, 1);
    return 0;
}

/* ---------------------------------------------------------- menu */

static void i_setmenustrip(void) { W32(A0 + WD_MENUSTRIP, A1); D0 = 1; }
static void i_clearmenustrip(void) { W32(A0 + WD_MENUSTRIP, 0); }
static void menu_abilita(uint32_t w, uint16_t num, int on) {
    int m = num & 31, it = (num >> 5) & 63, sub = (num >> 11) & 31;
    uint32_t mu = R32(w + WD_MENUSTRIP);
    for (int i = 0; mu && i < m; i++) mu = R32(mu + MU_NEXT);
    if (!mu) return;
    if (it == 63) { W16(mu + MU_FLAGS, on ? R16(mu + MU_FLAGS) | 1 : R16(mu + MU_FLAGS) & ~1); return; }
    uint32_t v = R32(mu + MU_FIRSTITEM);
    for (int i = 0; v && i < it; i++) v = R32(v + MI_NEXT);
    if (!v) return;
    if (sub != 31) { v = R32(v + MI_SUBITEM); for (int i = 0; v && i < sub; i++) v = R32(v + MI_NEXT); }
    if (v) W16(v + MI_FLAGS, on ? R16(v + MI_FLAGS) | 0x10 : R16(v + MI_FLAGS) & ~0x10);   /* ITEMENABLED */
}
static void i_onmenu(void) { menu_abilita(A0, (uint16_t)D0, 1); }
static void i_offmenu(void) { menu_abilita(A0, (uint16_t)D0, 0); }
static void i_itemaddress(void) {
    /* ItemAddress(menustrip A0, num D0) */
    uint16_t num = (uint16_t)D0;
    if (num == MENUNULL) { D0 = 0; return; }
    int m = num & 31, it = (num >> 5) & 63, sub = (num >> 11) & 31;
    uint32_t mu = A0;
    for (int i = 0; mu && i < m; i++) mu = R32(mu + MU_NEXT);
    uint32_t v = mu ? R32(mu + MU_FIRSTITEM) : 0;
    for (int i = 0; v && i < it; i++) v = R32(v + MI_NEXT);
    if (v && sub != 31) { v = R32(v + MI_SUBITEM); for (int i = 0; v && i < sub; i++) v = R32(v + MI_NEXT); }
    D0 = v;
}

/* il menu aperto si disegna sopra il video (vedi os_video), non sulla bitmap */

/* ---------------------------------------------------------- puntatore, tempo, preferenze */

static void i_setpointer(void) {
    /* SetPointer(w A0, dati A1, alto D0, largo D1, xoff D2, yoff D3) */
    s_puntatore = A1; s_ptr_alto = (int16_t)D0; s_ptr_largo = (int16_t)D1;
    s_ptr_dx = (int16_t)D2; s_ptr_dy = (int16_t)D3;
    W32(A0 + WD_POINTER, A1); W8(A0 + WD_PTRHEIGHT, D0); W8(A0 + WD_PTRWIDTH, D1);
    W8(A0 + WD_XOFFSET, D2); W8(A0 + WD_YOFFSET, D3);
}
static void i_clearpointer(void) { s_puntatore = 0; W32(A0 + WD_POINTER, 0); }
static void i_currenttime(void) {
    uint64_t us = os_microsecondi();
    W32(A0, (uint32_t)(us / 1000000u)); W32(A1, (uint32_t)(us % 1000000u));
}
static void i_doubleclick(void) {
    /* DoubleClick(s1 D0, m1 D1, s2 D2, m2 D3): mezzo secondo */
    int64_t a = (int64_t)D0 * 1000000 + D1, b = (int64_t)D2 * 1000000 + D3;
    D0 = (b - a) >= 0 && (b - a) < 500000;
}
static void i_displaybeep(void) {}
static void i_displayalert(void) {
    char t[200];
    os_leggi_stringa(A0 + 3, t, sizeof t);
    fprintf(stderr, "[intui] DisplayAlert $%08X: %s\n", D0, t);
    D0 = 0;
}
static void i_getprefs(void) {
    /* GetPrefs(buffer A0, size D0): FontHeight 8, DoubleClick 0,5 s, colori */
    uint8_t p[232];
    memset(p, 0, sizeof p);
    p[0] = 8;
    p[20 + 3] = 0; p[24 + 1] = 0x07; p[24 + 2] = 0xA1; p[24 + 3] = 0x20;   /* 0 s, 500000 us */
    static const uint16_t col[4] = { 0x0AAA, 0x0000, 0x0FFF, 0x068B };
    for (int i = 0; i < 4; i++) { p[110 + 2 * i] = (uint8_t)(col[i] >> 8); p[111 + 2 * i] = (uint8_t)col[i]; }
    p[102] = 0x0E; p[103] = 0x44; p[106] = 0x0E; p[107] = 0xEC;
    p[108] = 0; p[109] = 1;
    uint32_t n = D0 < 232 ? D0 : 232;
    memcpy(g_amiga_mem + A0, p, n);
    D0 = A0;
}
static void i_getscreendata(void) {
    /* GetScreenData(buffer A0, size D0, type D1, screen A1): lo schermo
     * Workbench finto, 640 x 256 a 2 piani con topaz 8 (req.library ne prende
     * il font e la barra per i suoi requester) */
    uint8_t s[SC_SIZE];
    memset(s, 0, sizeof s);
    s[SC_WIDTH] = 640 >> 8; s[SC_WIDTH + 1] = 640 & 0xFF;
    s[SC_HEIGHT] = 1; s[SC_HEIGHT + 1] = 0;
    s[SC_BARHEIGHT] = 10;
    s[35] = 11; s[36] = 4; s[37] = 4; s[38] = 2;                    /* WBor* */
    uint32_t ta = ta_topaz(), tf = os_topaz8();
    s[SC_FONT] = (uint8_t)(ta >> 24); s[SC_FONT + 1] = (uint8_t)(ta >> 16);
    s[SC_FONT + 2] = (uint8_t)(ta >> 8); s[SC_FONT + 3] = (uint8_t)ta;
    s[SC_RASTPORT + 52] = (uint8_t)(tf >> 24); s[SC_RASTPORT + 53] = (uint8_t)(tf >> 16);
    s[SC_RASTPORT + 54] = (uint8_t)(tf >> 8); s[SC_RASTPORT + 55] = (uint8_t)tf;
    s[SC_RASTPORT + 59] = 8; s[SC_RASTPORT + 61] = 8; s[SC_RASTPORT + 63] = 6;   /* TxHeight, TxWidth, TxBaseline */
    s[SC_DETAILPEN] = 0; s[SC_BLOCKPEN] = 1;
    s[SC_VIEWPORT + VP_MODES] = HIRES >> 8;
    s[SC_BITMAP + 5] = 2;
    uint32_t n = D0 < SC_SIZE ? D0 : SC_SIZE;
    memcpy(g_amiga_mem + A0, s, n);
    D0 = 1;
}

/* AllocRemember(key A0, size D0, flags D1): Remember = Next 0, Size 4, Memory 8 */
static void i_allocremember(void) {
    uint32_t r = os_alloca(12, MEMF_PUBLIC | MEMF_CLEAR);
    uint32_t m = os_alloca(D0, D1);
    if (!r || !m) { D0 = 0; return; }
    W32(r + 4, D0); W32(r + 8, m);
    W32(r, R32(A0));
    W32(A0, r);
    D0 = m;
}
static void i_freeremember(void) {
    /* FreeRemember(key A0, really D0) */
    uint32_t r = R32(A0);
    while (r) {
        uint32_t succ = R32(r);
        if (D0) os_libera(R32(r + 8), R32(r + 4));
        os_libera(r, 12);
        r = succ;
    }
    W32(A0, 0);
}
static void i_lockibase(void) { D0 = 0; }

/* Requester: per ora solo la contabilita' della finestra */
static void i_request(void) {
    /* Request(req A0, w A1) */
    W32(A0, R32(A1 + WD_FIRSTREQUEST));
    W32(A1 + WD_FIRSTREQUEST, A0);
    W16(A1 + WD_REQCOUNT, R16(A1 + WD_REQCOUNT) + 1);
    W32(A0 + 72, A1);
    uint32_t rp = R32(A1 + WD_RPORT);
    int x = S16(A0 + 4), y = S16(A0 + 6);
    if (!(R16(A0 + 28) & 0x1)) {                                    /* niente POINTREL */
        uint8_t fg = R8(rp + 25);
        W8(rp + 25, R8(A0 + 30));
        os_rettangolo(rp, x, y, x + S16(A0 + 8) - 1, y + S16(A0 + 10) - 1);
        W8(rp + 25, fg);
    }
    if (R32(A0 + 20)) disegna_bordo(rp, R32(A0 + 20), x, y);
    if (R32(A0 + 24)) stampa_itext(rp, R32(A0 + 24), x, y);
    for (uint32_t g = R32(A0 + 16); g; g = R32(g + GG_NEXT)) {
        uint16_t t = R16(g + GG_TYPE);
        W16(g + GG_TYPE, t | GTYP_REQGADGET);
    }
    D0 = 1;
}
static void i_endrequest(void) {
    /* EndRequest(req A0, w A1) */
    uint32_t q = A1 + WD_FIRSTREQUEST;
    while (R32(q) && R32(q) != A0) q = R32(q);
    if (R32(q)) W32(q, R32(A0));
    W16(A1 + WD_REQCOUNT, R16(A1 + WD_REQCOUNT) - 1);
}
static void i_initrequester(void) { memset(g_amiga_mem + A0, 0, 112); }
static void i_autorequest(void) {
    char t[200];
    uint32_t it = A1;
    os_leggi_stringa(R32(it + 12), t, sizeof t);
    fprintf(stderr, "[intui] AutoRequest: %s\n", t);
    D0 = 1;                                                  /* il primo gadget (positivo) */
}
static void i_buildsysrequest(void) { i_autorequest(); D0 = 1; }

/* ---------------------------------------------------------- ingressi */

static int s_vecchio_sx, s_vecchio_dx, s_mx_prima = -1, s_my_prima = -1;
static int s_tasti_coda[32], s_n_tasti;

void os_intuition_mouse(int x, int y, int sinistro, int destro) {
    if (x >= 0) {
        s_mx = x < 0 ? 0 : x > 639 ? 639 : x;
        s_my = y < 0 ? 0 : y > 511 ? 511 : y;
    }
    s_sx = sinistro; s_dx = destro;
}

void os_intuition_tasto(int codice_raw, int rilasciato) {
    if (s_n_tasti < 32) s_tasti_coda[s_n_tasti++] = codice_raw | (rilasciato ? 0x80 : 0);
}

/* ascii di un codice raw (tastiera USA), -1 = niente; anche per keymap.library */
int os_ascii_di(int raw, uint16_t q);
#define ascii_di os_ascii_di
int os_ascii_di(int raw, uint16_t q) {
    static const char base[] = "`1234567890-=\\ 0qwertyuiop[] 123asdfghjkl;'  456 zxcvbnm,./ .789";
    static const char shift[] = "~!@#$%^&*()_+| 0QWERTYUIOP{} 123ASDFGHJKL:\"  456 ZXCVBNM<>? .789";
    if (raw == 0x40) return ' ';
    if (raw == 0x41) return 8;
    if (raw == 0x42) return 9;
    if (raw == 0x44 || raw == 0x43) return 13;
    if (raw == 0x45) return 27;
    if (raw == 0x46) return 127;
    if (raw < (int)sizeof base - 1) {
        char c = (q & 3) ? shift[raw] : base[raw];
        return c == ' ' ? -1 : c;
    }
    return -1;
}

/* la finestra (e le coordinate) sotto il mouse */
static uint32_t finestra_sotto_mouse(void) {
    uint32_t sc = schermo_sotto_mouse();
    if (!sc) return 0;
    int mx = mouse_x(sc), my = mouse_y(sc);
    /* la prima della lista dello schermo che contiene il punto (davanti) */
    for (uint32_t w = R32(sc + SC_FIRSTWINDOW); w; w = R32(w + WD_NEXTWINDOW)) {
        int x = S16(w + WD_LEFTEDGE), y = S16(w + WD_TOPEDGE);
        if (mx >= x && mx < x + S16(w + WD_WIDTH) && my >= y && my < y + S16(w + WD_HEIGHT)) return w;
    }
    return 0;
}

static void apri_menu(uint32_t w);
static void chiudi_menu(int scegli);
static void aggiorna_menu(void);

void os_intuition_quadro(void) {
    int sx = s_sx, dx = s_dx;
    s_qualificatori = (uint16_t)((s_qualificatori & 0x00FF) | (sx ? 0x4000 : 0) | (dx ? 0x2000 : 0));
    /* le finestre aggiornano MouseX/Y; lo schermo davanti pure */
    for (int i = 0; i < s_n_finestre; i++) {
        uint32_t w = s_finestre[i], sc = R32(w + WD_WSCREEN);
        W16(w + WD_MOUSEX, mouse_x(sc) - S16(w + WD_LEFTEDGE));
        W16(w + WD_MOUSEY, mouse_y(sc) - S16(w + WD_TOPEDGE));
        W16(w + WD_GZZMOUSEX, mouse_x(sc) - S16(w + WD_LEFTEDGE) - R8(w + WD_BORDERLEFT));
        W16(w + WD_GZZMOUSEY, mouse_y(sc) - S16(w + WD_TOPEDGE) - R8(w + WD_BORDERTOP));
    }
    for (int i = 0; i < s_n_schermi; i++) {
        W16(s_schermi[i] + SC_MOUSEX, mouse_x(s_schermi[i]));
        W16(s_schermi[i] + SC_MOUSEY, mouse_y(s_schermi[i]));
    }
    if (s_n_schermi) { W16(s_ibase + 70, mouse_x(s_schermi[0])); W16(s_ibase + 68, mouse_y(s_schermi[0])); }

    uint32_t w = s_attiva;
    /* tasto sinistro premuto: attiva la finestra sotto il mouse, gadget */
    if (sx && !s_vecchio_sx && !s_menu_aperto) {
        uint32_t sotto = finestra_sotto_mouse();
        if (sotto && sotto != s_attiva) {
            if (s_attiva) manda(s_attiva, INACTIVEWINDOW, 0, 0);
            M68KState s = g_cpu;
            A0 = sotto;
            i_activatewindow();
            g_cpu = s;
            manda(sotto, ACTIVEWINDOW, 0, 0);
        }
        w = s_attiva;
        if (w) {
            uint32_t sc = R32(w + WD_WSCREEN);
            int mx = mouse_x(sc) - S16(w + WD_LEFTEDGE), my = mouse_y(sc) - S16(w + WD_TOPEDGE);
            /* il gadget di chiusura (in alto a sinistra): CLOSEWINDOW, il
             * programma chiude la finestra quando lo riceve */
            int chiusura = (R32(w + WD_FLAGS) & WFLG_CLOSEGADGET) && !(R32(w + WD_FLAGS) & WFLG_BORDERLESS)
                           && mx >= 0 && mx < 20 && my >= 0 && my < R8(w + WD_BORDERTOP);
            if (chiusura) { manda(w, CLOSEWINDOW, 0, 0); s_vecchio_sx = sx; s_vecchio_dx = dx; return; }
            uint32_t g = gadget_sotto(w, mx, my);
            if (s_gadget_attivo && s_gadget_attivo != g && (R16(s_gadget_attivo + GG_TYPE) & 7) == GTYP_STRGADGET) {
                uint32_t vecchio = s_gadget_attivo;
                s_gadget_attivo = 0;
                disegna_stringa(s_gadget_finestra, vecchio, 0);
            }
            if (g) {
                s_gadget_attivo = g; s_gadget_finestra = w;
                uint16_t tipo = R16(g + GG_TYPE) & 7;
                if (tipo == GTYP_BOOLGADGET || tipo == 2) {
                    uint16_t f = R16(g + GG_FLAGS);
                    if (R16(g + GG_ACTIVATION) & GACT_TOGGLESELECT) f ^= GFLG_SELECTED; else f |= GFLG_SELECTED;
                    W16(g + GG_FLAGS, f);
                    os_disegna_gadget(w, g, 1);
                } else if (tipo == GTYP_PROPGADGET) trascina_prop(w, g, mx, my);
                else if (tipo == GTYP_STRGADGET) {
                    uint32_t si = R32(g + GG_SPECIALINFO);
                    int n = 0;
                    while (R8(R32(si) + (uint32_t)n)) n++;
                    W16(si + 8, n);
                    disegna_stringa(w, g, 1);
                }
                if (R16(g + GG_ACTIVATION) & GACT_IMMEDIATE) manda(w, GADGETDOWN, 0, g);
            } else manda(w, MOUSEBUTTONS, SELECTDOWN, 0);
        }
    }
    /* tasto sinistro tenuto: i proporzionali seguono */
    if (sx && s_gadget_attivo && (R16(s_gadget_attivo + GG_TYPE) & 7) == GTYP_PROPGADGET) {
        uint32_t gw = s_gadget_finestra, sc = R32(gw + WD_WSCREEN);
        trascina_prop(gw, s_gadget_attivo, mouse_x(sc) - S16(gw + WD_LEFTEDGE), mouse_y(sc) - S16(gw + WD_TOPEDGE));
    }
    /* tasto sinistro rilasciato */
    if (!sx && s_vecchio_sx && !s_menu_aperto) {
        uint32_t g = s_gadget_attivo, gw = s_gadget_finestra;
        if (g && (R16(g + GG_TYPE) & 7) != GTYP_STRGADGET) {
            uint32_t sc = R32(gw + WD_WSCREEN);
            int mx = mouse_x(sc) - S16(gw + WD_LEFTEDGE), my = mouse_y(sc) - S16(gw + WD_TOPEDGE);
            int dentro = gadget_sotto(gw, mx, my) == g;
            uint16_t tipo = R16(g + GG_TYPE) & 7;
            if ((tipo == GTYP_BOOLGADGET || tipo == 2) && !(R16(g + GG_ACTIVATION) & GACT_TOGGLESELECT)) {
                W16(g + GG_FLAGS, R16(g + GG_FLAGS) & ~GFLG_SELECTED);
                os_disegna_gadget(gw, g, 1);
            }
            if ((R16(g + GG_ACTIVATION) & GACT_RELVERIFY) && (dentro || tipo == GTYP_PROPGADGET))
                manda(gw, GADGETUP, 0, g);
            s_gadget_attivo = 0;
        } else if (!g && w) manda(w, MOUSEBUTTONS, SELECTUP, 0);
    }
    /* tasto destro: menu, o messaggio se la finestra lo vuole (RMBTRAP) */
    if (dx != s_vecchio_dx && os_traccia)
        fprintf(stderr, "[intui] tasto destro %s, finestra $%06X menu $%06X flag $%08X\n", dx ? "giu" : "su", w,
                w ? R32(w + WD_MENUSTRIP) : 0, w ? R32(w + WD_FLAGS) : 0);
    if (dx != s_vecchio_dx && w) {
        if ((R32(w + WD_FLAGS) & WFLG_RMBTRAP) || !R32(w + WD_MENUSTRIP))
            manda(w, MOUSEBUTTONS, dx ? MENUDOWN : MENUUP, 0);
        else if (dx) apri_menu(w);
        else chiudi_menu(1);
    }
    if (s_menu_aperto) aggiorna_menu();
    /* movimento */
    if ((s_mx != s_mx_prima || s_my != s_my_prima) && w && (R32(w + WD_FLAGS) & WFLG_REPORTMOUSE))
        manda(w, MOUSEMOVE, 0, 0);
    if (s_mx != s_mx_prima || s_my != s_my_prima) {
        if (w && s_gadget_attivo && (R16(s_gadget_attivo + GG_ACTIVATION) & GACT_FOLLOWMOUSE))
            manda(w, MOUSEMOVE, 0, s_gadget_attivo);
    }
    s_mx_prima = s_mx; s_my_prima = s_my;
    /* tasti */
    for (int i = 0; i < s_n_tasti; i++) {
        int k = s_tasti_coda[i], raw = k & 0x7F, su = k & 0x80;
        static const int qual[8] = { 1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80 };   /* $60..$67 */
        if (raw >= 0x60 && raw <= 0x67) {
            if (su) s_qualificatori &= (uint16_t)~qual[raw - 0x60];
            else s_qualificatori |= (uint16_t)qual[raw - 0x60];
        }
        if (!w) continue;
        /* un gadget di testo attivo prende i tasti */
        if (s_gadget_attivo && !su && (R16(s_gadget_attivo + GG_TYPE) & 7) == GTYP_STRGADGET) {
            int a = raw == 0x4F ? -1 : raw == 0x4E ? -2 : ascii_di(raw, s_qualificatori);
            if (a != -1 || raw == 0x4F) {
                if (tasto_stringa(s_gadget_finestra, s_gadget_attivo, a)) {
                    uint32_t g = s_gadget_attivo;
                    s_gadget_attivo = 0;
                    disegna_stringa(s_gadget_finestra, g, 0);
                    if (R16(g + GG_ACTIVATION) & GACT_RELVERIFY) manda(s_gadget_finestra, GADGETUP, 0, g);
                }
            }
            continue;
        }
        int a = ascii_di(raw, s_qualificatori);
        if ((R32(w + WD_IDCMPFLAGS) & VANILLAKEY) && a >= 0 && !su) manda(w, VANILLAKEY, (uint16_t)a, 0);
        else manda(w, RAWKEY, (uint16_t)k, 0);
    }
    s_n_tasti = 0;
    /* INTUITICKS: 10 al secondo */
    extern uint64_t g_os_quadri;
    if (w && (g_os_quadri % 5) == 0) manda(w, INTUITICKS, 0, 0);
    s_vecchio_sx = sx; s_vecchio_dx = dx;
}

/* ---------------------------------------------------------- menu (disegnati sul video) */

/* il menu aperto: la barra in cima allo schermo della finestra, la colonna
 * delle voci del menu sotto il mouse. Le voci si disegnano con le loro
 * IntuiText/Image su una bitmap nostra, poi copiata sul video */
static uint32_t s_menu_rp, s_menu_bm;
static int s_menu_lar = 640, s_menu_alt = 256;

static void apri_menu(uint32_t w) {
    if (os_traccia) fprintf(stderr, "[intui] menu aperto sulla finestra $%06X\n", w);
    s_menu_aperto = 1; s_menu_finestra = w;
    s_menu_sel = s_voce_sel = s_sotto_sel = -1;
    W32(w + WD_FLAGS, R32(w + WD_FLAGS) | WFLG_MENUSTATE);
}

/* numero del menu, della voce e della sottovoce sotto il mouse */
static void menu_sotto_mouse(uint32_t w, int *m, int *it, int *sub) {
    uint32_t sc = R32(w + WD_WSCREEN);
    int mx = mouse_x(sc), my = mouse_y(sc);
    *m = *it = *sub = -1;
    int barra = R8(sc + SC_BARHEIGHT) + 1;
    uint32_t mu = R32(w + WD_MENUSTRIP);
    int i = 0;
    for (; mu; mu = R32(mu + MU_NEXT), i++) {
        int x = S16(mu + MU_LEFT), lar = S16(mu + MU_WIDTH);
        if (my < barra && mx >= x && mx < x + lar) { *m = i; return; }
        if (i != s_menu_sel) continue;
        *m = i;
        int k = 0;
        for (uint32_t v = R32(mu + MU_FIRSTITEM); v; v = R32(v + MI_NEXT), k++) {
            int vx = x + S16(v + MI_LEFT), vy = barra + S16(v + MI_TOP);
            if (mx >= vx && mx < vx + S16(v + MI_WIDTH) && my >= vy && my < vy + S16(v + MI_HEIGHT)) {
                *it = k;
                return;
            }
        }
    }
    if (s_menu_sel >= 0 && *m < 0) *m = s_menu_sel;
}

static void aggiorna_menu(void) {
    int m, it, sub;
    menu_sotto_mouse(s_menu_finestra, &m, &it, &sub);
    if (m >= 0) s_menu_sel = m;
    s_voce_sel = it;
    s_sotto_sel = sub;
}

static void chiudi_menu(int scegli) {
    if (!s_menu_aperto) return;
    uint32_t w = s_menu_finestra;
    s_menu_aperto = 0;
    W32(w + WD_FLAGS, R32(w + WD_FLAGS) & ~WFLG_MENUSTATE);
    uint16_t codice = MENUNULL;
    if (scegli && s_menu_sel >= 0 && s_voce_sel >= 0) {
        uint32_t mu = R32(w + WD_MENUSTRIP);
        for (int i = 0; mu && i < s_menu_sel; i++) mu = R32(mu + MU_NEXT);
        uint32_t v = mu ? R32(mu + MU_FIRSTITEM) : 0;
        for (int i = 0; v && i < s_voce_sel; i++) v = R32(v + MI_NEXT);
        if (mu && (R16(mu + MU_FLAGS) & 1) && v && (R16(v + MI_FLAGS) & 0x10)) {
            codice = (uint16_t)(s_menu_sel | s_voce_sel << 5 | 31 << 11);
            /* CHECKIT/MENUTOGGLE e MutualExclude */
            uint16_t f = R16(v + MI_FLAGS);
            if (f & 1) {                                              /* CHECKIT */
                if (f & 8) f ^= 0x100; else f |= 0x100;               /* MENUTOGGLE / CHECKED */
                W16(v + MI_FLAGS, f);
                uint32_t ex = R32(v + MI_MUTUALEXCLUDE);
                int k = 0;
                for (uint32_t o = R32(mu + MU_FIRSTITEM); o; o = R32(o + MI_NEXT), k++)
                    if (o != v && (ex >> k & 1)) W16(o + MI_FLAGS, R16(o + MI_FLAGS) & ~0x100);
            }
            W16(v + MI_NEXTSELECT, MENUNULL);
        }
    }
    if (os_traccia) fprintf(stderr, "[intui] menu chiuso: menu %d voce %d -> MENUPICK $%04X\n", s_menu_sel, s_voce_sel, codice);
    manda(w, MENUPICK, codice, 0);
}

/* disegna la barra e la colonna aperta sulla bitmap dei menu, come Intuition
 * sullo schermo: stessa profondita' e stessi colori dello schermo, barra e
 * riquadro nella BlockPen, bordi e titoli nella DetailPen, le voci con le
 * penne delle loro IntuiText/Image, la voce sotto il mouse in complemento
 * (HIGHCOMP), quelle spente retinate con la BlockPen. Su video vanno solo i
 * rettangoli della barra e del riquadro (s_menu_rett) */
static int s_menu_prof;
static int s_menu_rett[2][4], s_menu_n_rett;
static void disegna_menu(uint32_t sc) {
    uint32_t w = s_menu_finestra;
    int lar = S16(sc + SC_WIDTH), alt = S16(sc + SC_HEIGHT), barra = R8(sc + SC_BARHEIGHT) + 1;
    int prof = R8(R32(sc + SC_RASTPORT + 4) + 5);
    if (prof < 1) prof = 1;
    if (prof > 8) prof = 8;
    if (lar > 640) lar = 640;
    if (alt > 512) alt = 512;
    if (!s_menu_bm) {
        s_menu_bm = os_alloca(40, MEMF_PUBLIC | MEMF_CLEAR);
        for (int p = 0; p < 8; p++) W32(s_menu_bm + 8 + 4u * p, os_alloca(640 / 8 * 512, MEMF_CHIP | MEMF_CLEAR));
        s_menu_rp = os_alloca(100, MEMF_PUBLIC | MEMF_CLEAR);
    }
    W16(s_menu_bm, (uint16_t)(((lar + 15) >> 4) << 1)); W16(s_menu_bm + 2, (uint16_t)alt); W8(s_menu_bm + 5, (uint8_t)prof);
    s_menu_lar = lar; s_menu_alt = alt; s_menu_prof = prof;
    os_init_rastport(s_menu_rp);
    W32(s_menu_rp + 4, s_menu_bm);
    uint32_t tf = R32(sc + SC_RASTPORT + 52);
    if (tf) { W32(s_menu_rp + 52, tf); W16(s_menu_rp + 58, R16(tf + 20)); W16(s_menu_rp + 62, R16(tf + 26)); }
    for (int p = 0; p < prof; p++) memset(g_amiga_mem + R32(s_menu_bm + 8 + 4u * p), 0, (size_t)R16(s_menu_bm) * (size_t)alt);
    int dp = R8(sc + SC_DETAILPEN), bp = R8(sc + SC_BLOCKPEN), trim = -1;
    if (dp == 0xFF) dp = 0;
    if (bp == 0xFF) bp = 1;
    penne_t *pe = penne_di(sc, 0);
    if (pe && (R32(w + WD_FLAGS) & 0x00200000)) {                      /* WFLG_NEWLOOKMENUS */
        dp = pe->p[9]; bp = pe->p[10]; trim = pe->p[11];
    }
    s_menu_n_rett = 0;
    /* la barra */
    W8(s_menu_rp + 28, 1);                                              /* JAM2 */
    W8(s_menu_rp + 25, (uint8_t)bp);
    os_rettangolo(s_menu_rp, 0, 0, lar - 1, barra - 1);
    if (trim >= 0) { W8(s_menu_rp + 25, (uint8_t)trim); os_linea(s_menu_rp, 0, barra - 1, lar - 1, barra - 1); }
    s_menu_rett[s_menu_n_rett][0] = 0; s_menu_rett[s_menu_n_rett][1] = 0;
    s_menu_rett[s_menu_n_rett][2] = lar - 1; s_menu_rett[s_menu_n_rett][3] = barra - 1; s_menu_n_rett++;
    int i = 0;
    for (uint32_t mu = R32(w + WD_MENUSTRIP); mu; mu = R32(mu + MU_NEXT), i++) {
        int x = S16(mu + MU_LEFT);
        uint32_t nome = R32(mu + MU_NAME);
        int n = 0;
        while (R8(nome + (uint32_t)n)) n++;
        W8(s_menu_rp + 28, 0);                                          /* JAM1 */
        W8(s_menu_rp + 25, (uint8_t)dp);
        W16(s_menu_rp + 36, x + 4); W16(s_menu_rp + 38, R16(s_menu_rp + 62) + 1);
        os_testo(s_menu_rp, nome, n);
        if (!(R16(mu + MU_FLAGS) & 1)) {                                /* menu spento */
            W8(s_menu_rp + 25, (uint8_t)bp);
            for (int yy = 0; yy < barra - 1; yy++)
                for (int xx = x + (yy & 1); xx < x + S16(mu + MU_WIDTH); xx += 2) os_linea(s_menu_rp, xx, yy, xx, yy);
        }
        if (i != s_menu_sel) continue;
        W8(s_menu_rp + 28, 2);                                          /* il titolo scelto: complemento */
        os_rettangolo(s_menu_rp, x, 0, x + S16(mu + MU_WIDTH) - 1, barra - 2);
        /* la colonna delle voci */
        int x0 = 32767, y1 = 0, x1 = 0;
        for (uint32_t v = R32(mu + MU_FIRSTITEM); v; v = R32(v + MI_NEXT)) {
            int vx = x + S16(v + MI_LEFT), vy = barra + S16(v + MI_TOP);
            if (vx < x0) x0 = vx;
            if (vx + S16(v + MI_WIDTH) > x1) x1 = vx + S16(v + MI_WIDTH);
            if (vy + S16(v + MI_HEIGHT) > y1) y1 = vy + S16(v + MI_HEIGHT);
        }
        if (x0 == 32767) continue;
        int bx0 = x0 - 2, by0 = barra, bx1 = x1 + 1, by1 = y1 + 1;
        W8(s_menu_rp + 28, 1);
        W8(s_menu_rp + 25, (uint8_t)bp);
        os_rettangolo(s_menu_rp, bx0, by0, bx1, by1);
        W8(s_menu_rp + 25, (uint8_t)dp);
        os_linea(s_menu_rp, bx0, by0, bx0, by1); os_linea(s_menu_rp, bx1, by0, bx1, by1);
        os_linea(s_menu_rp, bx0, by1, bx1, by1);
        s_menu_rett[s_menu_n_rett][0] = bx0; s_menu_rett[s_menu_n_rett][1] = by0;
        s_menu_rett[s_menu_n_rett][2] = bx1; s_menu_rett[s_menu_n_rett][3] = by1; s_menu_n_rett++;
        int k = 0;
        for (uint32_t v = R32(mu + MU_FIRSTITEM); v; v = R32(v + MI_NEXT), k++) {
            int vx = x + S16(v + MI_LEFT), vy = barra + S16(v + MI_TOP);
            uint16_t f = R16(v + MI_FLAGS);
            uint32_t fill = R32(v + MI_ITEMFILL);
            W8(s_menu_rp + 28, 1);
            if (k == s_voce_sel && (f & 0x10) && (f & 0xC0) == 0 && R32(v + 22))   /* HIGHIMAGE: SelectFill */
                fill = R32(v + 22);
            if (fill) {
                if (f & 2) stampa_itext(s_menu_rp, fill, vx, vy);          /* ITEMTEXT */
                else disegna_immagine(s_menu_rp, fill, vx, vy);
            }
            if ((f & 1) && (f & 0x100)) {                                  /* il segno di spunta */
                W8(s_menu_rp + 25, (uint8_t)dp);
                os_linea(s_menu_rp, vx + 1, vy + 4, vx + 3, vy + 6);
                os_linea(s_menu_rp, vx + 3, vy + 6, vx + 8, vy + 1);
            }
            if (k == s_voce_sel && (f & 0x10) && (R16(mu + MU_FLAGS) & 1) && (f & 0xC0) == 0x40) {   /* HIGHCOMP */
                W8(s_menu_rp + 28, 2);
                os_rettangolo(s_menu_rp, vx, vy, vx + S16(v + MI_WIDTH) - 1, vy + S16(v + MI_HEIGHT) - 1);
            }
            if (k == s_voce_sel && (f & 0x10) && (R16(mu + MU_FLAGS) & 1) && (f & 0xC0) == 0x80) {   /* HIGHBOX */
                W8(s_menu_rp + 28, 2);
                int ax = vx, ay = vy, bx = vx + S16(v + MI_WIDTH) - 1, by = vy + S16(v + MI_HEIGHT) - 1;
                os_linea(s_menu_rp, ax, ay, bx, ay); os_linea(s_menu_rp, ax, by, bx, by);
                os_linea(s_menu_rp, ax, ay + 1, ax, by - 1); os_linea(s_menu_rp, bx, ay + 1, bx, by - 1);
            }
            if (!(f & 0x10)) {                                             /* spenta: retinata */
                W8(s_menu_rp + 28, 1);
                W8(s_menu_rp + 25, (uint8_t)bp);
                for (int yy = vy; yy < vy + S16(v + MI_HEIGHT); yy++)
                    for (int xx = vx + (yy & 1); xx < vx + S16(v + MI_WIDTH); xx += 2) os_linea(s_menu_rp, xx, yy, xx, yy);
            }
        }
    }
}

/* ---------------------------------------------------------- il video */

#define VIDEO_L 640
#define VIDEO_A 512
static uint32_t s_video[VIDEO_L * VIDEO_A];
static int s_video_alto = 512;

static inline uint32_t argb(uint16_t c) {
    uint32_t r = (c >> 8) & 15, g = (c >> 4) & 15, b = c & 15;
    return 0xFF000000u | (r * 17) << 16 | (g * 17) << 8 | (b * 17);
}

/* la bitmap che si vede (RasInfo->BitMap) e lo scostamento dentro di lei */
static uint32_t bitmap_visibile(uint32_t sc, int *rx, int *ry) {
    uint32_t ri = R32(sc + SC_VIEWPORT + VP_RASINFO);
    *rx = ri ? S16(ri + 8) : 0;
    *ry = ri ? S16(ri + 10) : 0;
    uint32_t bm = ri ? R32(ri + 4) : 0;
    return bm ? bm : sc + SC_BITMAP;
}

/* una riga dello schermo sc (riga y del suo ViewPort) nella riga del video */
static void riga_schermo(uint32_t sc, int y, uint32_t *dest, const uint32_t pal[256], int mod_ehb) {
    int rx, ry;
    uint32_t bm = bitmap_visibile(sc, &rx, &ry);
    int bpr = R16(bm), righe = R16(bm + 2), prof = R8(bm + 5);
    int hires = sch_hires(sc);
    int lar = S16(sc + SC_WIDTH);
    int sx = S16(sc + SC_LEFTEDGE) - rx;
    y += ry;
    (void)mod_ehb;
    if (y < 0 || y >= righe) { for (int x = 0; x < VIDEO_L; x++) dest[x] = pal[0]; return; }
    uint32_t piani[8];
    for (int p = 0; p < prof && p < 8; p++) piani[p] = R32(bm + 8 + 4u * p);
    int passo = hires ? 1 : 2;
    /* HAM: i due piani alti scelgono fra colore della palette (00) e modifica
     * di blu (01), rosso (10) o verde (11) del pixel prima. HAM8 (8 piani):
     * 64 colori, 6 bit di modifica; HAM6: 16 colori, 4 bit */
    int ham = (R16(sc + SC_VIEWPORT + VP_MODES) & 0x0800) && (prof == 8 || prof == 6);
    int hb = prof - 2;                                   /* bit dei dati: 6 o 4 */
    uint32_t prec = pal[0];
    /* gli indici di colore della riga, 8 pixel per volta: un byte di ogni
     * piano, "allargato" in 8 byte (un bit per byte) e spostato al suo posto */
    static uint64_t largo[256];
    if (!largo[255]) for (int b = 0; b < 256; b++) {
        uint64_t l = 0;
        for (int i = 0; i < 8; i++) if (b & (0x80 >> i)) l |= 1ull << (8 * i);
        largo[b] = l;
    }
    static uint8_t indice[4096 + 8];
    int lo = -sx > 0 ? -sx : 0, hi = VIDEO_L / passo - sx;
    if (hi > lar + rx) hi = lar + rx;
    if (hi > bpr * 8) hi = bpr * 8;
    if (hi > 4096) hi = 4096;
    for (int b = lo >> 3; b * 8 < hi; b++) {
        uint64_t v8 = 0;
        uint32_t off = (uint32_t)y * (uint32_t)bpr + (uint32_t)b;
        for (int p = 0; p < prof && p < 8; p++)
            if (piani[p] && piani[p] + off < g_amiga_mem_size) v8 |= largo[g_amiga_mem[piani[p] + off]] << p;
        memcpy(&indice[b * 8], &v8, 8);                  /* little endian: il byte 0 e' il pixel a sinistra */
    }
    for (int vx = 0; vx < VIDEO_L; vx += passo) {
        int x = vx / passo - sx;
        uint32_t c = pal[0];
        if (x >= lo && x < hi) {
            int v = indice[x];
            if (ham) {
                int dati = v & ((1 << hb) - 1), ctl = v >> hb;
                uint32_t k = hb == 6 ? (uint32_t)(dati << 2 | dati >> 4) : (uint32_t)(dati * 17);
                if (ctl == 0) c = pal[dati];
                else if (ctl == 1) c = (prec & 0xFFFFFF00u) | k;
                else if (ctl == 2) c = (prec & 0xFF00FFFFu) | k << 16;
                else c = (prec & 0xFFFF00FFu) | k << 8;
                prec = c;
            } else c = pal[v & 255];
        }
        dest[vx] = c;
        if (passo == 2) dest[vx + 1] = c;
    }
}

/* la palette dello schermo: 256 colori a 24 bit (ColorMap V39); con 6 piani
 * in EXTRA_HALFBRITE i colori 32-63 sono quelli di 0-31 a meta' luce */
static void palette_schermo(uint32_t sc, uint32_t pal[256], int *ehb) {
    uint32_t cm = R32(sc + SC_VIEWPORT + VP_COLORMAP);
    uint16_t modi = R16(sc + SC_VIEWPORT + VP_MODES);
    int n = cm ? R16(cm + 2) : 0;
    for (int i = 0; i < 256; i++) pal[i] = 0xFF000000u | (i < n ? os_cm_rgb24(cm, (uint32_t)i) : 0);
    *ehb = (modi & EXTRA_HALFBRITE) || (R8(sc + SC_BITMAP + 5) == 6 && n <= 32);
    if (*ehb)
        for (int i = 0; i < 32; i++) pal[32 + i] = 0xFF000000u | ((pal[i] >> 1) & 0x7F7F7F);
}

/* i cambi della copper list utente (CWait riga, CMove colore) fino alla riga y */
static void copper_utente(uint32_t sc, int y, uint32_t pal[256]) {
    uint32_t ucl = R32(sc + SC_VIEWPORT + VP_UCOPINS);
    if (!ucl) return;
    uint32_t cl = R32(ucl + 4);
    if (!cl) return;
    uint32_t ins = R32(cl + 12);
    int n = R16(cl + 28), attesa = 0;
    for (int i = 0; i < n; i++) {
        uint32_t p = ins + 6u * (uint32_t)i;
        int op = R16(p);
        if (op == 1) { attesa = S16(p + 2); if (attesa > y) return; }
        else {
            int reg = R16(p + 2);
            if (reg >= 0x180 && reg < 0x1C0 && attesa <= y) pal[(reg - 0x180) / 2] = argb(R16(p + 4));
        }
    }
}

/* la freccia di Intuition (forma e colori come la ROM: rosso e grigio) */
static const uint16_t s_freccia[16][2] = {
    {0xC000,0x4000},{0x7000,0xB000},{0x3C00,0x4C00},{0x3F00,0x4300},{0x1FC0,0x20C0},{0x1FC0,0x2000},
    {0x0F00,0x1100},{0x0D80,0x1280},{0x04C0,0x0940},{0x0460,0x08A0},{0x0020,0x0040},{0,0},{0,0},{0,0},{0,0},{0,0} };

/* gli sprite di graphics (GetSprite/ChangeSprite/MoveSprite, amiga_grafica.c):
 * SimpleSprite posctldata 0 (parole pos, ctl, poi due parole a riga), height
 * 4, x 6, y 8 nella risoluzione del ViewPort (lo schermo del gioco, HIRES
 * LACE: le coordinate del video). Uno sprite e' lores: pixel 2x2. Colori
 * 16 + 4 * (n / 2) + 1..3. Total Chaos ci fa i riquadri evidenziati */
extern uint32_t os_sprite[8];
static void disegna_sprite(void) {
    uint32_t sc = screen_davanti();
    uint32_t cm = sc ? R32(sc + SC_VIEWPORT + VP_COLORMAP) : 0;
    for (int n = 0; n < 8; n++) {
        uint32_t s = os_sprite[n];
        if (!s) continue;
        uint32_t dati = R32(s);
        int alto = R16(s + 4), x0 = S16(s + 6), y0 = S16(s + 8);
        if (!dati || alto <= 0 || alto > 256) continue;
        uint32_t col[4] = { 0, 0xFFFFFFFFu, 0xFFAAAAAAu, 0xFF555555u };
        if (cm && R16(cm + 2) >= 32)
            for (int i = 1; i < 4; i++) col[i] = 0xFF000000u | os_cm_rgb24(cm, (uint32_t)(16 + 4 * (n / 2) + i));
        for (int r = 0; r < alto; r++) {
            uint16_t a = R16(dati + 4 + 4u * (uint32_t)r), b = R16(dati + 6 + 4u * (uint32_t)r);
            if (!(a | b)) continue;
            for (int k = 0; k < 16; k++) {
                int v = (a >> (15 - k) & 1) | (b >> (15 - k) & 1) << 1;
                if (!v) continue;
                for (int yy = 0; yy < 2; yy++)
                    for (int xx = 0; xx < 2; xx++) {
                        int X = x0 + 2 * k + xx, Y = y0 + 2 * r + yy;
                        if (X >= 0 && X < VIDEO_L && Y >= 0 && Y < VIDEO_A) s_video[Y * VIDEO_L + X] = col[v];
                    }
            }
        }
    }
}

static void disegna_puntatore(void) {
    uint32_t sc = schermo_sotto_mouse();
    uint32_t col[4] = { 0, 0xFFE04040u, 0xFF000000u, 0xFFEEDDCCu };
    if (sc) {
        uint32_t cm = R32(sc + SC_VIEWPORT + VP_COLORMAP);
        if (cm && R16(cm + 2) >= 20)
            for (int i = 1; i < 4; i++) col[i] = 0xFF000000u | os_cm_rgb24(cm, (uint32_t)(16 + i));
    }
    /* uno sprite e' lores e non interlacciato: pixel 2x2 del video anche su
     * schermi HIRES|LACE o DblPAL (Intuition V39 raddoppia le righe del
     * puntatore), cosi' freccia e mirini hanno la forma giusta */
    int px = 2, py = 2;
    int alto = s_puntatore ? s_ptr_alto : 11;
    int dx = s_puntatore ? s_ptr_dx : 0, dy = s_puntatore ? s_ptr_dy : 0;
    for (int r = 0; r < alto && r < 64; r++) {
        uint16_t a, b;
        if (s_puntatore) { a = R16(s_puntatore + 4 + 4u * (uint32_t)r); b = R16(s_puntatore + 6 + 4u * (uint32_t)r); }
        else { a = s_freccia[r][0]; b = s_freccia[r][1]; }
        for (int k = 0; k < 16; k++) {
            int v = (a >> (15 - k) & 1) | (b >> (15 - k) & 1) << 1;
            if (!v) continue;
            for (int yy = 0; yy < py; yy++)
                for (int xx = 0; xx < px; xx++) {
                    int X = s_mx + (k + dx) * px + xx, Y = s_my + (r + dy) * py + yy;
                    if (X >= 0 && X < VIDEO_L && Y >= 0 && Y < VIDEO_A) s_video[Y * VIDEO_L + X] = col[v];
                }
        }
    }
}

const uint32_t *os_video(int *larghezza, int *altezza) {
    *larghezza = VIDEO_L;
    *altezza = s_video_alto;
    for (int i = 0; i < VIDEO_L * VIDEO_A; i++) s_video[i] = 0xFF000000u;
    /* dal fondo al davanti: ognuno dalla sua TopEdge in giu'. Sopra uno
     * schermo spostato in basso restano 3 righe vuote (il copper cambia
     * modo e palette): lo schermo dietro li' non si vede. Chaos ci conta:
     * la finestra delle informazioni e' alta 33 righe e lo schermo dei
     * testi parte dalla 36. In quelle righe i bitplane sono spenti e si vede
     * il colore 0, gia' quello dello schermo sotto (il copper carica la
     * palette all'inizio dello stacco): nell'introduzione, fra due schermi
     * viola, non c'e' nessuna riga nera.
     * Si disegna solo quel che si vede: per ogni riga del video il primo
     * schermo, dal davanti, che la copre (Total Chaos tiene aperti una decina
     * di schermi 640x512 a 8 piani: disegnarli tutti uno sopra l'altro
     * portava il gioco a 18 quadri al secondo) */
    static uint32_t pal[MAX_SCHERMI][256];
    static int ehb[MAX_SCHERMI], pronta[MAX_SCHERMI];
    memset(pronta, 0, sizeof pronta);
    uint32_t riga[VIDEO_L];
    int ultimo_k = -1, ultima_y = -1;
    for (int v = 0; v < VIDEO_A; v++) {
        int k, stacco = 0;
        for (k = 0; k < s_n_schermi; k++) {
            uint32_t sc = s_schermi[k];
            int top = S16(sc + SC_TOPEDGE) * (sch_lace(sc) ? 1 : 2);
            if (v >= top) break;
            if (k < s_n_schermi - 1 && top > 0 && v >= top - 6) { stacco = 1; break; }
        }
        if (k >= s_n_schermi) continue;                  /* nero */
        uint32_t sc = s_schermi[k];
        if (!pronta[k]) { palette_schermo(sc, pal[k], &ehb[k]); pronta[k] = 1; }
        if (stacco) {
            uint32_t p2[256];
            memcpy(p2, pal[k], sizeof p2);
            copper_utente(sc, 0, p2);
            for (int x = 0; x < VIDEO_L; x++) s_video[v * VIDEO_L + x] = p2[0];
            ultimo_k = -1;
            continue;
        }
        int lace = sch_lace(sc);
        int top = S16(sc + SC_TOPEDGE) * (lace ? 1 : 2);
        int y = (v - top) / (lace ? 1 : 2);
        if (k != ultimo_k || y != ultima_y) {
            uint32_t p2[256];
            memcpy(p2, pal[k], sizeof p2);
            copper_utente(sc, y, p2);
            if (ehb[k]) for (int i = 0; i < 32; i++) p2[32 + i] = 0xFF000000u | ((p2[i] >> 1) & 0x7F7F7F);
            riga_schermo(sc, y, riga, p2, ehb[k]);
            ultimo_k = k; ultima_y = y;
        }
        memcpy(&s_video[v * VIDEO_L], riga, sizeof riga);
    }
    /* il menu aperto, sopra lo schermo della sua finestra */
    if (s_menu_aperto && s_menu_finestra) {
        uint32_t sc = R32(s_menu_finestra + WD_WSCREEN);
        disegna_menu(sc);
        int lace = sch_lace(sc), hires = sch_hires(sc);
        int top = S16(sc + SC_TOPEDGE) * (lace ? 1 : 2);
        /* i colori sono quelli dello schermo */
        uint32_t cm = R32(sc + SC_VIEWPORT + VP_COLORMAP);
        int n_col = cm ? R16(cm + 2) : 0;
        uint32_t pal[256];
        for (int c = 0; c < 256; c++)
            pal[c] = 0xFF000000u | (c < n_col ? os_cm_rgb24(cm, (uint32_t)c) : (c ? 0xFFFFFFu : 0));
        int bpr = R16(s_menu_bm);
        uint32_t piani[8];
        for (int p = 0; p < s_menu_prof; p++) piani[p] = R32(s_menu_bm + 8 + 4u * p);
        for (int r = 0; r < s_menu_n_rett; r++)
            for (int y = s_menu_rett[r][1]; y <= s_menu_rett[r][3] && y < s_menu_alt; y++)
                for (int x = s_menu_rett[r][0]; x <= s_menu_rett[r][2] && x < s_menu_lar; x++) {
                    if (x < 0 || y < 0) continue;
                    uint32_t off = (uint32_t)y * (uint32_t)bpr + (uint32_t)(x >> 3);
                    uint8_t bit = (uint8_t)(0x80 >> (x & 7));
                    int v = 0;
                    for (int p = 0; p < s_menu_prof; p++) if (g_amiga_mem[piani[p] + off] & bit) v |= 1 << p;
                    uint32_t c = pal[v];
                    for (int yy = 0; yy < (lace ? 1 : 2); yy++)
                        for (int xx = 0; xx < (hires ? 1 : 2); xx++) {
                            int X = x * (hires ? 1 : 2) + xx, Y = top + y * (lace ? 1 : 2) + yy;
                            if (X < VIDEO_L && Y < VIDEO_A) s_video[Y * VIDEO_L + X] = c;
                        }
                }
    }
    disegna_sprite();
    disegna_puntatore();
    return s_video;
}

/* lo stato degli schermi su stderr (fine delle prove senza finestra) */
void os_intuition_stato(void) {
    for (int i = 0; i < s_n_schermi; i++) {
        int rx, ry;
        uint32_t sc = s_schermi[i], bm = bitmap_visibile(sc, &rx, &ry), cm = R32(sc + SC_VIEWPORT + VP_COLORMAP);
        int prof = R8(bm + 5), bpr = R16(bm), righe = R16(bm + 2);
        long pieni = 0;
        for (int p = 0; p < prof && p < 8; p++) {
            uint32_t pl = R32(bm + 8 + 4u * p), dim = (uint32_t)bpr * (uint32_t)righe;
            if (!dim || !os_e_ram(pl) || !os_e_ram(pl + dim - 1)) continue;   /* piani finti (-1) o liberati */
            for (uint32_t k = 0; k < dim; k++) pieni += g_amiga_mem[pl + k] != 0;
        }
        fprintf(stderr, "[intui] schermo %d $%06X %dx%d prof %d modi $%04X top %d, byte non nulli %ld, colori:",
                i, sc, S16(sc + SC_WIDTH), S16(sc + SC_HEIGHT), prof, R16(sc + SC_VIEWPORT + VP_MODES),
                S16(sc + SC_TOPEDGE), pieni);
        for (int c = 0; c < 8 && cm; c++) fprintf(stderr, " %03X", R16(R32(cm + 4) + 2u * (uint32_t)c));
        fprintf(stderr, "\n        piani:");
        for (int p = 0; p < prof && p < 8; p++) fprintf(stderr, " $%06X", R32(bm + 8 + 4u * p));
        fprintf(stderr, "\n");
        for (uint32_t w = R32(sc + SC_FIRSTWINDOW); w; w = R32(w + WD_NEXTWINDOW)) {
            uint32_t rbm = R32(R32(w + WD_RPORT) + 4);
            fprintf(stderr, "        finestra $%06X %d,%d %dx%d idcmp $%08X bitmap $%06X piano0 $%06X\n", w,
                    S16(w + WD_LEFTEDGE), S16(w + WD_TOPEDGE), S16(w + WD_WIDTH), S16(w + WD_HEIGHT),
                    R32(w + WD_IDCMPFLAGS), rbm, rbm ? R32(rbm + 8) : 0);
        }
    }
}

/* ---------------------------------------------------------- V36: tag */

/* la voce dopo *pp di una lista di tag (TAG_DONE 0, IGNORE 1, MORE 2, SKIP 3) */
static uint32_t tag_prossimo(uint32_t *pp) {
    uint32_t p = *pp;
    while (p) {
        uint32_t t = R32(p);
        if (t == 0) break;
        if (t == 1) { p += 8; continue; }
        if (t == 2) { p = R32(p + 4); continue; }
        if (t == 3) { p += 8u * (R32(p + 4) + 1); continue; }
        *pp = p + 8;
        return p;
    }
    *pp = 0;
    return 0;
}

#define SA_LEFT       0x80000021u
#define SA_DISPLAYID  0x80000032u
#define SA_COLORS32   0x80000043u
#define NS_EXTENDED   0x1000

static void i_openscreentaglist(void) {
    /* OpenScreenTagList(NewScreen A0 o NULL, tag A1): i tag diventano una
     * NewScreen (32 byte) e si apre come OpenScreen. DisplayID: HIRES $8000,
     * LACE $4; con piu' di 300 righe il video le mostra 1:1 (LACE), come il
     * DblPAL a 31 kHz */
    uint32_t ns0 = A0, tags = A1;
    uint32_t ns = os_alloca(32, MEMF_PUBLIC | MEMF_CLEAR);
    if (ns0) memcpy(g_amiga_mem + ns, g_amiga_mem + ns0, 32);
    else { W16(ns + 8, 1); W16(ns + 14, 0x000F); W8(ns + 11, 1); }   /* profondita' 1, CUSTOMSCREEN, BlockPen 1 */
    uint32_t penne = 0;                                   /* SA_Pens */
    uint32_t liste[2] = { tags, ns0 && (R16(ns0 + 14) & NS_EXTENDED) ? R32(ns0 + 32) : 0 };
    uint32_t id = 0xFFFFFFFFu, colori = 0, colori32 = 0, errore = 0;
    int interleaved = 0;
    for (int l = 1; l >= 0; l--) {                        /* prima l'ExtNewScreen, poi i tag */
        uint32_t p = liste[l], t;
        while ((t = tag_prossimo(&p)) != 0) {
            uint32_t tag = R32(t), v = R32(t + 4);
            switch (tag) {
            case 0x80000021u: W16(ns, v); break;                         /* SA_Left */
            case 0x80000022u: W16(ns + 2, v); break;                     /* SA_Top */
            case 0x80000023u: W16(ns + 4, v); break;                     /* SA_Width */
            case 0x80000024u: W16(ns + 6, v); break;                     /* SA_Height */
            case 0x80000025u: W16(ns + 8, v); break;                     /* SA_Depth */
            case 0x80000026u: W8(ns + 10, v); break;                     /* SA_DetailPen */
            case 0x80000027u: W8(ns + 11, v); break;                     /* SA_BlockPen */
            case 0x80000028u: W32(ns + 20, v); break;                    /* SA_Title */
            case 0x80000029u: colori = v; break;                         /* SA_Colors */
            case 0x8000002Au: errore = v; break;                         /* SA_ErrorCode */
            case 0x8000002Bu: W32(ns + 16, v); break;                    /* SA_Font */
            case 0x8000002Du: W16(ns + 14, (R16(ns + 14) & ~0x000Fu) | (v & 0x000F)); break;  /* SA_Type */
            case 0x8000002Eu: W32(ns + 28, v); W16(ns + 14, R16(ns + 14) | CUSTOMBITMAP); break;  /* SA_BitMap */
            case SA_DISPLAYID: id = v; break;
            case 0x80000036u: W16(ns + 14, v ? (R16(ns + 14) | SHOWTITLE) : (R16(ns + 14) & ~SHOWTITLE)); break;
            case 0x80000037u: W16(ns + 14, v ? (R16(ns + 14) | SCREENBEHIND) : (R16(ns + 14) & ~SCREENBEHIND)); break;
            case 0x8000003Au: penne = v; break;                          /* SA_Pens */
            case 0x80000042u: interleaved = v != 0; break;               /* SA_Interleaved */
            case SA_COLORS32: colori32 = v; break;
            default: break;                                              /* gli altri non cambiano il disegno */
            }
        }
    }
    if (id != 0xFFFFFFFFu) {
        uint16_t modi = R16(ns + 12) & ~(HIRES | LACE | 0x0800 | 0x0080);
        if (id & 0x8000) modi |= HIRES;
        if (id & 0x0004) modi |= LACE;
        modi |= id & (0x0800 | 0x0080);                  /* HAM, EXTRA_HALFBRITE */
        W16(ns + 12, modi);
    }
    if (S16(ns + 6) > 300) W16(ns + 12, R16(ns + 12) | LACE);
    if (S16(ns + 4) > 400) W16(ns + 12, R16(ns + 12) | HIRES);
    if (os_traccia)
        fprintf(stderr, "[intui] OpenScreenTagList %dx%dx%d id $%08X interleaved %d colori32 $%06X\n",
                S16(ns + 4), S16(ns + 6), S16(ns + 8), id, interleaved, colori32);
    uint32_t a0 = A0;
    A0 = ns;
    s_ns_interleaved = interleaved;
    i_openscreen();
    s_ns_interleaved = 0;
    A0 = a0;
    uint32_t sc = D0;
    os_libera(ns, 32);
    if (errore) W32(errore, sc ? 0 : 1);                  /* OSERR_NOMONITOR, il primo */
    if (!sc) return;
    uint32_t cm = R32(sc + SC_VIEWPORT + VP_COLORMAP);
    /* SA_Colors: ColorSpec (WORD indice, UWORD r, g, b a 4 bit) fino a -1 */
    for (uint32_t c = colori; c && S16(c) != -1; c += 8) {
        uint32_t r = R16(c + 2) & 15, g = R16(c + 4) & 15, b = R16(c + 6) & 15;
        os_cm_colore24(cm, R16(c), (r * 17) << 16 | (g * 17) << 8 | (b * 17));
    }
    if (colori32) os_loadrgb32_cm(cm, colori32);
    /* le penne di DrawInfo: quelle del "nuovo aspetto" di V39, cambiate da
     * SA_Pens fino a ~0 */
    static const uint16_t nuove[N_PENNE] = {0, 1, 1, 2, 1, 3, 1, 0, 2, 1, 2, 1};
    penne_t *pe = penne_di(sc, 1);
    if (pe) {
        memcpy(pe->p, nuove, sizeof pe->p);
        for (int i = 0; penne && i < N_PENNE && R16(penne + 2u * i) != 0xFFFF; i++) pe->p[i] = R16(penne + 2u * i);
        if (os_traccia && penne) {
            fprintf(stderr, "[intui] SA_Pens:");
            for (int i = 0; i < 16 && R16(penne + 2u * i) != 0xFFFF; i++) fprintf(stderr, " %u", R16(penne + 2u * i));
            fprintf(stderr, " (barra %u/%u)\n", pe->p[9], pe->p[10]);
        }
    }
    D0 = sc;
}

static void i_screendepth(void) {
    /* ScreenDepth(screen A0, flag D0, riservato A1): SDEPTH_TOBACK 1, altrimenti
     * davanti */
    if (D0 & 1) i_screentoback(); else i_screentofront();
}
static void i_screenposition(void) {
    /* ScreenPosition(screen A0, flag D0, x1 D1, y1 D2, x2 D3, y2 D4):
     * SPOS_ABSOLUTE 1 = posizione, altrimenti spostamento */
    uint32_t sc = A0;
    int dx = (int16_t)D1, dy = (int16_t)D2;
    if (D0 & 1) { dx -= S16(sc + SC_LEFTEDGE); dy -= S16(sc + SC_TOPEDGE); }
    uint32_t d0 = D0, d1 = D1;
    D0 = (uint32_t)dx; D1 = (uint32_t)dy;
    i_movescreen();
    D0 = d0; D1 = d1;
}

#define WA_LEFT 0x80000064u
static void i_openwindowtaglist(void) {
    /* OpenWindowTagList(NewWindow A0 o NULL, tag A1): come OpenWindow con una
     * NewWindow (48 byte) fatta dai tag */
    uint32_t nw0 = A0, tags = A1;
    uint32_t nw = os_alloca(48, MEMF_PUBLIC | MEMF_CLEAR);
    if (nw0) memcpy(g_amiga_mem + nw, g_amiga_mem + nw0, 48);
    else { W8(nw + 8, 0xFF); W8(nw + 9, 0xFF); W16(nw + 46, 1); }   /* penne dello schermo, WBENCHSCREEN */
    uint32_t p = tags, t;
    int lar_interna = -1, alt_interna = -1;
    while ((t = tag_prossimo(&p)) != 0) {
        uint32_t tag = R32(t), v = R32(t + 4), fl = R32(nw + 14);
        uint32_t bit = 0;
        switch (tag) {
        case 0x80000064u: W16(nw, v); break;                             /* WA_Left */
        case 0x80000065u: W16(nw + 2, v); break;                         /* WA_Top */
        case 0x80000066u: W16(nw + 4, v); break;                         /* WA_Width */
        case 0x80000067u: W16(nw + 6, v); break;                         /* WA_Height */
        case 0x80000068u: W8(nw + 8, v); break;                          /* WA_DetailPen */
        case 0x80000069u: W8(nw + 9, v); break;                          /* WA_BlockPen */
        case 0x8000006Au: W32(nw + 10, v); break;                        /* WA_IDCMP */
        case 0x8000006Bu: W32(nw + 14, v); break;                        /* WA_Flags */
        case 0x8000006Cu: W32(nw + 18, v); break;                        /* WA_Gadgets */
        case 0x8000006Du: W32(nw + 22, v); break;                        /* WA_Checkmark */
        case 0x8000006Eu: W32(nw + 26, v); break;                        /* WA_Title */
        case 0x80000070u: W32(nw + 30, v); W16(nw + 46, 0x000F); break;  /* WA_CustomScreen */
        case 0x80000071u: W32(nw + 34, v); W32(nw + 14, fl | 0x80); break;  /* WA_SuperBitMap */
        case 0x80000072u: W16(nw + 38, v); break;
        case 0x80000073u: W16(nw + 40, v); break;
        case 0x80000074u: W16(nw + 42, v); break;
        case 0x80000075u: W16(nw + 44, v); break;
        case 0x80000076u: lar_interna = (int)v; break;                   /* WA_InnerWidth */
        case 0x80000077u: alt_interna = (int)v; break;                   /* WA_InnerHeight */
        case 0x80000079u: if (v) { W32(nw + 30, v); W16(nw + 46, 0x000F); } break;   /* WA_PubScreen */
        case 0x80000080u: bit = WFLG_SIZEGADGET; break;
        case 0x80000081u: bit = WFLG_DRAGBAR; break;
        case 0x80000082u: bit = WFLG_DEPTHGADGET; break;
        case 0x80000083u: bit = WFLG_CLOSEGADGET; break;
        case 0x80000084u: bit = WFLG_BACKDROP; break;
        case 0x80000085u: bit = WFLG_REPORTMOUSE; break;
        case 0x80000086u: bit = 0x20000; break;                          /* WA_NoCareRefresh */
        case 0x80000087u: bit = WFLG_BORDERLESS; break;
        case 0x80000088u: bit = WFLG_ACTIVATE; break;
        case 0x80000089u: bit = WFLG_RMBTRAP; break;
        case 0x8000008Bu: bit = WFLG_SIMPLE_REFRESH; break;
        case 0x80000090u: bit = WFLG_GIMMEZEROZERO; break;
        default: break;
        }
        if (bit) W32(nw + 14, v ? (fl | bit) : (fl & ~bit));
    }
    if (lar_interna >= 0 || alt_interna >= 0) {
        /* misure interne: si aggiungono i bordi (come OpenWindow li mette) */
        int senza = (R32(nw + 14) & WFLG_BORDERLESS) != 0;
        if (lar_interna >= 0) W16(nw + 4, lar_interna + (senza ? 0 : 8));
        if (alt_interna >= 0) W16(nw + 6, alt_interna + (senza ? 0 : 13));
    }
    if (os_traccia)
        fprintf(stderr, "[intui] OpenWindowTagList %d,%d %dx%d flag $%08X schermo $%06X\n",
                S16(nw), S16(nw + 2), S16(nw + 4), S16(nw + 6), R32(nw + 14), R32(nw + 30));
    uint32_t a0 = A0;
    A0 = nw;
    i_openwindow();
    A0 = a0;
    os_libera(nw, 48);
}

/* ---------------------------------------------------------- tabella */

#define F(lvo, fn) [(lvo) / 6 - 1] = fn
static const os_funzione s_fn[N_INTUI_V40] = {
    F(30, i_niente), F(42, i_addgadget), F(48, i_niente), F(54, i_clearmenustrip),
    F(60, i_clearpointer), F(66, i_closescreen), F(72, i_closewindow), F(78, i_niente),
    F(84, i_currenttime), F(90, i_displayalert), F(96, i_displaybeep), F(102, i_doubleclick),
    F(108, i_drawborder), F(114, i_drawimage), F(120, i_endrequest), F(126, i_getprefs),
    F(132, i_getprefs), F(138, i_initrequester), F(144, i_itemaddress), F(150, i_modifyidcmp),
    F(156, i_modifyprop), F(162, i_movescreen), F(168, i_movewindow), F(174, i_offgadget),
    F(180, i_offmenu), F(186, i_ongadget), F(192, i_onmenu), F(198, i_openscreen),
    F(204, i_openwindow), F(210, i_niente), F(216, i_printitext), F(222, i_refreshgadgets),
    F(228, i_removegadget), F(234, i_reportmouse), F(240, i_request), F(246, i_screentoback),
    F(252, i_screentofront), F(258, i_niente), F(264, i_setmenustrip), F(270, i_setpointer),
    F(276, i_setwindowtitles), F(282, i_showtitle), F(288, i_sizewindow), F(294, i_viewaddress),
    F(300, i_viewportaddress), F(306, i_niente), F(312, i_niente), F(318, i_windowlimits),
    F(324, i_niente), F(330, i_intuitextlength), F(336, i_niente), F(342, i_niente),
    F(348, i_autorequest), F(354, i_niente), F(360, i_buildsysrequest), F(366, i_niente),
    F(372, i_niente), F(378, i_niente), F(384, i_niente), F(390, i_niente),
    F(396, i_allocremember), F(408, i_freeremember), F(414, i_lockibase), F(420, i_niente),
    F(426, i_getscreendata), F(432, i_refreshglist), F(438, i_addglist), F(444, i_removeglist),
    F(450, i_activatewindow), F(456, i_niente), F(462, i_activategadget), F(468, i_modifyprop),
    /* V36-V40 */
    F(606, i_openwindowtaglist), F(612, i_openscreentaglist), F(786, i_screendepth),
    F(792, i_screenposition),
};

void os_intuition_init(void) {
    static const os_libreria_c def = { "intuition.library", 40, N_INTUI_V40, 0x100, os_nomi_intuition, s_fn };
    s_ibase = os_crea_libreria(&def, NT_LIBRARY);
}
