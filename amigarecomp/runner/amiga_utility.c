/* amiga_utility.c - utility.library (V40) in C.
 *
 * Le librerie del Workbench 3.1 (diskfont, locale, asl, iffparse) e Total
 * Chaos la aprono: liste di tag, moltiplicazioni e divisioni a 32 bit,
 * confronti di stringhe senza maiuscole, date, hook. Mancano gli oggetti con
 * nome e Pack/UnpackStructureTags: se servono, os_manca lo dice.
 *
 * TagItem: ti_Tag (4), ti_Data (4). TAG_DONE 0, TAG_IGNORE 1, TAG_MORE 2
 * (ti_Data = la lista che continua), TAG_SKIP 3 (salta ti_Data voci).
 */
#include <stdio.h>
#include <string.h>
#include "genesis_runtime.h"
#include "amiga_os.h"

enum { TAG_DONE = 0, TAG_IGNORE = 1, TAG_MORE = 2, TAG_SKIP = 3 };

/* la voce dopo *pp, come NextTagItem: 0 a fine lista */
static uint32_t prossima(uint32_t *pp) {
    uint32_t p = *pp;
    while (p) {
        uint32_t tag = R32(p);
        switch (tag) {
        case TAG_DONE: *pp = 0; return 0;
        case TAG_IGNORE: p += 8; break;
        case TAG_MORE: p = R32(p + 4); break;
        case TAG_SKIP: p += 8u * (R32(p + 4) + 1); break;
        default: *pp = p + 8; return p;
        }
    }
    *pp = 0;
    return 0;
}

static uint32_t trova(uint32_t tag, uint32_t lista) {
    uint32_t p = lista, t;
    while ((t = prossima(&p)) != 0)
        if (R32(t) == tag) return t;
    return 0;
}

static void u_findtagitem(void) { D0 = trova(D0, A0); }

static void u_gettagdata(void) {
    uint32_t t = trova(D0, A0);
    D0 = t ? R32(t + 4) : D1;
}

static void u_nexttagitem(void) {
    /* NextTagItem(&lista A0): avanza il puntatore e restituisce la voce */
    uint32_t p = R32(A0);
    uint32_t t = prossima(&p);
    W32(A0, p);
    D0 = t;
}

static void u_packbooltags(void) {
    /* PackBoolTags(flag D0, tagList A0, boolMap A1) */
    uint32_t f = D0, p = A0, t;
    while ((t = prossima(&p)) != 0) {
        uint32_t m = trova(R32(t), A1);
        if (!m) continue;
        if (R32(t + 4)) f |= R32(m + 4); else f &= ~R32(m + 4);
    }
    D0 = f;
}

static int in_array(uint32_t tag, uint32_t arr) {
    for (uint32_t a = arr; R32(a) != TAG_DONE; a += 4)
        if (R32(a) == tag) return 1;
    return 0;
}

static void u_taginarray(void) { D0 = in_array(D0, A0) ? 1 : 0; }

static void u_filtertagitems(void) {
    /* FilterTagItems(tagList A0, filterArray A1, logic D0): AND tiene quelle
     * dell'elenco, NOT le toglie; le tolte diventano TAG_IGNORE */
    uint32_t logica = D0, p = A0, t, n = 0;
    while ((t = prossima(&p)) != 0) {
        int dentro = A1 && in_array(R32(t), A1);
        if (dentro == (logica == 0)) n++;
        else W32(t, TAG_IGNORE);
    }
    D0 = n;
}

static void u_maptags(void) {
    /* MapTags(tagList A0, mapList A1, mapType D0): 0 = toglie quelle senza
     * corrispondenza, 1 = le tiene */
    uint32_t tipo = D0, p = A0, t;
    while ((t = prossima(&p)) != 0) {
        uint32_t m = trova(R32(t), A1);
        if (m) W32(t, R32(m + 4));
        else if (tipo == 0) W32(t, TAG_IGNORE);
    }
}

/* AllocVec: la taglia nei 4 byte davanti */
static uint32_t alloca_vec(uint32_t n) {
    uint32_t a = os_alloca(n + 4, MEMF_PUBLIC | MEMF_CLEAR);
    if (!a) return 0;
    W32(a, n + 4);
    return a + 4;
}
static void libera_vec(uint32_t a) {
    if (a) os_libera(a - 4, R32(a - 4));
}

static void u_allocatetagitems(void) { D0 = alloca_vec(D0 * 8); }
static void u_freetagitems(void) { libera_vec(A0); }

