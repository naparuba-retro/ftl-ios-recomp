/* HLE de iostream (libstdc++ 4.2, iOS armv7) pour std::stringstream / istringstream /
 * ostringstream, sur la memoire invitee.
 *
 * Le code du jeu construit les flux "en ligne" (constructeurs generes) ; il ne s'appuie sur la
 * bibliotheque que pour : ios_base/basic_ios::init/clear, locale, stringbuf::str/_M_sync,
 * ostream::_M_insert / operator<<, istream::_M_extract / operator>> / getline.
 * Dispositions deduites du code du jeu (verifiees sur to_float, to_int, to_string<T>) :
 *   stringstream : istream @0 (vptr, gcount@4), ostream @8 (vptr), stringbuf @0xc,
 *                  ios_base/basic_ios (base virtuelle) @0x34, taille ~0xbc
 *   stringbuf    : vptr@0, eback@4 gptr@8 egptr@c pbase@10 pptr@14 epptr@18, locale@1c, mode@20,
 *                  std::string _M_string@24
 *   ios_base     : vptr@0 precision@4 width@8 flags@c exceptions@10 iostate@14 ... (0x70 octets)
 *   basic_ios    : tie@70 fill@74 fill_init@75 streambuf@78 ctype@7c num_put@80 num_get@84
 * La base virtuelle est retrouvee comme libstdc++ : this + *(vptr-12). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "rt.h"

uint32_t gs_data(uint32_t sp);
uint32_t gs_len(uint32_t sp);
uint32_t gs_cap(uint32_t sp);
void gs_assign(uint32_t sp, const uint8_t *t, uint32_t n);
void gs_append(uint32_t sp, const uint8_t *t, uint32_t n);
void gs_init(uint32_t sp, const uint8_t *t, uint32_t n);
void gs_reserve(uint32_t sp, uint32_t n);

/* ios_base / basic_ios */
#define B_PREC 0x04
#define B_WIDTH 0x08
#define B_FLAGS 0x0c
#define B_EXC 0x10
#define B_STATE 0x14
#define B_TIE 0x70
#define B_FILL 0x74
#define B_SB 0x78
/* streambuf / stringbuf */
#define S_INBEG 0x04
#define S_INCUR 0x08
#define S_INEND 0x0c
#define S_OUTBEG 0x10
#define S_OUTCUR 0x14
#define S_OUTEND 0x18
#define S_MODE 0x20
#define S_STR 0x24

enum { GOODBIT = 0, BADBIT = 1, EOFBIT = 2, FAILBIT = 4 };
enum { F_FIXED = 0x4, F_HEX = 0x8, F_LEFT = 0x20, F_OCT = 0x40, F_SCI = 0x100, F_SHOWPOS = 0x800,
       F_SKIPWS = 0x1000, F_UPPER = 0x4000, F_BOOLALPHA = 0x1 };

static void fatal(const char *m) { fprintf(stderr, "iostream HLE: %s\n", m); fflush(stderr); exit(12); }

static uint32_t bios_of(uint32_t sub)
{
    uint32_t vp = M32(sub);
    if (!vp) fatal("vptr nul (flux non construit ?)");
    return sub + (uint32_t)(int32_t)M32(vp - 12);
}
static uint32_t sb_of(uint32_t bios) { return M32(bios + B_SB); }
static void set_state(uint32_t bios, uint32_t bits) { M32(bios + B_STATE) = M32(bios + B_STATE) | bits; }
static int good(uint32_t bios) { return M32(bios + B_STATE) == 0; }

