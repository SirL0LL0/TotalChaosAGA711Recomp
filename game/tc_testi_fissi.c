/* tc_testi_fissi.c - le frasi che Total Chaos ha scritte nel programma, in
 * inglese, fuori dai cataloghi ("Press anything to continue" dei tutorial,
 * i messaggi del caricamento, il titolo della scelta della texture...): qui
 * si riscrivono nella memoria del programma nella lingua scelta.
 *
 * La lingua e' quella di Chaos.prefs all'avvio e poi quella scelta nel menu
 * Lingua (il terzo: le voci 1-13 sono le lingue, nello stesso ordine in ogni
 * lingua). Una traduzione si scrive solo se sta nel posto dell'originale (la
 * frase piu' gli zeri che la seguono): il codice del gioco la trova sempre
 * allo stesso indirizzo. Se non sta resta l'inglese (e lo si scrive nel
 * registro). Tornando all'inglese si rimette l'originale. */
#include "amiga_os.h"
#include "chaos_segmenti.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { const char *en, *it; } frase_t;

static const frase_t s_frasi[] = {
    /* tutorial */
    { "Press anything to continue", "Clic o tasto per continuare" },
    /* stile di evidenziazione (menu Controlli) */
    { "The Choice is Yours!", "A te la scelta!" },
    { "Choose a Hilite Style", "Scegli lo stile" },
    /* titoli delle finestre di file */
    { "Bottom Texture Selection", "Scelta texture del fondo" },
    { "Terrain Selection", "Scelta terreno" },
    { "Tutorial Selection", "Scelta tutorial" },
    /* il caricamento all'avvio */
    { "Setting up miscelaneous stuff.", "Preparo un po' di cose." },
    { "Loading the gfx.", "Carico grafica." },
    { "Setting up the sound effects.", "Preparo gli effetti sonori." },
    { "Scanning entire dirtree for any newly installed speech files.",
      "Cerco nelle cartelle nuovi file di voce installati." },
    { "Loading all the catalogs.", "Carico tutti i cataloghi." },
    { "Translating all the creature and spell names.", "Traduco i nomi di creature e incantesimi." },
    { "Loading the CellEffects.", "Carico gli effetti celle." },
    { "Generating Terrain Lists.", "Creo elenchi dei terreni." },
    { "Generating Scroll Images.", "Creo le pergamene." },
    { "Processing WeaponEffects Images.", "Preparo le immagini delle armi." },
    { "Finished Loading All Data.", "Caricamento completato." },
};
#define N_FRASI (sizeof s_frasi / sizeof s_frasi[0])

static uint32_t s_dove[N_FRASI];          /* indirizzo nel programma (0 = non trovata) */
static uint32_t s_posto[N_FRASI];         /* byte disponibili, zero finale compreso */
static int s_cercate;

/* le frasi nella memoria del programma (dati del gioco, sopra i hunk chip) */
static void cerca(void) {
    s_cercate = 1;
    const uint8_t *m = g_amiga_mem;
    for (size_t k = 0; k < N_FRASI; k++) {
        size_t l = strlen(s_frasi[k].en);
        for (uint32_t a = CHAOS_CARICA; a + l + 1 < CHAOS_FINE; a++) {
            if (m[a] != (uint8_t)s_frasi[k].en[0] || memcmp(m + a, s_frasi[k].en, l) || m[a + l]) continue;
            if (a > 0 && m[a - 1] >= 0x20 && m[a - 1] < 0x7F) continue;      /* dentro un'altra frase */
            uint32_t fine = a + (uint32_t)l;
            while (fine + 1 < CHAOS_FINE && !m[fine + 1]) fine++;        /* gli zeri dopo: posto libero */
            s_dove[k] = a;
            s_posto[k] = fine - a + 1;
            break;
        }
    }
}

static void scrivi(uint32_t a, const char *s, uint32_t posto) {
    uint32_t i = 0;
    for (; s[i] && i + 1 < posto; i++) g_amiga_mem[a + i] = (uint8_t)s[i];
    for (; i < posto; i++) g_amiga_mem[a + i] = 0;
}

static void applica(int italiano) {
    if (!s_cercate) cerca();
    for (size_t k = 0; k < N_FRASI; k++) {
        if (!s_dove[k]) continue;
        const char *t = italiano ? s_frasi[k].it : s_frasi[k].en;
        if (strlen(t) + 1 > s_posto[k]) {
            if (os_traccia) fprintf(stderr, "[testi] \"%s\" non sta in %u byte: resta in inglese\n", t, s_posto[k]);
            t = s_frasi[k].en;
        }
        scrivi(s_dove[k], t, s_posto[k]);
    }
}

/* le voci del menu Lingua: 1 inglese ... 6 italiano (codice MENUPICK:
 * menu | voce << 5) */
static void menu_scelto(uint16_t codice) {
    if ((codice & 0x1F) != 2) return;
    int voce = (codice >> 5) & 0x3F;
    if (voce >= 1 && voce <= 13) applica(voce == 6);
}

/* all'avvio, dopo aver caricato il programma: la lingua di Chaos.prefs */
void tc_testi_fissi_avvia(const char *dati) {
    char p[1100], r[256];
    snprintf(p, sizeof p, "%s/TotalChaos/Chaos.prefs", dati);
    int italiano = 0;
    FILE *f = fopen(p, "rb");
    if (f) {
        while (fgets(r, sizeof r, f))
            if (!strncmp(r, "Language:", 9) && strstr(r, "Italiano")) italiano = 1;
        fclose(f);
    }
    applica(italiano);
    extern void (*os_menu_scelto)(uint16_t codice);
    os_menu_scelto = menu_scelto;
}
