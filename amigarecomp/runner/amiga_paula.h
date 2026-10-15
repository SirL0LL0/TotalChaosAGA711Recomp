/* amiga_paula.h - audio Paula (4 canali DMA) per il runner. */
#pragma once
#include <stdint.h>

void amiga_paula_init(uint16_t *intreq);                 /* dove alzare gli interrupt audio */
void amiga_paula_volume(int percento);
void amiga_paula_avanza(uint64_t cicli_cpu);             /* genera fino a questo istante */
void amiga_paula_registro(uint32_t offset, uint16_t v);  /* AUDx* ($0A0-$0DA) */
void amiga_paula_dmacon(uint16_t prima, uint16_t dopo);
int  amiga_paula_prendi(int16_t **campioni);             /* campioni stereo pronti, poi azzera */

/* per un driver audio del sistema (audio.device): i registri di un canale,
 * accendere/spegnere il suo DMA, e un aggancio chiamato a fine blocco,
 * prima che Paula ricarichi LC/LEN (li si puo' cambiare li' per la nota dopo) */
void amiga_paula_imposta(int canale, uint32_t lc, uint16_t len, uint16_t per, uint16_t vol);
void amiga_paula_per_vol(int canale, uint16_t per, uint16_t vol);
void amiga_paula_accendi(int canale);
void amiga_paula_spegni(int canale);
int  amiga_paula_acceso(int canale);
extern void (*amiga_paula_fine_blocco)(int canale);