/* ---------- synchronisation des pointeurs de zones du stringbuf avec _M_string ---------- */
static void sb_resync(uint32_t sb, uint32_t old_data)
{
    uint32_t str = sb + S_STR;
    uint32_t nd = gs_data(str), len = gs_len(str), cap = gs_cap(str);
    uint32_t mode = M32(sb + S_MODE);
    static const int offs[] = { S_INBEG, S_INCUR, S_INEND, S_OUTBEG, S_OUTCUR };
    if (old_data && old_data != nd) {
        for (int i = 0; i < 5; i++) {
            uint32_t p = M32(sb + offs[i]);
            if (p) M32(sb + offs[i]) = nd + (p - old_data);
        }
    }
    if (mode & 0x08) M32(sb + S_INEND) = nd + len;       /* in : fin des donnees lisibles */
    if (mode & 0x10) M32(sb + S_OUTEND) = nd + cap;      /* out : fin de la capacite */
}

/* ecriture dans le stringbuf a la position courante (pptr) */
static void sb_write(uint32_t sb, const uint8_t *src, uint32_t n)
{
    uint32_t str = sb + S_STR;
    uint32_t mode = M32(sb + S_MODE);
    if (!(mode & 0x10)) return;
    uint32_t od = gs_data(str);
    uint32_t pos = M32(sb + S_OUTCUR) ? M32(sb + S_OUTCUR) - M32(sb + S_OUTBEG) : 0;
    uint32_t len = gs_len(str);
    if (pos + n > len) {
        uint32_t grow = pos + n - len;
        uint8_t *z = calloc(grow, 1);
        gs_append(str, z, grow);
        free(z);
    }
    uint32_t nd = gs_data(str);
    memcpy(MEM + nd + pos, src, n);
    if (!M32(sb + S_OUTBEG)) { M32(sb + S_OUTBEG) = nd; M32(sb + S_OUTCUR) = nd; }
    sb_resync(sb, od);
    /* libstdc++ 4.2 : les zones sont creees paresseusement a la premiere ecriture (overflow -> _M_sync) */
    if ((mode & 0x08) && !M32(sb + S_INBEG)) { M32(sb + S_INBEG) = nd; M32(sb + S_INCUR) = nd; }
    M32(sb + S_OUTBEG) = nd;
    M32(sb + S_OUTCUR) = nd + pos + n;
}

static int sb_peek(uint32_t sb)
{
    uint32_t cur = M32(sb + S_INCUR), end = M32(sb + S_INEND);
    if (!cur || cur >= end) return -1;
    return MG1(cur);
}
static void sb_bump(uint32_t sb) { M32(sb + S_INCUR) = M32(sb + S_INCUR) + 1; }

/* ---------- ios_base / basic_ios / locale ---------- */
EXT(__ZNSt8ios_baseC2Ev) { uint32_t b = A32(0); memset(GP(b), 0, 0x10); }
EXT(__ZNSt8ios_baseD2Ev) { }
EXT(__ZNSt6localeC1Ev) { M32(A32(0)) = 0; }
EXT(__ZNSt6localeD1Ev) { }
EXT(__ZNSt9basic_iosIcSt11char_traitsIcEE4initEPSt15basic_streambufIcS1_E)
{
    uint32_t b = A32(0), sb = A32(1);
    M32(b + B_PREC) = 6;
    M32(b + B_WIDTH) = 0;
    M32(b + B_FLAGS) = F_SKIPWS | 0x2;  /* skipws | dec */
    M32(b + B_EXC) = 0;
    M32(b + B_TIE) = 0;
    MG1(b + B_FILL) = ' ';
    M32(b + B_SB) = sb;
    M32(b + B_STATE) = sb ? GOODBIT : BADBIT;
}
EXT(__ZNSt9basic_iosIcSt11char_traitsIcEE5clearESt12_Ios_Iostate)
{
    uint32_t b = A32(0), st = A32(1);
    if (getenv("FTL_STREAMTRACE")) fprintf(stderr, "[clear bios=%08x etat=%u]\n", b, st);
    M32(b + B_STATE) = sb_of(b) ? st : (st | BADBIT);
}
EXT(__ZNKSt9basic_iosIcSt11char_traitsIcEE5widenEc) { RET32(A32(1) & 0xff); }

