#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Reconstruit le port FTL (iPad 1.5.10 -> PC / Linux) a partir de VOTRE copie de l'IPA.

    python build_from_ipa.py --ipa FTL__Faster_Than_Light_1.5.10_ios_6.0.ipa [--out build] [--ghidra DIR] [--jdk DIR]
                             [--pc-ftl-dat "C:/Games/FTL/ftl.dat.vanilla"]   (facultatif : traduction francaise)

Etapes (--only a,b / --skip a,b) :
  extract  : extrait de l'IPA l'executable (Mach-O armv7) et ftl.dat, verifie les SHA-256 (binary.toml)
  ghidra   : importe l'executable dans un projet Ghidra temporaire (analyse automatique) + FixSwitch.java
  lift     : Lift.java traduit le code ARM en C -> <out>/gen (gen_funcs_*.c, memoire du jeu mem_*.bin...)
  patch    : patches_apres_lift.py (correctifs du code genere) + binary_config.h (adresses propres au binaire)
  compile  : compile le code genere et le runtime (Windows : Visual Studio ; Linux : gcc + SDL2)
  game     : assemble <out>/game (executable, ftl.dat, genj/, override/24.font)
  fr       : (avec --pc-ftl-dat) patch francais : textes, polices accentuees, boutons

Rien du jeu n'est dans le depot : tout ce qui est genere reste dans <out> (ignore par git).
Prerequis : Python 3.11+, Ghidra 12.2 (https://ghidra-sre.org), JDK 21+ (Temurin...), et Visual Studio (Build Tools, C++) sous Windows
ou gcc + make + libsdl2-dev + libgl-dev sous Linux. Ghidra et le JDK peuvent aussi etre poses dans ./tools/ (ghidra_*/ et jdk*/).
"""
import argparse, glob, hashlib, os, platform, shutil, subprocess, sys, time, zipfile

try:
    import tomllib
except ImportError:
    sys.exit("Python 3.11 ou plus recent requis (module tomllib).")

ROOT = os.path.dirname(os.path.abspath(__file__))
RECOMP = os.path.join(ROOT, "work", "recomp")
SCRIPTS = os.path.join(ROOT, "work", "scripts")
FRDIR = os.path.join(ROOT, "work", "fr")
IS_WIN = platform.system() == "Windows"
STAGES = ["extract", "ghidra", "lift", "patch", "compile", "game", "fr"]


def log(*a): print("[build]", *a, flush=True)
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
        die("commande en echec (code %d)%s" % (r.returncode, " ; journal : " + logfile if logfile else ""))


# ---------------------------------------------------------------- outils
def find_ghidra(arg):
    cands = [arg, os.environ.get("GHIDRA_INSTALL_DIR")] + sorted(glob.glob(os.path.join(ROOT, "tools", "ghidra_*")))
    for c in cands:
        if c and os.path.isfile(os.path.join(c, "support", "analyzeHeadless" + (".bat" if IS_WIN else ""))): return os.path.abspath(c)
    die("Ghidra introuvable : --ghidra DIR, variable GHIDRA_INSTALL_DIR, ou ./tools/ghidra_12.2_PUBLIC")

def find_jdk(arg):
    cands = [arg, os.environ.get("JAVA_HOME")] + sorted(glob.glob(os.path.join(ROOT, "tools", "jdk*", "jdk*")))
    for c in cands:
        if c and os.path.isfile(os.path.join(c, "bin", "java" + (".exe" if IS_WIN else ""))): return os.path.abspath(c)
    die("JDK introuvable : --jdk DIR ou variable JAVA_HOME (JDK 21 ou plus)")

def check_ghidra_version(ghidra, want):
    p = os.path.join(ghidra, "Ghidra", "application.properties")
    ver = ""
    if os.path.exists(p):
        for l in open(p):
            if l.startswith("application.version="): ver = l.split("=", 1)[1].strip()
    if not ver.startswith(want):
        log("ATTENTION : Ghidra %s trouve, %s attendu (le resultat peut differer)" % (ver or "?", want))


def ghidra_env(c):
    env = dict(os.environ)
    env["JAVA_HOME"] = c.jdk
    env["PATH"] = os.path.join(c.jdk, "bin") + os.pathsep + env.get("PATH", "")
    env["JAVA_TOOL_OPTIONS"] = "-Dapplication.settingsdir=" + os.path.join(c.out, "gsettings")   # n'utilise pas vos reglages Ghidra
    return env

def headless(c, args, logname):
    exe = os.path.join(c.ghidra, "support", "analyzeHeadless" + (".bat" if IS_WIN else ""))
    run([exe, os.path.join(c.out, "ghidra"), "FTL"] + args, logfile=os.path.join(c.out, "logs", logname), env=ghidra_env(c))


# ---------------------------------------------------------------- etapes
def st_extract(c):
    ipa = c.args.ipa
    if not ipa or not os.path.isfile(ipa): die("--ipa FICHIER.ipa requis")
    h = sha256(ipa)
    if h != c.cfg["source"]["ipa_sha256"]:
        if not c.args.skip_hash_check:
            die("SHA-256 de l'IPA inattendu :\n  obtenu  %s\n  attendu %s\nCe port ne fonctionne qu'avec l'IPA FTL 1.5.10 d'origine (--skip-hash-check pour forcer)." % (h, c.cfg["source"]["ipa_sha256"]))
        log("ATTENTION : IPA different de la version attendue")
    d = os.path.join(c.out, "ipa"); os.makedirs(d, exist_ok=True)
    with zipfile.ZipFile(ipa) as z:
        names = z.namelist()
        exe = next((n for n in names if n.count("/") == 2 and n.startswith("Payload/") and n.endswith(".app/FTL")), None)
        dat = next((n for n in names if n.endswith(".app/ftl.dat")), None)
        if not exe or not dat: die("Payload/*.app/FTL ou ftl.dat absent de l'IPA")
        for n, out in ((exe, "FTL"), (dat, "ftl.dat")):
            with z.open(n) as src, open(os.path.join(d, out), "wb") as dst: shutil.copyfileobj(src, dst)
    hb = sha256(os.path.join(d, "FTL"))
    if hb != c.cfg["source"]["binary_sha256"] and not c.args.skip_hash_check:
        die("SHA-256 de l'executable inattendu (%s) : l'IPA est probablement chiffre (FairPlay) ou modifie. Il faut un IPA decrypte." % hb)
    log("extrait :", d)


def st_ghidra(c):
    if done(c, "ghidra") : return log("ghidra : deja fait")
    shutil.rmtree(os.path.join(c.out, "ghidra"), ignore_errors=True); os.makedirs(os.path.join(c.out, "ghidra"))
    headless(c, ["-import", os.path.join(c.out, "ipa", "FTL"), "-overwrite"], "ghidra_import.log")      # analyse automatique par defaut
    headless(c, ["-process", "FTL", "-noanalysis", "-scriptPath", SCRIPTS, "-postScript", "FixSwitch.java"], "ghidra_fixswitch.log")
    mark(c, "ghidra")


def st_lift(c):
    if done(c, "lift"): return log("lift : deja fait")
    gen = os.path.join(c.out, "gen"); shutil.rmtree(gen, ignore_errors=True); os.makedirs(gen)
    L = c.cfg["lift"]
    headless(c, ["-process", "FTL", "-noanalysis", "-scriptPath", SCRIPTS, "-postScript", "Lift.java", gen, L["mode"]] + ["OVERRIDE:" + o for o in L["overrides"]], "ghidra_lift.log")
    if not os.path.exists(os.path.join(gen, "blocks.txt")): die("Lift.java n'a rien produit (voir logs/ghidra_lift.log)")
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
    die("Visual Studio (outils C++ x64) introuvable : installez les Build Tools de Visual Studio")


def st_compile(c):
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
        run(["make", "-f", "Makefile.linux", "-j", str(os.cpu_count() or 2), "GENDIR=" + gen, "OBJDIR=" + obj, "OUT=" + os.path.join(binp, "ftl_linux")],
            cwd=RECOMP, logfile=os.path.join(c.out, "logs", "compile.log"))


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
    log("jeu pret :", g)


def st_fr(c):
    pc = c.args.pc_ftl_dat
    if not pc: return log("fr : ignore (pas de --pc-ftl-dat)")
    g = os.path.join(c.out, "game"); fo = os.path.join(c.out, "fr"); shutil.rmtree(fo, ignore_errors=True)
    en = os.path.join(c.out, "ipa", "ftl.dat")
    py = [sys.executable]
    run(py + [os.path.join(FRDIR, "fr_build.py"), pc, en, fo], cwd=FRDIR)
    run(py + [os.path.join(FRDIR, "make_fonts_fr.py"), en, fo], cwd=FRDIR)
    run(py + [os.path.join(FRDIR, "make_buttons_fr.py"), pc, en, fo], cwd=FRDIR)
    shutil.copy2(en, os.path.join(g, "ftl_en.dat"))
    run(py + [os.path.join(FRDIR, "apply_fr.py"), en, fo, os.path.join(g, "ftl.dat")], cwd=FRDIR)
    log("francais applique : ftl.dat (fr) et ftl_en.dat (anglais) dans", g)


def main():
    try: sys.stdout.reconfigure(errors="replace")
    except Exception: pass
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ipa"); ap.add_argument("--out", default=os.path.join(ROOT, "build"))
    ap.add_argument("--ghidra"); ap.add_argument("--jdk"); ap.add_argument("--pc-ftl-dat")
    ap.add_argument("--only"); ap.add_argument("--skip"); ap.add_argument("--skip-hash-check", action="store_true")
    ap.add_argument("--force", action="store_true", help="refait aussi Ghidra et Lift meme si deja faits")
    c = Ctx(); c.args = ap.parse_args(); c.out = os.path.abspath(c.args.out)
    c.cfg = tomllib.load(open(os.path.join(RECOMP, "binary.toml"), "rb"))
    stages = c.args.only.split(",") if c.args.only else list(STAGES)
    if c.args.skip: stages = [s for s in stages if s not in c.args.skip.split(",")]
    for s in stages:
        if s not in STAGES: die("etape inconnue : " + s)
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
        log("=== etape :", s)
        globals()["st_" + s](c)
    log("termine en %.0f s" % (time.time() - t0))


if __name__ == "__main__":
    main()
