/* tc_spec.c - Total Chaos AGA 7.11 per il runner: cosa mettere in memoria,
 * quali librerie del gioco registrare, dove sono i dati, come partire.
 *
 * I dati sono l'hardfile estratto (tools/estrai_hdf.py): la cartella DH0 con
 * il Workbench minimo (Libs, Devs, Fonts, Locale...) e TotalChaos/. Come la
 * Startup-Sequence: assegnazioni di sistema, ENV: e T: in RAM:, poi
 * "CD SYS:TotalChaos" e "Chaos -t". */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <direct.h>
#include <SDL.h>

#include "game_spec.h"
#include "genesis_runtime.h"
#include "amiga_os.h"
#include "chaos_segmenti.h"

extern uint32_t os_fine_programma;

/* l'immagine del programma: caricata dai file installati (tc_immagine.c),
 * niente file preparati da distribuire */
int tc_costruisci_immagine(const char *cartella_gioco, uint8_t *mem, char *err, size_t ne);
static int carica_immagine(const char *dati) {
    char cart[1100], err[700];
    snprintf(cart, sizeof cart, "%s/TotalChaos", dati);
    if (tc_costruisci_immagine(cart, g_amiga_mem, err, sizeof err)) return 1;
    fprintf(stderr, "%s\n", err);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Total Chaos AGA", err, NULL);
    return 0;
}

/* la cartella DH0 (quella con TotalChaos/Chaos): TC_DATI, poi "dati" accanto
 * all'eseguibile, poi dati_gioco/DH0 del repository (build/cmake/..\..) */
static int ha_dati(const char *cartella) {
    char p[1100];
    snprintf(p, sizeof p, "%s/TotalChaos/Chaos", cartella);
    FILE *f = fopen(p, "rb");
    if (f) fclose(f);
    return f != NULL;
}

static const char *trova_dati(void) {
    static char p[1024];
    const char *e = getenv("TC_DATI");
    if (e && ha_dati(e)) return e;
    char *qui = SDL_GetBasePath();
    snprintf(p, sizeof p, "%sinstall", qui ? qui : "");          /* installato dal launcher */
    if (!ha_dati(p)) snprintf(p, sizeof p, "%sdati", qui ? qui : "");
    if (!ha_dati(p)) snprintf(p, sizeof p, "%s..\\..\\dati_gioco\\DH0", qui ? qui : "");
    SDL_free(qui);
    return ha_dati(p) ? p : NULL;
}

void amiga_cpu_del_gioco(int x);

