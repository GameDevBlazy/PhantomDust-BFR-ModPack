"""HUD face sprite pack for one character (owner 2026-11-02: "a sprite pack for every character's face. We need one for
being damaged. We need one for all the status effects ... Make sure the background is transparent"). Independently
authored, pure Python (no PIL): reads the face renders of render_face_sprite.py (RGBA, transparent background) and paints
each state over them - wounds only on the character, effects (sparks, ice, motion lines, auras) around it. Output:
  <out>/<state>.png      256 x 256 RGBA, transparent background
  <out>/sheet.png        every state in one transparent atlas (6 columns, 256 x 256 cells), order = sheet.json
  <out>/sheet.json       cell -> state, and the health / status each state is for
Usage: python -B face_sprite_pack.py RAW_DIR OUT_DIR
"""
import json
import math
import os
import random
import struct
import sys
import zlib

from pd_common import png_bytes  # noqa: E402


# ---------------------------------------------------------------- PNG in / out
def load_png(path):
    b = open(path, 'rb').read()
    assert b[:8] == b'\x89PNG\r\n\x1a\n'
    o, idat, w = 8, b'', 0
    while o < len(b):
        n, t = struct.unpack_from('>I4s', b, o)
        d = b[o + 8:o + 8 + n]
        if t == b'IHDR':
            w, h, depth, ctype = struct.unpack_from('>IIBB', d)
            assert depth == 8 and ctype in (2, 6), (depth, ctype)
            bpp = 4 if ctype == 6 else 3
        elif t == b'IDAT':
            idat += d
        o += 12 + n
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev, p = [], bytearray(stride), 0
    for y in range(h):
        ft = raw[p]
        line = bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            u = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if ft == 1:
                line[i] = (line[i] + a) & 255
            elif ft == 2:
                line[i] = (line[i] + u) & 255
            elif ft == 3:
                line[i] = (line[i] + (a + u) // 2) & 255
            elif ft == 4:
                pa, pb, pc = abs(u - c), abs(a - c), abs(a + u - 2 * c)
                pr = a if pa <= pb and pa <= pc else u if pb <= pc else c
                line[i] = (line[i] + pr) & 255
        rows.append(line)
        prev = line
    px = []
    for line in rows:
        for x in range(w):
            q = x * bpp
            px.append([float(line[q]), float(line[q + 1]), float(line[q + 2]), float(line[q + 3]) if bpp == 4 else 255.0])
    return Img(w, h, px)


class Img:
    def __init__(self, w, h, px):
        self.w, self.h, self.px = w, h, px

    def copy(self):
        return Img(self.w, self.h, [p[:] for p in self.px])

    def save(self, path):
        open(path, 'wb').write(png_bytes(self.w, self.h, [tuple(max(0, min(255, int(c + 0.5))) for c in p) for p in self.px]))

    def half(self):
        w, h = self.w // 2, self.h // 2
        out = []
        for y in range(h):
            for x in range(w):
                q = [self.px[(2 * y + dy) * self.w + 2 * x + dx] for dy in (0, 1) for dx in (0, 1)]
                a = sum(p[3] for p in q)
                if a <= 0:
                    out.append([0.0, 0.0, 0.0, 0.0])
                else:                                            # alpha-weighted colour: no dark fringes
                    out.append([sum(p[k] * p[3] for p in q) / a for k in range(3)] + [a / 4])
        return Img(w, h, out)


# ---------------------------------------------------------------- painting
def blend(p, col, a):
    """paint colour col (r, g, b) with opacity a (0..1) over pixel p (straight alpha)"""
    if a <= 0:
        return
    a = min(1.0, a)
    pa = p[3] / 255.0
    na = a + pa * (1 - a)
    if na <= 0:
        return
    for k in range(3):
        p[k] = (col[k] * a + p[k] * pa * (1 - a)) / na
    p[3] = na * 255.0


def tint(img, col, amt, keep_light=True):
    for p in img.px:
        if p[3] <= 0:
            continue
        L = (0.3 * p[0] + 0.59 * p[1] + 0.11 * p[2]) / 255.0
        for k in range(3):
            t = col[k] * (0.35 + 0.9 * L) if keep_light else col[k]
            p[k] = p[k] * (1 - amt) + t * amt


def desat(img, amt, dark=1.0):
    for p in img.px:
        L = 0.3 * p[0] + 0.59 * p[1] + 0.11 * p[2]
        for k in range(3):
            p[k] = (p[k] * (1 - amt) + L * amt) * dark


def alpha_blur(img, r):
    w, h = img.w, img.h
    a = [p[3] / 255.0 for p in img.px]
    for _ in range(2):
        t = [0.0] * (w * h)
        for y in range(h):
            s, row = 0.0, a[y * w:(y + 1) * w]
            for x in range(-r, r + 1):
                if 0 <= x < w:
                    s += row[x]
            for x in range(w):
                t[y * w + x] = s / (2 * r + 1)
                if 0 <= x - r < w:
                    s -= row[x - r]
                if 0 <= x + r + 1 < w:
                    s += row[x + r + 1]
        a2 = [0.0] * (w * h)
        for x in range(w):
            s = 0.0
            for y in range(-r, r + 1):
                if 0 <= y < h:
                    s += t[y * w + x]
            for y in range(h):
                a2[y * w + x] = s / (2 * r + 1)
                if 0 <= y - r < h:
                    s -= t[(y - r) * w + x]
                if 0 <= y + r + 1 < h:
                    s += t[(y + r + 1) * w + x]
        a = a2
    return a


def glow(img, col, r, strength):
    """a soft aura around the character (outside it; it shows through the transparent background)"""
    a = alpha_blur(img, r)
    out = img.copy()
    for i, p in enumerate(out.px):
        g = min(1.0, a[i] * strength)
        if p[3] < 255 and g > 0:
            under = [p[0], p[1], p[2], p[3]]
            p[0], p[1], p[2], p[3] = col[0], col[1], col[2], g * 255.0
            blend(p, under[:3], under[3] / 255.0)
    return out


def disc(img, cx, cy, rad, col, a=1.0, body=None, soft=1.5, behind=False):
    e = rad + soft + 2
    for y in range(max(0, int(cy - e)), min(img.h, int(cy + e + 1))):
        for x in range(max(0, int(cx - e)), min(img.w, int(cx + e + 1))):
            d = math.hypot(x - cx, y - cy)
            if d > rad + soft:
                continue
            k = a * min(1.0, (rad + soft - d) / soft)
            p = img.px[y * img.w + x]
            if body is not None:
                k *= (p[3] / 255.0) * body
            if behind:                                           # only on the transparent background
                k *= 1.0 - p[3] / 255.0
            blend(p, col, k)


def line(img, x0, y0, x1, y1, th, col, a=1.0, body=None, taper=False, behind=False):
    n = max(2, int(math.hypot(x1 - x0, y1 - y0) / max(1.0, th * 0.4)))
    for i in range(n + 1):
        t = i / n
        r = th * (1 - abs(2 * t - 1) * 0.7) if taper else th
        disc(img, x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, r, col, a / max(1.0, th * 0.6), body, 1.2, behind)


def ring(img, cx, cy, rad, th, col, a=1.0):
    for y in range(max(0, int(cy - rad - th - 2)), min(img.h, int(cy + rad + th + 3))):
        for x in range(max(0, int(cx - rad - th - 2)), min(img.w, int(cx + rad + th + 3))):
            d = abs(math.hypot(x - cx, y - cy) - rad)
            if d < th + 1:
                blend(img.px[y * img.w + x], col, a * min(1.0, th + 1 - d))


def poly(img, pts, col, a=1.0):
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    for y in range(max(0, int(min(ys))), min(img.h, int(max(ys)) + 1)):
        for x in range(max(0, int(min(xs))), min(img.w, int(max(xs)) + 1)):
            inside, j = False, len(pts) - 1
            for i in range(len(pts)):
                xi, yi, xj, yj = pts[i][0], pts[i][1], pts[j][0], pts[j][1]
                if (yi > y) != (yj > y) and x < (xj - xi) * (y - yi) / (yj - yi + 1e-9) + xi:
                    inside = not inside
                j = i
            if inside:
                blend(img.px[y * img.w + x], col, a)


# ---------------------------------------------------------------- the marks (512 x 512 face renders)
def cuts(img, rng, n, depth):
    spots = [(250, 300), (345, 285), (300, 225), (225, 250), (360, 330), (275, 340)]
    for i in range(n):
        cx, cy = spots[i % len(spots)]
        cx += rng.uniform(-12, 12); cy += rng.uniform(-10, 10)
        ang = rng.uniform(-0.9, -0.3)
        L = rng.uniform(70, 110)
        x0, y0 = cx - math.cos(ang) * L / 2, cy - math.sin(ang) * L / 2
        x1, y1 = cx + math.cos(ang) * L / 2, cy + math.sin(ang) * L / 2
        line(img, x0, y0, x1, y1, 9 + 3 * depth, (120, 6, 10), 1.8, body=1.0, taper=True)
        line(img, x0 + 2, y0 + 2, x1 + 2, y1 + 2, 3.5, (240, 70, 70), 1.0, body=1.0, taper=True)
        if depth >= 2:                                           # blood running down
            for k in range(rng.randint(1, 2)):
                sx = x0 + (x1 - x0) * rng.uniform(0.3, 0.8)
                sy = y0 + (y1 - y0) * rng.uniform(0.3, 0.8)
                line(img, sx, sy, sx + rng.uniform(-6, 6), sy + rng.uniform(60, 110), 6.5, (150, 8, 12), 1.4, body=1.0, taper=True)


def bruises(img, rng, n, a):
    for i, (cx, cy, r) in enumerate([(240, 300, 24), (352, 296, 20), (300, 220, 18), (330, 345, 16)][:n]):
        for k in range(3):
            disc(img, cx + rng.uniform(-5, 5), cy + rng.uniform(-5, 5), r * rng.uniform(0.6, 1.0), (85, 25, 75), a * 0.35, body=1.0, soft=r * 0.9)
        disc(img, cx, cy, r * 0.25, (150, 40, 50), a * 0.12, body=1.0, soft=r * 0.6)


def sweat(img, x, y, s=1.0):
    s *= 2.2
    poly(img, [(x, y - 14 * s), (x + 7 * s, y + 2 * s), (x, y + 9 * s), (x - 7 * s, y + 2 * s)], (170, 220, 255), 0.85)
    disc(img, x - 2 * s, y + 1 * s, 2.5 * s, (255, 255, 255), 0.8)


def sparks(img, rng, col, n, rad=230):
    for _ in range(n):
        ang = rng.uniform(0, 2 * math.pi)
        r0 = rng.uniform(120, rad)
        x, y = 256 + math.cos(ang) * r0, 256 + math.sin(ang) * r0
        pts = [(x, y)]
        for k in range(4):
            x += rng.uniform(-18, 18); y += rng.uniform(-18, 18)
            pts.append((x, y))
        for (a1, b1), (a2, b2) in zip(pts, pts[1:]):
            line(img, a1, b1, a2, b2, 4.0, col, 1.6)
            line(img, a1, b1, a2, b2, 1.5, (255, 255, 255), 1.0)


def crystal(img, cx, cy, s, col=(170, 225, 255)):
    poly(img, [(cx, cy - 22 * s), (cx + 8 * s, cy), (cx, cy + 22 * s), (cx - 8 * s, cy)], col, 0.8)
    line(img, cx, cy - 18 * s, cx, cy + 18 * s, 1.0, (255, 255, 255), 0.7)


def arrow_up(img, cx, cy, s, col):
    poly(img, [(cx, cy - 30 * s), (cx + 22 * s, cy - 6 * s), (cx + 9 * s, cy - 6 * s), (cx + 9 * s, cy + 26 * s),
               (cx - 9 * s, cy + 26 * s), (cx - 9 * s, cy - 6 * s), (cx - 22 * s, cy - 6 * s)], col, 0.95)


def chevron(img, cx, cy, s, col):
    for k in range(3):
        x = cx + k * 18 * s
        line(img, x, cy - 16 * s, x + 14 * s, cy, 4.5 * s, col, 0.95)
        line(img, x + 14 * s, cy, x, cy + 16 * s, 4.5 * s, col, 0.95)


def plus(img, cx, cy, s, col):
    line(img, cx - 12 * s, cy, cx + 12 * s, cy, 4.5 * s, col, 0.95)
    line(img, cx, cy - 12 * s, cx, cy + 12 * s, 4.5 * s, col, 0.95)


def badge(img, cx, cy, r, col):
    disc(img, cx, cy, r + 4, (12, 12, 16), 0.85)
    ring(img, cx, cy, r, 2.5, col, 1.0)


def capsule_icon(img, cx, cy, s, col, empty=True):
    w, h = 18 * s, 34 * s
    for y in range(int(cy - h), int(cy + h)):
        for x in range(int(cx - w), int(cx + w)):
            dy = max(0.0, abs(y - cy) - (h - w))
            d = math.hypot(x - cx, dy)
            edge = abs(d - w)
            if edge < 3 * s:
                blend(img.px[y * img.w + x], col, 0.95)
            elif d < w and not empty:
                blend(img.px[y * img.w + x], col, 0.5)


def hand_icon(img, cx, cy, s, col):
    disc(img, cx, cy + 10 * s, 20 * s, col, 1.0)                 # palm
    for k, (dx, L) in enumerate([(-15, 26), (-5, 32), (5, 33), (15, 28)]):
        line(img, cx + dx * s, cy + 2 * s, cx + dx * s, cy - L * s, 5.5 * s, col, 1.0)
    line(img, cx - 20 * s, cy + 14 * s, cx - 32 * s, cy - 2 * s, 5.5 * s, col, 1.0)


def speed_lines(img, rng, up, n=22):
    for _ in range(n):
        x = rng.uniform(70, 440)
        y0 = rng.uniform(300, 470) if up else rng.uniform(40, 200)
        L = rng.uniform(40, 90)
        y1 = y0 + L if up else y0 - L
        line(img, x, y0, x, y1, 2.6, (235, 240, 255), 1.2, behind=True)


def glitch(img, rng):
    w = img.w
    out = img.copy()
    y = 0
    while y < img.h:
        hgt = rng.randint(4, 22)
        shift = rng.choice([0, 0, rng.randint(-26, 26)])
        gone = rng.random() < 0.12
        for yy in range(y, min(img.h, y + hgt)):
            for x in range(w):
                sx = min(w - 1, max(0, x - shift))
                p = img.px[yy * w + sx][:]
                if gone:
                    p[3] *= 0.15
                r = img.px[yy * w + min(w - 1, max(0, sx + 6))]
                b = img.px[yy * w + min(w - 1, max(0, sx - 6))]
                p[0], p[2] = r[0], b[2]
                out.px[yy * w + x] = p
        y += hgt
    return out


def ko_x(img, cx, cy, s):
    for sg in (1, -1):
        line(img, cx - 20 * s, cy - 20 * s * sg, cx + 20 * s, cy + 20 * s * sg, 5 * s, (230, 30, 30), 1.0)


def stars_dizzy(img, cx, cy, n=5):
    for k in range(n):
        a = 2 * math.pi * k / n
        x, y = cx + math.cos(a) * 120, cy + math.sin(a) * 34
        pts = []
        for j in range(10):
            r = 14 if j % 2 == 0 else 6
            aa = -math.pi / 2 + j * math.pi / 5
            pts.append((x + math.cos(aa) * r, y + math.sin(aa) * r))
        poly(img, pts, (255, 220, 60), 0.95)


# ---------------------------------------------------------------- the states
STATES = [
    # name, view, health / status it stands for
    ('health_20_full', 'front', 'life 20 (start): the natural look'),
    ('health_10_hurt', 'front', 'life 10-19: scuffed, a cut, sweat'),
    ('health_5_pretty_hurt', 'flinch', 'life 5-9: bruised, cuts'),
    ('health_3_seriously_hurt', 'flinch', 'life 3-4: bleeding, drained colour'),
    ('health_1_critical', 'slump', 'life 1-2: critical, red pulse'),
    ('health_0_ko', 'slump', 'life 0: knocked out'),
    ('damaged', 'flinch', 'taking a hit (flash)'),
    ('poison', 'front', 'status: poison'),
    ('paralysis', 'front', 'status: paralysis'),
    ('inverted_controls', 'front', 'status: inverted controls'),
    ('frozen', 'front', 'status: frozen'),
    ('frozen_hand', 'front', 'status: frozen hand (skills locked)'),
    ('knocked_up', 'back', 'knocked up into the air'),
    ('falling', 'fall', 'falling from a great height'),
    ('out_of_capsules', 'front', 'no capsules left in the arsenal'),
    ('speed_up', 'front', 'speed acquisition'),
    ('strength_up', 'front', 'strength acquisition'),
    ('aura_recovery', 'front', 'aura recovery'),
    ('regeneration', 'front', 'regeneration speed up'),
    ('erased', 'front', 'hit by an erase skill'),
    ('max_aura', 'front', 'at max aura'),
    ('low_health', 'slump', 'low life warning (pulse frame)'),
    ('invisible', 'front', 'status: invisible'),
]


def make(name, base):
    rng = random.Random(name)
    im = base.copy()
    if name == 'health_10_hurt':
        tint(im, (255, 120, 100), 0.10, False); cuts(im, rng, 2, 0); bruises(im, rng, 1, 1.0); sweat(im, 395, 230, 1.0)
    elif name == 'health_5_pretty_hurt':
        desat(im, 0.2, 0.92); tint(im, (255, 90, 80), 0.14, False); bruises(im, rng, 2, 1.3); cuts(im, rng, 3, 1); sweat(im, 400, 240, 1.0)
    elif name == 'health_3_seriously_hurt':
        desat(im, 0.4, 0.82); tint(im, (255, 60, 60), 0.18, False); bruises(im, rng, 3, 1.5); cuts(im, rng, 4, 2)
        im = glow(im, (200, 20, 30), 7, 1.0)
    elif name == 'health_1_critical':
        desat(im, 0.55, 0.75); tint(im, (255, 40, 40), 0.28, False); bruises(im, rng, 4, 1.7); cuts(im, rng, 5, 3)
        im = glow(im, (240, 20, 30), 12, 2.2)
    elif name == 'health_0_ko':
        bruises(im, rng, 4, 1.5); cuts(im, rng, 5, 3); desat(im, 0.9, 0.55)
        stars_dizzy(im, 256, 120); ko_x(im, 440, 440, 1.2)
    elif name == 'damaged':
        cuts(im, rng, 2, 1); tint(im, (255, 40, 40), 0.45, False)
        for k in range(10):
            a = 2 * math.pi * k / 10 + 0.2
            line(im, 256 + math.cos(a) * 170, 256 + math.sin(a) * 170, 256 + math.cos(a) * 235, 256 + math.sin(a) * 235, 4, (255, 240, 120), 0.95)
    elif name == 'poison':
        tint(im, (110, 200, 70), 0.42)
        for k in range(12):
            x, y, r = rng.uniform(80, 440), rng.uniform(60, 470), rng.uniform(7, 17)
            ring(im, x, y, r, 2.2, (150, 255, 90), 0.9); disc(im, x - r * 0.3, y - r * 0.3, r * 0.25, (230, 255, 220), 0.8)
        im = glow(im, (120, 40, 160), 8, 1.2)
    elif name == 'paralysis':
        desat(im, 0.2); tint(im, (255, 230, 90), 0.18)
        sparks(im, rng, (255, 225, 40), 9)
    elif name == 'inverted_controls':
        tint(im, (170, 90, 230), 0.25)
        ring(im, 256, 256, 215, 4, (200, 120, 255), 0.9)
        for a0 in (0.3, 0.3 + math.pi):                          # two arrows chasing each other the wrong way round
            for k in range(14):
                a = a0 - k * 0.09
                disc(im, 256 + math.cos(a) * 215, 256 + math.sin(a) * 215, 7, (240, 200, 255), 0.95)
            a = a0 + 0.02
            x, y = 256 + math.cos(a) * 215, 256 + math.sin(a) * 215
            poly(im, [(x + math.cos(a + math.pi / 2) * 24, y + math.sin(a + math.pi / 2) * 24),
                      (x + math.cos(a) * 18, y + math.sin(a) * 18), (x - math.cos(a) * 18, y - math.sin(a) * 18)], (240, 200, 255), 1.0)
    elif name == 'frozen':
        tint(im, (150, 205, 255), 0.55)
        for k in range(16):
            a = 2 * math.pi * k / 16
            crystal(im, 256 + math.cos(a) * 215, 256 + math.sin(a) * 205, rng.uniform(0.8, 1.4))
        im = glow(im, (190, 235, 255), 7, 1.3)
    elif name == 'frozen_hand':
        badge(im, 420, 420, 66, (160, 220, 255))
        hand_icon(im, 420, 425, 1.25, (230, 205, 180))
        for y in range(360, 486):                                # an ice block over the hand
            for x in range(360, 482):
                if math.hypot(x - 420, y - 420) < 62:
                    blend(im.px[y * im.w + x], (170, 225, 255), 0.45)
        crystal(im, 455, 380, 0.8); crystal(im, 385, 465, 0.7)
    elif name == 'knocked_up':
        speed_lines(im, rng, True); sweat(im, 395, 230, 1.0)
    elif name == 'falling':
        speed_lines(im, rng, False); sweat(im, 120, 250, 1.1); sweat(im, 400, 220, 0.9)
    elif name == 'out_of_capsules':
        desat(im, 0.25)
        badge(im, 420, 420, 66, (240, 90, 60))
        capsule_icon(im, 420, 420, 1.0, (235, 235, 235))
        line(im, 375, 465, 465, 375, 5, (240, 60, 50), 1.0)
    elif name == 'speed_up':
        im = glow(im, (60, 170, 255), 7, 1.1)
        badge(im, 420, 420, 66, (80, 180, 255)); chevron(im, 395, 420, 1.1, (120, 210, 255))
    elif name == 'strength_up':
        im = glow(im, (255, 110, 30), 9, 1.4)
        badge(im, 420, 420, 66, (255, 120, 40)); arrow_up(im, 420, 422, 1.3, (255, 150, 60))
    elif name == 'aura_recovery':
        im = glow(im, (255, 210, 80), 8, 1.2)
        for k in range(18):
            x, y = rng.uniform(60, 450), rng.uniform(50, 470)
            s = rng.uniform(5, 11)
            line(im, x - s, y, x + s, y, 1.6, (255, 240, 160), 0.95); line(im, x, y - s, x, y + s, 1.6, (255, 240, 160), 0.95)
    elif name == 'regeneration':
        tint(im, (120, 255, 150), 0.12)
        for k in range(7):
            plus(im, rng.uniform(70, 440), rng.uniform(60, 460), rng.uniform(0.8, 1.4), (90, 240, 120))
        im = glow(im, (90, 240, 120), 6, 0.9)
    elif name == 'erased':
        im = glitch(im, rng)
    elif name == 'max_aura':
        im = glow(im, (255, 215, 90), 14, 2.2)
        tint(im, (255, 240, 200), 0.12, False)
    elif name == 'invisible':
        # owner 2026-11-02: "a little transparent ... don't give it an outline": see-through, cool tint, nothing else
        for p in im.px:
            L = 0.3 * p[0] + 0.59 * p[1] + 0.11 * p[2]
            p[0], p[1], p[2] = L * 0.6 + 70, L * 0.7 + 90, L * 0.8 + 120
            p[3] *= 0.3
    elif name == 'low_health':
        desat(im, 0.55, 0.75); tint(im, (255, 20, 20), 0.5, False); bruises(im, rng, 4, 1.7); cuts(im, rng, 5, 3)
        im = glow(im, (255, 30, 40), 16, 2.8)
    return im


def main(raw, out):
    os.makedirs(out, exist_ok=True)
    views = {v: load_png(os.path.join(raw, 'v_%s.png' % v)) for v in ('front', 'flinch', 'slump', 'back', 'fall')}
    cells = []
    for i, (name, view, what) in enumerate(STATES):
        im = make(name, views[view]).half()
        im.save(os.path.join(out, name + '.png'))
        cells.append({'cell': i, 'state': name, 'for': what, 'view': view})
        print(name, flush=True)
    cols, cell = 6, 256
    rows = (len(STATES) + cols - 1) // cols
    sheet = Img(cols * cell, rows * cell, [[0.0, 0.0, 0.0, 0.0] for _ in range(cols * cell * rows * cell)])
    for c in cells:
        im = load_png(os.path.join(out, c['state'] + '.png'))
        ox, oy = (c['cell'] % cols) * cell, (c['cell'] // cols) * cell
        for y in range(cell):
            for x in range(cell):
                sheet.px[(oy + y) * sheet.w + ox + x] = im.px[y * cell + x]
    sheet.save(os.path.join(out, 'sheet.png'))
    json.dump({'cell_size': cell, 'columns': cols, 'cells': cells}, open(os.path.join(out, 'sheet.json'), 'w'), indent=1)


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
