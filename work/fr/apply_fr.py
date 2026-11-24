# -*- coding: utf-8 -*-
"""Applique le patch FR : ftl.dat iOS d'origine (anglais) + fichiers out/data_fr/data/*.xml -> ftl.dat francais.
   Les fichiers modifies sont ajoutes a la fin (non compresses) et l'index est corrige ; le reste est copie tel quel.
   usage: python apply_fr.py <ftl.dat iOS anglais> <dossier out> <ftl.dat sortie>"""
import sys, os, struct, shutil
from pkg import Pkg
src, out, dst = sys.argv[1:4]
p = Pkg(src)
shutil.copyfile(src, dst)
repl = {}
root = os.path.join(out, 'data_fr')
for dp, _, fs in os.walk(root):
    for f in fs:
        full = os.path.join(dp, f); rel = os.path.relpath(full, root).replace(os.sep, '/')
        repl[rel] = open(full, 'rb').read()
names = {e[0]: i for i, e in enumerate(p.ents)}
with open(dst, 'r+b') as g:
    g.seek(0, 2); pos = g.tell()
    for n, data in sorted(repl.items()):
        i = names[n]
        pad = (-pos) % 4
        if pad: g.write(b'\0' * pad); pos += pad
        g.write(data)
        ent = 16 + 20 * i
        g.seek(ent + 4); old = struct.unpack('>I', g.read(4))[0]
        g.seek(ent + 4); g.write(struct.pack('>I', old & 0x00ffffff))           # drapeau compression = 0
        g.seek(ent + 8); g.write(struct.pack('>III', pos, len(data), len(data)))  # offset, taille stockee, taille decompressee
        pos += len(data); g.seek(0, 2)
print(len(repl), 'fichiers remplaces ->', dst, os.path.getsize(dst), 'octets')
