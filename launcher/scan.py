# -*- coding: utf-8 -*-
"""Recherche et verification de l'IPA, detection de FTL PC, etat du jeu construit, ouverture de dossiers. Tout tourne en arriere-plan (threads)."""
import glob, hashlib, os, platform, subprocess, sys, threading

IS_WIN = platform.system() == "Windows"
IPA_SIZE = 173605774
IPA_EXAMPLE_URL = "https://archive.org/download/FTL__Faster_Than_Light_1.5.10_ios_6.0/FTL__Faster_Than_Light_1.5.10_ios_6.0.ipa"   # exemple de version supportee
IPA_SHA256 = "62bfa1ccb511479665bbcf73d8a9fca3c6606391e94d68ca9275eb5fea3e81b9"   # = work/recomp/binary.toml


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 22), b""): h.update(b)
    return h.hexdigest()


def check_ipa(path):
    """'ok' | 'bad' (mauvais fichier) | 'missing'."""
    try:
        if os.path.getsize(path) != IPA_SIZE: return "bad"
        return "ok" if sha256(path) == IPA_SHA256 else "bad"
    except OSError:
        return "missing"


def ipa_search_dirs(root):
    h = os.path.expanduser("~")
    d = [os.path.join(root, "ipa"), root, os.getcwd(), h + "/Downloads", h + "/Téléchargements", h + "/Desktop", h + "/Bureau", h + "/Documents", h]
    if IS_WIN:
        for drive in "CDEFGH": d += [drive + ":\\", drive + ":\\Downloads", drive + ":\\Games"]
    else:
        d += glob.glob("/run/media/*/*") + glob.glob("/media/*/*") + glob.glob("/mnt/*")
    seen, out = set(), []
    for x in d:
        x = os.path.normpath(x)
        if x not in seen and os.path.isdir(x): seen.add(x); out.append(x)
    return out


def find_ipas(root):
    """Fichiers *.ipa dont la taille est celle du bon IPA (le SHA-256 est verifie ensuite)."""
    res = []
    for d in ipa_search_dirs(root):
        try:
            for f in glob.glob(os.path.join(glob.escape(d), "*.ipa")):
                if os.path.getsize(f) == IPA_SIZE: res.append(f)
        except OSError:
            pass
    return list(dict.fromkeys(res))


class Scanner:
    """Etat de la recherche (lu par l'interface, ecrit par le thread)."""
    def __init__(self, root):
        self.root = root
        self.ipa_path = None; self.ipa_state = "scanning"        # scanning | verifying | ok | bad | none
        self.pc = None; self.pc_state = "scanning"               # scanning | found | none
        self._lock = threading.Lock()
        self._busy = False

    def start(self):
        threading.Thread(target=self._run, daemon=True).start()

    def _run(self):
        if self._busy: return
        self._busy = True
        try:
            if self.ipa_state != "ok":
                found = find_ipas(self.root)
                if found:
                    self.ipa_path, self.ipa_state = found[0], "verifying"
                    self.ipa_state = check_ipa(found[0]);
                    if self.ipa_state != "ok": self.ipa_path = found[0]
                else:
                    self.ipa_state = "none"
            if self.pc_state in ("scanning", "none"):
                try:
                    import detect_pc_ftl
                    b = detect_pc_ftl.best()
                    if b: self.pc, self.pc_state = b, "found"
                    else: self.pc_state = "none"
                except Exception:
                    self.pc_state = "none"
        finally:
            self._busy = False

    def rescan_ipa(self):
        """Appele regulierement quand l'ecran est ouvert : repere un IPA depose dans le dossier."""
        if self.ipa_state in ("none", "bad") and not self._busy:
            threading.Thread(target=self._run, daemon=True).start()

    def set_ipa(self, path):
        self.ipa_path, self.ipa_state = path, "verifying"
        def v(): self.ipa_state = check_ipa(path)
        threading.Thread(target=v, daemon=True).start()

    def set_pc(self, path):
        import detect_pc_ftl
        info = detect_pc_ftl.inspect(path)
        if not info or not info["has_fr"]: self.pc, self.pc_state = {"path": path, "bad": True}, "bad"; return
        info["store"] = "manual choice"; info["warnings"] = [] if info["vanilla"] else ["mod"]
        self.pc, self.pc_state = info, "found"


def game_state(root):
    """Dossier du jeu construit : (pret, chemin_exe, dossier)."""
    g = os.path.join(root, "build", "game")
    exe = os.path.join(g, "ftl_pc.exe" if IS_WIN else "ftl_linux")
    ok = os.path.isfile(exe) and os.path.isfile(os.path.join(g, "genj", "blocks.txt")) and (os.path.isfile(os.path.join(g, "ftl.dat")))
    return ok, exe, g


def langs_available(game_dir):
    """('fr'|'en'|None) langue active, et si l'echange est possible."""
    has_en, has_fr = os.path.isfile(os.path.join(game_dir, "ftl_en.dat")), os.path.isfile(os.path.join(game_dir, "ftl_fr.dat"))
    if has_en and not has_fr: return "fr", True        # ftl.dat = francais, ftl_en.dat = anglais
    if has_fr and not has_en: return "en", True
    return None, False


def swap_language(game_dir):
    a, b, c = (os.path.join(game_dir, n) for n in ("ftl.dat", "ftl_en.dat", "ftl_fr.dat"))
    if os.path.isfile(b) and not os.path.isfile(c): os.rename(a, c); os.rename(b, a)        # -> anglais
    elif os.path.isfile(c) and not os.path.isfile(b): os.rename(a, b); os.rename(c, a)      # -> francais


def open_folder(path):
    os.makedirs(path, exist_ok=True)
    try:
        if IS_WIN: os.startfile(path)
        elif platform.system() == "Darwin": subprocess.Popen(["open", path])
        else: subprocess.Popen(["xdg-open", path], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except Exception:
        pass
