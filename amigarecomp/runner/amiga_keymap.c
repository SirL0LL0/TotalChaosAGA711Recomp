/* amiga_keymap.c - keymap.library (V40) in C: sul Kickstart 3.1 sta in ROM.
 *
 * Total Chaos la apre per tradurre i tasti (MapRawKey). La keymap e' quella
 * USA del runner (os_ascii_di in amiga_intuition.c); frecce, tasti funzione
 * e Help danno le sequenze CSI come la keymap "usa" dell'Amiga.
 * InputEvent: ie_Class 4, ie_Code 6, ie_Qualifier 8.
 */
#include <stdio.h>
#include <string.h>
#include "genesis_runtime.h"
#include "amiga_os.h"

int os_ascii_di(int raw, uint16_t q);

static uint32_t s_keymap;                 /* una KeyMap vuota: per AskKeyMapDefault */

static void k_setkeymapdefault(void) {}
static void k_askkeymapdefault(void) { D0 = s_keymap; }

/* i caratteri di un tasto: n byte in out, 0 = niente */
static int traduci(int raw, uint16_t q, char *out) {
    if (raw >= 0x4C && raw <= 0x4F) {                  /* frecce: su, giu', destra, sinistra */
        out[0] = (char)0x9B; out[1] = "ABCD"[raw - 0x4C];
        return 2;
    }
    if (raw >= 0x50 && raw <= 0x59) {                  /* F1-F10 (con Shift: 10-19) */
        int n = raw - 0x50 + ((q & 3) ? 10 : 0);
        return sprintf(out, "\x9b%d~", n);
    }
    if (raw == 0x5F) { memcpy(out, "\x9b?~", 3); return 3; }
    int c = os_ascii_di(raw, q);
    if (c < 0) return 0;
    if (q & 8) c &= 0x1F;                              /* Ctrl */
    out[0] = (char)c;
    return 1;
}

static void k_maprawkey(void) {
    /* MapRawKey(event A0, buffer A1, length D1, keymap A2): byte scritti,
     * -1 se non ci stanno */
    uint32_t ev = A0, buf = A1;
    int32_t lung = (int32_t)D1;
    uint16_t codice = R16(ev + 6), q = R16(ev + 8);
    if (R8(ev + 4) != 1 || (codice & 0x80)) { D0 = 0; return; }   /* solo IECLASS_RAWKEY, tasto giu' */
    char s[8];
    int n = traduci(codice & 0x7F, q, s);
    if (n > lung) { D0 = (uint32_t)-1; return; }
    for (int i = 0; i < n; i++) W8(buf + (uint32_t)i, s[i]);
    D0 = (uint32_t)n;
}

#define F(lvo, fn) [(lvo) / 6 - 1] = fn
static const os_funzione s_fn[8] = {
    F(30, k_setkeymapdefault), F(36, k_askkeymapdefault), F(42, k_maprawkey),
};
static const char *const s_nomi[8] = {
    "Open", "Close", "Expunge", "Reserved", "SetKeyMapDefault", "AskKeyMapDefault", "MapRawKey", "MapANSI",
};

void os_keymap_init(void) {
    static const os_libreria_c def = { "keymap.library", 40, 8, 34 + 32, s_nomi, s_fn };
    os_crea_libreria(&def, NT_LIBRARY);
    s_keymap = os_alloca(32, MEMF_PUBLIC | MEMF_CLEAR);
}
