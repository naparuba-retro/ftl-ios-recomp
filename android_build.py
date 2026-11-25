# -*- coding: utf-8 -*-
"""Android APK builder: the optional "android" stage of build_from_ipa.py (python build_from_ipa.py --ipa ... --android).

Builds, from what the other stages produced in <out>/ (generated C in gen/, the game's data in game/):
  1. SDL2 for Android (ndk-build, sources downloaded by bootstrap.py)
  2. libmain.so: the recompiled game + the host runtime, with the Android NDK's clang (OpenGL ES 3.0, SDL2)
  3. the Java shell (SDL's activity + a 6-line subclass), dexed with d8
  4. an APK (aapt2) holding the libraries, ftl.dat and the other data files as assets, aligned and signed with a debug key.
The APK contains the game's data and code derived from your own IPA: install it on your own devices, never publish it.
Everything is downloaded by bootstrap.android() into ./tools/android (NDK r27c, build-tools 35.0.1, android.jar), pinned by SHA-1/SHA-256."""
import concurrent.futures as cf, glob, hashlib, os, platform, shutil, struct, subprocess, sys, time, zipfile, zlib

import bootstrap

ROOT = os.path.dirname(os.path.abspath(__file__))
RECOMP = os.path.join(ROOT, "work", "recomp")
SRC = os.path.join(ROOT, "android")
IS_WIN = platform.system() == "Windows"
API = 24                                                   # Android 7.0 : minimum
TARGET_API = 34
ABIS = {"arm64-v8a": "aarch64-linux-android", "x86_64": "x86_64-linux-android"}
PACKAGE = "org.ftlrecomp.app"


def log(*a): print("[android]", *a, flush=True)


def sh(cmd, **kw):
    """Run a command; on failure show its last output and stop."""
    r = subprocess.run(cmd, capture_output=True, text=True, errors="replace", **kw)
    if r.returncode:
        tail = "\n".join((r.stdout + r.stderr).strip().splitlines()[-25:])
        sys.exit("ERROR: command failed (%d): %s\n%s" % (r.returncode, " ".join(str(x) for x in cmd[:3]), tail))
    return r


def icon_png(path, size=192):
    """A plain launcher icon written without any library (dark space gradient, cyan blocky 'F')."""
    rows = []
    for y in range(size):
        row = bytearray([0])
        for x in range(size):
            r, g, b = 8 + y // 10, 10 + y // 8, 30 + y // 5
            u, v = x * 16 // size, y * 16 // size
            if (u in (4, 5) and 3 <= v <= 12) or (v in (3, 4) and 4 <= u <= 11) or (v in (7, 8) and 4 <= u <= 9): r, g, b = 90, 210, 255
            row += bytes((r, g, b))
        rows.append(bytes(row))
    def chunk(t, d): c = struct.pack(">I", len(d)) + t + d; return c + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0)) +
                           chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b""))


def sha256_files(paths):
    h = hashlib.sha256()
    for p, rel in paths:
        h.update(rel.encode()); h.update(b"\0")
        with open(p, "rb") as f:
            for b in iter(lambda: f.read(1 << 22), b""): h.update(b)
    return h.hexdigest()


