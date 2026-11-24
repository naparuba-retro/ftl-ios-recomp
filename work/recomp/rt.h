/* Runtime de la recompilation ARM -> C (voir scripts/Lift.java). */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include "regs.h"

#ifdef _MSC_VER
#define RT_TLS __declspec(thread)
#else
#define RT_TLS _Thread_local
#endif

/* Memoire invitee : adresse 32 bits -> MEM + adresse. */
extern uint8_t *MEM;
/* Registres invites (espace "register" de Ghidra), un jeu par thread hote. */
extern RT_TLS uint8_t REGS[REGS_SIZE];

typedef struct { uint32_t addr; void (*fn)(void); } FnEntry;
extern const FnEntry GEN_FNTAB[];
extern const unsigned GEN_FNTAB_N;

uint32_t rt_ldrex(uint32_t a);
uint32_t rt_strex(uint32_t a, uint32_t v);
void rt_call_ind(uint32_t addr);   /* appel indirect : adresse invitee (bit 0 = Thumb) */
void rt_tail_ind(uint32_t addr);   /* saut indirect hors fonction : appelle puis retourne */
void rt_unsupported(const char *what);
#define UNSUP(s) rt_unsupported(s)

#define R32(o) (*(uint32_t *)(REGS + (o)))
#define M32(a) (*(uint32_t *)(MEM + (uint32_t)(a)))

#ifdef NI_SX
#define H_SX static __declspec(noinline)
#else
#define H_SX static inline
#endif
#ifdef NI_SHIFT
#define H_SH static __declspec(noinline)
#else
#define H_SH static inline
#endif
#ifdef NI_FLAGS
#define H_FL static __declspec(noinline)
#else
#define H_FL static inline
#endif
H_SX uint64_t SX(uint64_t v, int bits)
{
    if (bits >= 64) return v;
    uint64_t m = 1ULL << (bits - 1);
    v &= (1ULL << bits) - 1;
    return (v ^ m) - m;
}
H_SH uint64_t SHL64(uint64_t a, uint64_t b) { return b >= 64 ? 0 : a << b; }
H_SH uint64_t SHR64(uint64_t a, uint64_t b) { return b >= 64 ? 0 : a >> b; }
H_SH uint64_t SAR64(uint64_t a, uint64_t b) { return (uint64_t)((int64_t)a >> (b >= 64 ? 63 : b)); }
H_FL uint64_t CARRY(uint64_t a, uint64_t b, int bits)
{
    if (bits >= 64) return (a + b) < a;
    uint64_t m = (1ULL << bits) - 1;
    return (((a & m) + (b & m)) >> bits) & 1;
}
H_FL uint64_t SCARRY(uint64_t a, uint64_t b, int bits)
{
    uint64_t s = a + b;
    return (((~(a ^ b)) & (a ^ s)) >> (bits - 1)) & 1;
}
H_FL uint64_t SBORROW(uint64_t a, uint64_t b, int bits)
{
    uint64_t d = a - b;
    return (((a ^ b) & (a ^ d)) >> (bits - 1)) & 1;
}

#ifdef NI_FP
#define H_FP static __declspec(noinline)
#else
#define H_FP static inline
#endif
H_FP float F32(uint32_t v) { float f; memcpy(&f, &v, 4); return f; }
H_FP double F64(uint64_t v) { double f; memcpy(&f, &v, 8); return f; }
H_FP uint32_t U32F(float f) { uint32_t v; memcpy(&v, &f, 4); return v; }
H_FP uint64_t U64F(double f) { uint64_t v; memcpy(&v, &f, 8); return v; }

/* Acces courts utilises par le code genere. */
#define G1(o) (*(uint8_t *)(REGS + (o)))
#define G2(o) (*(uint16_t *)(REGS + (o)))
#define G4(o) (*(uint32_t *)(REGS + (o)))
#define G8(o) (*(uint64_t *)(REGS + (o)))
#define MG1(o) (*(uint8_t *)(MEM + (o)))
#define MG2(o) (*(uint16_t *)(MEM + (o)))
#define MG4(o) (*(uint32_t *)(MEM + (o)))
#define MG8(o) (*(uint64_t *)(MEM + (o)))

/* ---- aides HLE (ABI ARM iOS "softfp" : arguments dans r0-r3 puis pile, flottants dans r0-r3) ---- */
#define A32(n) ((n) < 4 ? R32(REG_r0 + 4 * (n)) : M32(R32(REG_sp) + 4 * ((n) - 4)))
#define RET32(v) (R32(REG_r0) = (uint32_t)(v))
#define RET64(v) do { uint64_t _v = (uint64_t)(v); R32(REG_r0) = (uint32_t)_v; R32(REG_r1) = (uint32_t)(_v >> 32); } while (0)
#define GP(a) ((void *)(MEM + (uint32_t)(a)))
#define GSTR(a) ((char *)(MEM + (uint32_t)(a)))
static inline uint64_t A64(int n) { return ((uint64_t)A32(n + 1) << 32) | A32(n); }
static inline float AF(int n) { uint32_t v = A32(n); float f; memcpy(&f, &v, 4); return f; }
static inline double AD(int n) { uint64_t v = A64(n); double d; memcpy(&d, &v, 8); return d; }
#define RETF(f) do { float _rf = (f); uint32_t _rfbits; memcpy(&_rfbits, &_rf, 4); R32(REG_r0) = _rfbits; } while (0)
#define RETD(d) do { double _rd = (d); uint64_t _rbits; memcpy(&_rbits, &_rd, 8); RET64(_rbits); } while (0)
#define EXT(name) void ext_##name(void)

