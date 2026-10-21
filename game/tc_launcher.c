/* tc_launcher.c - il launcher di Total Chaos (recomp-ui) e cosa succede prima
 * che il gioco parta.
 *
 * - si sceglie (o si trascina) il pacchetto TotalChaosAGAr6.lha di Aminet: il
 *   launcher lo riconosce dal CRC32;
 * - alla partenza, se il gioco non e' ancora installato (o le nostre modifiche
 *   sono cambiate) lo si installa in "install" accanto al programma
 *   (tc_installa.c), con una finestrella di avanzamento;
 * - la lingua scelta nel launcher (le 13 del gioco) si scrive in
 *   install/TotalChaos/Chaos.prefs, come farebbe il menu Lingua del gioco; con
 *   l'italiano anche il launcher parla italiano;
 * - "Ripristina le impostazioni del gioco" rimette le preferenze del gioco
 *   come nel pacchetto (Chaos.prefs.originale, salvato all'installazione).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>

#include "amiga_impostazioni.h"
#include "amiga_video_gl.h"
#include "tc_installa.h"

#if defined(RECOMP_LAUNCHER)
#include "recomp_launcher.h"
#include "launcher_profile.h"
#endif

/* le lingue del gioco, come le scrive in Chaos.prefs (Latin-1) e il codice
 * della lingua del launcher (solo italiano e inglese hanno i testi) */
static const char *const s_lingue[] = {
    "English", "Fran\xE7" "ais", "Magyar", "Deutsch", "Espa\xF1" "ol", "Italiano", "Polski", "Norsk",
    "Svenska", "Suomi", "Portugu\xEA" "s", "Brazillianese", "Ellinika",
};
/* le stesse in UTF-8, per il launcher */
static const char *const s_lingue_utf8[] = {
    "English", "Fran\xC3\xA7" "ais", "Magyar", "Deutsch", "Espa\xC3\xB1" "ol", "Italiano", "Polski", "Norsk",
    "Svenska", "Suomi", "Portugu\xC3\xAA" "s", "Brazillianese", "Ellinika",
};
static const char *const s_codici[] = { "en", "fr", "hu", "de", "es", "it", "pl", "no", "sv", "fi", "pt", "pt", "el" };
#define N_LINGUE ((int)(sizeof s_lingue / sizeof s_lingue[0]))

static void cartella_install(char *p, size_t n) {
    char *qui = SDL_GetBasePath();
    snprintf(p, n, "%sinstall", qui ? qui : "");
    SDL_free(qui);
}
static void accanto(char *p, size_t n, const char *nome) {
    char *qui = SDL_GetBasePath();
    snprintf(p, n, "%s%s", qui ? qui : "", nome);
    SDL_free(qui);
}

static uint8_t *leggi(const char *p, size_t *n) {
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long l = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc(l > 0 ? (size_t)l + 1 : 1);
    if (d && l > 0 && fread(d, 1, (size_t)l, f) != (size_t)l) { free(d); d = NULL; }
    fclose(f);
    *n = d ? (size_t)(l > 0 ? l : 0) : 0;
    return d;
}

uint32_t tc_crc32(const uint8_t *d, size_t n);

/* il segno dell'installazione: quale pacchetto e quali modifiche */
static void segno_atteso(char *s, size_t n) {
    char mod[1100];
    accanto(mod, sizeof mod, "tc_modifiche.bin");
    size_t k;
    uint8_t *d = leggi(mod, &k);
    snprintf(s, n, "pacchetto %08X modifiche %08X\n", TC_LHA_CRC, d ? tc_crc32(d, k) : 0);
    free(d);
}
static int installato(const char *install) {
    char p[1200], atteso[128], c[128] = "";
    snprintf(p, sizeof p, "%s/installato.txt", install);
    FILE *f = fopen(p, "r");
    if (!f) return 0;
    if (!fgets(c, sizeof c, f)) c[0] = 0;
    fclose(f);
    segno_atteso(atteso, sizeof atteso);
    snprintf(p, sizeof p, "%s/TotalChaos/Chaos", install);
    FILE *g = fopen(p, "rb");
    if (g) fclose(g);
    return g && !strcmp(c, atteso);
}

