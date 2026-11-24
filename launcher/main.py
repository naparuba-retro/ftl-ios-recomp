# -*- coding: utf-8 -*-
"""Fenetre, boucle principale, mode captures (--shots) et demonstration (--demo)."""
import os, subprocess, sys, tempfile, time
import sdl2

from . import ui as U, app as A, i18n, scan, build
from .ui import W, H


def make_window(fullscreen):
    sdl2.SDL_SetHint(sdl2.SDL_HINT_RENDER_SCALE_QUALITY, b"linear")
    flags = sdl2.SDL_WINDOW_SHOWN | sdl2.SDL_WINDOW_RESIZABLE | sdl2.SDL_WINDOW_ALLOW_HIGHDPI
    if fullscreen: flags |= sdl2.SDL_WINDOW_FULLSCREEN_DESKTOP
    win = sdl2.SDL_CreateWindow(b"FTL", sdl2.SDL_WINDOWPOS_CENTERED, sdl2.SDL_WINDOWPOS_CENTERED, W, H, flags)
    r = sdl2.SDL_CreateRenderer(win, -1, sdl2.SDL_RENDERER_ACCELERATED | sdl2.SDL_RENDERER_PRESENTVSYNC) or sdl2.SDL_CreateRenderer(win, -1, sdl2.SDL_RENDERER_SOFTWARE)
    sdl2.SDL_RenderSetLogicalSize(r, W, H)
    return win, r


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
    real_ipa = None
    if demo:                                           # dossier jetable : rien d'ecrit dans le vrai dossier
        real_ipa = (scan.find_ipas(root) or [None])[0]   # si ton IPA existe, la demonstration en decoupe vraiment le fond
        root = tempfile.mkdtemp(prefix="ftl_demo_"); os.environ["FTL_FAKE_ROOT"] = root
    if shots: return make_shots(root, font_dir, T, shots)
    if selftest: return selftest_run(root, font_dir, T)
    sdl2.SDL_Init(sdl2.SDL_INIT_VIDEO | sdl2.SDL_INIT_GAMECONTROLLER | sdl2.SDL_INIT_EVENTS)
    sdlttf_init.init()
    fullscreen = fullscreen or bool(os.environ.get("SteamDeck") or os.environ.get("SteamGamepadUI"))
    win, r = make_window(fullscreen)
    ui = U.UI(r, font_dir)
    cmd = [sys.executable, os.path.join(os.path.dirname(__file__), "fake_build.py")] + (["fail"] if demo == "fail" else []) if demo else None
    app = A.App(root, ui, T, demo=cmd, scanning=not demo)
    demo_t0 = time.time()
    if demo:                                     # demonstration : on part de "rien trouve", l'IPA "arrive" apres 6 s
        sc = app.scanner; sc._busy = True; app.next_rescan = 1e18; sc.ipa_state, sc.pc_state = "none", "none"
        demo_pc = {"path": "C:/Games/FTL Advanced Edition/ftl.dat.vanilla", "store": "GOG", "warnings": [], "languages": ["fr"], "vanilla": True}
    for i in range(sdl2.SDL_NumJoysticks()):
        if sdl2.SDL_IsGameController(i): sdl2.SDL_GameControllerOpen(i)
    ev = sdl2.SDL_Event(); last = time.time()
    n = 0; phase = 0; t_auto = time.time(); last_pct = -1
    def log(m): print("[autotest %5.0fs] %s" % (time.time() - t_auto, m), flush=True)
    while not app.quit and not app.launch:
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
        exe, g = app.launch
        sdl2.SDL_HideWindow(win)
        try: rc = subprocess.call([exe, "."], cwd=g)
        except OSError as e: print("lancement impossible :", e); rc = 1
        sdl2.SDL_Quit(); return rc
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
    print("RESULTAT :", "TOUT OK" if ok else "ECHECS")
    return 0 if ok else 1
