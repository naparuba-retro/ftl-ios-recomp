/* HLE de std::string (libstdc++ 4.2, ABI "COW" d'iOS armv7) sur la memoire invitee.
 *
 * Disposition invitee : un std::string est un pointeur (4 octets) vers les caracteres ;
 * juste avant : { uint32 longueur; uint32 capacite; int32 compteur_de_references } (12 octets).
 * Representation vide partagee : symbole _S_empty_rep_storage, dont l'adresse est lue dans
 * l'emplacement de pointeur non-lazy 0x0026c498 du binaire (zone zero-initialisee). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rt.h"

#define EMPTY_SLOT 0x0026c498u
#define NPOS 0xFFFFFFFFu

static uint32_t empty_rep(void) { return M32(EMPTY_SLOT); }
static uint32_t S_LEN(uint32_t d) { return M32(d - 12); }
static uint32_t S_CAP(uint32_t d) { return M32(d - 8); }
static int32_t S_REF(uint32_t d) { return (int32_t)M32(d - 4); }
static int is_empty(uint32_t d) { return d - 12 == empty_rep(); }

static void fatal(const char *what)
{
    fprintf(stderr, "std::string: %s\n", what);
    exit(9);
}

static uint32_t rep_create(uint32_t cap, uint32_t oldcap)
{
    if (cap > 0x3ffffff0u) fatal("length_error");
    if (cap > oldcap && cap < 2 * oldcap) cap = 2 * oldcap;
    uint32_t rep = rt_alloc(12 + cap + 1);
    M32(rep) = 0;
    M32(rep + 4) = cap;
    M32(rep + 8) = 0;
    return rep + 12;  /* pointeur de donnees */
}

static void set_len(uint32_t d, uint32_t n)
{
    if (is_empty(d)) return;
    M32(d - 12) = n;
    M32(d - 4) = 0;  /* partageable */
    MG1(d + n) = 0;
}

static void dispose(uint32_t d)
{
    if (is_empty(d)) return;
    int32_t prev = S_REF(d);
    M32(d - 4) = (uint32_t)(prev - 1);
    if (prev <= 0) rt_free(d - 12);
}

static uint32_t clone(uint32_t d, uint32_t extra)
{
    uint32_t n = S_LEN(d);
    uint32_t nd = rep_create(n + extra, S_CAP(d));
    memcpy(MEM + nd, MEM + d, n);
    set_len(nd, n);
    return nd;
}

/* _M_grab : partage la representation (ou la copie si elle est "leaked") */
static uint32_t grab(uint32_t d)
{
    if (is_empty(d)) return d;
    if (S_REF(d) < 0) return clone(d, 0);
    M32(d - 4) = (uint32_t)(S_REF(d) + 1);
    return d;
}

static void mutate(uint32_t sp, uint32_t pos, uint32_t len1, uint32_t len2)
{
    uint32_t d = M32(sp);
    uint32_t old_size = S_LEN(d);
    uint32_t new_size = old_size + len2 - len1;
    uint32_t how_much = old_size - pos - len1;
    if (new_size > S_CAP(d) || S_REF(d) > 0) {
        uint32_t nd = rep_create(new_size, S_CAP(d));
        if (pos) memcpy(MEM + nd, MEM + d, pos);
        if (how_much) memcpy(MEM + nd + pos + len2, MEM + d + pos + len1, how_much);
        dispose(d);
        M32(sp) = nd;
        d = nd;
    } else if (how_much && len1 != len2) {
        memmove(MEM + d + pos + len2, MEM + d + pos + len1, how_much);
    }
    set_len(d, new_size);
}

static void reserve(uint32_t sp, uint32_t res)
{
    uint32_t d = M32(sp);
    if (res != S_CAP(d) || S_REF(d) > 0) {
        uint32_t n = S_LEN(d);
        if (res < n) res = n;
        uint32_t nd = rep_create(res, S_CAP(d));
        if (n) memcpy(MEM + nd, MEM + d, n);
        set_len(nd, n);
        dispose(d);
        M32(sp) = nd;
    }
}

