# -*- coding: utf-8 -*-
"""Detecte les installations de FTL (version PC) qui contiennent le francais officiel (data/text-fr.xml).

Sources explorees : GOG (registre Windows, dossiers par defaut), Steam (registre, ~/.steam, Flatpak, bibliotheques
du Steam Deck / carte SD via libraryfolders.vdf, appmanifest 212680), Heroic, dossiers usuels, et FTL_PC_DIR.
On n'a PAS confiance dans les noms de fichiers : chaque archive .dat trouvee est ouverte, son index lu, et on cherche les entrees data/text-<langue>.xml.
Les archives modifiees (Multiverse...) sont reperees (nombre d'entrees) et la version d'origine (ftl.dat.vanilla) est preferee.

    python detect_pc_ftl.py            liste lisible
    python detect_pc_ftl.py --json     sortie JSON
Utilise par build_from_ipa.py (--pc-ftl-dat auto) et, plus tard, par le menu de compilation.
"""
import glob, json, os, platform, re, struct, sys

IS_WIN = platform.system() == "Windows"
STEAM_APPID = "212680"                 # FTL: Faster Than Light
GOG_IDS = ["1207659102"]               # FTL: Advanced Edition
VANILLA_MAX_ENTRIES = 6000             # ftl.dat d'origine : ~3200 entrees ; Multiverse : >20000
LANG_RE = re.compile(r"^data/text-([A-Za-z\-]+)\.xml$")


def read_pkg_names(path):
    """Noms des entrees de l'index d'une archive PKG (big-endian) ; None si ce n'est pas une archive PKG."""
    try:
        with open(path, "rb") as f:
            h = f.read(16)
            if len(h) < 16 or h[:4] != b"PKG\n": return None
            hs, es, n, nt = struct.unpack(">HHII", h[4:16])
            if es != 20 or n > 2_000_000 or nt > 200_000_000: return None
            f.seek(hs); raw = f.read(n * es)
            f.seek(hs + n * es); names = f.read(nt)
        out = []
        for i in range(n):
            no = struct.unpack(">I", raw[i * es + 4:i * es + 8])[0] & 0xFFFFFF
            e = names.find(b"\0", no)
            out.append(names[no:e if e >= 0 else None].decode("latin1"))
        return out
    except OSError:
        return None


def inspect(path):
    names = read_pkg_names(path)
    if names is None: return None
    langs = sorted({m.group(1) for n in names for m in [LANG_RE.match(n)] if m})
    return {"path": os.path.abspath(path), "size": os.path.getsize(path), "entries": len(names), "languages": langs,
            "has_fr": "fr" in langs, "vanilla": len(names) <= VANILLA_MAX_ENTRIES}


# ---------------------------------------------------------------- recherche des dossiers d'installation
def _vdf_paths(vdf):
    try: t = open(vdf, encoding="utf-8", errors="replace").read()
    except OSError: return []
    return [m.group(1).replace("\\\\", "\\") for m in re.finditer(r'"path"\s+"([^"]+)"', t)]


