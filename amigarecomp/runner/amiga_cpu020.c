/* amiga_cpu020.c - le istruzioni del 68020 che il C generato delega al
 * runner: i campi di bit (BFTST, BFEXTU, BFCHG, BFEXTS, BFCLR, BFFFO, BFSET,
 * BFINS). L'indirizzamento esteso e la scala dell'indice stanno in
 * genesis_runtime.h.
 *
 * Un campo e' lungo da 1 a 32 bit (0 vuol dire 32) e parte da un offset in
 * bit contato dal bit piu' alto: in un registro dati l'offset si prende
 * modulo 32 e il campo gira attorno al registro; in memoria l'offset (con
 * segno se viene da un registro) sposta l'indirizzo di offset/8 byte e il
 * campo puo' toccare fino a cinque byte.
 */
#include "genesis_runtime.h"

extern M68KState g_cpu;
uint8_t m68k_read8(uint32_t addr);
void    m68k_write8(uint32_t addr, uint8_t val);

static void flag_campo(uint32_t campo, uint32_t larghezza) {
    g_cpu.SR &= ~0x0Fu;                                  /* N Z V C */
    if (campo == 0) g_cpu.SR |= SR_Z;
    if (campo >> (larghezza - 1) & 1u) g_cpu.SR |= SR_N;
}

uint32_t m68k_bitfield(int op, int dreg, uint32_t addr, int32_t offset,
                       uint32_t width, uint32_t insert) {
    uint32_t w = width & 31u;
    if (w == 0) w = 32;
    uint32_t maschera = w == 32 ? 0xFFFFFFFFu : ((1u << w) - 1u);
    uint32_t campo, ris = 0;

    if (dreg >= 0) {
        /* registro: il campo parte dal bit (31 - offset) e gira attorno */
        uint32_t o = (uint32_t)offset & 31u;
        uint32_t v = g_cpu.D[dreg];
        uint32_t ruotato = o ? (v << o | v >> (32 - o)) : v;   /* campo in alto */
        campo = (ruotato >> (32 - w)) & maschera;
        flag_campo(campo, w);
        uint32_t nuovo = campo;
        switch (op) {
        case 2: nuovo = ~campo & maschera; break;
        case 4: nuovo = 0; break;
        case 6: nuovo = maschera; break;
        case 7: nuovo = insert & maschera;
                g_cpu.SR &= ~0x0Fu;                     /* BFINS: flag dal valore inserito */
                if (nuovo == 0) g_cpu.SR |= SR_Z;
                if (nuovo >> (w - 1) & 1u) g_cpu.SR |= SR_N;
                break;
        }
        if (op == 2 || op == 4 || op == 6 || op == 7) {
            uint32_t m_alto = w == 32 ? 0xFFFFFFFFu : (maschera << (32 - w));
            ruotato = (ruotato & ~m_alto) | (nuovo << (32 - w));
            g_cpu.D[dreg] = o ? (ruotato >> o | ruotato << (32 - o)) : ruotato;
        }
    } else {
        /* memoria: al massimo 5 byte da addr + floor(offset / 8) */
        int32_t byte = offset >> 3;                     /* divisione per difetto anche se negativo */
        uint32_t bit = (uint32_t)offset & 7u;
        uint32_t a = addr + (uint32_t)byte;
        uint32_t nbyte = (bit + w + 7) / 8;
        uint64_t v = 0;
        for (uint32_t i = 0; i < nbyte; i++) v = v << 8 | m68k_read8(a + i);
        uint32_t sposta = nbyte * 8 - bit - w;
        campo = (uint32_t)(v >> sposta) & maschera;
        flag_campo(campo, w);
        uint32_t nuovo = campo;
        switch (op) {
        case 2: nuovo = ~campo & maschera; break;
        case 4: nuovo = 0; break;
        case 6: nuovo = maschera; break;
        case 7: nuovo = insert & maschera;
                g_cpu.SR &= ~0x0Fu;
                if (nuovo == 0) g_cpu.SR |= SR_Z;
                if (nuovo >> (w - 1) & 1u) g_cpu.SR |= SR_N;
                break;
        }
        if (op == 2 || op == 4 || op == 6 || op == 7) {
            uint64_t m = (uint64_t)maschera << sposta;
            v = (v & ~m) | ((uint64_t)nuovo << sposta);
            for (uint32_t i = 0; i < nbyte; i++)
                m68k_write8(a + i, (uint8_t)(v >> (8 * (nbyte - 1 - i))));
        }
    }

    switch (op) {
    case 1: ris = campo; break;                                   /* BFEXTU */
    case 3: ris = (w < 32 && (campo >> (w - 1) & 1u)) ? (campo | ~maschera) : campo; break;  /* BFEXTS */
    case 5: {                                                     /* BFFFO */
        uint32_t i = 0;
        while (i < w && !(campo >> (w - 1 - i) & 1u)) i++;
        ris = (uint32_t)offset + i;
        break;
    }
    }
    return ris;
}
