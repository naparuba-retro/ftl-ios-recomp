"""Compare les fonctions sys_* du binaire FTL avec l'API sysdep.h de chaque version de SIL."""
import os
import re

d = open('C:/SOURCES/ftl/work/FTL', 'rb').read()
s = set(x.decode() for x in re.findall(rb'[\x20-\x7e]{4,}', d))
insys = {x[1:] for x in s if re.match(r'_sys_[a-z0-9_]+$', x)}
roots = {
    '0.3': 'C:/SOURCES/ftl/sil_src/old/0.3',
    '0.3m': 'C:/SOURCES/ftl/sil_src/old/0.3m',
    '0.4g': 'C:/SOURCES/ftl/sil_src/old/0.4g',
    '0.5f': 'C:/SOURCES/ftl/sil_src/0.5f',
}
for v, base in roots.items():
    api = set()
    for root, _, fs in os.walk(base):
        for f in fs:
            if f == 'sysdep.h':
                api |= set(re.findall(r'\b(sys_[a-z0-9_]+)\b', open(os.path.join(root, f), errors='ignore').read()))
    print(v, 'api', len(api), '| binaire absent de l\'API:', len(insys - api), sorted(insys - api)[:6],
          '| API absente du binaire:', len(api - insys))
