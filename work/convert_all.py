#!/usr/bin/env python3
"""Convertit tous les .tex de FTL iOS (dossier extrait) en PNG.

  02080000 : BGRA 8888 brut (TEX_FORMAT_BGRA8888)
  02850000 : PVRTC 4 bpp RGBA (decodage maison, non compare a une reference)
  02860000 : PVRTC 2 bpp RGB  (idem, mode checkerboard simplifie)
Formats : voir include/SIL/texture.h dans les sources de SIL.

Usage : python convert_all.py extracted/ png/
Necessite numpy et Pillow.
"""
import os
import struct
import sys

import numpy as np
from PIL import Image


def morton_index(bx, by, nbits, ybit_low):
    m = np.zeros(bx.shape, dtype=np.int64)
    for i in range(nbits):
        yb = (by >> i) & 1
        xb = (bx >> i) & 1
        if ybit_low:
            m |= (yb << (2 * i)) | (xb << (2 * i + 1))
        else:
            m |= (xb << (2 * i)) | (yb << (2 * i + 1))
    # dimension la plus grande : les bits restants s'ajoutent en lineaire
    m |= ((bx >> nbits) << (2 * nbits)) | ((by >> nbits) << (2 * nbits))
    return m


def e5(v):
    return (v << 3) | (v >> 2)


def parse_colors(c):
    """c: uint32 array (mot de couleurs). Retourne A, B (n,4) en 0..255 et mode."""
    c = c.astype(np.int64)
    a16 = c & 0xFFFF
    b16 = c >> 16
    mode = c & 1

    # Couleur A
    ao = (a16 & 0x8000) != 0
    r_o = (a16 >> 10) & 31
    g_o = (a16 >> 5) & 31
    b4 = (a16 >> 1) & 15
    b_o = (b4 << 1) | (b4 >> 3)
    a3 = (a16 >> 12) & 7
    r4 = (a16 >> 8) & 15
    g4 = (a16 >> 4) & 15
    b3 = (a16 >> 1) & 7
    r_t = (r4 << 1) | (r4 >> 3)
    g_t = (g4 << 1) | (g4 >> 3)
    b_t = (b3 << 2) | (b3 >> 1)
    A = np.stack([
        e5(np.where(ao, r_o, r_t)),
        e5(np.where(ao, g_o, g_t)),
        e5(np.where(ao, b_o, b_t)),
        np.where(ao, 255, (a3 << 5) | (a3 << 2) | (a3 >> 1)),
    ], axis=-1)

    # Couleur B
    bo = (b16 & 0x8000) != 0
    r_o = (b16 >> 10) & 31
    g_o = (b16 >> 5) & 31
    b_o = b16 & 31
    a3 = (b16 >> 12) & 7
    r4 = (b16 >> 8) & 15
    g4 = (b16 >> 4) & 15
    b4 = b16 & 15
    r_t = (r4 << 1) | (r4 >> 3)
    g_t = (g4 << 1) | (g4 >> 3)
    b_t = (b4 << 1) | (b4 >> 3)
    B = np.stack([
        e5(np.where(bo, r_o, r_t)),
        e5(np.where(bo, g_o, g_t)),
        e5(np.where(bo, b_o, b_t)),
        np.where(bo, 255, (a3 << 5) | (a3 << 2) | (a3 >> 1)),
    ], axis=-1)
    return A.astype(np.float32), B.astype(np.float32), mode


