/* amiga_lha.h - archivi LHA (lettura lh0/lh5/lh6/lh7, scrittura lh0) */
#pragma once
#include <stddef.h>
#include <stdint.h>

/* estrae tutti i file dell'archivio chiamando scrivi(cartella, nome, dati, n)
 * (0 = scritto); restituisce quanti, -1 se l'archivio non c'e' */
int lha_estrai(const char *archivio, const char *cartella,
               int (*scrivi)(const char *cartella, const char *nome, const uint8_t *dati, size_t n));
/* un archivio nuovo con un file solo (-lh0-) */
int lha_crea(const char *archivio, const char *nome_nel_archivio, const uint8_t *dati, size_t n);
