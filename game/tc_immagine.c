/* tc_immagine.c - l'immagine del gioco costruita all'avvio dai file installati.
 *
 * Fa quello che faceva game/prepara_immagine.py (e LoadSeg sull'Amiga): carica
 * le quattro librerie del pacchetto (libs/) da $200000 e il programma Chaos a
 * $23A000, applica le rilocazioni, mette i hunk CHIP del programma da $1000,
 * scrive a $0 la pila e l'ingresso e traduce la frase della vittoria. Il
 * risultato deve avere il CRC dell'immagine con cui e' stato generato il C
 * ricompilato (CHAOS_IMMAGINE_CRC): cosi' non si distribuisce nessun file del
 * gioco, solo il programma che li sa caricare.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "chaos_segmenti.h"

#define CARICA      0x200000u
#define PROGRAMMA_A 0x23A000u
#define CHIP        0x001000u
#define PILA        0x1FF000u

enum { HUNK_HEADER = 0x3F3, HUNK_CODE = 0x3E9, HUNK_DATA = 0x3EA, HUNK_BSS = 0x3EB,
       HUNK_RELOC32 = 0x3EC, HUNK_SYMBOL = 0x3F0, HUNK_DEBUG = 0x3F1, HUNK_END = 0x3F2 };

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static void w32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

uint32_t tc_crc32(const uint8_t *d, size_t n) {
    static uint32_t t[256];
    if (!t[1]) for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = c >> 1 ^ (0xEDB88320u & -(c & 1));
        t[i] = c;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = t[(c ^ d[i]) & 0xFF] ^ c >> 8;
    return ~c;
}

static uint8_t *leggi(const char *p, size_t *n) {
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long l = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = l > 0 ? malloc((size_t)l) : NULL;
    if (d && fread(d, 1, (size_t)l, f) != (size_t)l) { free(d); d = NULL; }
    fclose(f);
    *n = d ? (size_t)l : 0;
    return d;
}

/* LoadSeg a indirizzo fisso; base_chip != 0: i hunk CHIP li'. Restituisce la
 * fine dei segmenti (non chip), 0 = errore. *ingresso: il primo hunk */
static uint32_t carica(const uint8_t *d, size_t n, uint8_t *mem, uint32_t dim, uint32_t base,
                       uint32_t base_chip, uint32_t *ingresso) {
    size_t o = 0;
#define LONG() (o + 4 <= n ? (o += 4, be32(d + o - 4)) : (fallito = 1, 0u))
    int fallito = 0;
    if (LONG() != HUNK_HEADER) return 0;
    while (LONG() && !fallito) { }
    LONG();
    uint32_t primo = LONG(), ultimo = LONG();
    if (fallito || ultimo < primo || ultimo - primo >= 64) return 0;
    int nh = (int)(ultimo - primo + 1);
    uint32_t inizio[64], taglia[64];
    int chip[64];
    uint32_t a = base, c = base_chip;
    for (int i = 0; i < nh; i++) {
        uint32_t g = LONG();
        taglia[i] = (g & 0x3FFFFFFFu) * 4;
        chip[i] = base_chip && (g >> 30) == 1;
        if (chip[i]) { inizio[i] = c + 8; c = (c + 8 + taglia[i] + 7) & ~7u; }
        else { inizio[i] = a + 8; a = (a + 8 + taglia[i] + 7) & ~7u; }
    }
    if (fallito || a > dim || c > dim) return 0;
    for (int i = 0; i < nh; i++) {
        w32(mem + inizio[i] - 8, taglia[i] + 8);
        w32(mem + inizio[i] - 4, i + 1 < nh ? (inizio[i + 1] - 8) >> 2 : 0);
    }
    int h = -1;
    while (o < n && !fallito) {
        uint32_t t = LONG() & 0x3FFFFFFFu;
        if (t == HUNK_CODE || t == HUNK_DATA) {
            if (++h >= nh) return 0;
            uint32_t k = LONG() * 4;
            if (o + k > n || k > taglia[h]) return 0;
            memcpy(mem + inizio[h], d + o, k);
            o += k;
        } else if (t == HUNK_BSS) {
            if (++h >= nh) return 0;
            LONG();
        } else if (t == HUNK_RELOC32) {
            for (;;) {
                uint32_t k = LONG();
                if (!k || fallito) break;
                uint32_t dest = LONG();
                if (dest >= (uint32_t)nh || h < 0) return 0;
                for (uint32_t i = 0; i < k; i++) {
                    uint32_t dove = inizio[h] + LONG();
                    if (dove + 4 > dim) return 0;
                    w32(mem + dove, be32(mem + dove) + inizio[dest]);
                }
            }
        } else if (t == HUNK_END) {
        } else if (t == HUNK_SYMBOL) {
            for (;;) {
                uint32_t k = LONG();
                if (!k || fallito) break;
                o += 4 * (size_t)k + 4;
            }
        } else if (t == HUNK_DEBUG) {
            uint32_t k = LONG();
            o += 4 * (size_t)k;
        } else return 0;
    }
#undef LONG
    if (fallito) return 0;
    *ingresso = inizio[0];
    return a;
}

/* l'immagine in mem (CHAOS_FINE byte, azzerati). 1 = fatta e col CRC giusto */
int tc_costruisci_immagine(const char *cartella_gioco, uint8_t *mem, char *err, size_t ne) {
    static const char *const librerie[] = {
        "libs/chunkyfonts.library", "libs/iff.library", "libs/req.library", "libs/z.library",
    };
    uint32_t a = CARICA, ingresso = 0;
    for (size_t i = 0; i <= sizeof librerie / sizeof librerie[0]; i++) {
        const char *nome = i < sizeof librerie / sizeof librerie[0] ? librerie[i] : "Chaos";
        int prog = i == sizeof librerie / sizeof librerie[0];
        if (prog) a = PROGRAMMA_A;
        char p[1200];
        snprintf(p, sizeof p, "%s/%s", cartella_gioco, nome);
        size_t n;
        uint8_t *d = leggi(p, &n);
        if (!d) { snprintf(err, ne, "Manca %s.", p); return 0; }
        uint32_t ing = 0, fine = carica(d, n, mem, CHAOS_FINE, a, prog ? CHIP : 0, &ing);
        free(d);
        if (!fine) { snprintf(err, ne, "%s non e' un eseguibile Amiga valido.", p); return 0; }
        if (prog) ingresso = ing;
        a = (fine + 0xFFFu) & ~0xFFFu;
    }
    w32(mem, PILA);
    w32(mem + 4, ingresso);
    /* testi scritti nel programma e non nel catalogo (come prepara_immagine.py) */
    static const char vecchio[] = " Player %d (%s) HAS WON THE GAME!", nuovo[] = " Giocatore %d (%s) HA VINTO!";
    for (uint32_t i = 0; i + sizeof vecchio <= CHAOS_FINE; i++)
        if (!memcmp(mem + i, vecchio, sizeof vecchio)) {         /* col terminatore */
            memset(mem + i, 0, sizeof vecchio - 1);
            memcpy(mem + i, nuovo, sizeof nuovo - 1);
        }
    uint32_t crc = tc_crc32(mem, CHAOS_FINE);
    if (crc != CHAOS_IMMAGINE_CRC) {
        snprintf(err, ne, "I file del gioco in %s non sono quelli attesi (CRC dell'immagine %08X, atteso %08X).\n"
                 "Reinstalla il gioco dal pacchetto TotalChaosAGAr6.lha di Aminet.", cartella_gioco, crc,
                 CHAOS_IMMAGINE_CRC);
        return 0;
    }
    return 1;
}
