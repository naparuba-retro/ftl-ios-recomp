#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Lanceur de FTL : au premier lancement (aucun jeu), un ecran guide la construction a partir de ton IPA ; ensuite, il lance le jeu.

    python play.py                 lance l'interface
    python play.py --lang en       interface en anglais
    python play.py --demo          demonstration avec un faux build (rien d'ecrit dans le dossier du projet)
    python play.py --shots DIR     ecrit une capture PNG de chaque ecran (sans fenetre)
    python play.py --selftest      teste les enchainements d'ecrans (sans fenetre)

Aucune installation : SDL2 et les polices sont recuperes automatiquement dans ./tools (bootstrap.py)."""
import os, sys

ROOT = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, ROOT)
import bootstrap

if __name__ == "__main__":
    sys.path.insert(0, bootstrap.ui_libs())
    fonts = bootstrap.fonts()
    import warnings; warnings.filterwarnings("ignore")
    from launcher import main
    sys.exit(main.run(ROOT, fonts, sys.argv[1:]))
