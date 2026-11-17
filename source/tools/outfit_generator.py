"""Outfit generator / manager (owner 2026-10-25: "make some type of image character generator that applies to the outfits
... I need at least 10 outfits or image generations that will retain that 512 by 512 for that character's ALR ... start off
with Freia ... show me the images first").

What it does, for one character outfit file (Player/<stem>/<stem><k>.alr, read from the clean reference):
1. finds the outfit's colour texture (the 512 x 512 DXT1 image, texture-table entry +0x0C packed size/format, +0x04 offset
   in the resource buffer; layouts from alr_transplant.py / export_all_characters.py) and decodes it;
2. makes themed variations of it: the clothing is recoloured through a colour ramp on its own light and shade (so every
   seam, fold and strap stays exactly where the model's UVs expect it); skin and the face are kept;
3. writes, per variation: a PNG preview, and the same ALR with only that texture's bytes replaced (re-encoded DXT1 with the
   full mip chain of the original size, so nothing else in the file moves);
4. writes a contact sheet of all variations for the owner to approve before anything is installed.
Standard library only (no PIL / numpy on this machine).
Usage: python -B -I outfit_generator.py CLEAN_ROOT STEM OUTFIT OUT_DIR [theme,theme,...]
"""
import json
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import alr_transplant as at  # noqa: E402
import export_all_effects as ee  # noqa: E402


# ---------------------------------------------------------------- colour helpers
def lum(c):
    return (0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]) / 255.0


def hsv(c):
    r, g, b = c[0] / 255.0, c[1] / 255.0, c[2] / 255.0
    mx, mn = max(r, g, b), min(r, g, b)
    d = mx - mn
    if d == 0:
        h = 0.0
    elif mx == r:
        h = (60 * ((g - b) / d)) % 360
    elif mx == g:
        h = 60 * ((b - r) / d) + 120
    else:
        h = 60 * ((r - g) / d) + 240
    return h, (d / mx if mx else 0.0), mx


def ramp(stops, t):
    t = 0.0 if t < 0 else 1.0 if t > 1 else t
    for (t0, c0), (t1, c1) in zip(stops, stops[1:]):
        if t <= t1:
            f = (t - t0) / (t1 - t0) if t1 > t0 else 0
            return tuple(int(round(a + (b - a) * f)) for a, b in zip(c0, c1))
    return stops[-1][1]


def mix(a, b, f):
    return tuple(int(round(x + (y - x) * f)) for x, y in zip(a, b))


def hash01(x, y, seed):
    n = (x * 374761393 + y * 668265263 + seed * 2147483647) & 0xFFFFFFFF
    n = (n ^ (n >> 13)) * 1274126177 & 0xFFFFFFFF
    return ((n ^ (n >> 16)) & 0xFFFF) / 65535.0


def value_noise(x, y, scale, seed):
    gx, gy = x / scale, y / scale
    x0, y0 = int(gx), int(gy)
    fx, fy = gx - x0, gy - y0
    fx, fy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
    a, b = hash01(x0, y0, seed), hash01(x0 + 1, y0, seed)
    c, d = hash01(x0, y0 + 1, seed), hash01(x0 + 1, y0 + 1, seed)
    return (a + (b - a) * fx) * (1 - fy) + (c + (d - c) * fx) * fy


