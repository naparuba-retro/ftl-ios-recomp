/* Runtime minimal de la recompilation : memoire invitee, appels indirects, HLE de base. */
#include "plat.h"
#include <stdio.h>
#include <stdlib.h>
#include "rt.h"
#include "binary_config.h"

uint8_t *MEM;
RT_TLS uint8_t REGS[REGS_SIZE];

void rt_unsupported(const char *what)
{
    fprintf(stderr, "RT: operation non supportee: %s (pc approx r15=%08x lr=%08x)\n", what,
            R32(REG_pc), R32(REG_lr));
    exit(3);
}

void rt_where(const char *label, uint32_t a)
{
    unsigned lo = 0, hi = GEN_FNTAB_N;
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        if (GEN_FNTAB[mid].addr <= (a & ~1u)) lo = mid + 1; else hi = mid;
    }
    fprintf(stderr, "    %s = %08x dans la fonction f_%08x\n", label, a, lo ? GEN_FNTAB[lo - 1].addr : 0);
}

void rt_call_ind(uint32_t addr)
{
    uint32_t a = addr & ~1u;
    unsigned lo = 0, hi = GEN_FNTAB_N;
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        if (GEN_FNTAB[mid].addr < a) lo = mid + 1; else hi = mid;
    }
    if (lo < GEN_FNTAB_N && GEN_FNTAB[lo].addr == a) { GEN_FNTAB[lo].fn(); return; }
    fprintf(stderr, "RT: appel indirect vers une adresse inconnue %08x\n", addr);
    rt_where("appelant (lr)", R32(REG_lr));
    fprintf(stderr, "    r0=%08x r1=%08x r2=%08x r3=%08x sp=%08x\n", R32(REG_r0), R32(REG_r1), R32(REG_r2), R32(REG_r3), R32(REG_sp));
    exit(4);
}

void rt_tail_ind(uint32_t addr) { rt_call_ind(addr); }

/* ---- memoire invitee ---- */
void rt_commit(uint32_t start, uint32_t size)
{
    uint32_t s = start & ~0xFFFu, e = (start + size + 0xFFFu) & ~0xFFFu;
#ifdef _WIN32
    if (!VirtualAlloc(MEM + s, e - s, MEM_COMMIT, PAGE_READWRITE)) {
#else
    if (mprotect(MEM + s, (size_t)e - s, PROT_READ | PROT_WRITE) != 0) {
#endif
        fprintf(stderr, "commit %08x+%x impossible\n", start, size);
        exit(2);
    }
}

void rt_init_memory(const char *gendir)
{
#ifdef _WIN32
    MEM = (uint8_t *)VirtualAlloc(NULL, 0x100000000ULL, MEM_RESERVE, PAGE_NOACCESS);
#else
    MEM = (uint8_t *)mmap(NULL, 0x100000000ULL, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (MEM == (uint8_t *)MAP_FAILED) MEM = NULL;
#endif
    if (!MEM) { fprintf(stderr, "reservation 4 Go impossible\n"); exit(2); }
    rt_commit(0, 0x380000); /* image complete (bss) + zone des symboles externes de Ghidra (0x36e000+) */
    char path[1024];
    snprintf(path, sizeof path, "%s/blocks.txt", gendir);
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); exit(2); }
    char name[256], file[256];
    unsigned long start, size;
    while (fscanf(f, "%255s %lx %lu %255s", name, &start, &size, file) == 4) {
        snprintf(path, sizeof path, "%s/%s", gendir, file);
        FILE *b = fopen(path, "rb");
        if (!b) { perror(path); exit(2); }
        rt_commit((uint32_t)start, (uint32_t)size);
        if (fread(MEM + start, 1, size, b) != size) { fprintf(stderr, "lecture %s\n", path); exit(2); }
        fclose(b);
    }
    fclose(f);
}

/* ---- tas invite : classes de taille puissances de 2, listes libres ---- */
static CRITICAL_SECTION heap_cs;
static int heap_cs_ready;
static uint32_t heap_next = 0x20000000;
static uint32_t heap_end = 0x20000000;
static uint32_t bins[28];
static int heap_check, heap_poison;

static void heap_lock(void)
{
    if (!heap_cs_ready) { InitializeCriticalSection(&heap_cs); heap_cs_ready = 1; }
    EnterCriticalSection(&heap_cs);
}

