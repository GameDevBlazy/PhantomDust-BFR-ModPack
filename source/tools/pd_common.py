"""Shared Phantom Dust format helpers for the BFR pack's build tools (independently authored for this project).
PNG writing, DXT1 / DXT5 texture blocks, the .alr texture entry and bounds, stage collision grouping, the stage catalog
rows, and the stock hard-object template used for Edgar's Lab pieces. Phantom Dust formats only.
The game folder is read from the PD_GAME environment variable (see pd_paths.py)."""
import collections
import math
import os
import struct
import zlib

import pd_paths
import ssb_names

CLEAN_MAP = pd_paths.MAP


def pack_normal(n):
    x, y, z = n
    L = math.sqrt(x * x + y * y + z * z) or 1.0
    x, y, z = x / L, y / L, z / L
    xi = max(-1023, min(1023, round(x * 1023))) & 0x7FF
    yi = max(-1023, min(1023, round(y * 1023))) & 0x7FF
    zi = max(-511, min(511, round(z * 511))) & 0x3FF
    return xi | (yi << 11) | (zi << 22)


def tex_entry(name, off, w, h, mips, fmt):
    we, he = w.bit_length() - 1, h.bit_length() - 1
    assert (1 << we) == w and (1 << he) == h
    packed = (he << 24) | (we << 20) | (mips << 16) | (fmt << 8) | 0x29
    n0, n1 = ssb_names.encode(name)
    return struct.pack('<7I', 0x00040001, off, 0, packed, 0, n0, n1)


def bounds(points):
    mn = [min(p[k] for p in points) for k in range(3)]
    mx = [max(p[k] for p in points) for k in range(3)]
    c = [(mn[k] + mx[k]) / 2 for k in range(3)]
    r = max(math.sqrt(sum((p[k] - c[k]) ** 2 for k in range(3))) for p in points)
    return struct.pack('<10f', c[0], c[1], c[2], r, mn[0], mn[1], mn[2], mx[0], mx[1], mx[2]) + b'\0' * 16


CELL = 40.0


MAX_TRIS = 32


KIND, FLAGS, POOL_KIND = 8, 779, 35


def _n(t):
    A, B, C = t
    u = [B[k] - A[k] for k in range(3)]
    v = [C[k] - A[k] for k in range(3)]
    return (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])


def build_object(tris):
    pool, index = [], {}

    def vid(p):
        key = tuple(round(c, 2) for c in p)
        if key not in index:
            index[key] = len(pool)
            pool.append(key)
        return index[key]
    cells = collections.defaultdict(list)
    dropped = 0
    for t in tris:
        ids = tuple(vid(p) for p in t)
        if len(set(ids)) < 3 or sum(c * c for c in _n([pool[i] for i in ids])) < 1e-10:
            dropped += 1
            continue
        mid = [sum(pool[i][k] for i in ids) / 3 for k in range(3)]
        cells[(int(math.floor(mid[0] / CELL)), int(math.floor(mid[2] / CELL)))].append(ids)
    groups = []
    for key in sorted(cells):
        lst = cells[key]
        for s in range(0, len(lst), MAX_TRIS):
            groups.append({'kind': KIND, 'flags': FLAGS, 'b3': 0xFF, 'tris': [ids + (0,) for ids in lst[s:s + MAX_TRIS]]})
    if len(pool) > 65535:
        raise ValueError('vertex pool exceeds u16 indices')
    return {'groups': groups, 'pool': pool, 'pool_hdr': {'kind': POOL_KIND, 'b2': 3, 'b3': 0xFF}}, dropped


def floor_height_below(obj, x, z, y_from):
    best = None
    P = obj['pool']
    for g in obj['groups']:
        for t in g['tris']:
            A, B, C = P[t[0]], P[t[1]], P[t[2]]
            n = _n((A, B, C))
            L = math.sqrt(sum(c * c for c in n))
            if L == 0 or n[1] / L < 0.7:
                continue
            # barycentric test in XZ
            d = (B[2] - C[2]) * (A[0] - C[0]) + (C[0] - B[0]) * (A[2] - C[2])
            if abs(d) < 1e-9:
                continue
            l1 = ((B[2] - C[2]) * (x - C[0]) + (C[0] - B[0]) * (z - C[2])) / d
            l2 = ((C[2] - A[2]) * (x - C[0]) + (A[0] - C[0]) * (z - C[2])) / d
            l3 = 1 - l1 - l2
            if min(l1, l2, l3) < -1e-6:
                continue
            y = l1 * A[1] + l2 * B[1] + l3 * C[1]
            if y <= y_from + 1e-3 and (best is None or y > best):
                best = y
    return best


def _565(c):
    return ((c[0] >> 3) << 11) | ((c[1] >> 2) << 5) | (c[2] >> 3)


def _rgb(v):
    r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
    return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))


