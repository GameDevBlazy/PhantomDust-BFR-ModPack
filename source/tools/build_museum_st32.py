"""Object museum on st32: every placeable object from st01-st08 on one flat floor (2026-10-20).

Uses only proven tools: alr_transplant.transplant (model groups + textures + vertex buffers; IGT-021
PASS) and build_st32_objects.build (names, collision, emitters, animations, placements; IGT-020/021
PASS). Objects keep their stock placement class and parameters (row +4..+7) from their first stock
placement row; rotation is set to 0 so every object stands upright for viewing.

Excluded: objects with no stock placement row, and classes 3/x and 4/x other than 4/16 (campaign hub
doors, triggers and limits). Break-effect rows (+0xF8) are not copied (their values are stage-specific).

Textures: the top `--drop-mips` mip levels are removed so the whole set fits the 12 MB stage-ALR
budget of every load mode (ST00_SHARED_EFFECTS.md).

Layout: stock st32 starts stay where they are (x = -72, z = 20-80). Objects are packed in rows on both
sides away from the start strip (|z| >= 300), inside the visible grid (+-1000) first, then out to the
collision edge (+-1900). Each object rests on the floor (y = 30 - its lowest model/collision point).

Usage: python -B -I build_museum_st32.py OUT_DIR [--drop-mips N] [--stages st01,st02,...]
"""
import argparse
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import alr_transplant as at  # noqa: E402
import build_st32_objects as bo  # noqa: E402
import pd_stage_dat as sd  # noqa: E402
import stage_sections as ss  # noqa: E402

import pd_paths  # noqa: E402
MAP = pd_paths.MAP
BUDGET = 0x1000000 - 0x400000          # stage .alr slot in the tight load mode (12 MB)
FLOOR_Y = 30.0
GAP = 40.0


def first_rows(dat):
    rows = {}
    for po, pc in ((0x04, 0x30), (0x0C, 0x38), (0x10, 0x3C)):
        o, c = struct.unpack_from('<i', dat, po)[0], struct.unpack_from('<i', dat, pc)[0]
        if o < 0 or c <= 0:
            continue
        for i in range(c):
            rows.setdefault(struct.unpack_from('<i', dat, o + 0x24 * i)[0], dat[o + 0x24 * i:o + 0x24 * i + 0x24])
    return rows


def model_bounds(alr_model, group_index):
    s = alr_model['slot'][group_index] if 0 <= group_index < len(alr_model['slot']) else None
    if s is None:
        return None
    g = alr_model['groups'][s]
    mn, mx = [1e9] * 3, [-1e9] * 3
    for cid, co, sp in at.walk(g):
        if cid == 0x02:
            b = struct.unpack_from('<10f', g, co + 8)
            for k in range(3):
                mn[k] = min(mn[k], b[4 + k])
                mx[k] = max(mx[k], b[7 + k])
    return (mn, mx) if mn[0] < 1e9 else None


def collision_bounds(blob):
    obj = sd.parse_collision(blob, 0)
    P = obj['pool']
    if not P:
        return None
    return [min(p[k] for p in P) for k in range(3)], [max(p[k] for p in P) for k in range(3)]


def pack(items):
    """items: [(key, w, d)] -> {key: (x, z)} centre positions, rows on +z then -z, then outward."""
    pos, regions = {}, [(300.0, 950.0, 1), (300.0, 950.0, -1), (950.0, 1900.0, 1), (950.0, 1900.0, -1)]
    queue = sorted(items, key=lambda t: -t[2])
    for z0, z1, side in regions:
        z = z0
        while queue and z < z1:
            x, row_d, placed = -940.0 if z1 <= 950 else -1880.0, 0.0, []
            xlim = 940.0 if z1 <= 950 else 1880.0
            for it in list(queue):
                key, w, d = it
                if z + d > z1:
                    continue
                if x + w > xlim:
                    continue
                pos[key] = (x + w / 2, side * (z + d / 2))
                x += w + GAP
                row_d = max(row_d, d)
                queue.remove(it)
                placed.append(key)
            if not placed:
                break
            z += row_d + GAP
    if queue:
        raise ValueError('%d objects did not fit the floor' % len(queue))
    return pos


