"""Character-select portraits (owner 2026-11-02: "create a neutral character sprite pack for the character's faces as well,
so that we can replace those"). Independently authored. Game facts (menu image chunk 0x10, FUN_140197750, MODLOG
2026-10-31 / 2026-11-02): the select screen's 11 faces are sprites player_00 .. player_10 (134 x 96) of the menu image -
VsMode.alr (split screen; textures 1-3, DXT5) and MNet.alr (Xbox Live; textures 9-11). This paints our renders (the
undamaged face of each player, face_sprites/<stem>/raw/v_front.png, cropped to 134:96) into those rectangles and
re-encodes the textures they sit on (DXT5); every other byte of the file is kept.
Usage: python -B build_select_portraits.py IN.alr OUT.alr FACES_DIR
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import alr_transplant as at          # noqa: E402
import st32_reference as sr          # noqa: E402
import pd_common as pc               # noqa: E402  (dxt5_block)
import face_sprite_pack as fsp       # noqa: E402  (load_png)

PW, PH = 134, 96


def portrait(faces, stem):
    """the front render (512 x 512 RGBA) -> 134 x 96 RGB on a dark backdrop (the select cell has no transparency)"""
    import os
    sp = faces + '\\' + stem + '\\raw\\s_front.png'      # v2: the shared-scale render (even framing) when there
    im = fsp.load_png(sp if os.path.exists(sp) else faces + '\\' + stem + '\\raw\\v_front.png')
    ch = int(512 * PH / PW)                     # 366 rows of the 512 render, a little above centre
    y0 = (512 - ch) // 2 - 20
    out = []
    for y in range(PH):
        for x in range(PW):
            acc = [0.0, 0.0, 0.0, 0.0]
            sx0, sx1 = x * 512 / PW, (x + 1) * 512 / PW
            sy0, sy1 = y0 + y * ch / PH, y0 + (y + 1) * ch / PH
            n = 0
            for yy in range(int(sy0), max(int(sy0) + 1, int(sy1))):
                for xx in range(int(sx0), max(int(sx0) + 1, int(sx1))):
                    p = im.px[min(511, max(0, yy)) * 512 + min(511, xx)]
                    a = p[3] / 255.0
                    for k in range(3):
                        acc[k] += p[k] * a + (16 + 8 * k) * (1 - a)    # dark blue-grey backdrop
                    n += 1
            out.append((int(acc[0] / n), int(acc[1] / n), int(acc[2] / n), 255))
    return out


def main(src, dst, faces):
    b = open(src, 'rb').read()
    R = at.parse(b)
    R['resource_dirty'] = True
    g = bytes(R['groups'][1])
    ntex, nspr = struct.unpack_from('<II', g, 8)
    te = at.tex_entries(R)
    res = bytearray(R['resource'])
    cells = {}                                   # texture -> [(x, y, stem)]
    for i in range(nspr):
        o = 0x20 + 0x34 * ntex + 0x3c * i
        nm = g[o + 4:o + 0x24].split(b'\0')[0].decode('latin1')
        if not (nm.startswith('player_') and nm[7:9].isdigit() and nm.endswith('.tga')):
            continue
        k = int(nm[7:9])
        if k > 10 or struct.unpack_from('<ii', g, o + 0x34) != (PW, PH):
            continue
        t = struct.unpack_from('<I', g, o)[0]
        u0, v0 = struct.unpack_from('<2f', g, o + 0x24)
        e = te[t]
        pk = struct.unpack_from('<I', e, 12)[0]
        w, h = 1 << ((pk >> 20) & 15), 1 << ((pk >> 24) & 15)
        cells.setdefault(t, []).append((int(u0 * w), int(v0 * h), 'pc%02da' % k))
    for t, lst in sorted(cells.items()):
        e = te[t]
        off, pk = struct.unpack_from('<I', e, 4)[0], struct.unpack_from('<I', e, 12)[0]
        w, h, fmt = 1 << ((pk >> 20) & 15), 1 << ((pk >> 24) & 15), (pk >> 8) & 0xff
        assert fmt == 0x0F and (pk >> 16) & 15 == 1, ('texture %d is not a 1-mip DXT5' % t)
        px = [tuple(p[:3]) + ((p[3] if len(p) > 3 else 255),) for p in sr.decode_tex(bytes(res), off, w, h, 0x0F)]
        for x, y, stem in lst:
            pic = portrait(faces, stem)
            for yy in range(PH):
                for xx in range(PW):
                    if 0 <= y + yy < h and 0 <= x + xx < w:
                        px[(y + yy) * w + x + xx] = pic[yy * PW + xx]
            print('texture %d: %s at %d, %d' % (t, stem, x, y))
        enc = bytearray()
        for by in range(0, h, 4):
            for bx in range(0, w, 4):
                enc += pc.dxt5_block([px[(by + j) * w + bx + i] for j in range(4) for i in range(4)])
        res[off:off + len(enc)] = enc
    R['resource'] = res
    out = at.build(R)
    assert len(out) == len(b)
    open(dst, 'wb').write(out)
    print('written', dst, len(out))


if __name__ == '__main__':
    main(*sys.argv[1:4])


FONT = {  # our own 5 x 7 capitals
    'C': ["01110", "10001", "10000", "10000", "10000", "10001", "01110"], 'O': ["01110", "10001", "10001", "10001", "10001", "10001", "01110"],
    'M': ["10001", "11011", "10101", "10101", "10001", "10001", "10001"], 'I': ["01110", "00100", "00100", "00100", "00100", "00100", "01110"],
    'N': ["10001", "11001", "10101", "10011", "10001", "10001", "10001"], 'G': ["01110", "10001", "10000", "10111", "10001", "10001", "01111"],
    'S': ["01111", "10000", "10000", "01110", "00001", "00001", "11110"], ' ': ["00000"] * 7}


def coming_soon():
    """134 x 96: 'COMING' / 'SOON' in block letters on the dark backdrop (the created-character cell)"""
    px = [(16, 24, 32, 255)] * (PW * PH)

    def word(t, y0, scale=3):
        w = len(t) * 6 * scale - scale
        x0 = (PW - w) // 2
        for i, ch in enumerate(t):
            for r, row in enumerate(FONT[ch]):
                for c, bit in enumerate(row):
                    if bit == '1':
                        for yy in range(scale):
                            for xx in range(scale):
                                x, y = x0 + (i * 6 + c) * scale + xx, y0 + r * scale + yy
                                if 0 <= x < PW and 0 <= y < PH:
                                    px[y * PW + x] = (230, 210, 150, 255)
    word('COMING', 18)
    word('SOON', 52)
    return px
