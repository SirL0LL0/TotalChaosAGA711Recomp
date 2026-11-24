/* amiga_posix.h - le funzioni del C di Windows che il runner usa, su macOS e
 * Linux (incluso in tutti i file C da CMakeLists.txt, fuori da Windows) */
#ifndef AMIGA_POSIX_H
#define AMIGA_POSIX_H
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* mkdir con un argomento solo, come quello di Windows (e con due), con i
 * nomi Latin-1 convertiti (amiga_percorsi.c) */
#ifdef AMIGA_PERCORSI_VERI
static inline int amiga_mkdir(const char *p, int modo, ...) { return (mkdir)(p, (mode_t)modo); }
#else
int lp_mkdir(const char *p);
static inline int amiga_mkdir(const char *p, int modo, ...) { (void)modo; return lp_mkdir(p); }
#endif
#define mkdir(...) amiga_mkdir(__VA_ARGS__, 0777)
#define _mkdir(p) amiga_mkdir((p), 0777)
#define _putenv putenv
#define _getcwd getcwd
#include "amiga_percorsi.h"
#endif