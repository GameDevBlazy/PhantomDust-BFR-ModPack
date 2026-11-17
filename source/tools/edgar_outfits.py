"""Edgar's new outfits (owner 2026-11-15: "replace Edgar's outfit 4 ... creating entirely new outfits for outfits 6 through 15
... The goal is to make each one unique"). Independently authored, on outfit_generator.py's decoder / DXT1 encoder (the
transparency of every mip kept exactly - hard owner rule).
Each design gives the three materials of Edgar's outfit-1 sheet its own colour - LEATHER (the dark coat, belts, straps),
CLOTH (the red shirt / lining), METAL (buckles, studs, rings) - and most give the leather a pattern of its own; the light
and shade of every pixel is kept (folds, seams and stitching stay where the model's UVs expect them). Skin, the face and the
hair are kept as they are.
Writes OUT/pc01a<k>.alr (k = 3 for outfit 4, 5..14 for outfits 6-15), PNG previews and a contact sheet.
Usage: python -B edgar_outfits.py CLEAN_ROOT OUT"""
import json
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import alr_transplant as at  # noqa: E402
import export_all_effects as ee  # noqa: E402
import outfit_generator as og  # noqa: E402

KEEP_BOXES = [(0, 0, 150, 182), (372, 0, 512, 136)]          # the face and the hair (texture areas)


def cls(c):
    r, g, b = c[0], c[1], c[2]
    L = og.lum(c)
    mx, mn = max(r, g, b), min(r, g, b)
    sat = (mx - mn) / mx if mx else 0.0
    if r > 55 and r > g * 1.45 and r > b * 1.35:
        return 'cloth'
    if L > 0.52 and sat < 0.22:
        return 'metal'
    return 'leather'


def n2(x, y, s, seed):
    return og.value_noise(x, y, s, seed)


# pattern(x, y, L) -> 0..1 (mix between the leather's two colours)
def p_plain(x, y):
    return 0.0


def p_lava(x, y):
    v = abs(n2(x, y, 34, 11) - 0.5) * 2
    return 1.0 if v < 0.07 else 0.0


def p_frost(x, y):
    return min(1.0, max(0.0, (n2(x, y, 7, 21) - 0.55) * 4)) * 0.8


def p_camo(x, y):
    n = n2(x, y, 30, 3) * 0.65 + n2(x, y, 10, 5) * 0.35
    return 0.0 if n < 0.42 else 0.5 if n < 0.58 else 1.0


def p_digital(x, y):
    bx, by = x // 6, y // 6
    n = n2(bx * 6, by * 6, 18, 7) * 0.7 + og.hash01(bx, by, 9) * 0.3
    return 0.0 if n < 0.45 else 0.5 if n < 0.62 else 1.0


def p_tiger(x, y):
    w = math.sin((x * 0.9 + y * 0.35) / 9.0 + n2(x, y, 22, 31) * 6.0)
    return 1.0 if w > 0.72 else 0.0


