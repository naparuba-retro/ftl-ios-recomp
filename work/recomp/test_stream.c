/* Test : to_int / to_float du jeu (code recompile) avec l'emulation iostream. */
#include <stdio.h>
#include <stdlib.h>
#include "rt.h"
void hle_stream_init_data(void);
void gs_init(uint32_t sp, const uint8_t *t, uint32_t n);
void f_00016f1c(void);   /* to_int */
void f_001916f0(void);   /* to_float */
int main(void)
{
    rt_init_memory("geng");
    hle_stream_init_data();
    uint32_t top = rt_new_stack(1u << 20);
    const char *tests[] = { "42", "-7", "  15", "3.5", "100x", "0" };
    for (int i = 0; i < 6; i++) {
        uint32_t s = rt_alloc(16);
        gs_init(s, (const uint8_t *)tests[i], (uint32_t)strlen(tests[i]));
        R32(REG_sp) = top - 64; R32(REG_lr) = 0;
        R32(REG_r0) = s;
        f_00016f1c();
        int vi = (int)R32(REG_r0);
        R32(REG_sp) = top - 64; R32(REG_r0) = s;
        f_001916f0();
        float vf; uint32_t b = R32(REG_r0); memcpy(&vf, &b, 4);
        printf("\"%s\" -> to_int=%d to_float=%g\n", tests[i], vi, vf);
    }
    return 0;
}
