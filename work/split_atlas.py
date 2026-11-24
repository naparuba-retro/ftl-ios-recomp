#!/usr/bin/env python3
"""Decoupe les atlas de FTL iOS en sprites PNG.

Chaque atlas/X.txt contient des lignes 'chemin x y largeur hauteur' (origine en haut a gauche)
qui decrivent des zones de atlas/X.png (issu de convert_all.py).

Usage : python -I split_atlas.py extracted/ png/ sprites/
"""
import os
import sys

from PIL import Image


def main():
    ext, png, out = sys.argv[1:4]
    total = oob = 0
    for root, _, files in os.walk(os.path.join(ext, 'atlas')):
        for f in files:
            if not f.endswith('.txt'):
                continue
            txt = os.path.join(root, f)
            rel = os.path.relpath(txt, ext)[:-4]
            im = Image.open(os.path.join(png, rel + '.png'))
            for line in open(txt, encoding='utf-8').read().splitlines():
                if not line.strip():
                    continue
                name, x, y, w, h = line.rsplit(' ', 4)
                x, y, w, h = int(x), int(y), int(w), int(h)
                if x + w > im.width or y + h > im.height:
                    oob += 1
                    print('hors limites :', rel, name, (x, y, w, h), im.size)
                    continue
                dst = os.path.normpath(os.path.join(out, name))
                if not os.path.abspath(dst).startswith(os.path.abspath(out)):
                    raise ValueError('chemin suspect : ' + name)
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                im.crop((x, y, x + w, y + h)).save(dst)
                total += 1
    print(total, 'sprites,', oob, 'hors limites')


if __name__ == '__main__':
    main()
