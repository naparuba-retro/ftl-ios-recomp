# -*- coding: utf-8 -*-
"""Construit la traduction FR de la version iOS a partir du francais officiel PC.
   LECTURE SEULE sur la source PC (ouverte en 'rb' par pkg.py, jamais ecrite).
   usage: python fr_build.py <ftl.dat PC vanilla> <ftl.dat iOS d'origine> <dossier_sortie>

   Paliers de correspondance (texte anglais iOS -> identifiant PC -> francais PC) :
     A  identique (espaces/casse/entites ignores)
     B  quasi identique (ponctuation, [x] / (x), casse) ET memes nombres
     C  fragment iOS = texte PC auquel il manque un placeholder (\\1) en tete ou en queue
"""
import re, sys, os, html, collections, json
from pkg import Pkg

PH = '\\1'   # placeholder PC : antislash + 1

pc_path, ios_path, out = sys.argv[1:4]
pc = Pkg(pc_path); ios = Pkg(ios_path)
pcf = {e[0]: e for e in pc.ents}; iof = {e[0]: e for e in ios.ents}
def rd(pkg, e): return pkg.read(e).decode('utf-8', 'replace')
def nocom(s): return re.sub(r'<!--.*?-->', '', s, flags=re.S)

def load(names):
    d = {}
    for n in names:
        for m in re.finditer(r'<text\s+name="([^"]+)"[^>]*>(.*?)</text>', nocom(rd(pc, pcf[n])), re.S):
            d[m.group(1)] = m.group(2)
    return d

en = load([n for n in pcf if re.fullmatch(r'data/text_[a-z]+\.xml', n)])
fr = load(['data/text-fr.xml'])

def norm(s): return re.sub(r'\s+', ' ', html.unescape(s.replace('\xa0', ' '))).strip().lower()
def norm2(s):
    s = norm(s).replace('[', '(').replace(']', ')')
    return re.sub(r'[^\w\d]+', ' ', s).strip()
def digits(s): return re.findall(r'\d+', s)
def clean_fr(s):
    # NBSP / fines -> espace ; apostrophes typographiques -> ' (polices bitmap/ttf du jeu)
    return s.replace('\xa0', ' ').replace(' ', ' ').replace('’', "'").replace('‘', "'")

idx1 = collections.defaultdict(list); idx2 = collections.defaultdict(list)
head = {}; tail = {}   # fragments : texte PC sans placeholder en tete/queue -> (fr sans placeholder)
for k, v in en.items():
    if k not in fr: continue
    idx1[norm(v)].append(k); idx2[norm2(v)].append(k)
    f = fr[k]
    if v.count('\\') == 1 and f.count('\\') == 1:
        if v.startswith(PH) and f.startswith(PH):
            head[norm(v[2:]).lstrip('-: ').strip()] = clean_fr(f[2:])
        elif v.endswith(PH) and f.endswith(PH):
            tail[norm(v[:-2]).rstrip('-: ').strip()] = clean_fr(f[:-2])

TAGS = ['text', 'title', 'short', 'desc', 'tooltip', 'power', 'unlock', 'flavorType', 'class']
def pick(c, key): return key if key in c else c[0]

def translate(key, x):
    if not re.search(r'[A-Za-z]{2}', x): return ('skip', None)
    c = idx1.get(norm(x))
    tier = 'A'
    if not c:
        c = [k for k in idx2.get(norm2(x), []) if digits(en[k]) == digits(x)]; tier = 'B'
    if c:
        r = clean_fr(fr[pick(c, key)]).strip()
        lead = x[:len(x) - len(x.lstrip())]; trail = x[len(x.rstrip()):]
        if x.strip().startswith('-') and not r.startswith('-'): r = '-' + r        # puces "-Detruit"
        if x.strip().endswith(':') and not r.endswith(':'): r = r.rstrip(' .') + ' :'   # "Status:" suivi de la valeur
        return (tier, lead + r + trail)
    lead = x[:len(x) - len(x.lstrip())]; trail = x[len(x.rstrip()):]
    core = norm(x).strip('-: ')
    # texte iOS = fin du texte PC (le PC met le placeholder devant) -> fr sans placeholder
    if core in head:
        return ('C', lead + head[core].strip() + trail)
    if core in tail:
        t = tail[core].rstrip()
        if not x.rstrip().endswith(':'): t = re.sub(r'\s*:$', '', t)   # le jeu iOS n'a pas de ':' ici
        if x.isupper(): t = t.upper()
        return ('C', lead + t + trail)
    return ('miss', None)

pat = re.compile(r'<(%s)((?:\s[^>]*)?)>([^<]*)</\1>' % '|'.join(TAGS + ['name']))
rows = []; stats = collections.Counter(); newfiles = {}
for name, e in sorted(iof.items()):
    if not (name.startswith('data/') and name.endswith('.xml')): continue
    f = os.path.basename(name); src = ios.read(e).decode('utf-8', 'replace')
    def sub(m):
        tag, attrs, body = m.group(1), m.group(2) or '', m.group(3)
        if tag == 'name' and f != 'achievements.xml': return m.group(0)   # identifiants / noms propres
        km = re.search(r'name="([^"]+)"', attrs); key = km.group(1) if km else ''
        t, res = translate(key, body)
        if t != 'skip': stats[t] += 1; stats[(f, t)] += 1
        rows.append((f, tag, key, t, body.strip().replace('\n', ' ')[:200], (res or '').replace('\n', ' ')[:200]))
        if res is None: return m.group(0)
        return '<%s%s>%s</%s>' % (tag, attrs, res, tag)
    new = pat.sub(sub, src)
    if new != src: newfiles[name] = new.encode('utf-8')

os.makedirs(out, exist_ok=True)
for n, b in newfiles.items():
    p = os.path.join(out, 'data_fr', n.replace('/', os.sep)); os.makedirs(os.path.dirname(p), exist_ok=True)
    open(p, 'wb').write(b)
with open(os.path.join(out, 'rapport_traduction.tsv'), 'w', encoding='utf-8') as g:
    g.write('fichier\tbalise\tcle\tniveau\tanglais_ios\tfrancais\n')
    for r in rows: g.write('\t'.join(r) + '\n')
s = {k: v for k, v in stats.items() if isinstance(k, str)}
tot = s.get('A', 0) + s.get('B', 0) + s.get('C', 0) + s.get('miss', 0)
done = tot - s.get('miss', 0)
print(s); print('total', tot, 'translated', done, '%.1f%%' % (100 * done / tot))
json.dump({'stats': s, 'total': tot, 'files': sorted(newfiles)}, open(os.path.join(out, 'stats.json'), 'w'), indent=1)
