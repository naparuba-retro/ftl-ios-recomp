"""Ajoute des glyphes accentues a la police bitmap fonts/24.font (nom du vaisseau au hangar, ASCII seul d'origine).
 - minuscules accentuees : base + accent composes (grave/aigu/circonflexe/trema/tilde/cedille) dans la bande libre y=97..126 de la texture 512x128 ;
 - majuscules accentuees : entrees partageant l'image de la lettre de base (affichees sans accent).
Entree : 24.font d'origine ; sortie : override/24.font (plus grand : 128 entrees de plus -> l'index de ftl.dat est corrige a la lecture)."""
import struct, sys
src = sys.argv[1] if len(sys.argv) > 1 else r"C:\SOURCES\ftl\work\extracted\fonts\24.font"
dst = sys.argv[2] if len(sys.argv) > 2 else r"C:\SOURCES\ftl\work\recomp\override\24.font"
d = bytearray(open(src, "rb").read())
magic, ver, height, baseline, pad, co, cc, cs, to, ts = struct.unpack(">4sBBBBiHhii", d[:24])
assert magic == b"FONT" and cs == 16 and co == 24 and to == co + cc * cs
tex = bytearray(d[to:to + ts])                      # en-tete TEX (32 octets) + pixels
W, H = struct.unpack(">hh", tex[8:12]); assert (W, H) == (512, 128) and len(tex) == 32 + W * H
glyph = {}
for i in range(cc):
    ch, x, y, w, h, asc, p, pk, qk = struct.unpack(">ihhBBbBhh", d[co + i * cs:co + (i + 1) * cs])
    glyph[ch] = (x, y, w, h, asc, pk, qk)
def bmp(ch):
    x, y, w, h, asc, pk, qk = glyph[ch]
    return [[tex[32 + (y + r) * W + x + c] for c in range(w)] for r in range(h)]
GH = 30
def blank(w): return [[0] * w for _ in range(GH)]
def put(canvas, img, dx, dy):
    for r, row in enumerate(img):
        for c, v in enumerate(row):
            rr, cc2 = r + dy, c + dx
            if 0 <= rr < GH and 0 <= cc2 < len(canvas[0]) and v > canvas[rr][cc2]: canvas[rr][cc2] = v
def crop_rows(img, r0, r1): return [row[:] if r0 <= i <= r1 else [0] * len(row) for i, row in enumerate(img)]
grave = bmp(ord("`")); acute = [row[::-1] for row in grave]
circ = bmp(ord("^")); tilde = bmp(ord("~")); diaer = bmp(ord('"'))
def cedilla(w):
    c = blank(w); mid = w // 2
    for r in (25, 26, 27):
        for x in range(mid - 3, mid + 3): c[r][x] = 255
    return c
accents = {"grave": (grave, -4), "acute": (acute, -4), "circ": (circ, -4), "tilde": (tilde, -4), "diaer": (diaer, 0)}
lower = [("à", "a", "grave"), ("â", "a", "circ"), ("ä", "a", "diaer"), ("ç", "c", "cedille"),
         ("è", "e", "grave"), ("é", "e", "acute"), ("ê", "e", "circ"), ("ë", "e", "diaer"),
         ("î", "i", "circ"), ("ï", "i", "diaer"), ("ô", "o", "circ"), ("ö", "o", "diaer"),
         ("ù", "u", "grave"), ("û", "u", "circ"), ("ü", "u", "diaer"), ("ÿ", "y", "diaer"), ("ñ", "n", "tilde")]
upper = {"À": "A", "Â": "A", "Ä": "A", "Ç": "C", "È": "E", "É": "E", "Ê": "E", "Ë": "E", "Î": "I", "Ï": "I",
         "Ô": "O", "Ö": "O", "Ù": "U", "Û": "U", "Ü": "U", "Ÿ": "Y", "Ñ": "N"}
newent = []
x = 1; ytop = 97
for ch, base, acc in lower:
    b = bmp(ord(base))
    if base == "i": b = crop_rows(b, 10, 29)                       # i sans point
    bw = len(b[0])
    if acc == "cedille": w = bw; canvas = blank(w); put(canvas, b, 0, 0); put(canvas, cedilla(w), 0, 0)
    else:
        aimg, dy = accents[acc]; aw = len(aimg[0]); w = max(bw, aw)
        canvas = blank(w); put(canvas, b, (w - bw) // 2, 0)
        # le bas de l'accent doit rester au-dessus du corps (ligne 10) : decalage vertical dy
        put(canvas, aimg, (w - aw) // 2, dy)
    for r in range(GH):
        for c in range(w): tex[32 + (ytop + r) * W + x + c] = canvas[r][c]
    _, _, _, _, asc, pk, qk = glyph[ord(base)]
    newent.append((ord(ch), x, ytop, w, GH, asc, pk, qk))
    x += w + 2
assert x < W, "bande libre trop courte"
for ch, base in upper.items():
    bx, by, bw, bh, asc, pk, qk = glyph[ord(base)]
    newent.append((ord(ch), bx, by, bw, bh, asc, pk, qk))
ents = bytearray(d[co:co + cc * cs])
for ch, x0, y0, w, h, asc, pk, qk in newent:
    ents += struct.pack(">ihhBBbBhh", ch, x0, y0, w, h, asc, 0, pk, qk)
count = cc + len(newent)
out = bytearray(d[:24]); out += ents
new_to = 24 + count * 16
struct.pack_into(">H", out, 12, count); struct.pack_into(">i", out, 16, new_to)
out += tex
open(dst, "wb").write(out)
print("written", dst, len(out), "bytes;", len(newent), "glyphs added; texture at", new_to)
