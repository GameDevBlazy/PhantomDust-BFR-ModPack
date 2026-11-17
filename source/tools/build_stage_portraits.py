"""Stage portraits for BOTH stage lists, with the game's own scan-line look (owner 2026-10-31: "I want the level list
pictures finished. They should have that gridded line like the original phantom dust level list pictures").
Supersedes build_vsmode_portraits.py (5.40: the split-screen list only, no lines). Independently authored; game facts
from PD Ghidra (full pass 065, pd_fn.py) and capstone, recorded in MODLOG 2026-10-31:
- The stock stage pictures (mission_map_stNN, 254 x 112) carry an opaque black row on every third row (rows 2, 5, 8 ...
  of the picture, DXT1 4-colour mode, alpha 255) - that is the "gridded line" look; the rest of the row is the picture.
- Split-screen STAGE LIST: vsmenu SSEL picture = 2.82(image slot 0, table_0x1f38[row], 48, 261), image = Menu/VsMode.alr.
- Xbox Live LEVEL LIST: livestagesel 0x17cc = 2.82(image slot 3, table_0x1328[row], 48, 261), image = Menu/MNet.alr
  (12 textures, 84 sprites; 49-65 = mission_map_st02..st26 on textures 0-2).
- Menu image chunk 0x10 (loader FUN_140197750): data = chunk + 8: byte +0 texture count, byte +4 sprite count, +8 name,
  +0x18 texture names (0x20 each), then descriptors 0x14 each {i16 w, i16 h, +4 (not read by the loader; the stock
  files hold the DDS size of the top mip = w*h*bpp/8 + 128), +8 sprites on this texture, +0xc kind (4 = DXT1 texture,
  8 = DXT5 texture; kinds < 0x2f in mask 0x44001000f800 are not textures), +0x10 written 0}, then sprite records 0x3c
  {u32 texture, name 0x20, f32 u0 v0 u1 v1, i32 w, i32 h}; rect = (w*u0, h*v0, w*u1 - x, h*v1 - y) truncated; one
  0x200-byte handle buffer per image = at most 128 sprites.
v2 (2026-10-31 22:xx, after the in-game check): the menu image file is read into the game's menu pool (pool 0, 2.57 MB, FUN_14015f7e0;
1.73 MB free with the stock VsMode loaded) - a 2 MB atlas inside the file never loads (black page). So the file now carries
only a SMALL texture (256 x 1024 DXT1, 128 KB: the 8 Map Slot pictures, one per 112-row cell), and the full atlas goes to
a loader-side file Map\\mx_portraits.dxt (raw DXT1 2048 x 2048) that the loader uploads into its own D3D11 copy of the
small texture (the game's textures are IMMUTABLE: FUN_1400bcac0 desc usage 1).
What this builds (packs/level_list_v2_2026-10-31/):
- VsMode.alr: the stock file + texture 4 "vsmode0900" (2048 x 2048 DXT1, 256 x 112 cells, 8 x 18) + 8 sprites: 63
  "cwo_mapslot7" (the loader moves its rect to the chosen pack's cell), 64-69 Map Slots 1-6, 70 Map Slot 8.
- MNet.alr: the INSTALLED file (the owner's menu art kept) + texture 12 "mnet0900" (the same atlas) + 8 sprites 84-91
  in the same order (84 = cwo_mapslot7).
- livestagesel_en.ssb: the installed one with table_0x1328 rows 11, 18-21, 27, 28, 29 (Map Slots 1-8) -> 85-89, 90,
  84, 91. (vsmenu_en.ssb already points rows at 63-70 since 5.40: unchanged, checked.)
- mx_portraits.txt: "#image<TAB>slot<TAB>textures<TAB>sprites<TAB>texture<TAB>sprite" per image (loader 5.41), the 5.40
  keys, and "<pack file><TAB><cell>" per mx_list row.
Every picture (pack pictures, renders, stock copies) gets the scan lines before encoding.
Usage: python -B -I build_stage_portraits.py CLEAN_VSMODE INSTALLED_MNET INSTALLED_VSMENU INSTALLED_LIVESTAGESEL
       INSTALLED_MX_LIST PORTRAIT_DIR OUT_DIR
"""
import hashlib
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import alr_transplant as at                  # noqa: E402
import pd_common as pc      # noqa: E402  (tex_entry)
import dxt1_tools                            # noqa: E402
import st32_reference as sr                  # noqa: E402
import ssb_names                             # noqa: E402

