/* dis68k.c - disassembla un'immagine piatta di memoria 68k con il
 * disassemblatore di Musashi (external/musashi/m68kdasm.c).
 *
 *   dis68k <immagine.bin> <base hex> <da hex> <a hex> [68000|68020]
 *
 * L'immagine e' la memoria a partire da <base> (per esempio un hunk caricato
 * e rilocato da game/hunk_amiga.py). Compilazione:
 *   clang -O1 -I external/musashi -I build/c2/musashi tools/dis68k.c external/musashi/m68kdasm.c -o build/dis68k.exe
 */
#include <stdio.h>
#include <stdlib.h>
#include "m68k.h"

static unsigned char *s_mem;
static unsigned s_base, s_dim;

static unsigned leggi8(unsigned a) { a -= s_base; return a < s_dim ? s_mem[a] : 0; }
unsigned int m68k_read_disassembler_8(unsigned int a) { return leggi8(a); }
unsigned int m68k_read_disassembler_16(unsigned int a) { return leggi8(a) << 8 | leggi8(a + 1); }
unsigned int m68k_read_disassembler_32(unsigned int a) {
    return m68k_read_disassembler_16(a) << 16 | m68k_read_disassembler_16(a + 2);
}
/* m68kdasm.c non usa la CPU, ma m68k.h dichiara queste per il linker */
unsigned int m68k_read_memory_8(unsigned int a) { return leggi8(a); }
unsigned int m68k_read_memory_16(unsigned int a) { return m68k_read_disassembler_16(a); }
unsigned int m68k_read_memory_32(unsigned int a) { return m68k_read_disassembler_32(a); }
void m68k_write_memory_8(unsigned int a, unsigned int v) { (void)a; (void)v; }
void m68k_write_memory_16(unsigned int a, unsigned int v) { (void)a; (void)v; }
void m68k_write_memory_32(unsigned int a, unsigned int v) { (void)a; (void)v; }

int main(int argc, char **argv) {
    if (argc < 5) { fprintf(stderr, "uso: dis68k <immagine> <base> <da> <a> [68000|68020]\n"); return 1; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END); s_dim = (unsigned)ftell(f); fseek(f, 0, SEEK_SET);
    s_mem = malloc(s_dim);
    if (!s_mem || fread(s_mem, 1, s_dim, f) != s_dim) return 1;
    fclose(f);
    s_base = (unsigned)strtoul(argv[2], NULL, 16);
    unsigned pc = (unsigned)strtoul(argv[3], NULL, 16), fine = (unsigned)strtoul(argv[4], NULL, 16);
    unsigned cpu = argc > 5 && atoi(argv[5]) == 68020 ? M68K_CPU_TYPE_68020 : M68K_CPU_TYPE_68000;
    char buf[256];
    while (pc < fine) {
        unsigned n = m68k_disassemble(buf, pc, cpu);
        printf("%06X  ", pc);
        for (unsigned i = 0; i < 10; i += 2) {
            if (i < n) printf("%04X ", m68k_read_disassembler_16(pc + i)); else printf("     ");
        }
        printf(" %s\n", buf);
        pc += n ? n : 2;
    }
    return 0;
}
