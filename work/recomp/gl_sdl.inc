/* Fenetre, contexte OpenGL, entrees et presentation via SDL2 (Linux / Steam Deck).
 * Inclus par gl_host.c (#ifndef _WIN32) : partage ses variables statiques (file d'evenements, mode texte...).
 * Le jeu dessine toujours en 1024x768 : si la fenetre a une autre taille (plein ecran), il dessine dans un FBO de 1024x768
 * recopie en conservant les proportions (bandes noires). */
static void write_png(const char *path, const uint8_t *rgba, int w, int h);
static SDL_Window *g_win;
static SDL_GLContext g_glctx;
static int g_win_w = 1024, g_win_h = 768;
static GLuint g_fbo_main, g_fbo_tex, g_fbo_rb;
static int g_scale;                              /* 1 : rendu dans le FBO puis recopie a l'echelle */
static int g_dst_x, g_dst_y, g_dst_w = 1024, g_dst_h = 768;
static SDL_GameController *g_pad;
static float g_cur_x = 512.0f, g_cur_y = 384.0f; /* curseur virtuel de la manette (espace 1024x768) */
static int g_pad_down;

static void sdl_layout(void)
{
    int dw = 0, dh = 0;
    SDL_GL_GetDrawableSize(g_win, &dw, &dh);
    SDL_GetWindowSize(g_win, &g_win_w, &g_win_h);
    if (dw <= 0 || dh <= 0) { dw = g_win_w; dh = g_win_h; }
    double s = (double)dw / 1024.0; if ((double)dh / 768.0 < s) s = (double)dh / 768.0;
    g_dst_w = (int)(1024.0 * s); g_dst_h = (int)(768.0 * s);
    g_dst_x = (dw - g_dst_w) / 2; g_dst_y = (dh - g_dst_h) / 2;
}
/* position fenetre (pixels) -> espace du jeu (0..1023, 0..767) */
static void sdl_to_game(int wx, int wy, int *gx, int *gy)
{
    double fx = (double)wx * ((double)g_dst_w + 2.0 * g_dst_x) / (double)(g_win_w ? g_win_w : 1);
    double fy = (double)wy * ((double)g_dst_h + 2.0 * g_dst_y) / (double)(g_win_h ? g_win_h : 1);
    int x = (int)((fx - g_dst_x) * 1024.0 / (double)g_dst_w), y = (int)((fy - g_dst_y) * 768.0 / (double)g_dst_h);
    *gx = x < 0 ? 0 : x > 1023 ? 1023 : x; *gy = y < 0 ? 0 : y > 767 ? 767 : y;
}
static int sdl_key(SDL_Keycode k)
{
    if (k >= SDLK_a && k <= SDLK_z) return (int)(k - SDLK_a) + 1;
    if (k >= SDLK_0 && k <= SDLK_9) return (int)(k - SDLK_0) + 27;
    if (k >= SDLK_F1 && k <= SDLK_F12) return (int)(k - SDLK_F1) + 101;
    switch (k) {
    case SDLK_TAB: return 37; case SDLK_RETURN: case SDLK_KP_ENTER: return 38; case SDLK_SPACE: return 39; case SDLK_BACKSPACE: return 88;
    case SDLK_DELETE: return 91; case SDLK_HOME: return 92; case SDLK_END: return 93;
    case SDLK_UP: return 96; case SDLK_DOWN: return 97; case SDLK_LEFT: return 98; case SDLK_RIGHT: return 99; case SDLK_ESCAPE: return 100;
    }
    return 0;
}
static void sdl_text(const char *s)   /* UTF-8 -> points de code */
{
    const unsigned char *u = (const unsigned char *)s;
    while (*u) {
        int cp, n = 1;
        if (u[0] < 0x80) cp = u[0];
        else if ((u[0] & 0xE0) == 0xC0 && (u[1] & 0xC0) == 0x80) { cp = ((u[0] & 0x1F) << 6) | (u[1] & 0x3F); n = 2; }
        else if ((u[0] & 0xF0) == 0xE0 && (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80) { cp = ((u[0] & 0x0F) << 12) | ((u[1] & 0x3F) << 6) | (u[2] & 0x3F); n = 3; }
        else if ((u[0] & 0xF8) == 0xF0 && (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80 && (u[3] & 0xC0) == 0x80) { cp = ((u[0] & 0x07) << 18) | ((u[1] & 0x3F) << 12) | ((u[2] & 0x3F) << 6) | (u[3] & 0x3F); n = 4; }
        else cp = u[0];
        if (cp >= 32 && cp != 127) text_input_cp(cp);
        u += n;
    }
}
/* manette : stick gauche / droit = curseur, A ou gachette droite = appui, B = Echap, X = Espace (pause), Y = Entree, Start = Echap */
static void pad_key(int key, int down) { if (!g_text_mode) eq_push(2, down ? 5 : 6, key); else if (down && key == 100) text_command(21); else if (down && key == 38) text_command(20); }
static void pad_poll(void)
{
    if (!g_pad) return;
    float ax = SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f, ay = SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f;
    float bx = SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_RIGHTX) / 32767.0f, by = SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_RIGHTY) / 32767.0f;
    if (fabsf(bx) > fabsf(ax)) ax = bx;  if (fabsf(by) > fabsf(ay)) ay = by;
    if (fabsf(ax) < 0.15f) ax = 0; if (fabsf(ay) < 0.15f) ay = 0;
    if (ax != 0 || ay != 0) {
        g_cur_x += ax * fabsf(ax) * 14.0f; g_cur_y += ay * fabsf(ay) * 14.0f;
        if (g_cur_x < 0) g_cur_x = 0; if (g_cur_x > 1023) g_cur_x = 1023; if (g_cur_y < 0) g_cur_y = 0; if (g_cur_y > 767) g_cur_y = 767;
        if (g_pad_down) tq_push(30, MAKELPARAM((int)g_cur_x, (int)g_cur_y));
    }
}
void host_pump(void)
{
    SDL_Event e; int gx, gy;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT: InterlockedExchange(&g_quit, 1); break;
        case SDL_WINDOWEVENT: if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED || e.window.event == SDL_WINDOWEVENT_RESIZED) { sdl_layout(); g_scale = (g_dst_w != 1024 || g_dst_h != 768 || g_dst_x || g_dst_y); } break;
        case SDL_MOUSEBUTTONDOWN: if (e.button.button == SDL_BUTTON_LEFT) { touch_down = 1; sdl_to_game(e.button.x, e.button.y, &gx, &gy); g_cur_x = (float)gx; g_cur_y = (float)gy; tq_push(29, MAKELPARAM(gx, gy)); } break;
        case SDL_MOUSEMOTION: if (touch_down) { sdl_to_game(e.motion.x, e.motion.y, &gx, &gy); g_cur_x = (float)gx; g_cur_y = (float)gy; tq_push(30, MAKELPARAM(gx, gy)); } break;
        case SDL_MOUSEBUTTONUP: if (e.button.button == SDL_BUTTON_LEFT && touch_down) { touch_down = 0; sdl_to_game(e.button.x, e.button.y, &gx, &gy); tq_push(31, MAKELPARAM(gx, gy)); } break;
        case SDL_TEXTINPUT: if (g_text_mode) sdl_text(e.text.text); break;
        case SDL_KEYDOWN:
            if (g_text_mode) {
                switch (e.key.keysym.sym) {
                case SDLK_BACKSPACE: text_command(23); break;  case SDLK_DELETE: text_command(24); break;
                case SDLK_LEFT: text_command(25); break;       case SDLK_RIGHT: text_command(26); break;
                case SDLK_HOME: text_command(27); break;       case SDLK_END: text_command(28); break;
                case SDLK_RETURN: case SDLK_KP_ENTER: text_command(20); break;
                case SDLK_ESCAPE: text_command(21); break;
                }
            } else { int k = sdl_key(e.key.keysym.sym); if (k && !e.key.repeat) eq_push(2, 5, k); }
            break;
        case SDL_KEYUP: if (!g_text_mode) { int k = sdl_key(e.key.keysym.sym); if (k) eq_push(2, 6, k); } break;
        case SDL_CONTROLLERDEVICEADDED: if (!g_pad) g_pad = SDL_GameControllerOpen(e.cdevice.which); break;
        case SDL_CONTROLLERDEVICEREMOVED: if (g_pad && !SDL_GameControllerGetAttached(g_pad)) { SDL_GameControllerClose(g_pad); g_pad = NULL; } break;
        case SDL_CONTROLLERBUTTONDOWN: case SDL_CONTROLLERBUTTONUP: {
            int dn = e.type == SDL_CONTROLLERBUTTONDOWN;
            switch (e.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_A: if (dn && !g_pad_down) { g_pad_down = 1; tq_push(29, MAKELPARAM((int)g_cur_x, (int)g_cur_y)); } else if (!dn && g_pad_down) { g_pad_down = 0; tq_push(31, MAKELPARAM((int)g_cur_x, (int)g_cur_y)); } break;
            case SDL_CONTROLLER_BUTTON_B: case SDL_CONTROLLER_BUTTON_START: pad_key(100, dn); break;
            case SDL_CONTROLLER_BUTTON_X: pad_key(39, dn); break;
            case SDL_CONTROLLER_BUTTON_Y: pad_key(38, dn); break;
            }
            break; }
        case SDL_CONTROLLERAXISMOTION:
            if (e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
                int dn = e.caxis.value > 16000;
                if (dn && !g_pad_down) { g_pad_down = 1; tq_push(29, MAKELPARAM((int)g_cur_x, (int)g_cur_y)); }
                else if (!dn && g_pad_down) { g_pad_down = 0; tq_push(31, MAKELPARAM((int)g_cur_x, (int)g_cur_y)); }
            }
            break;
        }
    }
    pad_poll();
}

