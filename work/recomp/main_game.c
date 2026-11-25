/* Point d'entree PC : charge l'image du binaire iOS recompile, execute les initialiseurs
 * statiques C++ (__mod_init_func) puis appelle SIL (sil__main) comme le ferait l'app iOS.
 * Usage : ftl_pc [dossier_de_ftl.dat] */
#include "plat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rt.h"
#include "gen_syms.h"
#ifdef _WIN32
#include <dbghelp.h>
#else
#include <signal.h>
#ifdef __ANDROID__
#include <SDL2/SDL.h>      /* renomme main en SDL_main : l'activite SDL l'appelle */
int android_prepare_data(void);
void android_log_init(void);
#else
#include <execinfo.h>
#endif
#endif
#ifdef RTC_BUILD
#include <rtcapi.h>
static int rtc_err(int t, const wchar_t *f, int l, const wchar_t *mod, const wchar_t *fmt, ...)
{
    static int inited, count;
    static char seen[64][2][64]; (void)seen;
    void *bt[6]; int n = CaptureStackBackTrace(1, 6, bt, NULL);
    HANDLE hp = GetCurrentProcess();
    if (!inited) { SymInitialize(hp, NULL, TRUE); inited = 1; }
    static DWORD64 done[512]; static int nd;
    for (int i = 0; i < n; i++) {
        DWORD d = 0; IMAGEHLP_LINE64 ln = { sizeof ln };
        if (SymGetLineFromAddr64(hp, (DWORD64)(uintptr_t)bt[i], &d, &ln) && !strstr(ln.FileName, "vctools")) {
            DWORD64 key = ((DWORD64)ln.LineNumber << 32) ^ (DWORD64)(uintptr_t)ln.FileName;
            int dup = 0; for (int k = 0; k < nd; k++) if (done[k] == key) dup = 1;
            if (i == 0 || 1) { if (dup) return 0; if (nd < 512) done[nd++] = key; }
            fprintf(stderr, "[RTC #%d %s:%lu]\n", t, ln.FileName, ln.LineNumber);
            break;
        }
    }
    (void)f; (void)l; (void)mod; (void)fmt; (void)count;
    return 0;
}
#endif

extern const char *g_datadir;
extern volatile int g_frames;

#ifdef _WIN32
static LONG WINAPI crash_filter(EXCEPTION_POINTERS *ep)
{
    uintptr_t fa = ep->ExceptionRecord->NumberParameters > 1 ? (uintptr_t)ep->ExceptionRecord->ExceptionInformation[1] : 0;
    { HANDLE hp = GetCurrentProcess(); SymInitialize(hp, NULL, TRUE); DWORD d = 0; IMAGEHLP_LINE64 ln = { sizeof ln };
      if (SymGetLineFromAddr64(hp, (DWORD64)(uintptr_t)ep->ExceptionRecord->ExceptionAddress, &d, &ln)) fprintf(stderr, "\n[source hote %s:%lu]", ln.FileName, ln.LineNumber); }
    fprintf(stderr, "\n[rip hote = base+%llx]", (unsigned long long)((uintptr_t)ep->ExceptionRecord->ExceptionAddress - (uintptr_t)GetModuleHandle(NULL)));
    fprintf(stderr, "\nPLANTAGE hote code=%08lx adresse=%p", ep->ExceptionRecord->ExceptionCode, (void *)fa);
    if (fa >= (uintptr_t)MEM && fa < (uintptr_t)MEM + 0x100000000ULL) fprintf(stderr, " (adresse invitee %08x)", (uint32_t)(fa - (uintptr_t)MEM));
    fprintf(stderr, "\n    r0=%08x r1=%08x r2=%08x r3=%08x r4=%08x r5=%08x\n    sp=%08x lr=%08x\n",
            R32(REG_r0), R32(REG_r1), R32(REG_r2), R32(REG_r3), R32(REG_r4), R32(REG_r5), R32(REG_sp), R32(REG_lr));
    fprintf(stderr, "    r6=%08x r7=%08x r8=%08x r9=%08x r10=%08x r11=%08x r12=%08x\n", R32(REG_r6), R32(REG_r7), R32(REG_r8), R32(REG_r9), R32(REG_r10), R32(REG_r11), R32(REG_r12));
    fprintf(stderr, "    [r7-0xd8]=%08x [r7-0xd4]=%08x [r7-0xac]=%08x mem[sp..]:", M32(R32(REG_r7) - 0xd8), M32(R32(REG_r7) - 0xd4), M32(R32(REG_r7) - 0xac));
    for (int i = 0; i < 24; i++) fprintf(stderr, " %x", M32(R32(REG_sp) + 4 * i));
    fprintf(stderr, "\n");
    { uint32_t b = M32(R32(REG_r7) - 0xac); fprintf(stderr, "    book@%08x:", b); for (int i = 0; i < 8; i++) fprintf(stderr, " %x", M32(b + 4 * i));
      uint32_t ll = M32(b + 8), ne = M32(b + 4); fprintf(stderr, "\n    lengthlist(%u):", ne); for (uint32_t i = 0; i < ne && i < 40; i++) fprintf(stderr, " %d", (int)M32(ll + 4 * i)); fprintf(stderr, "\n"); }
    rt_where("lr", R32(REG_lr));
    rt_backtrace("crash");
    fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}
