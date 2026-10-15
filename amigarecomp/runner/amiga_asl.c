/* amiga_asl.c - asl.library (V40) in C, al posto di quella del Workbench.
 *
 * Quella vera vuole gadtools.library e BOOPSI (in ROM sul Kickstart 3.1).
 * Qui le richieste si allocano come asl (FileRequester V38: fr_File 4,
 * fr_Drawer 8, fr_NumArgs 32, fr_ArgList 36, fr_Pattern 52) con i valori
 * iniziali dei tag; AslRequest per ora risponde "annullato" e lo scrive su
 * stderr. Il requester vero (per Load/Save di Total Chaos) e' da fare.
 */
#include <stdio.h>
#include <string.h>
#include "genesis_runtime.h"
#include "amiga_os.h"

#define ASL_TB                0x80080000u
#define ASLFR_INITIALFILE     (ASL_TB + 8)
#define ASLFR_INITIALDRAWER   (ASL_TB + 9)
#define ASLFR_INITIALPATTERN  (ASL_TB + 10)
#define DIM_RICHIESTA         128
#define DIM_STRINGA           256

/* il valore di un tag in una lista semplice (TAG_MORE e TAG_SKIP compresi) */
static uint32_t tag(uint32_t lista, uint32_t t, uint32_t predefinito) {
    for (uint32_t p = lista; p;) {
        uint32_t k = R32(p);
        if (k == 0) break;
        if (k == 1) { p += 8; continue; }
        if (k == 2) { p = R32(p + 4); continue; }
        if (k == 3) { p += 8u * (R32(p + 4) + 1); continue; }
        if (k == t) return R32(p + 4);
        p += 8;
    }
    return predefinito;
}

static void copia_stringa(uint32_t dest, uint32_t src) {
    uint32_t i = 0;
    if (src) for (; i + 1 < DIM_STRINGA && R8(src + i); i++) W8(dest + i, R8(src + i));
    W8(dest + i, 0);
}

static void a_allocaslrequest(void) {
    /* AllocAslRequest(tipo D0, tag A0): 0 file, 1 font, 2 modo video */
    uint32_t r = os_alloca(DIM_RICHIESTA + 3 * DIM_STRINGA, MEMF_PUBLIC | MEMF_CLEAR);
    if (!r) { D0 = 0; return; }
    W32(r + DIM_RICHIESTA - 4, D0);                    /* il tipo, in fondo (riservato) */
    if (D0 == 0) {
        uint32_t file = r + DIM_RICHIESTA, cart = file + DIM_STRINGA, schema = cart + DIM_STRINGA;
        copia_stringa(file, tag(A0, ASLFR_INITIALFILE, 0));
        copia_stringa(cart, tag(A0, ASLFR_INITIALDRAWER, 0));
        copia_stringa(schema, tag(A0, ASLFR_INITIALPATTERN, 0));
        W32(r + 4, file); W32(r + 8, cart); W32(r + 52, schema);
    }
    D0 = r;
}
static void a_freeaslrequest(void) {
    if (A0) os_libera(A0, DIM_RICHIESTA + 3 * DIM_STRINGA);
}
static void a_allocfilerequest(void) { D0 = 0; A0 = 0; a_allocaslrequest(); }
static void a_aslrequest(void) {
    /* AslRequest(richiesta A0, tag A1): 0 = annullato */
    fprintf(stderr, "[asl] AslRequest (tipo %u): per ora annullato\n", A0 ? R32(A0 + DIM_RICHIESTA - 4) : 0);
    D0 = 0;
}

#define F(lvo, fn) [(lvo) / 6 - 1] = fn
#define N(lvo, nome) [(lvo) / 6 - 1] = nome
static const os_funzione s_fn[12] = {
    F(30, a_allocfilerequest), F(36, a_freeaslrequest), F(42, a_aslrequest),
    F(48, a_allocaslrequest), F(54, a_freeaslrequest), F(60, a_aslrequest),
};
static const char *const s_nomi[12] = {
    N(6, "Open"), N(12, "Close"), N(18, "Expunge"), N(24, "Reserved"),
    N(30, "AllocFileRequest"), N(36, "FreeFileRequest"), N(42, "RequestFile"),
    N(48, "AllocAslRequest"), N(54, "FreeAslRequest"), N(60, "AslRequest"),
    N(66, "AbortAslRequest"), N(72, "ActivateAslRequest"),
};

void os_asl_init(void) {
    static const os_libreria_c def = { "asl.library", 40, 12, 34, s_nomi, s_fn };
    os_crea_libreria(&def, NT_LIBRARY);
}
