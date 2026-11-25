#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Builds the downloadable packages of the launcher (used by .github/workflows/release.yml, also runnable by hand).

    python packaging/make_dist.py windows  --out dist --version 1.0     -> dist/FTL-Recomp-1.0-windows-x64.zip
    python packaging/make_dist.py linux    --out dist --version 1.0     -> dist/FTL-Recomp-1.0-linux-x86_64.AppImage and .tar.gz   (AppImage needs --appimagetool)

A package holds the launcher, the build pipeline, recomplauncher.json (RecompLauncher support, at the top of every package) and a private Python: NO game content (the game is built on the user's machine from their own IPA).
Everything downloaded here is pinned by SHA-256."""
import argparse, hashlib, os, shutil, stat, struct, subprocess, sys, tarfile, urllib.request, zipfile, zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

PY_WIN = ("https://www.python.org/ftp/python/3.12.10/python-3.12.10-embed-amd64.zip",
          "4acbed6dd1c744b0376e3b1cf57ce906f9dc9e95e68824584c8099a63025a3c3")
PY_LINUX = ("https://github.com/astral-sh/python-build-standalone/releases/download/20261124/cpython-3.12.16%2B20261124-x86_64-unknown-linux-gnu-install_only.tar.gz",
            "6774c7f49ee21c1723f7ac91961a5bf9f3ee3ae6e4d82ff58bd358958b546461")
EXCLUDE = ("docs/", "packaging/", ".github/")          # documentation images and packaging scripts are not needed at run time


def download(url_sha, cache):
    url, sha = url_sha
    os.makedirs(cache, exist_ok=True)
    dst = os.path.join(cache, os.path.basename(url.split("?")[0]).replace("%2B", "+"))
    if not os.path.exists(dst):
        print("download", url)
        with urllib.request.urlopen(url) as r, open(dst, "wb") as f: shutil.copyfileobj(r, f)
    got = hashlib.sha256(open(dst, "rb").read()).hexdigest()
    if got != sha: raise SystemExit("SHA-256 mismatch for %s: %s" % (dst, got))
    return dst


def project_files():
    """Files tracked by git (so nothing generated or private can slip in), minus the excluded folders."""
    out = subprocess.run(["git", "ls-files", "-z"], cwd=ROOT, capture_output=True, check=True).stdout.decode("utf-8")
    return [f for f in out.split("\0") if f and not f.startswith(EXCLUDE)]


def copy_project(dst):
    for f in project_files():
        d = os.path.join(dst, f); os.makedirs(os.path.dirname(d), exist_ok=True); shutil.copy2(os.path.join(ROOT, f), d)


def write(path, text, mode=None, crlf=False):
    with open(path, "w", encoding="utf-8", newline="\r\n" if crlf else "\n") as f: f.write(text)
    if mode: os.chmod(path, mode)


def icon_png(path, size=256):
    """A plain 256x256 icon written without any library (dark space gradient, cyan 'F' block letters)."""
    rows = []
    for y in range(size):
        row = bytearray([0])
        for x in range(size):
            r, g, b = 8 + y // 12, 10 + y // 10, 30 + y // 6
            u, v = x * 16 // size, y * 16 // size                     # 16x16 grid
            if (u in (4, 5) and 3 <= v <= 12) or (v in (3, 4) and 4 <= u <= 11) or (v in (7, 8) and 4 <= u <= 9): r, g, b = 90, 210, 255
            row += bytes((r, g, b))
        rows.append(bytes(row))
    def chunk(t, d): c = struct.pack(">I", len(d)) + t + d; return c + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b"")
    open(path, "wb").write(png)


def windows(a):
    name = "FTL-Recomp-%s-windows-x64" % a.version
    stage = os.path.join(a.work, name); shutil.rmtree(stage, ignore_errors=True); os.makedirs(stage)
    copy_project(stage)
    with zipfile.ZipFile(download(PY_WIN, a.cache)) as z: z.extractall(os.path.join(stage, "python"))
    pth = [f for f in os.listdir(os.path.join(stage, "python")) if f.endswith("._pth")][0]
    stem = pth[:-5]
    # the embedded Python ignores PYTHONPATH and the script folder: list what it needs ('..' = the project folder)
    write(os.path.join(stage, "python", pth), "%s.zip\n.\n..\n" % stem, crlf=True)
    write(os.path.join(stage, "play.bat"), '@echo off\r\ncd /d "%~dp0"\r\n"%~dp0python\\python.exe" play.py %*\r\n', crlf=False)
    write(os.path.join(stage, "READ-ME-FIRST.txt"),
          "FTL launcher (Windows)\n\n1. Double-click play.bat.\n2. Give it your own FTL iPad IPA (version 1.5.10): drop it in the 'ipa' folder, or drag it onto the window.\n"
          "3. Click BUILD THE GAME (about 15 minutes, needs Internet the first time), then PLAY.\n\nNo game content is included in this package.\n"
          "Documentation: https://github.com/naparuba-retro/ftl-ios-recomp\n", crlf=True)
    os.makedirs(a.out, exist_ok=True)
    out = os.path.join(a.out, name + ".zip")
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for dp, _, fs in os.walk(stage):
            for f in sorted(fs):
                p = os.path.join(dp, f); z.write(p, os.path.relpath(p, a.work).replace(os.sep, "/"))
    print("OK", out, "%.1f MB" % (os.path.getsize(out) / 1e6))


APPRUN = r'''#!/bin/sh
# Entry point of the AppImage and of the Linux tarball (same folder, packed both ways). Runs from where it is, even read-only:
# everything the project writes (tools, your IPA, the built game, settings) goes to ONE data folder, never next to this file.
#   RecompLauncher passes RECOMPLAUNCHER_DATA; otherwise ~/.local/share/ftl-recomp (FTL_HOME to choose another).
HERE="$(dirname "$(readlink -f "$0")")"
export PYTHONDONTWRITEBYTECODE=1
export FTL_HOME="${FTL_HOME:-${RECOMPLAUNCHER_DATA:-${XDG_DATA_HOME:-$HOME/.local/share}/ftl-recomp}}"
mkdir -p "$FTL_HOME" || exit 1
cd "$FTL_HOME" || exit 1
exec "$HERE/usr/python/bin/python3" "$HERE/usr/share/ftl-recomp/play.py" "$@"
'''

DESKTOP = """[Desktop Entry]
Type=Application
Name=FTL Launcher
Comment=Build and play FTL natively from your own IPA
Exec=AppRun
Icon=ftl-recomp
Categories=Game;
Terminal=false
"""


def linux(a):
    appdir = os.path.join(a.work, "FTL-Recomp.AppDir"); shutil.rmtree(appdir, ignore_errors=True)
    share = os.path.join(appdir, "usr", "share", "ftl-recomp"); os.makedirs(share)
    copy_project(share)
    with tarfile.open(download(PY_LINUX, a.cache)) as t: t.extractall(os.path.join(appdir, "usr"))     # -> usr/python/
    if not os.path.isdir(os.path.join(appdir, "usr", "python", "bin")): raise SystemExit("unexpected Python archive layout")
    for junk in ("test", "idlelib", "tkinter", "turtledemo", "ensurepip"):                           # not used: keeps the image small
        shutil.rmtree(os.path.join(appdir, "usr", "python", "lib", "python3.12", junk), ignore_errors=True)
    write(os.path.join(appdir, "AppRun"), APPRUN, stat.S_IRWXU | stat.S_IRGRP | stat.S_IXGRP | stat.S_IROTH | stat.S_IXOTH)
    write(os.path.join(appdir, "ftl-recomp.desktop"), DESKTOP)
    icon_png(os.path.join(appdir, "ftl-recomp.png"))
    shutil.copy2(os.path.join(ROOT, "recomplauncher.json"), os.path.join(appdir, "recomplauncher.json"))     # RecompLauncher reads it from the AppDir root
    for sh in ("play.sh",): os.chmod(os.path.join(share, sh), 0o755)
    os.makedirs(a.out, exist_ok=True)
    tgz = os.path.join(a.out, "FTL-Recomp-%s-linux-x86_64.tar.gz" % a.version)      # the same folder, as a tarball (runs unpacked, via AppRun)
    with tarfile.open(tgz, "w:gz", compresslevel=9) as t: t.add(appdir, arcname="FTL-Recomp")
    print("OK", tgz, "%.1f MB" % (os.path.getsize(tgz) / 1e6))
    out = os.path.join(a.out, "FTL-Recomp-%s-linux-x86_64.AppImage" % a.version)
    if not a.appimagetool: print("AppDir ready:", appdir, "(pass --appimagetool to produce the .AppImage)"); return
    env = dict(os.environ, ARCH="x86_64", APPIMAGE_EXTRACT_AND_RUN="1")
    subprocess.run([a.appimagetool, appdir, out], check=True, env=env)
    os.chmod(out, 0o755)
    print("OK", out, "%.1f MB" % (os.path.getsize(out) / 1e6))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("target", choices=["windows", "linux"])
    ap.add_argument("--out", default=os.path.join(ROOT, "dist-packages")); ap.add_argument("--version", default="dev")
    ap.add_argument("--work", default=os.path.join(ROOT, "build", "_pkg")); ap.add_argument("--cache", default=os.path.join(ROOT, "build", "_pkg_cache"))
    ap.add_argument("--appimagetool")
    a = ap.parse_args()
    {"windows": windows, "linux": linux}[a.target](a)


if __name__ == "__main__":
    main()