/* ---------- stringbuf ---------- */
EXT(__ZNSt15basic_stringbufIcSt11char_traitsIcESaIcEE7_M_syncEPcmm)
{
    uint32_t sb = A32(0), base = A32(1), i = A32(2), o = A32(3);
    uint32_t mode = M32(sb + S_MODE), str = sb + S_STR;
    uint32_t endg = base + gs_len(str), endp = base + gs_cap(str);
    if (base != gs_data(str)) { endg += i; i = 0; endp = endg; }
    if (mode & 0x08) { M32(sb + S_INBEG) = base; M32(sb + S_INCUR) = base + i; M32(sb + S_INEND) = endg; }
    if (mode & 0x10) {
        M32(sb + S_OUTBEG) = base; M32(sb + S_OUTEND) = endp; M32(sb + S_OUTCUR) = base + o;
        if (!(mode & 0x08)) { M32(sb + S_INBEG) = endg; M32(sb + S_INCUR) = endg; M32(sb + S_INEND) = endg; }
    }
}
EXT(__ZNKSt15basic_stringbufIcSt11char_traitsIcESaIcEE3strEv)
{
    uint32_t ret = A32(0), sb = A32(1);
    uint32_t str = sb + S_STR;
    uint32_t data = gs_data(str);
    uint32_t pb = M32(sb + S_OUTBEG), pp = M32(sb + S_OUTCUR), eg = M32(sb + S_INEND);
    uint32_t n;
    if (pp) {
        uint32_t end = pp > eg ? pp : eg;
        n = end - pb;
        uint8_t *t = malloc(n ? n : 1);
        memcpy(t, MEM + pb, n);
        gs_init(ret, t, n);
        free(t);
    } else {
        n = gs_len(str);
        uint8_t *t = malloc(n ? n : 1);
        memcpy(t, MEM + data, n);
        gs_init(ret, t, n);
        free(t);
    }
}

/* ---------- sortie ---------- */
static void put_formatted(uint32_t os, const char *s, size_t n, int numeric)
{
    uint32_t bios = bios_of(os);
    if (getenv("FTL_STREAMTRACE")) fprintf(stderr, "[put os=%08x bios=%08x n=%u etat=%u sb=%08x mode=%u]\n", os, bios, (unsigned)n, M32(bios + B_STATE), sb_of(bios), M32(sb_of(bios) + S_MODE));
    if (!good(bios)) return;
    uint32_t sb = sb_of(bios);
    uint32_t width = M32(bios + B_WIDTH), flags = M32(bios + B_FLAGS);
    if (width > n) {
        size_t pad = width - n;
        char fill = (char)MG1(bios + B_FILL);
        uint8_t *buf = malloc(width);
        if (flags & F_LEFT) { memcpy(buf, s, n); memset(buf + n, fill, pad); }
        else { memset(buf, fill, pad); memcpy(buf + pad, s, n); }
        sb_write(sb, buf, (uint32_t)width);
        free(buf);
    } else {
        sb_write(sb, (const uint8_t *)s, (uint32_t)n);
    }
    M32(bios + B_WIDTH) = 0;
    (void)numeric;
}

static void fmt_int(uint32_t os, int64_t v, int is_unsigned)
{
    uint32_t bios = bios_of(os), flags = M32(bios + B_FLAGS);
    char b[64];
    if (flags & F_HEX) snprintf(b, sizeof b, (flags & F_UPPER) ? "%llX" : "%llx", (unsigned long long)v);
    else if (flags & F_OCT) snprintf(b, sizeof b, "%llo", (unsigned long long)v);
    else if (is_unsigned) snprintf(b, sizeof b, "%llu", (unsigned long long)v);
    else snprintf(b, sizeof b, (flags & F_SHOWPOS) ? "%+lld" : "%lld", (long long)v);
    put_formatted(os, b, strlen(b), 1);
}

