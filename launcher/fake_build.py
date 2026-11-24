# -*- coding: utf-8 -*-
"""Faux build_from_ipa.py pour tester le lanceur sans attendre 15 minutes : meme format de sortie, durees courtes.
Cree un faux jeu dans <racine>/build/game quand tout reussit (sauf si le premier argument est "fail")."""
import os, sys, time

root = os.environ.get("FTL_FAKE_ROOT", os.getcwd())
fail = "fail" in sys.argv
def say(s): print(s, flush=True)

say("[build] === etape : extract"); time.sleep(0.8)
say("[build] extrait")
say("[build] === etape : ghidra")
say("[bootstrap] telechargement https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_12.2_build/ghidra_12.2_PUBLIC_20261016.zip")
for i in range(0, 543, 90): say("[bootstrap]    %d / 543 Mo" % i); time.sleep(0.35)
time.sleep(0.6)
say("[build] === etape : lift"); time.sleep(1.2)
say("[build] === etape : patch"); time.sleep(0.3)
say("[build] === etape : compile")
for i in range(10, 65, 10): say("[build]    %d / 64 (%d s)" % (i, i)); time.sleep(0.35)
if fail:
    say("ERREUR : 1 fichier(s) en echec")
    sys.exit(1)
say("[build] === etape : game")
g = os.path.join(root, "build", "game"); os.makedirs(os.path.join(g, "genj"), exist_ok=True)
for f in ("ftl_pc.exe" if os.name == "nt" else "ftl_linux", "ftl.dat", "genj/blocks.txt"): open(os.path.join(g, f), "w").write("x")
time.sleep(0.4)
say("[build] termine")
