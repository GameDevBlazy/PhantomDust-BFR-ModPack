"""Edgar's Dream Workshop: the stage creator's build floor (owner 2026-10-23: "When the stage loads, it will load a
gridded texture on the ground, and it will have the coordinates of everywhere that you walk"). Independent research
tool; read-only on all inputs; writes a fixture for Map Slot 8 (stage 20, family 0, tested in game as MAP-SLOTS-03).

What it builds (everything with the maze builder's in-game-tested functions, build_maze_st32.py / IGT-070):
- a flat 60 m x 60 m floor (10 x 10 tiles of 6 m) at y 200 with a low 3 m perimeter wall, Dawn City's sky dome
  (st05 group 2, byte for byte), 4 starts (one per quarter, facing the centre) with 3 capsules each;
- the floor texture is a generated grid in exactly the format and size of the stock texture it replaces (st05 'romen',
  DXT1 512 x 512, 10 mip levels, size rule alr_transplant.texture_bytes): one tile = 6 m, a fine line every 0.75 m,
  a bright border line on every 6 m line, and a gold axis line through the centre tiles (x = 0 / z = 0);
- the coordinates are shown live by the loader's on-screen panel (CwoLoader v0.3).
The catalog row of Map Slot 8 (row 29) is renamed "Edgar's Dream Workshop" (stage number, family and flags kept).
Usage: python -B -I build_workshop.py OUT_DIR [--showroom]  (--showroom: Highway objects)
"""
import hashlib
import json
import math
import re
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import alr_transplant as at  # noqa: E402
import build_maze_st32 as mz  # noqa: E402
import pd_common as pc  # noqa: E402
import pd_paths  # noqa: E402
import pd_stage_dat as sd  # noqa: E402
import ssb_names  # noqa: E402

LIVE = pd_paths.DATA
STAGE = 20
CAT_ROW = 29
NAME = "Edgar's Dream Workshop"
TILES = 10
TILE = 60.0
FLOOR_Y = 200.0
WALL_H = 30.0
WALL_T = 4.0
STAND = 10.0
CAPSULE_STEP = (40.0, 80.0, 120.0)
BG, MINOR, MAJOR, AXIS = (38, 42, 52), (70, 80, 96), (90, 200, 230), (240, 200, 60)


def sha(b):
    return hashlib.sha256(b).hexdigest()


