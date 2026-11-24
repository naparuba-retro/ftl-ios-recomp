# -*- coding: utf-8 -*-
"""Recupere TOUTES les dependances de la reconstruction dans ./tools/ (ignore par git) : rien a telecharger ni installer a la main.

Chaque archive est telechargee depuis son depot officiel et verifiee par SHA-256 (epingle ici) avant d'etre decompressee.
  - Ghidra 12.2           (NSA, Apache-2.0)        : analyse du binaire iOS
  - JDK Temurin 25        (Eclipse Adoptium)       : fait tourner Ghidra
  - Zig 0.17.1            (compilateur C portable) : compile le code genere et le runtime (Windows et Linux)
  - SDL2 2.32.4 (en-tetes)                         : Linux seulement (le runtime SDL2 est deja present sur un Linux de bureau / le Steam Deck)
  - Pillow, fontTools     (PyPI, roues)            : traduction francaise (images, polices)
Seul Python 3.11+ est suppose present (c'est lui qui execute ce script).
"""
import hashlib, io, os, platform, shutil, sys, tarfile, time, urllib.request, zipfile, json

ROOT = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.join(ROOT, "tools")
IS_WIN = platform.system() == "Windows"
OSK = "windows" if IS_WIN else "linux"

DEPS = {
    "ghidra": {"url": "https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_12.2_build/ghidra_12.2_PUBLIC_20261016.zip",
               "sha256": "2c2cd776db3c1f6a097ba418922378ac3649cadd5e844a6f5f088ae628a7be67", "dir": "ghidra_12.2_PUBLIC"},
    "jdk": {
        "windows": {"url": "https://github.com/adoptium/temurin25-binaries/releases/download/jdk-25.0.5.1%2B2/OpenJDK25U-jdk_x64_windows_hotspot_25.0.5.1_2.zip",
                    "sha256": "849d380ab0a64d9098e7d3a2de28beb73841c3b255c8284f019ccd8748c5eb5b", "dir": "jdk-25.0.5.1+2"},
        "linux": {"url": "https://github.com/adoptium/temurin25-binaries/releases/download/jdk-25.0.5.1%2B2/OpenJDK25U-jdk_x64_linux_hotspot_25.0.5.1_2.tar.gz",
                  "sha256": "760f91b22c3efc9bd1b64ff950598231be82928cc807e30691a0e7a2995a62c0", "dir": "jdk-25.0.5.1+2"}},
    "zig": {
        "windows": {"url": "https://ziglang.org/download/0.17.1/zig-x86_64-windows-0.17.1.zip",
                    "sha256": "b03f477a1943673e02aca0f570e8030dffd536ce1f45cbc4d3326ec31159562e", "dir": "zig-x86_64-windows-0.17.1"},
        "linux": {"url": "https://ziglang.org/download/0.17.1/zig-x86_64-linux-0.17.1.tar.xz",
                  "sha256": "3581863e3cbcfd0fce0efc682ce7e9e93da96668a977791d356a26d2a7e9b17a", "dir": "zig-x86_64-linux-0.17.1"}},
    # Zig sur PyPI (paquet "ziglang" : memes binaires 0.17.1) : le CDN de PyPI est ~170x plus rapide que ziglang.org (mesure : 25 Mo/s contre 0,14 Mo/s)
    "zig_pypi": {
        "windows": {"url": "https://files.pythonhosted.org/packages/c8/6c/2013114fa780f0cf490cdc98bdd5bf7dce706a5dbd86e50ec4132cf7d4b1/ziglang-0.17.1-py3-none-win_amd64.whl",
                    "sha256": "69c3dfc3aff6a297ec4017aa904782c0e450eb988982d7f675d83edce27526fb", "dir": "zig-pypi-0.17.1"},
        "linux": {"url": "https://files.pythonhosted.org/packages/59/e3/2a34676370a11c36b7cd04ffb2439c1089c0530cf4b01408c77cf7f8d9c3/ziglang-0.17.1-py3-none-manylinux_2_12_x86_64.manylinux2010_x86_64.musllinux_1_1_x86_64.whl",
                  "sha256": "ea6cc5d87b033cf0aedfbc8f9549a908cae6c50ac31f1698188fa48a74524b54", "dir": "zig-pypi-0.17.1"}},
    "sdl2": {"url": "https://github.com/libsdl-org/SDL/releases/download/release-2.32.4/SDL2-2.32.4.tar.gz",
             "sha256": "f15b478253e1ff6dac62257ded225ff4e7d0c5230204ac3450f1144ee806f934", "dir": "SDL2-2.32.4"},
}
PYPI = {"pillow": "13.0.0", "fonttools": "4.68.0"}     # versions epinglees ; le SHA-256 vient des metadonnees PyPI (HTTPS)
UI_PYPI = {"pysdl2": "0.9.17", "pysdl2-dll": "2.32.10"}  # interface graphique du lanceur : SDL2 (fenetre, manette) + SDL2_ttf
FONTS = {   # polices libres (SIL OFL 1.1), telechargees et non redistribuees : depot google/fonts, commit epingle
    "PressStart2P-Regular.ttf": {"url": "https://raw.githubusercontent.com/google/fonts/83c62e30d87daa70216dab6b984d00d20d96d585/ofl/pressstart2p/PressStart2P-Regular.ttf",
                                 "sha256": "034c77f1f05ec89421e4a63f0e3a4ca1ecf852cc6d2bf611f126f275728e017d"},
    "VT323-Regular.ttf": {"url": "https://raw.githubusercontent.com/google/fonts/83c62e30d87daa70216dab6b984d00d20d96d585/ofl/vt323/VT323-Regular.ttf",
                          "sha256": "cf4de751ada78ceac033dbe16a687742939995b77bc2a052ae17a4957958594d"},
}