uint32_t rt_heap_used(void) { return heap_next - 0x20000000; }
uint32_t rt_alloc(uint32_t size)
{
    if (!size) size = 1;
    if (size > 0x1000000) { fprintf(stderr, "RT: gros malloc %u\n", size); rt_backtrace("malloc"); }
    int c = 0;
    uint32_t cap = 16;
    while (cap < size && c < 27) { cap <<= 1; c++; }
    if (!heap_cs_ready) { heap_check = getenv("FTL_HEAPCHECK") != NULL; heap_poison = getenv("FTL_POISON") != NULL; }
    heap_lock();
    uint32_t p;
    if (bins[c]) {
        p = bins[c];
        if (heap_check) {
            uint32_t capw = (16u << c) / 4;
            if (capw > 16384) capw = 16384;
            for (uint32_t k = 2; k < capw; k++) if (M32(p + 4 * k) != 0xDEADBEEFu) {
                fprintf(stderr, "RT: ecriture apres free : bloc %08x classe %u mot %u = %08x ; libere depuis ", p, c, k, M32(p + 4 * k));
                rt_where("lr", M32(p + 4)); break;
            }
            if (M32(p) != 0 && (M32(p) < 0x20000000u || M32(p) >= heap_next))
                { fprintf(stderr, "RT: lien du tas corrompu : bloc %08x classe %u lien %08x ; libere depuis ", p, c, M32(p)); rt_where("lr", M32(p + 4)); }
        }
        bins[c] = M32(p);
        M32(p - 4) = (uint32_t)c;
    } else {
        uint32_t need = cap + 16;
        if (heap_next + need > heap_end) {
            uint32_t chunk = need > 0x1000000 ? need : 0x1000000;
            rt_commit(heap_end, chunk);
            heap_end += chunk;
        }
        p = heap_next + 16;
        M32(p - 4) = (uint32_t)c;
        heap_next += need;
    }
    LeaveCriticalSection(&heap_cs);
    if (heap_poison) memset(MEM + p, 0xCD, cap);
    return p;
}

void rt_free(uint32_t p)
{
    if (!p) return;
    uint32_t c = M32(p - 4);
    if (c > 27) { fprintf(stderr, "RT: free() invalide ou double (%s) %08x\n", c & 0x100 ? "double" : "pointeur", p); rt_backtrace("free"); exit(5); }
    if (getenv("FTL_NOREUSE")) { M32(p - 4) = c | 0x100; return; }
    heap_lock();
    M32(p - 4) = c | 0x100;
    if (heap_check) {
        uint32_t capw = (16u << c) / 4;
        if (capw > 16384) capw = 16384;
        M32(p + 4) = R32(REG_lr);
        for (uint32_t k = 2; k < capw; k++) M32(p + 4 * k) = 0xDEADBEEFu;
    }
    M32(p) = bins[c];
    bins[c] = p;
    LeaveCriticalSection(&heap_cs);
}

uint32_t rt_realloc(uint32_t p, uint32_t size)
{
    if (!p) return rt_alloc(size);
    uint32_t cap = 16u << M32(p - 4);
    if (size <= cap) return p;
    uint32_t q = rt_alloc(size);
    memcpy(MEM + q, MEM + p, cap);
    rt_free(p);
    return q;
}

/* ---- piles et threads invites ---- */
static uint32_t stack_next = 0x60000000;
uint32_t rt_new_stack(uint32_t size)
{
    heap_lock();
    uint32_t base = stack_next;
    stack_next += (size + 0xFFFFu) & ~0xFFFFu;
    LeaveCriticalSection(&heap_cs);
    rt_commit(base, size);
    if (getenv("FTL_POISON")) memset(MEM + base, 0xCD, size - 4096);
    return base + size - 64;  /* sommet de pile */
}

void rt_call_guest(uint32_t addr, uint32_t r0)
{
    R32(REG_r0) = r0;
    rt_call_ind(addr);
}

/* pile d'appels invitee via le pointeur de cadre r7 (ABI iOS) */
void rt_backtrace(const char *title)
{
    fprintf(stderr, "  pile (%s) :\n", title);
    uint32_t fp = R32(REG_r7);
    rt_where("   lr", R32(REG_lr));
    for (int i = 0; i < 10 && fp > 0x1000 && fp < 0x7fffffff; i++) {
        uint32_t ret = M32(fp + 4);
        rt_where("   ret", ret);
        uint32_t nfp = M32(fp);
        if (nfp <= fp) break;
        fp = nfp;
    }
}

