/* amiga_diskfont.c - diskfont.library e translator.library in C.
 *
 * diskfont: OpenDiskFont legge il file .font (FontContentsHeader), sceglie
 * l'altezza chiesta (o la piu' vicina), carica il file del font con LoadSeg
 * e mette in GfxBase->TextFonts la TextFont che c'e' dentro (DiskFontHeader
 * dopo il "moveq #-1,d0 / rts" iniziale). Il gioco apre cosi' i suoi font in
 * PROGDIR:fonts/.
 *
 * translator: il gioco la apre con -t ma non la usa (i nomi sono campioni):
 * Translate restituisce una frase vuota.
 *
 * Prima erano le librerie del Workbench 3.1 ricompilate col gioco.
 */
#include "amiga_os.h"
#include "amiga_grafica.h"
#include "genesis_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

enum { TF_YSIZE = 20, TF_FLAGS = 23, TF_ACCESSORS = 30 };
#define FPF_DISKFONT 0x02
#define DFH_ID 0x0F80

static const char *parte_file(const char *p) {
    const char *s = p;
    for (const char *c = p; *c; c++) if (*c == '/' || *c == ':') s = c + 1;
    return s;
}

/* un font gia' in memoria con quel nome (senza cartella) e altezza */
static uint32_t gia_caricato(const char *nome, unsigned alto) {
    for (uint32_t n = R32(os_gfxbase + 140); n && R32(n + LN_SUCC); n = R32(n + LN_SUCC)) {
        char b[64];
        os_leggi_stringa(R32(n + LN_NAME), b, sizeof b);
        if (!strcasecmp(parte_file(b), parte_file(nome)) && R16(n + TF_YSIZE) == alto) return n;
    }
    return 0;
}

/* OpenDiskFont(textAttr A0) */
static void f_opendiskfont(void) {
    char nome[256], percorso[300];
    os_leggi_stringa(R32(A0), nome, sizeof nome);
    unsigned alto = R16(A0 + 4);
    uint32_t tf = gia_caricato(nome, alto);
    if (tf) { W16(tf + TF_ACCESSORS, R16(tf + TF_ACCESSORS) + 1); D0 = tf; return; }
    /* senza cartella: in FONTS: */
    if (strchr(nome, ':') || strchr(nome, '/')) snprintf(percorso, sizeof percorso, "%s", nome);
    else snprintf(percorso, sizeof percorso, "FONTS:%s", nome);
    FILE *f = os_dos_apri_host(percorso, "rb");
    uint8_t intest[4];
    if (!f || fread(intest, 1, 4, f) != 4) {
        if (f) fclose(f);
        D0 = 0;
        if (os_traccia) fprintf(stderr, "[diskfont] OpenDiskFont(\"%s\", %u): manca il file\n", nome, alto);
        return;
    }
    unsigned id = (unsigned)(intest[0] << 8 | intest[1]), n = (unsigned)(intest[2] << 8 | intest[3]);
    /* FontContents: fc_FileName[256], fc_YSize, fc_Style, fc_Flags (260 byte) */
    char scelto[256] = "";
    unsigned miglior = 0;
    for (unsigned i = 0; i < n && (id == 0x0F00 || id == 0x0F02 || id == 0x0F03); i++) {
        uint8_t c[260];
        if (fread(c, 1, 260, f) != 260) break;
        unsigned y = (unsigned)(c[256] << 8 | c[257]);
        int meglio = !scelto[0] || (y == alto) ||
                     (miglior != alto && abs((int)y - (int)alto) < abs((int)miglior - (int)alto));
        if (meglio) { memcpy(scelto, c, 255); scelto[255] = 0; miglior = y; }
    }
    fclose(f);
    if (!scelto[0]) { D0 = 0; return; }
    /* il file del font accanto al .font ("ChaosPearl/16") */
    char file[600];
    size_t cart = (size_t)(parte_file(percorso) - percorso);
    snprintf(file, sizeof file, "%.*s%s", (int)cart, percorso, scelto);
    uint32_t seg = os_dos_carica_segmenti(file);
    if (!seg) { D0 = 0; return; }
    uint32_t codice = BADDR(seg) + 4, dfh = codice + 4;
    if (R16(dfh + 14) != DFH_ID) {
        fprintf(stderr, "[diskfont] %s non e' un font (id $%04X)\n", file, R16(dfh + 14));
        D0 = 0;
        return;
    }
    W32(dfh + 18, seg);                                    /* dfh_Segment */
    tf = dfh + 54;                                         /* dfh_TF */
    W8(tf + TF_FLAGS, R8(tf + TF_FLAGS) | FPF_DISKFONT);
    W16(tf + TF_ACCESSORS, 1);
    W8(tf + LN_TYPE, 12);                                  /* NT_FONT */
    os_aggiungi_coda(os_gfxbase + 140, tf);                /* AddFont */
    if (os_traccia)
        fprintf(stderr, "[diskfont] OpenDiskFont(\"%s\", %u) = $%06X (%s, alto %u)\n", nome, alto, tf, file,
                R16(tf + TF_YSIZE));
    D0 = tf;
}

/* AvailFonts(buffer A0, dim D0, tipi D1): nessun font su disco da elencare */
static void f_availfonts(void) {
    if (D0 >= 2) W16(A0, 0);
    D0 = 0;
}

#define F(lvo, fn) [(lvo) / 6 - 1] = fn
#define N(lvo, nome) [(lvo) / 6 - 1] = nome
static const os_funzione s_fn_df[10] = { F(30, f_opendiskfont), F(36, f_availfonts) };
static const char *const s_nomi_df[10] = {
    N(6, "Open"), N(12, "Close"), N(18, "Expunge"), N(24, "Reserved"), N(30, "OpenDiskFont"),
    N(36, "AvailFonts"), N(42, "NewFontContents"), N(48, "DisposeFontContents"), N(54, "NewScaledDiskFont"),
    N(60, "GetDiskFontCtrl"),
};

/* Translate(testo A0, lung D0, buffer A1, dim D1): frase vuota */
static void t_translate(void) {
    if (D1) W8(A1, 0);
    D0 = 0;
}
static const os_funzione s_fn_tr[5] = { F(30, t_translate) };
static const char *const s_nomi_tr[5] = {
    N(6, "Open"), N(12, "Close"), N(18, "Expunge"), N(24, "Reserved"), N(30, "Translate"),
};

void os_diskfont_init(void) {
    static const os_libreria_c df = { "diskfont.library", 40, 10, 34, s_nomi_df, s_fn_df };
    static const os_libreria_c tr = { "translator.library", 37, 5, 34, s_nomi_tr, s_fn_tr };
    os_crea_libreria(&df, NT_LIBRARY);
    os_crea_libreria(&tr, NT_LIBRARY);
}