ATLAS = 2048
SMALL_W, SMALL_H = 256, 1024                # the in-file texture: 9 cells of 256 x 112, one per Map Slot sprite
CW, CH, COLS, ROWS = 256, 112, 8, 18         # cell stride; the picture is 254 x 112 at the cell's left
PW, PH = 254, 112
LINE_PHASE = 2                               # picture rows r with r % 3 == LINE_PHASE are black (the stock look)
VSMODE_SHA, MNET_SHA = '5b292b11', '56eb6efc'
VS_TABLE = [3, 3, 0, 1, 2, 3, 4, 5, 6, 3, 3, 3, 7, 8, 9, 10, 11, 12, 0, 0, 0, 0, 13, 14, 0, 15, 16, 0, 0, 0]   # vsmenu 0x1f38 (stock)
LIVE_TABLE = [3, 3, 49, 50, 51, 52, 53, 54, 55, 3, 3, 3, 56, 57, 58, 59, 60, 61, 0, 0, 0, 0, 62, 63, 0, 64, 65, 0, 0, 0]  # livestagesel 0x1328 (stock)
# v2b (in-game check 22:28): the picture tables are indexed by STAGE ID (0-29: the list's "row" = catalog row + 10 x
# variant, e.g. Lost / Light / Noble Palace = 3 / 13 / 23, Highway's 3rd variant = 22), not by catalog
# row; the split-screen and Xbox Live lists hold the 17 stock stage ids only (no Map Slot rows). Entries: 22 = Highway's 3rd
# variant (Map Slot 1, its st32 render), 10 = Map Slot 7 (the dynamic row), 27 / 28 / 18 / 20 = Map Slots 4 / 5 / 6 / 8.
SLOT_ROWS = {10: 0, 22: 1, 27: 4, 28: 5, 18: 6, 20: 7}                   # stage id -> sprite offset from the image's base
SLOT_STAGES = [('cwo_mapslot1', 'st32'), ('cwo_mapslot2', 'st33'), ('cwo_mapslot3', 'st34'), ('cwo_mapslot4', 'st27'),
               ('cwo_mapslot5', 'st28'), ('cwo_mapslot6', 'st18'), ('cwo_mapslot8', 'st20')]
STOCK = {'pc02': 'st02', 'pc03': 'st03', 'pc04': 'st04', 'pc05': 'st05', 'pc06': 'st06', 'pc07a': 'st07', 'pc07b': 'st07',
         'pc08': 'st08'}
IMAGES = {'VsMode': dict(sha=VSMODE_SHA, ntex=4, nspr=63, tex_name='VsMode0900.dds', entry='vsmode0900', slot=0, base=63),
          'MNet': dict(sha=MNET_SHA, ntex=12, nspr=84, tex_name='MNet0900.dds', entry='mnet0900', slot=3, base=84)}


def sha8(b):
    return hashlib.sha256(b).hexdigest()[:8]


