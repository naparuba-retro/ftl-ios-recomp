/* HLE de la libc / libgcc / maths / mach / runtime C++ de base pour le code ARM recompile.
 * Convention : arguments dans r0-r3 puis pile, retour dans r0 (r0:r1 pour 64 bits),
 * flottants simple precision dans r0.., doubles dans paires de registres. */
#include "plat.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include "rt.h"

/* ---------------- memoire ---------------- */
EXT(_malloc) { RET32(rt_alloc(A32(0))); }
EXT(_free) { rt_free(A32(0)); }
EXT(_realloc) { RET32(rt_realloc(A32(0), A32(1))); }
EXT(__Znwm) { RET32(rt_alloc(A32(0))); }
EXT(__Znam) { RET32(rt_alloc(A32(0))); }
EXT(__ZdlPv) { rt_free(A32(0)); }
EXT(__ZdaPv) { rt_free(A32(0)); }

EXT(_memcpy) { memcpy(GP(A32(0)), GP(A32(1)), A32(2)); }
EXT(_memmove) { memmove(GP(A32(0)), GP(A32(1)), A32(2)); RET32(A32(0)); }
EXT(_memset) { memset(GP(A32(0)), (int)A32(1), A32(2)); }
EXT(_memcmp) { RET32((int32_t)memcmp(GP(A32(0)), GP(A32(1)), A32(2))); }
EXT(_memchr)
{
    uint8_t *r = memchr(GP(A32(0)), (int)A32(1), A32(2));
    RET32(r ? (uint32_t)(r - MEM) : 0);
}
EXT(_memset_pattern16)
{
    uint8_t *d = GP(A32(0));
    uint8_t *pat = GP(A32(1));
    uint32_t n = A32(2);
    for (uint32_t i = 0; i < n; i++) d[i] = pat[i & 15];
}
EXT(___memcpy_chk) { memcpy(GP(A32(0)), GP(A32(1)), A32(2)); RET32(A32(0)); }
EXT(___memmove_chk) { memmove(GP(A32(0)), GP(A32(1)), A32(2)); RET32(A32(0)); }
EXT(___memset_chk) { memset(GP(A32(0)), (int)A32(1), A32(2)); RET32(A32(0)); }
EXT(___strcat_chk) { strcat(GSTR(A32(0)), GSTR(A32(1))); RET32(A32(0)); }

/* ---------------- chaines ---------------- */
EXT(_strlen) { RET32(strlen(GSTR(A32(0)))); }
EXT(_strcmp) { RET32((int32_t)strcmp(GSTR(A32(0)), GSTR(A32(1)))); }
EXT(_strncmp) { RET32((int32_t)strncmp(GSTR(A32(0)), GSTR(A32(1)), A32(2))); }
EXT(_strcpy) { strcpy(GSTR(A32(0)), GSTR(A32(1))); RET32(A32(0)); }
EXT(_strncpy) { strncpy(GSTR(A32(0)), GSTR(A32(1)), A32(2)); RET32(A32(0)); }
EXT(_strchr)
{
    char *r = strchr(GSTR(A32(0)), (int)A32(1));
    RET32(r ? (uint32_t)((uint8_t *)r - MEM) : 0);
}
EXT(_strrchr)
{
    char *r = strrchr(GSTR(A32(0)), (int)A32(1));
    RET32(r ? (uint32_t)((uint8_t *)r - MEM) : 0);
}
EXT(_strstr)
{
    char *r = strstr(GSTR(A32(0)), GSTR(A32(1)));
    RET32(r ? (uint32_t)((uint8_t *)r - MEM) : 0);
}
EXT(_strspn) { RET32(strspn(GSTR(A32(0)), GSTR(A32(1)))); }
EXT(_strcspn) { RET32(strcspn(GSTR(A32(0)), GSTR(A32(1)))); }
EXT(_strcasecmp) { RET32((int32_t)_stricmp(GSTR(A32(0)), GSTR(A32(1)))); }
EXT(_strncasecmp) { RET32((int32_t)_strnicmp(GSTR(A32(0)), GSTR(A32(1)), A32(2))); }
EXT(_strdup)
{
    size_t n = strlen(GSTR(A32(0))) + 1;
    uint32_t p = rt_alloc((uint32_t)n);
    memcpy(MEM + p, GSTR(A32(0)), n);
    RET32(p);
}
EXT(_atoi) { RET32((int32_t)atoi(GSTR(A32(0)))); }
static void set_end(uint32_t endp_addr, const char *start, char *end, uint32_t base_addr)
{
    if (endp_addr) M32(endp_addr) = base_addr + (uint32_t)(end - start);
}
EXT(_strtol)
{
    char *e;
    const char *s = GSTR(A32(0));
    long v = strtol(s, &e, (int)A32(2));
    set_end(A32(1), s, e, A32(0));
    RET32((int32_t)v);
}
EXT(_strtoul)
{
    char *e;
    const char *s = GSTR(A32(0));
    unsigned long v = strtoul(s, &e, (int)A32(2));
    set_end(A32(1), s, e, A32(0));
    RET32((uint32_t)v);
}
EXT(_strtoll)
{
    char *e;
    const char *s = GSTR(A32(0));
    long long v = strtoll(s, &e, (int)A32(2));
    set_end(A32(1), s, e, A32(0));
    RET64((uint64_t)v);
}
EXT(___toupper) { RET32((uint32_t)toupper((int)A32(0))); }

