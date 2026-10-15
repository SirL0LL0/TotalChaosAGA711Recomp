/* amiga_voce.h - i fonemi del narrator.device detti dalla voce di Windows. */
#pragma once

/* comincia a dire i fonemi (in sottofondo); 0 = niente voce, la frase e' gia' finita */
int  voce_parla(const char *fonemi, int n, int parole_al_minuto, int femmina);
int  voce_finita(void);
void voce_ferma(void);
