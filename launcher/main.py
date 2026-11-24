# -*- coding: utf-8 -*-
"""Fenetre, boucle principale, mode captures (--shots) et demonstration (--demo)."""
import os, subprocess, sys, tempfile, time
import sdl2

from . import ui as U, app as A, i18n, scan, build, settings
from .ui import W, H


def make_window(fullscreen):
    sdl2.SDL_SetHint(sdl2.SDL_HINT_RENDER_SCALE_QUALITY, b"linear")
    flags = sdl2.SDL_WINDOW_SHOWN | sdl2.SDL_WINDOW_RESIZABLE | sdl2.SDL_WINDOW_ALLOW_HIGHDPI
    if fullscreen: flags |= sdl2.SDL_WINDOW_FULLSCREEN_DESKTOP
    win = sdl2.SDL_CreateWindow(b"FTL", sdl2.SDL_WINDOWPOS_CENTERED, sdl2.SDL_WINDOWPOS_CENTERED, W, H, flags)
    r = sdl2.SDL_CreateRenderer(win, -1, sdl2.SDL_RENDERER_ACCELERATED | sdl2.SDL_RENDERER_PRESENTVSYNC) or sdl2.SDL_CreateRenderer(win, -1, sdl2.SDL_RENDERER_SOFTWARE)
    sdl2.SDL_RenderSetLogicalSize(r, W, H)
    return win, r


def launch_game(app, cmd=None):
    """Lance le jeu construit avec les options choisies ; renvoie son code de retour. cmd : commande de remplacement (tests)."""
    exe, g = app.launch
    genv = app.settings.game_env()
    A.ulog("launching the game with options: %s" % genv)
    try:
        rc = subprocess.call(cmd or [exe, "."], cwd=g, env=dict(os.environ, **genv))
    except OSError as e:
        A.ulog("cannot launch the game: %s" % e); rc = 1
    A.ulog("the game exited with code %s" % rc)
    return rc


