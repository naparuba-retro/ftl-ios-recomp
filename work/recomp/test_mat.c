/* Test : graphics_set_parallel_projection du jeu (code ARM recompile). */
#include <stdio.h>
#include <stdlib.h>
#include "rt.h"
#include "gen_syms.h"
void S_graphics_set_parallel_projection(void);
static uint32_t fbits(float f) { uint32_t v; memcpy(&v, &f, 4); return v; }
int main(void)
{
    rt_init_memory("geng");
    uint32_t top = rt_new_stack(1u << 20);
    uint32_t sp = top - 64;
    R32(REG_sp) = sp;
    R32(REG_lr) = 0;
    M32(sp) = fbits(-1.0f);       /* near, 5e argument */
    M32(sp + 4) = fbits(1.0f);    /* far, 6e argument */
    R32(REG_r0) = fbits(0.0f);    /* left */
    R32(REG_r1) = fbits(1024.0f); /* right */
    R32(REG_r2) = fbits(768.0f);  /* bottom */
    R32(REG_r3) = fbits(0.0f);    /* top */
    memset(MEM + sp - 512, 0, 512);
    S_graphics_set_parallel_projection();
    int found = 0;
    for (uint32_t a = sp - 512; a < sp; a += 4) {
        float f; memcpy(&f, MEM + a, 4);
        if (f == 2.0f / 1024.0f) { found = 1; printf("m11 trouve a sp-%u\n", sp - a); }
        if (f == 2.0f / -768.0f) printf("m22 (2/dy, dy=top-bottom=-768) trouve a sp-%u\n", sp - a);
    }
    printf("dernier sp-512..sp (floats non nuls) :");
    for (uint32_t a = sp - 512; a < sp; a += 4) { float f; memcpy(&f, MEM + a, 4); if (f != 0.0f) printf(" [%u]=%g", sp - a, f); }
    printf("\n%s\n", found ? "m11 OK" : "m11 ABSENT");
    return !found;
}
