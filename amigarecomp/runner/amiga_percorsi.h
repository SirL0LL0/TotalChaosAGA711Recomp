/* amiga_percorsi.h - percorsi lunghi su Windows: incluso in ogni file del
 * runner e del gioco (CMake, -include). I percorsi oltre i 240 caratteri
 * (il gioco ne ha di molto lunghi: Tutorials/Hints/English/
 * SecretStuffForDevelopers/...) passano col prefisso \\?\, che toglie il
 * limite di 260 di Windows; quelli corti restano come sono. */
#pragma once
#if defined(_WIN32) && !defined(AMIGA_PERCORSI_VERI)    /* amiga_percorsi.c usa quelle vere */
#include <stdio.h>
#include <sys/stat.h>
#include <dirent.h>
#include <direct.h>
#include <io.h>

FILE *lp_fopen(const char *p, const char *modo);
int lp_remove(const char *p);
int lp_rename(const char *a, const char *b);
int lp_stat(const char *p, struct stat *st);
DIR *lp_opendir(const char *p);
int lp_mkdir(const char *p);

#define fopen(p, m) lp_fopen(p, m)
#define remove(p) lp_remove(p)
#define rename(a, b) lp_rename(a, b)
#define stat(p, s) lp_stat(p, s)
#define opendir(p) lp_opendir(p)
#define mkdir(p) lp_mkdir(p)
#define _mkdir(p) lp_mkdir(p)
#endif
