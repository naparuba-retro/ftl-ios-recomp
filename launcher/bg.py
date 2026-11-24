# -*- coding: utf-8 -*-
"""Fonds d'ecran du lanceur.
 - cut_from_ipa : decoupe l'image du menu du jeu (planete + flotte) DANS L'IPA DE L'UTILISATEUR, a l'execution, et la met en cache
   (build/ui_cache/menu_bg.bmp). Rien du jeu n'est distribue (meme principe que les cartes de Golgo ou le logo de TMNT).
 - procedural : nebuleuse, planete et atmosphere calculees par le code (fond de secours, avant que l'IPA soit trouve)."""
import math, os, struct, zlib, zipfile

ENTRY = "img/ipad/main_menus/main_base2.tex"       # 1024x768, BGRA8888


def _read_index(f):
    """Lit l'en-tete et l'index PKG (big-endian) au debut du flux ; renvoie {nom: (flags, offset, taille_stockee, taille)} et le nombre d'octets lus."""
    h = f.read(16)
    if h[:4] != b"PKG\n": raise ValueError("ftl.dat : en-tete PKG attendu")
    hs, es, n, nt = struct.unpack(">HHII", h[4:16])
    rest = f.read(n * es + nt)
    raw, names = rest[:n * es], rest[n * es:]
    out = {}
    for i in range(n):
        _, no, off, st, un = struct.unpack(">IIIII", raw[i * es:i * es + 20])
        fl = no >> 24; no &= 0xFFFFFF
        out[names[no:names.index(b"\0", no)].decode("latin1")] = (fl, off, st, un)
    return out, 16 + len(rest)


def cut_from_ipa(ipa, out_bmp):
    """IPA -> ftl.dat (en flux) -> entree ENTRY -> BMP 24 bits. Renvoie True si reussi."""
    with zipfile.ZipFile(ipa) as z:
        member = next(n for n in z.namelist() if n.endswith(".app/ftl.dat"))
        with z.open(member) as f:
            index, pos = _read_index(f)
            fl, off, st, un = index[ENTRY]
            while pos < off:                                   # avance dans le flux sans rien stocker
                chunk = f.read(min(1 << 22, off - pos))
                if not chunk: return False
                pos += len(chunk)
            data = f.read(st)
    if fl or st != un: data = zlib.decompress(data)
    if data[:4] != b"TEX\n" or data[5] != 8: return False
    w, h = struct.unpack(">HH", data[8:12]); px = data[32:32 + w * h * 4]
    rowpad = (-(w * 3)) % 4
    rows = []
    for y in range(h - 1, -1, -1):                              # BMP : lignes de bas en haut, BGR
        row = px[y * w * 4:(y + 1) * w * 4]
        bgr = bytearray(w * 3); bgr[0::3] = row[0::4]; bgr[1::3] = row[1::4]; bgr[2::3] = row[2::4]
        rows.append(bytes(bgr) + b"\0" * rowpad)
    body = b"".join(rows)
    hdr = b"BM" + struct.pack("<IHHI", 54 + len(body), 0, 0, 54) + struct.pack("<IiiHHIIiiII", 40, w, h, 1, 24, 0, len(body), 2835, 2835, 0, 0)
    os.makedirs(os.path.dirname(out_bmp), exist_ok=True)
    tmp = out_bmp + ".part"
    with open(tmp, "wb") as g: g.write(hdr + body)
    os.replace(tmp, out_bmp)
    return True


# ---------------------------------------------------------------- fond genere
def procedural_pixels(w=320, h=180):
    """Octets BGRA (w x h) : nebuleuse violette/bleue, grande planete a droite avec atmosphere cyan, petite lune."""
    out = bytearray(w * h * 4)
    pcx, pcy, pr = w * 0.80, h * 0.78, h * 0.62        # planete
    mcx, mcy, mr = w * 0.22, h * 0.20, h * 0.045       # lune
    lx, ly = -0.65, -0.55                              # direction de la lumiere
    for y in range(h):
        for x in range(w):
            u, v = x / w, y / h
            n = (math.sin(u * 5.1 + math.sin(v * 3.7) * 1.4) + math.sin(v * 6.3 - u * 2.2 + 1.3) + math.sin((u + v) * 9.0 + math.sin(u * 4.0)) * 0.5) / 2.5
            n = max(0.0, n * 0.5 + 0.42)
            r_ = 6 + 48 * n * n * (1.0 - v * 0.5); g_ = 8 + 22 * n * n; b_ = 22 + 90 * n * n * (0.6 + u * 0.4)
            d = math.hypot(x - pcx, y - pcy)
            if d < pr:                                                  # disque de la planete
                nx, ny = (x - pcx) / pr, (y - pcy) / pr; nz = math.sqrt(max(0.0, 1 - nx * nx - ny * ny))
                lit = max(0.0, nx * lx + ny * ly + nz * 0.55) ** 1.4
                tex = 0.75 + 0.25 * math.sin(nx * 14 + math.sin(ny * 9) * 2.2) * math.sin(ny * 11 + nx * 3)
                rim = (1 - nz) ** 3
                r_ = 8 + 70 * lit * tex + 40 * rim; g_ = 12 + 95 * lit * tex + 150 * rim; b_ = 22 + 120 * lit * tex + 210 * rim
            elif d < pr * 1.07:                                         # halo atmospherique
                a = (1 - (d - pr) / (pr * 0.07)) ** 2
                r_ += 40 * a; g_ += 150 * a; b_ += 200 * a
            dm = math.hypot(x - mcx, y - mcy)
            if dm < mr:
                nx, ny = (x - mcx) / mr, (y - mcy) / mr; nz = math.sqrt(max(0.0, 1 - nx * nx - ny * ny))
                lit = max(0.0, nx * lx + ny * ly + nz * 0.5)
                r_, g_, b_ = 30 + 150 * lit, 28 + 140 * lit, 34 + 150 * lit
            i = (y * w + x) * 4
            out[i] = min(255, int(b_)); out[i + 1] = min(255, int(g_)); out[i + 2] = min(255, int(r_)); out[i + 3] = 255
    return bytes(out)