def p_circuit(x, y):
    gx, gy = x % 24, y % 24
    on = (gx < 2 and og.hash01(x // 24, y // 24, 41) > 0.4) or (gy < 2 and og.hash01(x // 24, y // 24, 43) > 0.5)
    pad = (gx - 12) ** 2 + (gy - 12) ** 2 < 6 and og.hash01(x // 24, y // 24, 47) > 0.7
    return 1.0 if on or pad else 0.0


def p_tartan(x, y):
    a = (x % 32) < 10
    b = (y % 32) < 10
    thin = (x % 32) in (20, 21) or (y % 32) in (20, 21)
    return 1.0 if (a and b) else 0.55 if (a or b) else 0.3 if thin else 0.0


def p_scales(x, y):
    row = y // 8
    cx = (x + (4 if row % 2 else 0)) % 8 - 4
    cy = y % 8
    d = math.sqrt(cx * cx + (cy - 1) ** 2)
    return 1.0 if 3.2 < d < 4.6 else 0.0


def p_dunes(x, y):
    return 0.5 + 0.5 * math.sin((y + n2(x, y, 40, 51) * 30) / 6.0)


def p_stars(x, y):
    h = og.hash01(x, y, 61)
    return 1.0 if h > 0.985 else 0.0


# (title, leather base, leather pattern colour, pattern, cloth, metal)
DESIGNS = {
    3: ('Royal Navy', (28, 36, 74), (40, 52, 104), p_plain, (226, 214, 180), (222, 178, 60)),
    5: ('Inferno', (34, 30, 30), (255, 120, 20), p_lava, (230, 90, 20), (60, 58, 64)),
    6: ('Glacier', (128, 158, 196), (214, 232, 250), p_frost, (30, 70, 150), (210, 225, 240)),
    7: ('Jungle', (54, 66, 40), (110, 96, 58), p_camo, (92, 104, 60), (70, 72, 70)),
    8: ('Urban Digital', (70, 74, 80), (128, 132, 138), p_digital, (240, 110, 20), (30, 30, 32)),
    9: ('Tiger', (214, 110, 30), (20, 16, 14), p_tiger, (24, 22, 22), (200, 160, 80)),
    10: ('Neon Circuit', (16, 18, 24), (40, 240, 255), p_circuit, (230, 40, 190), (60, 230, 255)),
    11: ('Tartan', (120, 26, 30), (24, 70, 44), p_tartan, (230, 222, 196), (150, 150, 154)),
    12: ('Serpent', (20, 96, 60), (6, 36, 24), p_scales, (220, 180, 60), (176, 120, 60)),
    13: ('Desert', (196, 164, 112), (170, 136, 88), p_dunes, (24, 128, 130), (190, 110, 60)),
    14: ('Void', (30, 14, 50), (230, 220, 255), p_stars, (150, 60, 230), (236, 236, 244)),
}


def design(rows, keep, k):
    title, lb, lp, pat, cloth, metal = DESIGNS[k]
    h, w = len(rows), len(rows[0])
    Lr = [[og.lum(c) for c in r] for r in rows]
    kinds = [[None if keep[y][x] else cls(rows[y][x]) for x in range(w)] for y in range(h)]
    mean = {}
    for name in ('leather', 'cloth', 'metal'):
        v = [Lr[y][x] for y in range(h) for x in range(w) if kinds[y][x] == name]
        mean[name] = (sum(v) / len(v)) if v else 0.5
    out = []
    for y in range(h):
        r = []
        for x in range(w):
            c = rows[y][x]
            kd = kinds[y][x]
            if kd is None:
                r.append(c[:3]); continue
            s = Lr[y][x] / max(0.05, mean[kd])
            s = max(0.25, min(1.9, s))
            if kd == 'leather':
                s = max(0.4, min(1.45, s))                     # v2: no blown-out highlights on the coat
                t = pat(x, y)
                base = og.mix(lb, lp, t)
                glow = pat in (p_lava, p_circuit, p_stars) and t > 0.5
                col = base if glow else tuple(v * s for v in base)
            elif kd == 'cloth':
                col = tuple(v * s for v in cloth)
            else:
                col = tuple(v * (0.55 + 0.45 * s) + 40 * max(0.0, s - 1.0) for v in metal)
            r.append(tuple(max(0, min(255, int(v))) for v in col))
        out.append(r)
    return out


def main(clean, out):
    src = os.path.join(clean, 'Assets', 'Data', 'Player', 'pc01a', 'pc01a0.alr')
    b = open(src, 'rb').read()
    m = at.parse(b)
    ti, toff, packed, w, h, mips, rows = og.colour_texture(m)
    size = at.texture_bytes(packed)
    original = (m['resource'] + m['trailing'])[toff:toff + size]
    skin = og.keep_mask(rows)
    keep = [[skin[y][x] or any(x0 <= x < x1 and y0 <= y < y1 for x0, y0, x1, y1 in KEEP_BOXES) for x in range(w)] for y in range(h)]
    os.makedirs(out, exist_ok=True)
    shown, labels, rep = [[tuple(p) for p in r] for r in rows], ['outfit 1 (stock)'], []
    shown = [shown]
    for k in sorted(DESIGNS):
        img = design(rows, keep, k)
        data = og.encode_chain(img, mips, size, original)
        nb = bytearray(b)
        nb[m['resource_offset'] + toff:m['resource_offset'] + toff + size] = data
        at.parse(bytes(nb))
        open(os.path.join(out, 'pc01a%d.alr' % k), 'wb').write(nb)
        back = ee.dxt_rows(data[:w * h // 2], w, h, 0x0C)
        ee.write_png_rgba(os.path.join(out, 'outfit%02d_%s.png' % (k + 1, DESIGNS[k][0].replace(' ', '_'))), back)
        shown.append([[tuple(p) for p in r] for r in back])
        labels.append('outfit %d %s' % (k + 1, DESIGNS[k][0]))
        rep.append({'outfit': k + 1, 'file': 'pc01a%d.alr' % k, 'design': DESIGNS[k][0]})
        print('outfit %2d %s' % (k + 1, DESIGNS[k][0]), flush=True)
    og.sheet(shown, labels, os.path.join(out, 'contact_sheet.png'), cell=256, cols=4)
    json.dump({'source': src, 'outfits': rep}, open(os.path.join(out, 'report.json'), 'w'), indent=1)


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