/* ---------------- maths (flottants simple precision en r0) ---------------- */
EXT(_acosf) { RETF(acosf(AF(0))); }
EXT(_atanf) { RETF(atanf(AF(0))); }
EXT(_cosf) { RETF(cosf(AF(0))); }
EXT(_sinf) { RETF(sinf(AF(0))); }
EXT(_tanf) { RETF(tanf(AF(0))); }
EXT(_ceilf) { RETF(ceilf(AF(0))); }
EXT(_floorf) { RETF(floorf(AF(0))); }
EXT(_roundf) { RETF(roundf(AF(0))); }
EXT(_fmodf) { RETF(fmodf(AF(0), AF(1))); }
EXT(_powf) { RETF(powf(AF(0), AF(1))); }
EXT(___powisf2) { RETF(powf(AF(0), (float)(int32_t)A32(1))); }
EXT(_ceil) { RETD(ceil(AD(0))); }
EXT(_floor) { RETD(floor(AD(0))); }
EXT(_round) { RETD(round(AD(0))); }
EXT(_trunc) { RETD(trunc(AD(0))); }
EXT(_fmod) { RETD(fmod(AD(0), AD(2))); }
EXT(_pow) { RETD(pow(AD(0), AD(2))); }
EXT(___isinfd) { RET32(isinf(AD(0)) ? 1 : 0); }
EXT(___isnand) { RET32(isnan(AD(0)) ? 1 : 0); }
EXT(___fixunsdfdi) { double d = AD(0); RET64(d <= 0 ? 0 : (uint64_t)d); }
EXT(___floatdidf) { RETD((double)(int64_t)A64(0)); }
EXT(___floatundidf) { static int n; if (getenv("FTL_TIMEDBG") && n++ < 12) fprintf(stderr, "[floatundidf %llu]\n", (unsigned long long)A64(0)); RETD((double)A64(0)); }

/* ---------------- divisions entieres (libgcc) ---------------- */
EXT(___divsi3) { int32_t b = (int32_t)A32(1); RET32(b ? (int32_t)A32(0) / b : 0); }
EXT(___modsi3) { int32_t b = (int32_t)A32(1); RET32(b ? (int32_t)A32(0) % b : 0); }
EXT(___udivsi3) { uint32_t b = A32(1); RET32(b ? A32(0) / b : 0); }
EXT(___umodsi3) { uint32_t b = A32(1); RET32(b ? A32(0) % b : 0); }
EXT(___divmodsi4)
{
    int32_t a = (int32_t)A32(0), b = (int32_t)A32(1);
    if (A32(2)) M32(A32(2)) = b ? (uint32_t)(a % b) : 0;
    RET32(b ? a / b : 0);
}
EXT(___divdi3) { int64_t b = (int64_t)A64(2); RET64(b ? (int64_t)A64(0) / b : 0); }
EXT(___udivdi3) { uint64_t b = A64(2); RET64(b ? A64(0) / b : 0); }
EXT(___umoddi3) { uint64_t b = A64(2); RET64(b ? A64(0) % b : 0); }

