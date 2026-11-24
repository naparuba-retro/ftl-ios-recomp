# -*- coding: utf-8 -*-
"""Moteur d'interface du lanceur : SDL2 (pysdl2), rendu 1280x720 mis a l'echelle, style FTL (espace sombre, panneaux a bordure claire,
focus jaune, barres vertes). Texte : Press Start 2P (titres, boutons) et VT323 (texte courant), polices libres telechargees par bootstrap.py."""
import ctypes, math, os, random, struct, zlib
import sdl2, sdl2.sdlttf as ttf

W, H = 1280, 720

# palette (inspiree de l'interface de FTL)
BG = (7, 9, 18)
PANEL = (21, 29, 41)
PANEL2 = (30, 41, 56)
BORDER = (205, 225, 235)
TEXT = (235, 242, 246)
DIM = (140, 158, 170)
YELLOW = (255, 228, 64)
GREEN = (92, 240, 120)
GREEN_D = (24, 70, 40)
RED = (255, 84, 72)
CYAN = (110, 220, 255)
BLACK = (0, 0, 0)


class UI:
    def __init__(self, renderer, font_dir, scale=1.0):
        self.r = renderer
        self.font_dir = font_dir
        self.fonts = {}
        self.tex = {}
        self.t = 0.0
        rnd = random.Random(1337)
        self.stars = [(rnd.randrange(W), rnd.randrange(H), rnd.choice((1, 1, 1, 2, 2, 3)), rnd.random() * 6.28) for _ in range(170)]

    # ------------------------------------------------------------ primitives
    def color(self, c, a=255):
        sdl2.SDL_SetRenderDrawColor(self.r, c[0], c[1], c[2], a)

    def rect(self, x, y, w, h, c, a=255):
        self.color(c, a)
        if a != 255: sdl2.SDL_SetRenderDrawBlendMode(self.r, sdl2.SDL_BLENDMODE_BLEND)
        sdl2.SDL_RenderFillRect(self.r, sdl2.SDL_Rect(int(x), int(y), int(w), int(h)))

    def frame(self, x, y, w, h, c, th=2):
        self.rect(x, y, w, th, c); self.rect(x, y + h - th, w, th, c); self.rect(x, y, th, h, c); self.rect(x + w - th, y, th, h, c)

    def panel(self, x, y, w, h, border=BORDER, fill=PANEL, th=2, cut=8, a=235):
        """Panneau a coins coupes (comme les cadres de FTL)."""
        self.rect(x, y, w, h, fill, a)
        self.frame(x, y, w, h, border, th)
        for cx, cy in ((x, y), (x + w - cut, y), (x, y + h - cut), (x + w - cut, y + h - cut)):
            self.rect(cx, cy, cut, cut, BG)                       # gomme les coins
        for i in range(0, cut, th):                                 # diagonales
            self.rect(x + cut - i - th, y + i, th, th, border); self.rect(x + w - cut + i, y + i, th, th, border)
            self.rect(x + cut - i - th, y + h - i - th, th, th, border); self.rect(x + w - cut + i, y + h - i - th, th, th, border)

    # ------------------------------------------------------------ texte
    def font(self, name, size):
        k = (name, size)
        if k not in self.fonts:
            f = ttf.TTF_OpenFont(os.path.join(self.font_dir, name).encode(), size)
            if not f: raise RuntimeError("police introuvable : " + name)
            self.fonts[k] = f
        return self.fonts[k]

    def _text_tex(self, s, name, size, col):
        k = (s, name, size, col)
        t = self.tex.get(k)
        if t is None:
            surf = ttf.TTF_RenderUTF8_Blended(self.font(name, size), s.encode("utf-8"), sdl2.SDL_Color(col[0], col[1], col[2], 255))
            if not surf: return None
            t = (sdl2.SDL_CreateTextureFromSurface(self.r, surf), surf.contents.w, surf.contents.h)
            sdl2.SDL_FreeSurface(surf)
            if len(self.tex) > 600: self.tex.clear()
            self.tex[k] = t
        return t

    def measure(self, s, name="VT323-Regular.ttf", size=26):
        w, h = ctypes.c_int(), ctypes.c_int()
        ttf.TTF_SizeUTF8(self.font(name, size), s.encode("utf-8"), ctypes.byref(w), ctypes.byref(h))
        return w.value, h.value

    def text(self, s, x, y, col=TEXT, size=26, name="VT323-Regular.ttf", anchor="l", shadow=True):
        if not s: return 0
        t = self._text_tex(s, name, size, col)
        if not t: return 0
        tex, w, h = t
        if anchor == "c": x -= w // 2
        elif anchor == "r": x -= w
        if shadow:
            st = self._text_tex(s, name, size, BLACK)
            if st: sdl2.SDL_RenderCopy(self.r, st[0], None, sdl2.SDL_Rect(int(x) + 2, int(y) + 2, w, h))
        sdl2.SDL_RenderCopy(self.r, tex, None, sdl2.SDL_Rect(int(x), int(y), w, h))
        return w

    # majuscules accentuees : la police les ecrase dans sa grille de 8 pixels ; on dessine la lettre de base et l'accent au-dessus
    CAPS = {"À": ("A", "grave"), "Â": ("A", "circ"), "Ä": ("A", "diaer"), "Ç": ("C", "cedil"), "È": ("E", "grave"), "É": ("E", "acute"),
            "Ê": ("E", "circ"), "Ë": ("E", "diaer"), "Î": ("I", "circ"), "Ï": ("I", "diaer"), "Ô": ("O", "circ"), "Ö": ("O", "diaer"),
            "Ù": ("U", "grave"), "Û": ("U", "circ"), "Ü": ("U", "diaer")}
    ACC = {"acute": ((4, -2), (3, -1)), "grave": ((2, -2), (3, -1)), "circ": ((3, -2), (2, -1), (4, -1)), "diaer": ((2, -1), (4, -1)), "cedil": ((3, 7), (2, 8))}

    def title(self, s, x, y, col=TEXT, size=24, anchor="l"):
        name = "PressStart2P-Regular.ttf"
        n = len(s)
        if anchor == "c": x -= n * size // 2
        elif anchor == "r": x -= n * size
        if not any(c in self.CAPS for c in s):
            self.text(s, x, y, col, size, name, "l"); return n * size
        u = size / 8.0
        for i, ch in enumerate(s):
            cx = x + i * size
            if ch in self.CAPS:
                base, acc = self.CAPS[ch]
                self.text(base, cx, y, col, size, name, "l")
                for dx, dy in self.ACC[acc]:
                    self.rect(cx + dx * u + 2, y + dy * u + 2, u, u, BLACK); self.rect(cx + dx * u, y + dy * u, u, u, col)
            elif ch != " ": self.text(ch, cx, y, col, size, name, "l")
        return n * size

    def wrap(self, s, width, size=26, name="VT323-Regular.ttf"):
        lines = []
        for para in s.split("\n"):
            cur = ""
            for word in para.split(" "):
                t = (cur + " " + word).strip()
                if cur and self.measure(t, name, size)[0] > width: lines.append(cur); cur = word
                else: cur = t
            lines.append(cur)
        return lines

    def paragraph(self, s, x, y, width, col=TEXT, size=26, lh=None):
        lh = lh or int(size * 1.05)
        for ln in self.wrap(s, width, size):
            self.text(ln, x, y, col, size); y += lh
        return y

    # ------------------------------------------------------------ fond, widgets
    def background(self, dt=0.0):
        self.t += dt
        self.rect(0, 0, W, H, BG)
        for x, y, s, ph in self.stars:
            b = 0.55 + 0.45 * math.sin(self.t * 1.3 + ph)
            v = int(70 + 150 * b * (s / 3.0 + 0.3))
            self.rect(x, y, s, s, (v, v, min(255, v + 25)))
        # lueur lointaine d'une planete en bas a droite
        for i in range(7):
            self.rect(W - 360 + i * 22, H - 60 + i * 8, 520 - i * 44, 120, (30 + i * 5, 24 + i * 6, 60 + i * 8), 28)

    def header(self, title, sub=""):
        self.panel(30, 24, W - 60, 92, th=3, fill=PANEL2)
        self.title(title, 62, 46, YELLOW, 28)
        if sub: self.text(sub, 64, 82, DIM, 24)
        self.title("FTL", W - 66, 46, CYAN, 28, anchor="r")

    def button(self, label, x, y, w, h, focus=False, enabled=True, primary=False, small=False):
        if not enabled: border, fill, tc = (70, 80, 90), PANEL, (90, 100, 108)
        elif primary: border, fill, tc = (GREEN if not focus else YELLOW), GREEN_D, (TEXT if not focus else YELLOW)
        else: border, fill, tc = (BORDER if not focus else YELLOW), (PANEL2 if not focus else (48, 58, 40)), (TEXT if not focus else YELLOW)
        self.panel(x, y, w, h, border=border, fill=fill, th=3 if focus else 2, cut=6)
        if focus and enabled:
            self.frame(x - 5, y - 5, w + 10, h + 10, YELLOW, 1)
        self.title(label, x + w // 2, y + (h - (12 if small else 16)) // 2, tc, 12 if small else 16, anchor="c")

    def bar(self, x, y, w, h, frac, col=GREEN):
        """Barre de progression par segments (comme les barres d'energie de FTL)."""
        self.rect(x, y, w, h, (10, 16, 20)); self.frame(x, y, w, h, BORDER, 2)
        seg, gap = 14, 3
        n = (w - 8) // (seg + gap)
        full = int(n * max(0.0, min(1.0, frac)))
        for i in range(n):
            self.rect(x + 4 + i * (seg + gap), y + 5, seg, h - 10, col if i < full else (22, 34, 30))

    def check(self, x, y, col=GREEN, s=3):
        for dx, dy in ((0, 3), (1, 4), (2, 5), (3, 4), (4, 3), (5, 2), (6, 1), (7, 0)):
            self.rect(x + dx * s, y + dy * s, s * 2, s, col)

    def cross(self, x, y, col=RED, s=3):
        for i in range(7):
            self.rect(x + i * s, y + i * s, s * 2, s, col); self.rect(x + (6 - i) * s, y + i * s, s * 2, s, col)

    def spinner(self, x, y, col=YELLOW, s=6):
        k = int(self.t * 8) % 4
        for i, (dx, dy) in enumerate(((0, 0), (1, 0), (1, 1), (0, 1))):
            self.rect(x + dx * (s + 2), y + dy * (s + 2), s, s, col if i == k else (60, 66, 70))

    def arrow(self, x, y, col=YELLOW, s=4):
        for i in range(5): self.rect(x + i * s, y + i * s, s, (9 - 2 * i) * s, col)

    # ------------------------------------------------------------ capture (mode sans fenetre)
def surface_to_png(surf, path):
    """Ecrit une SDL_Surface ARGB8888 en PNG (zlib seul)."""
    w, h, pitch = surf.contents.w, surf.contents.h, surf.contents.pitch
    raw = ctypes.string_at(surf.contents.pixels, pitch * h)
    rows = []
    for y in range(h):
        row = raw[y * pitch:y * pitch + w * 4]
        px = bytearray(row)
        px[0::4], px[2::4] = row[2::4], row[0::4]                   # BGRA -> RGBA
        px[3::4] = b"\xff" * w
        rows.append(b"\x00" + bytes(px))
    def chunk(t, b): return struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(b"".join(rows), 6)) + chunk(b"IEND", b"")
    open(path, "wb").write(png)
