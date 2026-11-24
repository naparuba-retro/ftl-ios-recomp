/* Backend hote des fonctions sys_* de SIL 0.3 (couche systeme remplacee par du code PC).
 * Fichiers/temps/threads : reels (Windows). Graphisme, son, entrees : "nuls" (mode sans fenetre)
 * pour faire tourner la logique du jeu ; a remplacer par OpenGL/WASAPI/etc. */
#include "plat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "rt.h"
#include "binary_config.h"   /* adresses propres au binaire (binary.toml) */

#define SYS(name) void ext__sys_##name(void)

const char *g_datadir = ".";
static int trace_on(void) { static int t = -1; if (t < 0) t = getenv("FTL_TRACE") != NULL; return t; }
#define TR(...) do { if (trace_on()) { fprintf(stderr, __VA_ARGS__); fflush(stderr); } } while (0)
static int g_max_frames = 0;   /* 0 = illimite ; FTL_FRAMES=<n> pour les tests */
volatile int g_frames;
volatile LONG g_quit;
static uint32_t g_input_cb;

/* ---------------- enregistrement / relecture d'une partie ----------------
 * FTL_RECORD=<dossier|auto>  : enregistre dans <dossier>\actions.txt la graine/l'heure de depart, toutes les entrees (toucher,
 *                              clavier, texte) avec le numero d'image ou le jeu les a recues, et copie les sauvegardes de depart.
 * FTL_REPLAY=<dossier>       : rejoue exactement la partie (horloge virtuelle 16,667 ms/image, memes entrees aux memes images,
 *                              sauvegardes de depart restaurees dans <dossier>\savedata_run : les vraies sauvegardes ne sont pas touchees).
 *                              FTL_REPLAY_LIVE=1 : rend la main au clavier/souris a la fin de l'enregistrement ; FTL_FAST=1 : sans limite de vitesse.
 * Dans les deux modes l'horloge du jeu est virtuelle (reproductible) et time() vaut epoch + image/60. */
