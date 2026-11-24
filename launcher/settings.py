# -*- coding: utf-8 -*-
"""Reglages du lanceur et du jeu (settings.ini a la racine du projet, ignore par git). Enregistres a chaque changement.
Le lanceur transmet les choix du jeu au moment de le lancer, sous forme de variables d'environnement que le jeu comprend deja."""
import configparser, os, platform

IS_WIN = platform.system() == "Windows"

DEFAULTS = {
    "game": {"fullscreen": "auto", "vsync": "on", "show_fps": "off", "sound": "on", "record": "off"},
    "launcher": {"language": "en", "fullscreen": "auto"},
}
CHOICES = {
    ("game", "fullscreen"): ["auto", "on", "off"], ("game", "vsync"): ["on", "off"], ("game", "show_fps"): ["off", "on"],
    ("game", "sound"): ["on", "off"], ("game", "record"): ["off", "on"],
    ("launcher", "language"): ["en", "fr"], ("launcher", "fullscreen"): ["auto", "on", "off"],
}


def on_deck():
    return bool(os.environ.get("SteamDeck") or os.environ.get("SteamGamepadUI"))


class Settings:
    def __init__(self, path):
        self.path = path
        self.cp = configparser.ConfigParser()
        for sec, kv in DEFAULTS.items(): self.cp[sec] = dict(kv)
        try: self.cp.read(path, encoding="utf-8")
        except (configparser.Error, OSError): pass
        for (sec, key), ch in CHOICES.items():                       # valeur invalide -> defaut
            if self.cp.get(sec, key, fallback="") not in ch: self.cp[sec][key] = DEFAULTS[sec][key]

    def get(self, sec, key): return self.cp.get(sec, key)

    def cycle(self, sec, key):
        ch = CHOICES[(sec, key)]; cur = self.get(sec, key)
        self.cp[sec][key] = ch[(ch.index(cur) + 1) % len(ch)]
        self.save()
        return self.cp[sec][key]

    def save(self):
        try:
            tmp = self.path + ".tmp"
            with open(tmp, "w", encoding="utf-8") as f: self.cp.write(f)
            os.replace(tmp, self.path)
        except OSError:
            pass

    # ---- application
    def launcher_fullscreen(self):
        v = self.get("launcher", "fullscreen")
        return on_deck() if v == "auto" else v == "on"

    def game_env(self):
        """Variables d'environnement lues par le jeu (voir hle_sys.c, gl_host.c, gl_sdl.inc)."""
        e = {}
        fs = self.get("game", "fullscreen")
        if fs == "on" or (fs == "auto" and on_deck()): e["FTL_FULLSCREEN"] = "1"
        elif fs == "off": e["FTL_WINDOWED"] = "1"
        if self.get("game", "vsync") == "off": e["FTL_NOVSYNC"] = "1"
        if self.get("game", "show_fps") == "on": e["FTL_FPS"] = "1"
        if self.get("game", "sound") == "off": e["FTL_NOSOUND"] = "1"
        if self.get("game", "record") == "on": e["FTL_RECORD"] = "auto"
        return e
