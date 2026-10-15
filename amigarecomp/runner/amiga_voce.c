/* amiga_voce.c - la voce del narrator.device con la sintesi vocale di Windows.
 *
 * Il narrator dell'Amiga non legge testo ma fonemi (quelli che translator.library
 * ricava dall'inglese, o scritti a mano dal programma): "PLEY5IXAX PIHKAARD."
 * Qui diventano fonemi SAPI (insieme americano: aa ae ah ... zh, accento 1/2)
 * dentro <pron sym="..."/>, una parola per tag, e la voce di Windows li dice
 * in sottofondo. voce_finita() dice quando ha finito, per rispondere alla
 * richiesta di scrittura solo allora, come il vero narrator.
 *
 * AMIGA_VOCE=0 la spegne (le frasi si completano subito, mute).
 */
#define COBJMACROS
#include <windows.h>
#include <sapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "amiga_voce.h"

static ISpVoice *s_voce;
static int s_pronta, s_spenta;
static int s_sesso = -1;                  /* voce scelta: 0 maschio, 1 femmina */

/* la prima voce che risponde ai requisiti SAPI ("Gender=Male;Language=409") */
static int voce_con(const wchar_t *requisiti) {
    ISpObjectTokenCategory *cat = NULL;
    IEnumSpObjectTokens *elenco = NULL;
    ISpObjectToken *voce = NULL;
    int ok = 0;
    if (FAILED(CoCreateInstance(&CLSID_SpObjectTokenCategory, NULL, CLSCTX_ALL,
                                &IID_ISpObjectTokenCategory, (void **)&cat))) return 0;
    if (SUCCEEDED(ISpObjectTokenCategory_SetId(cat, SPCAT_VOICES, FALSE)) &&
        SUCCEEDED(ISpObjectTokenCategory_EnumTokens(cat, requisiti, NULL, &elenco)) &&
        IEnumSpObjectTokens_Next(elenco, 1, &voce, NULL) == S_OK) {
        ok = SUCCEEDED(ISpVoice_SetVoice(s_voce, voce));
        ISpObjectToken_Release(voce);
    }
    if (elenco) IEnumSpObjectTokens_Release(elenco);
    ISpObjectTokenCategory_Release(cat);
    return ok;
}

/* i fonemi sono inglesi: serve una voce inglese (americana, poi britannica),
 * del sesso chiesto se c'e'; la voce di sistema puo' essere italiana */
static void scegli_voce(int femmina) {
    if (voce_con(femmina ? L"Gender=Female;Language=409" : L"Gender=Male;Language=409")) return;
    if (voce_con(L"Language=409")) return;
    if (voce_con(femmina ? L"Gender=Female;Language=809" : L"Gender=Male;Language=809")) return;
    voce_con(L"Language=809");
}

static int prepara(void) {
    if (s_pronta) return s_voce != NULL;
    s_pronta = 1;
    const char *e = getenv("AMIGA_VOCE");
    if (e && *e == '0') { s_spenta = 1; return 0; }
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(CoCreateInstance(&CLSID_SpVoice, NULL, CLSCTX_ALL, &IID_ISpVoice, (void **)&s_voce))) s_voce = NULL;
    return s_voce != NULL;
}

/* i fonemi dell'Amiga, i piu' lunghi prima; NULL = si salta (colpo di glottide) */
static const struct { const char *amiga, *sapi; } s_fonemi[] = {
    { "/H", "h" }, { "/C", "h" }, { "NX", "ng" }, { "DX", "t" }, { "QX", NULL },
    { "RX", "r" }, { "LX", "l" }, { "UL", "ax l" }, { "UM", "ax m" }, { "UN", "ax n" },
    { "IY", "iy" }, { "IH", "ih" }, { "EH", "eh" }, { "AE", "ae" }, { "AA", "aa" },
    { "AH", "ah" }, { "AO", "ao" }, { "UH", "uh" }, { "ER", "er" }, { "OH", "ao" },
    { "AX", "ax" }, { "IX", "ih" }, { "EY", "ey" }, { "AY", "ay" }, { "OY", "oy" },
    { "AW", "aw" }, { "OW", "ow" }, { "UW", "uw" }, { "TH", "th" }, { "DH", "dh" },
    { "SH", "sh" }, { "ZH", "zh" }, { "CH", "ch" },
    { "R", "r" }, { "L", "l" }, { "W", "w" }, { "Y", "y" }, { "M", "m" }, { "N", "n" },
    { "B", "b" }, { "D", "d" }, { "G", "g" }, { "P", "p" }, { "T", "t" }, { "K", "k" },
    { "F", "f" }, { "V", "v" }, { "S", "s" }, { "Z", "z" }, { "J", "jh" }, { "Q", NULL },
};