const char *g_sd = "savedata";
typedef struct { int frame, type, detail, val; float x, y; } RpEv;
static int rp_mode, rp_inited, rp_n, rp_i, rp_end = -1, rp_susp = -1, rp_live, rp_fast;
static FILE *rp_log; static RpEv *rp_ev; static int64_t rp_epoch; static char rp_dir[300], rp_run[340];
extern void host_drain_events(void);
extern int host_next_event(int *type, int *detail, float *x, float *y, int *val);
#ifdef _WIN32
static void rp_bs(char *s) { for (; *s; s++) if (*s == '/') *s = '\\'; }
#else
static void rp_bs(char *s) { (void)s; }
#endif
static void rp_cmd(const char *fmt, const char *a, const char *b)
{
    char c[1000], A[400], B[400]; snprintf(A, sizeof A, "%s", a); snprintf(B, sizeof B, "%s", b ? b : ""); rp_bs(A); rp_bs(B);
    snprintf(c, sizeof c, fmt, A, B); system(c);
}
#ifdef _WIN32
#define RP_RMDIR "rmdir /S /Q \"%s\" >nul 2>&1"
#define RP_COPY "xcopy /E /I /Y /Q \"%s\" \"%s\" >nul 2>&1"
#else
#define RP_RMDIR "rm -rf \"%s\" 2>/dev/null"
#define RP_COPY "mkdir -p \"%2$s\"; cp -r \"%1$s\"/. \"%2$s\"/ 2>/dev/null"
#endif
static void rp_close(void) { if (rp_log) { fprintf(rp_log, "end %d\n", g_frames); fclose(rp_log); rp_log = NULL; } }
static void rp_init(void)
{
    if (rp_inited) return;
    rp_inited = 1;
    const char *r = getenv("FTL_RECORD"), *p = getenv("FTL_REPLAY");
    rp_fast = getenv("FTL_FAST") != NULL;
    char path[400];
    if (p) {
        rp_mode = 2; snprintf(rp_dir, sizeof rp_dir, "%s", p);
        snprintf(path, sizeof path, "%s/actions.txt", rp_dir);
        FILE *f = fopen(path, "r");
        if (!f) { fprintf(stderr, "FTL_REPLAY : %s introuvable\n", path); exit(9); }
        char line[200]; int cap = 0;
        while (fgets(line, sizeof line, f)) {
            long long v; int fr, ty, d, va; unsigned xb, yb;
            if (sscanf(line, "epoch %lld", &v) == 1) rp_epoch = v;
            else if (sscanf(line, "seed %lld", &v) == 1) { char b[40]; snprintf(b, sizeof b, "%lld", v); _putenv_s("FTL_SEED", b); }
            else if (sscanf(line, "ev %d %d %d %d %x %x", &fr, &ty, &d, &va, &xb, &yb) == 6) {
                if (rp_n == cap) { cap = cap ? cap * 2 : 1024; rp_ev = realloc(rp_ev, (size_t)cap * sizeof *rp_ev); }
                RpEv *e = &rp_ev[rp_n++]; e->frame = fr; e->type = ty; e->detail = d; e->val = va; memcpy(&e->x, &xb, 4); memcpy(&e->y, &yb, 4);
            }
            else if (sscanf(line, "susp %d", &fr) == 1) rp_susp = fr;
            else if (sscanf(line, "end %d", &fr) == 1) rp_end = fr;
        }
        fclose(f);
        if (rp_end < 0) rp_end = (rp_n ? rp_ev[rp_n - 1].frame : 0) + 120;
        rp_live = getenv("FTL_REPLAY_LIVE") != NULL;
        snprintf(rp_run, sizeof rp_run, "%s/savedata_run", rp_dir); g_sd = rp_run;
        char st[400]; snprintf(st, sizeof st, "%s/savedata_start", rp_dir);
        rp_cmd(RP_RMDIR, rp_run, NULL);
        CreateDirectoryA(rp_run, NULL);
        rp_cmd(RP_COPY, st, rp_run);
        fprintf(stderr, "[relecture %s : %d entrees, %d images]\n", rp_dir, rp_n, rp_end);
    } else if (r) {
        rp_mode = 1; rp_epoch = (int64_t)time(NULL);
        if (!strcmp(r, "auto")) snprintf(rp_dir, sizeof rp_dir, "replays/partie_%lld", (long long)rp_epoch); else snprintf(rp_dir, sizeof rp_dir, "%s", r);
        CreateDirectoryA("replays", NULL); CreateDirectoryA(rp_dir, NULL);
        char st[400]; snprintf(st, sizeof st, "%s/savedata_start", rp_dir);
        CreateDirectoryA(st, NULL);
        rp_cmd(RP_COPY, "savedata", st);
        snprintf(path, sizeof path, "%s/actions.txt", rp_dir);
        rp_log = fopen(path, "w");
        if (!rp_log) { fprintf(stderr, "FTL_RECORD : impossible d'ecrire %s\n", path); exit(9); }
        const char *sd = getenv("FTL_SEED");
        fprintf(rp_log, "# FTL enregistrement : entrees recues par le jeu, par numero d'image\n# ev <image> <type> <detail> <valeur> <x:float32 hex> <y:float32 hex>\n# types : 6 toucher (29 appui, 30 deplacement, 31 relache ; x,y en 0..1) ; 2 clavier (5 enfonce, 6 relache ; valeur = touche) ; 5 texte (19 caractere, 20 valide, 21 annule, 22 efface...)\n");
        fprintf(rp_log, "epoch %lld\n", (long long)rp_epoch);
        if (sd) fprintf(rp_log, "seed %lld\n", atoll(sd));
        fflush(rp_log); atexit(rp_close);
        fprintf(stderr, "[enregistrement dans %s]\n", rp_dir);
    }
}
uint32_t rp_time(void) { rp_init(); return rp_mode ? (uint32_t)(rp_epoch + g_frames / 60) : 0; }
void rp_note_seed(uint64_t s) { rp_init(); if (rp_log) { fprintf(rp_log, "# srand image %d : %llu\n", g_frames, (unsigned long long)s); fflush(rp_log); } }
static int rp_next_event(int *ty, int *d, float *x, float *y, int *val)
{
    rp_init();
    if (rp_mode == 2 && !(rp_i >= rp_n && rp_live)) {
        host_drain_events();                                   /* souris/clavier reels ignores pendant la relecture */
        if (rp_i < rp_n && rp_ev[rp_i].frame <= g_frames) {
            RpEv *e = &rp_ev[rp_i++]; *ty = e->type; *d = e->detail; *x = e->x; *y = e->y; *val = e->val; return 1;
        }
        return 0;
    }
    int r = host_next_event(ty, d, x, y, val);
    if (rp_log) { static int last = -1; if (g_frames / 300 != last) { last = g_frames / 300; fprintf(rp_log, "end %d\n", g_frames); fflush(rp_log); } }   /* repere de duree (le dernier "end" fait foi) */
    if (r && rp_log) {
        unsigned xb, yb; memcpy(&xb, x, 4); memcpy(&yb, y, 4);
        fprintf(rp_log, "ev %d %d %d %d %08x %08x\n", g_frames, *ty, *d, *val, xb, yb); fflush(rp_log);
    }
    return r;
}
/* limite a 60 images/s l'horloge virtuelle (sinon le jeu tournerait a la vitesse du processeur) */
void rp_pace(void)
{
    static LARGE_INTEGER f, t0; static int f0;
    if (!rp_mode || rp_fast || getenv("FTL_NOVSYNC")) return;
    if (!f.QuadPart) { QueryPerformanceFrequency(&f); QueryPerformanceCounter(&t0); f0 = g_frames; }
    LARGE_INTEGER n;
    for (;;) {
        QueryPerformanceCounter(&n);
        double ms = (double)(n.QuadPart - t0.QuadPart) * 1000.0 / (double)f.QuadPart, want = (g_frames - f0) * (1000.0 / 60.0);
        if (ms > want + 200.0) { t0 = n; f0 = g_frames; return; }   /* en retard : pas de rattrapage en rafale */
        if (ms >= want) return;
        if (want - ms > 2.0) Sleep(1);
    }
}

