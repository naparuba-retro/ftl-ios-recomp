/* Fenetre Windows + contexte OpenGL (compatibilite) et implementation hote des appels
 * OpenGL ES 2 du jeu (les fonctions gl* importees par le code iOS recompile).
 * Les pointeurs passes par le jeu sont des adresses de la memoire invitee : traduits en MEM+adresse,
 * sauf quand un buffer (VBO/IBO) est lie, auquel cas ce sont des decalages. */
#include "plat.h"
#ifdef _WIN32
#include <GL/gl.h>
#elif defined(__ANDROID__)
#include <SDL2/SDL.h>
#include <GLES3/gl3.h>         /* Android : OpenGL ES 3.0 natif (le jeu est en ES 2 : plus de traduction vers OpenGL de bureau) */
#ifndef APIENTRY
#define APIENTRY
#endif
#else
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>   /* declarations OpenGL fournies par SDL2 : pas besoin de libgl-dev */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rt.h"

void pvrtc_decode_level(const uint8_t *data, int w, int h, int bpp, uint8_t *out);

extern volatile int g_frames;
extern volatile LONG g_quit;

/* ---------------- fonctions OpenGL >= 1.2 chargees dynamiquement ---------------- */
typedef ptrdiff_t GLsz;
#define GLFUNCS(X) \
    X(void, glAttachShader, (GLuint, GLuint)) \
    X(void, glBindAttribLocation, (GLuint, GLuint, const char *)) \
    X(void, glBindBuffer, (GLenum, GLuint)) \
    X(void, glBindFramebuffer, (GLenum, GLuint)) \
    X(void, glBindRenderbuffer, (GLenum, GLuint)) \
    X(void, glBindVertexArray, (GLuint)) \
    X(void, glBlitFramebuffer, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum)) \
    X(void, glBlendColor, (float, float, float, float)) \
    X(void, glBlendEquation, (GLenum)) \
    X(void, glBufferData, (GLenum, GLsz, const void *, GLenum)) \
    X(void, glBufferSubData, (GLenum, GLsz, GLsz, const void *)) \
    X(GLenum, glCheckFramebufferStatus, (GLenum)) \
    X(void, glCompileShader, (GLuint)) \
    X(GLuint, glCreateProgram, (void)) \
    X(GLuint, glCreateShader, (GLenum)) \
    X(void, glDeleteBuffers, (GLsizei, const GLuint *)) \
    X(void, glDeleteFramebuffers, (GLsizei, const GLuint *)) \
    X(void, glDeleteProgram, (GLuint)) \
    X(void, glDeleteRenderbuffers, (GLsizei, const GLuint *)) \
    X(void, glDeleteShader, (GLuint)) \
    X(void, glDeleteVertexArrays, (GLsizei, const GLuint *)) \
    X(void, glDisableVertexAttribArray, (GLuint)) \
    X(void, glEnableVertexAttribArray, (GLuint)) \
    X(void, glFramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint)) \
    X(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) \
    X(void, glGenBuffers, (GLsizei, GLuint *)) \
    X(void, glGenFramebuffers, (GLsizei, GLuint *)) \
    X(void, glGenRenderbuffers, (GLsizei, GLuint *)) \
    X(void, glGenVertexArrays, (GLsizei, GLuint *)) \
    X(void, glGenerateMipmap, (GLenum)) \
    X(void, glGetProgramiv, (GLuint, GLenum, GLint *)) \
    X(void, glGetShaderiv, (GLuint, GLenum, GLint *)) \
    X(void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei *, char *)) \
    X(void, glGetProgramInfoLog, (GLuint, GLsizei, GLsizei *, char *)) \
    X(GLint, glGetUniformLocation, (GLuint, const char *)) \
    X(void, glLinkProgram, (GLuint)) \
    X(void, glRenderbufferStorage, (GLenum, GLenum, GLsizei, GLsizei)) \
    X(void, glShaderSource, (GLuint, GLsizei, const char *const *, const GLint *)) \
    X(void, glUniform1f, (GLint, float)) \
    X(void, glUniform1i, (GLint, GLint)) \
    X(void, glUniform2f, (GLint, float, float)) \
    X(void, glUniform4fv, (GLint, GLsizei, const float *)) \
    X(void, glUniformMatrix4fv, (GLint, GLsizei, GLboolean, const float *)) \
    X(void, glUseProgram, (GLuint)) \
    X(void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *)) \
    X(BOOL, wglSwapIntervalEXT, (int))

#define DECL(r, n, a) typedef r(APIENTRY *PFN_##n) a; static PFN_##n p_##n;
GLFUNCS(DECL)
#undef DECL

#ifdef _WIN32
static HWND g_hwnd;
static HDC g_dc;
static HGLRC g_rc;
#endif
static int g_gl_debug, g_gl_trace;
#define GT(...) do { if (g_gl_trace) { fprintf(stderr, __VA_ARGS__); } } while (0)
static GLuint g_array_buf, g_elem_buf;
static int g_draws, g_texup, g_clears;
static GLint g_vp[4];

static void gl_check(const char *where)
{
    if (!g_gl_debug) return;
    GLenum e = glGetError();
    if (e) { fprintf(stderr, "[GL erreur 0x%04x apres %s]\n", e, where); fflush(stderr); }
}
#define CHK() gl_check(__func__)

/* ---------------- fenetre ---------------- */
#ifdef _WIN32
/* Le jeu dessine toujours en 1024x768. En plein ecran l'image est recopiee centree, proportions gardees (bandes noires) :
 * g_dst_* dit ou elle tombe dans la fenetre (par defaut tout : 0,0,1024,768, comme avant). */
