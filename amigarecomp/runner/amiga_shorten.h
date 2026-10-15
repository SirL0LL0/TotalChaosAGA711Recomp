/* amiga_shorten.h - decompressione Shorten (Tony Robinson, versioni 0-3).
 *
 * Il pacchetto di Total Chaos su Aminet ha voci, effetti e moduli compressi
 * con "shorten -t s8 -p5" (firma "ajkg"); sull'Amiga li decomprimeva il
 * gioco al primo avvio (c/UnshortenEverything020). Qui li si decomprime
 * all'installazione.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

/* 1 se i dati cominciano con la firma di Shorten */
int shorten_e(const uint8_t *dati, size_t n);
/* decomprime: *uscita allocata con malloc (da liberare), *n_uscita byte.
 * 0 = errore (formato non supportato o dati rovinati) */
int shorten_decomprimi(const uint8_t *dati, size_t n, uint8_t **uscita, size_t *n_uscita);
