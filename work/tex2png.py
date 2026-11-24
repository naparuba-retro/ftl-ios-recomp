#!/usr/bin/env python3
"""Convertit un .tex de FTL iOS en PNG (sans dependance).

Format observe : 32 octets d'en-tete ('TEX\\n', u8 version, u8 format, ...,
u16 largeur @8, u16 hauteur @10, big-endian) puis pixels BGRA 8 bits bruts (convertis en RGBA).
Verifie : taille = 32 + largeur*hauteur*4 sur les fichiers testes.

Usage : python -I tex2png.py entree.tex sortie.png
"""
import struct
import sys
import zlib


def tex_to_png(src, dst):
    d = open(src, 'rb').read()
    if d[:4] != b'TEX\n':
        raise ValueError('pas un TEX')
    w, h = struct.unpack('>HH', d[8:12])
    if d[5] != 0x08:
        raise ValueError('format non BGRA8888 : utiliser convert_all.py')
    px = d[32:]
    if len(px) != w * h * 4:
        raise ValueError(f'taille inattendue : {len(px)} != {w*h*4}')
    a = bytearray(px)
    a[0::4], a[2::4] = px[2::4], px[0::4]  # BGRA -> RGBA
    px = bytes(a)
    raw = b''.join(b'\0' + px[y * w * 4:(y + 1) * w * 4] for y in range(h))

    def chunk(t, b):
        c = struct.pack('>I', len(b)) + t + b
        return c + struct.pack('>I', zlib.crc32(t + b) & 0xFFFFFFFF)

    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) \
        + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b'')
    open(dst, 'wb').write(png)
    return w, h


if __name__ == '__main__':
    print(tex_to_png(sys.argv[1], sys.argv[2]))