def cell_xy(c):
    return (c % COLS) * CW, (c // COLS) * CH


def lum(p):
    return 299 * p[0] + 587 * p[1] + 114 * p[2]


def scanlines(rows):
    """The stock look: every third picture row (2, 5, 8 ...) opaque black."""
    return [[(0, 0, 0)] * PW if r % 3 == LINE_PHASE else [p[:3] for p in row] for r, row in enumerate(rows)]


def dxt1_block(px):
    """16 RGB -> 8 bytes, 4-colour mode with endpoint 1 = black (every block holds a scan line row, kept exactly
    black) and endpoint 0 fitted by least squares to the picture pixels along the line through black (each picture
    pixel takes the nearest of c0, 2/3 c0, 1/3 c0, black)."""
    pic = [p for p in px if p != (0, 0, 0)]
    if not pic:
        return struct.pack('<HHI', 0, 0, 0)
    c0 = [float(v) for v in max(pic, key=lum)]
    for _ in range(4):
        num, den = [0.0, 0.0, 0.0], 0.0
        for p in pic:
            s = min((1.0, 2.0 / 3.0, 1.0 / 3.0), key=lambda t: sum((p[k] - t * c0[k]) ** 2 for k in range(3)))
            for k in range(3):
                num[k] += s * p[k]
            den += s * s
        c0 = [min(255.0, max(0.0, v / den)) for v in num]
    q0 = dxt1_tools._to565((int(round(c0[0])), int(round(c0[1])), int(round(c0[2]))))
    if q0 == 0:
        return struct.pack('<HHI', 0, 0, 0)
    e0 = dxt1_tools._565(q0)
    pal = [e0, (0, 0, 0), tuple(2 * a // 3 for a in e0), tuple(a // 3 for a in e0)]
    bits = 0
    for i, p in enumerate(px):
        j = 1 if p == (0, 0, 0) else min(range(4), key=lambda q: sum((pal[q][k] - p[k]) ** 2 for k in range(3)))
        bits |= j << (2 * i)
    return struct.pack('<HHI', q0, 0, bits)


def encode_atlas(img):
    """img: {cell: rows (112 x 254 RGB)} -> DXT1 bytes for 2048 x 2048 (cells without a picture stay black)."""
    black = struct.pack('<HHI', 0, 0, 0)
    out = bytearray()
    for by in range(0, ATLAS, 4):
        for bx in range(0, ATLAS, 4):
            c = (by // CH) * COLS + bx // CW
            rows = img.get(c) if by // CH < ROWS else None
            x0, y0 = cell_xy(c)
            if rows is None or bx - x0 >= PW:
                out += black
                continue
            px = [rows[by - y0 + j][min(PW - 1, bx - x0 + i)] for j in range(4) for i in range(4)]
            out += dxt1_block(px)
    return bytes(out)


def stock_pictures(alr):
    """mission_map_stNN sprites of VsMode0400 (texture 0, 2048 x 512 DXT1) -> {stNN: rows 112 x 254}."""
    R = at.parse(alr)
    e = at.tex_entries(R)[0]
    off, packed = struct.unpack_from('<I', e, 4)[0], struct.unpack_from('<I', e, 12)[0]
    w, h = 1 << ((packed >> 20) & 15), 1 << ((packed >> 24) & 15)
    assert (w, h) == (2048, 512)
    pix = sr.decode_tex(bytes(R['resource']), off, w, h, 0x0C)
    g = bytes(R['groups'][R['slot'][1]])
    out = {}
    ntex = struct.unpack_from('<I', g, 8)[0]
    for i in range(struct.unpack_from('<I', g, 12)[0]):
        o = 0x20 + 0x34 * ntex + 0x3c * i
        t = struct.unpack_from('<I', g, o)[0]
        name = g[o + 4:o + 0x24].split(b'\0')[0].decode()
        if t == 0 and name.startswith('mission_map_st'):
            u0, v0 = struct.unpack_from('<2f', g, o + 0x24)
            x, y = int(w * u0), int(h * v0)
            out[name[12:16]] = [[pix[(y + r) * w + x + c][:3] for c in range(PW)] for r in range(PH)]
    return out


def chunk10(g, ntex0, nspr0, tex_name, sprites):
    """The image group with one more texture (ours, DXT1 2048 x 2048) and the extra sprites on it."""
    cid, span, ntex, nspr = struct.unpack_from('<IIII', g, 0)
    assert cid == 0x10 and (ntex, nspr) == (ntex0, nspr0) and g[span:] == struct.pack('<II', 0, 8), (cid, ntex, nspr)
    names = g[0x20:0x20 + 0x20 * ntex]
    descs = g[0x20 + 0x20 * ntex:0x20 + 0x34 * ntex]
    recs = g[0x20 + 0x34 * ntex:span]
    assert len(recs) == 0x3c * nspr
    name = bytearray(names[-0x20:])                       # the last entry's 16-byte tail kept
    name[0:0x10] = tex_name.encode().ljust(0x10, b'\0')
    desc = bytearray(descs[0:0x14])                       # texture 0 (DXT1 in both files), sized and counted for ours
    assert struct.unpack_from('<I', desc, 0xc)[0] == 4, 'texture 0 is not the DXT1 kind'
    struct.pack_into('<hhI', desc, 0, SMALL_W, SMALL_H, SMALL_W * SMALL_H // 2 + 128)
    struct.pack_into('<III', desc, 8, len(sprites), 4, 0)
    new = b''
    for k, (nm, c) in enumerate(sprites):               # sprite k = row k of the small texture
        x, y = 0, k * CH
        new += struct.pack('<I', ntex) + nm.encode().ljust(0x20, b'\0') + struct.pack(
            '<4f2i', (x + 0.25) / SMALL_W, (y + 0.25) / SMALL_H, (x + PW + 0.25) / SMALL_W, (y + PH + 0.25) / SMALL_H, PW, PH)
    body = bytes(g[0x10:0x20]) + bytes(names) + bytes(name) + bytes(descs) + bytes(desc) + bytes(recs) + new
    hdr = struct.pack('<IIII', 0x10, 0x10 + len(body), ntex + 1, nspr + len(sprites))
    return bytearray(hdr + body + struct.pack('<II', 0, 8))


def small_texture(dxt, sprites):
    """The in-file texture: row k (112 px) = the atlas cell of sprite k; DXT1 256 x 1024."""
    out = bytearray(SMALL_W * SMALL_H // 2)
    for k, (nm, c) in enumerate(sprites):
        x0, y0 = cell_xy(c)
        for br in range(CH // 4):
            src = ((y0 // 4 + br) * (ATLAS // 4) + x0 // 4) * 8
            dst = ((k * CH) // 4 + br) * (SMALL_W // 4) * 8
            out[dst:dst + SMALL_W // 4 * 8] = dxt[src:src + SMALL_W // 4 * 8]
    return bytes(out)


def build_image(alr, spec, dxt, sprites):
    """alr + the small texture (the sprites' cells) + the sprites -> new alr bytes (parsed back and checked)."""
    dxt = small_texture(dxt, sprites)
    R = at.parse(alr)
    R['resource_dirty'] = True
    pos = at.align(len(R['resource']), 0x100)
    R['resource'] += b'\0' * (pos - len(R['resource'])) + dxt
    R['resource'] += b'\0' * (at.align(len(R['resource']), 0x80) - len(R['resource']))
    te = at.tex_entries(R)
    assert len(te) == spec['ntex']
    te.append(bytearray(pc.tex_entry(spec['entry'], pos, SMALL_W, SMALL_H, 1, 0x0C)))
    at.set_tex_entries(R, te)
    R['groups'][R['slot'][1]] = chunk10(R['groups'][R['slot'][1]], spec['ntex'], spec['nspr'], spec['tex_name'], sprites)
    new_alr = at.build(R)
    C = at.parse(new_alr)
    g = bytes(C['groups'][C['slot'][1]])
    ntex, nspr = struct.unpack_from('<II', g, 8)
    per = [struct.unpack_from('<I', g, 0x20 + 0x20 * ntex + 0x14 * t + 8)[0] for t in range(ntex)]
    owners = [struct.unpack_from('<I', g, 0x20 + 0x34 * ntex + 0x3c * i)[0] for i in range(nspr)]
    assert (ntex, nspr) == (spec['ntex'] + 1, spec['nspr'] + len(sprites)) and nspr <= 128, (ntex, nspr)
    assert per == [owners.count(t) for t in range(ntex)], (per, owners)
    for i, (nm, c) in enumerate(sprites):                 # each new record: our texture, the cell's rect, 254 x 112
        o = 0x20 + 0x34 * ntex + 0x3c * (spec['nspr'] + i)
        assert owners[spec['nspr'] + i] == spec['ntex'] and g[o + 4:o + 0x24].split(b'\0')[0].decode() == nm
        uv = struct.unpack_from('<4f', g, o + 0x24)
        x, y = int(SMALL_W * uv[0]), int(SMALL_H * uv[1])
        assert (x, y) == (0, i * CH) and (int(SMALL_W * uv[2]) - x, int(SMALL_H * uv[3]) - y) == (PW, PH), (nm, x, y)
    e = at.tex_entries(C)[spec['ntex']]
    assert bytes(C['resource'][struct.unpack_from('<I', e, 4)[0]:][:len(dxt)]) == dxt
    assert len(new_alr) - len(alr) < 0x30000, 'the file grew by more than the small texture'
    # the stock part of the resource and every old texture entry unchanged
    O = at.parse(alr)
    assert bytes(C['resource'][:len(O['resource'])]) == bytes(O['resource'])
    assert [bytes(x) for x in at.tex_entries(C)[:spec['ntex']]] == [bytes(x) for x in at.tex_entries(O)]
    return new_alr


def patch_table(ssb, stock_table, base):
    """The script's picture table (found once, stock or already patched) -> Map Slot rows at base + offset."""
    want = list(stock_table)
    for row, off in SLOT_ROWS.items():
        want[row] = base + off
    b = bytearray(ssb)
    for cand in (stock_table, want):
        pat = struct.pack('<30i', *cand)
        p = b.find(pat)
        if p >= 0:
            assert b.find(pat, p + 1) < 0, 'picture table found twice'
            b[p:p + 120] = struct.pack('<30i', *want)
            return bytes(b), p
    raise SystemExit('picture table not found')


def main(clean_vsmode, inst_mnet, inst_vsmenu, inst_livestagesel, mx_list, pdir, out):
    vs_alr = open(clean_vsmode, 'rb').read()
    mn_alr = open(inst_mnet, 'rb').read()
    assert sha8(vs_alr) == VSMODE_SHA, 'not the stock VsMode.alr'
    assert sha8(mn_alr) == MNET_SHA, 'not the installed MNet.alr (56eb6efc)'
    rows = [l.split('\t') for l in open(mx_list, encoding='utf-8').read().splitlines() if l.strip()]
    stock = stock_pictures(vs_alr)
    cells, img, src = {}, {}, {}
    order = [r[0] for r in rows] + [s for _, s in SLOT_STAGES] + ['st10']
    for c, key in enumerate(order):
        if key in STOCK:
            pic, src[key] = stock[STOCK[key]], 'stock mission_map_' + STOCK[key]
        else:
            for sub in ('official', 'renders'):
                p = os.path.join(pdir, sub, key + '.png')
                if os.path.exists(p):
                    pic, src[key] = [r[:PW] for r in dxt1_tools.read_png(p)[:PH]], sub
                    break
            else:
                raise SystemExit('no picture for ' + key)
        assert len(pic) == PH and all(len(r) == PW for r in pic), key
        img[c] = scanlines(pic)
        cells[key] = c
    assert len(order) <= COLS * ROWS
    # atlas preview (to look at) and the texture
    prev = [[(0, 0, 0)] * ATLAS for _ in range(ROWS * CH)]
    for c, r in img.items():
        x0, y0 = cell_xy(c)
        for j in range(PH):
            prev[y0 + j][x0:x0 + PW] = r[j]
    os.makedirs(out, exist_ok=True)
    dxt1_tools.write_png(os.path.join(out, 'atlas_preview.png'), prev)
    dxt = encode_atlas(img)
    # the encoded atlas keeps every scan line black and the picture rows close to the source
    dec = sr.decode_tex(dxt, 0, ATLAS, ATLAS, 0x0C)
    worst, err_sum, err_n = 0, 0, 0
    for c, r in img.items():
        x0, y0 = cell_xy(c)
        for j in range(PH):
            line = [dec[(y0 + j) * ATLAS + x0 + i][:3] for i in range(PW)]
            if j % 3 == LINE_PHASE:
                assert all(p == (0, 0, 0) for p in line), ('scan line not black', c, j)
            else:
                worst = max(worst, max(abs(a[k] - b[k]) for a, b in zip(line, r[j]) for k in range(3)))
                err_sum += sum(abs(a[k] - b[k]) for a, b in zip(line, r[j]) for k in range(3)); err_n += 3 * PW
    sprites = [('cwo_mapslot7', cells['st10'])] + [(nm, cells[s]) for nm, s in SLOT_STAGES]
    vs_alr_new = build_image(vs_alr, IMAGES['VsMode'], dxt, sprites)
    mn_new = build_image(mn_alr, IMAGES['MNet'], dxt, sprites)
    open(os.path.join(out, 'VsMode.alr'), 'wb').write(vs_alr_new)
    open(os.path.join(out, 'MNet.alr'), 'wb').write(mn_new)
    # scripts: vsmenu (stock table or the 5.40 one) -> 63-70; livestagesel -> 84-91
    vs_ssb = open(inst_vsmenu, 'rb').read()
    vs_new, vs_at = patch_table(vs_ssb, VS_TABLE, IMAGES['VsMode']['base'])
    assert len(vs_new) == len(vs_ssb) and sum(a != b for a, b in zip(vs_new, vs_ssb)) <= 4 * len(SLOT_ROWS)
    open(os.path.join(out, 'vsmenu_en.ssb'), 'wb').write(vs_new)
    open(os.path.join(out, 'mx_portraits.dxt'), 'wb').write(dxt)
    live_ssb = open(inst_livestagesel, 'rb').read()
    live_new, live_at = patch_table(live_ssb, LIVE_TABLE, IMAGES['MNet']['base'])
    assert len(live_new) == len(live_ssb) and sum(a != b for a, b in zip(live_new, live_ssb)) <= 4 * len(SLOT_ROWS)
    open(os.path.join(out, 'livestagesel_en.ssb'), 'wb').write(live_new)
    with open(os.path.join(out, 'mx_portraits.txt'), 'w', newline='\n') as f:
        f.write('#atlas\t%d\t%d\t%d\t%d\t%d\n' % (ATLAS, ATLAS, CW, CH, COLS))
        for nm in ('VsMode', 'MNet'):
            s = IMAGES[nm]
            h0, h1 = ssb_names.encode(s['entry'])
            f.write('#image\t%d\t%d\t%d\t%d\t%d\t%u\t%u\t%d\n' % (s['slot'], s['ntex'] + 1, s['nspr'] + len(sprites), s['ntex'],
                                                               s['base'], h0, h1, 0))
        f.write('#sprite\t%d\n#default\t%d\n' % (IMAGES['VsMode']['base'], cells['st10']))
        for r in rows:
            f.write('%s\t%d\n' % (r[0], cells[r[0]]))
    man = {'VsMode.alr': {'old': sha8(vs_alr), 'new': sha8(vs_alr_new), 'bytes': len(vs_alr_new)},
           'MNet.alr': {'old': sha8(mn_alr), 'new': sha8(mn_new), 'bytes': len(mn_new)},
           'vsmenu_en.ssb': {'old': sha8(vs_ssb), 'new': sha8(vs_new), 'table_at': hex(vs_at)},
           'mx_portraits.dxt': sha8(dxt),
           'livestagesel_en.ssb': {'old': sha8(live_ssb), 'new': sha8(live_new), 'table_at': hex(live_at)},
           'mx_portraits.txt': sha8(open(os.path.join(out, 'mx_portraits.txt'), 'rb').read()),
           'mx_list': sha8(open(mx_list, 'rb').read()), 'scan_line_phase': LINE_PHASE, 'worst_channel_error': worst, 'mean_channel_error': round(err_sum / max(1, err_n), 2),
           'cells': cells, 'sources': src, 'sprites': {nm: c for nm, c in sprites},
           'images': {nm: dict(IMAGES[nm], sha=None) for nm in IMAGES}}
    json.dump(man, open(os.path.join(out, 'manifest.json'), 'w'), indent=1)
    print(json.dumps({k: man[k] for k in ('VsMode.alr', 'MNet.alr', 'vsmenu_en.ssb', 'livestagesel_en.ssb',
                                          'mx_portraits.txt', 'worst_channel_error', 'mean_channel_error')}))


if __name__ == '__main__':
    main(*sys.argv[1:8])