static void u_clonetagitems(void) {
    uint32_t p = A0, t, n = 0;
    while (prossima(&p)) n++;
    uint32_t c = alloca_vec((n + 1) * 8);
    if (c) {
        uint32_t q = c;
        p = A0;
        while ((t = prossima(&p)) != 0) { W32(q, R32(t)); W32(q + 4, R32(t + 4)); q += 8; }
        W32(q, TAG_DONE);
    }
    D0 = c;
}

static void u_refreshtagitemclones(void) {
    /* RefreshTagItemClones(clone A0, original A1) */
    uint32_t q = A0, p = A1, t;
    if (!q) return;
    while ((t = prossima(&p)) != 0) { W32(q, R32(t)); W32(q + 4, R32(t + 4)); q += 8; }
    W32(q, TAG_DONE);
}

static void u_applytagchanges(void) {
    /* ApplyTagChanges(list A0, changeList A1) */
    uint32_t p = A0, t;
    while ((t = prossima(&p)) != 0) {
        uint32_t c = trova(R32(t), A1);
        if (c) W32(t + 4, R32(c + 4));
    }
}

static void u_filtertagchanges(void) {
    /* FilterTagChanges(changeList A0, originalList A1, apply D0): toglie dalle
     * modifiche quelle che non cambiano niente; con apply le applica */
    uint32_t p = A0, t;
    while ((t = prossima(&p)) != 0) {
        uint32_t o = trova(R32(t), A1);
        if (!o) continue;
        if (R32(o + 4) == R32(t + 4)) W32(t, TAG_IGNORE);
        else if (D0) W32(o + 4, R32(t + 4));
    }
}

static void u_callhookpkt(void) {
    /* CallHookPkt(hook A0, object A2, message A1): h_Entry a +8 */
    if (!A0 || !R32(A0 + 8)) { D0 = 0; return; }
    os_chiama_68k(R32(A0 + 8));
}

/* ---- aritmetica ---- */
static void u_smult32(void) { D0 = (uint32_t)((int32_t)D0 * (int32_t)D1); }
static void u_umult32(void) { D0 = D0 * D1; }
static void u_sdivmod32(void) {
    int32_t a = (int32_t)D0, b = (int32_t)D1;
    if (!b) { D0 = 0; D1 = 0; return; }
    D0 = (uint32_t)(a / b); D1 = (uint32_t)(a % b);
}
static void u_udivmod32(void) {
    uint32_t a = D0, b = D1;
    if (!b) { D0 = 0; D1 = 0; return; }
    D0 = a / b; D1 = a % b;
}
/* SMult64/UMult64: D0 i 32 bit alti, D1 i bassi */
static void u_smult64(void) {
    int64_t r = (int64_t)(int32_t)D0 * (int64_t)(int32_t)D1;
    D0 = (uint32_t)((uint64_t)r >> 32); D1 = (uint32_t)r;
}
static void u_umult64(void) {
    uint64_t r = (uint64_t)D0 * (uint64_t)D1;
    D0 = (uint32_t)(r >> 32); D1 = (uint32_t)r;
}

/* ---- caratteri (ISO-8859-1, come utility.library) ---- */
static uint8_t maiuscola(uint8_t c) {
    if ((c >= 'a' && c <= 'z') || (c >= 0xE0 && c <= 0xFE && c != 0xF7)) return (uint8_t)(c - 0x20);
    return c;
}
static uint8_t minuscola(uint8_t c) {
    if ((c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7)) return (uint8_t)(c + 0x20);
    return c;
}
static void u_toupper(void) { D0 = maiuscola((uint8_t)D0); }
static void u_tolower(void) { D0 = minuscola((uint8_t)D0); }

static int32_t confronta(uint32_t a, uint32_t b, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        uint8_t x = maiuscola(R8(a + i)), y = maiuscola(R8(b + i));
        if (x != y) return (int32_t)x - (int32_t)y;
        if (!x) return 0;
    }
    return 0;
}
static void u_stricmp(void) { D0 = (uint32_t)confronta(A0, A1, 0xFFFFFFFFu); }
static void u_strnicmp(void) { D0 = (uint32_t)confronta(A0, A1, D0); }

/* ---- date: secondi dal 1/1/1978. ClockData: sec, min, hour, mday, month,
 * year, wday (UWORD) ---- */
