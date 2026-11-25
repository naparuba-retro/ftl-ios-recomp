# -*- coding: utf-8 -*-
"""Light check of the optional Android stage (run by CI, no NDK, no game data): the pinned Google packages still exist in Google's
catalogue with the SHA-1 we pin, the SDL2 sources answer, and the Android build scripts at least compile."""
import os, re, sys, py_compile, urllib.request
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
import bootstrap

def get(url, method="GET"):
    req = urllib.request.Request(url, method=method, headers={"User-Agent": "ftl-recomp-ci"})
    with urllib.request.urlopen(req, timeout=120) as r:
        return r.status, r.read() if method == "GET" else b""

bad = []
_, cat = get(bootstrap._G + "repository2-3.xml")
cat = cat.decode("utf-8", "replace")
for key, spec in bootstrap.ANDROID.items():
    for osk, (url, sha) in [(k, v) for k, v in spec.items() if k not in ("dir",)]:
        name = url.rsplit("/", 1)[1]
        m = re.search(r'<checksum[^>]*>([0-9a-f]{40})</checksum>\s*<url>' + re.escape(name) + r'</url>', cat)
        if not m: bad.append("%s/%s: %s not in Google's catalogue (moved or withdrawn?)" % (key, osk, name))
        elif m.group(1) != sha: bad.append("%s/%s: %s has SHA-1 %s in the catalogue, pinned %s" % (key, osk, name, m.group(1), sha))
        else: print("ok  %-14s %-8s %s" % (key, osk, name))
        try: get(url, "HEAD")
        except Exception as e: bad.append("%s/%s: %s not downloadable (%s)" % (key, osk, name, e))

sdl = bootstrap.DEPS["sdl2"]["url"]
try: get(sdl, "HEAD"); print("ok  SDL2 sources", sdl.rsplit("/", 1)[1])
except Exception as e: bad.append("SDL2 sources: %s (%s)" % (sdl, e))

for f in ("android_build.py", "build_from_ipa.py", "bootstrap.py"):
    try: py_compile.compile(os.path.join(ROOT, f), doraise=True); print("ok  compiles", f)
    except py_compile.PyCompileError as e: bad.append(str(e))
for f in ("android/AndroidManifest.xml", "android/java/org/ftlrecomp/app/FTLActivity.java", "work/recomp/android_glue.c"):
    if not os.path.exists(os.path.join(ROOT, f)): bad.append("missing " + f)

if bad:
    print("\nFAILED:\n  " + "\n  ".join(bad)); sys.exit(1)
print("Android stage: pins and scripts OK")