EXT(__ZNSolsEi) { fmt_int(A32(0), (int32_t)A32(1), 0); RET32(A32(0)); }
EXT(__ZNSo9_M_insertImEERSoT_) { fmt_int(A32(0), (int64_t)(uint64_t)A32(1), 1); RET32(A32(0)); }
EXT(__ZNSo9_M_insertIbEERSoT_)
{
    uint32_t os = A32(0), v = A32(1) & 0xff, bios = bios_of(os);
    if (M32(bios + B_FLAGS) & F_BOOLALPHA) put_formatted(os, v ? "true" : "false", v ? 4 : 5, 0);
    else fmt_int(os, v, 1);
    RET32(os);
}
EXT(__ZNSo9_M_insertIdEERSoT_)
{
    uint32_t os = A32(0), bios = bios_of(os);
    double d = AD(2);
    uint32_t flags = M32(bios + B_FLAGS), prec = M32(bios + B_PREC);
    char b[512];
    if ((flags & (F_FIXED | F_SCI)) == F_FIXED) snprintf(b, sizeof b, "%.*f", (int)prec, d);
    else if ((flags & (F_FIXED | F_SCI)) == F_SCI) snprintf(b, sizeof b, "%.*e", (int)prec, d);
    else snprintf(b, sizeof b, "%.*g", (int)prec, d);
    put_formatted(os, b, strlen(b), 1);
    RET32(os);
}
EXT(__ZSt16__ostream_insertIcSt11char_traitsIcEERSt13basic_ostreamIT_T0_ES6_PKS3_i)
{
    uint32_t os = A32(0), s = A32(1), n = A32(2);
    uint8_t *t = malloc(n ? n : 1);
    memcpy(t, MEM + s, n);
    put_formatted(os, (const char *)t, n, 0);
    free(t);
    RET32(os);
}

/* ---------- entree ---------- */
static void skip_ws(uint32_t sb)
{
    for (;;) {
        int c = sb_peek(sb);
        if (c < 0 || !isspace(c)) return;
        sb_bump(sb);
    }
}

/* extrait un jeton numerique ; retourne 0 en cas d'echec. tok : tampon hote */
static int take_token(uint32_t is, char *tok, size_t cap, int allow_float)
{
    uint32_t bios = bios_of(is);
    if (!good(bios)) { set_state(bios, FAILBIT); return 0; }
    uint32_t sb = sb_of(bios);
    if (M32(bios + B_FLAGS) & F_SKIPWS) skip_ws(sb);
    size_t n = 0;
    int c = sb_peek(sb);
    if (c < 0) { set_state(bios, EOFBIT | FAILBIT); return 0; }
    for (;;) {
        c = sb_peek(sb);
        if (c < 0) { set_state(bios, EOFBIT); break; }
        int ok = isdigit(c) || ((c == '+' || c == '-') && (n == 0 || (allow_float && (tok[n - 1] == 'e' || tok[n - 1] == 'E'))));
        if (allow_float) ok = ok || c == '.' || c == 'e' || c == 'E';
        if (!ok || n + 1 >= cap) break;
        tok[n++] = (char)c;
        sb_bump(sb);
    }
    tok[n] = 0;
    if (n == 0) { set_state(bios, FAILBIT); return 0; }
    return 1;
}

EXT(__ZNSirsERi)
{
    uint32_t is = A32(0), out = A32(1);
    char tok[64];
    if (take_token(is, tok, sizeof tok, 0)) {
        char *e;
        long v = strtol(tok, &e, 10);
        if (*e) set_state(bios_of(is), FAILBIT); else M32(out) = (uint32_t)(int32_t)v;
    }
    RET32(is);
}
EXT(__ZNSi10_M_extractIfEERSiRT_)
{
    uint32_t is = A32(0), out = A32(1);
    char tok[128];
    if (take_token(is, tok, sizeof tok, 1)) {
        char *e;
        float v = strtof(tok, &e);
        if (*e) set_state(bios_of(is), FAILBIT); else { uint32_t bits; memcpy(&bits, &v, 4); M32(out) = bits; }
    }
    RET32(is);
}
EXT(__ZNSi10_M_extractIbEERSiRT_)
{
    uint32_t is = A32(0), out = A32(1);
    char tok[16];
    if (take_token(is, tok, sizeof tok, 0)) {
        if (!strcmp(tok, "0")) MG1(out) = 0;
        else if (!strcmp(tok, "1")) MG1(out) = 1;
        else set_state(bios_of(is), FAILBIT);
    }
    RET32(is);
}