static void tc_avvia(void) {
    amiga_cpu_del_gioco(40);                       /* 68060 a 50 MHz, come consiglia il gioco */
    os_inizio_chip = CHAOS_FINE_CHIP;              /* i hunk CHIP del gioco stanno sotto */
    os_fine_programma = CHAOS_FINE;
    const char *dati = trova_dati();
    if (!dati) {
        const char *m = "Non trovo i dati di Total Chaos (DH0 con TotalChaos/Chaos).\n"
                        "Estrai l'hardfile con tools/estrai_hdf.py e metti DH0 nella cartella \"dati\" "
                        "accanto al programma, o indica la cartella con la variabile TC_DATI.";
        fprintf(stderr, "%s\n", m);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Total Chaos AGA", m, NULL);
        exit(1);
    }
    if (!carica_immagine(dati)) exit(1);
    static char sub[1100], ram[1100];
#define CARTELLA(nome) (snprintf(sub, sizeof sub, "%s/%s", dati, nome), sub)
    /* RAM: per ENV: e T: (il gioco ci scrive), accanto all'eseguibile */
    char *qui = SDL_GetBasePath();
    snprintf(ram, sizeof ram, "%sRAM", qui ? qui : "");
    SDL_free(qui);
    if (getenv("TC_RAM")) snprintf(ram, sizeof ram, "%s", getenv("TC_RAM"));   /* prove in parallelo */
    _mkdir(ram);
    os_init();
    os_dos_init(dati);
    os_grafica_init();
    os_intuition_init();
    os_device_init();
    os_utility_init();
    os_keymap_init();
    os_layers_init();
    os_asl_init();
    os_iffparse_init();                            /* il Workbench che il gioco apre, in C */
    os_diskfont_init();
    for (size_t i = 0; i < sizeof chaos_librerie / sizeof chaos_librerie[0]; i++)
        os_libreria_disco(chaos_librerie[i].nome, chaos_librerie[i].seglist, chaos_librerie[i].dati,
                          chaos_librerie[i].vettori, chaos_librerie[i].struttura, chaos_librerie[i].init);
    extern void os_req_aggancia(void);
    os_req_aggancia();                             /* Salva senza finestra (amiga_asl.c) */
    os_dos_volume("TotalChaos");
    os_dos_assegna("TotalChaos", dati);
    os_dos_assegna("DH0", dati);
    os_dos_assegna("SYS", dati);
    os_dos_assegna("C", CARTELLA("C"));
    os_dos_assegna("S", CARTELLA("S"));
    os_dos_assegna("L", CARTELLA("L"));
    os_dos_assegna("LIBS", CARTELLA("Libs"));
    os_dos_assegna("DEVS", CARTELLA("Devs"));
    os_dos_assegna("FONTS", CARTELLA("Fonts"));
    os_dos_assegna("LOCALE", CARTELLA("Locale"));
    os_dos_assegna("PROGDIR", CARTELLA("TotalChaos"));      /* la cartella di Chaos */
    os_dos_assegna("RAM", ram);
    os_dos_assegna("ENV", ram);
    os_dos_assegna("T", ram);
    /* "-p -t", la riga della Startup-Sequence quando VideoSel sceglie il
     * DblPAL: schermi DblPAL 640x512 senza interlacciamento (-p) e il
     * narrator che dice anche i nomi (-t). TC_ARGOMENTI per cambiarli */
    os_prepara_avvio(CHAOS_CARICA >> 2, getenv("TC_ARGOMENTI") ? getenv("TC_ARGOMENTI") : "-p -t", "Chaos");
    os_dos_processo();
    os_dos_cd(CARTELLA("TotalChaos"));
#undef CARTELLA
    os_chiama_68k(CHAOS_INGRESSO);
}

/* la finestra di debug (F11 con --debug): per ora nessuna variabile del gioco */
#include "amiga_debug.h"
const debug_gioco_t g_debug_gioco = { 0 };

/* il codice caricato a runtime (Mod.ChaosTheme78, un modulo col suo player
 * in 68000) lo esegue Musashi: amiga_interprete.c */
int amiga_interpreta(uint32_t addr);

/* --installa <TotalChaosAGAr6.lha>: installa il gioco in TC_DATI o "dati"
 * accanto al programma, con le modifiche di tc_modifiche.bin accanto al
 * programma; --installa-originale: senza modifiche (per tools/crea_modifiche.py) */
#include "tc_installa.h"
static void mostra(void *ctx, int fatti, int totale, const char *fase) {
    (void)ctx;
    fprintf(stderr, "\r%s: %d/%d   ", fase, fatti, totale);
}
static int tc_argomento(const char *k, const char *v) {
    int originale = !strcmp(k, "--installa-originale");
    if ((strcmp(k, "--installa") && !originale) || !v) return 0;
    char *qui = SDL_GetBasePath();
    char dati[1100], mod[1100], m[800];
    snprintf(dati, sizeof dati, "%s", getenv("TC_DATI") ? getenv("TC_DATI") : "");
    if (!dati[0]) snprintf(dati, sizeof dati, "%sinstall", qui ? qui : "");
    snprintf(mod, sizeof mod, "%stc_modifiche.bin", qui ? qui : "");
    SDL_free(qui);
    int ok = tc_installa(v, dati, originale ? NULL : mod, mostra, NULL, m, sizeof m);
    fprintf(stderr, "\n%s\n", m);
    exit(ok ? 0 : 1);
}

const GameSpec g_game_spec = {
    .display_name = "Total Chaos AGA 7.11",
    .short_name = "TotalChaos",
    .call_entry_point = tc_avvia,
    .dispatch_override = amiga_interpreta,
    .handle_arg = tc_argomento,
};