def interp_plane(col, w, h, bw, bh, sx, sy):
    """Interpolation bilineaire (avec repliement) des couleurs de blocs.
    sx, sy : largeur/hauteur d'un bloc en pixels ; echantillon au pixel sx/2, sy/2."""
    xs = np.arange(w) - sx // 2
    ys = np.arange(h) - sy // 2
    bx0 = (xs // sx) % bw
    bx1 = (bx0 + 1) % bw
    fx = ((xs % sx) / sx).astype(np.float32)
    by0 = (ys // sy) % bh
    by1 = (by0 + 1) % bh
    fy = ((ys % sy) / sy).astype(np.float32)
    c = col.reshape(bh, bw, 4)
    top = c[by0][:, bx0] * (1 - fx)[None, :, None] + c[by0][:, bx1] * fx[None, :, None]
    bot = c[by1][:, bx0] * (1 - fx)[None, :, None] + c[by1][:, bx1] * fx[None, :, None]
    return top * (1 - fy)[:, None, None] + bot * fy[:, None, None]


def pvrtc_decode(data, w, h, bpp, ybit_low=True):
    bwpx = 4 if bpp == 4 else 8
    bw, bh = w // bwpx, h // 4
    nbits = int(np.log2(min(bw, bh)))
    words = np.frombuffer(data, '<u4').reshape(-1, 2)
    by, bx = np.divmod(np.arange(bw * bh), bw)
    order = morton_index(bx, by, nbits, ybit_low)
    mod = words[order, 0].astype(np.int64)
    col = words[order, 1]
    A, B, mode = parse_colors(col)
    Ai = interp_plane(A, w, h, bw, bh, bwpx, 4)
    Bi = interp_plane(B, w, h, bw, bh, bwpx, 4)

    X, Y = np.meshgrid(np.arange(w), np.arange(h))
    blk = (Y // 4) * bw + (X // bwpx)
    lx, ly = X % bwpx, Y % 4
    m = mod[blk]
    md = mode[blk]
    punch = np.zeros((h, w), dtype=bool)
    if bpp == 4:
        mv = (m >> ((ly * 4 + lx) * 2)) & 3
        wt = np.where(md == 0, np.array([0, 3, 5, 8])[mv], np.array([0, 4, 4, 8])[mv]) / 8.0
        punch = (md == 1) & (mv == 2)
    else:
        bit = (m >> (ly * 8 + lx)) & 1
        wt = np.where(md == 0, bit * 8, 0).astype(np.float32)
        # mode 1 : valeurs 2 bits sur les pixels (x+y) pairs, voisins moyennes
        if md.any():
            idx = (ly * 8 + lx)
            even = ((lx + ly) & 1) == 0
            rank = (idx // 2)
            v2 = (m >> (rank * 2)) & 3
            w2 = np.array([0, 3, 5, 8])[v2].astype(np.float32)
            w2 = np.where(even, w2, 0)
            g = np.zeros((h, w), dtype=np.float32)
            g[even] = w2[even]
            # moyenne des 4 voisins pour les pixels impairs
            n = (np.roll(g, 1, 0) + np.roll(g, -1, 0) + np.roll(g, 1, 1) + np.roll(g, -1, 1)) / 2.0
            w2 = np.where(even, g, n)
            wt = np.where(md == 1, w2, wt)
        wt = wt / 8.0
    wt = wt.astype(np.float32)[..., None]
    out = Ai * (1 - wt) + Bi * wt
    out[..., 3] = np.where(punch, 0, out[..., 3])
    return np.clip(np.rint(out), 0, 255).astype(np.uint8)


def smoothness(img):
    a = img[..., :3].astype(np.int32)
    return np.abs(np.diff(a, axis=1)).mean() + np.abs(np.diff(a, axis=0)).mean()


def convert(src, dst):
    d = open(src, 'rb').read()
    if d[:4] != b'TEX\n':
        raise ValueError('pas un TEX')
    fmt = d[4:8].hex()
    w, h = struct.unpack('>HH', d[8:12])
    body = d[32:]
    if fmt == '02080000':
        # TEX_FORMAT_BGRA8888 (0x08, voir SIL include/SIL/texture.h)
        img = np.frombuffer(body, np.uint8).reshape(h, w, 4)[..., [2, 1, 0, 3]]
    elif fmt in ('02850000', '02860000'):
        # 0x85 = PVRTC4_RGBA, 0x86 = PVRTC2_RGB
        bpp = 4 if fmt == '02850000' else 2
        cands = [pvrtc_decode(body, w, h, bpp, y) for y in (True, False)]
        img = min(cands, key=smoothness)
    else:
        raise ValueError('format inconnu ' + fmt)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    Image.fromarray(img, 'RGBA').save(dst)
    return fmt


def main():
    src, dst = sys.argv[1], sys.argv[2]
    n = 0
    counts = {}
    for root, _, files in os.walk(src):
        for f in files:
            if f.endswith('.tex'):
                p = os.path.join(root, f)
                rel = os.path.relpath(p, src)
                fmt = convert(p, os.path.join(dst, rel[:-4] + '.png'))
                counts[fmt] = counts.get(fmt, 0) + 1
                n += 1
    print(n, 'images', counts)


if __name__ == '__main__':
    main()
