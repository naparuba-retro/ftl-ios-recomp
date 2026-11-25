# -*- coding: utf-8 -*-
"""Windowless modes, used by RecompLauncher (see recomplauncher.json) and handy from a terminal. Python standard library only.

    play.py --prepare --ipa FILE [--pc-ftl-dat FILE] [--data DIR]   build the game (no window; prints "progress 0.4 Text" lines)
    play.py --play [--fullscreen|--windowed] [--vsync|--no-vsync] [--fps|--no-fps] [--sound|--no-sound] [--record|--no-record] [--data DIR]
                                                                  start the built game with settings.ini (options given here win)

Exit codes: 0 ok; 1 the build failed; 2 a wrong or missing input file; 3 (--play) the game is not built yet.
Nothing changes for the graphical launcher: these modes only exist when --prepare or --play is given."""
import hashlib, json, os, subprocess, sys, time

from . import build, scan, settings

CODE = build.CODE
MARKER = os.path.join("build", "game", ".built.json")          # written once a build has fully finished
PIPELINE = ("build_from_ipa.py", "work/recomp", "work/scripts", "work/fr", "steamdeck")


def say(text): print(text, flush=True)
def progress(p, text): say("progress %.3f %s" % (max(0.0, min(1.0, p)), text))


def pipeline_key():
    """Hash of everything that decides what a build produces: a new release that doesn't change it needs no new build."""
    h = hashlib.sha256()
    for entry in PIPELINE:
        p = os.path.join(CODE, entry)
        files = [p] if os.path.isfile(p) else sorted(os.path.join(d, f) for d, _, fs in os.walk(p) for f in fs if not f.endswith(".pyc"))
        for f in files:
            h.update(os.path.relpath(f, CODE).replace(os.sep, "/").encode()); h.update(b"\0")
            with open(f, "rb") as fh: h.update(fh.read().replace(b"\r\n", b"\n"))
    return h.hexdigest()


def read_marker(data):
    try:
        with open(os.path.join(data, MARKER), encoding="utf-8") as f: return json.load(f)
    except (OSError, ValueError):
        return None


def prepare(data, ipa, pc):
    if not ipa or not os.path.isfile(ipa):
        say("The IPA was not found: %s" % ipa); return 2
    progress(0.0, "Checking your IPA")
    if scan.check_ipa(ipa) != "ok":
        say("This is not the FTL 1.5.10 iPad IPA this port needs (expected SHA-256 %s)." % scan.IPA_SHA256); return 2
    if pc:
        import detect_pc_ftl
        info = detect_pc_ftl.inspect(pc)
        if not info or not info["has_fr"]:
            say("This ftl.dat has no French text (data/text-fr.xml): %s" % pc); return 2
    key = {"pipeline": pipeline_key(), "ipa": scan.IPA_SHA256, "french": bool(pc), "pc_size": os.path.getsize(pc) if pc else 0}
    if read_marker(data) == key and scan.game_state(data)[0]:
        progress(1.0, "Already built"); return 0
    marker = os.path.join(data, MARKER)
    if os.path.exists(marker): os.remove(marker)                # a build cut short must never look finished
    runner = build.BuildRunner(data, ipa, pc)
    runner.start()
    last, last_t = -1, 0.0
    while runner.state == "running":
        time.sleep(1.0)
        p = runner.progress(); label = (runner.stage or "starting") + (" - " + runner.activity if runner.activity else "")
        if int(p * 100) != last or time.time() - last_t > 120:    # also a line now and then on long steps: silence for an hour counts as stuck
            progress(p, label); last, last_t = int(p * 100), time.time()
    if runner.state != "done":
        say("The build failed at stage %s. Last lines:" % runner.error_stage)
        for line in runner.lines[-25:]: say("  " + line)
        say("Full log: %s" % runner.log_path); return 1
    os.makedirs(os.path.dirname(marker), exist_ok=True)
    with open(marker, "w", encoding="utf-8") as f: json.dump(key, f)
    progress(1.0, "Done"); return 0


# command-line options (RecompLauncher passes them from the game's page): each one overrides settings.ini for that start
QUICK_CRASH = (11, -11, 139)       # SIGSEGV as seen by the parent, on Linux
QUICK_SECONDS = 15.0


def run_game(cmd, cwd, env, log=say):
    """Start the game and wait for it. A crash in the first seconds (seen once on the Steam Deck, in the system SDL3 audio, never again
    in many tries) is retried once: the second start is normal. Any other exit code is returned as it is."""
    for attempt in (1, 2):
        t0 = time.time()
        try:
            rc = subprocess.call(cmd, cwd=cwd, env=env)
        except OSError as e:
            log("Cannot start the game: %s" % e); return 1
        if attempt == 1 and rc in QUICK_CRASH and time.time() - t0 < QUICK_SECONDS:
            log("The game crashed %.1f s after starting (code %s): starting it once more." % (time.time() - t0, rc)); continue
        return rc


FLAGS = {"--fullscreen": ("game", "fullscreen", "on"), "--windowed": ("game", "fullscreen", "off"),
         "--vsync": ("game", "vsync", "on"), "--no-vsync": ("game", "vsync", "off"),
         "--fps": ("game", "show_fps", "on"), "--no-fps": ("game", "show_fps", "off"),
         "--sound": ("game", "sound", "on"), "--no-sound": ("game", "sound", "off"),
         "--record": ("game", "record", "on"), "--no-record": ("game", "record", "off")}


def play(data, flags):
    ok, exe, g = scan.game_state(data)
    if not ok:
        say("The game is not built yet. Set it up first (RecompLauncher: Set Up Again; or run play.py)."); return 3
    path = os.path.join(data, "settings.ini")
    st = settings.Settings(path)
    if not os.path.exists(path): st.save()                       # first start: write the readable, commented file
    for f in flags:
        if f in FLAGS: st.cp[FLAGS[f][0]][FLAGS[f][1]] = FLAGS[f][2]
    if not any(f in flags for f in ("--fullscreen", "--windowed")) and st.get("game", "fullscreen") == "auto"             and os.environ.get("RECOMPLAUNCHER_MODE") == "bigpicture":
        st.cp["game"]["fullscreen"] = "on"                       # Big Picture (couch, Steam Deck): start full screen
    env = dict(os.environ, **st.game_env())
    return run_game([exe, "."], g, env)


def main(argv, data):
    """Returns an exit code, or None when argv asks for neither mode (the graphical launcher then starts as usual)."""
    if "--prepare" in argv:
        val = lambda name: next((a.split("=", 1)[1] if "=" in a else argv[i + 1] for i, a in enumerate(argv)
                                 if (a == name and i + 1 < len(argv)) or a.startswith(name + "=")), None)
        ipa, pc = val("--ipa"), val("--pc-ftl-dat")
        return prepare(data, os.path.abspath(ipa) if ipa else None, os.path.abspath(pc) if pc else None)
    if "--play" in argv:
        return play(data, argv)
    return None
