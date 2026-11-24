/* Son : sys_sound_* de SIL (sysdep/ios/sound.c) recree cote hote.
 * Le melangeur de SIL (sound_mixer_*) est le code invite recompile ; un thread hote l'appelle
 * par tranches de 1024 images (S16 stereo) comme le fait le thread mixer d'iOS.
 * Windows : waveOut. Linux : SDL2 (file d'attente audio). */
#include "plat.h"
#ifdef _WIN32
#include <mmsystem.h>
#else
#include <SDL2/SDL.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rt.h"
#include "gen_syms.h"

#define SYS(name) void ext__sys_##name(void)
#define SLICE 1024
#define NBUF 4
#define RATE 44100

void S_sound_mixer_init(void);
void S_sound_mixer_get_pcm(void);
void S_sound_mixer_cleanup(void);
void S_sound_mixer_reset(void);
void S_sound_mixer_setdata(void);
void S_sound_mixer_setvol(void);
void S_sound_mixer_setpan(void);
void S_sound_mixer_setfade(void);
void S_sound_mixer_start(void);
void S_sound_mixer_stop(void);
void S_sound_mixer_status(void);

uint32_t rt_heap_used(void);
static int g_ready, g_stop, g_mute;
static HANDLE g_thread;
#ifdef _WIN32
static HANDLE g_event;
static HWAVEOUT g_wo;
static WAVEHDR g_hdr[NBUF];
#else
static SDL_AudioDeviceID g_dev;
#endif
static int16_t g_pcm[NBUF][SLICE * 2];
static CRITICAL_SECTION g_lock;   /* exclut le thread audio des appels de configuration du jeu */
static int g_lock_init;
static volatile LONG g_slices;

/* appelle une fonction invitee a 4 arguments ; sauvegarde les registres d'appel du thread courant */
static uint32_t gcall(void (*fn)(void), uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    R32(REG_r0) = a; R32(REG_r1) = b; R32(REG_r2) = c; R32(REG_r3) = d;
    fn();
    return R32(REG_r0);
}

static uint32_t g_gbuf;
static int g_mainmode;

/* remplit les tampons libres ; renvoie le nombre de tranches produites */
#ifdef _WIN32
static int pump(void)
{
    int worked = 0;
    for (int i = 0; i < NBUF && !g_stop; i++) {
        if (!(g_hdr[i].dwFlags & WHDR_DONE)) continue;
        EnterCriticalSection(&g_lock);
        gcall(S_sound_mixer_get_pcm, g_gbuf, SLICE, 0, 0);
        LeaveCriticalSection(&g_lock);
        if (g_mute) memset(g_pcm[i], 0, SLICE * 4); else memcpy(g_pcm[i], GP(g_gbuf), SLICE * 4);
        g_hdr[i].dwFlags &= ~WHDR_DONE;
        waveOutWrite(g_wo, &g_hdr[i], sizeof g_hdr[i]);
        if (InterlockedIncrement(&g_slices) % 50 == 0 && getenv("FTL_SOUNDTRACE")) {
            int pk = 0; for (int k = 0; k < SLICE * 2; k++) { int v = abs(g_pcm[i][k]); if (v > pk) pk = v; }
            fprintf(stderr, "son : %ld tranches, crete %d, tas %u Mo\n", (long)g_slices, pk, rt_heap_used() >> 20);
        }
        worked = 1;
    }
    return worked;
}
#else
static int pump(void)
{
    int worked = 0;
    while (!g_stop && SDL_GetQueuedAudioSize(g_dev) < (Uint32)(NBUF * SLICE * 4)) {
        int i = (int)(g_slices % NBUF);
        EnterCriticalSection(&g_lock);
        gcall(S_sound_mixer_get_pcm, g_gbuf, SLICE, 0, 0);
        LeaveCriticalSection(&g_lock);
        if (g_mute) memset(g_pcm[i], 0, SLICE * 4); else memcpy(g_pcm[i], GP(g_gbuf), SLICE * 4);
        SDL_QueueAudio(g_dev, g_pcm[i], SLICE * 4);
        InterlockedIncrement(&g_slices);
        worked = 1;
    }
    return worked;
}
#endif

/* mode "principal" (FTL_AUDIO_MAIN) : appele a chaque image depuis le thread du jeu */
void hle_sound_pump_main(void)
{
    if (!g_ready || !g_mainmode) return;
    uint8_t save[REGS_SIZE];
    memcpy(save, REGS, REGS_SIZE);
    pump();
    memcpy(REGS, save, REGS_SIZE);
}

static DWORD WINAPI audio_main(LPVOID p)
{
    (void)p;
    R32(REG_sp) = rt_new_stack(1u << 20);
    R32(REG_lr) = 0;
    g_gbuf = rt_alloc(SLICE * 4);
    while (!g_stop) {
#ifdef _WIN32
        if (!pump()) WaitForSingleObject(g_event, 20);
#else
        if (!pump()) usleep(3000);
#endif
    }
    return 0;
}