int host_window_init(int width, int height)
{
    g_gl_debug = getenv("FTL_GLDEBUG") != NULL;
    g_gl_trace = getenv("FTL_GLTRACE") != NULL;
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) != 0) { fprintf(stderr, "SDL_Init : %s\n", SDL_GetError()); return 0; }
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    Uint32 fl = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI;
    int full = getenv("FTL_FULLSCREEN") != NULL || getenv("SteamDeck") != NULL || getenv("SteamGamepadUI") != NULL;   /* Steam Deck : plein ecran */
    if (getenv("FTL_WINDOWED")) full = 0;
    if (full) fl |= SDL_WINDOW_FULLSCREEN_DESKTOP; else fl |= SDL_WINDOW_RESIZABLE;
    { const char *ws = getenv("FTL_WINSIZE"); int ww, wh; if (ws && sscanf(ws, "%dx%d", &ww, &wh) == 2 && ww > 0 && wh > 0) { width = ww; height = wh; } }   /* test : fenetre a une autre taille */
    g_win = SDL_CreateWindow("FTL (iPad, recompile)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, fl);
    if (!g_win) { fprintf(stderr, "SDL_CreateWindow : %s\n", SDL_GetError()); return 0; }
    g_glctx = SDL_GL_CreateContext(g_win);
    if (!g_glctx) { fprintf(stderr, "SDL_GL_CreateContext : %s\n", SDL_GetError()); return 0; }
#define LOAD(r_, n, a) p_##n = (PFN_##n)SDL_GL_GetProcAddress(#n);
    GLFUNCS(LOAD)
#undef LOAD
    SDL_GL_SetSwapInterval(getenv("FTL_NOVSYNC") ? 0 : 1);
    SDL_ShowCursor(1);
    SDL_StartTextInput();
    sdl_layout();
    g_scale = (g_dst_w != 1024 || g_dst_h != 768 || g_dst_x || g_dst_y);
    /* FBO de rendu 1024x768 (utilise seulement si la fenetre n'est pas a cette taille ; cree toujours : redimensionnement possible) */
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
    p_glBindFramebuffer(0x8D40, 0);
    if (g_scale) p_glBindFramebuffer(0x8D40, g_fbo_main);
    for (int i = 0; i < SDL_NumJoysticks(); i++) if (SDL_IsGameController(i)) { g_pad = SDL_GameControllerOpen(i); break; }
    fprintf(stderr, "OpenGL hote : %s | %s%s\n", (const char *)glGetString(GL_RENDERER), (const char *)glGetString(GL_VERSION), g_scale ? " | mise a l'echelle" : "");
    return 1;
}
/* le jeu "lie" le framebuffer 0 : on le redirige vers le FBO quand la fenetre n'est pas en 1024x768 */
static GLuint host_fb(GLuint fb) { return (fb == 0 && g_scale) ? g_fbo_main : fb; }
static void host_swap(void)
{
    if (g_scale) {
        int dw = g_dst_w + 2 * g_dst_x, dh = g_dst_h + 2 * g_dst_y;
        GLint saved = 0; glGetIntegerv(0x8CA6, &saved);
        p_glBindFramebuffer(0x8CA8 /* READ */, g_fbo_main);
        p_glBindFramebuffer(0x8CA9 /* DRAW */, 0);
        GLboolean sc = glIsEnabled(GL_SCISSOR_TEST); GLfloat cc[4]; glGetFloatv(GL_COLOR_CLEAR_VALUE, cc);
        glViewport(0, 0, dw, dh);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
        p_glBlitFramebuffer(0, 0, 1024, 768, g_dst_x, g_dst_y, g_dst_x + g_dst_w, g_dst_y + g_dst_h, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        { static int ws = -2; if (ws == -2) { const char *e = getenv("FTL_WINSHOT"); ws = e ? atoi(e) : -1; }
          if (ws >= 0 && g_frames == ws) { uint8_t *px = malloc((size_t)dw * dh * 4); p_glBindFramebuffer(0x8CA8, 0); glReadBuffer(GL_BACK); glReadPixels(0, 0, dw, dh, GL_RGBA, GL_UNSIGNED_BYTE, px);
            for (int i = 3; i < dw * dh * 4; i += 4) px[i] = 255; write_png("winshot.png", px, dw, dh); free(px); fprintf(stderr, "[capture fenetre winshot.png %dx%d]\n", dw, dh); } }
        SDL_GL_SwapWindow(g_win);
        p_glBindFramebuffer(0x8D40, (GLuint)saved);
        glViewport(g_vp[0], g_vp[1], g_vp[2], g_vp[3]);
        if (sc) glEnable(GL_SCISSOR_TEST);
        glClearColor(cc[0], cc[1], cc[2], cc[3]);
    } else {
        SDL_GL_SwapWindow(g_win);
    }
}