def log(*a): print("[bootstrap]", *a, flush=True)


def _sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""): h.update(b)
    return h.hexdigest()


def _get(url, dest, sha256):
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    if os.path.exists(dest) and _sha256(dest) == sha256: return dest
    for attempt in range(3):
        try:
            log("telechargement", url)
            req = urllib.request.Request(url, headers={"User-Agent": "ftl-build-bootstrap"})
            t0, n = time.time(), 0
            with urllib.request.urlopen(req, timeout=60) as r, open(dest + ".part", "wb") as f:
                tot = int(r.headers.get("Content-Length") or 0)
                for chunk in iter(lambda: r.read(1 << 20), b""):
                    f.write(chunk); n += len(chunk)
                    if tot and (n // (1 << 20)) % 50 == 0: log("   %d / %d Mo" % (n >> 20, tot >> 20))
            got = _sha256(dest + ".part")
            if got != sha256: raise RuntimeError("SHA-256 inattendu : %s (attendu %s)" % (got, sha256))
            os.replace(dest + ".part", dest)
            log("   ok (%d Mo en %.0f s)" % (n >> 20, time.time() - t0))
            return dest
        except Exception as e:
            log("   echec (%s)%s" % (e, ", nouvel essai" if attempt < 2 else ""))
            time.sleep(2)
    sys.exit("ERREUR : impossible de telecharger " + url)


def _extract(archive, dest_parent):
    os.makedirs(dest_parent, exist_ok=True)
    if archive.endswith((".zip", ".whl")):   # une roue Python est un zip
        with zipfile.ZipFile(archive) as z:
            base = os.path.realpath(dest_parent)
            for n in z.namelist():
                if not os.path.realpath(os.path.join(dest_parent, n)).startswith(base): sys.exit("archive suspecte : " + n)
            z.extractall(dest_parent)
            if not IS_WIN:                        # zipfile ne restaure pas les droits : Ghidra a besoin de ses scripts et du decompilateur executables
                for i in z.infolist():
                    mode = (i.external_attr >> 16) & 0o777
                    if mode and not i.is_dir(): os.chmod(os.path.join(dest_parent, i.filename), mode | 0o600)
    else:
        with tarfile.open(archive) as t:
            try: t.extractall(dest_parent, filter="data")
            except TypeError: t.extractall(dest_parent)


def _ensure(name, spec, marker):
    d = os.path.join(TOOLS, spec["dir"])
    if os.path.exists(os.path.join(d, *marker)): return d
    arch = _get(spec["url"], os.path.join(TOOLS, "_dl", os.path.basename(spec["url"].split("?")[0]).replace("%2B", "+")), spec["sha256"])
    log("extraction", name)
    shutil.rmtree(d, ignore_errors=True)
    _extract(arch, TOOLS)
    if not os.path.exists(os.path.join(d, *marker)): sys.exit("ERREUR : %s : contenu inattendu apres extraction dans %s" % (name, d))
    return d


def ghidra():
    return _ensure("Ghidra", DEPS["ghidra"], ["support", "analyzeHeadless" + (".bat" if IS_WIN else "")])

def jdk():
    return _ensure("JDK", DEPS["jdk"][OSK], ["bin", "java" + (".exe" if IS_WIN else "")])

def zig():
    ext = ".exe" if IS_WIN else ""
    spec = DEPS["zig_pypi"][OSK]; d = os.path.join(TOOLS, spec["dir"]); exe = os.path.join(d, "ziglang", "zig" + ext)
    if os.path.exists(exe): return exe
    off = os.path.join(TOOLS, DEPS["zig"][OSK]["dir"], "zig" + ext)           # deja telecharge depuis ziglang.org
    if os.path.exists(off): return off
    try:                                                                         # 1) roue PyPI (rapide)
        arch = _get(spec["url"], os.path.join(TOOLS, "_dl", os.path.basename(spec["url"])), spec["sha256"])
        shutil.rmtree(d, ignore_errors=True); log("extraction Zig (PyPI)"); _extract(arch, d)
        if not os.path.exists(exe): raise RuntimeError("zig absent de la roue")
        if not IS_WIN: os.chmod(exe, 0o755)
        return exe
    except (SystemExit, Exception) as e:
        log("PyPI indisponible (%s) : repli sur ziglang.org" % e)
    d = _ensure("Zig", DEPS["zig"][OSK], ["zig" + ext])                        # 2) site officiel
    return os.path.join(d, "zig" + ext)

def sdl2_include():
    """Dossier a passer en -I : contient SDL2/SDL.h (Linux)."""
    d = _ensure("SDL2 (en-tetes)", DEPS["sdl2"], ["include", "SDL.h"])
    out = os.path.join(TOOLS, "sdl2_include")
    if not os.path.exists(os.path.join(out, "SDL2", "SDL.h")):
        shutil.rmtree(out, ignore_errors=True); shutil.copytree(os.path.join(d, "include"), os.path.join(out, "SDL2"))
    return out

def pylibs(packages=None, name="base"):
    """Roues PyPI decompressees dans tools/pylibs ; renvoie ce dossier (a mettre dans PYTHONPATH). Par defaut : Pillow et fontTools (patch francais)."""
    packages = packages or PYPI
    d = os.path.join(TOOLS, "pylibs"); ok = os.path.join(d, ".ok-%s-%s-%d%d" % (name, OSK, sys.version_info[0], sys.version_info[1]))
    if os.path.exists(ok): return d
    tag = "cp%d%d" % sys.version_info[:2]
    for pkg, ver in packages.items():
        meta = json.load(urllib.request.urlopen("https://pypi.org/pypi/%s/%s/json" % (pkg, ver), timeout=60))
        files = meta["urls"]
        def pick():
            pure = [f for f in files if f["filename"].endswith("none-any.whl")]
            if pure: return pure[0]
            plat = lambda n: ("win_amd64" in n) if IS_WIN else ("manylinux" in n and "x86_64" in n)
            cands = [f for f in files if f["filename"].endswith(".whl") and ("-%s-%s-" % (tag, tag) in f["filename"] or "-py2.py3-none-" in f["filename"] or "-py3-none-" in f["filename"])
                     and plat(f["filename"])]
            return cands[0] if cands else None
        f = pick()
        if not f: sys.exit("ERREUR : aucune roue %s %s pour Python %s sur %s" % (pkg, ver, tag, OSK))
        w = _get(f["url"], os.path.join(TOOLS, "_dl", f["filename"]), f["digests"]["sha256"])
        log("installation locale de", f["filename"])
        _extract(w, d)
    open(ok, "w").write("ok\n")
    return d


def ui_libs():
    """pysdl2 + SDL2 (roues PyPI) pour l'interface du lanceur ; renvoie le dossier a ajouter a sys.path."""
    return pylibs(UI_PYPI, "ui")

def fonts():
    """Polices de l'interface (telechargees, SHA-256 verifie) dans tools/fonts ; renvoie le dossier."""
    d = os.path.join(TOOLS, "fonts")
    for name, spec in FONTS.items(): _get(spec["url"], os.path.join(d, name), spec["sha256"])
    return d


if __name__ == "__main__":
    for fn in (ghidra, jdk, zig, pylibs) + (() if IS_WIN else (sdl2_include,)):
        print(fn.__name__, "->", fn())
