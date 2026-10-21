/* amiga_lola.h - Lola Retro 1084S (amiga_lola.c): nove passaggi GLSL 3.30. */
#pragma once
#include <stdint.h>

int  lola_init(void);          /* 1 pronto, 0 non disponibile (serve GLSL 3.30) */
/* il quadro w x h ARGB nel rettangolo (vx, vy, vw, vh) della finestra */
void lola_presenta(const uint32_t *pixel, int w, int h, int vx, int vy, int vw, int vh);