def build_sdl(sdk, work, abi):
    """libSDL2.so for one ABI, with the NDK's own ndk-build."""
    libs = os.path.join(work, "sdl_libs"); obj = os.path.join(work, "sdl_obj_" + abi)
    so = os.path.join(libs, abi, "libSDL2.so")
    if os.path.exists(so): return os.path.dirname(so)
    ndk_build = os.path.join(sdk["ndk"], "ndk-build.cmd" if IS_WIN else "ndk-build")
    log("building SDL2 for", abi)
    sh([ndk_build, "NDK_PROJECT_PATH=" + work, "APP_BUILD_SCRIPT=" + os.path.join(sdk["sdl"], "Android.mk"), "APP_ABI=" + abi,
        "APP_PLATFORM=android-%d" % API, "NDK_OUT=" + obj, "NDK_LIBS_OUT=" + libs, "-j", str(max(1, (os.cpu_count() or 2) // 2))])
    if not os.path.exists(so): sys.exit("ERROR: SDL2 build produced no libSDL2.so")
    return os.path.dirname(so)


def build_main(sdk, out, work, abi, jobs):
    """libmain.so: generated game code + host runtime, one ABI."""
    gen = os.path.join(out, "gen"); obj = os.path.join(work, "obj_" + abi); os.makedirs(obj, exist_ok=True)
    triple = ABIS[abi]
    host = "windows-x86_64" if IS_WIN else "linux-x86_64"
    clang = os.path.join(sdk["ndk"], "toolchains", "llvm", "prebuilt", host, "bin", "clang" + (".exe" if IS_WIN else ""))
    target = ["--target=%s%d" % (triple, API)]
    inc = bootstrap.sdl2_include()
    flags = target + ["-std=gnu17", "-O2", "-fPIC", "-w", "-fno-strict-aliasing", "-fwrapv", "-ffp-contract=off", "-fno-delete-null-pointer-checks",
                      "-fno-sanitize=undefined", "-I", gen, "-I", RECOMP, "-I", inc]
    rtsrc = "rt.c hle_libc.c hle_string.c hle_sys.c hle_sound.c hle_stream.c hle_thread.c hle_rbtree.c neon.c gl_host.c pvrtc.c main_game.c android_glue.c".split()
    hle = "hle_libc.c hle_string.c hle_sys.c hle_sound.c hle_stream.c hle_thread.c hle_rbtree.c gl_host.c".split()
    r = subprocess.run([sys.executable, "-I", "make_stubs.py", os.path.join(gen, "gen_ext.h")] + hle, cwd=RECOMP, capture_output=True, text=True)
    if r.returncode: sys.exit("ERROR: make_stubs.py: " + r.stderr)
    stubs = os.path.join(obj, "stubs.c"); open(stubs, "w").write(r.stdout)
    jobs_l = [(f, os.path.join(obj, "g_" + os.path.basename(f)[:-2] + ".o")) for f in sorted(glob.glob(os.path.join(gen, "gen_*.c")))]
    jobs_l += [(os.path.join(RECOMP, f), os.path.join(obj, f[:-2] + ".o")) for f in rtsrc] + [(stubs, os.path.join(obj, "stubs.o"))]
    log("compiling %d files for %s with the NDK's clang" % (len(jobs_l), abi))

    def one(job):
        src, o = job
        if os.path.exists(o) and os.path.getmtime(o) > max(os.path.getmtime(src), newest_header): return None
        r = subprocess.run([clang] + flags + ["-c", src, "-o", o], capture_output=True, text=True, errors="replace")
        return None if r.returncode == 0 else (src, r.stderr[-3000:])
    newest_header = max(os.path.getmtime(h) for h in glob.glob(os.path.join(RECOMP, "*.h")) + glob.glob(os.path.join(RECOMP, "*.inc")) + glob.glob(os.path.join(gen, "*.h")))
    fails = []; t0 = time.time()
    with cf.ThreadPoolExecutor(jobs) as ex:
        for i, res in enumerate(ex.map(one, jobs_l)):
            if res: fails.append(res)
            if (i + 1) % 10 == 0: log("   %d / %d (%.0f s)" % (i + 1, len(jobs_l), time.time() - t0))
    if fails:
        for src, err in fails[:3]: print("--- " + src + "\n" + err)
        sys.exit("ERROR: %d file(s) failed" % len(fails))
    sdl_dir = build_sdl(sdk, work, abi)
    so = os.path.join(work, "libs_" + abi); os.makedirs(so, exist_ok=True)
    main_so = os.path.join(so, "libmain.so")
    sh([clang] + target + ["-shared", "-o", main_so] + [o for _, o in jobs_l] +
       ["-L" + sdl_dir, "-lSDL2", "-lGLESv3", "-lEGL", "-llog", "-landroid", "-lm", "-ldl", "-Wl,--no-undefined", "-Wl,-z,max-page-size=16384"])
    return [main_so, os.path.join(sdl_dir, "libSDL2.so")]


def build_dex(sdk, jdk, work):
    classes = os.path.join(work, "classes"); dex = os.path.join(work, "dex")
    shutil.rmtree(classes, ignore_errors=True); shutil.rmtree(dex, ignore_errors=True); os.makedirs(classes); os.makedirs(dex)
    srcs = glob.glob(os.path.join(sdk["sdl"], "android-project", "app", "src", "main", "java", "org", "libsdl", "app", "*.java"))
    srcs += glob.glob(os.path.join(SRC, "java", "org", "ftlrecomp", "app", "*.java"))
    android_jar = os.path.join(sdk["platform"], "android.jar")
    java = os.path.join(jdk, "bin", "java" + (".exe" if IS_WIN else "")); javac = os.path.join(jdk, "bin", "javac" + (".exe" if IS_WIN else ""))
    log("compiling the Java shell (%d files)" % len(srcs))
    sh([javac, "--release", "11", "-nowarn", "-cp", android_jar, "-d", classes] + srcs)
    cls = [os.path.join(d, f) for d, _, fs in os.walk(classes) for f in fs if f.endswith(".class")]
    sh([java, "-cp", os.path.join(sdk["build_tools"], "lib", "d8.jar"), "com.android.tools.r8.D8", "--lib", android_jar, "--min-api", str(API),
        "--release", "--output", dex] + cls)
    return os.path.join(dex, "classes.dex")


def build_apk(out, abis=("arm64-v8a",), jobs=None, version="1.0"):
    """Returns the path of the signed APK (<out>/android/FTL-Recomp.apk)."""
    game = os.path.join(out, "game")
    for need in ("ftl.dat", os.path.join("genj", "blocks.txt")):
        if not os.path.exists(os.path.join(game, need)): sys.exit("ERROR: %s is missing: run the earlier stages first (extract ... game)" % os.path.join(game, need))
    sdk = bootstrap.android(); jdk = bootstrap.jdk()
    work = os.path.join(out, "android"); os.makedirs(work, exist_ok=True)
    jobs = jobs or max(1, os.cpu_count() or 2)
    libs = {abi: build_main(sdk, out, work, abi, jobs) for abi in abis}
    dex = build_dex(sdk, jdk, work)

    # ---- assets: the game's data (ftl.dat, genj/, override/), a manifest to copy it out of the APK, and an id that changes with the data
    assets = os.path.join(work, "assets"); shutil.rmtree(assets, ignore_errors=True)
    files = []
    for sub in ("ftl.dat", "genj", "override"):
        p = os.path.join(game, sub)
        if os.path.isfile(p): files.append((p, sub))
        elif os.path.isdir(p):
            files += [(os.path.join(d, f), os.path.relpath(os.path.join(d, f), game).replace(os.sep, "/")) for d, _, fs in os.walk(p) for f in sorted(fs)]
    log("assets: %d files" % len(files))
    lines = []
    for src, rel in files:
        dst = os.path.join(assets, rel); os.makedirs(os.path.dirname(dst), exist_ok=True); shutil.copy2(src, dst)
        lines.append("%s %d" % (rel, os.path.getsize(src)))
    open(os.path.join(assets, "manifest.txt"), "w", newline="\n").write("\n".join(lines) + "\n")
    open(os.path.join(assets, "build.id"), "w").write(sha256_files(files)[:32])

    # ---- resources + manifest -> base APK
    res = os.path.join(work, "res", "mipmap-xxxhdpi"); os.makedirs(res, exist_ok=True)
    icon_png(os.path.join(res, "ic_launcher.png"))
    bt = sdk["build_tools"]; aapt2 = os.path.join(bt, "aapt2" + (".exe" if IS_WIN else ""))
    flat = os.path.join(work, "flat"); shutil.rmtree(flat, ignore_errors=True); os.makedirs(flat)
    sh([aapt2, "compile", os.path.join(res, "ic_launcher.png"), "-o", flat])
    base = os.path.join(work, "base.apk")
    if os.path.exists(base): os.remove(base)
    sh([aapt2, "link", "-o", base, "--manifest", os.path.join(SRC, "AndroidManifest.xml"), "-I", os.path.join(sdk["platform"], "android.jar"),
        "--min-sdk-version", str(API), "--target-sdk-version", str(TARGET_API), "--version-code", "1", "--version-name", version,
        "-A", assets, "-0", ".dat", "-0", ".bin", "-0", ".font"] + glob.glob(os.path.join(flat, "*.flat")))

    # ---- add the dex and the native libraries (stored, not compressed), align, sign
    log("assembling the APK")
    with zipfile.ZipFile(base, "a") as z:
        z.write(dex, "classes.dex", compress_type=zipfile.ZIP_STORED)
        for abi, paths in libs.items():
            for p in paths: z.write(p, "lib/%s/%s" % (abi, os.path.basename(p)), compress_type=zipfile.ZIP_STORED)
    aligned = os.path.join(work, "aligned.apk")
    if os.path.exists(aligned): os.remove(aligned)
    sh([os.path.join(bt, "zipalign" + (".exe" if IS_WIN else "")), "-f", "-p", "4", base, aligned])
    ks = os.path.join(work, "debug.keystore")
    if not os.path.exists(ks):
        sh([os.path.join(jdk, "bin", "keytool" + (".exe" if IS_WIN else "")), "-genkeypair", "-keystore", ks, "-storepass", "android", "-keypass", "android",
            "-alias", "ftl", "-keyalg", "RSA", "-keysize", "2048", "-validity", "10000", "-dname", "CN=FTL recomp (debug key)"])
    final = os.path.join(work, "FTL-Recomp.apk")
    if os.path.exists(final): os.remove(final)
    env = dict(os.environ, JAVA_HOME=jdk, PATH=os.path.join(jdk, "bin") + os.pathsep + os.environ.get("PATH", ""))
    signer = os.path.join(bt, "apksigner.bat" if IS_WIN else "apksigner")
    sh([signer, "sign", "--ks", ks, "--ks-pass", "pass:android", "--key-pass", "pass:android", "--out", final, aligned], env=env)
    sh([signer, "verify", final], env=env)
    log("APK ready: %s (%.0f MB)" % (final, os.path.getsize(final) / 1e6))
    return final


def adb(*args, check=True):
    exe = os.path.join(bootstrap.android()["platform_tools"], "adb" + (".exe" if IS_WIN else ""))
    r = subprocess.run([exe] + list(args), capture_output=True, text=True, errors="replace")
    if check and r.returncode: sys.exit("ERROR: adb %s failed:\n%s" % (" ".join(args), (r.stdout + r.stderr).strip()))
    return r


def install(apk):
    """adb install -r on the phone plugged in by USB (USB debugging on). Then 'adb logcat -s SDL FTL' shows what the game says."""
    out = adb("devices").stdout
    devs = [l.split()[0] for l in out.splitlines()[1:] if l.strip().endswith("device")]
    if not devs: sys.exit("No Android device found. Plug it in, turn on USB debugging (Settings > Developer options) and accept the prompt on the phone.\n" + out)
    log("installing on %s (about 200 MB, a minute or two)" % devs[0])
    adb("-s", devs[0], "install", "-r", apk)
    log("installed. Start FTL on the phone. If it does not work, run:  adb logcat -d | findstr /i \"SDL FTL libmain DEBUG AndroidRuntime\"")


if __name__ == "__main__":
    import argparse
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True, help="the build folder of build_from_ipa.py (holding gen/ and game/)")
    ap.add_argument("--abi", default="arm64-v8a", help="comma list: " + ", ".join(ABIS))
    ap.add_argument("--jobs", type=int)
    ap.add_argument("--install", action="store_true", help="then install the APK on the phone plugged in by USB (adb)")
    a = ap.parse_args()
    apk = build_apk(os.path.abspath(a.out), tuple(a.abi.split(",")), a.jobs)
    if a.install: install(apk)
