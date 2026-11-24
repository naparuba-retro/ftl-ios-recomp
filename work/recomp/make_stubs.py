"""Genere stubs.c : une fonction ext_* par symbole externe pas encore implemente par les hle_*.c.
Un stub arrete le programme en affichant le nom (le jeu ne doit pas l'atteindre en silence).
Usage : python -I make_stubs.py gen/gen_ext.h hle_libc.c hle_string.c hle_sys.c ... > stubs.c"""
import re
import sys

ext = [m.group(1) for m in re.finditer(r'^void (ext_[A-Za-z0-9_]+)\(void\);', open(sys.argv[1]).read(), re.M)]
done = set()
for p in sys.argv[2:]:
    t = open(p, encoding='utf-8').read()
    for m in re.finditer(r'\bEXT\(([A-Za-z0-9_]+)\)', t):
        done.add('ext_' + m.group(1))
    for m in re.finditer(r'\bSYS\(([A-Za-z0-9_]+)\)', t):
        done.add('ext__sys_' + m.group(1))
    for m in re.finditer(r'\bIOS\(([A-Za-z0-9_]+)\)', t):
        done.add('ext__ios_' + m.group(1))
print('/* Genere par make_stubs.py : fonctions externes non implementees. */')
print('#include <stdio.h>')
print('#include <stdlib.h>')
print('#include "rt.h"')
n = 0
for e in sorted(ext):
    if e not in done:
        n += 1
        print('void %s(void) { fprintf(stderr, "EXTERNE NON IMPLEMENTEE: %s (lr=%%08x)\\n", R32(REG_lr)); '
              'rt_where("appelant", R32(REG_lr)); '
              'fprintf(stderr, "   r0=%%08x r1=%%08x r2=%%08x r3=%%08x\\n", R32(REG_r0), R32(REG_r1), R32(REG_r2), R32(REG_r3)); '
              'fflush(stderr); exit(10); }' % (e, e))
sys.stderr.write('%d externes, %d implementees, %d stubs\n' % (len(ext), len(ext) - n, n))