def grid_pixels(n, axis):
    """n x n RGB tile: border line (shared with the neighbour tile, half on each side), 7 inner fine lines;
    axis tiles get a gold centre line along their x = 0 / z = 0 edge."""
    px = [[BG] * n for _ in range(n)]
    minor_w, major_w = max(1, n // 256), max(1, n // 128)
    for k in range(1, 8):
        c = k * n // 8
        for i in range(n):
            for d in range(minor_w):
                px[c + d - minor_w // 2][i] = MINOR
                px[i][c + d - minor_w // 2] = MINOR
    for i in range(n):
        for d in range(major_w):
            for c in (d, n - 1 - d):
                px[c][i] = MAJOR
                px[i][c] = MAJOR
    if axis:
        for i in range(n):
            for d in range(major_w * 2):
                px[i][d] = AXIS            # u = 0 edge (the tile's x = 0 line)
    return px


def dxt1(px):
    """DXT1 encode (4-colour mode): per 4x4 block, end points = darkest and brightest pixel."""
    n = len(px)
    out = bytearray()
    for by in range(0, n, 4):
        for bx in range(0, n, 4):
            blk = [px[by + y][bx + x] for y in range(4) for x in range(4)]
            lum = lambda c: c[0] * 3 + c[1] * 6 + c[2]
            hi, lo = max(blk, key=lum), min(blk, key=lum)
            c0 = ((hi[0] >> 3) << 11) | ((hi[1] >> 2) << 5) | (hi[2] >> 3)
            c1 = ((lo[0] >> 3) << 11) | ((lo[1] >> 2) << 5) | (lo[2] >> 3)
            if c0 == c1:
                out += struct.pack('<HHI', c0, c1, 0)
                continue
            if c0 < c1:
                c0, c1, hi, lo = c1, c0, lo, hi
            pal = [hi, lo, tuple((2 * a + b) // 3 for a, b in zip(hi, lo)), tuple((a + 2 * b) // 3 for a, b in zip(hi, lo))]
            bits = 0
            for i, c in enumerate(blk):
                k = min(range(4), key=lambda j: sum((c[t] - pal[j][t]) ** 2 for t in range(3)))
                bits |= k << (2 * i)
            out += struct.pack('<HHI', c0, c1, bits)
    return bytes(out)


def grid_texture(packed, axis):
    w = 1 << ((packed >> 24) & 15)
    mips, fmt = (packed >> 16) & 15, (packed >> 8) & 0xFF
    assert fmt == 0x0C and w == 1 << ((packed >> 20) & 15)
    data = bytearray()
    for k in range(mips):
        n = max(w >> k, 4)
        data += dxt1(grid_pixels(n, axis)) if (w >> k) >= 4 else dxt1(grid_pixels(4, axis))[:8]
    size = at.texture_bytes(packed)
    assert len(data) <= size
    return bytes(data) + bytes(size - len(data))


def replace_texture_data(R, idx, data):
    ent = at.tex_entries(R)[idx]
    off = struct.unpack_from('<I', ent, 4)[0]
    assert len(data) == at.texture_bytes(struct.unpack_from('<I', ent, 12)[0])
    R['resource'][off:off + len(data)] = data


def add_showroom(alr, dat, stage='st02', limit=8):
    """Variant 01b: a row of stock stage objects along the north side (z = 240), copied with the museum builder's
    in-game-tested steps (build_museum_st32: alr_transplant models + textures, build_st32_objects rows; IGT-021 car
    and lift PASS). They are the first test pieces for the live editor (BACK+START pulls the nearest one)."""
    import build_museum_st32 as mu
    import build_st32_objects as bo
    import stage_sections as ss
    sdat = open(os.path.join(mz.MAP, stage + '.dat'), 'rb').read()
    donor = open(os.path.join(mz.MAP, stage + '.alr'), 'rb').read()
    D = at.parse(donor)
    r = ss.decode_stage(sdat)
    rows = mu.first_rows(sdat)
    chosen, classes = [], set()
    for k, name in enumerate(r['names']):                  # one object per class: a varied row (car, cones, rails ...)
        if k in rows:
            cls = struct.unpack_from('<BB', rows[k], 4)
            if cls[0] != 3 and (cls[0] != 4 or cls == (4, 16)) and cls != (2, 58) and cls not in classes:
                classes.add(cls)
                chosen.append(k)
    chosen = chosen[:limit]
    groups = sorted({g for k in chosen for g in r['ob00'][k][:3] if g is not None and 0 <= g < len(D['slot'])
                     and D['slot'][g] is not None})
    alr, rep = at.transplant(alr, donor, groups, tex_drop_mips=2)
    gmap = dict(zip(groups, rep['new_group_indices']))
    objects, placements, info = [], [], []
    x = -260.0
    for i, k in enumerate(chosen):
        parts = bo.object_parts(sdat, k)
        parts['row'] = [g if (w > 2 or g < 0 or g in gmap) else -1 for w, g in enumerate(parts['row'])]
        b = None
        for w in (0, 1, 2):
            g = r['ob00'][k][w]
            if g is not None and g >= 0 and g in gmap:
                b = mu.model_bounds(D, g)
                if b:
                    break
        if b is None and 6 in parts['oc']:
            b = mu.collision_bounds(parts['oc'][6])
        if b is None:
            b = ([-20.0, 0.0, -20.0], [20.0, 20.0, 20.0])
        mn, mx = b
        w = max(mx[0] - mn[0], 10.0)
        px, pz, py = x - mn[0], 240.0 - (mn[2] + mx[2]) / 2, FLOOR_Y - mn[1]
        objects.append((parts, gmap))
        placements.append((0, bo.placement_row(rows[k], i, (px, py, pz), (0.0, 0.0, 0.0))))
        info.append({'name': parts['name'], 'class': list(struct.unpack_from('<BB', rows[k], 4)),
                     'pos': [round(px, 1), round(py, 1), round(pz, 1)]})
        x += w + 20.0
        if x > 260.0:
            break
    return alr, objects[:len(placements)], placements, info


def build(out_dir, showroom=False, walls=True, sky='st05'):
    """walls=False: one open floor (owner 2026-10-24: "the template stage just has to be empty ... one floor");
    sky: the stage whose sky dome (ALR group 2) is used."""
    st32d = open(os.path.join(mz.MAP, 'st32.dat'), 'rb').read()
    st32a = open(os.path.join(mz.MAP, 'st32.alr'), 'rb').read()
    st05a = open(os.path.join(mz.MAP, 'st05.alr'), 'rb').read()
    sky_a = open(os.path.join(mz.MAP, (sky or 'st05') + '.alr'), 'rb').read()
    assert sha(st32d).startswith(mz.CLEAN['st32.dat']) and sha(st32a).startswith(mz.CLEAN['st32.alr'])
    h = TILES * TILE / 2
    # ---------------- ALR: sky + grid floor + walls
    out1, trep = at.transplant(st32a, sky_a, [2])
    R = at.parse(out1)
    D = at.parse(st05a)
    R['resource'] = bytearray(R['resource'])
    R['resource_dirty'] = True
    R['groups'][R['slot'][2]] = bytearray(R['groups'][R['slot'][trep['new_group_indices'][0]]])
    if sky is None:                                         # no built-in sky: st32's own empty group (group 3) instead
        R['groups'][R['slot'][2]] = bytearray(R['groups'][R['slot'][3]])
    t_grid = mz.copy_texture(R, D, 'romen')
    t_axis = mz.copy_texture(R, D, 'romen')
    t_wall = mz.copy_texture(R, D, 'metal')
    R['resource'] = bytearray(R['resource'])
    packed = struct.unpack_from('<I', at.tex_entries(R)[t_grid], 12)[0]
    replace_texture_data(R, t_grid, grid_texture(packed, False))
    replace_texture_data(R, t_axis, grid_texture(packed, True))
    quads = []
    for i in range(TILES):
        for j in range(TILES):
            x0, z0 = -h + TILE * i, -h + TILE * j
            b = mz.box(x0, x0 + TILE, FLOOR_Y - 10, FLOOR_Y, z0, z0 + TILE)
            on_axis = abs(x0) < 1e-6 or abs(z0) < 1e-6
            quads.append((1 if on_axis else 0, b['top'], (1, 1)))
    walls = [(-h - WALL_T, h + WALL_T, -h - WALL_T, -h), (-h - WALL_T, h + WALL_T, h, h + WALL_T),
             (-h - WALL_T, -h, -h, h), (h, h + WALL_T, -h, h)] if walls else []
    for x0, x1, z0, z1 in walls:
        b = mz.box(x0, x1, FLOOR_Y - 10, FLOOR_Y + WALL_H, z0, z1)
        for side in ('x-', 'x+', 'z-', 'z+', 'top'):
            # texture coordinates are int16 * 4096 (at most ~8 repeats): one wall-texture repeat per 12 m
            quads.append((2, b[side], (max(x1 - x0, z1 - z0) / (2 * TILE), WALL_H / (2 * TILE))))
    g0, nverts = mz.world_group(R, quads, [t_grid, t_axis, t_wall] if walls else [t_grid, t_axis])
    R['groups'][R['slot'][0]] = g0
    alr = at.build(R, keep_resource_offset=False)
    C = at.parse(alr)
    # ---------------- DAT: collision + spawns
    stock = sd.parse_collision(st32d, struct.unpack_from('<i', st32d, 0x94)[0])
    pc.KIND, pc.FLAGS, pc.POOL_KIND = stock['groups'][0]['kind'], stock['groups'][0]['flags'], stock['pool_hdr']['kind']
    pc.CELL = 80.0
    tris = []
    for i in range(TILES):
        for j in range(TILES):
            x0, z0 = -h + TILE * i, -h + TILE * j
            tris += mz.tris_of([(x0, FLOOR_Y, z0), (x0, FLOOR_Y, z0 + TILE), (x0 + TILE, FLOOR_Y, z0 + TILE), (x0 + TILE, FLOOR_Y, z0)])
    for x0, x1, z0, z1 in walls:
        b = mz.box(x0, x1, FLOOR_Y, FLOOR_Y + WALL_H, z0, z1)
        for side in ('top', 'x-', 'x+', 'z-', 'z+'):
            tris += mz.tris_of(b[side], both=True)
    obj, dropped = pc.build_object(tris)
    coll = sd.encode_collision(obj, keep_stored=False)
    back = sd.parse_collision(coll, 0)
    cp_t = sd.parse_cp00(st32d, struct.unpack_from('<i', st32d, 0xA4)[0])[0]
    entries, pts = [], []
    for x, z in ((-150.0, -150.0), (150.0, 150.0), (-150.0, 150.0), (150.0, -150.0)):
        hd = math.atan2(-x, -z)
        fy = pc.floor_height_below(back, x, z, FLOOR_Y + 5)
        assert fy is not None and abs(fy - FLOOR_Y) < 0.01
        e = bytearray(sd.make_cp00_entry(cp_t, (x, fy + STAND, z), hd))
        caps = []
        for k, d in enumerate(CAPSULE_STEP):
            px, pz = x + math.sin(hd) * d, z + math.cos(hd) * d
            cy = pc.floor_height_below(back, px, pz, FLOOR_Y + 5)
            assert cy is not None and abs(cy - FLOOR_Y) < 0.01
            struct.pack_into('<3f', e, 20 + 12 * k, px, cy + STAND, pz)
            caps.append([round(px, 1), round(cy + STAND, 1), round(pz, 1)])
        entries.append(bytes(e))
        pts.append({'start': [x, fy + STAND, z], 'heading': round(hd, 3), 'capsules': caps})
    blocks = {w: sd.encode_cp00([entries[(i + k) % 4] for i in range(4)]) for k, w in enumerate((0xA4, 0xA8, 0xAC, 0xB8, 0xBC))}
    sections = dict(blocks)
    sections[0x94] = coll
    out = bytearray(sd.append_and_repoint(st32d, sections))
    struct.pack_into('<I', out, 0x138, len(out))
    dat = bytes(out)
    for w in blocks:
        assert len(sd.parse_cp00(dat, struct.unpack_from('<i', dat, w)[0])) == 4
    showroom_info, objects, placements = [], [], []
    if showroom:
        alr, objects, placements, showroom_info = add_showroom(alr, dat)
    pack_skipped = []
    if objects:
        import build_museum_st32 as mu
        import build_st32_objects as bo
        dat = bo.build(dat, objects, placements, with_f8=False)
        if len(alr) > mu.BUDGET:
            raise ValueError('workshop ALR over budget: %d' % len(alr))
        C = at.parse(alr)
        for w in blocks:
            assert len(sd.parse_cp00(dat, struct.unpack_from('<i', dat, w)[0])) == 4
    # ---------------- catalog: rename Map Slot 8, keep everything else
    cat_path = os.path.join(LIVE, 'setup', 'stageset', 'stageset_en.dat')
    cat = open(cat_path, 'rb').read()
    declared, count, pool, rows = pc.catalog_rows(cat)
    row = rows[CAT_ROW]
    assert row['stage'] == STAGE, row
    new_cat = pc.catalog_set_row(cat, CAT_ROW, NAME, row['family'], row['internal'], row['stage'], row['variants'], row['flag'])
    assert pc.catalog_rows(new_cat)[3][CAT_ROW]['display'] == NAME
    # ---------------- fixture
    os.makedirs(os.path.join(out_dir, 'Map'), exist_ok=True)
    os.makedirs(os.path.join(out_dir, 'setup', 'stageset'), exist_ok=True)
    files = {'Map/st%02d.dat' % STAGE: dat, 'Map/st%02d.alr' % STAGE: alr, 'setup/stageset/stageset_en.dat': new_cat}
    man = {'test': NAME + ': grid build floor for the stage creator (Map Slot 8, stage 20)', 'files': {}}
    for rel, data in files.items():
        open(os.path.join(out_dir, *rel.split('/')), 'wb').write(data)
        live = os.path.join(LIVE, *rel.split('/'))
        man['files'][rel] = {'action': 'replace', 'base_sha256': sha(open(live, 'rb').read()) if os.path.exists(live) else None, 'new_sha256': sha(data)}
    json.dump(man, open(os.path.join(out_dir, 'manifest.json'), 'w'), indent=1)
    rep = {'name': NAME, 'stage': STAGE, 'size_m': TILES * TILE / 10, 'tile_m': TILE / 10, 'render_vertices': nverts,
           'alr_groups': len(C['slot']), 'textures': len(at.tex_entries(C)), 'collision_vertices': len(obj['pool']),
           'spawns': pts, 'showroom': showroom_info, 'alr_bytes': len(alr),
           'files': {k: v['new_sha256'][:8] for k, v in man['files'].items()}}
    json.dump(rep, open(os.path.join(out_dir, 'build_report.json'), 'w'), indent=1)
    return rep


if __name__ == '__main__':
    print(json.dumps(build(sys.argv[1], showroom='--showroom' in sys.argv), indent=1))
