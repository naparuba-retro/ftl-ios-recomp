/* Operations vectorielles NEON/VFP : semantique des CALLOTHER de Ghidra (ARMneon.sinc).
 * Les operandes vectoriels (registres D=8 octets, Q=16 octets) sont des tampons d'octets ;
 * la taille de lane vient des constantes du p-code ou, a defaut, du suffixe du mnemonique
 * (ex. "vshr.u64" -> 8 octets, non signe). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include "rt.h"

/* float -> demi-precision IEEE (arrondi au plus proche pair) */
static uint16_t f2h(float f)
{
    uint32_t x; memcpy(&x, &f, 4);
    uint32_t sign = (x >> 16) & 0x8000, e = (x >> 23) & 0xff, m = x & 0x7fffff;
    if (e == 0xff) return (uint16_t)(sign | 0x7c00 | (m ? 0x200 : 0));
    int ne = (int)e - 127 + 15;
    if (ne >= 31) return (uint16_t)(sign | 0x7c00);
    if (ne <= 0) {
        if (ne < -10) return (uint16_t)sign;
        m |= 0x800000;
        int sh = 14 - ne;
        uint32_t h = m >> sh, rem = m & ((1u << sh) - 1), half = 1u << (sh - 1);
        if (rem > half || (rem == half && (h & 1))) h++;
        return (uint16_t)(sign | h);
    }
    uint32_t h = ((uint32_t)ne << 10) | (m >> 13), rem = m & 0x1fff;
    if (rem > 0x1000 || (rem == 0x1000 && (h & 1))) h++;
    return (uint16_t)(sign | h);
}

static void bad(const char *what, int op)
{
    fprintf(stderr, "RT: NEON non supporte: %s (op %d)\n", what, op);
    fflush(stderr);
    exit(11);
}

/* valeur d'un operande scalaire : constante ou petit temporaire (ex. le "fbits" des vcvt/vshr) */
static uint64_t cval(const NIn *x)
{
    if (x->k) return x->c;
    uint64_t v = 0;
    if (x->p) memcpy(&v, x->p, x->sz > 8 ? 8 : x->sz);
    return v;
}

static uint64_t getl(const uint8_t *p, int e, int i)
{
    uint64_t v = 0;
    memcpy(&v, p + (size_t)i * e, e);
    return v;
}
static void setl(uint8_t *p, int e, int i, uint64_t v) { memcpy(p + (size_t)i * e, &v, e); }
static int64_t sxl(uint64_t v, int e)
{
    int b = e * 8;
    if (b >= 64) return (int64_t)v;
    uint64_t m = 1ULL << (b - 1);
    v &= (1ULL << b) - 1;
    return (int64_t)((v ^ m) - m);
}
static float getf(const uint8_t *p, int i) { float f; memcpy(&f, p + 4 * i, 4); return f; }
static void setf(uint8_t *p, int i, float f) { memcpy(p + 4 * i, &f, 4); }

/* operande vectoriel : constante (zero) -> tampon de zeros */
static const uint8_t *vecof(const NIn *x, uint8_t *zero)
{
    if (x->k) { memset(zero, 0, 16); return zero; }
    return x->p;
}