volatile int rt_trace_on;
uint32_t g_player_ship, g_other_ship;
static const char *trace_filter;
static int trace_depth_limit = 2000000000;
void rt_trace_enter(uint32_t addr)
{
    if (!trace_filter) { trace_filter = getenv("FTL_CALLTRACE"); if (!trace_filter) { rt_trace_on = 0; return; } }
    {   /* FTL_WATCH=<adresse invitee hex> : signale tout changement de l'octet, avec les dernieres fonctions entrees */
        static uint32_t wa; static int wv = -1, winit; static uint32_t ring[64]; static int rn;
        if (!winit) { winit = 1; const char *e = getenv("FTL_WATCH"); wa = e ? (uint32_t)strtoul(e, NULL, 16) : 0; }
        extern volatile int g_frames;
        if (wa && g_frames >= 1700) {
            ring[rn++ & 63] = addr;
            int v = MG1(wa);
            if (v != wv) {
                if (wv >= 0) {
                    fprintf(stderr, "[WATCH %08x : %d -> %d ; dernieres fonctions entrees :", wa, wv, v);
                    for (int k = 1; k <= 40; k++) {
                        uint32_t a = ring[(rn - 1 - k) & 63]; unsigned l2 = 0, h2 = GEN_NAMES_N;
                        while (l2 < h2) { unsigned m = (l2 + h2) / 2; if (GEN_NAMES[m].addr < a) l2 = m + 1; else h2 = m; }
                        fprintf(stderr, " <%s", (l2 < GEN_NAMES_N && GEN_NAMES[l2].addr == a) ? GEN_NAMES[l2].name : "?");
                    }
                    fprintf(stderr, "]\n");
                }
                wv = v;
            }
        }
    }
    unsigned lo = 0, hi = GEN_NAMES_N;
    while (lo < hi) { unsigned m = (lo + hi) / 2; if (GEN_NAMES[m].addr < addr) lo = m + 1; else hi = m; }
    if (lo < GEN_NAMES_N && GEN_NAMES[lo].addr == addr) {
        const char *n = GEN_NAMES[lo].name;
        if (!strcmp(trace_filter, "all") || strstr(n, trace_filter)) {
            if (!strcmp(n, "_gray_raster_render")) {
                uint32_t pr = R32(REG_r1), tg = M32(pr), src = M32(pr + 4);
                fprintf(stderr, "[gray_raster_render target rows=%u width=%u pitch=%d buf=%08x flags=%u | src n_points=%d]\n", M32(tg), M32(tg + 4), (int)M32(tg + 8), M32(tg + 12), M32(pr + 8), (int)(int16_t)(M32(src) >> 16));
            }
            if (!strcmp(n, "SoundControl::ParseSoundFile")) {
                const char *s = GSTR(R32(REG_r0)); fprintf(stderr,"[PSF words %08x %08x %08x %08x]\n",M32(R32(REG_r0)),M32(R32(REG_r0)+4),M32(R32(REG_r0)+8),M32(R32(REG_r0)+12)); fprintf(stderr,"[PSF r0=%08x len=%u cap=%u rc=%d r1=%08x r2=%08x sp=%08x]\n",R32(REG_r0),M32(R32(REG_r0)-12),M32(R32(REG_r0)-8),(int)M32(R32(REG_r0)-4),R32(REG_r1),R32(REG_r2),R32(REG_sp)); char b[64]; strncpy(b, s, 63); b[63] = 0;
                for (char *q = b; *q; q++) if (*q < 32) *q = ' ';
                fprintf(stderr, "[ParseSoundFile buf=%08x \"%s\"]\n", R32(REG_r0), b);
            }
            if (!strcmp(n, "CFPS::OnLoop")) {
                static int c2; if ((c2++ % 120) == 1) {
#ifdef BIN_FN_TIME_NOW
                    extern void BIN_FN_TIME_NOW(void); uint8_t sv[REGS_SIZE]; memcpy(sv, REGS, REGS_SIZE);
                    BIN_FN_TIME_NOW(); fprintf(stderr, "[time_now brut r0=%08x r1=%08x d18=%016llx d16=%016llx]\n", R32(REG_r0), R32(REG_r1), (unsigned long long)*(uint64_t *)(REGS + 0x390), (unsigned long long)*(uint64_t *)(REGS + 0x380)); memcpy(REGS, sv, REGS_SIZE);
#endif
                }
                if ((c2 % 120) == 1 && 0) {
                    uint32_t t = R32(REG_r0); double a, b2; float f; uint64_t u = *(uint64_t *)(MEM + t + 4), w = *(uint64_t *)(MEM + t + 0xc); uint32_t v = M32(t + 0x14);
                    memcpy(&a, &u, 8); memcpy(&b2, &w, 8); memcpy(&f, &v, 4);
                    fprintf(stderr, "[CFPS::OnLoop this=%08x d4=%g dc=%g speed=%g n18=%u n1c=%u n20=%u]\n", t, a, b2, f, M32(t + 0x18), M32(t + 0x1c), M32(t + 0x20));
                }
            }
            if (!strcmp(n, "CommandGui::OnTouch")) {
                uint32_t t = R32(REG_r0);
                { extern volatile int g_frames; fprintf(stderr, "[image %d] ", g_frames); } fprintf(stderr, "[CommandGui::OnTouch type=%u id=%u x=%g y=%g | v1018=%08x..%08x | [2394]=%08x [2398]=%08x [239c]=%08x vec(23a0)=%08x..%08x]\n", R32(REG_r1), R32(REG_r2),
                        *(float *)(MEM + R32(REG_sp) + 0), *(float *)(MEM + R32(REG_sp) + 4), M32(t + 0x1018), M32(t + 0x101c), M32(t + 0x2394), M32(t + 0x2398), M32(t + 0x239c), M32(t + 0x23a0), M32(t + 0x23a4));
            }
            if (!strcmp(n, "CommandGui::OnTouch") && R32(REG_r1) == 1) {
                uint32_t t = R32(REG_r0), b = M32(t + 0x1018), e = M32(t + 0x101c);
                fprintf(stderr, "[liste 1018 (%u elements):", (e - b) / 4);
                for (uint32_t p = b; p < e; p += 4) { uint32_t o = M32(p); fprintf(stderr, " %08x(vt=%08x,open=%u)", o, M32(o), MG1(o + 4)); }
                fprintf(stderr, "]\n");
            }
            if (!strcmp(n, "CommandGui::LoadEvent")) {
                uint32_t t = R32(REG_r1), p = M32(t + 0x106c);
                const char *s1 = p > 0x10000 && p < 0x60000000 ? (const char *)(MEM + p) : "";
                if (s1[0]) fprintf(stderr, "[LoadEvent: evenement en attente \"%.60s\" int=%d]\n", s1, (int)M32(t + 0x1070));
            }
            if (!strcmp(n, "CommandGui::OnLoop")) {
                extern volatile int g_frames; static uint8_t snap[0x2600]; static int have; uint32_t t = R32(REG_r0);
                const char *e = getenv("FTL_SNAP"); int f1 = e ? atoi(e) : 0, f2 = 0; if (e) { while (*e && *e != ',') e++; if (*e) f2 = atoi(e + 1); }
                if (f1 && g_frames == f1 && !have) { memcpy(snap, MEM + t, sizeof snap); have = 1; fprintf(stderr, "[snap CommandGui @%08x a l'image %d]\n", t, g_frames); }
                if (f2 && g_frames == f2 && have == 1) {
                    have = 2; fprintf(stderr, "[diff CommandGui image %d -> %d :", f1, f2);
                    for (uint32_t o = 0; o < sizeof snap; o += 4) { uint32_t a = *(uint32_t *)(snap + o), b = M32(t + o); if (a != b) fprintf(stderr, " +%x:%x->%x", o, a, b); }
                    fprintf(stderr, "]\n");
                }
            }
            if (!strcmp(n, "ChoiceBox::OnRender")) {
                extern volatile int g_frames; static int lastf = -1; const char *e = getenv("FTL_CBDUMP"); int f = e ? atoi(e) : 0;
                if (f && g_frames >= f && g_frames != lastf && (g_frames - f) % 100 == 0 && g_frames < f + 400) {
                    lastf = g_frames; uint32_t t = R32(REG_r0);
                    fprintf(stderr, "[ChoiceBox@%08x image %d :", t, g_frames);
                    for (int i = 0; i < 0x40; i++) {
                        uint32_t v = M32(t + 4 * i); fprintf(stderr, " +%x=%x", 4 * i, v);
                        if ((v > 0x10000 && v < 0x380000 - 64) || (v >= 0x20000000 && v < 0x20000000 + rt_heap_used() - 64)) { const char *s = (const char *)(MEM + v); int k = 0; while (k < 50 && s[k] >= 32 && s[k] < 127) k++; if (k >= 6) fprintf(stderr, "\"%.40s\"", s); }
                    }
                    fprintf(stderr, "]\n");
                }
            }
            if (!strcmp(n, "CommandGui::NewLocation") || !strcmp(n, "WorldManager::CreateShip") || !strcmp(n, "ShipAI::SetEscapeTimer") || !strcmp(n, "ShipAI::Escape") || !strcmp(n, "ShipAI::Escaped")) {
                extern volatile int g_frames;
                fprintf(stderr, "[appel %s a l'image %d r0=%08x r1=%08x r2=%08x]\n", n, g_frames, R32(REG_r0), R32(REG_r1), R32(REG_r2));
            }
            if (!strncmp(n, "ShipManager::Damage", 19) || !strcmp(n, "ShipManager::GetDodged") || !strcmp(n, "ShipManager::SetDestroyed") || !strcmp(n, "ShipAI::Destroyed") || !strcmp(n, "ShipManager::JumpLeave")) {
                extern volatile int g_frames;
                fprintf(stderr, "[%s image %d ship=%08x r1=%08x r2=%08x r3=%08x]\n", n, g_frames, R32(REG_r0), R32(REG_r1), R32(REG_r2), R32(REG_r3));
            }
            if (!strcmp(n, "ShipManager::JumpLeave")) { extern uint32_t g_player_ship; if (!g_player_ship) g_player_ship = R32(REG_r0); }
            if (!strcmp(n, "ShipManager::OnLoop")) { extern uint32_t g_player_ship, g_other_ship; uint32_t t = R32(REG_r0); if (!g_player_ship) g_player_ship = t; else if (t != g_player_ship) g_other_ship = t; }
            if (!strcmp(n, "CFPS::OnLoop")) {
                extern volatile int g_frames; extern uint32_t g_player_ship, g_other_ship; static int done; const char *e = getenv("FTL_KILLENEMY");
                if (e && !done && g_frames >= atoi(e) && g_other_ship) {
                    done = 1;
#ifdef BIN_FN_DAMAGE_HULL
                    extern void BIN_FN_DAMAGE_HULL(void); uint8_t sv[REGS_SIZE]; memcpy(sv, REGS, REGS_SIZE);
                    R32(REG_r0) = g_other_ship; R32(REG_r1) = 200; R32(REG_r2) = 1;
                    fprintf(stderr, "[TEST : degats massifs sur le vaisseau ennemi %08x a l'image %d]\n", g_other_ship, g_frames);
                    BIN_FN_DAMAGE_HULL(); memcpy(REGS, sv, REGS_SIZE);
#endif
                }
            }
            if (!strcmp(n, "_userdata_save_data") || !strcmp(n, "_userdata_load_data") || !strcmp(n, "_userdata_delete_data")) {
                uint32_t p = R32(REG_r0); static int cnt; if (strcmp(n, "_userdata_load_data") && cnt++ < 200) fprintf(stderr, "[%s chemin=\"%.60s\" r1=%08x r2=%u]\n", n, p > 0x1000 ? (const char *)(MEM + p) : "?", R32(REG_r1), R32(REG_r2));
            }
            if (!strncmp(n, "freetype::", 10) && strstr(n, "print") && getenv("FTL_FONTDBG")) {
                static int cnt; uint32_t a[8] = { R32(REG_r0), R32(REG_r1), R32(REG_r2), R32(REG_r3), M32(R32(REG_sp)), M32(R32(REG_sp) + 4), M32(R32(REG_sp) + 8), M32(R32(REG_sp) + 12) };
                for (int i = 0; i < 8; i++) {
                    uint32_t p = a[i]; if (p < 0x1000 || p >= 0x70000000) continue;
                    uint32_t d = (p < 0x380000 || (p >= 0x20000000 && p < 0x20000000 + rt_heap_used())) ? M32(p) : 0;
                    const char *s = (d > 0x1000 && d < 0x70000000 && ((d >= 0x20000000 && d < 0x20000000 + rt_heap_used()) || d < 0x380000)) ? (const char *)(MEM + d) : "";
                    if (!strstr(s, "Kestrel") && ((p >= 0x20000000 && p < 0x20000000 + rt_heap_used() - 64) || (p < 0x380000 - 64))) s = (const char *)(MEM + p);
                    if (strstr(s, "Kestrel") && cnt == 0) { uint32_t fo = R32(REG_r1); fprintf(stderr, "[police @%08x :", fo); for (int q = 0; q < 24; q++) { uint32_t w = M32(fo + 4 * q); fprintf(stderr, " %x", w); if ((w >= 0x20000000 && w < 0x20000000 + rt_heap_used() - 64) || (w > 0x1000 && w < 0x380000 - 64)) { const char *z = (const char *)(MEM + w); int k = 0; while (k < 40 && z[k] >= 32 && z[k] < 127) k++; if (k >= 4) fprintf(stderr, "\"%.40s\"", z); } } fprintf(stderr, "]\n"); }
                    if (strstr(s, "Kestrel") && cnt++ < 6) fprintf(stderr, "[%s : arg%d texte \"%.30s\" ; r0=%08x r1=%08x r2=%08x r3=%08x sp0=%08x sp1=%08x]\n", n, i, s, a[0], a[1], a[2], a[3], a[4], a[5]);
                }
            }
            if (!strcmp(n, "TextInput::OnLoop")) {
                static char last[128]; uint32_t t = R32(REG_r0), p = M32(t + 4); char cur[128] = "";
                if (p > 0x1000 && p < 0x70000000) { const unsigned char *s = (const unsigned char *)(MEM + p); int k; for (k = 0; k < 40 && s[k]; k++) sprintf(cur + 3 * k, "%02x ", s[k]); }
                if (strcmp(cur, last)) { strcpy(last, cur); fprintf(stderr, "[TextInput chaine = %s]\n", cur); }
            }
            if (!strcmp(n, "CommandGui::IsPaused")) {
                static uint32_t last = 0xffffffff; uint32_t t = R32(REG_r0);
                uint32_t v = MG1(t + 0x19bc) | (MG1(t + 0x19bd) << 8) | (MG1(t + 0x19be) << 16) | (MG1(t + 0x19bf) << 24);
                if (v != last) { last = v; fprintf(stderr, "[pause: 19bc=%u 19bd=%u 19be=%u 19bf=%u]\n", MG1(t + 0x19bc), MG1(t + 0x19bd), MG1(t + 0x19be), MG1(t + 0x19bf)); }
            }
            if (!strcmp(n, "ChoiceBox::GetChoice")) {
                static int last = -12345; uint32_t t = R32(REG_r0); int v = (int)M32(t + 0x48);
                if (v != last) { last = v; fprintf(stderr, "[ChoiceBox::GetChoice this=%08x choix=%d potentiel=%d]\n", t, v, (int)M32(t + 0x44)); }
            }
            if (!strcmp(n, "ChoiceBox::OnTouch")) {
                uint32_t t = R32(REG_r0), b = M32(t + 0x38), e = M32(t + 0x3c);
                fprintf(stderr, "[ChoiceBox::OnTouch this=%08x type=%d x=%d y=%d | potentiel=%d choix=%d | %u rect(s):", t, (int)R32(REG_r1), (int)R32(REG_r2), (int)R32(REG_r3), (int)M32(t + 0x44), (int)M32(t + 0x48), (e - b) / 16);
                for (uint32_t p = b; p < e && p < b + 64; p += 16) fprintf(stderr, " (%d,%d,%d,%d)", (int)M32(p), (int)M32(p + 4), (int)M32(p + 8), (int)M32(p + 12));
                fprintf(stderr, " | taille_police=%d | choix0 @%08x: %d %d %d %d]\n", (int)M32(t + 0x54), M32(t + 0x28), (int)M32(M32(t + 0x28)), (int)M32(M32(t + 0x28) + 4), (int)M32(M32(t + 0x28) + 8), (int)M32(M32(t + 0x28) + 12));
            }
            if (!strcmp(n, "TutorialManager::OnTouch")) {
                uint32_t t = R32(REG_r0);
                fprintf(stderr, "[TutorialManager::OnTouch this=%08x running=%u +66=%u type=%u | words+4:", t, MG1(t), MG1(t + 0x66), R32(REG_r1));
                for (int i = 0; i < 20; i++) fprintf(stderr, " %x", M32(t + 4 + 4 * i));
                fprintf(stderr, "]\n");
            }
            if (!strcmp(n, "CFPS::OnLoopUnpaused")) {
                static int c; if ((c++ % 60) == 0) { uint32_t t = R32(REG_r0); float f; uint32_t v = M32(t + 0x14); memcpy(&f, &v, 4); double a, b; uint64_t u = *(uint64_t *)(MEM + t + 4), w = *(uint64_t *)(MEM + t + 0xc); memcpy(&a, &u, 8); memcpy(&b, &w, 8);
                  fprintf(stderr, "[CFPS speed=%g d4=%g dc=%g n1c=%u unit=%g w=%08x %08x]\n", f, a, b, M32(t + 0x1c), *(double *)(MEM + 0x28b000), M32(0x28b000), M32(0x28b004)); }
            }
            if (!strcmp(n, "StarMap::DeleteMap")) {
                uint32_t t = R32(REG_r0);
                fprintf(stderr, "[DeleteMap this=%08x reach=%u total=%u]\n", t, M32(t + 0x668), (M32(t + 0x24) - M32(t + 0x20)) / 4);
            }
            if (!strcmp(n, "_FT_Render_Glyph_Internal")) {
                uint32_t sl = R32(REG_r1);
                fprintf(stderr, "[RenderGlyph slot=%08x format=%08x n_contours=%d n_points=%d pts=%08x w=%u h=%u]\n", sl, M32(sl + 0x48),
                        (int)(int16_t)(M32(sl + 0x6c) & 0xffff), (int)(int16_t)(M32(sl + 0x6c) >> 16), M32(sl + 0x70), M32(sl + 0x50), M32(sl + 0x4c));
                { int np = (int)(int16_t)(M32(sl + 0x6c) >> 16); uint32_t pp = M32(sl + 0x70); int mnx = 1 << 30, mxx = -(1 << 30), mny = 1 << 30, mxy = -(1 << 30);
                  for (int i = 0; i < np && pp; i++) { int x = (int)M32(pp + 8 * i), y = (int)M32(pp + 8 * i + 4); if (x < mnx) mnx = x; if (x > mxx) mxx = x; if (y < mny) mny = y; if (y > mxy) mxy = y; }
                  fprintf(stderr, "   cbox hote x[%d..%d] y[%d..%d]\n", mnx, mxx, mny, mxy); }
            }
            if (strstr(n, "ResourceControl::LoadFile") && R32(REG_r1) > 0x1000) {
                uint32_t sd = M32(R32(REG_r1));
                if (sd > 0x1000 && sd < 0x7f000000) fprintf(stderr, "[LoadFile \"%s\"]\n", (char *)(MEM + sd));
            }
            if (trace_depth_limit-- > 0) { fprintf(stderr, "[appel %s @%08x]\n", n, addr); fflush(stderr); }
        }
    }
}