/* ---- la finestrella dell'installazione */
typedef struct { SDL_Window *w; SDL_Renderer *r; } avanzamento_t;
static void disegna(void *ctx, int fatti, int totale, const char *fase) {
    avanzamento_t *a = ctx;
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) { }
    if (!a->r) return;
    int pct = totale ? fatti * 100 / totale : 0;
    char t[160];
    snprintf(t, sizeof t, "Total Chaos AGA - %s %d%%", !strcmp(fase, "modifiche") ? "installing changes" : "installing", pct);
    SDL_SetWindowTitle(a->w, t);
    SDL_SetRenderDrawColor(a->r, 0x00, 0x22, 0x55, 255);
    SDL_RenderClear(a->r);
    SDL_Rect bordo = { 20, 40, 440, 30 }, pieno = { 22, 42, 436 * pct / 100, 26 };
    SDL_SetRenderDrawColor(a->r, 0xFF, 0x88, 0x00, 255);
    SDL_RenderDrawRect(a->r, &bordo);
    SDL_RenderFillRect(a->r, &pieno);
    SDL_RenderPresent(a->r);
}

static int installa(const char *pacchetto, const char *install, char *err, size_t ne) {
    avanzamento_t a = { 0 };
    SDL_InitSubSystem(SDL_INIT_VIDEO);
    a.w = SDL_CreateWindow("Total Chaos AGA - installing", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 480, 110, 0);
    if (a.w) a.r = SDL_CreateRenderer(a.w, -1, 0);
    char mod[1100];
    accanto(mod, sizeof mod, "tc_modifiche.bin");
    int ok = tc_installa(pacchetto, install, mod, disegna, &a, err, ne);
    if (ok) {
        char p[1200], atteso[128];
        segno_atteso(atteso, sizeof atteso);
        snprintf(p, sizeof p, "%s/installato.txt", install);
        FILE *f = fopen(p, "w");
        if (f) { fputs(atteso, f); fclose(f); }
    }
    if (a.r) SDL_DestroyRenderer(a.r);
    if (a.w) SDL_DestroyWindow(a.w);
    return ok;
}

/* la lingua nelle preferenze del gioco (riga "Language:\t<nome>") */
static void scrivi_lingua(const char *install, int lingua) {
    if (lingua < 0 || lingua >= N_LINGUE) return;
    char p[1200];
    snprintf(p, sizeof p, "%s/TotalChaos/Chaos.prefs", install);
    size_t n;
    char *d = (char *)leggi(p, &n);
    if (!d) return;
    d[n] = 0;
    FILE *f = fopen(p, "wb");
    if (!f) { free(d); return; }
    int scritta = 0;
    for (char *r = d; *r;) {
        char *fine = strchr(r, '\n');
        size_t l = fine ? (size_t)(fine - r + 1) : strlen(r);
        if (!strncmp(r, "Language:", 9)) { fprintf(f, "Language:\t%s\n", s_lingue[lingua]); scritta = 1; }
        else fwrite(r, 1, l, f);
        r += l;
    }
    if (!scritta) fprintf(f, "Language:\t%s\n", s_lingue[lingua]);
    fclose(f);
    free(d);
}

/* "Ripristina tutte le impostazioni": le preferenze del gioco come nel
 * pacchetto (se e' installato) e quelle del launcher (il launcher rimette le
 * sue predefinite in memoria: GameInfo.tool_restores_defaults) */
static int ripristina_tutto(char *msg, size_t n) {
    char install[1100], da[1200], a[1200];
    cartella_install(install, sizeof install);
    snprintf(da, sizeof da, "%s/TotalChaos/Chaos.prefs.originale", install);
    snprintf(a, sizeof a, "%s/TotalChaos/Chaos.prefs", install);
    amiga_impostazioni_cancella();
    size_t k;
    uint8_t *d = leggi(da, &k);
    if (d) {
        FILE *f = fopen(a, "wb");
        int ok = f && fwrite(d, 1, k, f) == k;
        if (f) fclose(f);
        free(d);
        if (!ok) { snprintf(msg, n, "Could not write the game preferences."); return 0; }
    }
    snprintf(msg, n, "%s", "All settings restored to their defaults.");
    return 1;
}

