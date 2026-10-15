/* prova_lha.c - amiga_lha.c sull'archivio di Aminet: per ogni file percorso,
 * dimensione e CRC32, da confrontare con i file estratti da 7-Zip
 * (tools/prova_lha.py).
 *   clang -O2 -I amigarecomp/runner tools/prova_lha.c amigarecomp/runner/amiga_lha.c -o build/prova_lha.exe
 */
#include "amiga_lha.h"
#include <stdio.h>

static uint32_t crc32(const uint8_t *d, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= d[i];
        for (int k = 0; k < 8; k++) c = c >> 1 ^ (0xEDB88320u & -(c & 1));
    }
    return ~c;
}

static int scrivi(void *ctx, const char *perc, const uint8_t *dati, size_t n) {
    (void)ctx;
    printf("%s\t%zu\t%08X\n", perc, n, crc32(dati, n));
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) return 2;
    int n = lha_estrai_tutto(argv[1], NULL, scrivi);
    fprintf(stderr, "file estratti: %d\n", n);
    return n < 0;
}