/* ---------------- fichiers ---------------- */
typedef struct { FILE *f; char path[512]; int64_t size; int used; } HFile;
static HFile files[256];
typedef struct { int32_t result; int used; } HAsync;
static HAsync asyncs[1024];
static CRITICAL_SECTION file_cs;
static int file_cs_init;

static void flock(void) { if (!file_cs_init) { InitializeCriticalSection(&file_cs); file_cs_init = 1; } EnterCriticalSection(&file_cs); }
static void funlock(void) { LeaveCriticalSection(&file_cs); }

static HFile *fh_get(uint32_t h)
{
    if (h < 1 || h > 256 || !files[h - 1].used) return NULL;
    return &files[h - 1];
}

static uint32_t file_open_path(const char *full)
{
    FILE *f = fopen(full, "rb");
    if (!f) return 0;
    flock();
    for (int i = 0; i < 256; i++) {
        if (!files[i].used) {
            files[i].used = 1;
            files[i].f = f;
            strncpy(files[i].path, full, sizeof files[i].path - 1);
            _fseeki64(f, 0, SEEK_END);
            files[i].size = _ftelli64(f);
            _fseeki64(f, 0, SEEK_SET);
            funlock();
            return (uint32_t)(i + 1);
        }
    }
    funlock();
    fclose(f);
    return 0;
}

SYS(file_open)
{
    const char *path = GSTR(A32(0));
    char full[1024];
    if (path[0] == '/' || (path[0] && path[1] == ':')) snprintf(full, sizeof full, "%s", path);
    else snprintf(full, sizeof full, "%s/%s", g_datadir, path);
    uint32_t h = file_open_path(full);
    TR("[file_open %s -> %u]\n", full, h);
    if (!h) fprintf(stderr, "sys_file_open: echec %s\n", full);
    RET32(h);
}
SYS(file_dup)
{
    HFile *f = fh_get(A32(0));
    RET32(f ? file_open_path(f->path) : 0);
}
SYS(file_close)
{
    HFile *f = fh_get(A32(0));
    if (f) { flock(); fclose(f->f); f->used = 0; funlock(); }
}
SYS(file_size)
{
    HFile *f = fh_get(A32(0));
    R32(REG_r1) = 0;
    RET32(f ? (uint32_t)f->size : 0);
}
SYS(file_seek)
{
    HFile *f = fh_get(A32(0));
    /* ancienne API : pos 32 bits (r1) et how (r2) ; sinon pos 64 bits (r2:r3) et how sur la pile */
    int64_t pos; uint32_t how;
    if (A32(2) <= 2) { pos = (int32_t)A32(1); how = A32(2); }
    else { pos = (int64_t)A64(2); how = A32(4); }
    if (!f) { RET32(0); return; }
    flock();
    int r = _fseeki64(f->f, pos, how == 0 ? SEEK_SET : how == 1 ? SEEK_CUR : SEEK_END);
    funlock();
    RET32(r == 0);
}
/* Remplacement de ressources de ftl.dat (dossier override/, indexe par nom de fichier) :
 *  - remplacement sur place : l'entree garde son emplacement et sa taille (zeros de remplissage si le fichier est plus court) ;
 *    (exemple d'usage ; aucun remplacement sur place n'est actif : c&c.ttf <- c&cnew.ttf changeait l'aspect des chiffres du HUD).
 *  - extension virtuelle (virt) : le nouveau fichier, plus grand, est servi apres la fin reelle de ftl.dat et l'entree de l'index
 *    (decalage, taille stockee, taille decompressee) est corrigee a la lecture ; ex. fonts/24.font + glyphes accentues (make_font24.py).
 * Les decalages viennent de l'index de ftl.dat (ftl_dat.py). */
