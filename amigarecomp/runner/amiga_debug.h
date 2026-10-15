/* amiga_debug.h - modalita' debug del runner: registrazione sempre attiva e
 * finestra di ispezione mentre si gioca.
 *
 * Sempre attivi (costano poco, girano anche senza finestra):
 *   - anello delle chiamate: ogni JSR/BSR con quadro, indirizzo di ritorno, A7
 *   - anello delle scritture sulle variabili osservate: vecchio e nuovo valore
 *     e la chiamata piu' recente (dove stava il gioco)
 *   - anello dei messaggi, che il gioco segnala con amiga_debug_messaggio()
 *     (finiscono anche in messaggi.log)
 * F11 apre/chiude la finestra, Shift+F11 salva tutto in debug_dump.txt.
 *
 * Il motore non conosce il gioco: variabili e agganci li dichiara il gioco
 * in g_debug_gioco (game/<gioco>_debug.c).
 */
#pragma once
#include <stdint.h>
#include <SDL.h>

typedef enum { DV_U8, DV_U16, DV_U32, DV_HEX8, DV_HEX16, DV_CIFRE8 } debug_formato_t;

typedef struct {
    uint32_t indirizzo;
    debug_formato_t formato;     /* DV_CIFRE8: 8 cifre decimali, una per byte */
    const char *nome;
} debug_var_t;

/* chiamata a una funzione che tornera' a 'ritorno': il gioco ci appende
 * le sue osservazioni (per esempio "messaggio composto") */
typedef struct {
    uint32_t ritorno;
    void (*fn)(uint32_t ritorno);
} debug_aggancio_t;

typedef struct {
    const debug_var_t *var;
    int n_var;
    const debug_aggancio_t *agganci;
    int n_agganci;
    const char *annotazioni;     /* CSV indirizzo,nome,note per i nomi delle funzioni */
} debug_gioco_t;

extern const debug_gioco_t g_debug_gioco;

/* attivo: F11 (finestra), Shift+F11 (dump), messaggi.log e le variabili
 * osservate; altrimenti restano solo gli agganci del gioco. apri_finestra:
 * la finestra subito */
void amiga_debug_init(int attivo, int apri_finestra);
void amiga_debug_chiamata(uint32_t ritorno, uint32_t a7);
void amiga_debug_scrittura(uint32_t indirizzo, int taglia, uint32_t vecchio, uint32_t nuovo);
void amiga_debug_messaggio(int numero, const char *testo);
void amiga_debug_quadro(uint64_t quadro);
int  amiga_debug_evento(const SDL_Event *ev);    /* 1 = consumato */
void amiga_debug_dump(const char *file);

/* contatori per la finestra (sempre attivi: solo incrementi) */
typedef struct {
    uint64_t chiamate;           /* JSR/BSR del codice ricompilato */
    uint64_t os;                 /* funzioni di sistema in C */
    uint64_t interpretate;       /* ingressi in Musashi (codice caricato a runtime) */
    uint64_t salti;              /* JMP indiretti risolti al volo */
    uint64_t custom_l, custom_s; /* registri dei chip custom letti/scritti */
    uint64_t cia;                /* accessi ai CIA */
    uint64_t suoni, suoni_saltati, file;
} debug_stat_t;
extern debug_stat_t g_dstat;

/* copertura delle partite giocate (--debug): cosa e' successo, sommato a
 * quello delle sessioni precedenti in copertura_giocatore.txt/.bin accanto
 * all'eseguibile. tipo: "api", "suono", "file", "menu", "tasto", "manca"... */
void amiga_debug_evento_gioco(const char *tipo, const char *nome);
void amiga_debug_api(int lib, int k, const char *libreria, const char *funzione);
void amiga_debug_copertura(uint8_t *bitmap, uint32_t byte);   /* la bitmap dei punti di chiamata */
void amiga_debug_salva(void);                                 /* copertura su disco (anche all'uscita) */

/* bitmap delle variabili osservate: test veloce dentro m68k_write* */
extern uint8_t g_debug_osservati[0x100000 / 8];
static inline int amiga_debug_osservato(uint32_t a) {
    return a < 0x100000u && (g_debug_osservati[a >> 3] >> (a & 7) & 1);
}
