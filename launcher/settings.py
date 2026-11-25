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


COMMENTS = {
    ("game", "fullscreen"): "Game window. auto = fullscreen on the Steam Deck (and in RecompLauncher's Big Picture mode), a window elsewhere.",
    ("game", "vsync"): "Vertical sync: keeps the picture tear-free at 60 images per second. Turn it off to measure speed.",
    ("game", "show_fps"): "Show the number of images per second.",
    ("game", "sound"): "Game sound. Turn it off if your audio device gives trouble.",
    ("game", "record"): "Record your games (inputs and random seed) so a crash or bug can be replayed exactly. Files go to build/game/replays.",
    ("launcher", "language"): "Language of the launcher screens.",
    ("launcher", "fullscreen"): "Launcher window. auto = fullscreen on the Steam Deck.",
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
            with open(tmp, "w", encoding="utf-8") as f: f.write(self.render())
            os.replace(tmp, self.path)
        except OSError:
            pass

    def render(self):
        """The file a person can read: every option written out, with its choices explained."""
        out = ["# FTL settings. You can edit this file: the launcher and the game read it.",
               "# Values are chosen from the lists below. Under RecompLauncher, the options on the game's page win for that start.", ""]
        for sec, keys in DEFAULTS.items():
            out.append("[%s]" % sec)
            for key in keys:
                out += ["# " + COMMENTS[(sec, key)], "# choices: " + ", ".join(CHOICES[(sec, key)]), "%s = %s" % (key, self.get(sec, key)), ""]
        return "\n".join(out)

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
