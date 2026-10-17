/* amiga_video_gl.h - presentazione OpenGL: pixel netti o filtri CRT. */
#pragma once
#include <stdint.h>
#include <SDL.h>

/* modelli di schermo, nell'ordine del launcher (amiga_profile.h) */
enum { SCHERMO_NITIDO, SCHERMO_1084S, SCHERMO_EASYMODE, SCHERMO_SCANLINE, AMIGA_SCHERMI };
const char *amiga_video_gl_nome(int modo);

int  amiga_video_gl_init(SDL_Window *win, int modo);   /* 0 = non disponibile: usare SDL_Renderer */
void amiga_video_gl_modo(int modo);                    /* SCHERMO_* */
int  amiga_video_gl_modo_attuale(void);
void amiga_video_gl_filtro(int lineare);               /* pixel netti ammorbiditi (filtro lineare) */
void amiga_video_gl_scala_intera(int si);              /* 1 pixel perfetti, 0 il piu' grande possibile */
/* forma del pixel, nell'ordine del launcher: PAL o NTSC (come su un monitor
 * 4:3 di quello standard), quadrata (monitor multisync / flicker fixer: 640 x
 * 512 a 5:4), allargata a 16:9 o 16:10 (televisori e monitor di oggi), o
 * tutta la finestra (riempita in ogni direzione) */
enum { ASPETTO_PAL, ASPETTO_NTSC, ASPETTO_QUADRATI, ASPETTO_169, ASPETTO_1610, ASPETTO_RIEMPI, AMIGA_ASPETTI };
double amiga_video_gl_par(int aspetto);                /* larghezza di un pixel / altezza */
void amiga_video_gl_aspetto(int aspetto);              /* ASPETTO_* */
void amiga_video_gl_presenta(const uint32_t *pixel);   /* quadro 320x256 ARGB */
/* un quadro di altro formato (es. 640 x 512, il video di Intuition): le
 * righe valgono per rettangolo e texture al posto di quelle dell'hardware */
void amiga_video_gl_dimensioni(int larghezza, int altezza);
void amiga_video_gl_righe_scala(int righe);   /* righe vere per la scala intera (0 = quelle del quadro) */
/* dove va il quadro (righe x 320) in una finestra w x h con la forma del
 * pixel ASPETTO_*; con intera il fattore verticale e' intero (il piu'
 * grande possibile), senza riempie la finestra */
void amiga_video_gl_rettangolo(int modo, int righe, int intera, int aspetto, int w, int h, int *vw, int *vh);
/* lo stesso con le impostazioni in uso (scala intera, forma del pixel): il
 * mouse deve cadere dove si disegna */
void amiga_video_gl_rettangolo_attuale(int righe, int w, int h, int *vw, int *vh);