/* ---------------- temps / mach / systeme ---------------- */
EXT(_time)
{
    uint32_t t = (uint32_t)time(NULL);
    { extern uint32_t rp_time(void); uint32_t r = rp_time(); if (r) t = r; }   /* enregistrement/relecture : heure reproductible */
    if (A32(0)) M32(A32(0)) = t;
    RET32(t);
}
static uint64_t qpc_ns(void)
{
    static LARGE_INTEGER freq;
    LARGE_INTEGER now;
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return (uint64_t)((double)now.QuadPart * 1e9 / (double)freq.QuadPart);
}
EXT(_mach_absolute_time) { RET64(qpc_ns()); }
EXT(_mach_timebase_info) { M32(A32(0)) = 1; M32(A32(0) + 4) = 1; RET32(0); }
EXT(_mach_wait_until)
{
    uint64_t target = A64(0), now = qpc_ns();
    if (target > now) Sleep((DWORD)((target - now) / 1000000));
    RET32(0);
}
EXT(_nanosleep)
{
    uint32_t sec = M32(A32(0)), nsec = M32(A32(0) + 4);
    Sleep(sec * 1000 + nsec / 1000000);
    RET32(0);
}
EXT(_sysconf)
{
    switch (A32(0)) {
        case 58: RET32(4); break;      /* _SC_NPROCESSORS_ONLN */
        case 29: RET32(4096); break;   /* _SC_PAGESIZE */
        default: RET32((uint32_t)-1); break;
    }
}
EXT(_sysctlbyname) { RET32((uint32_t)-1); }
EXT(_task_info) { RET32(5); }
EXT(_host_statistics) { RET32(5); }
EXT(_host_page_size) { if (A32(1)) M32(A32(1)) = 4096; RET32(0); }
EXT(_mach_host_self) { RET32(1); }

/* ---------------- runtime C++ ---------------- */
static uint32_t errno_addr;
EXT(___error)
{
    if (!errno_addr) errno_addr = rt_alloc(16);
    RET32(errno_addr);
}
EXT(___cxa_atexit) { RET32(0); }
EXT(___cxa_guard_acquire)
{
    uint32_t g = A32(0);
    if (MG1(g) == 0) RET32(1); else RET32(0);
}
EXT(___cxa_guard_release) { MG1(A32(0)) = 1; }
EXT(___assert_rtn)
{
    fprintf(stderr, "ASSERT invite: %s (%s:%u) %s\n", GSTR(A32(0)), GSTR(A32(1)), A32(2), GSTR(A32(3)));
    fflush(stderr);
    exit(6);
}
EXT(_abort) { fprintf(stderr, "abort() invite\n"); fflush(stderr); exit(7); }
EXT(_exit) { fflush(stdout); exit((int)A32(0)); }
EXT(__ZSt17__throw_bad_allocv) { fprintf(stderr, "throw std::bad_alloc\n"); exit(8); }
EXT(__ZSt20__throw_length_errorPKc) { fprintf(stderr, "throw length_error: %s\n", GSTR(A32(0))); exit(8); }
EXT(__ZSt20__throw_out_of_rangePKc) { fprintf(stderr, "throw out_of_range: %s\n", GSTR(A32(0))); exit(8); }

/* ---------------- tri / aleatoire ---------------- */
/* rand() iOS : 31 bits (RAND_MAX = 0x7fffffff) ; celui de MSVC s'arrete a 32767 */
static uint64_t rng_state = 1;
EXT(_rand)
{
    rng_state = rng_state * 6364136223846793005ULL + 1442695040888963407ULL;
    RET32((uint32_t)(rng_state >> 33));
}
/* FTL_SEED=<n> : graine fixe (ignore les srand(time) du jeu) pour des parties reproductibles */
EXT(_srand)
{
    const char *e = getenv("FTL_SEED");
    uint64_t sd = e ? (uint64_t)atoll(e) : (uint64_t)A32(0);
    { extern void rp_note_seed(uint64_t); rp_note_seed(sd); }
    rng_state = sd * 2654435761ULL + 1;
}