static int g_win_w = 1024, g_win_h = 768, g_dst_x, g_dst_y, g_dst_w = 1024, g_dst_h = 768;
static GLuint g_fbo_main, g_fbo_tex, g_fbo_rb;
static int g_scale;                              /* 1 : rendu dans le FBO puis recopie a l'echelle */
static void write_png(const char *path, const uint8_t *rgba, int w, int h);
#endif
/* file d'evenements souris -> tactile (lue par sys_input_update dans le fil principal) */
typedef struct { int type, detail; float x, y; int val; } HostTouch;
static HostTouch tq[256];
static int tq_head, tq_tail;
static int touch_down;
static void tq_push(int detail, LPARAM l)
{
#ifdef _WIN32
    float x = ((float)(short)LOWORD(l) - (float)g_dst_x) / (float)g_dst_w, y = ((float)(short)HIWORD(l) - (float)g_dst_y) / (float)g_dst_h;
#else
    float x = (float)(short)LOWORD(l) / 1024.0f, y = (float)(short)HIWORD(l) / 768.0f;
#endif
    if (x < 0) x = 0; if (x > 1) x = 1;
    if (y < 0) y = 0; if (y > 1) y = 1;
    int n = (tq_head + 1) & 255;
    if (n == tq_tail) return;
    tq[tq_head].type = 6; tq[tq_head].detail = detail; tq[tq_head].x = x; tq[tq_head].y = y; tq[tq_head].val = 0;
    tq_head = n;
}
/* evenements clavier (type 2) et texte (type 5) : INPUT_EVENT_KEYBOARD / INPUT_EVENT_TEXT de SIL */
volatile int g_text_mode;
static void eq_push(int type, int detail, int val)
{
    int n = (tq_head + 1) & 255;
    if (n == tq_tail) return;
    tq[tq_head].type = type; tq[tq_head].detail = detail; tq[tq_head].x = tq[tq_head].y = 0; tq[tq_head].val = val;
    tq_head = n;
}
/* Le jeu range les noms dans un std::string d'octets : un caractere non ASCII est donc envoye en UTF-8, octet par octet.
 * Un miroir du texte (octets + curseur) permet de traiter un caractere entier pour Retour arriere / Suppr / fleches. */
static unsigned char sh[512]; static int sh_len, sh_pos;
static int sh_prev(int p) { if (p <= 0) return 0; p--; while (p > 0 && (sh[p] & 0xC0) == 0x80) p--; return p; }
static int sh_next(int p) { if (p >= sh_len) return sh_len; p++; while (p < sh_len && (sh[p] & 0xC0) == 0x80) p++; return p; }
static void text_input_cp(int cp)
{
    unsigned char b[4]; int n;
    if (cp < 0x80) { b[0] = (unsigned char)cp; n = 1; }
    else if (cp < 0x800) { b[0] = (unsigned char)(0xC0 | (cp >> 6)); b[1] = (unsigned char)(0x80 | (cp & 0x3F)); n = 2; }
    else if (cp < 0x10000) { b[0] = (unsigned char)(0xE0 | (cp >> 12)); b[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F)); b[2] = (unsigned char)(0x80 | (cp & 0x3F)); n = 3; }
    else { b[0] = (unsigned char)(0xF0 | (cp >> 18)); b[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F)); b[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F)); b[3] = (unsigned char)(0x80 | (cp & 0x3F)); n = 4; }
    if (sh_len + n < (int)sizeof sh) { memmove(sh + sh_pos + n, sh + sh_pos, (size_t)(sh_len - sh_pos)); memcpy(sh + sh_pos, b, (size_t)n); sh_len += n; sh_pos += n; }
    for (int i = 0; i < n; i++) eq_push(5, 19, b[i]);
}
static void text_command(int detail)       /* 20 DONE, 21 CANCELLED, 22 CLEAR, 23 BACKSPACE, 24 DELETE, 25 GAUCHE, 26 DROITE, 27 DEBUT, 28 FIN */
{
    int p, n, i;
    switch (detail) {
    case 22: sh_len = sh_pos = 0; eq_push(5, 22, 0); break;
    case 23: p = sh_prev(sh_pos); n = sh_pos - p; if (n) { memmove(sh + p, sh + sh_pos, (size_t)(sh_len - sh_pos)); sh_len -= n; sh_pos = p; for (i = 0; i < n; i++) eq_push(5, 23, 0); } break;
    case 24: p = sh_next(sh_pos); n = p - sh_pos; if (n) { memmove(sh + sh_pos, sh + p, (size_t)(sh_len - p)); sh_len -= n; for (i = 0; i < n; i++) eq_push(5, 24, 0); } break;
    case 25: p = sh_prev(sh_pos); n = sh_pos - p; sh_pos = p; for (i = 0; i < n; i++) eq_push(5, 25, 0); break;
    case 26: p = sh_next(sh_pos); n = p - sh_pos; sh_pos = p; for (i = 0; i < n; i++) eq_push(5, 26, 0); break;
    case 27: sh_pos = 0; eq_push(5, 27, 0); break;
    case 28: sh_pos = sh_len; eq_push(5, 28, 0); break;
    default: eq_push(5, detail, 0); break;
    }
}
/* debut de saisie : texte par defaut en UTF-8 (CLEAR puis octets) */
void host_text_begin(const char *def)
{
#ifdef __ANDROID__
    SDL_StartTextInput();
#endif
    g_text_mode = 1; sh_len = sh_pos = 0; eq_push(5, 22, 0);
    for (const unsigned char *c = (const unsigned char *)(def ? def : ""); *c && sh_len < (int)sizeof sh - 1; c++) { sh[sh_len++] = *c; eq_push(5, 19, *c); }
    sh_pos = sh_len;
}
void host_text_mode(int on)
{
    g_text_mode = on;
#ifdef __ANDROID__
    if (!on) SDL_StopTextInput();
#endif
}

#ifdef _WIN32
/* touches virtuelles -> codes de touches SIL (keycodes.h) */
static int vk_to_key(WPARAM w)
{
    if (w >= 'A' && w <= 'Z') return (int)(w - 'A') + 1;
    if (w >= '0' && w <= '9') return (int)(w - '0') + 27;
    if (w >= VK_F1 && w <= VK_F12) return (int)(w - VK_F1) + 101;
    switch (w) {
    case VK_TAB: return 37; case VK_RETURN: return 38; case VK_SPACE: return 39; case VK_BACK: return 88;
    case VK_DELETE: return 91; case VK_HOME: return 92; case VK_END: return 93;
    case VK_UP: return 96; case VK_DOWN: return 97; case VK_LEFT: return 98; case VK_RIGHT: return 99; case VK_ESCAPE: return 100;
    }
    return 0;
}
#endif
void host_drain_events(void) { tq_tail = tq_head; }
int host_next_event(int *type, int *detail, float *x, float *y, int *val)
{
    if (tq_tail == tq_head) return 0;
    *type = tq[tq_tail].type; *detail = tq[tq_tail].detail; *x = tq[tq_tail].x; *y = tq[tq_tail].y; *val = tq[tq_tail].val;
    tq_tail = (tq_tail + 1) & 255;
    return 1;
}