def run(root, font_dir, argv):
    import ctypes
    from . import sdlttf_init
    lang = None; shots = None; demo = None; fullscreen = False; selftest = False; smoke = 0; autotest = False
    it = iter(argv)
    for a in it:
        if a == "--lang": lang = next(it)
        elif a == "--shots": shots = next(it)
        elif a == "--demo": demo = "ok"
        elif a == "--demo-fail": demo = "fail"
        elif a == "--fullscreen": fullscreen = True
        elif a == "--selftest": selftest = True
        elif a == "--smoke": smoke = int(next(it))
        elif a == "--autotest": autotest = True
    T = i18n.pick(lang)
    cli_lang = lang
    real_ipa = None
    if demo:                                           # dossier jetable : rien d'ecrit dans le vrai dossier
        real_ipa = (scan.find_ipas(root) or [None])[0]   # si ton IPA existe, la demonstration en decoupe vraiment le fond
        root = tempfile.mkdtemp(prefix="ftl_demo_"); os.environ["FTL_FAKE_ROOT"] = root
    if shots: return make_shots(root, font_dir, T, shots)
    if selftest: return selftest_run(root, font_dir, T)
    # SDL transforme SIGTERM/SIGINT en "fenetre fermee" : on prend la main pour savoir QUI demande l'arret (session SSH terminee, systeme, utilisateur)
    import signal
    sdl2.SDL_SetHint(b"SDL_NO_SIGNAL_HANDLERS", b"1")
    got = []
    def on_signal(sig, frm): got.append(signal.Signals(sig).name)
    for name in ("SIGTERM", "SIGHUP", "SIGINT"):
        if hasattr(signal, name): signal.signal(getattr(signal, name), on_signal)
    sdl2.SDL_Init(sdl2.SDL_INIT_VIDEO | sdl2.SDL_INIT_GAMECONTROLLER | sdl2.SDL_INIT_EVENTS)
    sdlttf_init.init()
    st0 = settings.Settings(os.path.join(root, "settings.ini"))
    if not cli_lang: T = i18n.pick(st0.get("launcher", "language"))
    fullscreen = fullscreen or st0.launcher_fullscreen()
    win, r = make_window(fullscreen)
    ui = U.UI(r, font_dir)
    cmd = [sys.executable, os.path.join(os.path.dirname(__file__), "fake_build.py")] + (["fail"] if demo == "fail" else []) if demo else None
    app = A.App(root, ui, T, demo=cmd, scanning=not demo)
    app.window = win
    demo_t0 = time.time()
    if demo:                                     # demonstration : on part de "rien trouve", l'IPA "arrive" apres 6 s
        sc = app.scanner; sc._busy = True; app.next_rescan = 1e18; sc.ipa_state, sc.pc_state = "none", "none"
        demo_pc = {"path": "C:/Games/FTL Advanced Edition/ftl.dat.vanilla", "store": "GOG", "warnings": [], "languages": ["fr"], "vanilla": True}
    for i in range(sdl2.SDL_NumJoysticks()):
        if sdl2.SDL_IsGameController(i): sdl2.SDL_GameControllerOpen(i)
    ev = sdl2.SDL_Event(); last = time.time()
    n = 0; phase = 0; t_auto = time.time(); last_pct = -1
    from .app import ulog
    def log(m): print("[autotest %5.0fs] %s" % (time.time() - t_auto, m), flush=True)
    while not app.quit and not app.launch:
        if got:
            ulog("signal received: %s -> the program asked to stop (not a click in the window)" % ", ".join(got)); app.quit = True; break
        if autotest:                                   # test de bout en bout : detecte l'IPA, construit pour de vrai, puis clique JOUER
            if phase == 0 and app.scanner.ipa_state == "ok": log("IPA detected: %s" % app.scanner.ipa_path); app.activate("build"); phase = 1; log("BUILD clicked")
            elif phase == 0 and time.time() - t_auto > 60: log("FAIL: IPA not detected in 60 s"); return 2
            elif phase == 1 and app.runner:
                pct = int(app.runner.progress() * 100)
                if pct != last_pct and pct % 5 == 0: last_pct = pct; log("progress %d%% (stage %s) %s" % (pct, app.runner.stage, app.runner.activity))
            if phase == 1 and app.state == "ready": log("build finished -> READY screen"); app.activate("play"); phase = 2; log("PLAY clicked")
            if app.state == "error": log("FAIL: build error"); [print("   " + l) for l in (app.runner.lines[-12:] if app.runner else [])]; return 3
            if time.time() - t_auto > 3 * 3600: log("FAIL: timeout"); return 4
        n += 1
        if smoke and n > smoke: print("fenetre ouverte et %d images dessinees : ok" % smoke); break
        while sdl2.SDL_PollEvent(ctypes.byref(ev)): app.handle(ev)
        now = time.time(); dt = now - last; last = now
        if demo and app.state == "setup" and now - demo_t0 > 6 and app.scanner.ipa_state == "none":
            app.scanner.ipa_path, app.scanner.ipa_state = real_ipa or os.path.join(root, "ipa", "FTL__Faster_Than_Light_1.5.10_ios_6.0.ipa"), "verifying"
        if demo and now - demo_t0 > 8 and app.scanner.ipa_state == "verifying":
            app.scanner.ipa_state = "ok"; app.scanner.pc, app.scanner.pc_state = demo_pc, "found"
        app.draw(dt); sdl2.SDL_RenderPresent(r)
    if app.launch:
        sdl2.SDL_HideWindow(win)
        rc = launch_game(app)
        sdl2.SDL_Quit(); return rc
    A.ulog("exit: app.quit=%s launch=%s" % (app.quit, bool(app.launch)))
    sdl2.SDL_Quit(); return 0


# ---------------------------------------------------------------- captures sans fenetre
def software_target():
    surf = sdl2.SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, sdl2.SDL_PIXELFORMAT_ARGB8888)
    return surf, sdl2.SDL_CreateSoftwareRenderer(surf)


