/* Android : premiere copie des donnees du jeu hors de l'APK (assets) vers le stockage interne de l'appli, puis dossier de travail.
 * L'APK contient : assets/ftl.dat, assets/genj/*, assets/override/*, assets/manifest.txt (lignes "chemin taille") et assets/build.id.
 * La copie n'est refaite que si build.id change (nouvel APK). Le jeu lit ensuite ses fichiers normalement (fopen, dossier courant). */
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

void host_progress(float p);

/* Sous Android stdout/stderr vont a /dev/null : on les envoie a logcat (etiquette "FTL") pour que adb logcat -s FTL montre tout ce que dit le jeu. */
#include <android/log.h>
#include <pthread.h>
static int g_logpipe[2];
static void *log_thread(void *arg)
{
    (void)arg;
    char buf[1024]; size_t n = 0; char c;
    while (read(g_logpipe[0], &c, 1) == 1) {
        if (c == '\n' || n >= sizeof buf - 1) { buf[n] = 0; __android_log_write(ANDROID_LOG_INFO, "FTL", buf); n = 0; }
        else buf[n++] = c;
    }
    return NULL;
}
void android_log_init(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0); setvbuf(stderr, NULL, _IONBF, 0);
    if (pipe(g_logpipe) != 0) return;
    dup2(g_logpipe[1], 1); dup2(g_logpipe[1], 2);
    pthread_t t; if (pthread_create(&t, NULL, log_thread, NULL) == 0) pthread_detach(t);
}

static char *read_asset(const char *name, size_t *n)
{
    SDL_RWops *rw = SDL_RWFromFile(name, "rb");
    if (!rw) return NULL;
    Sint64 sz = SDL_RWsize(rw);
    if (sz < 0 || sz > (64 << 20)) { SDL_RWclose(rw); return NULL; }
    char *b = malloc((size_t)sz + 1);
    size_t got = b ? SDL_RWread(rw, b, 1, (size_t)sz) : 0;
    SDL_RWclose(rw);
    if (!b) return NULL;
    b[got] = 0; if (n) *n = got;
    return b;
}

static void make_dirs(char *path)               /* cree les dossiers de "a/b/c.ext" (pas le fichier) */
{
    for (char *p = path + 1; *p; p++)
        if (*p == '/') { *p = 0; mkdir(path, 0700); *p = '/'; }
}

int android_prepare_data(void)
{
    const char *dst = SDL_AndroidGetInternalStoragePath();
    if (!dst || chdir(dst) != 0) { fprintf(stderr, "stockage interne indisponible\n"); return 0; }
    fprintf(stderr, "dossier de travail : %s\n", dst);
    char *want = read_asset("build.id", NULL);
    if (!want) { fprintf(stderr, "APK incomplet : assets/build.id manquant\n"); return 0; }
    for (char *c = want; *c; c++) if (*c == '\n' || *c == '\r') *c = 0;
    char have[160] = "";
    FILE *f = fopen(".ready", "rb");
    if (f) { size_t k = fread(have, 1, sizeof have - 1, f); have[k] = 0; fclose(f); }
    if (strcmp(have, want) == 0 && access("ftl.dat", R_OK) == 0) { free(want); return 1; }       /* deja copie */

    char *man = read_asset("manifest.txt", NULL);
    if (!man) { fprintf(stderr, "APK incomplet : assets/manifest.txt manquant\n"); free(want); return 0; }
    unlink(".ready");
    double total = 0, done = 0;
    for (char *l = man; l && *l; ) { char *e = strchr(l, '\n'); char *sp = strrchr(l, ' '); if (sp && (!e || sp < e)) total += atof(sp + 1); l = e ? e + 1 : NULL; }
    if (total < 1) total = 1;
    host_progress(0);
    Uint32 last = 0;
    for (char *l = man; l && *l; ) {
        char *e = strchr(l, '\n'); if (e) *e = 0;
        char *sp = strrchr(l, ' ');
        if (sp) {
            *sp = 0;
            char path[512]; snprintf(path, sizeof path, "%s", l);
            if (strstr(path, "..") || path[0] == '/') { fprintf(stderr, "chemin refuse : %s\n", path); free(man); free(want); return 0; }
            SDL_RWops *in = SDL_RWFromFile(path, "rb");
            char tmp[520]; snprintf(tmp, sizeof tmp, "%s", path); make_dirs(tmp);
            FILE *out = in ? fopen(path, "wb") : NULL;
            if (!in || !out) { fprintf(stderr, "copie impossible : %s\n", path); if (in) SDL_RWclose(in); if (out) fclose(out); free(man); free(want); return 0; }
            static char buf[1 << 20]; size_t n;
            while ((n = SDL_RWread(in, buf, 1, sizeof buf)) > 0) {
                if (fwrite(buf, 1, n, out) != n) { fprintf(stderr, "ecriture impossible : %s (stockage plein ?)\n", path); SDL_RWclose(in); fclose(out); free(man); free(want); return 0; }
                done += (double)n;
                Uint32 now = SDL_GetTicks();
                if (now - last > 50) { host_progress((float)(done / total)); last = now; SDL_PumpEvents(); }
            }
            SDL_RWclose(in); fclose(out);
        }
        l = e ? e + 1 : NULL;
    }
    host_progress(1.0f);
    f = fopen(".ready", "wb"); if (f) { fputs(want, f); fclose(f); }
    free(man); free(want);
    return 1;
}