#ifdef _WIN32
static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_LBUTTONDOWN: SetCapture(h); touch_down = 1; tq_push(29, l); return 0;     /* INPUT_TOUCH_DOWN */
    case WM_MOUSEMOVE: if (touch_down) tq_push(30, l); return 0;                       /* INPUT_TOUCH_MOVE */
    case WM_LBUTTONUP: if (touch_down) { touch_down = 0; tq_push(31, l); ReleaseCapture(); } return 0; /* INPUT_TOUCH_UP */
    case WM_CHAR:
        if (g_text_mode && w >= 32 && w != 127) {   /* INPUT_TEXT_INPUT ; paires de substitution UTF-16 recombinees */
            static unsigned hi;
            if (w >= 0xD800 && w < 0xDC00) hi = (unsigned)w;
            else if (w >= 0xDC00 && w < 0xE000) { if (hi) text_input_cp((int)(0x10000 + ((hi - 0xD800) << 10) + ((unsigned)w - 0xDC00))); hi = 0; }
            else text_input_cp((int)w);
        }
        return 0;
    case WM_KEYDOWN: case WM_SYSKEYDOWN:
        if (m == WM_SYSKEYDOWN && w == VK_F4) { InterlockedExchange(&g_quit, 1); return 0; }
        if (g_text_mode) {
            switch (w) {
            case VK_BACK: text_command(23); break;  case VK_DELETE: text_command(24); break;
            case VK_LEFT: text_command(25); break;  case VK_RIGHT: text_command(26); break;
            case VK_HOME: text_command(27); break;  case VK_END: text_command(28); break;
            case VK_RETURN: text_command(20); break;  /* INPUT_TEXT_DONE */
            case VK_ESCAPE: text_command(21); break;  /* INPUT_TEXT_CANCELLED */
            }
        } else { int k = vk_to_key(w); if (k && !(l & 0x40000000)) eq_push(2, 5, k); }   /* INPUT_KEYBOARD_KEY_DOWN (sans repetition) */
        return 0;
    case WM_KEYUP: case WM_SYSKEYUP:
        if (!g_text_mode) { int k = vk_to_key(w); if (k) eq_push(2, 6, k); }
        return 0;
    case WM_CLOSE: InterlockedExchange(&g_quit, 1); return 0;
    case WM_DESTROY: return 0;
    }
    return DefWindowProc(h, m, w, l);
}