/* copie hote d'une source invitee (la source peut etre dans la chaine elle-meme) */
static uint8_t *tmp_copy(uint32_t src, uint32_t n)
{
    uint8_t *t = malloc(n ? n : 1);
    memcpy(t, MEM + src, n);
    return t;
}

static void init_from(uint32_t sp, const uint8_t *s, uint32_t n)
{
    if (n == 0) { M32(sp) = empty_rep() + 12; return; }
    uint32_t d = rep_create(n, 0);
    memcpy(MEM + d, s, n);
    set_len(d, n);
    M32(sp) = d;
}

/* ---------- representation ---------- */
EXT(__ZNSs4_Rep9_S_createEmmRKSaIcE)
{
    RET32(rep_create(A32(0), A32(1)) - 12);  /* retourne l'adresse du _Rep */
}
EXT(__ZNSs4_Rep10_M_destroyERKSaIcE) { rt_free(A32(0)); }

EXT(__ZNSs12_M_leak_hardEv)
{
    uint32_t sp = A32(0), d = M32(sp);
    if (is_empty(d)) return;
    if (S_REF(d) > 0) { mutate(sp, 0, 0, 0); d = M32(sp); }
    M32(d - 4) = (uint32_t)-1;
}
EXT(__ZNSs9_M_mutateEmmm) { mutate(A32(0), A32(1), A32(2), A32(3)); }
EXT(__ZNSs7reserveEm) { reserve(A32(0), A32(1)); }

/* ---------- constructeurs ---------- */
EXT(__ZNSsC1EPKcRKSaIcE)
{
    uint32_t sp = A32(0), s = A32(1);
    uint32_t n = (uint32_t)strlen(GSTR(s));
    uint8_t *t = tmp_copy(s, n);
    init_from(sp, t, n);
    free(t);
}
EXT(__ZNSsC1EPKcmRKSaIcE)
{
    uint32_t sp = A32(0), s = A32(1), n = A32(2);
    uint8_t *t = tmp_copy(s, n);
    init_from(sp, t, n);
    free(t);
}
EXT(__ZNSsC1ERKSs)
{
    uint32_t sp = A32(0), d = M32(A32(1));
    M32(sp) = grab(d);
}
EXT(__ZNSsC1ERKSsmm)
{
    uint32_t sp = A32(0), d = M32(A32(1)), pos = A32(2), n = A32(3);
    uint32_t sz = S_LEN(d);
    if (pos > sz) fatal("basic_string::basic_string: pos > size()");
    if (n > sz - pos) n = sz - pos;
    uint8_t *t = tmp_copy(d + pos, n);
    init_from(sp, t, n);
    free(t);
}

/* ---------- modification ---------- */
static void assign_bytes(uint32_t sp, const uint8_t *t, uint32_t n)
{
    uint32_t d = M32(sp);
    mutate(sp, 0, S_LEN(d), n);
    memcpy(MEM + M32(sp), t, n);
}

EXT(__ZNSs6assignEPKcm)
{
    uint32_t sp = A32(0), s = A32(1), n = A32(2);
    uint8_t *t = tmp_copy(s, n);
    assign_bytes(sp, t, n);
    free(t);
    RET32(sp);
}
EXT(__ZNSs6assignERKSs)
{
    uint32_t sp = A32(0), od = M32(A32(1)), d = M32(sp);
    if (od != d) {
        uint32_t nd = grab(od);
        dispose(d);
        M32(sp) = nd;
    }
    RET32(sp);
}

static void append_bytes(uint32_t sp, const uint8_t *t, uint32_t n)
{
    if (!n) return;
    uint32_t d = M32(sp), len = S_LEN(d) + n;
    if (len > S_CAP(d) || S_REF(d) > 0) { reserve(sp, len); d = M32(sp); }
    memcpy(MEM + d + S_LEN(d), t, n);
    set_len(d, len);
}