/* istream::getline(char *s, streamsize n, char delim) */
EXT(__ZNSi7getlineEPcic)
{
    uint32_t is = A32(0), s = A32(1);
    int32_t n = (int32_t)A32(2);
    int delim = (int)(A32(3) & 0xff);
    uint32_t bios = bios_of(is);
    M32(is + 4) = 0;  /* _M_gcount */
    if (!good(bios)) { set_state(bios, FAILBIT); RET32(is); return; }
    uint32_t sb = sb_of(bios);
    int32_t count = 0;
    int hit_delim = 0;
    for (;;) {
        int c = sb_peek(sb);
        if (c < 0) { set_state(bios, EOFBIT); break; }
        if (c == delim) { sb_bump(sb); count++; hit_delim = 1; break; }
        if (count + 1 >= n) { set_state(bios, FAILBIT); break; }
        MG1(s + (uint32_t)(count)) = (uint8_t)c;
        sb_bump(sb);
        count++;
    }
    if (n > 0) {
        int stored = hit_delim ? count - 1 : count;
        MG1(s + (uint32_t)stored) = 0;
    }
    M32(is + 4) = (uint32_t)count;
    if (count == 0 && !hit_delim) set_state(bios, FAILBIT);
    if (getenv("FTL_STREAMTRACE") && count == 0) fprintf(stderr, "[getline vide: sb=%08x mode=%u in=%08x/%08x/%08x str.len=%u]\n", sb, M32(sb + S_MODE), M32(sb + S_INBEG), M32(sb + S_INCUR), M32(sb + S_INEND), gs_len(sb + S_STR));
    if (getenv("FTL_STREAMTRACE")) fprintf(stderr, "[getline n=%d count=%d delim=%d etat=%u \"%s\"]\n", n, count, delim, M32(bios + B_STATE), n > 0 ? (char *)GP(s) : "");
    RET32(is);
}

/* std::getline(istream&, string&, char) */
EXT(__ZSt7getlineIcSt11char_traitsIcESaIcEERSt13basic_istreamIT_T0_ES7_RSbIS4_S5_T1_ES4_)
{
    uint32_t is = A32(0), str = A32(1);
    int delim = (int)(A32(2) & 0xff);
    uint32_t bios = bios_of(is);
    if (!good(bios)) { set_state(bios, FAILBIT); RET32(is); return; }
    uint32_t sb = sb_of(bios);
    gs_assign(str, (const uint8_t *)"", 0);
    uint32_t extracted = 0;
    int hit_delim = 0;
    for (;;) {
        int c = sb_peek(sb);
        if (c < 0) { set_state(bios, EOFBIT); break; }
        sb_bump(sb);
        extracted++;
        if (c == delim) { hit_delim = 1; break; }
        uint8_t ch = (uint8_t)c;
        gs_append(str, &ch, 1);
    }
    if (extracted == 0 && !hit_delim) set_state(bios, FAILBIT);
    if (getenv("FTL_STREAMTRACE")) { uint32_t d = gs_data(str); fprintf(stderr, "[std::getline extrait=%u \"%.*s\" etat=%u]\n", extracted, (int)gs_len(str), (char *)GP(d), M32(bios + B_STATE)); }
    RET32(is);
}