typedef struct { const char *file; int64_t off, size; uint8_t *data; int loaded; int virt; int64_t idx_pos, blob_len; } Ovr;
static Ovr ovrs[] = {
    { "override/24.font",                         0, BIN_FONT24_SIZE, NULL, 0, 1, BIN_FONT24_IDX_POS, 0 },
};
static void ovr_load(Ovr *o)
{
    if (o->loaded) return;
    o->loaded = 1;
    FILE *g = fopen(o->file, "rb");
    if (!g) return;
    fseek(g, 0, SEEK_END); long n = ftell(g); fseek(g, 0, SEEK_SET);
    size_t cap = o->virt ? (size_t)n : (size_t)o->size;
    o->data = calloc(1, cap);
    o->blob_len = o->virt ? n : (n < o->size ? n : o->size);
    if (fread(o->data, 1, (size_t)(o->blob_len), g) != (size_t)o->blob_len) { free(o->data); o->data = NULL; }
    fclose(g);
}
static void put_be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
/* applique les remplacements sur les n octets lus a la position p0 ; renvoie le nombre d'octets valides (len max) */
static size_t ovr_patch(HFile *f, int64_t p0, uint8_t *dst, size_t n, size_t len)
{
    size_t l = strlen(f->path);
    if (l < 7 || _stricmp(f->path + l - 7, "ftl.dat") != 0) return n;
    for (size_t i = 0; i < sizeof ovrs / sizeof ovrs[0]; i++) {
        Ovr *o = &ovrs[i];
        ovr_load(o);
        if (!o->data) continue;
        if (!o->virt) {
            if (p0 + (int64_t)n <= o->off || p0 >= o->off + o->size) continue;
            int64_t a = p0 > o->off ? p0 : o->off, b = (p0 + (int64_t)n < o->off + o->size) ? p0 + (int64_t)n : o->off + o->size;
            memcpy(dst + (a - p0), o->data + (a - o->off), (size_t)(b - a));
            TR("[override %s : %lld octets a %lld]", o->file, (long long)(b - a), (long long)a);
        } else {
            /* 1) correction de l'entree d'index : decalage = fin reelle du fichier, tailles = taille du nouveau fichier */
            uint8_t ent[12]; put_be32(ent, (uint32_t)f->size); put_be32(ent + 4, (uint32_t)o->blob_len); put_be32(ent + 8, (uint32_t)o->blob_len);
            for (int k = 0; k < 12; k++) { int64_t q = o->idx_pos + 8 + k; if (q >= p0 && q < p0 + (int64_t)n) dst[q - p0] = ent[k]; }
            /* 2) donnees du nouveau fichier, servies au-dela de la fin reelle */
            int64_t vs = f->size, ve = f->size + o->blob_len;
            if (p0 < ve && p0 + (int64_t)len > vs) {
                int64_t a = p0 > vs ? p0 : vs, b = (p0 + (int64_t)len < ve) ? p0 + (int64_t)len : ve;
                memcpy(dst + (a - p0), o->data + (a - vs), (size_t)(b - a));
                if ((size_t)(b - p0) > n) n = (size_t)(b - p0);
                TR("[override %s : %lld octets virtuels]", o->file, (long long)(b - a));
            }
        }
    }
    return n;
}
static int32_t do_read(HFile *f, uint32_t buf, int32_t len, int64_t at, int use_at)
{
    if (!f || len < 0) return -1;
    flock();
    int64_t save = _ftelli64(f->f);
    if (use_at) _fseeki64(f->f, at, SEEK_SET);
    int64_t p0 = use_at ? at : save;
    size_t n = fread(MEM + buf, 1, (size_t)len, f->f);
    n = ovr_patch(f, p0, MEM + buf, n, (size_t)len);
    TR("[read fh=%p len=%d at=%lld use_at=%d -> %d]\n", (void *)f, (int)len, (long long)at, use_at, (int)n);
    if (use_at) _fseeki64(f->f, save, SEEK_SET);
    funlock();
    return (int32_t)n;
}
SYS(file_read) { RET32((uint32_t)do_read(fh_get(A32(0)), A32(1), (int32_t)A32(2), 0, 0)); }
SYS(file_read_at) { RET32((uint32_t)do_read(fh_get(A32(0)), A32(1), (int32_t)A32(2), (int64_t)A32(3), 1)); }
SYS(file_read_async)
{
    int32_t r = do_read(fh_get(A32(0)), A32(1), (int32_t)A32(2), (int64_t)A32(3), 1);
    flock();
    for (int i = 0; i < 1024; i++) {
        if (!asyncs[i].used) { asyncs[i].used = 1; asyncs[i].result = r; funlock(); RET32((uint32_t)(i + 1)); return; }
    }
    funlock();
    RET32(0);
}
SYS(file_poll_async) { RET32(1); }
SYS(file_wait_async)
{
    uint32_t id = A32(0);
    int32_t r = -1;
    if (id >= 1 && id <= 1024 && asyncs[id - 1].used) { r = asyncs[id - 1].result; asyncs[id - 1].used = 0; }
    RET32((uint32_t)r);
}
SYS(file_abort_async) { RET32(1); }
SYS(dir_open) { RET32(0); }
SYS(dir_read) { RET32(0); }
SYS(dir_close) { }

