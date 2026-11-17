"""Pure-Python DXT1 (BC1) decode/encode and PNG read/write (standard library only; 2026-10-21).

Used for small UI edits (stage-select thumbnails in Menu/VsMode.alr). Not fast; fine for sheets up to
2048 x 512 and regions of a few hundred pixels. Encoder: per 4x4 block, endpoints = the two colours
with the largest distance among the block's pixels (simple and stable), 4-colour mode.
"""
import struct
import zlib


def _565(c):
    r, g, b = (c >> 11) & 31, (c >> 5) & 63, c & 31
    return (r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2)


def decode(data, w, h):
    """-> list of rows of (r, g, b, a)."""
    px = [[(0, 0, 0, 255)] * w for _ in range(h)]
    o = 0
    for by in range(0, h, 4):
        for bx in range(0, w, 4):
            c0, c1, bits = struct.unpack_from('<HHI', data, o)
            o += 8
            a, b = _565(c0), _565(c1)
            if c0 > c1:
                pal = [a + (255,), b + (255,), tuple((2 * x + y) // 3 for x, y in zip(a, b)) + (255,),
                       tuple((x + 2 * y) // 3 for x, y in zip(a, b)) + (255,)]
            else:
                pal = [a + (255,), b + (255,), tuple((x + y) // 2 for x, y in zip(a, b)) + (255,), (0, 0, 0, 0)]
            for i in range(16):
                y, x = by + i // 4, bx + i % 4
                if y < h and x < w:
                    px[y][x] = pal[(bits >> (2 * i)) & 3]
    return px


def _to565(c):
    return (c[0] >> 3) << 11 | (c[1] >> 2) << 5 | (c[2] >> 3)


def encode_block(block):
    """block: 16 (r, g, b) -> 8 bytes (4-colour DXT1)."""
    best, pair = -1, (block[0], block[0])
    for i in range(16):
        for j in range(i + 1, 16):
            d = sum((p - q) ** 2 for p, q in zip(block[i], block[j]))
            if d > best:
                best, pair = d, (block[i], block[j])
    c0, c1 = _to565(pair[0]), _to565(pair[1])
    if c0 < c1:
        c0, c1 = c1, c0
    if c0 == c1:
        return struct.pack('<HHI', c0, c1, 0)
    a, b = _565(c0), _565(c1)
    pal = [a, b, tuple((2 * x + y) // 3 for x, y in zip(a, b)), tuple((x + 2 * y) // 3 for x, y in zip(a, b))]
    bits = 0
    for i, p in enumerate(block):
        k = min(range(4), key=lambda q: sum((u - v) ** 2 for u, v in zip(p, pal[q])))
        bits |= k << (2 * i)
    return struct.pack('<HHI', c0, c1, bits)


def write_region(data, w, x0, y0, rgb_rows):
    """Re-encode the 4x4 blocks covering rgb_rows placed at (x0, y0) (x0, y0, width, height multiples of 4)."""
    out = bytearray(data)
    rh, rw = len(rgb_rows), len(rgb_rows[0])
    assert x0 % 4 == 0 and y0 % 4 == 0 and rw % 4 == 0 and rh % 4 == 0
    for by in range(0, rh, 4):
        for bx in range(0, rw, 4):
            blk = [rgb_rows[by + i // 4][bx + i % 4][:3] for i in range(16)]
            o = (((y0 + by) // 4) * (w // 4) + (x0 + bx) // 4) * 8
            out[o:o + 8] = encode_block(blk)
    return bytes(out)


def write_png(path, rows):
    h, w = len(rows), len(rows[0])
    raw = b''.join(b'\0' + bytes(c for p in r for c in (p + (255,))[:4]) for r in rows)

    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
                           + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def read_png(path):
    """8-bit RGB/RGBA non-interlaced PNG -> rows of (r, g, b, a)."""
    b = open(path, 'rb').read()
    assert b[:8] == b'\x89PNG\r\n\x1a\n'
    o, idat, w = 8, b'', 0
    while o < len(b):
        n, t = struct.unpack_from('>I4s', b, o)
        d = b[o + 8:o + 8 + n]
        if t == b'IHDR':
            w, h, depth, ctype, _, _, inter = struct.unpack('>IIBBBBB', d)
            assert depth == 8 and ctype in (2, 6) and inter == 0, (depth, ctype, inter)
            bpp = 4 if ctype == 6 else 3
        elif t == b'IDAT':
            idat += d
        o += 12 + n
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev, i = [], bytearray(stride), 0
    for _ in range(h):
        f, line = raw[i], bytearray(raw[i + 1:i + 1 + stride])
        i += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            up = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + up) & 255
            elif f == 3:
                line[x] = (line[x] + (a + up) // 2) & 255
            elif f == 4:
                p = a + up - c
                pa, pb, pc = abs(p - a), abs(p - up), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else up if pb <= pc else c)) & 255
        rows.append([tuple(line[x:x + bpp]) + ((255,) if bpp == 3 else ()) for x in range(0, stride, bpp)])
        prev = line
    return rows


def resize(rows, w, h):
    """Box/nearest resample to w x h."""
    sh, sw = len(rows), len(rows[0])
    out = []
    for y in range(h):
        y0, y1 = y * sh // h, max(y * sh // h + 1, (y + 1) * sh // h)
        r = []
        for x in range(w):
            x0, x1 = x * sw // w, max(x * sw // w + 1, (x + 1) * sw // w)
            acc, n = [0, 0, 0, 0], 0
            for yy in range(y0, y1):
                for xx in range(x0, x1):
                    p = rows[yy][xx]
                    for k in range(4):
                        acc[k] += p[k]
                    n += 1
            r.append(tuple(v // n for v in acc))
        out.append(r)
    return out