# ---------------------------------------------------------------- what is kept
def keep_mask(rows, tile=16, share=0.35):
    """True for skin and the face: 16 x 16 tiles where skin-like pixels (hue 8-36, saturation 0.22-0.72) are at least
    a third are kept whole, then grown by one tile so the face's own shadows (eyes, hair line) stay as painted."""
    h, w = len(rows), len(rows[0])
    skin = [[False] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            hu, s, v = hsv(rows[y][x])
            skin[y][x] = 8 <= hu <= 36 and 0.22 <= s <= 0.72 and 0.22 <= v <= 0.98
    th, tw = h // tile, w // tile
    frac = [[sum(skin[ty * tile + i][tx * tile + j] for i in range(tile) for j in range(tile)) / float(tile * tile)
             for tx in range(tw)] for ty in range(th)]
    hot = [[frac[ty][tx] >= share for tx in range(tw)] for ty in range(th)]
    near = lambda ty, tx: sum(hot[min(max(ty + dy, 0), th - 1)][min(max(tx + dx, 0), tw - 1)] for dy in (-1, 0, 1) for dx in (-1, 0, 1))
    grown = [[frac[ty][tx] >= 0.08 and near(ty, tx) >= 3 for tx in range(tw)] for ty in range(th)]   # only tiles with skin
    return [[hot[y // tile][x // tile] or grown[y // tile][x // tile] or skin[y][x] for x in range(w)] for y in range(h)]


def keep_mask_v2(rows, tile=16, share=0.35, min_tiles=3):
    """Version 2 (2026-10-27, all fighters): also keeps darker skin (JD's face was half recoloured by version 1) and no
    longer keeps lone skin-coloured pixels on the clothing (orange specks). Skin test: hue 5-40, saturation 0.18-0.80,
    value 0.10-0.98. Tiles at least a third skin form regions; regions under 3 tiles are dropped; kept regions grow by
    one tile into tiles with some skin, then holes are closed (a tile with 5+ kept neighbours is kept). Whole tiles only."""
    h, w = len(rows), len(rows[0])
    th, tw = h // tile, w // tile
    frac = [[0.0] * tw for _ in range(th)]
    for ty in range(th):
        for tx in range(tw):
            n = 0
            for i in range(tile):
                row = rows[ty * tile + i]
                for j in range(tile):
                    hu, s, v = hsv(row[tx * tile + j])
                    n += 5 <= hu <= 40 and 0.18 <= s <= 0.80 and 0.10 <= v <= 0.98
            frac[ty][tx] = n / float(tile * tile)
    hot = [[frac[ty][tx] >= share for tx in range(tw)] for ty in range(th)]
    seen = [[False] * tw for _ in range(th)]
    keep = [[False] * tw for _ in range(th)]
    for ty in range(th):
        for tx in range(tw):
            if hot[ty][tx] and not seen[ty][tx]:
                comp, stack = [], [(ty, tx)]
                seen[ty][tx] = True
                while stack:
                    a, b = stack.pop()
                    comp.append((a, b))
                    for da, db in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                        na, nb = a + da, b + db
                        if 0 <= na < th and 0 <= nb < tw and hot[na][nb] and not seen[na][nb]:
                            seen[na][nb] = True
                            stack.append((na, nb))
                if len(comp) >= min_tiles:
                    for a, b in comp:
                        keep[a][b] = True

    def ring(g, ty, tx):
        return sum(g[ty + dy][tx + dx] for dy in (-1, 0, 1) for dx in (-1, 0, 1) if (dy or dx) and 0 <= ty + dy < th
                   and 0 <= tx + dx < tw)
    keep = [[keep[ty][tx] or (frac[ty][tx] >= 0.05 and ring(keep, ty, tx) >= 1) for tx in range(tw)] for ty in range(th)]
    keep = [[keep[ty][tx] or ring(keep, ty, tx) >= 5 for tx in range(tw)] for ty in range(th)]
    return [[keep[y // tile][x // tile] for x in range(w)] for y in range(h)]


# ---------------------------------------------------------------- themes
THEMES = {
    'fire': ('Fire element', [(0, (10, 4, 2)), (0.3, (70, 10, 0)), (0.55, (190, 50, 0)), (0.78, (250, 140, 20)), (1, (255, 236, 170))]),
    'ice': ('Ice element', [(0, (6, 16, 42)), (0.38, (38, 96, 168)), (0.7, (140, 208, 240)), (1, (246, 252, 255))]),
    'shadow': ('Shadow', [(0, (5, 0, 10)), (0.5, (48, 14, 70)), (0.8, (118, 60, 160)), (1, (210, 172, 255))]),
    'gold': ('Royal gold', [(0, (30, 18, 0)), (0.4, (128, 84, 10)), (0.7, (220, 170, 52)), (1, (255, 246, 192))]),
    'toxic': ('Toxic', [(0, (4, 20, 0)), (0.5, (58, 140, 10)), (0.8, (160, 240, 40)), (1, (236, 255, 172))]),
    'crimson': ('Crimson', [(0, (14, 0, 2)), (0.5, (118, 6, 22)), (0.8, (200, 40, 52)), (1, (255, 192, 192))]),
    'ocean': ('Ocean', [(0, (0, 14, 26)), (0.5, (0, 88, 110)), (0.8, (40, 178, 190)), (1, (202, 250, 246))]),
    'camo': ('Urban camo', None),
    'neon': ('Neon circuit', None),
    'spectral': ('Spectral ghost', None),
    # 2026-10-27: an 11th for the fighters that have only 4 outfits (no OG 5th), so they reach 15 too
    'ivory': ('Ivory', [(0, (34, 30, 26)), (0.45, (150, 140, 122)), (0.75, (222, 214, 196)), (1, (255, 252, 244))]),
}


def theme_pixel(name, c, x, y, L, edge):
    stops = THEMES[name][1]
    if stops:
        t = 0.04 + 0.96 * L ** 1.15
        out = ramp(stops, t)
        if name == 'fire' and edge > 0.2:                                           # glowing seams and folds
            out = mix(out, (255, 190, 60), min(0.6, edge))
        if name == 'gold' and edge > 0.18:                                          # bright metal rims
            out = mix(out, (255, 250, 210), min(1.0, edge * 1.4))
        return out
    if name == 'camo':
        n = value_noise(x, y, 26, 3) * 0.65 + value_noise(x, y, 9, 5) * 0.35
        base = (52, 56, 44) if n < 0.38 else (96, 100, 82) if n < 0.55 else (150, 150, 132) if n < 0.7 else (24, 26, 22)
        k = 0.45 + 0.9 * L
        return tuple(min(255, int(v * k)) for v in base)
    if name == 'neon':
        base = tuple(int(v * 0.22) for v in (40, 30, 60))
        glow = (255, 40, 200) if value_noise(x, y, 64, 9) > 0.5 else (30, 230, 255)
        e = min(1.0, edge * 3.2)
        out = mix(base, glow, e)
        return mix(out, (L * 90, L * 70, L * 120), 0.35)
    if name == 'spectral':
        g = 120 + int(135 * L)
        out = (int(g * 0.80), int(g * 0.92), min(255, int(g * 1.08)))
        return mix(out, (180, 230, 255), 0.25 * value_noise(x, y, 48, 13))
    raise ValueError(name)


def edges(Lr):
    h, w = len(Lr), len(Lr[0])
    e = [[0.0] * w for _ in range(h)]
    for y in range(1, h - 1):
        for x in range(1, w - 1):
            gx = Lr[y][x + 1] - Lr[y][x - 1]
            gy = Lr[y + 1][x] - Lr[y - 1][x]
            e[y][x] = math.sqrt(gx * gx + gy * gy)
    return e


def make_variant(rows, keep, Lr, Er, name):
    h, w = len(rows), len(rows[0])
    out = []
    for y in range(h):
        r = []
        for x in range(w):
            c = rows[y][x]
            if keep[y][x]:
                r.append(c[:3])
            else:
                r.append(tuple(max(0, min(255, int(v))) for v in theme_pixel(name, c, x, y, Lr[y][x], Er[y][x])))
        out.append(r)
    return out


# ---------------------------------------------------------------- DXT1 with the full mip chain
def _to565(c):
    return (c[0] >> 3) << 11 | (c[1] >> 2) << 5 | (c[2] >> 3)


def _565(v):
    r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
    return (r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2)


def encode_block(blk, clear=None):
    """DXT1 block. clear = 16 flags of transparent pixels (owner 2026-10-25, a hard rule: the transparency of the original
    is never filled in): a block with any transparent pixel uses DXT1's 3-colour mode (colour0 <= colour1, index 3 =
    transparent), exactly as the game decodes it; other blocks use 4 colours. End points = the darkest and the brightest
    opaque pixel, indices to the nearest palette colour."""
    if clear and any(clear):
        solid = [p for p, c in zip(blk, clear) if not c] or [(0, 0, 0)]
        ls = [p[0] * 299 + p[1] * 587 + p[2] * 114 for p in solid]
        c0, c1 = sorted((_to565(solid[ls.index(min(ls))]), _to565(solid[ls.index(max(ls))])))   # c0 <= c1: 3 colours
        a, b = _565(c0), _565(c1)
        pal = [a, b, tuple((x + y) // 2 for x, y in zip(a, b))]
        bits = 0
        for i, p in enumerate(blk):
            if clear[i]:
                k = 3
            else:
                best, k = 1 << 30, 0
                for q in range(3):
                    d = (p[0] - pal[q][0]) ** 2 + (p[1] - pal[q][1]) ** 2 + (p[2] - pal[q][2]) ** 2
                    if d < best:
                        best, k = d, q
            bits |= k << (2 * i)
        return struct.pack('<HHI', c0, c1, bits)
    ls = [p[0] * 299 + p[1] * 587 + p[2] * 114 for p in blk]
    lo, hi = blk[ls.index(min(ls))], blk[ls.index(max(ls))]
    c0, c1 = _to565(hi), _to565(lo)
    if c0 == c1:
        return struct.pack('<HHI', c0, c1, 0)
    if c0 < c1:
        c0, c1 = c1, c0
    a, b = _565(c0), _565(c1)
    pal = [a, b, tuple((2 * x + y) // 3 for x, y in zip(a, b)), tuple((x + 2 * y) // 3 for x, y in zip(a, b))]
    bits = 0
    for i, p in enumerate(blk):
        best, k = 1 << 30, 0
        for q in range(4):
            d = (p[0] - pal[q][0]) ** 2 + (p[1] - pal[q][1]) ** 2 + (p[2] - pal[q][2]) ** 2
            if d < best:
                best, k = d, q
        bits |= k << (2 * i)
    return struct.pack('<HHI', c0, c1, bits)


def encode_level(rows, clear_blocks):
    """clear_blocks: per block (in order) the 16 transparent flags of the ORIGINAL texture at this mip level."""
    h, w = len(rows), len(rows[0])
    out = bytearray()
    n = 0
    for by in range(0, max(h, 4), 4):
        for bx in range(0, max(w, 4), 4):
            blk = [rows[min(by + i // 4, h - 1)][min(bx + i % 4, w - 1)] for i in range(16)]
            out += encode_block(blk, clear_blocks[n])
            n += 1
    return out


def half(rows):
    h, w = len(rows), len(rows[0])
    if h == 1 and w == 1:
        return rows
    nh, nw = max(h // 2, 1), max(w // 2, 1)
    out = []
    for y in range(nh):
        r = []
        for x in range(nw):
            ps = [rows[min(2 * y + dy, h - 1)][min(2 * x + dx, w - 1)] for dy in (0, 1) for dx in (0, 1)]
            r.append(tuple(sum(p[k] for p in ps) // 4 for k in range(3)))
        out.append(r)
    return out


def level_sizes(w, h, mips):
    out = []
    for k in range(max(mips, 1)):
        mw, mh = max(w >> k, 1), max(h >> k, 1)
        out.append((mw, mh, max(mw // 4, 1) * max(mh // 4, 1) * 8))
    return out


def clear_flags(level_bytes):
    """Per block: which of its 16 pixels decode as transparent (3-colour block, index 3) - the game's own DXT1 rule."""
    out = []
    for o in range(0, len(level_bytes), 8):
        c0, c1, bits = struct.unpack_from('<HHI', level_bytes, o)
        out.append([c0 <= c1 and ((bits >> (2 * i)) & 3) == 3 for i in range(16)])
    return out


def encode_chain(rows, mips, size, original):
    """The whole mip chain; each level keeps exactly the transparent pixels of the original's level (checked)."""
    w, h = len(rows[0]), len(rows)
    data = bytearray()
    lvl, o = rows, 0
    for mw, mh, sz in level_sizes(w, h, mips):
        clear = clear_flags(original[o:o + sz])
        enc = encode_level(lvl, clear)
        assert len(enc) == sz
        assert clear_flags(enc) == clear, 'transparency changed at mip %dx%d' % (mw, mh)   # the hard rule
        data += enc
        o += sz
        lvl = half(lvl)
    assert len(data) <= size, (len(data), size)
    return bytes(data) + bytes(original[len(data):size])   # the padding after the chain, as the original has it


# ---------------------------------------------------------------- one outfit file
def colour_texture(m):
    res = m['resource'] + m['trailing']
    for i, t in enumerate(at.tex_entries(m)):
        toff, packed = struct.unpack_from('<I', t, 4)[0], struct.unpack_from('<I', t, 12)[0]
        w, h = 1 << ((packed >> 20) & 15), 1 << ((packed >> 24) & 15)
        fmt, mips = (packed >> 8) & 255, (packed >> 16) & 15
        if fmt == 0x0C and w == 512 and h == 512:
            return i, toff, packed, w, h, mips, ee.dxt_rows(res[toff:toff + w * h // 2], w, h, fmt)
    raise ValueError('no 512 x 512 DXT1 colour texture')


def more_colour_textures(m, first):
    """The other 512 x 512 DXT1 textures after the first (Ommato has three body sheets)."""
    res, out = m['resource'] + m['trailing'], []
    for i, t in enumerate(at.tex_entries(m)):
        toff, packed = struct.unpack_from('<I', t, 4)[0], struct.unpack_from('<I', t, 12)[0]
        w, h = 1 << ((packed >> 20) & 15), 1 << ((packed >> 24) & 15)
        fmt, mips = (packed >> 8) & 255, (packed >> 16) & 15
        if i != first and fmt == 0x0C and w == 512 and h == 512:
            out.append((i, toff, packed, w, h, mips, ee.dxt_rows(res[toff:toff + w * h // 2], w, h, fmt)))
    return out


def sheet(images, labels, out_png, cell=256, cols=5):
    rows_n = (len(images) + cols - 1) // cols
    W, H = cell * cols, (cell + 0) * rows_n
    canvas = [[(20, 20, 20, 255)] * W for _ in range(H)]
    for n, img in enumerate(images):
        ox, oy = (n % cols) * cell, (n // cols) * cell
        step = len(img) // cell
        for y in range(cell):
            src = img[y * step]
            row = canvas[oy + y]
            for x in range(cell):
                p = src[x * step]
                row[ox + x] = (p[0], p[1], p[2], 255) if len(p) < 4 or p[3] else (60, 0, 60, 255)   # magenta = transparent
    ee.write_png_rgba(out_png, canvas)


def main(clean, stem, outfit, out, which=None, mask=1, all_textures=False):
    src = os.path.join(clean, 'Assets', 'Data', 'Player', stem, '%s%d.alr' % (stem, outfit))
    b = open(src, 'rb').read()
    m = at.parse(b)
    ti, toff, packed, w, h, mips, rows = colour_texture(m)
    size = at.texture_bytes(packed)
    os.makedirs(out, exist_ok=True)
    ee.write_png_rgba(os.path.join(out, '00_stock.png'), rows)
    # the round trip of the stock image through this encoder (quality check of the encoder itself)
    # mask 1: skin kept (Freia, players); 2: darker skin too (JD); 0: nothing kept (monsters: their brown bodies read as skin)
    # a list of (x0, y0, x1, y1) keeps exactly those texture areas (JD: his face)
    if isinstance(mask, (list, tuple)):
        keep = [[any(x0 <= x < x1 and y0 <= y < y1 for x0, y0, x1, y1 in mask) for x in range(w)] for y in range(h)]
    else:
        keep = keep_mask_v2(rows) if mask == 2 else keep_mask(rows) if mask == 1 else [[False] * w for _ in range(h)]
    Lr = [[lum(c) for c in r] for r in rows]
    Er = edges(Lr)
    names = which or list(THEMES)
    extra = []                                              # all_textures: the other body sheets, whole (nothing kept)
    for e in (more_colour_textures(m, ti) if all_textures else []):
        eL = [[lum(c) for c in r] for r in e[6]]
        extra.append(e + (eL, edges(eL)))
    report = {'source': src, 'texture_index': ti, 'size': [w, h], 'mips': mips, 'bytes': size, 'kept_pixels':
              sum(map(sum, keep)), 'mask': mask, 'extra_textures': [e[0] for e in extra], 'variants': []}
    previews = [[tuple(p) for p in r] for r in rows]
    shown, labels = [previews], ['stock']
    for k, name in enumerate(names, 1):
        img = make_variant(rows, keep, Lr, Er, name)
        ee.write_png_rgba(os.path.join(out, '%02d_%s.png' % (k, name)),
                          [[p + (rows[y][x][3],) for x, p in enumerate(r)] for y, r in enumerate(img)])
        original = (m['resource'] + m['trailing'])[toff:toff + size]
        data = encode_chain(img, mips, size, original)
        rel = 'resource' if toff + size <= len(m['resource']) else 'trailing'
        nb = bytearray(b)
        start = m['resource_offset'] + toff
        nb[start:start + size] = data
        for ei, etoff, epacked, ew, eh, emips, erows, eL, eE in extra:
            nokeep = [[False] * ew for _ in range(eh)]
            esize = at.texture_bytes(epacked)
            eorig = (m['resource'] + m['trailing'])[etoff:etoff + esize]
            nb[m['resource_offset'] + etoff:m['resource_offset'] + etoff + esize] = encode_chain(
                make_variant(erows, nokeep, eL, eE, name), emips, esize, eorig)
        at.parse(bytes(nb))                                 # still a valid container
        alr = os.path.join(out, '%02d_%s_%s%d.alr' % (k, name, stem, outfit))
        open(alr, 'wb').write(nb)
        # what the game will show: decode the top mip back
        back = ee.dxt_rows(data[:w * h // 2], w, h, 0x0C)
        ee.write_png_rgba(os.path.join(out, '%02d_%s_ingame.png' % (k, name)), back)
        shown.append([[tuple(p) for p in r] for r in back])
        labels.append(name)
        report['variants'].append({'n': k, 'theme': name, 'title': THEMES[name][0], 'alr': os.path.basename(alr), 'buffer': rel})
        print('%02d %-9s done' % (k, name), flush=True)
    sheet(shown, labels, os.path.join(out, 'contact_sheet.png'))
    json.dump(report, open(os.path.join(out, 'report.json'), 'w'), indent=1)


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4], sys.argv[5].split(',') if len(sys.argv) > 5 else None)


# ---------------------------------------------------------------- v3.8: a theme on any built character file
THEME_ORDER = ['fire', 'ice', 'shadow', 'gold', 'toxic', 'crimson', 'ocean', 'camo', 'neon', 'spectral']   # loader rows 1-10


def apply_theme(alr_bytes, theme):
    """The theme on the file's outfit colour texture (the first 512 x 512 DXT1 texture = the body's; a head swapped in
    keeps its own texture), transparency identical on every mip (encode_chain checks it). Only those bytes change."""
    m = at.parse(alr_bytes)
    ti, toff, packed, w, h, mips, rows = colour_texture(m)
    size = at.texture_bytes(packed)
    original = (m['resource'] + m['trailing'])[toff:toff + size]
    keep = keep_mask(rows)
    Lr = [[lum(c) for c in r] for r in rows]
    Er = edges(Lr)
    img = make_variant(rows, keep, Lr, Er, theme)
    data = encode_chain(img, mips, size, original)
    out = bytearray(alr_bytes)
    start = m['resource_offset'] + toff
    out[start:start + size] = data
    return bytes(out)