/* ---------------- temps ---------------- */
static LARGE_INTEGER t_freq;
SYS(time_init)
{
    QueryPerformanceFrequency(&t_freq);
    RET32(1000000);  /* unites par seconde : microsecondes */
}
/* FTL_FIXEDTIME=1 : horloge virtuelle (16,667 ms par image + les delais demandes) -> executions reproductibles bit a bit */
static int fixed_time = -1;
static volatile int64_t virt_extra;
static int64_t now_us(void)
{
    LARGE_INTEGER n;
    if (fixed_time < 0) { rp_init(); fixed_time = getenv("FTL_FIXEDTIME") != NULL || rp_mode != 0; }
    if (fixed_time) return (int64_t)g_frames * 16667 + virt_extra + 1000000;
    if (!t_freq.QuadPart) QueryPerformanceFrequency(&t_freq);
    QueryPerformanceCounter(&n);
    return (int64_t)((double)n.QuadPart * 1e6 / (double)t_freq.QuadPart);
}
SYS(time_now) { RET64(now_us()); }
SYS(time_delay)
{
    int64_t us = (int64_t)A64(0);
    if (fixed_time > 0) { if (us > 0) virt_extra += us; return; }
    if (us > 0) Sleep((DWORD)((us + 999) / 1000));
}

/* ---------------- threads / mutex / semaphores ---------------- */
typedef struct { HANDLE h; int32_t result; uint32_t func, param, stack; int used; } HThread;
#define MAX_HTHREADS 1024
static HThread threads[MAX_HTHREADS];
static CRITICAL_SECTION thr_cs;
static int thr_cs_init;

static DWORD WINAPI thread_main(LPVOID arg)
{
    HThread *t = (HThread *)arg;
    R32(REG_sp) = t->stack;   /* pile invitee propre a l'emplacement, reutilisee d'un thread a l'autre */
    R32(REG_lr) = 0;
    rt_call_guest(t->func, t->param);
    t->result = (int32_t)R32(REG_r0);
    return 0;
}
SYS(thread_create)
{
    if (!thr_cs_init) { InitializeCriticalSection(&thr_cs); thr_cs_init = 1; }
    EnterCriticalSection(&thr_cs);
    int i;
    for (i = 0; i < MAX_HTHREADS; i++) if (!threads[i].used) break;
    if (i == MAX_HTHREADS) { LeaveCriticalSection(&thr_cs); RET32(0); return; }
    HThread *t = &threads[i];
    t->used = 1;
    LeaveCriticalSection(&thr_cs);
    t->func = A32(2);
    t->param = A32(3);
    if (!t->stack) t->stack = rt_new_stack(1 << 20);
    t->h = CreateThread(NULL, 64u << 20, thread_main, t, 0, NULL);
    if (!t->h) { t->used = 0; RET32(0); return; }
    RET32((uint32_t)(i + 1));
}
SYS(thread_wait)
{
    uint32_t id = A32(0);
    if (id < 1 || id > MAX_HTHREADS || !threads[id - 1].used) { RET32(0); return; }
    HThread *t = &threads[id - 1];
    WaitForSingleObject(t->h, INFINITE);
    if (A32(1)) M32(A32(1)) = (uint32_t)t->result;
    CloseHandle(t->h);
    t->h = NULL;
    t->used = 0;   /* l'emplacement (et sa pile) redevient disponible */
    RET32(1);
}
SYS(thread_yield) { SwitchToThread(); }

