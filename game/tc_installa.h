/* tc_installa.h - installazione del gioco dal pacchetto di Aminet (tc_installa.c) */
#pragma once
#include <stddef.h>

#define TC_LHA_DIMENSIONE 95779935u
#define TC_LHA_CRC        0x793901A5u

/* fatti/totale: file dell'archivio; fase: "estrazione", "modifiche" */
typedef void (*tc_progresso_fn)(void *ctx, int fatti, int totale, const char *fase);

/* lha: TotalChaosAGAr6.lha; dati: la cartella da riempire (diventa
 * <dati>/TotalChaos); modifiche: tc_modifiche.bin o NULL (solo l'originale).
 * 1 = fatto; err: il messaggio (anche di riuscita) */
int tc_installa(const char *lha, const char *dati, const char *modifiche, tc_progresso_fn progresso, void *ctx,
                char *err, size_t ne);