def build(out_dir, drop_mips=2, stages=None, exclude_classes=()):
    """exclude_classes: placement classes to leave out, e.g. {(2, 58)} = pdStageObjFallCrm (Sein's timed
    falling ruins; museum IGT-026/026b crash suspect, Batch 042)."""
    os.makedirs(out_dir, exist_ok=True)
    stages = stages or ['st0%d' % i for i in range(1, 9)]
    alr = open(os.path.join(MAP, 'st32.alr'), 'rb').read()
    base_dat = open(os.path.join(MAP, 'st32.dat'), 'rb').read()
    objects, info = [], []
    for st in stages:
        dat = open(os.path.join(MAP, st + '.dat'), 'rb').read()
        donor = open(os.path.join(MAP, st + '.alr'), 'rb').read()
        D = at.parse(donor)
        r = ss.decode_stage(dat)
        rows = first_rows(dat)
        chosen = []
        for k, name in enumerate(r['names']):
            if k not in rows:
                continue
            cls = struct.unpack_from('<BB', rows[k], 4)
            if cls[0] == 3 or (cls[0] == 4 and cls != (4, 16)) or tuple(cls) in exclude_classes:
                continue
            chosen.append(k)
        groups = sorted({g for k in chosen for g in r['ob00'][k][:3] if g is not None and g >= 0
                         and g < len(D['slot']) and D['slot'][g] is not None})
        if groups:
            alr, rep = at.transplant(alr, donor, groups, tex_drop_mips=drop_mips)
            gmap = dict(zip(groups, rep['new_group_indices']))
        else:
            gmap = {}
        for k in chosen:
            parts = bo.object_parts(dat, k)
            parts['row'] = [g if (w > 2 or g < 0 or g in gmap) else -1 for w, g in enumerate(parts['row'])]
            b = None
            for w in (0, 1, 2):
                g = r['ob00'][k][w]
                if g is not None and g >= 0 and g in gmap:
                    b = model_bounds(D, g)
                    if b:
                        break
            if b is None and 6 in parts['oc']:
                b = collision_bounds(parts['oc'][6])
            if b is None:
                b = ([-20.0, 0.0, -20.0], [20.0, 20.0, 20.0])
            objects.append((parts, gmap))
            info.append({'stage': st, 'object': k, 'name': parts['name'], 'class': list(struct.unpack_from('<BB', rows[k], 4)),
                         'params': list(struct.unpack_from('<BB', rows[k], 6)), 'bounds': b, 'template': rows[k],
                         'models': [g for g in r['ob00'][k][:3] if g is not None and g >= 0]})
    if len(alr) > BUDGET + 0x400000 - 0x400000 and len(alr) > BUDGET:
        raise ValueError('museum ALR is %d bytes, over the %d-byte stage budget' % (len(alr), BUDGET))
    items = []
    for i, it in enumerate(info):
        (mn, mx) = it['bounds']
        items.append((i, max(mx[0] - mn[0], 10.0), max(mx[2] - mn[2], 10.0)))
    pos = pack(items)
    placements = []
    for i, it in enumerate(info):
        (mn, mx) = it['bounds']
        cx, cz = pos[i]
        x = cx - (mn[0] + mx[0]) / 2
        z = cz - (mn[2] + mx[2]) / 2
        y = FLOOR_Y - mn[1]
        placements.append((0, bo.placement_row(it['template'], i, (x, y, z), (0.0, 0.0, 0.0))))
        it['pos'] = [round(x, 1), round(y, 1), round(z, 1)]
    dat_out = bo.build(base_dat, objects, placements, with_f8=False)
    open(os.path.join(out_dir, 'st32.alr'), 'wb').write(alr)
    open(os.path.join(out_dir, 'st32.dat'), 'wb').write(dat_out)
    for it in info:
        del it['template']
    rep = {'objects': len(info), 'alr_size': len(alr), 'alr_budget': BUDGET, 'dat_size': len(dat_out),
           'drop_mips': drop_mips, 'stages': stages, 'catalogue': info}
    json.dump(rep, open(os.path.join(out_dir, 'museum_catalogue.json'), 'w'), indent=1)
    return rep


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--drop-mips', type=int, default=2)
    ap.add_argument('--stages', default='')
    ap.add_argument('--exclude-class', action='append', default=[], help='e.g. 2/58')
    a = ap.parse_args()
    excl = {tuple(int(x) for x in c.split('/')) for c in a.exclude_class}
    rep = build(a.out, a.drop_mips, [s for s in a.stages.split(',') if s] or None, excl)
    rep['excluded_classes'] = sorted(excl)
    print(json.dumps({k: v for k, v in rep.items() if k != 'catalogue'}, indent=1))