volatile int rt_probe_on;
void rt_probe(uint32_t addr)
{
    static int budget = 2500;
    if (budget-- <= 0) return;
    fprintf(stderr, "@%08x r0=%08x r1=%08x r2=%08x r3=%08x r4=%08x r5=%08x r6=%08x r8=%08x r9=%08x r10=%08x r11=%08x r12=%08x ZR=%d NG=%d CY=%d OV=%d\n",
            addr, R32(REG_r0), R32(REG_r1), R32(REG_r2), R32(REG_r3), R32(REG_r4), R32(REG_r5), R32(REG_r6), R32(REG_r8), R32(REG_r9), R32(REG_r10), R32(REG_r11), R32(REG_r12),
            G1(0x65), G1(0x64), G1(0x66), G1(0x67));
}

/* moniteur exclusif ARM emule par compare-and-swap (par thread : adresse et valeur lues par ldrex) */
static RT_TLS uint32_t ex_addr = 0xffffffffu, ex_val;
uint32_t rt_ldrex(uint32_t a)
{
    uint32_t v = *(volatile uint32_t *)(MEM + a);
    ex_addr = a; ex_val = v;
    return v;
}
uint32_t rt_strex(uint32_t a, uint32_t v)
{
    int ok = ex_addr == a && (uint32_t)_InterlockedCompareExchange((volatile long *)(MEM + a), (long)v, (long)ex_val) == ex_val;
    ex_addr = 0xffffffffu;
    return ok ? 0 : 1;
}
