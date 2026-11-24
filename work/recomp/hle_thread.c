/* HLE de pthread / mach semaphores / sched pour le workqueue de SIL (src/workqueue.c, darwin).
 * Les objets invites (pthread_mutex_t, pthread_cond_t) restent opaques : on les associe par
 * adresse a des primitives Windows. */
#include "plat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rt.h"

/* ---------------- threads ---------------- */
typedef struct { HANDLE h; uint32_t fn, arg, ret; } PT;
static PT pts[128];
static int npts = 1;                       /* 0 : thread principal */
static RT_TLS uint32_t tls_self;           /* identifiant pthread du thread courant (0 = principal) */

static DWORD WINAPI pt_main(LPVOID p)
{
    PT *t = (PT *)p;
    tls_self = (uint32_t)(t - pts);
    R32(REG_sp) = rt_new_stack(1u << 20);
    R32(REG_lr) = 0;
    rt_call_guest(t->fn, t->arg);
    t->ret = R32(REG_r0);
    return 0;
}

EXT(_pthread_create)
{
    if (npts >= 128) { RET32(11); return; }
    PT *t = &pts[npts];
    t->fn = A32(2);
    t->arg = A32(3);
    M32(A32(0)) = (uint32_t)npts;
    npts++;
    t->h = CreateThread(NULL, 256u << 20, pt_main, t, 0, NULL);
    RET32(t->h ? 0 : 11);
}
EXT(_pthread_join)
{
    uint32_t id = A32(0);
    if (id > 0 && id < (uint32_t)npts && pts[id].h) {
        WaitForSingleObject(pts[id].h, INFINITE);
        if (A32(1)) M32(A32(1)) = pts[id].ret;
    }
    RET32(0);
}
EXT(_pthread_self) { RET32(tls_self); }
EXT(_pthread_attr_init) { memset(GP(A32(0)), 0, 8); RET32(0); }
EXT(_pthread_attr_destroy) { RET32(0); }
EXT(_pthread_attr_setstacksize) { RET32(0); }
EXT(_pthread_attr_setschedparam) { RET32(0); }
EXT(_pthread_attr_setschedpolicy) { RET32(0); }
EXT(_pthread_getschedparam)
{
    if (A32(1)) M32(A32(1)) = 1;           /* SCHED_OTHER */
    if (A32(2)) M32(A32(2)) = 31;          /* priorite */
    RET32(0);
}
EXT(_sched_get_priority_max) { RET32(47); }
EXT(_sched_get_priority_min) { RET32(15); }
EXT(_sched_yield) { SwitchToThread(); RET32(0); }

/* ---------------- mutex / variables de condition (par adresse) ---------------- */
#define NTAB 256
typedef struct { uint32_t addr; CRITICAL_SECTION cs; CONDITION_VARIABLE cv; int used; } Obj;
static Obj mtab[NTAB], ctab[NTAB];
static CRITICAL_SECTION tab_cs;
static int tab_ready;

static Obj *lookup(Obj *tab, uint32_t addr)
{
    if (!tab_ready) { InitializeCriticalSection(&tab_cs); tab_ready = 1; }
    EnterCriticalSection(&tab_cs);
    for (int i = 0; i < NTAB; i++) {
        if (tab[i].used && tab[i].addr == addr) { LeaveCriticalSection(&tab_cs); return &tab[i]; }
    }
    for (int i = 0; i < NTAB; i++) {
        if (!tab[i].used) {
            tab[i].used = 1;
            tab[i].addr = addr;
            InitializeCriticalSection(&tab[i].cs);
            InitializeConditionVariable(&tab[i].cv);
            LeaveCriticalSection(&tab_cs);
            return &tab[i];
        }
    }
    LeaveCriticalSection(&tab_cs);
    fprintf(stderr, "HLE pthread: table pleine\n");
    exit(13);
}

EXT(_pthread_mutex_lock) { EnterCriticalSection(&lookup(mtab, A32(0))->cs); RET32(0); }
EXT(_pthread_mutex_unlock) { LeaveCriticalSection(&lookup(mtab, A32(0))->cs); RET32(0); }
EXT(_pthread_cond_init) { lookup(ctab, A32(0)); RET32(0); }
EXT(_pthread_cond_destroy) { RET32(0); }
EXT(_pthread_cond_signal) { WakeConditionVariable(&lookup(ctab, A32(0))->cv); RET32(0); }
EXT(_pthread_cond_wait)
{
    Obj *c = lookup(ctab, A32(0)), *m = lookup(mtab, A32(1));
    SleepConditionVariableCS(&c->cv, &m->cs, INFINITE);
    RET32(0);
}

/* ---------------- semaphores mach ---------------- */
static HANDLE msem[1024];
EXT(_semaphore_create)
{
    /* semaphore_create(task, semaphore_t *out, policy, value) */
    for (int i = 0; i < 1024; i++) {
        if (!msem[i]) {
            msem[i] = CreateSemaphore(NULL, (LONG)A32(3), 0x7fffffff, NULL);
            M32(A32(1)) = (uint32_t)(i + 1);
            RET32(0);
            return;
        }
    }
    RET32(1);
}
EXT(_semaphore_destroy)
{
    uint32_t i = A32(1);
    if (i && i <= 1024 && msem[i - 1]) { CloseHandle(msem[i - 1]); msem[i - 1] = 0; }
    RET32(0);
}
/* Les semaphores de SIL sont crees par sys_semaphore_create (hle_sys.c) et manipules par les appels
 * mach semaphore_* des petits wrappers sys_* (traduits en appels directs) : meme table. */
void ext__sys_semaphore_wait(void);
void ext__sys_semaphore_signal(void);
EXT(_semaphore_signal) { ext__sys_semaphore_signal(); RET32(0); }
EXT(_semaphore_wait) { ext__sys_semaphore_wait(); RET32(0); }
EXT(_semaphore_timedwait)
{
    /* semaphore_timedwait(sem, mach_timespec{sec,nsec}) : attente bornee sur la table de hle_sys */
    uint32_t i = A32(0), sec = A32(1), nsec = A32(2);
    extern HANDLE hle_sem_handle(uint32_t id);
    HANDLE h = hle_sem_handle(i);
    if (!h) { RET32(1); return; }
    DWORD r = WaitForSingleObject(h, sec * 1000 + nsec / 1000000);
    RET32(r == WAIT_OBJECT_0 ? 0 : 49);  /* KERN_OPERATION_TIMED_OUT */
}