EXT(__ZNSs6appendEPKcm)
{
    uint32_t sp = A32(0), s = A32(1), n = A32(2);
    uint8_t *t = tmp_copy(s, n);
    append_bytes(sp, t, n);
    free(t);
    RET32(sp);
}
EXT(__ZNSs6appendERKSs)
{
    uint32_t sp = A32(0), od = M32(A32(1));
    uint32_t n = S_LEN(od);
    uint8_t *t = tmp_copy(od, n);
    append_bytes(sp, t, n);
    free(t);
    RET32(sp);
}
EXT(__ZNSs6appendEmc)
{
    uint32_t sp = A32(0), n = A32(1);
    uint8_t c = (uint8_t)A32(2);
    if (n) {
        uint8_t *t = malloc(n);
        memset(t, c, n);
        append_bytes(sp, t, n);
        free(t);
    }
    RET32(sp);
}
EXT(__ZNSs6insertEmPKcm)
{
    uint32_t sp = A32(0), pos = A32(1), s = A32(2), n = A32(3);
    if (pos > S_LEN(M32(sp))) fatal("basic_string::insert: pos > size()");
    uint8_t *t = tmp_copy(s, n);
    mutate(sp, pos, 0, n);
    memcpy(MEM + M32(sp) + pos, t, n);
    free(t);
    RET32(sp);
}
EXT(__ZNSs7replaceEmmPKcm)
{
    uint32_t sp = A32(0), pos = A32(1), n1 = A32(2), s = A32(3), n2 = A32(4);
    uint32_t sz = S_LEN(M32(sp));
    if (pos > sz) fatal("basic_string::replace: pos > size()");
    if (n1 > sz - pos) n1 = sz - pos;
    uint8_t *t = tmp_copy(s, n2);
    mutate(sp, pos, n1, n2);
    memcpy(MEM + M32(sp) + pos, t, n2);
    free(t);
    RET32(sp);
}
EXT(__ZNSs15_M_replace_safeEmmPKcm)
{
    uint32_t sp = A32(0), pos = A32(1), n1 = A32(2), s = A32(3), n2 = A32(4);
    uint8_t *t = tmp_copy(s, n2);
    mutate(sp, pos, n1, n2);
    memcpy(MEM + M32(sp) + pos, t, n2);
    free(t);
    RET32(sp);
}

/* ---------- lecture ---------- */
EXT(__ZNKSs4findEPKcmm)
{
    uint32_t d = M32(A32(0)), s = A32(1), pos = A32(2), n = A32(3);
    uint32_t sz = S_LEN(d);
    if (n == 0) { RET32(pos <= sz ? pos : NPOS); return; }
    if (n > sz) { RET32(NPOS); return; }
    for (; pos + n <= sz; pos++) {
        if (memcmp(MEM + d + pos, MEM + s, n) == 0) { RET32(pos); return; }
    }
    RET32(NPOS);
}
EXT(__ZNKSs7compareEPKc)
{
    uint32_t d = M32(A32(0)), s = A32(1);
    uint32_t sz = S_LEN(d), osz = (uint32_t)strlen(GSTR(s));
    uint32_t m = sz < osz ? sz : osz;
    int r = memcmp(MEM + d, MEM + s, m);
    if (r == 0) r = (int)(sz - osz);
    RET32((int32_t)r);
}

/* ---------- interface pour les autres HLE (flux) ---------- */
uint32_t gs_data(uint32_t sp) { return M32(sp); }
uint32_t gs_len(uint32_t sp) { return S_LEN(M32(sp)); }
uint32_t gs_cap(uint32_t sp) { return S_CAP(M32(sp)); }
void gs_assign(uint32_t sp, const uint8_t *t, uint32_t n) { assign_bytes(sp, t, n); }
void gs_append(uint32_t sp, const uint8_t *t, uint32_t n) { append_bytes(sp, t, n); }
void gs_init(uint32_t sp, const uint8_t *t, uint32_t n) { init_from(sp, t, n); }
void gs_reserve(uint32_t sp, uint32_t n) { reserve(sp, n); }