def make_shots(root, font_dir, T, outdir):
    from . import sdlttf_init
    sdl2.SDL_Init(sdl2.SDL_INIT_EVENTS); sdlttf_init.init()
    os.makedirs(outdir, exist_ok=True)
    surf, r = software_target()
    ui = U.UI(r, font_dir)
    app = A.App(root, ui, T, scanning=False)
    sc = app.scanner; sc._busy = True; app.next_rescan = 1e18        # pas de thread de recherche : etats fixes
    ipa = os.path.join(root, "ipa", "FTL__Faster_Than_Light_1.5.10_ios_6.0.ipa")
    pc = {"path": r"C:\Games\FTL Advanced Edition\ftl.dat.vanilla", "store": "GOG", "warnings": [], "languages": ["de", "es", "fr", "it"], "vanilla": True}

    def shot(name, frames=3):
        for _ in range(frames): ui.background(0.0); app.draw(0.2)
        U.surface_to_png(surf, os.path.join(outdir, name + ".png"))
        print("capture", name)

    app.state = "setup"; sc.ipa_state, sc.pc_state = "none", "none"; app.focus = "open_dir"; shot("1_first_launch_empty")
    sc.ipa_path, sc.ipa_state, sc.pc, sc.pc_state = ipa, "ok", pc, "found"; app.focus = "build"; app.focus_set_once = True; shot("2_ready_to_build")
    sc.ipa_state = "verifying"; app.focus = "browse_ipa"; shot("3_checking")
    sc.ipa_state = "bad"; shot("4_wrong_ipa")
    sc.ipa_state, sc.pc_state, sc.pc = "ok", "none", None; app.focus = "build"; shot("5_no_pc_ftl")
    app.open_browse("ipa"); app.browse_cd(root); shot("6_browse")
    r2 = build.BuildRunner(root, ipa, pc["path"]); r2.state = "running"; r2.done = {"extract", "ghidra", "lift", "patch"}; r2.stage = "compile"; r2.t0 = time.time() - 754
    r2.activity = "compile 31/64"; r2.lines = ["[build] === stage : compile", "[build] compiling 64 files with Zig (x86_64-windows-gnu)", "[build]    10 / 64 (19 s)", "[build]    20 / 64 (26 s)", "[build]    30 / 64 (40 s)"]
    r2.sub = 31 / 64; app.runner, app.state, app.focus = r2, "build", "cancel"; shot("7_building")
    r2.done = {"extract"}; r2.stage = "ghidra"; r2.activity = "dl ghidra_12.2_PUBLIC_20261016.zip"; r2.t0 = time.time() - 95; r2.sub = 0.2; shot("8_build_download")
    r2.state, r2.error_stage = "error", "compile"; r2.lines = ["[build] === stage : compile", "ERROR: 1 file(s) failed", "--- hle_sound.c", "hle_sound.c:154:16: error: incompatible function pointer types passing", "  void (*)(void)  vs  int (*)(void)", "1 error generated."]
    app.state, app.focus = "error", "retry"; shot("9_error")
    app.runner = None; g = os.path.join(root, "build", "game"); app.game_dir = g; app.state, app.focus = "ready", "play"; shot("10_ready_to_play")
    app.modal = "rebuild"; shot("11_confirm_rebuild")
    app.settings.save = lambda: None                      # captures : rien n'est ecrit dans settings.ini
    app.modal = None; app.prev_state = "ready"; app.state = "options"; app.disk_bytes = int(2.3 * 1024 ** 3); app.focus = "back"; shot("12_options")
    app.settings.cp["game"]["vsync"] = "off"; app.settings.cp["game"]["record"] = "on"; app.settings.cp["launcher"]["fullscreen"] = "on"
    app.T = i18n.FR; app.disk_msg = "Terminé : 2,1 GB libérés"; app.focus = "free"; shot("13_options_fr")
    app.modal = "free"; shot("14_confirm_free_space")
    return 0


