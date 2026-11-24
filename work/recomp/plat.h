/* Couche de portabilite : Windows natif, ou Linux (Steam Deck) avec de petits equivalents POSIX des appels Win32 utilises.
 * Le code du runtime garde ses noms Win32 ; seules la fenetre/GL/son ont des versions SDL2 (#ifndef _WIN32 dans gl_host.c, hle_sound.c). */
#pragma once
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <sched.h>
#include <pthread.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/syscall.h>

typedef unsigned long DWORD;
typedef long LONG;
typedef int BOOL;
typedef void *LPVOID;
typedef void *HANDLE;
typedef intptr_t LPARAM;
typedef uintptr_t WPARAM;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define INFINITE 0xFFFFFFFFul
#define WAIT_OBJECT_0 0
#define WAIT_TIMEOUT 258
#define ERROR_FILE_NOT_FOUND 2
#define MAKELPARAM(l, h) ((LPARAM)(((unsigned)(unsigned short)(l)) | ((unsigned)(unsigned short)(h) << 16)))
#define LOWORD(l) ((unsigned short)((l) & 0xffff))
#define HIWORD(l) ((unsigned short)(((l) >> 16) & 0xffff))

typedef union { int64_t QuadPart; } LARGE_INTEGER;
static inline BOOL QueryPerformanceFrequency(LARGE_INTEGER *f) { f->QuadPart = 1000000000LL; return 1; }
static inline BOOL QueryPerformanceCounter(LARGE_INTEGER *c)
{
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    c->QuadPart = (int64_t)t.tv_sec * 1000000000LL + t.tv_nsec; return 1;
}
static inline void Sleep(DWORD ms) { struct timespec t = { (time_t)(ms / 1000), (long)(ms % 1000) * 1000000L }; nanosleep(&t, NULL); }
static inline BOOL SwitchToThread(void) { sched_yield(); return 1; }
static inline DWORD GetCurrentThreadId(void) { return (DWORD)syscall(SYS_gettid); }
static inline DWORD GetLastError(void) { return 0; }

#define InterlockedExchange(p, v) __atomic_exchange_n((p), (v), __ATOMIC_SEQ_CST)
#define InterlockedIncrement(p) (__atomic_add_fetch((p), 1, __ATOMIC_SEQ_CST))
static inline LONG InterlockedCompareExchange(volatile LONG *p, LONG exch, LONG comp)
{ __atomic_compare_exchange_n(p, &comp, exch, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); return comp; }

/* sections critiques / variables de condition */
typedef pthread_mutex_t CRITICAL_SECTION;
typedef pthread_cond_t CONDITION_VARIABLE;
static inline void InitializeCriticalSection(CRITICAL_SECTION *c)
{ pthread_mutexattr_t a; pthread_mutexattr_init(&a); pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE); pthread_mutex_init(c, &a); pthread_mutexattr_destroy(&a); }
static inline void EnterCriticalSection(CRITICAL_SECTION *c) { pthread_mutex_lock(c); }
static inline void LeaveCriticalSection(CRITICAL_SECTION *c) { pthread_mutex_unlock(c); }
static inline void InitializeConditionVariable(CONDITION_VARIABLE *v) { pthread_cond_init(v, NULL); }
static inline void WakeConditionVariable(CONDITION_VARIABLE *v) { pthread_cond_signal(v); }
static inline BOOL SleepConditionVariableCS(CONDITION_VARIABLE *v, CRITICAL_SECTION *c, DWORD ms) { (void)ms; pthread_cond_wait(v, c); return 1; }

