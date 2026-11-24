# -*- coding: utf-8 -*-
"""Ecrit <dossier_gen>/binary_config.h a partir de binary.toml et de l'index de ftl.dat (iOS, anglais d'origine).
   usage: python gen_binary_config.py <binary.toml> <ftl.dat> <dossier_gen>"""
import struct, sys, os
try:
    import tomllib
except ImportError:                       # Python < 3.11
    import tomli as tomllib

toml_path, dat_path, out_dir = sys.argv[1:4]
cfg = tomllib.load(open(toml_path, 'rb'))

def pkg_entry(path, name):
    """(position de l'entree dans le fichier, taille decompressee) de <name> dans l'index PKG (big-endian) de ftl.dat."""
    with open(path, 'rb') as f:
        h = f.read(16)
        assert h[:4] == b'PKG\n', 'ftl.dat : en-tete PKG attendu'
        hs, es, n, nt = struct.unpack('>HHII', h[4:16])
        f.seek(hs); raw = f.read(n * es)
        f.seek(hs + n * es); names = f.read(nt)
    for i in range(n):
        _, no, off, st, un = struct.unpack('>IIIII', raw[i * es:i * es + 20])
        no &= 0xffffff
        if names[no:names.index(b'\0', no)].decode('latin1') == name:
            return hs + i * es, un
    raise SystemExit('entree %s introuvable dans %s' % (name, path))

r, d, fd = cfg['runtime'], cfg.get('debug', {}), cfg['ftl_dat']
idx_pos, size = pkg_entry(dat_path, fd['font24_name'])
L = ['/* Genere par gen_binary_config.py depuis binary.toml : ne pas editer. */', '#pragma once']
L.append('#define BIN_MOD_INIT_START 0x%08xu' % r['mod_init_start'])
L.append('#define BIN_MOD_INIT_SIZE %du' % r['mod_init_size'])
L.append('#define BIN_SIL_MAIN 0x%08xu' % r['sil_main'])
L.append('#define BIN_EMPTY_SLOT 0x%08xu' % r['empty_slot'])
L.append('#define BIN_FN_MEM_ALLOC f_%08x' % r['mem_alloc'])
L.append('#define BIN_FONT24_IDX_POS %d' % idx_pos)
L.append('#define BIN_FONT24_SIZE %d' % size)
if 'time_now' in d: L.append('#define BIN_FN_TIME_NOW f_%08x' % d['time_now'])
if 'damage_hull' in d: L.append('#define BIN_FN_DAMAGE_HULL f_%08x' % d['damage_hull'])
os.makedirs(out_dir, exist_ok=True)
open(os.path.join(out_dir, 'binary_config.h'), 'w').write('\n'.join(L) + '\n')
print('binary_config.h written: fonts/24.font index at %d, size %d' % (idx_pos, size))
