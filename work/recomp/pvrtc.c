/* Decodeur PVRTC1 (2 et 4 bpp) vers RGBA8, utilise quand le jeu envoie des textures PVRTC
 * a un GPU qui ne les gere pas (la quasi-totalite des GPU de PC).
 * Port du decodeur Python de work/convert_all.py (ordre de Morton : y sur les bits bas). */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static uint32_t morton(uint32_t bx, uint32_t by, int nbits)
{
    uint32_t m = 0;
    for (int i = 0; i < nbits; i++) {
        m |= ((by >> i) & 1u) << (2 * i);
        m |= ((bx >> i) & 1u) << (2 * i + 1);
    }
    m |= ((bx >> nbits) << (2 * nbits)) | ((by >> nbits) << (2 * nbits));
    return m;
}

static int e5(int v) { return (v << 3) | (v >> 2); }

static void colors(uint32_t c, float A[4], float B[4], int *mode)
{
    uint32_t a16 = c & 0xFFFF, b16 = c >> 16;
    *mode = (int)(c & 1);
    int r, g, b, al;
    if (a16 & 0x8000) {
        r = (a16 >> 10) & 31; g = (a16 >> 5) & 31;
        int b4 = (a16 >> 1) & 15; b = (b4 << 1) | (b4 >> 3);
        al = 255;
    } else {
        int a3 = (a16 >> 12) & 7, r4 = (a16 >> 8) & 15, g4 = (a16 >> 4) & 15, b3 = (a16 >> 1) & 7;
        r = (r4 << 1) | (r4 >> 3); g = (g4 << 1) | (g4 >> 3); b = (b3 << 2) | (b3 >> 1);
        al = (a3 << 5) | (a3 << 2) | (a3 >> 1);
    }
    A[0] = (float)e5(r); A[1] = (float)e5(g); A[2] = (float)e5(b); A[3] = (float)al;
    if (b16 & 0x8000) {
        r = (b16 >> 10) & 31; g = (b16 >> 5) & 31; b = b16 & 31; al = 255;
    } else {
        int a3 = (b16 >> 12) & 7, r4 = (b16 >> 8) & 15, g4 = (b16 >> 4) & 15, b4 = b16 & 15;
        r = (r4 << 1) | (r4 >> 3); g = (g4 << 1) | (g4 >> 3); b = (b4 << 1) | (b4 >> 3);
        al = (a3 << 5) | (a3 << 2) | (a3 >> 1);
    }
    B[0] = (float)e5(r); B[1] = (float)e5(g); B[2] = (float)e5(b); B[3] = (float)al;
}

static int imod(int a, int b) { int r = a % b; return r < 0 ? r + b : r; }
static int idiv(int a, int b) { int q = a / b; if ((a % b) && ((a < 0) != (b < 0))) q--; return q; }

/* data : blocs de 8 octets en ordre de Morton ; w,h : taille d'un niveau ; out : w*h*4 octets RGBA */
void pvrtc_decode_level(const uint8_t *data, int w, int h, int bpp, uint8_t *out)
{
    int bwpx = bpp == 4 ? 4 : 8;
    int bw = (w + bwpx - 1) / bwpx, bh = (h + 3) / 4;
    if (bw < 2) bw = 2;
    if (bh < 2) bh = 2;
    int W = bw * bwpx, H = bh * 4;
    int nb = bw < bh ? bw : bh, nbits = 0;
    while ((1 << nbits) < nb) nbits++;
    float (*A)[4] = malloc(sizeof(float[4]) * bw * bh), (*B)[4] = malloc(sizeof(float[4]) * bw * bh);
    uint32_t *mod = malloc(4 * bw * bh);
    int *mode = malloc(sizeof(int) * bw * bh);
    for (int by = 0; by < bh; by++)
        for (int bx = 0; bx < bw; bx++) {
            uint32_t idx = morton(bx, by, nbits);
            const uint32_t *wd = (const uint32_t *)(data + (size_t)idx * 8);
            colors(wd[1], A[by * bw + bx], B[by * bw + bx], &mode[by * bw + bx]);
            mod[by * bw + bx] = wd[0];
        }
    static const int wt4[4] = { 0, 3, 5, 8 }, wt4p[4] = { 0, 4, 4, 8 };
    float *g = NULL;
    if (bpp == 2) g = calloc((size_t)W * H, sizeof(float));
    /* 2 bpp mode 1 : valeurs explicites sur les pixels (x+y) pairs */
    if (bpp == 2) {
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                int blk = (y / 4) * bw + (x / 8), lx = x % 8, ly = y % 4;
                if (mode[blk] == 1 && (((lx + ly) & 1) == 0)) {
                    int rank = (ly * 8 + lx) / 2;
                    g[(size_t)y * W + x] = (float)wt4[(mod[blk] >> (rank * 2)) & 3];
                }
            }
    }
    for (int y = 0; y < H; y++) {
        int ys = y - 2, by0 = imod(idiv(ys, 4), bh), by1 = (by0 + 1) % bh;
        float fy = (float)imod(ys, 4) / 4.0f;
        for (int x = 0; x < W; x++) {
            int xs = x - bwpx / 2, bx0 = imod(idiv(xs, bwpx), bw), bx1 = (bx0 + 1) % bw;
            float fx = (float)imod(xs, bwpx) / (float)bwpx;
            float a[4], b[4];
            for (int k = 0; k < 4; k++) {
                float t0 = A[by0 * bw + bx0][k] * (1 - fx) + A[by0 * bw + bx1][k] * fx;
                float t1 = A[by1 * bw + bx0][k] * (1 - fx) + A[by1 * bw + bx1][k] * fx;
                a[k] = t0 * (1 - fy) + t1 * fy;
                t0 = B[by0 * bw + bx0][k] * (1 - fx) + B[by0 * bw + bx1][k] * fx;
                t1 = B[by1 * bw + bx0][k] * (1 - fx) + B[by1 * bw + bx1][k] * fx;
                b[k] = t0 * (1 - fy) + t1 * fy;
            }
            int blk = (y / 4) * bw + (x / bwpx), lx = x % bwpx, ly = y % 4;
            float wgt; int punch = 0;
            if (bpp == 4) {
                int mv = (mod[blk] >> ((ly * 4 + lx) * 2)) & 3;
                wgt = (mode[blk] == 0 ? wt4[mv] : wt4p[mv]) / 8.0f;
                punch = mode[blk] == 1 && mv == 2;
            } else if (mode[blk] == 0) {
                wgt = (float)(((mod[blk] >> (ly * 8 + lx)) & 1) * 8) / 8.0f;
            } else if (((lx + ly) & 1) == 0) {
                wgt = g[(size_t)y * W + x] / 8.0f;
            } else {
                float n = (g[(size_t)imod(y - 1, H) * W + x] + g[(size_t)((y + 1) % H) * W + x]
                         + g[(size_t)y * W + imod(x - 1, W)] + g[(size_t)y * W + (x + 1) % W]) / 4.0f;
                wgt = n / 8.0f;
            }
            if (x < w && y < h) {
                uint8_t *o = out + ((size_t)y * w + x) * 4;
                for (int k = 0; k < 4; k++) {
                    float v = a[k] * (1 - wgt) + b[k] * wgt;
                    if (k == 3 && punch) v = 0;
                    o[k] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : (int)(v + 0.5f));
                }
            }
        }
    }
    free(A); free(B); free(mod); free(mode); free(g);
}
