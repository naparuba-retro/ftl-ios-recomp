#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""FTL launcher: on first launch (no game built yet) a screen guides the build from your IPA; afterwards it starts the game.

    python play.py                 start the interface (English; --lang fr for French, or FTL_LANG=fr)
    python play.py --demo          walk-through with a fake build (nothing is written to the project folder)
    python play.py --shots DIR     write a PNG of every screen (no window)
    python play.py --selftest      test the screen flow (no window)

Nothing to install: SDL2 and the fonts are fetched automatically into ./tools (bootstrap.py)."""
import faulthandler, os, sys
faulthandler.enable()          # un plantage natif (SDL, pilote) laisse une trace dans le journal

ROOT = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, ROOT)
import bootstrap

if __name__ == "__main__":
    sys.path.insert(0, bootstrap.ui_libs())
    fonts = bootstrap.fonts()
    import warnings; warnings.filterwarnings("ignore")
    from launcher import main
    sys.exit(main.run(ROOT, fonts, sys.argv[1:]))