static HANDLE mutexes[1024];
SYS(mutex_create)
{
    for (int i = 0; i < 1024; i++)
        if (!mutexes[i]) { mutexes[i] = CreateMutex(NULL, A32(0) ? TRUE : FALSE, NULL); RET32((uint32_t)(i + 1)); return; }
    RET32(0);
}
SYS(mutex_destroy) { uint32_t i = A32(0); if (i && i <= 1024 && mutexes[i - 1]) { CloseHandle(mutexes[i - 1]); mutexes[i - 1] = 0; } }
SYS(mutex_lock) { uint32_t i = A32(0); if (i && i <= 1024 && mutexes[i - 1]) WaitForSingleObject(mutexes[i - 1], INFINITE); }
SYS(mutex_unlock) { uint32_t i = A32(0); if (i && i <= 1024 && mutexes[i - 1]) ReleaseMutex(mutexes[i - 1]); }

static HANDLE sems[1024];
SYS(semaphore_create)
{
    for (int i = 0; i < 1024; i++)
        if (!sems[i]) { sems[i] = CreateSemaphore(NULL, (LONG)A32(0), 0x7fffffff, NULL); TR("[sem %d create init=%d]\n", i + 1, (int)A32(0)); RET32((uint32_t)(i + 1)); return; }
    RET32(0);
}
SYS(semaphore_destroy) { uint32_t i = A32(0); if (i && i <= 1024 && sems[i - 1]) { CloseHandle(sems[i - 1]); sems[i - 1] = 0; } }
SYS(semaphore_wait) { uint32_t i = A32(0); TR("[sem %u wait thr %lu lr=%08x]\n", i, GetCurrentThreadId(), R32(REG_lr)); if (i && i <= 1024 && sems[i - 1]) WaitForSingleObject(sems[i - 1], INFINITE); TR("[sem %u woke thr %lu]\n", i, GetCurrentThreadId()); }
SYS(semaphore_wait_timeout)
{
    uint32_t i = A32(0);
    float to = AF(1);
    if (!(i && i <= 1024 && sems[i - 1])) { RET32(0); return; }
    RET32(WaitForSingleObject(sems[i - 1], (DWORD)(to * 1000.0f)) == WAIT_OBJECT_0);
}
SYS(semaphore_signal) { uint32_t i = A32(0); TR("[sem %u signal thr %lu]\n", i, GetCurrentThreadId()); if (i && i <= 1024 && sems[i - 1]) ReleaseSemaphore(sems[i - 1], 1, NULL); }

/* ---------------- donnees utilisateur ---------------- */
SYS(userdata_get_data_path)
{
    rp_init();
    char p[420]; snprintf(p, sizeof p, "%s%s/", g_sd[0] && g_sd[1] == ':' ? "" : "./", g_sd);
    uint32_t a = rt_alloc((uint32_t)strlen(p) + 1);
    strcpy(GSTR(a), p);
    CreateDirectoryA(g_sd, NULL);
    RET32(a);
}
/* sys_userdata_perform (SIL 0.3, structure SysUserDataParams ARM32) : sauvegardes, reglages et fichiers de donnees
 * dans ./savedata/ (memes noms que sysdep/ios/userdata.c : save/save-NNNN.bin, settings.bin, stats.bin). */