#else
static void crash_signal(int sig, siginfo_t *si, void *uc)
{
    (void)uc;
    uintptr_t fa = (uintptr_t)si->si_addr;
    fprintf(stderr, "\nPLANTAGE hote signal=%d adresse=%p", sig, (void *)fa);
    if (fa >= (uintptr_t)MEM && fa < (uintptr_t)MEM + 0x100000000ULL) fprintf(stderr, " (adresse invitee %08x)", (uint32_t)(fa - (uintptr_t)MEM));
    fprintf(stderr, "\n    r0=%08x r1=%08x r2=%08x r3=%08x r4=%08x r5=%08x\n    sp=%08x lr=%08x\n",
            R32(REG_r0), R32(REG_r1), R32(REG_r2), R32(REG_r3), R32(REG_r4), R32(REG_r5), R32(REG_sp), R32(REG_lr));
    rt_where("lr", R32(REG_lr));
#ifndef __ANDROID__
    { void *bt[24]; int n = backtrace(bt, 24); backtrace_symbols_fd(bt, n, 2); }
#endif
    fflush(stderr);
    _exit(11);
}
#endif
void S_sil__main(void);
void hle_stream_init_data(void);
int host_window_init(int w, int h);

#include "binary_config.h"   /* adresses propres au binaire : binary.toml */
#define MOD_INIT_START BIN_MOD_INIT_START
#define MOD_INIT_SIZE BIN_MOD_INIT_SIZE

static int game_main(int argc, char **argv)
{
#ifdef _WIN32
    SetUnhandledExceptionFilter(crash_filter);
#else
    { struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_sigaction = crash_signal; sa.sa_flags = SA_SIGINFO; sigaction(SIGSEGV, &sa, NULL); sigaction(SIGBUS, &sa, NULL); sigaction(SIGFPE, &sa, NULL); sigaction(SIGILL, &sa, NULL); }
#endif
#ifdef RTC_BUILD
    _RTC_SetErrorFuncW(rtc_err);
#endif
    rt_trace_on = getenv("FTL_CALLTRACE") != NULL;
    rt_probe_on = getenv("FTL_PROBE") != NULL;
#ifdef _WIN32
    { ULONG g = 65536; SetThreadStackGuarantee(&g); }
#endif
    if (argc > 1) g_datadir = argv[1];
#ifdef __ANDROID__
    android_log_init();
    /* Android : la fenetre d'abord (barre de progression pendant la copie de ftl.dat hors de l'APK), puis le dossier de travail du jeu */
    if (!host_window_init(1024, 768)) { fprintf(stderr, "Impossible de creer la fenetre OpenGL ES\n"); return 1; }
    if (!android_prepare_data()) return 1;
#endif
    rt_init_memory("genj");
    R32(REG_sp) = rt_new_stack(8u << 20);
    R32(REG_lr) = 0;

#ifndef __ANDROID__
    if (!host_window_init(1024, 768)) { fprintf(stderr, "Impossible de creer la fenetre OpenGL\n"); return 1; }
#endif
    hle_stream_init_data();

    /* initialiseurs statiques (dyld les executerait avant main) */
    for (uint32_t i = 0; i < MOD_INIT_SIZE / 4; i++) {
        uint32_t fn = M32(MOD_INIT_START + 4 * i);
        if (!fn) continue;
        fprintf(stderr, "init %u/%u : %08x\n", i + 1, MOD_INIT_SIZE / 4, fn);
        fflush(stderr);
        rt_call_ind(fn);
    }

    uint32_t argv_a = rt_alloc(16), name = rt_alloc(8);
    strcpy(GSTR(name), "FTL");
    M32(argv_a) = name;
    M32(argv_a + 4) = 0;
    R32(REG_r0) = 1;
    R32(REG_r1) = argv_a;
    R32(REG_r2) = BIN_SIL_MAIN;  /* sil_main (FTL) : 3e argument de sil__main dans cette version de SIL */
    fprintf(stderr, "sil__main...\n");
    fflush(stderr);
    S_sil__main();
    fprintf(stderr, "sil__main retourne %d apres %d images\n", (int)R32(REG_r0), g_frames);
    return (int)R32(REG_r0);
}

#ifdef _WIN32
int main(int argc, char **argv) { return game_main(argc, argv); }
#else
/* Linux : le jeu tourne dans un thread a grande pile (256 Mo, comme /STACK sous Windows) */
static int g_argc; static char **g_argv; static int g_ret;
static DWORD WINAPI game_thread(LPVOID p) { (void)p; g_ret = game_main(g_argc, g_argv); return 0; }
int main(int argc, char **argv)
{
    g_argc = argc; g_argv = argv;
    HANDLE h = CreateThread(NULL, 256u << 20, game_thread, NULL, 0, NULL);
    if (!h) { fprintf(stderr, "thread principal impossible\n"); return 1; }
    WaitForSingleObject(h, INFINITE);
    return g_ret;
}
#endif
