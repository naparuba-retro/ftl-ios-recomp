/* Test : decompresse des entrees de ftl.dat avec le code ARM recompile (decompress_to)
 * et compare avec les sorties attendues produites par Python.
 * Usage : test_zlib gen_dir vec.bin decompress_to_hex_addr */
#include <stdio.h>
#include <stdlib.h>
#include "rt.h"

void rt_init_memory(const char *gendir);
void rt_commit(uint32_t start, uint32_t size);
void f_001e87c4(void); /* _decompress_to : adresse verifiee dans Ghidra */

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage\n"); return 1; }
    rt_init_memory(argv[1]);
    const uint32_t STACK_TOP = 0x7F800000, IN = 0x30000000, OUT = 0x40000000;
    rt_commit(STACK_TOP - 0x100000, 0x100000);
    FILE *f = fopen(argv[2], "rb");
    if (!f) { perror("vec"); return 1; }
    uint32_t n;
    fread(&n, 4, 1, f);
    int ok = 0, bad = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t insz, outsz;
        fread(&insz, 4, 1, f);
        fread(&outsz, 4, 1, f);
        uint8_t *in = malloc(insz), *exp = malloc(outsz);
        fread(in, 1, insz, f);
        fread(exp, 1, outsz, f);
        rt_commit(IN, insz + 16);
        rt_commit(OUT, outsz + 16);
        memcpy(MEM + IN, in, insz);
        memset(MEM + OUT, 0xAA, outsz + 16);
        uint32_t sp = STACK_TOP - 64;
        uint32_t ret_ptr = sp + 16;          /* int32_t *out_size_ret */
        M32(ret_ptr) = 0xdeadbeef;
        M32(sp) = ret_ptr;                   /* 5e argument sur la pile */
        R32(REG_sp) = sp;
        R32(REG_lr) = 0;
        R32(REG_r0) = IN;
        R32(REG_r1) = insz;
        R32(REG_r2) = OUT;
        R32(REG_r3) = outsz;
        f_001e87c4();
        uint32_t res = R32(REG_r0), got = M32(ret_ptr);
        int good = res == 1 && got == outsz && memcmp(MEM + OUT, exp, outsz) == 0;
        if (good) ok++; else {
            bad++;
            printf("ECHEC entree %u : retour=%u taille=%u (attendu %u)\n", i, res, got, outsz);
        }
        free(in); free(exp);
    }
    printf("%d identiques, %d differents sur %u\n", ok, bad, n);
    return bad ? 1 : 0;
}
