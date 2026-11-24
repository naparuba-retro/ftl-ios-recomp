# -*- coding: utf-8 -*-
"""Ajoute les lettres accentuees francaises aux polices TTF du jeu iOS qui n'ont que l'ASCII
   (JustinFont7/10/11/12/12Bold). Polices en pixels (unite 64) : les accents sont dessines en pixels.
   usage: python make_fonts_fr.py <ftl.dat iOS anglais> <dossier out>   -> out/data_fr/fonts/*.ttf"""
import sys, os, io
from array import array
from fontTools.ttLib import TTFont
from fontTools.ttLib.tables import ttProgram
from fontTools.ttLib.tables._g_l_y_f import Glyph, GlyphCoordinates
from pkg import Pkg

U = 64
# accents : liste de cellules (col, ligne) ; ligne 0 = la plus basse ; largeur 3 colonnes (0..2)
ACC = {
    'grave': [(0, 1), (1, 0)],
    'acute': [(1, 1), (0, 0)],
    'circ':  [(1, 1), (0, 0), (2, 0)],
    'diaer': [(0, 0), (2, 0)],
    'tilde': [(0, 1), (1, 1), (1, 0), (2, 0)] ,
    'cedil': 'below',
}
LOW = {'à': ('a', 'grave'), 'â': ('a', 'circ'), 'ä': ('a', 'diaer'), 'ç': ('c', 'cedil'),
       'è': ('e', 'grave'), 'é': ('e', 'acute'), 'ê': ('e', 'circ'), 'ë': ('e', 'diaer'),
       'î': ('i', 'circ'), 'ï': ('i', 'diaer'), 'ô': ('o', 'circ'), 'ö': ('o', 'diaer'),
       'ù': ('u', 'grave'), 'û': ('u', 'circ'), 'ü': ('u', 'diaer'), 'ÿ': ('y', 'diaer'), 'ñ': ('n', 'tilde')}
UPP = {'À': ('A', 'grave'), 'Â': ('A', 'circ'), 'Ä': ('A', 'diaer'), 'Ç': ('C', 'cedil'),
       'È': ('E', 'grave'), 'É': ('E', 'acute'), 'Ê': ('E', 'circ'), 'Ë': ('E', 'diaer'),
       'Î': ('I', 'circ'), 'Ï': ('I', 'diaer'), 'Ô': ('O', 'circ'), 'Ö': ('O', 'diaer'),
       'Ù': ('U', 'grave'), 'Û': ('U', 'circ'), 'Ü': ('U', 'diaer')}

def contours(g, glyf):
    coords, ends, flags = g.getCoordinates(glyf)
    res = []; s = 0
    for e in ends:
        res.append([(int(x), int(y)) for x, y in coords[s:e + 1]]); s = e + 1
    return res

def rect(x0, y0, x1, y1):   # sens horaire (contour exterieur TrueType)
    return [(x0, y0), (x0, y1), (x1, y1), (x1, y0)]

def build(font, base, accent, top, is_upper):
    cm = font.getBestCmap(); glyf = font['glyf']; hmtx = font['hmtx']
    bg = glyf[cm[ord(base)]]; adv = hmtx[cm[ord(base)]][0]
    cs = contours(bg, glyf)
    xs = [p[0] for c in cs for p in c]; cx0, cx1 = min(xs), max(xs)
    mid = (cx0 + cx1) / 2.0
    if accent == 'cedil':
        col = int(round((mid - U / 2.0) / U)); new = [rect(col * U, -U, (col + 1) * U, 0), rect((col - 1) * U, -2 * U, col * U, -U)]
    else:
        cells = ACC[accent]; ox = int(round((mid - 1.5 * U) / U)) * U
        new = [rect(ox + c * U, top + r * U, ox + (c + 1) * U, top + (r + 1) * U) for c, r in cells]
    return cs + new, adv

def to_glyph(cs, glyf):
    g = Glyph(); pts = []; ends = []
    for c in cs:
        pts += c; ends.append(len(pts) - 1)
    g.numberOfContours = len(cs); g.coordinates = GlyphCoordinates(pts); g.endPtsOfContours = ends
    g.flags = array('B', [1] * len(pts)); g.program = ttProgram.Program(); g.program.fromBytecode(b'')
    g.recalcBounds(glyf)
    return g

def extend(data):
    font = TTFont(io.BytesIO(data)); cm = font.getBestCmap()
    if ord('é') in cm: return None
    glyf = font['glyf']; hmtx = font['hmtx']; order = list(font.getGlyphOrder())
    xh = glyf[cm[ord('x')]].yMax; cap = glyf[cm[ord('E')]].yMax
    def add(ch, cs, adv):
        name = 'uni%04X' % ord(ch)
        g = to_glyph(cs, glyf); glyf[name] = g; order.append(name); hmtx[name] = (adv, g.xMin)
        for t in font['cmap'].tables:
            if t.isUnicode() and t.format in (4, 12): t.cmap[ord(ch)] = name
    for ch, (b, a) in LOW.items():
        cs, adv = build(font, b, a, xh, False); add(ch, cs, adv)
    for ch, (b, a) in UPP.items():
        cs, adv = build(font, b, a, cap, True); add(ch, cs, adv)
    # oe / OE : deux lettres collees, serrees d'un pixel
    for ch, (l, r) in (('œ', ('o', 'e')), ('Œ', ('O', 'E'))):
        gl, gr = glyf[cm[ord(l)]], glyf[cm[ord(r)]]
        dx = gl.xMax - U
        cs = contours(gl, glyf) + [[(x + dx, y) for x, y in c] for c in contours(gr, glyf)]
        add(ch, cs, dx + hmtx[cm[ord(r)]][0])
    font.setGlyphOrder(order)
    font['maxp'].numGlyphs = len(order)
    b = io.BytesIO(); font.save(b)
    return b.getvalue()

if __name__ == '__main__':
    ios_path, out = sys.argv[1:3]
    k = Pkg(ios_path)
    d = os.path.join(out, 'data_fr', 'fonts'); os.makedirs(d, exist_ok=True)
    for e in k.ents:
        if e[0].startswith('fonts/') and e[0].endswith('.ttf') and 'Justin' in e[0]:
            r = extend(k.read(e))
            if r:
                open(os.path.join(d, os.path.basename(e[0])), 'wb').write(r); print('etendu', e[0], len(r))
            else: print('deja accentue', e[0])