void rt_neon(int op, uint8_t *out, int osz, const NIn *in, int n, int mb, int mu, int mf)
{
    uint8_t tmp[16], z0[16], z1[16];
    memset(tmp, 0, sizeof tmp);
    const uint8_t *a = n > 0 ? vecof(&in[0], z0) : NULL;
    const uint8_t *b = n > 1 ? vecof(&in[1], z1) : NULL;
    int lanes;

    switch (op) {
    /* ---------- flottants simple precision ---------- */
    case NEON_FloatVectorAdd:
    case NEON_FloatVectorSub:
    case NEON_FloatVectorMult:
    case NEON_FloatVectorMax:
    case NEON_FloatVectorMin: {
        uint64_t fes = in[2].c;
        if (fes != 2) bad("flottant demi-precision", op);
        lanes = osz / 4;
        for (int i = 0; i < lanes; i++) {
            float x = getf(a, i), y = getf(b, i), r;
            switch (op) {
            case NEON_FloatVectorAdd: r = x + y; break;
            case NEON_FloatVectorSub: r = x - y; break;
            case NEON_FloatVectorMult: r = x * y; break;
            case NEON_FloatVectorMax: r = (x > y || isnan(y)) ? x : y; if (isnan(x)) r = x; break;
            default: r = (x < y || isnan(y)) ? x : y; if (isnan(x)) r = x; break;
            }
            setf(tmp, i, r);
        }
        break;
    }
    case NEON_FloatVectorAbsolute:
        lanes = osz / 4;
        for (int i = 0; i < lanes; i++) setf(tmp, i, fabsf(getf(a, i)));
        break;
    case NEON_FloatVectorNeg: {
        int e = in[1].c == 1 ? (int)in[2].c : (in[2].c == 2 ? 4 : 2);
        if (e != 4) bad("vneg non f32", op);
        lanes = osz / 4;
        for (int i = 0; i < lanes; i++) setf(tmp, i, -getf(a, i));
        break;
    }
    case NEON_FloatVectorCompareGreaterThan:
    case NEON_FloatVectorCompareGreaterThanOrEqual:
        lanes = osz / 4;
        for (int i = 0; i < lanes; i++) {
            float x = getf(a, i), y = getf(b, i);
            int t = op == NEON_FloatVectorCompareGreaterThan ? x > y : x >= y;
            setl(tmp, 4, i, t ? 0xFFFFFFFFu : 0);
        }
        break;

    /* ---------- entiers ---------- */
    case NEON_VectorAdd:
    case NEON_VectorSub:
    case NEON_VectorMultiply:
    case NEON_VectorMultiplyAccumulate: {
        int e = (int)in[2].c;                    /* octets par element source */
        int isu = (n > 3 ? (int)in[3].c : mu) != 0;
        int asz = in[0].sz, bsz = in[1].sz;
        int scalar_b = in[1].p && bsz <= 4 && osz >= 8 && !(osz == 8 && bsz == 8);
        if (mf) {                                /* vmul.f32 / vmla.f32 / vadd.f32 par scalaire ou vecteur */
            lanes = osz / 4;
            float s = scalar_b ? getf(b, 0) : 0;
            for (int i = 0; i < lanes; i++) {
                float x = getf(a, i), y = scalar_b ? s : getf(b, i), acc = getf(out, i), r;
                if (op == NEON_VectorAdd) r = x + y;
                else if (op == NEON_VectorSub) r = x - y;
                else if (op == NEON_VectorMultiply) r = x * y;
                else r = acc + x * y;
                setf(tmp, i, r);
            }
            break;
        }
        int wide_out = (osz == 16 && asz == 8 && bsz == 8) || (osz == 16 && asz == 16 && bsz == 8 && op != NEON_VectorMultiply);
        if (op == NEON_VectorMultiply || op == NEON_VectorMultiplyAccumulate) wide_out = (osz == 16 && asz == 8);
        if (wide_out) {
            int oe = e * 2;
            lanes = 8 / e;
            for (int i = 0; i < lanes; i++) {
                int64_t x = (asz == 16 && op != NEON_VectorMultiply) ? (isu ? (int64_t)getl(a, oe, i) : sxl(getl(a, oe, i), oe))
                                                                        : (isu ? (int64_t)getl(a, e, i) : sxl(getl(a, e, i), e));
                int64_t y = scalar_b ? (isu ? (int64_t)getl(b, bsz, 0) : sxl(getl(b, bsz, 0), bsz))
                                     : (isu ? (int64_t)getl(b, e, i) : sxl(getl(b, e, i), e));
                int64_t r = op == NEON_VectorAdd ? x + y : op == NEON_VectorSub ? x - y
                          : op == NEON_VectorMultiply ? x * y : (int64_t)getl(out, oe, i) + x * y;
                setl(tmp, oe, i, (uint64_t)r);
            }
            break;
        }
        lanes = osz / e;
        for (int i = 0; i < lanes; i++) {
            uint64_t x = getl(a, e, i);
            uint64_t y = scalar_b ? getl(b, bsz, 0) : getl(b, e, i);
            uint64_t r;
            if (op == NEON_VectorAdd) r = x + y;
            else if (op == NEON_VectorSub) r = x - y;
            else if (op == NEON_VectorMultiply) r = x * y;
            else r = getl(out, e, i) + x * y;
            setl(tmp, e, i, r);
        }
        break;
    }
    case NEON_VectorAbsolute: {
        int e = (int)in[1].c;
        lanes = osz / e;
        for (int i = 0; i < lanes; i++) {
            int64_t x = sxl(getl(a, e, i), e);
            setl(tmp, e, i, (uint64_t)(x < 0 ? -x : x));
        }
        break;
    }
    case NEON_VectorCompareEqual:
    case NEON_VectorCompareGreaterThan:
    case NEON_VectorCompareGreaterThanOrEqual: {
        int e = (int)in[2].c;
        int isu = (n > 3 ? (int)in[3].c : mu) != 0;
        if (n == 3 || in[2].k == 0) e = (int)in[2].c;
        lanes = osz / e;
        for (int i = 0; i < lanes; i++) {
            uint64_t x = getl(a, e, i), y = getl(b, e, i);
            int t;
            if (op == NEON_VectorCompareEqual) t = x == y;
            else if (isu) t = op == NEON_VectorCompareGreaterThan ? x > y : x >= y;
            else t = op == NEON_VectorCompareGreaterThan ? sxl(x, e) > sxl(y, e) : sxl(x, e) >= sxl(y, e);
            setl(tmp, e, i, t ? ~0ULL : 0);
        }
        break;
    }
    case NEON_VectorBitwiseSelect:
        for (int i = 0; i < osz; i++) tmp[i] = (uint8_t)((a[i] & b[i]) | (~a[i] & in[2].p[i]));
        break;
    case NEON_VectorShiftLeft: {
        if (in[1].k) {                           /* decalage immediat : (src, imm, bits, udt) */
            int e = (int)(in[2].c / 8);
            int sh = (int)in[1].c;
            lanes = osz / e;
            for (int i = 0; i < lanes; i++) setl(tmp, e, i, sh >= e * 8 ? 0 : getl(a, e, i) << sh);
        } else {                                 /* par vecteur : (src, vshift, esize, udt) */
            int e = (int)in[2].c;
            int isu = (int)in[3].c;
            lanes = osz / e;
            for (int i = 0; i < lanes; i++) {
                int s = (int8_t)(getl(in[1].p, e, i) & 0xff);
                uint64_t x = getl(a, e, i), r;
                if (s >= 0) r = s >= e * 8 ? 0 : x << s;
                else if (isu) r = -s >= e * 8 ? 0 : x >> -s;
                else r = (uint64_t)(sxl(x, e) >> (-s >= e * 8 ? e * 8 - 1 : -s));
                setl(tmp, e, i, r);
            }
        }
        break;
    }
    case NEON_VectorShiftRight:
    case NEON_VectorShiftRightAccumulate: {
        int e = mb ? mb : 4;
        int k = op == NEON_VectorShiftRightAccumulate ? 1 : 0;
        const uint8_t *src = op == NEON_VectorShiftRightAccumulate ? b : a;
        int sh = (int)in[1 + k].c;
        lanes = osz / e;
        for (int i = 0; i < lanes; i++) {
            uint64_t x = getl(src, e, i), r;
            if (mu) r = sh >= e * 8 ? 0 : x >> sh;
            else r = (uint64_t)(sxl(x, e) >> (sh >= e * 8 ? e * 8 - 1 : sh));
            if (k) r += getl(out, e, i);
            setl(tmp, e, i, r);
        }
        break;
    }
    case NEON_VectorCopyLong: {
        int e = (int)in[1].c, isu = (int)in[2].c;
        lanes = 8 / e;
        for (int i = 0; i < lanes; i++) {
            uint64_t x = getl(a, e, i);
            setl(tmp, e * 2, i, isu ? x : (uint64_t)sxl(x, e));
        }
        break;
    }
    case NEON_VectorCopyNarrow: {
        int e = (int)in[1].c;                    /* octets par element source */
        lanes = 16 / e;
        for (int i = 0; i < lanes; i++) setl(tmp, e / 2, i, getl(a, e, i));
        break;
    }
    case NEON_VectorGetElement: {
        int idx = (int)in[1].c, e = (int)in[2].c, sg = (int)in[3].c;
        uint64_t x = getl(a, e, idx);
        setl(tmp, 4, 0, sg ? (uint64_t)sxl(x, e) : x);
        break;
    }

    /* ---------- conversions ---------- */
    case NEON_VectorSignedToFloat:
    case NEON_VectorUnsignedToFloat: {
        uint32_t raw; memcpy(&raw, a, 4);
        double v = op == NEON_VectorSignedToFloat ? (double)(int32_t)raw : (double)raw;
        if (osz == 8) memcpy(tmp, &v, 8); else setf(tmp, 0, (float)v);
        break;
    }
    case NEON_FixedToFP: {
        int fb = (int)in[1].c, isu = (int)in[2].c, w = (int)in[4].c;
        lanes = osz / (w / 8);
        for (int i = 0; i < lanes; i++) {
            uint64_t x = getl(a, w / 8, i);
            double v = isu ? (double)x : (double)sxl(x, w / 8);
            if (w == 32) setf(tmp, i, (float)(v / ldexp(1.0, fb)));
            else bad("FixedToFP largeur != 32", op);
        }
        break;
    }
    case NEON_FPToFixed: {
        int fb = (int)in[1].c, isu = (int)in[2].c, w = (int)in[4].c;
        if (w != 32) bad("FPToFixed largeur != 32", op);
        lanes = osz / 4;
        for (int i = 0; i < lanes; i++) {
            double v = (double)getf(a, i) * ldexp(1.0, fb);
            v = trunc(v);
            int64_t r;
            if (isnan(v)) r = 0;
            else if (isu) r = v < 0 ? 0 : v > 4294967295.0 ? 4294967295LL : (int64_t)v;
            else r = v < -2147483648.0 ? -2147483648LL : v > 2147483647.0 ? 2147483647LL : (int64_t)v;
            setl(tmp, 4, i, (uint64_t)r);
        }
        break;
    }
    case NEON_SignedSaturate: {
        int bits = (int)in[1].c;
        int64_t v = (int32_t)getl(a, 4, 0);
        int64_t hi = (1LL << (bits - 1)) - 1, lo = -(1LL << (bits - 1));
        setl(tmp, 4, 0, (uint64_t)(v > hi ? hi : v < lo ? lo : v));
        break;
    }
    case NEON_SignedDoesSaturate: {
        int bits = (int)in[1].c;
        int64_t v = (int32_t)getl(a, 4, 0);
        int64_t hi = (1LL << (bits - 1)) - 1, lo = -(1LL << (bits - 1));
        tmp[0] = (v > hi || v < lo);
        break;
    }
    case NEON_VectorSignedFixedToFloat:
    case NEON_VectorUnsignedFixedToFloat: {
        int fb = (int)cval(&in[1]);
        int isu = op == NEON_VectorUnsignedFixedToFloat;
        if (mb == 4) {                               /* vcvt.f32.s32/u32 : virgule fixe 32 bits -> f32 */
            lanes = osz / 4;
            for (int i = 0; i < lanes; i++) {
                uint64_t x = getl(a, 4, i);
                double v = isu ? (double)(uint32_t)x : (double)(int32_t)x;
                setf(tmp, i, (float)(v / ldexp(1.0, fb)));
            }
        } else if (mb == 2) {
            /* Ghidra decode a tort "vcvt.f16.s16/u16 Qd,Qm,#n" pour l'encodage NEON "vshr.s32/u32 Qd,Qm,#n"
             * (imm6 >= 32, L=0) : on execute le decalage a droite sur des mots de 32 bits. */
            lanes = osz / 4;
            for (int i = 0; i < lanes; i++) {
                uint32_t x = (uint32_t)getl(a, 4, i);
                uint32_t r = isu ? (fb >= 32 ? 0 : x >> fb) : (uint32_t)((int32_t)x >> (fb >= 32 ? 31 : fb));
                setl(tmp, 4, i, r);
            }
        } else bad("FixedToFloat largeur inconnue", op);
        break;
    }
    case NEON_vrev: {
        int blk = mb, el = mu;
        if (blk <= 0 || el <= 0 || blk < el) bad("vrev parametres", op);
        for (int b0 = 0; b0 < osz; b0 += blk) {
            int per = blk / el;
            for (int i = 0; i < per; i++) memcpy(tmp + b0 + i * el, a + b0 + (per - 1 - i) * el, el);
        }
        break;
    }
    default:
        bad("operation inconnue", op);
    }
    memcpy(out, tmp, osz);
}
