/* amiga_hw.h - l'hardware Amiga (OCS) visto dal runner.
 *
 * Il runner non conosce il gioco: memoria, chip custom, CIA e video sono
 * quelli dell'A500. Cio' che e' del gioco arriva da g_game_spec.
 */
#pragma once
#include <stdint.h>

#define AMIGA_CICLI_RIGA    454u                 /* 68000 a 7,09 MHz, riga PAL */
#define AMIGA_RIGHE_QUADRO  313u
#define AMIGA_CICLI_QUADRO  (AMIGA_CICLI_RIGA * AMIGA_RIGHE_QUADRO)
#define AMIGA_LARGHEZZA     320
#define AMIGA_ALTEZZA       256
#define AMIGA_ALTEZZA_GIOCO 200                  /* righe usate di solito (finestra video NTSC-safe) */
#define AMIGA_PAR           (4.0 / 3.0 * 256.0 / 320.0)   /* forma del pixel lowres PAL su un 4:3 */
#define AMIGA_PAR_NTSC      (4.0 / 3.0 * 200.0 / 320.0)   /* e NTSC: 200 righe riempiono il 4:3 */

extern uint8_t *g_amiga_mem;          /* immagine della chip RAM (e oltre) */
extern uint32_t g_amiga_mem_size;

/* mappa a 32 bit, come un A1200 con scheda acceleratrice:
 *   $000000-$1FFFFF  chip (vettori, strutture di sistema, pila sotto $200000)
 *   $200000-$9FFFFF  fast bassa: librerie e programma (il codice sta sotto
 *                    $400000 per il ricompilatore), poi AllocMem
 *   $A00000-$FFFFFF  CIA e chip custom (amiga_hw)
 *   $08000000-...    OS_FAST_DIM di fast a 32 bit
 * g_amiga_mem copre tutto fino a OS_MEM_TOT. Le basi delle librerie stanno
 * sotto i 16 MB (MEMF_24BITDMA): il dispatcher del codice generato ragiona
 * ancora a 24 bit sugli indirizzi di salto. */
#define OS_MEM          0xA00000u     /* fine della memoria bassa (chip + fast bassa) */
#define OS_FAST_ALTA    0x08000000u
#define OS_FAST_DIM     0x08000000u   /* 128 MB (32 MB e' il minimo consigliato) */
#define OS_MEM_TOT      (OS_FAST_ALTA + OS_FAST_DIM)
/* RAM (1) o chip custom/CIA/niente (0) */
static inline int os_e_ram(uint32_t a) { return a < OS_MEM || (uint32_t)(a - OS_FAST_ALTA) < OS_FAST_DIM; }
extern uint64_t g_amiga_letture_vhpos;

/* velocita' della CPU rispetto al 68000 a 7,09 MHz (1 = A500, 12 = A4000/040):
 * il C generato conta cicli 68000, e in un quadro la CPU ne fa
 * AMIGA_CICLI_QUADRO * g_amiga_cpu_x (g_vblank_threshold); pennello, audio e
 * tempo dei chip custom restano quelli veri (cicli / g_amiga_cpu_x) */
extern uint32_t g_amiga_cpu_x;

void     amiga_hw_init(void);
int      amiga_hw_audio(int16_t **campioni);   /* campioni stereo a 48 kHz del quadro, poi azzera */

/* registri e periferiche */
uint8_t  amiga_hw_read8(uint32_t addr);
uint16_t amiga_hw_read16(uint32_t addr);
void     amiga_hw_write8(uint32_t addr, uint8_t val);
void     amiga_hw_write16(uint32_t addr, uint16_t val);

/* CIA-B: latch del timer A (0) o B (1) e se e' avviato */
unsigned amiga_hw_ciab_timer(int b);
int      amiga_hw_ciab_avviato(int b);
/* CIA-A: timer A (0) o B (1), avviato, abilitato nell'ICR; AbleICR */
unsigned amiga_hw_ciaa_timer(int b);
int      amiga_hw_ciaa_avviato(int b);
int      amiga_hw_ciaa_abilitato(int bit);
uint8_t  amiga_hw_ciaa_ableicr(uint8_t maschera);

/* interrupt in attesa e abilitati: livello 1-7, 0 = nessuno */
int      amiga_hw_livello_interrupt(void);

/* fine quadro: copper + bitplane nel framebuffer, tastiera, VBlank */
void     amiga_hw_fine_quadro(void);
const uint32_t *amiga_hw_framebuffer(void);
/* righe usate dal gioco, dall'alto del framebuffer: la finestra video
 * (DIWSTOP - DIWSTRT); sotto e' sempre nero */
int      amiga_hw_altezza_visibile(void);

/* ingressi dall'host */
void     amiga_hw_tasto(uint8_t codice_raw, int rilasciato);
void     amiga_hw_joystick(int su, int giu, int sx, int dx, int fuoco);
void     amiga_hw_mouse(int dx, int dy, int sinistro, int destro);   /* porta 0 */
