/* amiga_impostazioni.h - impostazioni.ini accanto all'eseguibile: quello che
 * si sceglie nel launcher e vale anche alla partenza successiva */
#pragma once

typedef struct {
    int scala;                       /* 0 = la piu' grande che entra nello schermo */
    int schermo_intero, filtro, intera;
    int crt;                         /* SCHERMO_* (amiga_video_gl.h) */
    int aspetto;                     /* ASPETTO_* */
    int audio, volume;               /* volume 0-100 */
    int salta_launcher;
    int lingua;                      /* lingua del gioco: indice nell'elenco del gioco (0 = English) */
    char pacchetto[512];             /* il pacchetto del gioco scelto ("" = nessuno) */
} amiga_impostazioni_t;

void amiga_impostazioni_predefinite(amiga_impostazioni_t *s);
void amiga_impostazioni_leggi(amiga_impostazioni_t *s);     /* le predefinite se il file non c'e' */
void amiga_impostazioni_scrivi(const amiga_impostazioni_t *s);
void amiga_impostazioni_cancella(void);                      /* torna alle predefinite */

/* il gioco: launcher, installazione, preparazione dei dati. 0 = si parte,
 * altrimenti si esce. salta: niente launcher (--senza-launcher) */
int gioco_launcher(amiga_impostazioni_t *s, int salta);
