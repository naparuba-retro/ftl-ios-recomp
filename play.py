#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""FTL launcher: on first launch (no game built yet) a screen guides the build from your IPA; afterwards it starts the game.

    python play.py                 start the interface (English; --lang fr for French, or FTL_LANG=fr)
    python play.py --demo          walk-through with a fake build (nothing is written to the project folder)
    python play.py --shots DIR     write a PNG of every screen (no window)
    python play.py --selftest      test the screen flow (no window)
    python play.py --prepare --ipa FILE [--pc-ftl-dat FILE]   build without a window (RecompLauncher's setup step)
    python play.py --play [--fullscreen] [--no-vsync] ...     start the built game (RecompLauncher's Play)
    --data DIR                     keep tools, build and settings in DIR (or set FTL_HOME)

Nothing to install: SDL2 and the fonts are fetched automatically into ./tools (bootstrap.py)."""
import faulthandler, os, sys
faulthandler.enable()          # un plantage natif (SDL, pilote) laisse une trace dans le journal

ROOT = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, ROOT)
# --data DIR : where tools/, build/, ipa/ and settings.ini live (default: this folder). Same as the FTL_HOME variable.
for _i, _a in enumerate(sys.argv):
    if _a == "--data" and _i + 1 < len(sys.argv): os.environ["FTL_HOME"] = sys.argv[_i + 1]; del sys.argv[_i:_i + 2]; break
    if _a.startswith("--data="): os.environ["FTL_HOME"] = _a[7:]; del sys.argv[_i]; break
import bootstrap

if __name__ == "__main__":
    from launcher import headless          # --prepare / --play: no window, no SDL needed (used by RecompLauncher)
    _rc = headless.main(sys.argv[1:], bootstrap.DATA)
    if _rc is not None: sys.exit(_rc)
    sys.path.insert(0, bootstrap.ui_libs())
    fonts = bootstrap.fonts()
    import warnings; warnings.filterwarnings("ignore")
    from launcher import main
    sys.exit(main.run(bootstrap.DATA, fonts, sys.argv[1:]))