def steam_roots():
    roots = []
    if IS_WIN:
        try:
            import winreg
            for hive, key, val in ((winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam", "SteamPath"),
                                   (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\WOW6432Node\Valve\Steam", "InstallPath"),
                                   (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Valve\Steam", "InstallPath")):
                try:
                    with winreg.OpenKey(hive, key) as k: roots.append(winreg.QueryValueEx(k, val)[0])
                except OSError: pass
        except ImportError: pass
        for pf in (os.environ.get("ProgramFiles(x86)"), os.environ.get("ProgramFiles")):
            if pf: roots.append(os.path.join(pf, "Steam"))
    else:
        h = os.path.expanduser("~")
        roots += [h + "/.steam/steam", h + "/.local/share/Steam", h + "/.steam/root", h + "/.var/app/com.valvesoftware.Steam/.local/share/Steam",
                  h + "/snap/steam/common/.local/share/Steam"]
    return [r for r in dict.fromkeys(os.path.normpath(r) for r in roots) if os.path.isdir(r)]


def steam_install_dirs():
    libs = []
    for r in steam_roots():
        libs.append(r)
        libs += _vdf_paths(os.path.join(r, "steamapps", "libraryfolders.vdf"))
    if not IS_WIN:                                           # carte SD du Steam Deck et disques montes
        libs += glob.glob("/run/media/*/*") + glob.glob("/media/*/*") + glob.glob("/mnt/*")
    dirs = []
    for lib in dict.fromkeys(libs):
        sa = os.path.join(lib, "steamapps")
        mf = os.path.join(sa, "appmanifest_%s.acf" % STEAM_APPID)
        if os.path.isfile(mf):
            m = re.search(r'"installdir"\s+"([^"]+)"', open(mf, encoding="utf-8", errors="replace").read())
            if m: dirs.append(("Steam", os.path.join(sa, "common", m.group(1))))
        dirs += [("Steam", d) for d in glob.glob(os.path.join(sa, "common", "FTL*"))]
    return dirs


def gog_install_dirs():
    dirs = []
    if IS_WIN:
        try:
            import winreg
            for gid in GOG_IDS:
                for view in (winreg.KEY_WOW64_32KEY, winreg.KEY_WOW64_64KEY):
                    try:
                        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\GOG.com\Games\%s" % gid, 0, winreg.KEY_READ | view) as k:
                            dirs.append(("GOG", winreg.QueryValueEx(k, "path")[0]))
                    except OSError: pass
        except ImportError: pass
        for pf in (os.environ.get("ProgramFiles(x86)"), os.environ.get("ProgramFiles")):
            if pf: dirs += [("GOG", d) for d in glob.glob(os.path.join(pf, "GOG Galaxy", "Games", "FTL*"))]
        for drive in "CDEFGH":
            dirs += [("GOG", d) for d in glob.glob(drive + ":\\GOG Games\\FTL*")]
    h = os.path.expanduser("~")
    dirs += [("Heroic/GOG", d) for d in glob.glob(h + "/Games/Heroic/*FTL*")]
    dirs += [("Heroic/GOG", d) for d in glob.glob(h + "/Games/Heroic/*/*FTL*")]
    return dirs


def usual_dirs():
    dirs = []
    if IS_WIN:
        for drive in "CDEFGH":
            for parent in ("Games", "Jeux", "Program Files (x86)", "Program Files"):
                dirs += [("common folder", d) for d in glob.glob("%s:\\%s\\*FTL*" % (drive, parent))]
    else:
        h = os.path.expanduser("~")
        dirs += [("common folder", d) for d in glob.glob(h + "/Games/*FTL*") + glob.glob(h + "/Games/*/*FTL*") + glob.glob(h + "/*FTL*")]
    env = os.environ.get("FTL_PC_DIR")
    if env: dirs.append(("FTL_PC_DIR", env))
    return dirs


def dat_files(folder, depth=3):
    """Fichiers susceptibles d'etre des archives : *.dat, *.dat.*, 'ftl.dat.vanilla'... (profondeur limitee)."""
    res = []
    base = folder.rstrip("\\/").count(os.sep)
    for root, dirs, files in os.walk(folder):
        if root.count(os.sep) - base >= depth: dirs[:] = []
        for fn in files:
            low = fn.lower()
            if low.endswith(".dat") or ".dat." in low: res.append(os.path.join(root, fn))
    return res


def find_candidates():
    """Liste de candidats valides (contenant data/text-fr.xml), les meilleurs d'abord."""
    seen, out = set(), []
    for store, d in steam_install_dirs() + gog_install_dirs() + usual_dirs():
        d = os.path.normpath(d)
        if not os.path.isdir(d): continue
        for f in dat_files(d):
            key = os.path.normcase(os.path.abspath(f))
            if key in seen: continue
            seen.add(key)
            info = inspect(f)
            if not info or not info["languages"]: continue            # pas une archive de FTL avec des textes
            info["store"] = store
            info["warnings"] = []
            if not info["vanilla"]: info["warnings"].append("modified archive (%d entries, a Multiverse-type mod?): the translation may be wrong" % info["entries"])
            if not info["has_fr"]: info["warnings"].append("no data/text-fr.xml (languages: %s)" % ", ".join(info["languages"]))
            info["score"] = (info["has_fr"], info["vanilla"], os.path.basename(f).lower().endswith(".vanilla"), -info["size"])
            out.append(info)
    out.sort(key=lambda i: i["score"], reverse=True)
    for i in out: i.pop("score")
    return out


def best():
    c = [i for i in find_candidates() if i["has_fr"] and i["vanilla"]]
    return c[0] if c else None


def check_file(path):
    """Verifie un fichier choisi a la main ; renvoie (ok, message)."""
    i = inspect(path)
    if not i: return False, "not an FTL archive (PKG header expected)"
    if not i["has_fr"]: return False, "no data/text-fr.xml (languages: %s)" % (", ".join(i["languages"]) or "none")
    if not i["vanilla"]: return True, "warning: modified archive (%d entries); prefer the original ftl.dat (e.g. ftl.dat.vanilla)" % i["entries"]
    return True, "ok (%d entries)" % i["entries"]


if __name__ == "__main__":
    c = find_candidates()
    if "--json" in sys.argv:
        print(json.dumps(c, indent=1, ensure_ascii=False)); sys.exit(0 if c else 1)
    if not c:
        print("No PC installation of FTL with French text found."); sys.exit(1)
    for k, i in enumerate(c):
        print("%s %s  [%s]  %d MB, %d entries, languages: %s" % ("->" if k == 0 else "  ", i["path"], i["store"], i["size"] >> 20, i["entries"], ", ".join(i["languages"])))
        for w in i["warnings"]: print("      ! " + w)