def dxt5_block(px):
    a = [p[3] for p in px]
    a0, a1 = max(a), min(a)
    if a0 == a1:
        aidx = [0] * 16
    else:
        pal = [a0, a1] + [((7 - i) * a0 + i * a1) // 7 for i in range(1, 7)]
        aidx = [min(range(8), key=lambda i: abs(pal[i] - v)) for v in a]
    bits = 0
    for i, ix in enumerate(aidx):
        bits |= ix << (3 * i)
    lo = [min(p[k] for p in px) for k in range(3)]
    hi = [max(p[k] for p in px) for k in range(3)]
    c0, c1 = _565(hi), _565(lo)
    if c0 < c1:
        c0, c1 = c1, c0
    e0, e1 = _rgb(c0), _rgb(c1)
    pal = [e0, e1, tuple((2 * e0[k] + e1[k]) // 3 for k in range(3)), tuple((e0[k] + 2 * e1[k]) // 3 for k in range(3))]
    cidx = 0
    if c0 != c1:
        for i, p in enumerate(px):
            j = min(range(4), key=lambda q: sum((pal[q][k] - p[k]) ** 2 for k in range(3)))
            cidx |= j << (2 * i)
    return struct.pack('<BB', a0, a1) + bits.to_bytes(6, 'little') + struct.pack('<HHI', c0, c1, cidx)


def _rgb565(c):
    return ((c >> 11) & 31) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31


def _color_block(blk, pix, x0, y0, w, dxt1):
    c0, c1, bits = struct.unpack_from('<HHI', blk, 0)
    p0, p1 = _rgb565(c0), _rgb565(c1)
    if c0 > c1 or not dxt1:
        pal = [p0 + (255,), p1 + (255,), tuple((2 * a + c) // 3 for a, c in zip(p0, p1)) + (255,),
               tuple((a + 2 * c) // 3 for a, c in zip(p0, p1)) + (255,)]
    else:
        pal = [p0 + (255,), p1 + (255,), tuple((a + c) // 2 for a, c in zip(p0, p1)) + (255,), (0, 0, 0, 0)]
    for i in range(16):
        pix[(y0 + i // 4) * w + x0 + i % 4] = pal[(bits >> (2 * i)) & 3]


def _alpha_block(blk, alphas, x0, y0, w):
    a0, a1 = blk[0], blk[1]
    bits = int.from_bytes(blk[2:8], 'little')
    if a0 > a1:
        pal = [a0, a1] + [((6 - k) * a0 + (k + 1) * a1) // 7 for k in range(6)]
    else:
        pal = [a0, a1] + [((4 - k) * a0 + (k + 1) * a1) // 5 for k in range(4)] + [0, 255]
    for i in range(16):
        alphas[(y0 + i // 4) * w + x0 + i % 4] = pal[(bits >> (3 * i)) & 7]


def png_bytes(w, h, pix):
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for p in pix[y * w:(y + 1) * w]:
            raw.extend(p)

    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data) & 0xFFFFFFFF)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(bytes(raw), 6)) + chunk(b'IEND', b''))


def catalog_rows(d):
    declared, count, pool, _ = struct.unpack_from('<IIiI', d, 0)
    rows = []
    for i in range(count):
        dsp, fam, intl, stage, var, flag, pad = struct.unpack_from('<3i4h', d, 16 + 20 * i)
        nm = []
        for o in (dsp, fam, intl):
            a = pool + o
            nm.append(d[a:d.index(b'\0', a)].decode('latin-1'))
        rows.append({'row': i, 'display': nm[0], 'family': nm[1], 'internal': nm[2], 'stage': stage,
                     'variants': var, 'flag': flag, 'pad': pad})
    return declared, count, pool, rows


def catalog_set_row(d, row, display, family, internal, stage, variants, flag):
    declared, count, pool, _ = catalog_rows(d)
    if not 0 <= row < count:
        raise ValueError('row out of range')
    # append after the last byte of the file: some owner-edited catalogs keep names past the
    # declared size, so nothing existing is moved or cut
    out = bytearray(d)
    offs = []
    for s in (display, family, internal):
        offs.append(len(out) - pool)
        out += s.encode('latin-1') + b'\0'
    struct.pack_into('<3i3h', out, 16 + 20 * row, offs[0], offs[1], offs[2], stage, variants, flag)
    struct.pack_into('<I', out, 0, len(out))
    return bytes(out)


TEMPLATE_STAGE, TEMPLATE_NAME = 'st03', 'st03_bridgeX_'


def template():
    import build_museum_st32 as mu
    import build_st32_objects as bo
    import stage_sections as ss
    dat = open(os.path.join(CLEAN_MAP, TEMPLATE_STAGE + '.dat'), 'rb').read()
    names = ss.decode_stage(dat)['names']
    k = names.index(TEMPLATE_NAME)
    parts = bo.object_parts(dat, k)
    row = mu.first_rows(dat)[k]
    assert struct.unpack_from('<BB', row, 4) == (2, 4)
    assert parts['row'][1] < 0 and parts['row'][2] < 0 and not parts['oe'] and not parts['oa'], 'template must be a hard object'
    return parts, row