static uint32_t q_cmp, q_size;
static int q_less(uint32_t a, uint32_t b)
{
    R32(REG_r0) = a;
    R32(REG_r1) = b;
    rt_call_ind(q_cmp);
    return (int32_t)R32(REG_r0) < 0;
}
EXT(_qsort)
{
    uint32_t base = A32(0), n = A32(1), size = A32(2);
    q_cmp = A32(3);
    q_size = size;
    if (n < 2) return;
    /* tri par insertion stable sur les elements invites (suffisant pour de petits tableaux) */
    uint8_t *tmp = malloc(size);
    for (uint32_t i = 1; i < n; i++) {
        memcpy(tmp, MEM + base + i * size, size);
        uint32_t tmpaddr = rt_alloc(size);
        memcpy(MEM + tmpaddr, tmp, size);
        uint32_t j = i;
        while (j > 0 && q_less(tmpaddr, base + (j - 1) * size)) {
            memcpy(MEM + base + j * size, MEM + base + (j - 1) * size, size);
            j--;
        }
        memcpy(MEM + base + j * size, MEM + tmpaddr, size);
        rt_free(tmpaddr);
    }
    free(tmp);
    if (getenv("FTL_QSORTCHECK")) {
        for (uint32_t i = 1; i < n; i++) if (q_less(base + i * size, base + (i - 1) * size)) {
            fprintf(stderr, "qsort : tri incorrect a %u/%u (taille %u, base %08x)\n", i, n, size, base); break; }
        fprintf(stderr, "qsort n=%u size=%u ok\n", n, size);
    }
}

/* ---------------- iostream : initialiseurs statiques sans effet ---------------- */
EXT(__ZNSt8ios_base4InitC1Ev) { }
EXT(__ZNSt8ios_base4InitD1Ev) { }

/* ---------------- fichiers POSIX (chemins relatifs au dossier courant) ---------------- */
#ifdef _WIN32
#include <io.h>
#include <direct.h>
#endif
#include <sys/stat.h>
static void set_errno(int e) { static uint32_t ea; if (!ea) ea = rt_alloc(16); M32(ea) = (uint32_t)e; }
EXT(_access) { RET32(_access(GSTR(A32(0)), (int)A32(1) & 6) == 0 ? 0 : (uint32_t)-1); }
EXT(_mkdir) { int r = _mkdir(GSTR(A32(0))); RET32(r == 0 ? 0 : (uint32_t)-1); }
EXT(_unlink) { RET32(remove(GSTR(A32(0))) == 0 ? 0 : (uint32_t)-1); }
EXT(_remove) { RET32(remove(GSTR(A32(0))) == 0 ? 0 : (uint32_t)-1); }
EXT(_rename) { RET32(rename(GSTR(A32(0)), GSTR(A32(1))) == 0 ? 0 : (uint32_t)-1); }
EXT(_chdir) { RET32(_chdir(GSTR(A32(0))) == 0 ? 0 : (uint32_t)-1); }

#define NFILES 64
static FILE *cfiles[NFILES];
EXT(_fopen)
{
    FILE *f = fopen(GSTR(A32(0)), GSTR(A32(1)));
    if (!f) { set_errno(2); RET32(0); return; }
    for (int i = 0; i < NFILES; i++)
        if (!cfiles[i]) { cfiles[i] = f; RET32(0x7000 + i); return; }
    fclose(f);
    RET32(0);
}
static FILE *cf(uint32_t h) { return (h >= 0x7000 && h < 0x7000 + NFILES) ? cfiles[h - 0x7000] : NULL; }
EXT(_fclose) { FILE *f = cf(A32(0)); if (f) { fclose(f); cfiles[A32(0) - 0x7000] = NULL; } RET32(0); }
EXT(_fread) { FILE *f = cf(A32(3)); RET32(f ? (uint32_t)fread(GP(A32(0)), A32(1), A32(2), f) : 0); }
EXT(_fseek) { FILE *f = cf(A32(0)); RET32(f ? (uint32_t)fseek(f, (long)(int32_t)A32(1), (int)A32(2)) : (uint32_t)-1); }
EXT(_ftell) { FILE *f = cf(A32(0)); RET32(f ? (uint32_t)ftell(f) : (uint32_t)-1); }

/* ---------------- setjmp / longjmp (chemin d'erreur de libpng : on suppose qu'on n'y retourne jamais) ---------------- */
EXT(_setjmp) { RET32(0); }
EXT(_longjmp) { fprintf(stderr, "longjmp invite non supporte (erreur dans le code invite ?)\n"); rt_where("lr", R32(REG_lr)); fflush(stderr); exit(14); }
