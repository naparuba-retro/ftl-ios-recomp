#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Rebuilds the FTL port (iPad 1.5.10 -> PC / Linux) from YOUR copy of the IPA.

    python build_from_ipa.py --ipa FTL__Faster_Than_Light_1.5.10_ios_6.0.ipa [--out build] [--pc-ftl-dat auto]

Stages (--only a,b / --skip a,b):
  extract  : extracts the Mach-O armv7 executable and ftl.dat from the IPA and checks their SHA-256 (binary.toml)
  ghidra   : imports the executable into a temporary Ghidra project (automatic analysis) + FixSwitch.java
  lift     : Lift.java translates the ARM code to C -> <out>/gen
  patch    : patches_apres_lift.py (fixes to the generated code) + binary_config.h (binary-specific addresses)
  compile  : compiles the generated code and the runtime (Zig by default; --compiler system for Visual Studio / gcc)
  game     : assembles <out>/game (executable, ftl.dat, genj/, override/24.font)
  fr       : (with --pc-ftl-dat) French patch: texts, accented fonts, buttons
  android  : (optional, --android or --only android) builds an Android APK from the result: <out>/android/FTL-Recomp.apk
             (downloads the Android NDK etc., about 1 GB; see android_build.py). Install it on your own phone or tablet.

Nothing from the game is in the repository: everything generated stays in <out> (ignored by git).
Only Python 3.11+ is required: Ghidra, the JDK, the compiler, SDL2 headers... are downloaded into ./tools by bootstrap.py."""
import argparse, glob, hashlib, os, platform, shutil, subprocess, sys, time, zipfile

try:
    import tomllib
except ImportError:
    sys.exit("Python 3.11 ou plus recent requis (module tomllib).")

ROOT = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, ROOT)
import bootstrap   # telechargement verifie des dependances dans ./tools
NO_DL = False
RECOMP = os.path.join(ROOT, "work", "recomp")
SCRIPTS = os.path.join(ROOT, "work", "scripts")
FRDIR = os.path.join(ROOT, "work", "fr")
IS_WIN = platform.system() == "Windows"
STAGES = ["extract", "ghidra", "lift", "patch", "compile", "game", "fr"]
OPTIONAL_STAGES = ["android"]          # only run when asked (--android or --only)


def log(*a): print("[build]", *a, flush=True)


def be_gentle(c):
    """Linux : le build tourne en priorite minimale (nice 19, ionice classe idle) et n'utilise que la moitie des coeurs :
    la machine (ex. un Steam Deck en cours d'utilisation) reste reactive. Les sous-processus heritent de la priorite."""
    c.jobs = c.args.jobs or (os.cpu_count() or 2)
    if IS_WIN or c.args.no_nice: return
    c.jobs = c.args.jobs or max(1, (os.cpu_count() or 2) // 2)
    try: os.nice(19)
    except OSError: pass
    if shutil.which("ionice"):
        try: subprocess.run(["ionice", "-c3", "-p", str(os.getpid())], capture_output=True)
        except OSError: pass
    log("lowest priority (nice 19, ionice idle), %d parallel task(s) (--jobs / --no-nice to change)" % c.jobs)
def die(msg): sys.exit("ERREUR : " + msg)
def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""): h.update(b)
    return h.hexdigest()


class Ctx:
    pass


def stamp(c, name): return os.path.join(c.out, "stamps", name)
def done(c, name): return os.path.exists(stamp(c, name))
def mark(c, name):
    os.makedirs(os.path.dirname(stamp(c, name)), exist_ok=True)
    open(stamp(c, name), "w").write(time.strftime("%Y-%m-%d %H:%M:%S\n"))


def run(cmd, logfile=None, cwd=None, env=None, shell=False):
    """Execute cmd ; sortie redirigee vers logfile (affichee en cas d'echec)."""
    log("$", cmd if isinstance(cmd, str) else " ".join(cmd))
    if logfile:
        os.makedirs(os.path.dirname(logfile), exist_ok=True)
        with open(logfile, "wb") as lf:
            r = subprocess.run(cmd, cwd=cwd, env=env, stdout=lf, stderr=subprocess.STDOUT, shell=shell)
    else:
        r = subprocess.run(cmd, cwd=cwd, env=env, shell=shell)
    if r.returncode != 0:
        if logfile:
            tail = open(logfile, "rb").read().decode("cp850" if IS_WIN else "utf-8", "replace").splitlines()[-25:]
            print("\n".join(tail))
        die("command failed (code %d)%s" % (r.returncode, "; log: " + logfile if logfile else ""))


# ---------------------------------------------------------------- outils
def find_ghidra(arg):
    cands = [arg, os.environ.get("GHIDRA_INSTALL_DIR")] + sorted(glob.glob(os.path.join(bootstrap.TOOLS, "ghidra_*")))
    for c in cands:
        if c and os.path.isfile(os.path.join(c, "support", "analyzeHeadless" + (".bat" if IS_WIN else ""))): return os.path.abspath(c)
    if not NO_DL: return bootstrap.ghidra()
    die("Ghidra not found: --ghidra DIR, GHIDRA_INSTALL_DIR variable, or ./tools/ghidra_12.2_PUBLIC (or drop --no-download)")

def find_jdk(arg):
    cands = [arg, os.environ.get("JAVA_HOME")] + sorted(glob.glob(os.path.join(bootstrap.TOOLS, "jdk*", "jdk*")))
    for c in cands:
        if c and os.path.isfile(os.path.join(c, "bin", "java" + (".exe" if IS_WIN else ""))): return os.path.abspath(c)
    if not NO_DL: return bootstrap.jdk()
    die("JDK not found: --jdk DIR or JAVA_HOME variable (JDK 21 or newer) (or drop --no-download)")

def check_ghidra_version(ghidra, want):
    p = os.path.join(ghidra, "Ghidra", "application.properties")
    ver = ""
    if os.path.exists(p):
        for l in open(p):
            if l.startswith("application.version="): ver = l.split("=", 1)[1].strip()
    if not ver.startswith(want):
        log("WARNING: Ghidra %s found, %s expected (the result may differ)" % (ver or "?", want))


def ghidra_env(c):
    env = dict(os.environ)
    env["JAVA_HOME"] = c.jdk
    env["PATH"] = os.path.join(c.jdk, "bin") + os.pathsep + env.get("PATH", "")
    env["JAVA_TOOL_OPTIONS"] = "-Dapplication.settingsdir=" + os.path.join(c.out, "gsettings") + (" -XX:ActiveProcessorCount=%d" % c.jobs if not IS_WIN else "")   # n'utilise pas vos reglages Ghidra ; limite les threads de la JVM
    return env

def headless(c, args, logname):
    exe = os.path.join(c.ghidra, "support", "analyzeHeadless" + (".bat" if IS_WIN else ""))
    run([exe, os.path.join(c.out, "ghidra"), "FTL"] + args, logfile=os.path.join(c.out, "logs", logname), env=ghidra_env(c))


# ---------------------------------------------------------------- etapes
def st_extract(c):
    ipa = c.args.ipa
    if not ipa or not os.path.isfile(ipa): die("--ipa FILE.ipa required")
    h = sha256(ipa)
    if h != c.cfg["source"]["ipa_sha256"]:
        if not c.args.skip_hash_check:
            die("unexpected IPA SHA-256:\n  got      %s\n  expected %s\nThis port only works with the original FTL 1.5.10 IPA (--skip-hash-check to force)." % (h, c.cfg["source"]["ipa_sha256"]))
        log("WARNING: the IPA differs from the expected version")
    d = os.path.join(c.out, "ipa"); os.makedirs(d, exist_ok=True)
    with zipfile.ZipFile(ipa) as z:
        names = z.namelist()
        exe = next((n for n in names if n.count("/") == 2 and n.startswith("Payload/") and n.endswith(".app/FTL")), None)
        dat = next((n for n in names if n.endswith(".app/ftl.dat")), None)
        if not exe or not dat: die("Payload/*.app/FTL or ftl.dat missing from the IPA")
        for n, out in ((exe, "FTL"), (dat, "ftl.dat")):
            with z.open(n) as src, open(os.path.join(d, out), "wb") as dst: shutil.copyfileobj(src, dst)
    hb = sha256(os.path.join(d, "FTL"))
    if hb != c.cfg["source"]["binary_sha256"] and not c.args.skip_hash_check:
        die("unexpected executable SHA-256 (%s): the IPA is probably encrypted (FairPlay) or modified. A decrypted IPA is required." % hb)
    log("extracted:", d)


def st_ghidra(c):
    if done(c, "ghidra") : return log("ghidra: already done")
    shutil.rmtree(os.path.join(c.out, "ghidra"), ignore_errors=True); os.makedirs(os.path.join(c.out, "ghidra"))
    headless(c, ["-import", os.path.join(c.out, "ipa", "FTL"), "-overwrite"], "ghidra_import.log")      # analyse automatique par defaut
    headless(c, ["-process", "FTL", "-noanalysis", "-scriptPath", SCRIPTS, "-postScript", "FixSwitch.java"], "ghidra_fixswitch.log")
    mark(c, "ghidra")


def st_lift(c):
    if done(c, "lift"): return log("lift: already done")
    gen = os.path.join(c.out, "gen"); shutil.rmtree(gen, ignore_errors=True); os.makedirs(gen)
    L = c.cfg["lift"]
    headless(c, ["-process", "FTL", "-noanalysis", "-scriptPath", SCRIPTS, "-postScript", "Lift.java", gen, L["mode"]] + ["OVERRIDE:" + o for o in L["overrides"]], "ghidra_lift.log")
    if not os.path.exists(os.path.join(gen, "blocks.txt")): die("Lift.java produced nothing (see logs/ghidra_lift.log)")
    mark(c, "lift")


def st_patch(c):
    gen = os.path.join(c.out, "gen")
    run([sys.executable, os.path.join(RECOMP, "patches_apres_lift.py"), gen])
    run([sys.executable, os.path.join(RECOMP, "gen_binary_config.py"), os.path.join(RECOMP, "binary.toml"), os.path.join(c.out, "ipa", "ftl.dat"), gen])


def vcvars():
    vw = os.path.join(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"), "Microsoft Visual Studio", "Installer", "vswhere.exe")
    if os.path.exists(vw):
        r = subprocess.run([vw, "-latest", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"], capture_output=True, text=True)
        p = r.stdout.strip()
        if p:
            v = os.path.join(p, "VC", "Auxiliary", "Build", "vcvars64.bat")
            if os.path.exists(v): return v
    die("Visual Studio (C++ x64 tools) not found: install the Visual Studio Build Tools")


def st_compile_system(c):
    gen = os.path.join(c.out, "gen"); obj = os.path.join(c.out, "obj"); binp = os.path.join(c.out, "bin")
    os.makedirs(binp, exist_ok=True)
    if IS_WIN:
        os.makedirs(os.path.join(obj, "gen"), exist_ok=True); os.makedirs(os.path.join(obj, "rt"), exist_ok=True)
        stubs = os.path.join(obj, "stubs.c")
        rtsrc = "rt.c hle_libc.c hle_string.c hle_sys.c hle_sound.c hle_stream.c hle_thread.c hle_rbtree.c neon.c gl_host.c pvrtc.c main_game.c".split()
        hle = "hle_libc.c hle_string.c hle_sys.c hle_sound.c hle_stream.c hle_thread.c hle_rbtree.c gl_host.c".split()
        r = subprocess.run([sys.executable, "-I", "make_stubs.py", os.path.join(gen, "gen_ext.h")] + hle, cwd=RECOMP, capture_output=True, text=True)
        if r.returncode: die("make_stubs.py : " + r.stderr)
        open(stubs, "w").write(r.stdout)
        q = lambda p: '"%s"' % p
        bat = os.path.join(obj, "build.bat")
        genobj = (os.path.join(obj, "gen") + "/").replace("\\", "/"); rtobj = (os.path.join(obj, "rt") + "/").replace("\\", "/")   # "/" final : un "\\" avant le guillemet l'echapperait
        lines = ["@echo off", 'set "PATH=%ProgramFiles(x86)%\\Microsoft Visual Studio\\Installer;%PATH%"', 'call %s >nul' % q(vcvars()), "cd /d %s" % q(RECOMP),
                 "cl /nologo /c /MP /O2 /W0 /std:c11 /I %s /I . /Fo:%s %s %s" % (q(gen), q(genobj), q(os.path.join(gen, "gen_funcs_*.c")), q(os.path.join(gen, "gen_tab.c")) + " " + q(os.path.join(gen, "gen_names.c"))),
                 "if errorlevel 1 exit /b 1",
                 "cl /nologo /c /MP /O2 /W0 /std:c11 /I %s /I . /Fo:%s %s %s" % (q(gen), q(rtobj), " ".join(rtsrc), q(stubs)),
                 "if errorlevel 1 exit /b 1",
                 "link /nologo /out:%s %s %s /STACK:268435456 winmm.lib dbghelp.lib opengl32.lib user32.lib gdi32.lib" % (q(os.path.join(binp, "ftl_pc.exe")), q(rtobj + "*.obj"), q(genobj + "*.obj"))]
        open(bat, "w").write("\r\n".join(lines) + "\r\n")
        run(["cmd", "/c", bat], logfile=os.path.join(c.out, "logs", "compile.log"))
    else:
        run(["make", "-f", "Makefile.linux", "-j", str(c.jobs), "GENDIR=" + gen, "OBJDIR=" + obj, "OUT=" + os.path.join(binp, "ftl_linux")],
            cwd=RECOMP, logfile=os.path.join(c.out, "logs", "compile.log"))


def find_lib(name):
    for d in ("/usr/lib/x86_64-linux-gnu", "/usr/lib64", "/usr/lib", "/lib/x86_64-linux-gnu", "/lib64", "/usr/local/lib"):
        if os.path.exists(os.path.join(d, name)): return d
    die("library %s not found on this system (the SDL2/OpenGL runtime must be installed; it is on a desktop Linux and on the Steam Deck)" % name)


def st_compile(c):
    """Compile le code genere et le runtime avec Zig (clang embarque), identique sous Windows et Linux, sans rien installer."""
    if c.args.compiler == "system": return st_compile_system(c)
    import concurrent.futures as cf
    zig = bootstrap.zig()
    gen = os.path.join(c.out, "gen"); obj = os.path.join(c.out, "obj"); binp = os.path.join(c.out, "bin")
    for d in (os.path.join(obj, "gen"), os.path.join(obj, "rt"), binp): os.makedirs(d, exist_ok=True)
    target = "x86_64-windows-gnu" if IS_WIN else "x86_64-linux-gnu.2.28"     # glibc 2.28 minimum : fonctionne sur SteamOS et la plupart des Linux
    env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=os.path.join(c.out, "zigcache"), ZIG_LOCAL_CACHE_DIR=os.path.join(c.out, "zigcache"))
    flags = ["-O2", "-w", "-fno-strict-aliasing", "-fwrapv", "-ffp-contract=off", "-fno-delete-null-pointer-checks", "-fno-sanitize=undefined", "-I", gen, "-I", RECOMP]
    if IS_WIN: flags += ["-std=gnu11"]
    else: flags += ["-std=gnu17", "-pthread", "-D_REENTRANT", "-I", bootstrap.sdl2_include()]
    rtsrc = "rt.c hle_libc.c hle_string.c hle_sys.c hle_sound.c hle_stream.c hle_thread.c hle_rbtree.c neon.c gl_host.c pvrtc.c main_game.c".split()
    hle = "hle_libc.c hle_string.c hle_sys.c hle_sound.c hle_stream.c hle_thread.c hle_rbtree.c gl_host.c".split()
    r = subprocess.run([sys.executable, "-I", "make_stubs.py", os.path.join(gen, "gen_ext.h")] + hle, cwd=RECOMP, capture_output=True, text=True)
    if r.returncode: die("make_stubs.py : " + r.stderr)
    stubs = os.path.join(obj, "stubs.c"); open(stubs, "w").write(r.stdout)
    jobs = [(f, os.path.join(obj, "gen", os.path.basename(f)[:-2] + ".o")) for f in sorted(glob.glob(os.path.join(gen, "gen_*.c")))]
    jobs += [(os.path.join(RECOMP, f), os.path.join(obj, "rt", f[:-2] + ".o")) for f in rtsrc] + [(stubs, os.path.join(obj, "rt", "stubs.o"))]
    log("compiling %d files with Zig (%s)" % (len(jobs), target))
    newest_header = max(os.path.getmtime(h) for h in glob.glob(os.path.join(RECOMP, "*.h")) + glob.glob(os.path.join(RECOMP, "*.inc")) + glob.glob(os.path.join(gen, "*.h")))   # a changed header rebuilds everything
    def one(job):
        src, o = job
        if os.path.exists(o) and os.path.getmtime(o) > max(os.path.getmtime(src), newest_header): return None
        r = subprocess.run([zig, "cc", "-target", target] + flags + ["-c", src, "-o", o], env=env, capture_output=True, cwd=RECOMP)
        return None if r.returncode == 0 else (src, r.stderr.decode("utf-8", "replace")[-3000:])
    t0 = time.time(); fails = []
    with cf.ThreadPoolExecutor(c.jobs) as ex:
        for i, res in enumerate(ex.map(one, jobs)):
            if res: fails.append(res)
            if (i + 1) % 10 == 0: log("   %d / %d (%.0f s)" % (i + 1, len(jobs), time.time() - t0))
    if fails:
        for src, err in fails[:3]: print("--- " + src + "\n" + err)
        die("%d file(s) failed" % len(fails))
    exe = os.path.join(binp, "ftl_pc.exe" if IS_WIN else "ftl_linux")
    link = [zig, "cc", "-target", target, "-o", exe] + [o for _, o in jobs]
    if IS_WIN: link += ["-lopengl32", "-luser32", "-lgdi32", "-lwinmm", "-ldbghelp", "-Wl,--stack,268435456"]
    else:
        link += ["-s", "-pthread", "-lm", "-ldl", "-Wl,--allow-shlib-undefined"]   # -s : sans symboles de debug (64 Mo -> ~10 Mo)
        for lib in ("libSDL2-2.0.so.0", "libGL.so.1"): link += ["-L" + find_lib(lib), "-l:" + lib]
    run(link, logfile=os.path.join(c.out, "logs", "link.log"), env=env)
    log("executable:", exe)


def st_game(c):
    g = os.path.join(c.out, "game"); gen = os.path.join(c.out, "gen")
    os.makedirs(os.path.join(g, "genj"), exist_ok=True); os.makedirs(os.path.join(g, "override"), exist_ok=True)
    exe = "ftl_pc.exe" if IS_WIN else "ftl_linux"
    shutil.copy2(os.path.join(c.out, "bin", exe), os.path.join(g, exe))
    for f in ["blocks.txt"] + [os.path.basename(p) for p in glob.glob(os.path.join(gen, "mem_*.bin"))]:
        shutil.copy2(os.path.join(gen, f), os.path.join(g, "genj", f))
    shutil.copy2(os.path.join(c.out, "ipa", "ftl.dat"), os.path.join(g, "ftl.dat"))
    # police du nom de vaisseau (accents) : override/24.font
    sys.path.insert(0, FRDIR)
    from pkg import Pkg
    k = Pkg(os.path.join(c.out, "ipa", "ftl.dat"))
    e = next(x for x in k.ents if x[0] == c.cfg["ftl_dat"]["font24_name"])
    src = os.path.join(c.out, "ipa", "24.font"); open(src, "wb").write(k.read(e))
    run([sys.executable, os.path.join(RECOMP, "make_font24.py"), src, os.path.join(g, "override", "24.font")])
    if IS_WIN:
        open(os.path.join(g, "lancer_ftl.bat"), "w").write('@echo off\r\ncd /d "%~dp0"\r\nftl_pc.exe .\r\n')
    else:
        for n in ("run.sh", "enregistrer.sh", "rejouer.sh", "langue.sh"): shutil.copy2(os.path.join(ROOT, "steamdeck", n), os.path.join(g, n))
        for n in ("ftl_linux", "run.sh", "enregistrer.sh", "rejouer.sh", "langue.sh"): os.chmod(os.path.join(g, n), 0o755)   # droits perdus par git archive / zip
    log("game ready:", g)


def st_android(c):
    import android_build
    android_build.build_apk(c.out, tuple(c.args.android_abi.split(",")), c.jobs)


def st_fr(c):
    pc = c.args.pc_ftl_dat
    if not pc: return log("fr: skipped (no --pc-ftl-dat; use --pc-ftl-dat auto to look for your PC copy of FTL)")
    import detect_pc_ftl
    if pc == "auto":
        b = detect_pc_ftl.best()
        if not b: return log("fr: skipped, no PC installation of FTL with French text found (pass --pc-ftl-dat <path>)")
        pc = b["path"]; log("PC copy of FTL found (%s): %s" % (b["store"], pc))
    else:
        ok, msg = detect_pc_ftl.check_file(pc)
        if not ok: die("--pc-ftl-dat: " + msg)
        log("--pc-ftl-dat: " + msg)
    g = os.path.join(c.out, "game"); fo = os.path.join(c.out, "fr"); shutil.rmtree(fo, ignore_errors=True)
    en = os.path.join(c.out, "ipa", "ftl.dat")
    py = [sys.executable]
    env = dict(os.environ, PYTHONPATH=bootstrap.pylibs() + os.pathsep + os.environ.get("PYTHONPATH", ""))
    run(py + [os.path.join(FRDIR, "fr_build.py"), pc, en, fo], cwd=FRDIR, env=env)
    run(py + [os.path.join(FRDIR, "make_fonts_fr.py"), en, fo], cwd=FRDIR, env=env)
    run(py + [os.path.join(FRDIR, "make_buttons_fr.py"), pc, en, fo], cwd=FRDIR, env=env)
    shutil.copy2(en, os.path.join(g, "ftl_en.dat"))
    run(py + [os.path.join(FRDIR, "apply_fr.py"), en, fo, os.path.join(g, "ftl.dat")], cwd=FRDIR, env=env)
    log("French applied: ftl.dat (French) and ftl_en.dat (English) in", g)


def main():
    try: sys.stdout.reconfigure(errors="replace")
    except Exception: pass
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ipa"); ap.add_argument("--out", default=os.path.join(bootstrap.DATA, "build"))
    ap.add_argument("--ghidra"); ap.add_argument("--jdk"); ap.add_argument("--pc-ftl-dat", help="ftl.dat of your PC copy of FTL (official French), or \"auto\" to look for it")
    ap.add_argument("--compiler", choices=["zig", "system"], default="zig", help="zig (downloaded, default) or the system compiler (Visual Studio / gcc+make)")
    ap.add_argument("--no-download", action="store_true", help="download nothing: use --ghidra/--jdk or ./tools")
    ap.add_argument("--jobs", type=int, help="number of parallel tasks (default: all cores; half of them on Linux)")
    ap.add_argument("--no-nice", action="store_true", help="Linux: do not lower the build priority")
    ap.add_argument("--only"); ap.add_argument("--skip"); ap.add_argument("--skip-hash-check", action="store_true")
    ap.add_argument("--android", action="store_true", help="also build an Android APK (stage android: downloads the NDK, about 1 GB)")
    ap.add_argument("--android-abi", default="arm64-v8a", help="APK architectures, comma list: arm64-v8a (phones, tablets), x86_64 (emulator)")
    ap.add_argument("--force", action="store_true", help="redo Ghidra and Lift even if already done")
    c = Ctx(); c.args = ap.parse_args(); c.out = os.path.abspath(c.args.out)
    for k in ("ipa", "pc_ftl_dat", "ghidra", "jdk"):      # chemins absolus : les sous-processus changent de dossier
        v = getattr(c.args, k)
        if v and v != "auto": setattr(c.args, k, os.path.abspath(v))
    global NO_DL
    NO_DL = c.args.no_download
    be_gentle(c)
    c.cfg = tomllib.load(open(os.path.join(RECOMP, "binary.toml"), "rb"))
    stages = c.args.only.split(",") if c.args.only else list(STAGES) + (OPTIONAL_STAGES if c.args.android else [])
    if c.args.skip: stages = [s for s in stages if s not in c.args.skip.split(",")]
    for s in stages:
        if s not in STAGES + OPTIONAL_STAGES: die("unknown stage: " + s)
    if c.args.force:
        for s in ("ghidra", "lift"):
            if os.path.exists(stamp(c, s)): os.remove(stamp(c, s))
    if any(s in stages for s in ("ghidra", "lift")):
        c.ghidra = find_ghidra(c.args.ghidra); c.jdk = find_jdk(c.args.jdk)
        check_ghidra_version(c.ghidra, c.cfg["source"]["ghidra_version"])
        log("Ghidra :", c.ghidra); log("JDK    :", c.jdk)
    os.makedirs(c.out, exist_ok=True)
    t0 = time.time()
    for s in stages:
        log("=== stage :", s)
        globals()["st_" + s](c)
    log("done in %.0f s" % (time.time() - t0))


if __name__ == "__main__":
    main()