#if defined(RECOMP_LAUNCHER)
static int strumento(const char *rom, char *msg, size_t n) {
    (void)rom;
    return ripristina_tutto(msg, n);
}

int tc_manuale(const char *lingua, const RecompLauncherCManualPage **pagine);   /* tc_manuale.c */

/* i testi del gioco nel launcher, in italiano (GameInfo.host_text) */
static const char *const s_testi_it[] = {
    "Package", "pacchetto",
    "PACKAGE", "PACCHETTO",
    "Drag TotalChaosAGAr6.lha here\n(from Aminet, game/strat)", "Trascina qui TotalChaosAGAr6.lha\n(da Aminet, game/strat)",
    "Reset all settings", "Ripristina tutte le impostazioni",
    "Puts back both the game's own preferences (options, terrain, music... as in the Aminet package) and "
    "this launcher's settings.",
    "Rimette come all'inizio sia le preferenze del gioco (opzioni, terreno, musica... come nel pacchetto di "
    "Aminet) sia le impostazioni di questo launcher.",
    "All settings restored to their defaults.", "Tutte le impostazioni sono tornate come all'inizio.",
    "Could not write the game preferences.", "Non riesco a scrivere le preferenze del gioco.",
    "Left and right buttons are the Amiga mouse buttons; the right one on the top bar opens the game "
    "menus. F12 or a left click while holding the middle button release the pointer (the middle button alone examines, like Q), a click in the window takes it back. "
    "Hold Tab to fast forward. The Manual explains the rest.",
    "I tasti sinistro e destro sono quelli del mouse dell'Amiga; il destro sulla barra in alto apre i menu "
    "del gioco. F12 o un clic sinistro tenendo premuto il tasto centrale liberano il puntatore (il centrale da solo esamina, come Q), un clic nella finestra lo riprende. Tieni "
    "premuto Tab per andare avanti veloce. Il resto lo spiega il Manuale.",
    "Installed in the install folder: ready to play.", "Installato nella cartella install: pronto per giocare.",
    "Not installed yet: PLAY installs it in the install folder (about 20 seconds).",
    "Non ancora installato: GIOCA lo installa nella cartella install (una ventina di secondi).",
    "Installed with older changes: PLAY updates it (saved games and high scores stay).",
    "Installato con modifiche vecchie: GIOCA lo aggiorna (salvataggi e record restano).",
    "Total Chaos AGA 7.11 - James Conwell and Team Chaos (2008), freeware on Aminet (game/strat).\n"
    "PC port by static recompilation: SirL0LL0, with Claude. Italian translation completed for this port.\n"
    "The game's files are not included: they are extracted from TotalChaosAGAr6.lha downloaded from Aminet.",
    "Total Chaos AGA 7.11 - James Conwell e il Team Chaos (2008), freeware su Aminet (game/strat).\n"
    "Versione per PC per ricompilazione statica: SirL0LL0, con Claude. Traduzione italiana completata per "
    "questa versione.\n"
    "I file del gioco non sono inclusi: si estraggono da TotalChaosAGAr6.lha scaricato da Aminet.",
    /* scala e forma dello schermo */
    "x0.5 (256 lines)", "x0,5 (256 righe)",
    "x1 (512 lines, native)", "x1 (512 righe, originale)",
    "x1.5 (768 lines)", "x1,5 (768 righe)",
    "x2 (1024 lines)", "x2 (1024 righe)",
    "x2.5 (1280 lines)", "x2,5 (1280 righe)",
    "x3 (1536 lines)", "x3 (1536 righe)",
    "x3.5 (1792 lines)", "x3,5 (1792 righe)",
    "x4 (2048 lines)", "x4 (2048 righe)",
    "PAL TV / 1084S (4:3)", "TV PAL / 1084S (4:3)",
    "NTSC TV (4:3)", "TV NTSC (4:3)",
    "Multisync, square pixels (5:4)", "Multisync, pixel quadrati (5:4)",
    "Stretched 16:9", "Allargato 16:9",
    "Stretched 16:10", "Allargato 16:10",
    "Fill the window", "Riempi la finestra",
    "Sharp pixels", "Pixel netti",
    "Sharp bilinear (even lines)", "Sharp bilinear (righe uguali)",
    "Bilinear (soft)", "Bilineare (morbido)",
    "Bicubic", "Bicubico",
    "Lanczos", "Lanczos",
    "Scale2x (rounded edges)", "Scale2x (bordi arrotondati)",
    "Scaling filter", "Filtro di scala",
    NULL,
};