static int bisestile(int a) { return (a % 4 == 0 && a % 100 != 0) || a % 400 == 0; }
static const int s_giorni_mese[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

static void u_amiga2date(void) {
    uint32_t s = D0, cd = A0;
    uint32_t giorni = s / 86400, resto = s % 86400;
    W16(cd + 0, resto % 60); W16(cd + 2, resto / 60 % 60); W16(cd + 4, resto / 3600);
    W16(cd + 12, (giorni + 0) % 7);                      /* 1/1/1978 era domenica */
    int anno = 1978;
    while (giorni >= (uint32_t)(bisestile(anno) ? 366 : 365)) { giorni -= bisestile(anno) ? 366 : 365; anno++; }
    int mese = 0;
    for (;;) {
        int g = s_giorni_mese[mese] + (mese == 1 && bisestile(anno));
        if (giorni < (uint32_t)g) break;
        giorni -= (uint32_t)g; mese++;
    }
    W16(cd + 6, giorni + 1); W16(cd + 8, mese + 1); W16(cd + 10, anno);
}

static uint32_t data_in_secondi(uint32_t cd) {
    int anno = R16(cd + 10), mese = R16(cd + 8), giorno = R16(cd + 6);
    uint32_t g = 0;
    for (int a = 1978; a < anno; a++) g += bisestile(a) ? 366 : 365;
    for (int m = 1; m < mese; m++) g += (uint32_t)(s_giorni_mese[m - 1] + (m == 2 && bisestile(anno)));
    g += (uint32_t)(giorno - 1);
    return g * 86400u + R16(cd + 4) * 3600u + R16(cd + 2) * 60u + R16(cd + 0);
}

static void u_date2amiga(void) { D0 = data_in_secondi(A0); }

static void u_checkdate(void) {
    uint32_t cd = A0;
    int anno = R16(cd + 10), mese = R16(cd + 8), giorno = R16(cd + 6);
    int ok = anno >= 1978 && mese >= 1 && mese <= 12 && giorno >= 1 &&
             giorno <= s_giorni_mese[mese - 1] + (mese == 2 && bisestile(anno)) &&
             R16(cd + 4) < 24 && R16(cd + 2) < 60 && R16(cd + 0) < 60;
    D0 = ok ? data_in_secondi(cd) : 0;
}

static void u_getuniqueid(void) {
    static uint32_t id = 0x10000;
    D0 = id++;
}

/* ---------------------------------------------------------- tabella */

#define F(lvo, fn) [(lvo) / 6 - 1] = fn
#define N(lvo, nome) [(lvo) / 6 - 1] = nome
static const os_funzione s_fn[45] = {
    F(30, u_findtagitem), F(36, u_gettagdata), F(42, u_packbooltags), F(48, u_nexttagitem),
    F(54, u_filtertagchanges), F(60, u_maptags), F(66, u_allocatetagitems), F(72, u_clonetagitems),
    F(78, u_freetagitems), F(84, u_refreshtagitemclones), F(90, u_taginarray),
    F(96, u_filtertagitems), F(102, u_callhookpkt), F(120, u_amiga2date), F(126, u_date2amiga),
    F(132, u_checkdate), F(138, u_smult32), F(144, u_umult32), F(150, u_sdivmod32),
    F(156, u_udivmod32), F(162, u_stricmp), F(168, u_strnicmp), F(174, u_toupper),
    F(180, u_tolower), F(186, u_applytagchanges), F(198, u_smult64), F(204, u_umult64),
    F(270, u_getuniqueid),
};
static const char *const s_nomi[45] = {
    N(6, "Open"), N(12, "Close"), N(18, "Expunge"), N(24, "Reserved"),
    N(30, "FindTagItem"), N(36, "GetTagData"), N(42, "PackBoolTags"), N(48, "NextTagItem"),
    N(54, "FilterTagChanges"), N(60, "MapTags"), N(66, "AllocateTagItems"), N(72, "CloneTagItems"),
    N(78, "FreeTagItems"), N(84, "RefreshTagItemClones"), N(90, "TagInArray"),
    N(96, "FilterTagItems"), N(102, "CallHookPkt"), N(120, "Amiga2Date"), N(126, "Date2Amiga"),
    N(132, "CheckDate"), N(138, "SMult32"), N(144, "UMult32"), N(150, "SDivMod32"),
    N(156, "UDivMod32"), N(162, "Stricmp"), N(168, "Strnicmp"), N(174, "ToUpper"),
    N(180, "ToLower"), N(186, "ApplyTagChanges"), N(198, "SMult64"), N(204, "UMult64"),
    N(210, "PackStructureTags"), N(216, "UnpackStructureTags"), N(222, "AddNamedObject"),
    N(228, "AllocNamedObjectA"), N(234, "AttemptRemNamedObject"), N(240, "FindNamedObject"),
    N(246, "FreeNamedObject"), N(252, "NamedObjectName"), N(258, "ReleaseNamedObject"),
    N(264, "RemNamedObject"), N(270, "GetUniqueID"),
};

void os_utility_init(void) {
    static const os_libreria_c def = { "utility.library", 40, 45, 40, s_nomi, s_fn };
    os_crea_libreria(&def, NT_LIBRARY);
}
