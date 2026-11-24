# -*- coding: utf-8 -*-
"""Lance build_from_ipa.py en sous-processus et en suit l'avancement en lisant sa sortie ("[build] === etape : x", telechargements, compilation)."""
import os, platform, re, signal, subprocess, sys, threading, time

IS_WIN = platform.system() == "Windows"
STAGES = ["extract", "ghidra", "lift", "patch", "compile", "game", "fr"]
WEIGHTS = {"extract": 3, "ghidra": 30, "lift": 22, "patch": 2, "compile": 33, "game": 2, "fr": 8}   # part approximative du temps total
RE_STAGE = re.compile(r"=== etape : (\w+)")
RE_DL = re.compile(r"\[bootstrap\]\s+(\d+) / (\d+) Mo")
RE_COMP = re.compile(r"\[build\]\s+(\d+) / (\d+) \(")


class BuildRunner:
    def __init__(self, root, ipa, pc_dat, out=None, extra=None):
        self.root, self.ipa, self.pc_dat = root, ipa, pc_dat
        self.out = out or os.path.join(root, "build")
        self.extra = extra or []
        self.stage = None; self.done = set(); self.state = "idle"      # idle | running | done | error | cancelled
        self.activity = ""; self.sub = 0.0; self.lines = []; self.t0 = 0.0; self.proc = None; self.error_stage = None
        self.log_path = os.path.join(self.out, "logs", "lanceur.log"); self.cmd = None   # cmd : commande de remplacement (demonstration)

    # progression globale 0..1
    def progress(self):
        total = float(sum(WEIGHTS.values())); acc = sum(WEIGHTS[s] for s in self.done)
        if self.stage in WEIGHTS and self.stage not in self.done: acc += WEIGHTS[self.stage] * min(0.95, self.sub)
        return min(1.0, acc / total)

    def command(self):
        if self.cmd: return self.cmd
        c = [sys.executable, os.path.join(self.root, "build_from_ipa.py"), "--ipa", self.ipa, "--out", self.out]
        if self.pc_dat: c += ["--pc-ftl-dat", self.pc_dat]
        return c + self.extra

    def start(self):
        os.makedirs(os.path.dirname(self.log_path), exist_ok=True)
        self.state = "running"; self.t0 = time.time()
        threading.Thread(target=self._run, daemon=True).start()

    def _run(self):
        try:
            kw = {"cwd": self.root, "stdout": subprocess.PIPE, "stderr": subprocess.STDOUT, "env": dict(os.environ, PYTHONUNBUFFERED="1", PYTHONIOENCODING="utf-8")}
            if IS_WIN: kw["creationflags"] = subprocess.CREATE_NEW_PROCESS_GROUP
            else: kw["start_new_session"] = True
            self.proc = subprocess.Popen(self.command(), **kw)
            with open(self.log_path, "w", encoding="utf-8") as log:
                for raw in self.proc.stdout:
                    line = raw.decode("utf-8", "replace").rstrip()
                    log.write(line + "\n"); log.flush()
                    self._parse(line)
            rc = self.proc.wait()
            if self.state == "cancelled": return
            if rc == 0:
                self.done.update(STAGES); self.stage = None; self.state = "done"
            else:
                self.error_stage = self.stage; self.state = "error"
        except Exception as e:
            self.lines.append("ERREUR du lanceur : %s" % e); self.error_stage = self.stage; self.state = "error"

    def _parse(self, line):
        self.lines.append(line)
        if len(self.lines) > 400: del self.lines[:200]
        m = RE_STAGE.search(line)
        if m:
            if self.stage in STAGES: self.done.add(self.stage)
            self.stage = m.group(1); self.sub = 0.0; self.activity = ""
            return
        m = RE_DL.search(line)
        if m: self.activity = "dl %s/%s" % (m.group(1), m.group(2)); return
        if "telechargement" in line and "bootstrap" in line:
            name = line.rsplit("/", 1)[-1][:60]; self.activity = "dl " + name; return
        m = RE_COMP.search(line)
        if m: self.sub = int(m.group(1)) / max(1, int(m.group(2))); self.activity = "compile %s/%s" % (m.group(1), m.group(2)); return
        if self.stage == "ghidra" or self.stage == "lift":
            self.sub = min(0.9, self.sub + 0.002)                         # progression lente et honnete : pas de pourcentage reel disponible

    def cancel(self):
        self.state = "cancelled"
        p = self.proc
        if not p or p.poll() is not None: return
        try:
            if IS_WIN: subprocess.run(["taskkill", "/F", "/T", "/PID", str(p.pid)], capture_output=True)
            else: os.killpg(os.getpgid(p.pid), signal.SIGTERM)
        except Exception:
            pass

    def elapsed(self):
        return time.time() - self.t0 if self.t0 else 0.0