uint32_t rt_alloc(uint32_t size);
void rt_free(uint32_t p);
uint32_t rt_realloc(uint32_t p, uint32_t size);
void rt_commit(uint32_t start, uint32_t size);
void rt_init_memory(const char *gendir);
uint32_t rt_new_stack(uint32_t size);
void rt_call_guest(uint32_t addr, uint32_t r0);  /* appelle une fonction invitee avec 1 argument */

/* ---- operations vectorielles (neon.c) ---- */
typedef struct { const uint8_t *p; int sz; uint64_t c; int k; } NIn;  /* k = 1 : constante c */
enum {
    NEON_FloatVectorAdd, NEON_FloatVectorSub, NEON_FloatVectorMult, NEON_FloatVectorMax, NEON_FloatVectorMin,
    NEON_FloatVectorAbsolute, NEON_FloatVectorNeg, NEON_FloatVectorCompareGreaterThan,
    NEON_FloatVectorCompareGreaterThanOrEqual, NEON_VectorAdd, NEON_VectorSub, NEON_VectorMultiply,
    NEON_VectorMultiplyAccumulate, NEON_VectorAbsolute, NEON_VectorCompareEqual, NEON_VectorCompareGreaterThan,
    NEON_VectorCompareGreaterThanOrEqual, NEON_VectorBitwiseSelect, NEON_VectorShiftLeft, NEON_VectorShiftRight,
    NEON_VectorShiftRightAccumulate, NEON_VectorCopyLong, NEON_VectorCopyNarrow, NEON_VectorGetElement,
    NEON_VectorSignedToFloat, NEON_VectorUnsignedToFloat, NEON_FixedToFP, NEON_FPToFixed, NEON_SignedSaturate,
    NEON_SignedDoesSaturate, NEON_VectorSignedFixedToFloat, NEON_VectorUnsignedFixedToFloat, NEON_vrev
};
void rt_neon(int op, uint8_t *out, int osz, const NIn *in, int n, int mb, int mu, int mf);

void rt_where(const char *label, uint32_t a);

/* ---- operations binaires sur plus de 8 octets (registres Q de NEON) ---- */
static inline void big_and(uint8_t *d, const uint8_t *a, const uint8_t *b, int n) { uint8_t t[32]; for (int i = 0; i < n; i++) t[i] = a[i] & b[i]; memcpy(d, t, n); }
static inline void big_or(uint8_t *d, const uint8_t *a, const uint8_t *b, int n) { uint8_t t[32]; for (int i = 0; i < n; i++) t[i] = a[i] | b[i]; memcpy(d, t, n); }
static inline void big_xor(uint8_t *d, const uint8_t *a, const uint8_t *b, int n) { uint8_t t[32]; for (int i = 0; i < n; i++) t[i] = a[i] ^ b[i]; memcpy(d, t, n); }
static inline void big_not(uint8_t *d, const uint8_t *a, const uint8_t *b, int n) { uint8_t t[32]; (void)b; for (int i = 0; i < n; i++) t[i] = (uint8_t)~a[i]; memcpy(d, t, n); }
static inline void big_shl(uint8_t *d, const uint8_t *a, uint32_t bits, int n)
{
    uint8_t t[32] = {0};
    int by = bits / 8, bi = bits % 8;
    for (int i = n - 1; i >= 0; i--) {
        int s = i - by;
        uint8_t v = 0;
        if (s >= 0) v = (uint8_t)(a[s] << bi);
        if (bi && s - 1 >= 0) v |= (uint8_t)(a[s - 1] >> (8 - bi));
        t[i] = v;
    }
    memcpy(d, t, n);
}
static inline void big_shr(uint8_t *d, const uint8_t *a, uint32_t bits, int n)
{
    uint8_t t[32] = {0};
    int by = bits / 8, bi = bits % 8;
    for (int i = 0; i < n; i++) {
        int s = i + by;
        uint8_t v = 0;
        if (s < n) v = (uint8_t)(a[s] >> bi);
        if (bi && s + 1 < n) v |= (uint8_t)(a[s + 1] << (8 - bi));
        t[i] = v;
    }
    memcpy(d, t, n);
}

void rt_backtrace(const char *title);

/* ---- trace d'appels (FTL_CALLTRACE=<sous-chaine du nom> ou "all") ---- */
typedef struct { uint32_t addr; const char *name; } FnName;
extern const FnName GEN_NAMES[];
extern const unsigned GEN_NAMES_N;
extern volatile int rt_trace_on;
void rt_trace_enter(uint32_t addr);
#define RT_ENTER(a) do { if (rt_trace_on) rt_trace_enter(a); } while (0)

/* ---- sonde d'instruction (FTL_PROBE=1) : affiche les registres avant chaque instruction des fonctions lifted avec PROBE: ---- */
extern volatile int rt_probe_on;
void rt_probe(uint32_t addr);
#define RT_PROBE(a) do { if (rt_probe_on) rt_probe(a); } while (0)