void host_pump(void)
{
    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

int host_window_init(int width, int height)
{
    g_gl_debug = getenv("FTL_GLDEBUG") != NULL;
    g_gl_trace = getenv("FTL_GLTRACE") != NULL;
    HINSTANCE hi = GetModuleHandle(NULL);
    WNDCLASS wc = {0};
    wc.lpfnWndProc = wndproc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "FTLPC";
    RegisterClass(&wc);
    RECT r = { 0, 0, width, height };
    AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    int full = getenv("FTL_FULLSCREEN") != NULL && getenv("FTL_WINDOWED") == NULL;
    if (full) {
        SetProcessDPIAware();     /* sans cela Windows etire une fenetre "1920x1080" sur un ecran mis a l'echelle : image floue */
        g_hwnd = CreateWindow("FTLPC", "FTL (iPad, recompile)", WS_POPUP | WS_VISIBLE, 0, 0,
                              GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), NULL, NULL, hi, NULL);
    } else {
        g_hwnd = CreateWindow("FTLPC", "FTL (iPad, recompile)", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE,
                              CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, NULL, NULL, hi, NULL);
    }
    if (!g_hwnd) return 0;
    g_dc = GetDC(g_hwnd);
    PIXELFORMATDESCRIPTOR pfd = {0};
    pfd.nSize = sizeof pfd;
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    int pf = ChoosePixelFormat(g_dc, &pfd);
    if (!pf || !SetPixelFormat(g_dc, pf, &pfd)) return 0;
    g_rc = wglCreateContext(g_dc);
    if (!g_rc || !wglMakeCurrent(g_dc, g_rc)) return 0;
#define LOAD(r_, n, a) p_##n = (PFN_##n)wglGetProcAddress(#n);
    GLFUNCS(LOAD)
#undef LOAD
    if (p_wglSwapIntervalEXT) p_wglSwapIntervalEXT(getenv("FTL_NOVSYNC") ? 0 : 1);
    {   /* emplacement de l'image 1024x768 dans la zone cliente */
        RECT cr; GetClientRect(g_hwnd, &cr);
        int dw = cr.right > 0 ? cr.right : width, dh = cr.bottom > 0 ? cr.bottom : height;
        double sc = (double)dw / 1024.0; if ((double)dh / 768.0 < sc) sc = (double)dh / 768.0;
        g_win_w = dw; g_win_h = dh;
        g_dst_w = (int)(1024.0 * sc); g_dst_h = (int)(768.0 * sc);
        g_dst_x = (dw - g_dst_w) / 2; g_dst_y = (dh - g_dst_h) / 2;
        g_scale = (g_dst_w != 1024 || g_dst_h != 768 || g_dst_x || g_dst_y);
    }
    if (g_scale) {      /* FBO de rendu 1024x768 : le jeu dessine dedans, puis il est recopie a l'echelle */
        glGenTextures(1, &g_fbo_tex);
        glBindTexture(GL_TEXTURE_2D, g_fbo_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1024, 768, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);
        p_glGenRenderbuffers(1, &g_fbo_rb);
        p_glBindRenderbuffer(0x8D41, g_fbo_rb);
        p_glRenderbufferStorage(0x8D41, 0x88F0 /* DEPTH24_STENCIL8 */, 1024, 768);
        p_glGenFramebuffers(1, &g_fbo_main);
        p_glBindFramebuffer(0x8D40, g_fbo_main);
        p_glFramebufferTexture2D(0x8D40, 0x8CE0, GL_TEXTURE_2D, g_fbo_tex, 0);
        p_glFramebufferRenderbuffer(0x8D40, 0x821A /* DEPTH_STENCIL_ATTACHMENT */, 0x8D41, g_fbo_rb);
    }
    fprintf(stderr, "OpenGL hote : %s | %s%s\n", (const char *)glGetString(GL_RENDERER), (const char *)glGetString(GL_VERSION), g_scale ? " | mise a l'echelle" : "");
    return 1;
}
/* le jeu "lie" le framebuffer 0 : on le redirige vers le FBO quand la fenetre n'est pas en 1024x768 */
static GLuint host_fb(GLuint fb) { return (fb == 0 && g_scale) ? g_fbo_main : fb; }
static void host_swap(void)
{
    if (g_scale) {
        GLint saved = 0; glGetIntegerv(0x8CA6, &saved);
        p_glBindFramebuffer(0x8CA8 /* READ */, g_fbo_main);
        p_glBindFramebuffer(0x8CA9 /* DRAW */, 0);
        GLboolean sc = glIsEnabled(GL_SCISSOR_TEST); GLfloat cc[4]; glGetFloatv(GL_COLOR_CLEAR_VALUE, cc);
        glViewport(0, 0, g_win_w, g_win_h);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
        p_glBlitFramebuffer(0, 0, 1024, 768, g_dst_x, g_dst_y, g_dst_x + g_dst_w, g_dst_y + g_dst_h, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        { static int ws = -2; if (ws == -2) { const char *e = getenv("FTL_WINSHOT"); ws = e ? atoi(e) : -1; }
          if (ws >= 0 && g_frames == ws) { uint8_t *px = malloc((size_t)g_win_w * g_win_h * 4); p_glBindFramebuffer(0x8CA8, 0); glReadBuffer(GL_BACK); glReadPixels(0, 0, g_win_w, g_win_h, GL_RGBA, GL_UNSIGNED_BYTE, px);
            for (int i = 3; i < g_win_w * g_win_h * 4; i += 4) px[i] = 255; write_png("winshot.png", px, g_win_w, g_win_h); free(px); fprintf(stderr, "[capture fenetre winshot.png %dx%d]\n", g_win_w, g_win_h); } }
        SwapBuffers(g_dc);
        p_glBindFramebuffer(0x8D40, (GLuint)saved);
        glViewport(g_vp[0], g_vp[1], g_vp[2], g_vp[3]);
        if (sc) glEnable(GL_SCISSOR_TEST);
        glClearColor(cc[0], cc[1], cc[2], cc[3]);
    } else {
        SwapBuffers(g_dc);
    }
}
#else
#include "gl_sdl.inc"
#endif

/* ---------------- hooks iOS : presentation, synchro verticale, dimensions ---------------- */
#define IOS(name) void ext__ios_##name(void)
static LARGE_INTEGER t0, tfreq;
IOS(display_width) { RET32(1024); }
IOS(display_height) { RET32(768); }
/* ---- capture d'ecran (FTL_SHOT=<numero d'image>) : ecrit shot_<n>.png ---- */
static uint32_t crc_tab[256];
static uint32_t crc32_update(uint32_t c, const uint8_t *p, size_t n)
{
    if (!crc_tab[1]) for (uint32_t i = 0; i < 256; i++) { uint32_t v = i; for (int k = 0; k < 8; k++) v = (v & 1) ? 0xEDB88320u ^ (v >> 1) : v >> 1; crc_tab[i] = v; }
    c = ~c;
    for (size_t i = 0; i < n; i++) c = crc_tab[(c ^ p[i]) & 0xff] ^ (c >> 8);
    return ~c;
}
static void be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static void png_chunk(FILE *f, const char *type, const uint8_t *data, uint32_t n)
{
    uint8_t h[8];
    be32(h, n);
    memcpy(h + 4, type, 4);
    fwrite(h, 1, 8, f);
    if (n) fwrite(data, 1, n, f);
    uint32_t c = crc32_update(0, (const uint8_t *)type, 4);
    c = crc32_update(c, data, n);
    uint8_t t[4];
    be32(t, c);
    fwrite(t, 1, 4, f);
}
static void write_png(const char *path, const uint8_t *rgba, int w, int h)
{
    size_t rawn = (size_t)(w * 4 + 1) * h;
    uint8_t *raw = malloc(rawn);
    for (int y = 0; y < h; y++) {                       /* GL : origine en bas */
        raw[(size_t)y * (w * 4 + 1)] = 0;
        memcpy(raw + (size_t)y * (w * 4 + 1) + 1, rgba + (size_t)(h - 1 - y) * w * 4, (size_t)w * 4);
    }
    size_t nblk = (rawn + 65534) / 65535;
    uint8_t *z = malloc(rawn + nblk * 5 + 6);
    size_t zi = 0;
    z[zi++] = 0x78; z[zi++] = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < rawn; i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    for (size_t off = 0; off < rawn; off += 65535) {
        size_t len = rawn - off < 65535 ? rawn - off : 65535;
        z[zi++] = (off + len >= rawn);
        z[zi++] = (uint8_t)len; z[zi++] = (uint8_t)(len >> 8);
        z[zi++] = (uint8_t)~len; z[zi++] = (uint8_t)(~len >> 8);
        memcpy(z + zi, raw + off, len);
        zi += len;
    }
    be32(z + zi, (b << 16) | a);
    zi += 4;
    FILE *f = fopen(path, "wb");
    if (f) {
        static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
        fwrite(sig, 1, 8, f);
        uint8_t ihdr[13];
        be32(ihdr, (uint32_t)w); be32(ihdr + 4, (uint32_t)h);
        ihdr[8] = 8; ihdr[9] = 6; ihdr[10] = ihdr[11] = ihdr[12] = 0;
        png_chunk(f, "IHDR", ihdr, 13);
        png_chunk(f, "IDAT", z, (uint32_t)zi);
        png_chunk(f, "IEND", NULL, 0);
        fclose(f);
    }
    free(raw); free(z);
}

IOS(present_view)
{
    /* clic synthetique pour les tests : FTL_CLICK=image,x,y (appui a l'image, relache a la suivante) */
    static int cl[128][3], ncl = -1;
    if (ncl < 0) {
        ncl = 0;
        const char *e = getenv("FTL_CLICK");   /* "image,x,y;image,x,y;..." */
        while (e && *e && ncl < 128) {
            if (sscanf(e, "%d,%d,%d", &cl[ncl][0], &cl[ncl][1], &cl[ncl][2]) != 3) break;
            ncl++;
            e = strchr(e, ';');
            if (e) e++;
        }
    }
    for (int i = 0; i < ncl; i++)
        if (g_frames == cl[i][0] || g_frames == cl[i][0] + 1)
            tq_push(g_frames == cl[i][0] ? 29 : 31, MAKELPARAM(cl[i][1], cl[i][2]));
    {   /* saisie scriptee (tests) : FTL_TYPE="image:texte;image:texte" ; FTL_SPECIAL="image:done|cancel|clear|back|left|right|home|end|del;..." */
        static struct { int frame; char *text; int pos; } ty[8]; static struct { int frame; int cmd; int done; } sp[16]; static int nty = -1, nsp;
        if (nty < 0) {
            nty = 0;
            const char *e = getenv("FTL_TYPE");
            while (e && *e && nty < 8) { ty[nty].frame = atoi(e); const char *c = strchr(e, ':'); if (!c) break; c++; const char *sc = strchr(c, ';'); size_t L = sc ? (size_t)(sc - c) : strlen(c); ty[nty].text = malloc(L + 1); memcpy(ty[nty].text, c, L); ty[nty].text[L] = 0; nty++; e = sc ? sc + 1 : NULL; }
            e = getenv("FTL_SPECIAL");
            while (e && *e && nsp < 16) {
                sp[nsp].frame = atoi(e); const char *c = strchr(e, ':'); if (!c) break; c++; const char *sc = strchr(c, ';'); size_t L = sc ? (size_t)(sc - c) : strlen(c);
                static const char *nm[] = { "done", "cancel", "clear", "back", "del", "left", "right", "home", "end" }; static const int cd[] = { 20, 21, 22, 23, 24, 25, 26, 27, 28 };
                sp[nsp].cmd = 0; for (int k = 0; k < 9; k++) if (strlen(nm[k]) == L && !strncmp(c, nm[k], L)) sp[nsp].cmd = cd[k];
                nsp++; e = sc ? sc + 1 : NULL;
            }
        }
        for (int i = 0; i < nty; i++) if (g_frames >= ty[i].frame && ty[i].text[ty[i].pos]) {   /* UTF-8 -> point de code, un par image */
            const unsigned char *u = (const unsigned char *)ty[i].text + ty[i].pos; int cp, n = 1;
            if (u[0] < 0x80) cp = u[0];
            else if ((u[0] & 0xE0) == 0xC0 && (u[1] & 0xC0) == 0x80) { cp = ((u[0] & 0x1F) << 6) | (u[1] & 0x3F); n = 2; }
            else if ((u[0] & 0xF0) == 0xE0 && (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80) { cp = ((u[0] & 0x0F) << 12) | ((u[1] & 0x3F) << 6) | (u[2] & 0x3F); n = 3; }
            else if ((u[0] & 0xF8) == 0xF0) { cp = ((u[0] & 0x07) << 18) | ((u[1] & 0x3F) << 12) | ((u[2] & 0x3F) << 6) | (u[3] & 0x3F); n = 4; }
            else cp = u[0];
            ty[i].pos += n; text_input_cp(cp);
        }
        for (int i = 0; i < nsp; i++) if (!sp[i].done && g_frames >= sp[i].frame && sp[i].cmd) { sp[i].done = 1; text_command(sp[i].cmd); }
    }
    static int shots[16], nshots = -1; int shot = 0;
    if (nshots < 0) { nshots = 0; const char *e = getenv("FTL_SHOT"); while (e && *e && nshots < 16) { shots[nshots++] = atoi(e); while (*e && *e != ',') e++; if (*e == ',') e++; } }
    for (int i = 0; i < nshots; i++) if (shots[i] > 0 && g_frames == shots[i] - 1) shot = shots[i];
    if (shot > 0) {
        uint8_t *buf = malloc(1024 * 768 * 4);
        glReadPixels(0, 0, 1024, 768, GL_RGBA, GL_UNSIGNED_BYTE, buf);
        for (int i = 3; i < 1024 * 768 * 4; i += 4) buf[i] = 255;
        char name[64];
        sprintf(name, "shot_%d.png", shot);
        write_png(name, buf, 1024, 768);
        free(buf);
        fprintf(stderr, "[capture %s]\n", name);
    }
    if (g_gl_debug && (g_frames % 30) == 0) {
        GLint prog = 0, fb = 0; glGetIntegerv(0x8B8D, &prog); glGetIntegerv(0x8CA6, &fb);
        fprintf(stderr, "[image %d : %d dessins, %d clear, viewport %d,%d %dx%d, programme %d, fbo %d]\n", g_frames, g_draws, g_clears, g_vp[0], g_vp[1], g_vp[2], g_vp[3], prog, fb);
    }
    g_draws = g_clears = 0;
    host_swap();
    g_frames++;
    { extern void rp_pace(void); rp_pace(); }
    /* FTL_FPS=1 : debit mesure (fenetres de 120 images : fps moyen, image la plus lente) */
    {
        static int on = -1, n; static LARGE_INTEGER freq, last, t0; static double worst;
        if (on < 0) { on = getenv("FTL_FPS") != NULL; QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&last); t0 = last; }
        if (on) {
            LARGE_INTEGER now; QueryPerformanceCounter(&now);
            double dt = (double)(now.QuadPart - last.QuadPart) * 1000.0 / (double)freq.QuadPart;
            last = now; if (dt > worst) worst = dt;
            if (++n == 120) {
                double tot = (double)(now.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart;
                fprintf(stderr, "[fps image %d : %.1f fps moyen, image la plus lente %.1f ms]\n", g_frames, 120.0 * 1000.0 / tot, worst);
                n = 0; worst = 0; t0 = now;
            }
        }
    }
    { extern void hle_sound_pump_main(void); hle_sound_pump_main(); }
    host_pump();
}
IOS(vsync) { host_pump(); Sleep(1); }
IOS(get_frame_counter)
{
    LARGE_INTEGER n;
    if (!tfreq.QuadPart) { QueryPerformanceFrequency(&tfreq); QueryPerformanceCounter(&t0); }
    QueryPerformanceCounter(&n);
    RET32((uint32_t)((double)(n.QuadPart - t0.QuadPart) * 60.0 / (double)tfreq.QuadPart));
}
IOS(get_frame_interval) { RET32(1); }
IOS(set_frame_interval) { }

/* ---------------- chaines guest ---------------- */
static uint32_t gstr(const char *s)
{
    uint32_t a = rt_alloc((uint32_t)strlen(s) + 1);
    strcpy(GSTR(a), s);
    return a;
}

EXT(_glGetString)
{
    static uint32_t cache[0x2000 - 0x1f00];
    uint32_t name = A32(0);
    if (name < 0x1f00 || name >= 0x2000) { RET32(0); return; }
    if (!cache[name - 0x1f00]) {
        const char *s = "";
        switch (name) {
        case 0x1f00: s = "FTL-PC"; break;
        case 0x1f01: s = "Host OpenGL (compat)"; break;
        case 0x1f02: s = "OpenGL ES 2.0 FTL-PC"; break;
        case 0x1f03: s = "GL_EXT_discard_framebuffer GL_OES_element_index_uint GL_EXT_texture_format_BGRA8888 "
                         "GL_IMG_texture_compression_pvrtc GL_OES_packed_depth_stencil GL_EXT_packed_depth_stencil"; break;
        case 0x8b8c: s = "OpenGL ES GLSL ES 1.00"; break;
        }
        cache[name - 0x1f00] = gstr(s);
    }
    RET32(cache[name - 0x1f00]);
}

/* ---------------- 1.1 : appels directs ---------------- */
EXT(_glBindTexture) { GT("glBindTexture %x %u\n", A32(0), A32(1)); glBindTexture(A32(0), A32(1)); CHK(); }
EXT(_glBlendFunc) { GT("glBlendFunc %x %x\n", A32(0), A32(1)); glBlendFunc(A32(0), A32(1)); CHK(); }
EXT(_glClear) { GT("glClear %x\n", A32(0)); g_clears++; glClear(A32(0)); CHK(); }
EXT(_glClearColor) { GT("glClearColor %f %f %f %f\n", AF(0), AF(1), AF(2), AF(3)); glClearColor(AF(0), AF(1), AF(2), AF(3)); CHK(); }
EXT(_glColorMask) { GT("glColorMask %d %d %d %d\n", (int)(A32(0)&0xff),(int)(A32(1)&0xff),(int)(A32(2)&0xff),(int)(A32(3)&0xff)); glColorMask(A32(0) & 0xff, A32(1) & 0xff, A32(2) & 0xff, A32(3) & 0xff); CHK(); }
EXT(_glCullFace) { GT("glCullFace %x\n", A32(0)); glCullFace(A32(0)); CHK(); }
EXT(_glDeleteTextures) { glDeleteTextures((GLsizei)A32(0), (const GLuint *)GP(A32(1))); CHK(); }
EXT(_glDepthFunc) { GT("glDepthFunc %x\n", A32(0)); glDepthFunc(A32(0)); CHK(); }
EXT(_glDepthMask) { GT("glDepthMask %d\n", (int)(A32(0)&0xff)); glDepthMask(A32(0) & 0xff); CHK(); }
#ifdef __ANDROID__
EXT(_glDepthRangef) { glDepthRangef(AF(0), AF(1)); CHK(); }
#else
EXT(_glDepthRangef) { glDepthRange(AF(0), AF(1)); CHK(); }
#endif
EXT(_glDisable) { GT("glDisable %x\n", A32(0)); glDisable(A32(0)); CHK(); }
EXT(_glEnable) { GT("glEnable %x\n", A32(0)); glEnable(A32(0)); CHK(); }
EXT(_glFinish) { glFinish(); }
EXT(_glFlush) { glFlush(); }
EXT(_glFrontFace) { glFrontFace(A32(0)); CHK(); }
EXT(_glGenTextures) { glGenTextures((GLsizei)A32(0), (GLuint *)GP(A32(1))); CHK(); }
EXT(_glLineWidth) { glLineWidth(AF(0)); CHK(); }
EXT(_glPixelStorei) { GT("glPixelStorei %x %d\n", A32(0), (int)A32(1)); glPixelStorei(A32(0), (GLint)A32(1)); CHK(); }
EXT(_glScissor) { GT("glScissor %d %d %d %d\n", (int)A32(0),(int)A32(1),(int)A32(2),(int)A32(3)); glScissor((GLint)A32(0), (GLint)A32(1), (GLsizei)A32(2), (GLsizei)A32(3)); CHK(); }
EXT(_glStencilFunc) { glStencilFunc(A32(0), (GLint)A32(1), A32(2)); CHK(); }
EXT(_glStencilOp) { glStencilOp(A32(0), A32(1), A32(2)); CHK(); }
EXT(_glViewport) { GT("glViewport %d %d %d %d\n", (int)A32(0),(int)A32(1),(int)A32(2),(int)A32(3)); g_vp[0] = (GLint)A32(0); g_vp[1] = (GLint)A32(1); g_vp[2] = (GLint)A32(2); g_vp[3] = (GLint)A32(3); glViewport((GLint)A32(0), (GLint)A32(1), (GLsizei)A32(2), (GLsizei)A32(3)); CHK(); }
EXT(_glTexParameteri) { GT("glTexParameteri %x %x %d\n", A32(0), A32(1), (int)A32(2)); glTexParameteri(A32(0), A32(1), (GLint)A32(2)); CHK(); }
EXT(_glReadPixels)
{
    glReadPixels((GLint)A32(0), (GLint)A32(1), (GLsizei)A32(2), (GLsizei)A32(3), A32(4), A32(5), GP(A32(6)));
    CHK();
}
EXT(_glDrawArrays) { GT("glDrawArrays mode=%x first=%d count=%d\n", A32(0), (int)A32(1), (int)A32(2)); g_draws++; glDrawArrays(A32(0), (GLint)A32(1), (GLsizei)A32(2)); CHK(); }
EXT(_glDrawElements)
{
    const void *idx = g_elem_buf ? (const void *)(uintptr_t)A32(3) : (const void *)GP(A32(3));
    GT("glDrawElements mode=%x count=%d type=%x idx=%08x ebo=%u abo=%u\n", A32(0), (int)A32(1), A32(2), A32(3), g_elem_buf, g_array_buf);
    g_draws++;
    glDrawElements(A32(0), (GLsizei)A32(1), A32(2), idx);
    CHK();
}

static GLint fix_internal(GLenum fmt)
{
    return fmt == 0x80E1 ? 0x8058 /* GL_BGRA -> GL_RGBA8 */ : (GLint)fmt;
}
#ifdef __ANDROID__
/* OpenGL ES 3.0 n'a pas GL_BGRA (format des textures iOS) : octets R et B echanges a l'envoi */
static uint8_t *bgra_to_rgba(const void *src, size_t w, size_t h)
{
    uint8_t *o = malloc(w * h * 4); const uint8_t *s = (const uint8_t *)src;
    if (!o) return NULL;
    for (size_t i = 0; i < w * h; i++) { o[4 * i] = s[4 * i + 2]; o[4 * i + 1] = s[4 * i + 1]; o[4 * i + 2] = s[4 * i]; o[4 * i + 3] = s[4 * i + 3]; }
    return o;
}
#endif
EXT(_glTexImage2D)
{
    const void *px = A32(8) ? GP(A32(8)) : NULL;
#ifdef __ANDROID__
    if (A32(6) == 0x80E1 && px) {
        uint8_t *t = bgra_to_rgba(px, A32(3), A32(4));
        glTexImage2D(A32(0), (GLint)A32(1), 0x8058, (GLsizei)A32(3), (GLsizei)A32(4), (GLint)A32(5), GL_RGBA, A32(7), t ? t : px);
        free(t); CHK(); return;
    }
    if (A32(6) == 0x80E1) { glTexImage2D(A32(0), (GLint)A32(1), 0x8058, (GLsizei)A32(3), (GLsizei)A32(4), (GLint)A32(5), GL_RGBA, A32(7), NULL); CHK(); return; }
#endif
    GT("glTexImage2D tgt=%x lvl=%d ifmt=%x %dx%d fmt=%x type=%x data=%d\n", A32(0), (int)A32(1), A32(2), (int)A32(3), (int)A32(4), A32(6), A32(7), px != NULL);
    if (g_gl_trace && px && A32(2) == 0x1906) { const uint8_t *q = (const uint8_t *)px; size_t nz = 0, n = (size_t)A32(3) * A32(4); for (size_t i = 0; i < n; i++) nz += q[i] != 0; fprintf(stderr, "  [alpha %ux%u octets non nuls : %zu]\n", A32(3), A32(4), nz); }
    glTexImage2D(A32(0), (GLint)A32(1), fix_internal(A32(2)), (GLsizei)A32(3), (GLsizei)A32(4), (GLint)A32(5), A32(6), A32(7), px);
    CHK();
}
EXT(_glTexSubImage2D)
{
#ifdef __ANDROID__
    if (A32(6) == 0x80E1) {
        uint8_t *t = bgra_to_rgba(GP(A32(8)), A32(4), A32(5));
        glTexSubImage2D(A32(0), (GLint)A32(1), (GLint)A32(2), (GLint)A32(3), (GLsizei)A32(4), (GLsizei)A32(5), GL_RGBA, A32(7), t ? t : GP(A32(8)));
        free(t); CHK(); return;
    }
#endif
    glTexSubImage2D(A32(0), (GLint)A32(1), (GLint)A32(2), (GLint)A32(3), (GLsizei)A32(4), (GLsizei)A32(5), A32(6), A32(7), GP(A32(8)));
    CHK();
}
EXT(_glCompressedTexImage2D)
{
    uint32_t target = A32(0), level = A32(1), ifmt = A32(2), w = A32(3), h = A32(4), border = A32(5), size = A32(6);
    (void)size;
    if (ifmt >= 0x8C00 && ifmt <= 0x8C03) {
        int bpp = (ifmt == 0x8C00 || ifmt == 0x8C02) ? 4 : 2;
        uint8_t *rgba = malloc((size_t)w * h * 4);
        pvrtc_decode_level((const uint8_t *)GP(A32(7)), (int)w, (int)h, bpp, rgba);
        glTexImage2D(target, (GLint)level, 0x8058, (GLsizei)w, (GLsizei)h, (GLint)border, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        free(rgba);
        CHK();
    } else {
        fprintf(stderr, "glCompressedTexImage2D : format 0x%x non pris en charge\n", ifmt);
    }
}

/* ---------------- >= 1.2 / extensions ---------------- */
EXT(_glAttachShader) { p_glAttachShader(A32(0), A32(1)); CHK(); }
EXT(_glBindAttribLocation) { GT("glBindAttribLocation prog=%u idx=%u name=%s\n", A32(0), A32(1), GSTR(A32(2))); p_glBindAttribLocation(A32(0), A32(1), GSTR(A32(2))); CHK(); }
EXT(_glBindBuffer)
{
    uint32_t t = A32(0), b = A32(1);
    if (t == 0x8892) g_array_buf = b; else if (t == 0x8893) g_elem_buf = b;
    p_glBindBuffer(t, b);
    CHK();
}
EXT(_glBindFramebuffer) { GT("glBindFramebuffer %x %u\n", A32(0), A32(1)); p_glBindFramebuffer(A32(0), host_fb(A32(1))); CHK(); }
EXT(_glBindRenderbuffer) { p_glBindRenderbuffer(A32(0), A32(1)); CHK(); }
EXT(_glBindVertexArrayOES) { p_glBindVertexArray(A32(0)); CHK(); }
EXT(_glBlendColor) { p_glBlendColor(AF(0), AF(1), AF(2), AF(3)); CHK(); }
EXT(_glBlendEquation) { p_glBlendEquation(A32(0)); CHK(); }
EXT(_glBufferData) { p_glBufferData(A32(0), (GLsz)A32(1), A32(2) ? GP(A32(2)) : NULL, A32(3)); CHK(); }
EXT(_glBufferSubData) { p_glBufferSubData(A32(0), (GLsz)A32(1), (GLsz)A32(2), GP(A32(3))); CHK(); }
EXT(_glCheckFramebufferStatus) { GLenum st = p_glCheckFramebufferStatus(A32(0)); GT("glCheckFramebufferStatus -> %x\n", st); RET32(st); }
EXT(_glCompileShader)
{
    GLuint s = A32(0);
    p_glCompileShader(s);
    GLint ok = 0;
    p_glGetShaderiv(s, 0x8B81, &ok);
    if (!ok) {
        char log[2048] = {0};
        p_glGetShaderInfoLog(s, sizeof log - 1, NULL, log);
        fprintf(stderr, "Echec de compilation du shader %u :\n%s\n", s, log);
    }
}
EXT(_glCreateProgram) { RET32(p_glCreateProgram()); }
EXT(_glCreateShader) { RET32(p_glCreateShader(A32(0))); }
EXT(_glDeleteBuffers) { p_glDeleteBuffers((GLsizei)A32(0), (const GLuint *)GP(A32(1))); CHK(); }
EXT(_glDeleteFramebuffers) { p_glDeleteFramebuffers((GLsizei)A32(0), (const GLuint *)GP(A32(1))); CHK(); }
EXT(_glDeleteProgram) { p_glDeleteProgram(A32(0)); CHK(); }
EXT(_glDeleteRenderbuffers) { p_glDeleteRenderbuffers((GLsizei)A32(0), (const GLuint *)GP(A32(1))); CHK(); }
EXT(_glDeleteShader) { p_glDeleteShader(A32(0)); CHK(); }
EXT(_glDeleteVertexArraysOES) { p_glDeleteVertexArrays((GLsizei)A32(0), (const GLuint *)GP(A32(1))); CHK(); }
EXT(_glDisableVertexAttribArray) { p_glDisableVertexAttribArray(A32(0)); CHK(); }
EXT(_glDiscardFramebufferEXT) { }
EXT(_glEnableVertexAttribArray) { GT("glEnableVertexAttribArray %u\n", A32(0)); p_glEnableVertexAttribArray(A32(0)); CHK(); }
EXT(_glFramebufferRenderbuffer) { p_glFramebufferRenderbuffer(A32(0), A32(1), A32(2), A32(3)); CHK(); }
EXT(_glFramebufferTexture2D) { GT("glFramebufferTexture2D %x %x %x %u lvl %d\n", A32(0), A32(1), A32(2), A32(3), (int)A32(4)); p_glFramebufferTexture2D(A32(0), A32(1), A32(2), A32(3), (GLint)A32(4)); CHK(); }
EXT(_glGenBuffers) { p_glGenBuffers((GLsizei)A32(0), (GLuint *)GP(A32(1))); CHK(); }
EXT(_glGenFramebuffers) { p_glGenFramebuffers((GLsizei)A32(0), (GLuint *)GP(A32(1))); CHK(); }
EXT(_glGenRenderbuffers) { p_glGenRenderbuffers((GLsizei)A32(0), (GLuint *)GP(A32(1))); CHK(); }
EXT(_glGenVertexArraysOES) { p_glGenVertexArrays((GLsizei)A32(0), (GLuint *)GP(A32(1))); CHK(); }
EXT(_glGenerateMipmap) { p_glGenerateMipmap(A32(0)); CHK(); }
EXT(_glGetProgramiv) { p_glGetProgramiv(A32(0), A32(1), (GLint *)GP(A32(2))); CHK(); }
EXT(_glGetShaderiv) { p_glGetShaderiv(A32(0), A32(1), (GLint *)GP(A32(2))); CHK(); }
EXT(_glGetUniformLocation) { GT("glGetUniformLocation prog=%u name=%s\n", A32(0), GSTR(A32(1))); RET32((uint32_t)p_glGetUniformLocation(A32(0), GSTR(A32(1)))); }
EXT(_glLinkProgram)
{
    GLuint p = A32(0);
    p_glLinkProgram(p);
    GLint ok = 0;
    p_glGetProgramiv(p, 0x8B82, &ok);
    if (!ok) {
        char log[2048] = {0};
        p_glGetProgramInfoLog(p, sizeof log - 1, NULL, log);
        fprintf(stderr, "Echec d'edition de liens du programme %u :\n%s\n", p, log);
    }
}
EXT(_glRenderbufferStorage) { p_glRenderbufferStorage(A32(0), A32(1), (GLsizei)A32(2), (GLsizei)A32(3)); CHK(); }

/* glShaderSource : GLSL ES 1.00 -> GLSL 1.20 (qualificateurs de precision retires) */
EXT(_glShaderSource)
{
    GLuint sh = A32(0);
    uint32_t count = A32(1), arr = A32(2), lens = A32(3);
    size_t cap = 256;
    for (uint32_t i = 0; i < count; i++) cap += strlen(GSTR(M32(arr + 4 * i))) + 1;
    char *src = malloc(cap + 1), *o = src;
#ifdef __ANDROID__
    /* Android : le GLSL ES 1.00 d'iOS passe tel quel (precision comprise) */
    for (uint32_t i = 0; i < count; i++) {
        const char *s = GSTR(M32(arr + 4 * i));
        size_t n = lens && (int32_t)M32(lens + 4 * i) >= 0 ? (size_t)M32(lens + 4 * i) : strlen(s);
        memcpy(o, s, n); o += n;
    }
    *o = 0;
    { const char *list[1] = { src }; p_glShaderSource(sh, 1, list, NULL); }
    free(src);
    CHK();
    return;
#endif
    o += sprintf(o, "#version 120\n#define lowp\n#define mediump\n#define highp\n");
    for (uint32_t i = 0; i < count; i++) {
        const char *s = GSTR(M32(arr + 4 * i));
        size_t n = lens && (int32_t)M32(lens + 4 * i) >= 0 ? (size_t)M32(lens + 4 * i) : strlen(s);
        const char *p = s, *end = s + n;
        while (p < end) {
            const char *nl = memchr(p, '\n', (size_t)(end - p));
            size_t ln = nl ? (size_t)(nl - p) + 1 : (size_t)(end - p);
            const char *q = p;
            while (q < p + ln && (*q == ' ' || *q == '\t')) q++;
            if (strncmp(q, "precision ", 10) != 0) { memcpy(o, p, ln); o += ln; }
            p += ln;
        }
        *o++ = '\n';
    }
    *o = 0;
    GT("---- shader %u ----\n%s\n", sh, src);
    const char *list[1] = { src };
    p_glShaderSource(sh, 1, list, NULL);
    free(src);
    CHK();
}
EXT(_glUniform1f) { p_glUniform1f((GLint)A32(0), AF(1)); CHK(); }
EXT(_glUniform1i) { GT("glUniform1i loc=%d v=%d\n", (int)A32(0), (int)A32(1)); p_glUniform1i((GLint)A32(0), (GLint)A32(1)); CHK(); }
EXT(_glUniform2f) { p_glUniform2f((GLint)A32(0), AF(1), AF(2)); CHK(); }
EXT(_glUniform4fv) { GT("glUniform4fv loc=%d n=%d v=%f,%f,%f,%f\n", (int)A32(0),(int)A32(1), ((float*)GP(A32(2)))[0], ((float*)GP(A32(2)))[1], ((float*)GP(A32(2)))[2], ((float*)GP(A32(2)))[3]); p_glUniform4fv((GLint)A32(0), (GLsizei)A32(1), (const float *)GP(A32(2))); CHK(); }
EXT(_glUniformMatrix4fv) {
    if (g_gl_trace) {
        const float *m = (const float *)GP(A32(3));
        if (m[0] == 0 && m[5] == 0) rt_backtrace("matrice a echelle nulle");
        fprintf(stderr, "glUniformMatrix4fv loc=%d n=%d transpose=%d [%g %g %g %g | %g %g %g %g | %g %g %g %g | %g %g %g %g]\n",
                (int)A32(0), (int)A32(1), (int)(A32(2) & 0xff), m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11], m[12], m[13], m[14], m[15]);
    }
    p_glUniformMatrix4fv((GLint)A32(0), (GLsizei)A32(1), A32(2) & 0xff, (const float *)GP(A32(3))); CHK(); }
EXT(_glUseProgram) { GT("glUseProgram %u\n", A32(0)); p_glUseProgram(A32(0)); CHK(); }
EXT(_glVertexAttribPointer)
{ GT("glVertexAttribPointer idx=%u size=%d type=%x norm=%d stride=%d ptr=%08x\n", A32(0), (int)A32(1), A32(2), (int)(A32(3)&0xff), (int)A32(4), A32(5));
    const void *ptr = g_array_buf ? (const void *)(uintptr_t)A32(5) : (const void *)GP(A32(5));
    p_glVertexAttribPointer(A32(0), (GLint)A32(1), A32(2), A32(3) & 0xff, (GLsizei)A32(4), ptr);
    CHK();
}