# ---------------------------------------------------------------- test des enchainements (sans fenetre)
def selftest_run(root, font_dir, T):
    from . import sdlttf_init
    sdl2.SDL_Init(sdl2.SDL_INIT_EVENTS); sdlttf_init.init()
    surf, r = software_target(); ui = U.UI(r, font_dir)
    os.environ["FTL_FAKE_ROOT"] = root = tempfile.mkdtemp(prefix="ftl_selftest_")
    cmd = [sys.executable, os.path.join(os.path.dirname(__file__), "fake_build.py")]
    app = A.App(root, ui, T, demo=cmd, scanning=False); sc = app.scanner; sc._busy = True; sc.ipa_state, sc.pc_state = "none", "none"; app.next_rescan = 1e18
    ok = True
    def check(cond, msg):
        nonlocal ok
        print(("ok   " if cond else "ECHEC"), msg); ok &= bool(cond)
    def frames(n=3):
        for _ in range(n): app.draw(0.05)
    frames(); check(app.state == "setup", "demarre sur l'ecran de premier lancement")
    check(not any(w[0] == "build" and w[5] for w in app.widgets), "CONSTRUIRE desactive sans IPA")
    sc.ipa_path, sc.ipa_state = os.path.join(root, "ipa", "x.ipa"), "ok"; frames()
    check(any(w[0] == "build" and w[5] for w in app.widgets), "CONSTRUIRE active une fois l'IPA reconnu")
    check(app.focus == "build", "le focus va sur CONSTRUIRE")
    app.key_action("right"); frames(); check(app.focus == "quit", "fleche droite : le focus va sur QUITTER (%s)" % app.focus)
    app.key_action("left"); frames(); check(app.focus == "build", "fleche gauche : retour sur CONSTRUIRE (%s)" % app.focus)
    app.key_action("up"); frames(); check(app.focus not in ("build", None), "fleche haut : le focus monte (%s)" % app.focus)
    app.focus = "build"; app.key_action("ok"); frames()
    check(app.state == "build", "Entree sur CONSTRUIRE lance la construction")
    t0 = time.time()
    while app.state == "build" and time.time() - t0 < 40: time.sleep(0.2); frames(1)
    check(app.state == "ready", "fin du faux build -> ecran PRET (%s)" % app.state)
    app.focus = "rebuild"; app.key_action("ok"); frames(); check(app.modal == "rebuild", "RECONSTRUIRE demande confirmation")
    app.key_action("back"); frames(); check(app.modal is None, "Echap annule la confirmation")
    app.focus = "play"; app.key_action("ok"); check(app.launch is not None, "JOUER prepare le lancement du jeu")
    # le vrai lancement : le jeu recoit bien les options (la commande de remplacement sort avec 7 si FTL_NOVSYNC=1)
    app.settings.cp["game"]["vsync"] = "off"
    app.launch = (sys.executable, root)
    rc = launch_game(app, [sys.executable, "-c", "import os,sys; sys.exit(7 if os.environ.get('FTL_NOVSYNC') == '1' else 3)"])
    check(rc == 7, "launch_game execute le programme avec les options en variables d'environnement (code %s)" % rc)
    app.launch = (os.path.join(root, "does-not-exist.exe"), root)
    check(launch_game(app) == 1, "un jeu introuvable ne fait pas planter le lanceur (code de retour 1)")
    app.settings.cp["game"]["vsync"] = "on"
    app.launch = None
    # annulation + echec
    app.state = "setup"; sc.ipa_state = "ok"; app.focus = "build"; app.demo = cmd + ["fail"]; app.key_action("ok")
    t0 = time.time()
    while app.state == "build" and time.time() - t0 < 40: time.sleep(0.2); frames(1)
    check(app.state == "error", "echec du build -> ecran d'erreur (%s)" % app.state)
    app.key_action("ok"); frames(); check(app.state == "build", "RESSAYER relance la construction")
    app.focus = "cancel"; app.key_action("ok"); frames(); check(app.state == "setup", "ANNULER revient au premier lancement")
    # parcourir
    app.open_browse("ipa"); frames(); check(app.state == "browse" and len(app.browse["entries"]) > 0, "PARCOURIR liste un dossier")
    app.key_action("back"); frames(); check(app.state == "setup", "Echap quitte le navigateur")
    # ---- options
    app.state = "setup"; app.runner = None; frames(); check(any(w[0] == "options" for w in app.widgets), "le bouton OPTIONS est present sur l'ecran de depart")
    app.activate("options"); frames(); check(app.state == "options", "OPTIONS ouvre l'ecran d'options")
    app.activate("opt:game:vsync"); app.activate("opt:game:show_fps"); app.activate("opt:game:sound"); app.activate("opt:game:record")
    env = app.settings.game_env()
    check(env.get("FTL_NOVSYNC") == "1" and env.get("FTL_FPS") == "1" and env.get("FTL_NOSOUND") == "1" and env.get("FTL_RECORD") == "auto", "les options deviennent des variables d'environnement du jeu : %s" % sorted(env))
    from . import settings as _st
    again = _st.Settings(app.settings.path)
    check(again.get("game", "vsync") == "off" and again.get("game", "record") == "on", "les reglages sont enregistres dans settings.ini et relus")
    app.activate("opt:launcher:language"); check(app.T is i18n.FR, "la langue de l'interface change tout de suite")
    app.activate("opt:launcher:language"); check(app.T is i18n.EN, "... et revient")
    app.settings.cp["game"]["fullscreen"] = "on"; check(app.settings.game_env().get("FTL_FULLSCREEN") == "1", "plein ecran -> FTL_FULLSCREEN")
    app.settings.cp["game"]["fullscreen"] = "off"; check(app.settings.game_env().get("FTL_WINDOWED") == "1", "fenetre -> FTL_WINDOWED")
    # espace disque : sur un dossier jetable uniquement
    for d, f in (("tools/ghidra_test", "a.bin"), ("tools/jdk-test", "b.bin"), ("tools/_dl", "c.zip"), ("build/gen", "d.c"), ("build/ghidra", "e.bin"),
                 ("tools/pylibs", "KEEP1"), ("tools/fonts", "KEEP2"), ("build/game", "KEEP3"), ("build/logs", "KEEP4"), ("ipa", "KEEP5.ipa")):
        os.makedirs(os.path.join(root, d), exist_ok=True); open(os.path.join(root, d, f), "wb").write(b"x" * 1000)
    app.state = "setup"; app.open_options(); t0 = time.time()
    while app.disk_bytes is None and time.time() - t0 < 10: time.sleep(0.1)
    check(app.disk_bytes == 5000, "le calcul de l'espace supprimable ne compte que les outils et fichiers de build (%s octets)" % app.disk_bytes)
    app.activate("free"); frames(); check(app.modal == "free", "LIBERER demande confirmation")
    app.activate("no"); frames(); check(app.modal is None and os.path.exists(os.path.join(root, "tools/ghidra_test")), "NON ne supprime rien")
    app.activate("free"); app.activate("yes"); t0 = time.time()
    while time.time() - t0 < 10 and (app.disk_busy or os.path.exists(os.path.join(root, "tools/ghidra_test"))): time.sleep(0.1)
    gone = not any(os.path.exists(os.path.join(root, d)) for d in ("tools/ghidra_test", "tools/jdk-test", "tools/_dl", "build/gen", "build/ghidra"))
    kept = all(os.path.exists(os.path.join(root, d)) for d in ("tools/pylibs/KEEP1", "tools/fonts/KEEP2", "build/game/KEEP3", "build/logs/KEEP4", "ipa/KEEP5.ipa"))
    check(gone, "OUI supprime les outils telecharges et les fichiers de build"); check(kept, "... mais garde pylibs, polices, le jeu, les journaux et l'IPA")
    app.key_action("back"); frames(); check(app.state == "setup", "Echap quitte les options et revient a l'ecran precedent (%s)" % app.state)
    print("RESULTAT :", "TOUT OK" if ok else "ECHECS")
    return 0 if ok else 1
