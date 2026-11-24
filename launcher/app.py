# -*- coding: utf-8 -*-
"""Lanceur FTL : ecran de premier lancement (aucun jeu), construction avec progression, puis "jouer".
Entrees : clavier, souris, manette (Steam Deck), glisser-deposer d'un fichier."""
import ctypes, os, subprocess, sys, time
import sdl2

from . import ui as U
from .ui import W, H
from . import scan, build, i18n

STAGE_ORDER = build.STAGES


def fit(ui, s, maxw, size=26):
    """Raccourcit un chemin au milieu pour qu'il tienne dans maxw pixels."""
    if ui.measure(s, size=size)[0] <= maxw: return s
    keep = len(s)
    while keep > 12 and ui.measure(s[:keep // 2] + "..." + s[-(keep // 2):], size=size)[0] > maxw: keep -= 2
    return s[:keep // 2] + "..." + s[-(keep // 2):]


class App:
    def __init__(self, root, ui, T, demo=None, scanning=True):
        self.root, self.ui, self.T = root, ui, T
        self.ipa_dir = os.path.join(root, "ipa")
        os.makedirs(self.ipa_dir, exist_ok=True)
        self.scanner = scan.Scanner(root)
        if scanning: self.scanner.start()
        self.state = "setup"
        self.widgets = []; self.focus = None; self.mouse_moved = False
        self.want_fr = True
        self.runner = None; self.demo = demo
        self.browse = None            # dict : kind, dir, entries, idx, scroll
        self.modal = None             # 'rebuild'
        self.next_rescan = 0.0; self.quit = False; self.launch = None; self.focus_set_once = False
        ok, self.exe, self.game_dir = scan.game_state(root)
        if ok: self.state = "ready"
        self.bg_cache = os.path.join(root, "build", "ui_cache", "menu_bg.bmp"); self.bg_thread = None; self.bg_state = "none"   # none | cutting | ready | failed
        if os.path.isfile(self.bg_cache) and ui.load_photo(self.bg_cache): self.bg_state = "ready"

    # ------------------------------------------------------------ widgets / focus
    def add(self, wid, label, x, y, w, h, enabled=True, primary=False, small=False):
        self.widgets.append((wid, x, y, w, h, enabled))
        self.ui.button(label, x, y, w, h, focus=(self.focus == wid), enabled=enabled, primary=primary, small=small)

    def first_focus(self, wid):
        if self.focus is None or self.focus not in [w[0] for w in self.widgets if w[5]]: self.focus = wid

    def move_focus(self, dx, dy):
        cur = next((w for w in self.widgets if w[0] == self.focus), None)
        cand = [w for w in self.widgets if w[5] and w[0] != self.focus]
        if not cand: return
        if not cur: self.focus = cand[0][0]; return
        cx, cy = cur[1] + cur[3] / 2, cur[2] + cur[4] / 2
        best, bs = None, 1e12
        for w in cand:
            wx, wy = w[1] + w[3] / 2 - cx, w[2] + w[4] / 2 - cy
            main, side = (wx * dx + wy * dy), abs(wx * dy) + abs(wy * dx)
            if main <= 4: continue
            score = main + side * 2.5
            if score < bs: best, bs = w[0], score
        if best: self.focus = best

    def widget_at(self, x, y):
        for w in self.widgets:
            if w[5] and w[1] <= x < w[1] + w[3] and w[2] <= y < w[2] + w[4]: return w[0]
        return None

    # ------------------------------------------------------------ actions
    def activate(self, wid):
        s = self.scanner
        if wid == "quit": self.quit = True
        elif wid == "open_dir": scan.open_folder(self.ipa_dir)
        elif wid == "copy_link": sdl2.SDL_SetClipboardText(scan.IPA_EXAMPLE_URL.encode()); self.copied_until = time.time() + 2.0
        elif wid == "browse_ipa": self.open_browse("ipa")
        elif wid == "browse_pc": self.open_browse("dat")
        elif wid == "use_fr": self.want_fr = not self.want_fr
        elif wid == "skip_pc": self.want_fr = False; s.pc_state = "none" if s.pc_state != "found" else s.pc_state
        elif wid == "build": self.start_build()
        elif wid == "cancel":
            if self.runner: self.runner.cancel()
            self.runner = None; self.state = "setup"; self.focus = None
        elif wid == "retry": self.start_build()
        elif wid == "log":
            if self.runner: scan.open_folder(os.path.dirname(self.runner.log_path))
        elif wid == "back": self.back()
        elif wid == "play": self.play()
        elif wid == "lang": scan.swap_language(self.game_dir)
        elif wid == "rebuild": self.modal = "rebuild"; self.focus = "no"
        elif wid == "yes": self.modal = None; self.state = "setup"; self.focus = None
        elif wid == "no": self.modal = None; self.focus = "play"

    def back(self):
        if self.modal: self.modal = None; self.focus = "play"
        elif self.state == "browse": self.state = "setup"; self.focus = None
        elif self.state in ("error",): self.state = "setup"; self.focus = None

    def start_build(self):
        s = self.scanner
        if s.ipa_state != "ok": return
        pc = s.pc["path"] if (s.pc_state == "found" and self.want_fr and s.pc) else None
        self.runner = build.BuildRunner(self.root, s.ipa_path, pc)
        if self.demo: self.runner.cmd = self.demo
        self.runner.start(); self.state = "build"; self.focus = "cancel"

    def play(self):
        ok, exe, g = scan.game_state(self.root)
        if ok: self.launch = (exe, g)

    # ------------------------------------------------------------ navigateur de fichiers
    def open_browse(self, kind):
        start = self.ipa_dir if kind == "ipa" else os.path.expanduser("~")
        if kind == "dat" and self.scanner.pc and self.scanner.pc.get("path"): start = os.path.dirname(self.scanner.pc["path"])
        self.browse = {"kind": kind, "dir": None, "entries": [], "idx": 0, "scroll": 0}
        self.browse_cd(start if os.path.isdir(start) else os.path.expanduser("~"))
        self.state = "browse"; self.focus = None

    def browse_cd(self, d):
        b = self.browse; b["dir"] = d; ents = []
        if d is None:
            for L in "CDEFGHIJKLMNOPQRSTUVWXYZ":
                if os.path.isdir(L + ":\\"): ents.append((L + ":\\", "dir", L + ":\\"))
        else:
            par = os.path.dirname(d.rstrip("\\/"))
            ents.append((self.T["up"], "up", par if par and par != d else None))
            try:
                names = sorted(os.listdir(d), key=str.lower)
            except OSError:
                names = []
            for n in names:
                p = os.path.join(d, n)
                try:
                    if os.path.isdir(p): ents.append((n + os.sep, "dir", p))
                except OSError: continue
            for n in names:
                p = os.path.join(d, n); low = n.lower()
                ok = low.endswith(".ipa") if b["kind"] == "ipa" else (low.endswith(".dat") or ".dat." in low)
                if ok and os.path.isfile(p): ents.append((n, "file", p))
        b["entries"], b["idx"], b["scroll"] = ents, 0, 0

    def browse_enter(self):
        b = self.browse
        if not b["entries"]: return
        label, kind, p = b["entries"][b["idx"]]
        if kind in ("dir", "up"): self.browse_cd(p)
        elif kind == "file":
            if b["kind"] == "ipa": self.scanner.set_ipa(p)
            else:
                self.scanner.set_pc(p); self.want_fr = self.scanner.pc_state == "found"
            self.state = "setup"; self.focus = None

    # ------------------------------------------------------------ entrees
    def key_action(self, name):
        if self.state == "browse":
            b = self.browse
            if name == "up": b["idx"] = max(0, b["idx"] - 1)
            elif name == "down": b["idx"] = min(len(b["entries"]) - 1, b["idx"] + 1)
            elif name == "pageup": b["idx"] = max(0, b["idx"] - 10)
            elif name == "pagedown": b["idx"] = min(len(b["entries"]) - 1, b["idx"] + 10)
            elif name == "ok": self.browse_enter()
            elif name == "back": self.back()
            elif name == "parent":
                if b["entries"] and b["entries"][0][1] == "up": self.browse_cd(b["entries"][0][2])
            return
        if name in ("up", "down", "left", "right"): self.move_focus({"left": -1, "right": 1}.get(name, 0), {"up": -1, "down": 1}.get(name, 0))
        elif name == "ok" and self.focus: self.activate(self.focus)
        elif name == "back": self.back()

    def handle(self, e):
        t = e.type
        if t == sdl2.SDL_QUIT: self.quit = True
        elif t == sdl2.SDL_KEYDOWN:
            k = e.key.keysym.sym
            m = {sdl2.SDLK_UP: "up", sdl2.SDLK_DOWN: "down", sdl2.SDLK_LEFT: "left", sdl2.SDLK_RIGHT: "right", sdl2.SDLK_RETURN: "ok", sdl2.SDLK_KP_ENTER: "ok",
                 sdl2.SDLK_SPACE: "ok", sdl2.SDLK_ESCAPE: "back", sdl2.SDLK_BACKSPACE: "parent", sdl2.SDLK_PAGEUP: "pageup", sdl2.SDLK_PAGEDOWN: "pagedown"}.get(k)
            if k == sdl2.SDLK_TAB: m = "right"
            if m: self.key_action(m)
        elif t == sdl2.SDL_MOUSEMOTION:
            self.mouse = (e.motion.x, e.motion.y); self.mouse_moved = True
        elif t == sdl2.SDL_MOUSEBUTTONDOWN and e.button.button == sdl2.SDL_BUTTON_LEFT:
            if self.state == "browse" and not self.modal:
                self.browse_click(e.button.x, e.button.y)
            w = self.widget_at(e.button.x, e.button.y)
            if w: self.focus = w; self.activate(w)
        elif t == sdl2.SDL_MOUSEWHEEL and self.state == "browse":
            self.key_action("up" if e.wheel.y > 0 else "down")
        elif t == sdl2.SDL_DROPFILE:
            p = e.drop.file.decode("utf-8", "replace") if e.drop.file else ""
            sdl2.SDL_free(e.drop.file)
            self.on_drop(p)
        elif t == sdl2.SDL_CONTROLLERBUTTONDOWN:
            b = e.cbutton.button
            m = {sdl2.SDL_CONTROLLER_BUTTON_DPAD_UP: "up", sdl2.SDL_CONTROLLER_BUTTON_DPAD_DOWN: "down", sdl2.SDL_CONTROLLER_BUTTON_DPAD_LEFT: "left",
                 sdl2.SDL_CONTROLLER_BUTTON_DPAD_RIGHT: "right", sdl2.SDL_CONTROLLER_BUTTON_A: "ok", sdl2.SDL_CONTROLLER_BUTTON_B: "back",
                 sdl2.SDL_CONTROLLER_BUTTON_LEFTSHOULDER: "pageup", sdl2.SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: "pagedown",
                 sdl2.SDL_CONTROLLER_BUTTON_START: "ok"}.get(b)
            if m: self.key_action(m)
        elif t == sdl2.SDL_CONTROLLERAXISMOTION and e.caxis.axis in (sdl2.SDL_CONTROLLER_AXIS_LEFTX, sdl2.SDL_CONTROLLER_AXIS_LEFTY):
            self.axis_event(e.caxis.axis, e.caxis.value)
        elif t == sdl2.SDL_CONTROLLERDEVICEADDED:
            sdl2.SDL_GameControllerOpen(e.cdevice.which)

    _axis = {}
    def axis_event(self, axis, v):
        old = self._axis.get(axis, 0); new = 0 if abs(v) < 16000 else (1 if v > 0 else -1)
        self._axis[axis] = new
        if new and new != old:
            self.key_action(("right" if new > 0 else "left") if axis == sdl2.SDL_CONTROLLER_AXIS_LEFTX else ("down" if new > 0 else "up"))

    def on_drop(self, p):
        if not p: return
        low = p.lower()
        if low.endswith(".ipa"): self.scanner.set_ipa(p)
        elif low.endswith(".dat") or ".dat." in low: self.scanner.set_pc(p); self.want_fr = self.scanner.pc_state == "found"
        if self.state == "browse": self.state = "setup"
        self.focus = None

    def browse_click(self, x, y):
        b = self.browse
        if not b or not (60 <= x < W - 60 and 200 <= y < 200 + 14 * 34): return
        i = b["scroll"] + (y - 200) // 34
        if 0 <= i < len(b["entries"]):
            if b["idx"] == i: self.browse_enter()
            else: b["idx"] = i

    # ------------------------------------------------------------ images
    def draw(self, dt):
        ui = self.ui; T = self.T
        self.update_background()
        self.widgets = []
        ui.background(dt)
        st = self.state
        if self.runner:
            if self.runner.state == "done" and st == "build": ok, self.exe, self.game_dir = scan.game_state(self.root); self.state = "ready"; self.runner = None; self.focus = "play"; st = "ready"
            elif self.runner.state == "error" and st == "build": self.state = "error"; self.focus = "retry"; st = "error"
        {"setup": self.draw_setup, "browse": self.draw_browse, "build": self.draw_build, "error": self.draw_error, "ready": self.draw_ready}[st](ui, T)
        if self.modal: self.draw_modal(ui, T)
        if self.mouse_moved:
            self.mouse_moved = False
            w = self.widget_at(*self.mouse)
            if w: self.focus = w

    mouse = (0, 0)
    copied_until = 0.0

    def update_background(self):
        """Des que l'IPA est reconnu, on en decoupe l'image du menu du jeu (en arriere-plan) pour en faire le fond d'ecran."""
        s = self.scanner
        if self.bg_state == "none" and s.ipa_state == "ok" and s.ipa_path and os.path.isfile(s.ipa_path):
            self.bg_state = "cutting"
            def job():
                from . import bg
                try: self.bg_state = "cut" if bg.cut_from_ipa(s.ipa_path, self.bg_cache) else "failed"
                except Exception: self.bg_state = "failed"
            self.bg_thread = __import__("threading").Thread(target=job, daemon=True); self.bg_thread.start()
        elif self.bg_state == "cut":
            self.bg_state = "ready" if self.ui.load_photo(self.bg_cache) else "failed"

    def hint(self, text):
        self.ui.text(text, W // 2, H - 34, U.DIM, 24, anchor="c")

    def draw_setup(self, ui, T):
        s = self.scanner
        if time.time() > self.next_rescan: s.rescan_ipa(); self.next_rescan = time.time() + 2.0
        ui.header(T["setup_title"], T["setup_sub"])
        # --- 1. IPA
        ui.panel(40, 128, W - 80, 252)
        ui.title(T["step1"], 66, 150, U.CYAN, 16)
        if s.ipa_state in ("none", "scanning", "bad"):
            if s.ipa_state == "bad":
                ui.text(fit(ui, s.ipa_path or "", W - 170), 66, 184, U.TEXT, 28)
                ui.cross(66, 220); ui.text(T["ipa_bad"], 106, 216, U.RED, 28)
            else:
                ui.text(T["step1_drop"], 66, 180, U.TEXT, 28)
                ui.rect(66, 214, W - 132, 40, (8, 12, 18)); ui.frame(66, 214, W - 132, 40, U.YELLOW, 2)
                ui.text(fit(ui, self.ipa_dir, W - 160), 80, 220, U.YELLOW, 28)
                ui.text(T["scanning"] if s.ipa_state == "scanning" else T["ipa_none"], 66, 258, U.DIM, 26)
            ui.text(T["ipa_example"], 66, 286, U.CYAN, 22)
            ui.text(scan.IPA_EXAMPLE_URL, 66 + ui.measure(T["ipa_example"], size=22)[0] + 12, 286, U.TEXT, 20)
            self.add("open_dir", T["btn_open"], 66, 326, 330, 44, small=True)
            self.add("browse_ipa", T["btn_browse"], 412, 326, 230, 44, small=True)
            self.add("copy_link", T["copied"] if time.time() < self.copied_until else T["btn_copy"], 658, 326, 290, 44, small=True)
        else:
            ui.text(fit(ui, s.ipa_path or "", W - 170), 66, 200, U.TEXT, 28)
            if s.ipa_state == "verifying": ui.spinner(66, 250); ui.text(T["verifying"], 100, 246, U.YELLOW, 28)
            else: ui.check(66, 250); ui.text(T["ipa_ok"], 106, 246, U.GREEN, 28)
            self.add("browse_ipa", T["btn_browse"], 66, 326, 230, 44, small=True)
        # --- 2. FTL PC
        ui.panel(40, 392, W - 80, 134)
        ui.title(T["step2"], 66, 410, U.CYAN, 16)
        if s.pc_state == "found" and s.pc:
            ui.text("%s%s %s" % (T["pc_found"], T["colon"], s.pc.get("store", "")), 66, 440, U.GREEN if self.want_fr else U.DIM, 28)
            ui.text(fit(ui, s.pc["path"], W - 460), 66, 468, U.DIM, 26)
            if s.pc.get("warnings"): ui.text(T["pc_warn"], 66, 494, U.YELLOW, 24)
            self.add("use_fr", ("[X] " if self.want_fr else "[  ] ") + T["use_fr"], W - 400, 434, 330, 40, small=True)
            self.add("browse_pc", T["btn_browse"], W - 400, 480, 330, 36, small=True)
        elif s.pc_state == "bad":
            ui.cross(66, 440); ui.text(T["pc_bad"], 106, 438, U.RED, 28)
            self.add("browse_pc", T["btn_browse"], 66, 482, 230, 36, small=True)
        else:
            ui.text(T["pc_none"] if s.pc_state != "scanning" else T["scanning"], 66, 442, U.DIM, 28)
            self.add("browse_pc", T["btn_browse"], 66, 482, 230, 36, small=True)
        # --- 3. construire
        ui.panel(40, 538, W - 80, 142)
        ui.title(T["step3"], 66, 554, U.CYAN, 16)
        ui.text(T["step3_info"], 66, 584, U.TEXT, 28)
        ready = s.ipa_state == "ok"
        self.add("build", T["btn_build"], 66, 624, 470, 46, enabled=ready, primary=True)
        self.add("quit", T["btn_quit"], W - 330, 624, 260, 46, small=True)
        if ready and not self.focus_set_once: self.focus = "build"; self.focus_set_once = True
        self.first_focus("open_dir" if s.ipa_state in ("none", "scanning") else ("build" if ready else "browse_ipa"))

    def draw_browse(self, ui, T):
        b = self.browse
        ui.header(T["browse_title"], T["browse_ipa"] if b["kind"] == "ipa" else T["browse_dat"])
        ui.panel(40, 128, W - 80, 540)
        ui.text("%s%s %s" % (T["folder"], T["colon"], fit(ui, b["dir"] or "(PC)", W - 260)), 66, 148, U.YELLOW, 28)
        ui.rect(60, 196, W - 120, 14 * 34 + 8, (8, 12, 18), 220)
        if b["idx"] < b["scroll"]: b["scroll"] = b["idx"]
        if b["idx"] >= b["scroll"] + 14: b["scroll"] = b["idx"] - 13
        if not b["entries"]: ui.text(T["empty"], 80, 210, U.DIM, 26)
        for row in range(14):
            i = b["scroll"] + row
            if i >= len(b["entries"]): break
            label, kind, p = b["entries"][i]; y = 200 + row * 34
            if i == b["idx"]: ui.rect(62, y - 2, W - 124, 32, (60, 56, 20)); ui.frame(62, y - 2, W - 124, 32, U.YELLOW, 2)
            col = U.CYAN if kind in ("dir", "up") else U.GREEN
            ui.text(fit(ui, label, W - 200), 80, y, col, 26)
        self.hint(T["hint_nav"])

    def draw_build(self, ui, T):
        r = self.runner
        ui.header(T["build_title"], T["build_sub"])
        ui.panel(40, 128, 760, 420)
        y = 152
        for st in STAGE_ORDER:
            if st == "fr" and not r.pc_dat: continue
            done = st in r.done; cur = (r.stage == st and not done)
            col = U.GREEN if done else (U.YELLOW if cur else U.DIM)
            if done: ui.check(70, y + 4, U.GREEN)
            elif cur: ui.spinner(72, y + 2)
            else: ui.rect(74, y + 12, 8, 8, (70, 80, 90))
            ui.text(T["stage_" + st], 120, y, col, 32)
            y += 52
        ui.panel(820, 128, W - 860, 420)
        el = int(r.elapsed()); ui.title(T["elapsed"], 846, 152, U.CYAN, 14)
        ui.text("%d:%02d" % (el // 60, el % 60), 846, 180, U.TEXT, 44)
        act = r.activity
        if act.startswith("dl "): act = T["download"] + " : " + act[3:]
        elif act.startswith("compile "): act = T["compiling"] + " " + act[8:]
        ui.paragraph(act, 846, 250, W - 910, U.YELLOW, 26)
        for k, line in enumerate(r.lines[-5:]):
            ui.text(fit(ui, line, W - 910, 22), 846, 360 + k * 26, U.DIM, 22)
        p = r.progress()
        ui.bar(40, 566, W - 80, 44, p)
        ui.title("%d %%" % int(p * 100), W // 2, 578, U.TEXT, 16, anchor="c")
        self.add("cancel", T["btn_cancel"], W - 300, 626, 260, 44, small=True)
        self.first_focus("cancel")

    def draw_error(self, ui, T):
        r = self.runner
        ui.header(T["error_title"], T["error_sub"])
        ui.panel(40, 128, W - 80, 430, border=U.RED)
        if r and r.error_stage: ui.title("%s%s %s" % (T["error_at"], T["colon"], T.get("stage_" + r.error_stage, r.error_stage)), 66, 152, U.RED, 16)
        lines = (r.lines[-13:] if r else [])
        for k, line in enumerate(lines): ui.text(fit(ui, line, W - 150, 24), 66, 196 + k * 27, U.TEXT, 24)
        self.add("retry", T["btn_retry"], 66, 590, 300, 52, primary=True)
        self.add("log", T["btn_log"], 390, 590, 360, 52, small=True)
        self.add("back", T["btn_back"], 774, 590, 200, 52, small=True)
        self.add("quit", T["btn_quit"], W - 300, 590, 260, 52, small=True)
        self.first_focus("retry")

    def draw_ready(self, ui, T):
        ui.header(T["ready_title"], T["ready_sub"])
        ui.panel(300, 170, W - 600, 380)
        self.add("play", T["btn_play"], 380, 214, W - 760, 96, primary=True)
        cur, can = scan.langs_available(self.game_dir)
        y = 340
        if can: self.add("lang", T["lang_fr"] if cur == "fr" else T["lang_en"], 380, y, W - 760, 56, small=True); y += 84
        self.add("rebuild", T["btn_rebuild"], 380, y, (W - 760) // 2 - 10, 52, small=True)
        self.add("quit", T["btn_quit"], 380 + (W - 760) // 2 + 10, y, (W - 760) // 2 - 10, 52, small=True)
        ui.text(fit(ui, self.game_dir, W - 700, 24), W // 2, y + 84, U.DIM, 24, anchor="c")
        self.first_focus("play")

    def draw_modal(self, ui, T):
        ui.rect(0, 0, W, H, (0, 0, 0), 170)
        ui.panel(340, 250, W - 680, 200, border=U.YELLOW)
        ui.paragraph(T["confirm_rebuild"], 372, 280, W - 744, U.TEXT, 30)
        self.widgets = []
        self.add("yes", T["yes"], 400, 380, 220, 48, small=True)
        self.add("no", T["no"], W - 620, 380, 220, 48, small=True)
        self.first_focus("no")