SYS(sound_init)
{
    uint32_t n = A32(1), out = A32(2);
    for (uint32_t i = 0; i < n; i++) M32(out + 4 * i) = i + 1;
    if (!g_lock_init) { InitializeCriticalSection(&g_lock); g_lock_init = 1; }
    g_mute = getenv("FTL_NOSOUND") != NULL;
#ifdef _WIN32
    WAVEFORMATEX f = { WAVE_FORMAT_PCM, 2, RATE, RATE * 4, 4, 16, 0 };
    g_event = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (waveOutOpen(&g_wo, WAVE_MAPPER, &f, (DWORD_PTR)g_event, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        fprintf(stderr, "son : peripherique audio indisponible, son desactive\n");
        RET32(0);
        return;
    }
#else
    if (!SDL_WasInit(SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "son : SDL audio indisponible (%s), son desactive\n", SDL_GetError());
        RET32(0);
        return;
    }
    SDL_AudioSpec want; SDL_zero(want);
    want.freq = RATE; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = SLICE; want.callback = NULL;
    g_dev = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
    if (!g_dev) {
        fprintf(stderr, "son : peripherique audio indisponible (%s), son desactive\n", SDL_GetError());
        RET32(0);
        return;
    }
#endif
    if (!gcall(S_sound_mixer_init, n, RATE, 0, 0)) { RET32(0); return; }
    g_ready = 1;
#ifdef _WIN32
    for (int i = 0; i < NBUF; i++) {
        g_hdr[i].lpData = (LPSTR)g_pcm[i];
        g_hdr[i].dwBufferLength = SLICE * 4;
        waveOutPrepareHeader(g_wo, &g_hdr[i], sizeof g_hdr[i]);
        g_hdr[i].dwFlags |= WHDR_DONE;
    }
#else
    SDL_PauseAudioDevice(g_dev, 0);
#endif
    g_mainmode = getenv("FTL_AUDIO_MAIN") != NULL;
    if (g_mainmode) g_gbuf = rt_alloc(SLICE * 4);
    else g_thread = CreateThread(NULL, 16u << 20, audio_main, NULL, 0, NULL);
    RET32(n);
}
SYS(sound_playback_rate) { RET32(g_ready ? RATE : 0); }
SYS(sound_check_headphone_disconnect) { RET32(0); }
SYS(sound_acknowledge_headphone_disconnect) { }
SYS(sound_cleanup)
{
    if (!g_ready) return;
    g_stop = 1;
#ifdef _WIN32
    SetEvent(g_event);
#endif
    if (g_thread) WaitForSingleObject(g_thread, 2000);
#ifdef _WIN32
    waveOutReset(g_wo);
    for (int i = 0; i < NBUF; i++) waveOutUnprepareHeader(g_wo, &g_hdr[i], sizeof g_hdr[i]);
    waveOutClose(g_wo);
#else
    SDL_CloseAudioDevice(g_dev);
#endif
    g_ready = 0;
    gcall(S_sound_mixer_cleanup, 0, 0, 0, 0);
}

#define LOCKED(call) do { EnterCriticalSection(&g_lock); call; LeaveCriticalSection(&g_lock); } while (0)

SYS(sound_setdata)
{
    if (!g_ready) { RET32(1); return; }
    uint32_t a = A32(0), b = A32(1), c = A32(2), d = A32(3), r = 0;
    LOCKED(r = gcall(S_sound_mixer_setdata, a, b, c, d));
    RET32(r);
}
SYS(sound_setvol) { if (!g_ready) return; uint32_t a = A32(0), b = A32(1);
    LOCKED(gcall(S_sound_mixer_setvol, a, b, 0, 0)); }
SYS(sound_setpan) { if (!g_ready) return; uint32_t a = A32(0), b = A32(1);
    LOCKED(gcall(S_sound_mixer_setpan, a, b, 0, 0)); }
SYS(sound_start) { if (!g_ready) return; uint32_t a = A32(0), b = A32(1);
    LOCKED(gcall(S_sound_mixer_start, a, b, 0, 0)); }
SYS(sound_stop) { if (!g_ready) return; uint32_t a = A32(0), b = A32(1);
    LOCKED(gcall(S_sound_mixer_stop, a, b, 0, 0)); }
SYS(sound_reset) { if (!g_ready) return; uint32_t a = A32(0), b = A32(1);
    LOCKED(gcall(S_sound_mixer_reset, a, b, 0, 0)); }
SYS(sound_setfade)
{
    /* (channel, float target, float length, int cut) : 4 registres, softfp */
    if (!g_ready) return;
    uint32_t a = A32(0), b = A32(1), c = A32(2), d = A32(3);
    LOCKED(gcall(S_sound_mixer_setfade, a, b, c, d));
}
SYS(sound_status)
{
    if (!g_ready) { RET32(0); return; }
    uint32_t a = A32(0), r = 0;
    LOCKED(r = gcall(S_sound_mixer_status, a, 0, 0, 0));
    RET32(r);
}