extern void BIN_FN_MEM_ALLOC(void);   /* mem_alloc(size, align, flags) du jeu (binary.toml) */
static uint32_t guest_mem_alloc(uint32_t size)
{
    uint8_t sv[REGS_SIZE]; memcpy(sv, REGS, REGS_SIZE);
    R32(REG_r0) = size; R32(REG_r1) = 0; R32(REG_r2) = 0; R32(REG_r3) = 0;
    BIN_FN_MEM_ALLOC();
    uint32_t r = R32(REG_r0);
    memcpy(REGS, sv, REGS_SIZE);
    return r;
}
static void ud_mkdirs(char *path)
{
    for (char *p = path; *p; p++) if (*p == '/' && p > path && p[-1] != ':') { char c = *p; *p = 0; CreateDirectoryA(path, NULL); *p = c; }
}
static void ud_fix(char *p) { for (; *p; p++) if (*p == 92) *p = '/'; }
SYS(userdata_perform)
{
    uint32_t P = A32(0);
    int op = (int)M32(P), num = (int)M32(P + 16);
    uint32_t dpath = M32(P + 20), sdata = M32(P + 32), ssize = M32(P + 36), scan = M32(P + 72), scount = M32(P + 76);
    char path[600];
    int ok = 0;
    switch (op) {
    case 1: case 2: case 3: snprintf(path, sizeof path, "%s/save/save-%04d.bin", g_sd, num); break;
    case 4: snprintf(path, sizeof path, "%s/save", g_sd); break;
    case 5: case 6: snprintf(path, sizeof path, "%s/settings.bin", g_sd); break;
    case 8: case 9: case 10:
        snprintf(path, sizeof path, "%s/%s", g_sd, dpath ? GSTR(dpath) : "data.bin"); ud_fix(path); break;
    default: path[0] = 0; break;
    }
    ud_mkdirs(path);
    switch (op) {
    case 1: case 5: case 8: {            /* sauvegardes (fichier ecrit en entier) */
        FILE *f = fopen(path, "wb");
        if (f) { ok = fwrite(GP(sdata), 1, ssize, f) == ssize; fclose(f); }
        break; }
    case 2: case 6: case 9: {            /* chargements */
        FILE *f = fopen(path, "rb");
        if (f) {
            fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
            uint32_t buf = guest_mem_alloc((uint32_t)(n > 0 ? n : 1));
            TR("[userdata lecture %s taille=%ld tampon=%08x]\n", path, n, buf);
            if (buf && fread(GP(buf), 1, (size_t)n, f) == (size_t)n) {
                M32(P + 52) = buf; M32(P + 56) = (uint32_t)n; M32(P + 60) = 0; M32(P + 64) = 0; M32(P + 68) = 0; ok = 1;
            }
            fclose(f);
        }
        break; }
    case 3: case 10:                       /* suppressions */
        ok = remove(path) == 0 || GetLastError() == ERROR_FILE_NOT_FOUND || 1;
        break;
    case 4: {                              /* inventaire des sauvegardes */
        memset(GP(scan), 0, scount);
        for (uint32_t i = 0; i < scount; i++) {
            char p2[200]; snprintf(p2, sizeof p2, "%s/save/save-%04d.bin", g_sd, num + (int)i);
            FILE *f = fopen(p2, "rb"); if (f) { MG1(scan + i) = 1; fclose(f); }
        }
        ok = 1; break; }
    case 7: ok = 1; break;                 /* capture d'ecran : ignoree */
    case 11: case 12: case 14: ok = 1; break;  /* statistiques : acceptees sans stockage */
    case 13: ok = 0; break;                /* RETRIEVE_STATS : echec -> valeurs par defaut */
    default: ok = 0; break;
    }
    TR("[userdata op=%d num=%d chemin=%s -> %d]\n", op, num, path, ok);
    RET32(ok);
}
SYS(last_error) { RET32(0); }
SYS(reset_idle_timer) { }