/* objets noyau : threads, semaphores, mutex (HANDLE = pointeur vers PH) */
typedef struct PH {
    int kind;                       /* 1 thread, 2 semaphore, 3 mutex */
    pthread_t th; int started, done;
    DWORD (*fn)(LPVOID); LPVOID arg;
    long count;                     /* semaphore */
    pthread_mutex_t m; pthread_cond_t c;   /* etat (thread/semaphore) ; pour kind 3, m est le mutex recursif lui-meme */
} PH;
static inline void *ph_thread_main(void *p)
{
    PH *h = (PH *)p; h->fn(h->arg);
    pthread_mutex_lock(&h->m); h->done = 1; pthread_cond_broadcast(&h->c); pthread_mutex_unlock(&h->m);
    return NULL;
}
static inline HANDLE CreateThread(void *attr, size_t stack, DWORD (*fn)(LPVOID), LPVOID arg, DWORD flags, DWORD *id)
{
    (void)attr; (void)flags; (void)id;
    PH *h = (PH *)calloc(1, sizeof *h); h->kind = 1; h->fn = fn; h->arg = arg;
    pthread_mutex_init(&h->m, NULL); pthread_cond_init(&h->c, NULL);
    pthread_attr_t a; pthread_attr_init(&a); if (stack) pthread_attr_setstacksize(&a, stack);
    if (pthread_create(&h->th, &a, ph_thread_main, h) != 0) { pthread_attr_destroy(&a); free(h); return NULL; }
    pthread_attr_destroy(&a); h->started = 1;
    return h;
}
static inline HANDLE CreateSemaphore(void *attr, LONG init, LONG max, const char *name)
{
    (void)attr; (void)max; (void)name;
    PH *h = (PH *)calloc(1, sizeof *h); h->kind = 2; h->count = init;
    pthread_mutex_init(&h->m, NULL); pthread_cond_init(&h->c, NULL);
    return h;
}
static inline HANDLE CreateMutex(void *attr, BOOL owner, const char *name)
{
    (void)attr; (void)name;
    PH *h = (PH *)calloc(1, sizeof *h); h->kind = 3;
    InitializeCriticalSection(&h->m);
    if (owner) pthread_mutex_lock(&h->m);
    return h;
}
static inline BOOL ReleaseSemaphore(HANDLE hh, LONG n, LONG *prev)
{
    PH *h = (PH *)hh; (void)prev;
    pthread_mutex_lock(&h->m); h->count += n; pthread_cond_broadcast(&h->c); pthread_mutex_unlock(&h->m);
    return 1;
}
static inline BOOL ReleaseMutex(HANDLE hh) { pthread_mutex_unlock(&((PH *)hh)->m); return 1; }
static inline DWORD WaitForSingleObject(HANDLE hh, DWORD ms)
{
    PH *h = (PH *)hh; struct timespec ts;
    if (!h) return WAIT_TIMEOUT;
    if (h->kind == 3) { pthread_mutex_lock(&h->m); return WAIT_OBJECT_0; }
    if (ms != INFINITE) { clock_gettime(CLOCK_REALTIME, &ts); ts.tv_sec += ms / 1000; ts.tv_nsec += (long)(ms % 1000) * 1000000L; if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; } }
    pthread_mutex_lock(&h->m);
    for (;;) {
        if (h->kind == 2 && h->count > 0) { h->count--; pthread_mutex_unlock(&h->m); return WAIT_OBJECT_0; }
        if (h->kind == 1 && h->done) { pthread_mutex_unlock(&h->m); return WAIT_OBJECT_0; }
        if (ms == INFINITE) pthread_cond_wait(&h->c, &h->m);
        else if (pthread_cond_timedwait(&h->c, &h->m, &ts) != 0) { pthread_mutex_unlock(&h->m); return WAIT_TIMEOUT; }
    }
}
static inline BOOL CloseHandle(HANDLE hh)
{
    PH *h = (PH *)hh; if (!h) return 0;
    if (h->kind == 1 && h->started) { pthread_detach(h->th); h->started = 0; }
    return 1;   /* petites fuites volontaires : les objets du jeu sont peu nombreux */
}

#define _stricmp strcasecmp
#define _fseeki64 fseeko
#define _ftelli64 ftello
#define _putenv_s(k, v) setenv((k), (v), 1)
#define CreateDirectoryA(p, sa) mkdir((p), 0777)
#define _strnicmp strncasecmp
#define _access(p, m) access((p), (m))
#define _mkdir(p) mkdir((p), 0777)
#define _chdir(p) chdir(p)
#define _InterlockedCompareExchange(p, e, c) InterlockedCompareExchange((volatile LONG *)(p), (LONG)(e), (LONG)(c))
#endif