/* std::operator>>(istream&, string&) */
EXT(__ZStrsIcSt11char_traitsIcESaIcEERSt13basic_istreamIT_T0_ES7_RSbIS4_S5_T1_E)
{
    uint32_t is = A32(0), str = A32(1);
    uint32_t bios = bios_of(is);
    if (!good(bios)) { set_state(bios, FAILBIT); RET32(is); return; }
    uint32_t sb = sb_of(bios);
    if (M32(bios + B_FLAGS) & F_SKIPWS) skip_ws(sb);
    gs_assign(str, (const uint8_t *)"", 0);
    uint32_t n = 0;
    for (;;) {
        int c = sb_peek(sb);
        if (c < 0) { set_state(bios, EOFBIT); break; }
        if (isspace(c)) break;
        uint8_t ch = (uint8_t)c;
        gs_append(str, &ch, 1);
        sb_bump(sb);
        n++;
    }
    if (n == 0) set_state(bios, FAILBIT);
    if (getenv("FTL_STREAMTRACE")) { uint32_t d = gs_data(str); fprintf(stderr, "[>>string n=%u \"%.*s\" etat=%u]\n", n, (int)gs_len(str), (char *)GP(d), M32(bios + B_STATE)); }
    M32(bios + B_WIDTH) = 0;
    RET32(is);
}

/* ---------- donnees externes de libstdc++ (VTT / vtables du stringstream) ---------- */
#define SLOT(a) M32(a)
void hle_stream_init_data(void)
{
    /* Les emplacements non-lazy du binaire pointent deja vers des blocs vides (0x36e000+).
     * On y construit : une vtable complete de basic_stringstream et deux vtables de construction.
     * vptr-12 doit donner le decalage de la base virtuelle depuis le sous-objet concerne :
     *   istream@0 -> 0x34 ; ostream@8 -> 0x2c ; ios_base -> 0. */
    uint32_t F = SLOT(0x26c4ac);            /* vtable de basic_stringstream (complete) */
    uint32_t G1 = 0x36f400, G2 = 0x36f440;  /* vtables de construction (istream / ostream dans stringstream) */
    M32(F + 0x00) = 0x34;
    M32(F + 0x14) = 0x2c;
    M32(F + 0x28) = 0;
    M32(G1 + 0x00) = 0x34;
    M32(G2 + 0x00) = 0x2c;
    uint32_t VTT = SLOT(0x26c4a0);          /* VTT de basic_stringstream */
    uint32_t e[10] = { F + 0xc, F + 0xc, G1 + 0xc, F + 0x34, G2 + 0xc, F + 0x34, F + 0x34, F + 0x20, F + 0x34, F + 0x20 };
    for (int i = 0; i < 10; i++) M32(VTT + 4 * i) = e[i];

    /* basic_istringstream : istream@0, stringbuf@8, ios_base@0x30 (emplacements 0x26c544 VTT / 0x26c548 vtable) */
    uint32_t F2 = SLOT(0x26c548);
    M32(F2 + 0x00) = 0x30;
    M32(F2 + 0x14) = 0;
    uint32_t VTT2 = SLOT(0x26c544);
    uint32_t G3 = 0x36f480;
    M32(G3) = 0x30;
    uint32_t e2[6] = { F2 + 0xc, F2 + 0xc, G3 + 0xc, F2 + 0x20, F2 + 0x20, F2 + 0x20 };
    for (int i = 0; i < 6; i++) M32(VTT2 + 4 * i) = e2[i];

    /* basic_ostringstream : ostream@0, stringbuf@4, ios_base@0x2c (0x26c54c VTT / 0x26c550 vtable) */
    uint32_t F3 = SLOT(0x26c550);
    M32(F3 + 0x00) = 0x2c;
    M32(F3 + 0x14) = 0;
    uint32_t VTT3 = SLOT(0x26c54c);
    uint32_t G4 = 0x36f4c0;
    M32(G4) = 0x2c;
    uint32_t e3[6] = { F3 + 0xc, F3 + 0xc, G4 + 0xc, F3 + 0x20, F3 + 0x20, F3 + 0x20 };
    for (int i = 0; i < 6; i++) M32(VTT3 + 4 * i) = e3[i];
}