/* la scala della finestra in mezzi passi dello schermo del gioco (640 x 512):
 * il valore k sono 256 righe per k, come la scala del runner */
static const char *const s_scale[] = {
    "x0.5 (256 lines)", "x1 (512 lines, native)", "x1.5 (768 lines)", "x2 (1024 lines)",
    "x2.5 (1280 lines)", "x3 (1536 lines)", "x3.5 (1792 lines)", "x4 (2048 lines)",
};
#endif

int gioco_launcher(amiga_impostazioni_t *imp, int salta) {
    char install[1100];
    cartella_install(install, sizeof install);
#if defined(RECOMP_LAUNCHER)
    if (!salta) {
        RecompLauncherCSettings io, predef;
        memset(&io, 0, sizeof io);
        /* la scala "automatica": la piu' grande che entra nello schermo */
        int auto_k = 3;
        {
            SDL_DisplayMode dm;
            SDL_InitSubSystem(SDL_INIT_VIDEO);
            if (SDL_GetDesktopDisplayMode(0, &dm) == 0) auto_k = (dm.h - 120) / 256;
            if (auto_k < 1) auto_k = 1;
            if (auto_k > 8) auto_k = 8;
        }
        io.window_scale = imp->scala > 0 ? imp->scala : auto_k; io.fullscreen = imp->schermo_intero; io.linear_filter = imp->filtro == FILTRO_BILINEARE; io.filter_index = imp->filtro;
        io.enable_audio = imp->audio; io.audio_freq = 48000; io.volume = imp->volume;
        io.skip_launcher = imp->salta_launcher; io.screen_kind = imp->crt; io.integer_scale = imp->intera;
        io.aspect_index = imp->aspetto; io.language_index = imp->lingua;
        amiga_impostazioni_t d;
        amiga_impostazioni_predefinite(&d);
        memset(&predef, 0, sizeof predef);
        predef.window_scale = auto_k; predef.fullscreen = d.schermo_intero; predef.linear_filter = 0; predef.filter_index = d.filtro;
        predef.enable_audio = d.audio; predef.audio_freq = 48000; predef.volume = d.volume;
        predef.screen_kind = d.crt; predef.integer_scale = d.intera; predef.aspect_index = d.aspetto;
        predef.language_index = d.lingua;
        RecompLauncherCGameInfo gi;
        memset(&gi, 0, sizeof gi);
        launcher_profile_apply("amiga", &gi);
        gi.name = "Total Chaos AGA";
        gi.rom_noun = "Package";
        gi.default_settings = &predef;
        gi.expected_crc = TC_LHA_CRC;
        gi.has_expected_crc = 1;
        static const char *const schemi[] = { "*.lha", "*.lzh" };
        gi.rom_patterns = schemi;
        gi.num_rom_patterns = 2;
        gi.rom_filter_desc = "TotalChaosAGAr6.lha from Aminet (game/strat)";
        gi.language_labels = s_lingue_utf8;
        gi.num_languages = N_LINGUE;
        gi.ui_languages = s_codici;
        gi.tool_label = "Reset all settings";
        gi.tool_note = "Puts back both the game's own preferences (options, terrain, music... as in the Aminet package) "
                       "and this launcher's settings.";
        gi.tool_run = strumento;
        gi.tool_restores_defaults = 1;
        gi.rom_drop_zone = 1;
        gi.rom_drop_hint = "Drag TotalChaosAGAr6.lha here\n(from Aminet, game/strat)";
        gi.manual_pages = tc_manuale;
        gi.pointer_settings_hidden = 1;
        gi.pointer_legend =
            "Left and right buttons are the Amiga mouse buttons; the right one on the top bar opens the game "
            "menus. F12 or a left click while holding the middle button release the pointer (the middle button alone examines, like Q), a click in the window takes it back. "
            "Hold Tab to fast forward. The Manual explains the rest.";
        {
            char p[1200];
            snprintf(p, sizeof p, "%s/TotalChaos/Chaos", install);
            FILE *c = fopen(p, "rb");
            if (c) fclose(c);
            gi.dashboard_note = installato(install) ? "Installed in the install folder: ready to play."
                              : c ? "Installed with older changes: PLAY updates it (saved games and high scores stay)."
                                  : "Not installed yet: PLAY installs it in the install folder (about 20 seconds).";
        }
        gi.host_text_lang = "it";
        gi.host_text = s_testi_it;
        /* le forme dello schermo: quelle dei monitor dell'Amiga (TV PAL o NTSC,
         * multisync a pixel quadrati) e quelle di oggi (allargato, riempito) */
        static const char *const aspetti[AMIGA_ASPETTI] = {
            "PAL TV / 1084S (4:3)", "NTSC TV (4:3)", "Multisync, square pixels (5:4)",
            "Stretched 16:9", "Stretched 16:10", "Fill the window",
        };
        gi.aspect_labels = aspetti;
        gi.num_aspect_labels = AMIGA_ASPETTI;
        /* i filtri di scala del modello nitido (FILTRO_* in amiga_video_gl.h) */
        static const char *const filtri[AMIGA_FILTRI] = {
            "Sharp pixels", "Sharp bilinear (even lines)", "Bilinear (soft)", "Bicubic", "Lanczos",
            "Scale2x (rounded edges)",
        };
        gi.filter_labels = filtri;
        gi.num_filter_labels = AMIGA_FILTRI;
        gi.scale_labels = s_scale;
        gi.num_scale_labels = (int)(sizeof s_scale / sizeof s_scale[0]);
        gi.credits_text =
            "Total Chaos AGA 7.11 - James Conwell and Team Chaos (2008), freeware on Aminet (game/strat).\n"
            "PC port by static recompilation: SirL0LL0, with Claude. Italian translation completed for this port.\n"
            "The game's files are not included: they are extracted from TotalChaosAGAr6.lha downloaded from Aminet.";
        static char scelto[600];
        int rc = recomp_launcher_run_window(gi.name, &io, &gi, ".", imp->pacchetto, scelto, sizeof scelto);
        if (rc != 0) return 1;
        if (scelto[0]) snprintf(imp->pacchetto, sizeof imp->pacchetto, "%s", scelto);
        imp->scala = io.window_scale >= 0 ? io.window_scale : 0;
        imp->schermo_intero = io.fullscreen; imp->filtro = io.filter_index >= 0 && io.filter_index < AMIGA_FILTRI ? io.filter_index : FILTRO_NETTO; imp->audio = io.enable_audio;
        imp->volume = io.volume; imp->salta_launcher = io.skip_launcher;
        imp->crt = io.screen_kind >= 0 && io.screen_kind < AMIGA_SCHERMI ? io.screen_kind : SCHERMO_NITIDO;
        imp->intera = io.integer_scale ? 1 : 0;
        imp->aspetto = io.aspect_index >= 0 && io.aspect_index < AMIGA_ASPETTI ? io.aspect_index : ASPETTO_PAL;
        imp->lingua = io.language_index >= 0 && io.language_index < N_LINGUE ? io.language_index : 0;
        amiga_impostazioni_scrivi(imp);
    }
#else
    (void)salta;
#endif
    /* l'installazione, se manca o e' di modifiche vecchie */
    if (!installato(install)) {
        char err[800];
        if (!imp->pacchetto[0]) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Total Chaos AGA",
                                     "Choose TotalChaosAGAr6.lha (Aminet, game/strat) in the launcher first.", NULL);
            return 1;
        }
        if (!installa(imp->pacchetto, install, err, sizeof err)) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Total Chaos AGA", err, NULL);
            return 1;
        }
    }
    scrivi_lingua(install, imp->lingua);
    return 0;
}