/* ---------------- entrees (nul) ---------------- */
SYS(input_init)
{
    g_input_cb = A32(0);
    rp_init();
    const char *e = getenv("FTL_FRAMES");
    if (e) g_max_frames = atoi(e);
    RET32(1);
}
SYS(input_cleanup) { }
void host_pump(void);
int host_next_event(int *type, int *detail, float *x, float *y, int *val);
void host_text_mode(int on);
void host_text_begin(const char *def);
static uint32_t ev_buf, next_touch_id = 1, cur_touch_id;
static double t_epoch = -1;
static double now_seconds(void)
{
    LARGE_INTEGER n;
    if (!t_freq.QuadPart) QueryPerformanceFrequency(&t_freq);
    QueryPerformanceCounter(&n);
    double t = (double)n.QuadPart / (double)t_freq.QuadPart;
    if (t_epoch < 0) t_epoch = t;
    return t - t_epoch;
}
/* InputEvent (SIL 0.3, ARM32) : type@0 detail@4 timestamp(double)@8 ; touch : id@16 x@20 y@24 initial_x@28 initial_y@32 */
SYS(input_update)
{
    host_pump();
    int ty, d, val; float x, y;
    while (g_input_cb && rp_next_event(&ty, &d, &x, &y, &val)) {
        if (!ev_buf) ev_buf = rt_alloc(64);
        memset(GP(ev_buf), 0, 64);
        M32(ev_buf) = (uint32_t)ty;            /* 2 clavier, 5 texte, 6 toucher */
        M32(ev_buf + 4) = (uint32_t)d;
        double ts = now_seconds();
        memcpy(GP(ev_buf + 8), &ts, 8);
        if (ty == 6) {
            if (d == 29) cur_touch_id = next_touch_id++;
            M32(ev_buf + 16) = cur_touch_id;
            memcpy(GP(ev_buf + 20), &x, 4);
            memcpy(GP(ev_buf + 24), &y, 4);
        } else if (ty == 5) {
            M32(ev_buf + 16) = (uint32_t)val;  /* TextInputEvent.ch */
        } else if (ty == 2) {
            M32(ev_buf + 16) = (uint32_t)val;  /* KeyboardInputEvent.key */
        }
        rt_call_guest(g_input_cb, ev_buf);
    }
}
SYS(input_info)
{
    uint32_t p = A32(0);
    memset(GP(p), 0, 20);
    MG1(p + 15) = 1;  /* has_text */
    MG1(p + 16) = 1;  /* text_uses_custom_interface */
    MG1(p + 17) = 1;  /* text_has_prompt */
    MG1(p + 18) = 1;  /* has_touch */
}
SYS(input_is_quit_requested) { RET32((g_max_frames && g_frames >= g_max_frames) || g_quit || (rp_mode == 2 && !rp_live && g_frames >= rp_end)); }
/* mise en veille (iOS) : FTL_SUSPEND=<image> la simule ; la fenetre qui perd le focus ou se reduit la declenche aussi */
volatile int g_suspend_req;
SYS(input_is_suspend_requested)
{
    static int at = -2, fired;
    if (at == -2) { const char *e = getenv("FTL_SUSPEND"); at = e ? atoi(e) : -1; }
    rp_init();
    if (rp_mode == 2) at = rp_susp;
    if (at >= 0 && !fired && g_frames >= at) { fired = 1; g_suspend_req = 1; if (rp_log) { fprintf(rp_log, "susp %d\n", g_frames); fflush(rp_log); } }
    RET32(g_suspend_req ? 1 : 0);
}
SYS(input_acknowledge_suspend_request) { g_suspend_req = 0; }
/* texte : comme l'OSK iOS, la saisie demarre avec le texte par defaut (CLEAR puis les caracteres), puis edition en direct (touches), DONE a la validation */
SYS(input_text_set_state)
{
    int on = (int)A32(0);
    uint32_t txt = A32(1);
    if (on) host_text_begin(txt ? GSTR(txt) : NULL); else host_text_mode(0);
    TR("[texte : interface active=%d defaut=%s]", on, txt ? GSTR(txt) : "(aucun)");
}

/* ---------------- aides iOS de SIL (sysdep/ios/util.m, gamekit.m, osk.m, ...) ---------------- */
#define IOS(name) void ext__ios_##name(void)
static uint32_t str_app_name, str_support_path;
static uint32_t guest_cstr(const char *s)
{
    uint32_t a = rt_alloc((uint32_t)strlen(s) + 1);
    strcpy(GSTR(a), s);
    return a;
}
IOS(get_application_name) { if (!str_app_name) str_app_name = guest_cstr("FTL"); RET32(str_app_name); }
IOS(get_application_support_path) { if (!str_support_path) { rp_init(); char b[420]; snprintf(b, sizeof b, "%s%s/", g_sd[0] && g_sd[1] == ':' ? "" : "./", g_sd); str_support_path = guest_cstr(b); } RET32(str_support_path); }
IOS(get_device) { RET32(0); }
IOS(get_model) { RET32(0); }
IOS(get_model_for) { RET32(0); }
IOS(version_is_at_least) { RET32(1); }
IOS(compare_versions) { RET32(0); }
IOS(register_vsync_function) { }
IOS(call_vsync_functions) { }
IOS(enable_idle_timer) { }
IOS(forward_input_event) { }
IOS(current_player) { RET32(0); }
IOS(userdata_init) { RET32(1); }
IOS(dialog) { fprintf(stderr, "[dialog iOS ignore]\n"); }
IOS(show_dialog_formatted) { fprintf(stderr, "[dialog formate ignore: %s]\n", GSTR(A32(0))); }
IOS(gamekit_authenticate) { }
IOS(gamekit_auth_status) { RET32(0); }
IOS(gamekit_acknowledge_auth_change) { }
IOS(gamekit_load_achievements) { }
IOS(gamekit_update_achievements) { }
IOS(gamekit_player_id) { RET32(0); }
IOS(gamekit_clear_achievements) { }
IOS(osk_open) { }
IOS(osk_is_running) { RET32(0); }
IOS(osk_get_text) { RET32(0); }
IOS(osk_close) { }

HANDLE hle_sem_handle(uint32_t id) { return (id && id <= 1024) ? sems[id - 1] : NULL; }