static int vocale(const char *s) {
    static const char *const v[] = { "iy", "ih", "eh", "ae", "aa", "ah", "ao", "uh", "er", "ax",
                                     "ey", "ay", "oy", "aw", "ow", "uw" };
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) if (!strcmp(s, v[i])) return 1;
    return 0;
}

/* fonemi Amiga -> XML per SAPI; parole in <pron>, punteggiatura in pause */
static void converti(const char *in, int n, wchar_t *out, size_t max) {
    char xml[4096], parola[512];
    size_t x = 0, p = 0;
    int ultima_vocale = 0;
    xml[0] = 0;
#define METTI(s) do { const char *_s = (s); size_t _l = strlen(_s); \
                      if (x + _l < sizeof xml) { memcpy(xml + x, _s, _l); x += _l; xml[x] = 0; } } while (0)
#define CHIUDI_PAROLA() do { if (p) { parola[p] = 0; METTI("<pron sym=\""); METTI(parola); METTI("\"/>"); p = 0; } \
                             ultima_vocale = 0; } while (0)
    for (int i = 0; i < n && in[i];) {
        char c = in[i];
        if (c >= '1' && c <= '9') {                  /* accento sulla vocale appena detta */
            if (ultima_vocale && p + 3 < sizeof parola) { parola[p++] = ' '; parola[p++] = c >= '5' ? '1' : '2'; }
            ultima_vocale = 0;
            i++;
            continue;
        }
        if (c == ' ') { CHIUDI_PAROLA(); METTI(" "); i++; continue; }
        if (c == '.' || c == '?' || c == '!' ) { CHIUDI_PAROLA(); METTI("<silence msec=\"350\"/>"); i++; continue; }
        if (c == ',' || c == '-' || c == '(' || c == ')') { CHIUDI_PAROLA(); METTI("<silence msec=\"150\"/>"); i++; continue; }
        int trovato = 0;
        for (size_t k = 0; k < sizeof s_fonemi / sizeof s_fonemi[0]; k++) {
            size_t l = strlen(s_fonemi[k].amiga);
            if (i + (int)l <= n && !strncmp(in + i, s_fonemi[k].amiga, l)) {
                const char *s = s_fonemi[k].sapi;
                if (s && p + strlen(s) + 2 < sizeof parola) {
                    if (p) parola[p++] = ' ';
                    memcpy(parola + p, s, strlen(s));
                    p += strlen(s);
                    ultima_vocale = vocale(s);
                }
                i += (int)l;
                trovato = 1;
                break;
            }
        }
        if (!trovato) i++;                            /* simboli che non conosciamo */
    }
    CHIUDI_PAROLA();
#undef METTI
#undef CHIUDI_PAROLA
    MultiByteToWideChar(CP_ACP, 0, xml, -1, out, (int)max);
    if (getenv("AMIGA_VOCE_LOG")) fprintf(stderr, "[voce] %.*s -> %s\n", n, in, xml);
}

int voce_parla(const char *fonemi, int n, int velocita, int femmina) {
    if (!prepara()) return 0;
    if (femmina != s_sesso) { s_sesso = femmina; scegli_voce(femmina); }
    /* narrator: 40-400 parole al minuto, 150 normale; SAPI: -10..10 */
    int r = (velocita - 150) / 15;
    ISpVoice_SetRate(s_voce, r < -10 ? -10 : r > 10 ? 10 : r);
    static wchar_t testo[8192];
    converti(fonemi, n, testo, sizeof testo / sizeof testo[0]);
    /* SPF_PARSE_SAPI: senza, il testo che non comincia con <speak> e' preso per SSML */
    HRESULT h = ISpVoice_Speak(s_voce, testo, SPF_ASYNC | SPF_IS_XML | SPF_PARSE_SAPI | SPF_PURGEBEFORESPEAK, NULL);
    if (getenv("AMIGA_VOCE_LOG")) fprintf(stderr, "[voce] Speak = %08lX\n", (unsigned long)h);
    return SUCCEEDED(h);
}

int voce_finita(void) {
    if (!s_voce) return 1;
    return ISpVoice_WaitUntilDone(s_voce, 0) == S_OK;
}

void voce_ferma(void) {
    if (s_voce) ISpVoice_Speak(s_voce, NULL, SPF_PURGEBEFORESPEAK, NULL);
}
