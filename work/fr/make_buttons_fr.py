# -*- coding: utf-8 -*-
"""Images de boutons / bandeaux en francais pour la version iOS, a partir des images fr du PC (lecture seule).
   Pour chaque img/loc-fr/X.png du PC qui a un equivalent iOS img/X.tex (ou img/ipad/X.tex) ET une image PC anglaise img/X.png :
   on recompose une image au format iOS (.tex BGRA8, en-tete 32 octets) en gardant la hauteur et les marges iOS,
   alignee par le bas et la gauche sur le texte, la largeur s'adaptant au texte francais.
   usage: python make_buttons_fr.py <ftl.dat PC vanilla> <ftl.dat iOS anglais> <dossier out> [dossier_apercu]"""
import sys, io, os, struct
from PIL import Image
from pkg import Pkg

pc = Pkg(sys.argv[1]); ios = Pkg(sys.argv[2]); out = sys.argv[3]
prev = sys.argv[4] if len(sys.argv) > 4 else None
ni = {e[0]: e for e in ios.ents}; npc = {e[0]: e for e in pc.ents}

def bbox(im, t=100):
    return im.split()[3].point(lambda v: 255 if v > t else 0).getbbox()
def read_tex(d):
    w, h = struct.unpack('>HH', d[8:12]); raw = d[32:32 + w * h * 4]
    px = bytearray(raw); px[0::4] = raw[2::4]; px[2::4] = raw[0::4]
    return Image.frombytes('RGBA', (w, h), bytes(px)), d[:32]
def write_tex(im, hdr):
    w, h = im.size; raw = im.tobytes(); px = bytearray(raw); px[0::4] = raw[2::4]; px[2::4] = raw[0::4]
    return hdr[:8] + struct.pack('>HH', w, h) + hdr[12:20] + struct.pack('>I', w * h * 4) + hdr[24:32] + bytes(px)

SKIP = {'main_menus/Loading_main'}   # mise en page differente iPad/PC
done = []; skipped = []
for n, e in sorted(npc.items()):
    if not (n.startswith('img/loc-fr/') and n.endswith('.png')): continue
    base = n[len('img/loc-fr/'):-4]
    if base in SKIP: skipped.append((base, 'exclue (mise en page differente)')); continue
    tex = next((c for c in ('img/%s.tex' % base, 'img/ipad/%s.tex' % base) if c in ni), None)
    en = 'img/%s.png' % base
    if not tex or en not in npc: skipped.append((base, 'pas d\'equivalent iOS' if not tex else 'pas de PNG anglais PC')); continue
    a, hdr = read_tex(ios.read(ni[tex]))
    fr = Image.open(io.BytesIO(pc.read(e))).convert('RGBA')
    ia, fb = bbox(a), bbox(fr)
    if not ia or not fb: skipped.append((base, 'vide')); continue
    W, H = a.size; ix0, iy0, ix1, iy1 = ia; fx0, fy0, fx1, fy1 = fb
    if base.startswith('main_menus/'):
        # boutons du menu : position gauche fixe dans le jeu et alignement a droite -> on garde la taille iOS ;
        # texte aligne a droite, reduit (proportions gardees) s'il est plus large que l'original
        tw = ix1 - ix0
        sc = min(1.0, tw / float(fx1 - fx0))
        if sc < 1.0:
            fr = fr.resize((max(1, int(round(fr.width * sc))), max(1, int(round(fr.height * sc)))), Image.LANCZOS)
            fx0, fy0, fx1, fy1 = [int(round(v * sc)) for v in (fx0, fy0, fx1, fy1)]
        dx = ix1 - fx1
        dy = (iy0 + iy1) // 2 - (fy0 + fy1) // 2
        canvas = Image.new('RGBA', (W, H), (0, 0, 0, 0))
        canvas.alpha_composite(fr, (max(dx, 0), max(dy, 0)), (max(-dx, 0), max(-dy, 0))) if True else None
    else:
        nw = (fx1 - fx0) + ix0 + (W - ix1)
        dx, dy = ix0 - fx0, iy1 - fy1                  # gauche et bas alignes (bandeaux centres : la largeur suit le texte)
        canvas = Image.new('RGBA', (nw, H), (0, 0, 0, 0))
        canvas.alpha_composite(fr, (dx, dy)) if dx >= 0 and dy >= 0 else canvas.paste(fr, (dx, dy))
    p = os.path.join(out, 'data_fr', tex.replace('/', os.sep)); os.makedirs(os.path.dirname(p), exist_ok=True)
    open(p, 'wb').write(write_tex(canvas, hdr))
    done.append((tex, a.size, canvas.size))
    if prev:
        os.makedirs(prev, exist_ok=True); name = base.replace('/', '_')
        sheet = Image.new('RGBA', (max(a.width, canvas.width) + 10, H * 2 + 10), (40, 40, 60, 255))
        sheet.alpha_composite(a, (0, 0)); sheet.alpha_composite(canvas, (0, H + 10)); sheet.save(os.path.join(prev, name + '.png'))
for d in done: print('ok   ', d)
for s in skipped: print('saute', s)
print(len(done), 'images remplacees,', len(skipped), 'sautees')
