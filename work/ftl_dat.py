#!/usr/bin/env python3
"""Extracteur pour ftl.dat (FTL iOS 1.5.10, format PKG).

Usage:
  python -I ftl_dat.py ftl.dat sortie/        # extrait tout
  python -I ftl_dat.py ftl.dat --list         # liste seulement

Format (big-endian) :
  en-tete 16 octets : 'PKG\n', u16 taille_en-tete, u16 taille_entree (20),
                      u32 nb_entrees, u32 taille_du_pool_de_noms
  entrees (20 octets) : u32 hash, u32 (flags<<24 | offset_nom_dans_pool),
                        u32 offset_donnees, u32 taille_stockee, u32 taille_decompressee
  pool de noms : chaines terminees par \\0, juste apres les entrees
  flag 1 = zlib, 0 = brut
"""
import os
import struct
import sys
import zlib


def read_index(f):
    f.seek(0)
    magic, hs, es, cnt, pool = struct.unpack('>4sHHII', f.read(16))
    if magic != b'PKG\n':
        raise ValueError('pas un fichier PKG')
    f.seek(hs)
    ents = [struct.unpack('>IIIII', f.read(es)) for _ in range(cnt)]
    f.seek(hs + cnt * es)
    names = f.read(pool)
    out = []
    for h, n, off, size, unpacked in ents:
        o = n & 0xFFFFFF
        name = names[o:names.index(b'\0', o)].decode('utf-8')
        out.append((name, n >> 24, off, size, unpacked))
    return out


def safe_path(root, name):
    p = os.path.normpath(os.path.join(root, *name.split('/')))
    if os.path.commonpath([os.path.abspath(root), os.path.abspath(p)]) != os.path.abspath(root):
        raise ValueError('chemin suspect : ' + name)
    return p


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    src, dst = sys.argv[1], sys.argv[2]
    with open(src, 'rb') as f:
        idx = read_index(f)
        if dst == '--list':
            for name, fl, off, size, un in sorted(idx):
                print(f'{name}\t{un}\t{"zlib" if fl else "raw"}')
            return 0
        bad = 0
        for name, fl, off, size, un in idx:
            f.seek(off)
            data = f.read(size)
            if fl:
                data = zlib.decompress(data)
            if len(data) != un:
                bad += 1
                print('taille inattendue :', name)
            p = safe_path(dst, name)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, 'wb') as o:
                o.write(data)
        print(f'{len(idx)} fichiers extraits, {bad} avertissements')
    return 0


if __name__ == '__main__':
    sys.exit(main())
