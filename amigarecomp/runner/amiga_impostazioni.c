/* amiga_impostazioni.c - impostazioni.ini accanto all'eseguibile */
#include "amiga_impostazioni.h"
#include "amiga_video_gl.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void percorso(char *p, size_t n) {
    char *qui = SDL_GetBasePath();
    snprintf(p, n, "%simpostazioni.ini", qui ? qui : "");
    SDL_free(qui);
}

void amiga_impostazioni_predefinite(amiga_impostazioni_t *s) {
    memset(s, 0, sizeof *s);
    s->scala = 0;                    /* la piu' grande che entra */
    s->intera = 1;
    s->crt = SCHERMO_NITIDO;
    s->aspetto = ASPETTO_PAL;
    s->audio = 1;
    s->volume = 80;
    s->lingua = 0;                   /* English, come il gioco originale */
}

void amiga_impostazioni_leggi(amiga_impostazioni_t *s) {
    amiga_impostazioni_predefinite(s);
    char p[1100];
    percorso(p, sizeof p);
    FILE *f = fopen(p, "r");
    if (!f) return;
    char riga[700];
    while (fgets(riga, sizeof riga, f)) {
        riga[strcspn(riga, "\r\n")] = 0;
        char *u = strchr(riga, '=');
        if (!u) continue;
        *u = 0;
        const char *k = riga, *val = u + 1;
        int v = atoi(val);
        if (!strcmp(k, "pacchetto")) snprintf(s->pacchetto, sizeof s->pacchetto, "%s", val);
        else if (!strcmp(k, "scala")) s->scala = v >= 0 && v <= 8 ? v : 0;
        else if (!strcmp(k, "schermo_intero")) s->schermo_intero = v != 0;
        else if (!strcmp(k, "filtro")) s->filtro = v != 0;
        else if (!strcmp(k, "scala_intera")) s->intera = v != 0;
        else if (!strcmp(k, "schermo")) s->crt = v >= 0 && v < AMIGA_SCHERMI ? v : SCHERMO_NITIDO;
        else if (!strcmp(k, "aspetto")) s->aspetto = v >= 0 && v < AMIGA_ASPETTI ? v : ASPETTO_PAL;
        else if (!strcmp(k, "audio")) s->audio = v != 0;
        else if (!strcmp(k, "volume")) s->volume = v >= 0 && v <= 100 ? v : 80;
        else if (!strcmp(k, "salta_launcher")) s->salta_launcher = v != 0;
        else if (!strcmp(k, "lingua")) s->lingua = v >= 0 && v < 32 ? v : 0;
    }
    fclose(f);
}

void amiga_impostazioni_scrivi(const amiga_impostazioni_t *s) {
    char p[1100];
    percorso(p, sizeof p);
    FILE *f = fopen(p, "w");
    if (!f) return;
    fprintf(f, "scala=%d\nschermo_intero=%d\nfiltro=%d\nscala_intera=%d\nschermo=%d\naspetto=%d\n",
            s->scala, s->schermo_intero, s->filtro, s->intera, s->crt, s->aspetto);
    fprintf(f, "audio=%d\nvolume=%d\nsalta_launcher=%d\nlingua=%d\npacchetto=%s\n",
            s->audio, s->volume, s->salta_launcher, s->lingua, s->pacchetto);
    fclose(f);
}

void amiga_impostazioni_cancella(void) {
    char p[1100];
    percorso(p, sizeof p);
    remove(p);
}
