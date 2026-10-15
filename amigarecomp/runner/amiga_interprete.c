/* amiga_interprete.c - il codice 68000 che il programma carica a runtime,
 * eseguito da Musashi.
 *
 * Il ricompilatore vede solo l'immagine preparata prima. Un programma che
 * carica codice da disco e ci salta dentro (Total Chaos: Mod.ChaosTheme78 e'
 * un modulo "custom" col suo player in 68000) finirebbe in un DISPATCH MISS.
 * Qui (GameSpec.dispatch_override) un salto in RAM fuori dall'immagine si fa
 * eseguire a Musashi come una chiamata: l'indirizzo di ritorno in pila e'
 * sostituito da una sentinella, e Musashi va finche' non ci torna. Le
 * chiamate che quel codice fa al programma ricompilato o a una libreria
 * (tabella dei salti) tornano native: l'aggancio di istruzione sposta il PC
 * su un NOP, si chiama la funzione con i registri di Musashi e il ritorno
 * gia' in pila, e si riprende all'indirizzo che la funzione ha lasciato.
 * La memoria passa dalle funzioni del runner: chip custom e CIA compresi.
 */
#include "amiga_os.h"
#include "genesis_runtime.h"
#include "m68k.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern uint32_t os_fine_programma;
extern uint64_t g_os_quadri;
int g_musashi_ibrido;                          /* letto da amiga_verifica.c (memoria, aggancio) */
int os_e_vettore_libreria(uint32_t addr);      /* amiga_os.c */
int glue_slot_spingi(uint32_t a7);             /* amiga_glue.c */
void glue_slot_ripristina(int n);

#define INIZIO_IMMAGINE 0x200000u
#define SENTINELLA      0xF00000u              /* amiga_verifica.c ci legge NOP */

static enum { CORRE, FINE, NATIVO } s_st;
static uint32_t s_pc_nativo;
static unsigned long s_istruzioni, s_chiamate;

static int nativo(uint32_t pc) {
    return (pc >= INIZIO_IMMAGINE && pc < os_fine_programma) || os_e_vettore_libreria(pc);
}

/* prima di ogni istruzione (amiga_verifica.c lo chiama in modo ibrido) */
void ibrido_istruzione(unsigned int pc) {
    s_istruzioni++;
    if (s_st != CORRE) { m68k_set_reg(M68K_REG_PC, SENTINELLA + 4); m68k_end_timeslice(); return; }
    if (pc == SENTINELLA) { s_st = FINE; m68k_end_timeslice(); return; }
    if (nativo(pc)) {
        s_st = NATIVO;
        s_pc_nativo = pc;
        m68k_set_reg(M68K_REG_PC, SENTINELLA + 4);  /* un NOP al posto dell'istruzione nativa */
        m68k_end_timeslice();
    }
}

static void a_musashi(void) {
    m68k_set_reg(M68K_REG_SR, g_cpu.SR & 0x1F);              /* modo utente, i flag */
    for (int i = 0; i < 8; i++) {
        m68k_set_reg((m68k_register_t)(M68K_REG_D0 + i), g_cpu.D[i]);
        m68k_set_reg((m68k_register_t)(M68K_REG_A0 + i), g_cpu.A[i]);
    }
}
static void da_musashi(void) {
    for (int i = 0; i < 8; i++) {
        g_cpu.D[i] = m68k_get_reg(NULL, (m68k_register_t)(M68K_REG_D0 + i));
        g_cpu.A[i] = m68k_get_reg(NULL, (m68k_register_t)(M68K_REG_A0 + i));
    }
    g_cpu.SR = (uint16_t)((g_cpu.SR & ~0x1Fu) | (m68k_get_reg(NULL, M68K_REG_SR) & 0x1F));
}

int amiga_interpreta(uint32_t addr) {
    static int iniziato, livello;
    if (!os_e_ram(addr) || nativo(addr)) return 0;
    if (!iniziato) {
        m68k_init();
        m68k_set_cpu_type(M68K_CPU_TYPE_68020);
        g_musashi_ibrido = 1;
        s_st = FINE;
        m68k_pulse_reset();
        iniziato = 1;
        fprintf(stderr, "[interprete] codice caricato a runtime a $%06X (quadro %llu): lo esegue Musashi\n",
                addr, (unsigned long long)g_os_quadri);
    }
    /* una chiamata annidata (il codice interpretato chiama il nativo, che
     * torna a chiamare codice interpretato): il contesto di fuori si salva */
    void *fuori = NULL;
    int st_fuori = s_st;
    if (livello) {
        fuori = malloc(m68k_context_size());
        if (fuori) m68k_get_context(fuori);
    }
    livello++;
    g_musashi_ibrido = 1;
    uint32_t a7 = g_cpu.A[7], ritorno = m68k_read32(a7);
    m68k_write32(a7, SENTINELLA);
    a_musashi();
    m68k_set_reg(M68K_REG_PC, addr);
    s_st = CORRE;
    for (;;) {
        while (s_st == CORRE) m68k_execute(100000);
        da_musashi();
        if (s_st == FINE) break;
        /* una chiamata nativa: il ritorno e' in cima alla pila */
        uint32_t pila = g_cpu.A[7];
        int n = glue_slot_spingi(pila);
        s_chiamate++;
        call_by_address(s_pc_nativo);
        glue_slot_ripristina(n);
        uint32_t r = m68k_read32(pila);
        if (g_cpu.A[7] == pila) g_cpu.A[7] += 4;               /* l'RTS ricompilato lascia il ritorno */
        if (r == SENTINELLA) break;
        a_musashi();
        m68k_set_reg(M68K_REG_PC, r);
        s_st = CORRE;
    }
    m68k_write32(a7, ritorno);
    g_cpu.A[7] = a7 + 4;
    livello--;
    if (fuori) { m68k_set_context(fuori); free(fuori); }
    s_st = st_fuori;
    if (!livello) g_musashi_ibrido = 0;
    return 1;
}
