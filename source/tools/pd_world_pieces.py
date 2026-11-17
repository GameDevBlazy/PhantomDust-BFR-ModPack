"""Phantom Dust world mesh -> separate hard pieces (owner 2026-10-24: "We need those unbreakable pieces ... break the
non-breakable pieces down into individual meshes"). Independent research tool; read-only on all inputs.

What is a "hard piece": the parts of a stage that are not stage objects - the world model (stage ALR group 0, the
arena; group 1, the far scenery) and the world collision in the stage .dat (collision section, offset +0x94). The
stage asset list's "hard scenery" rows for groups >= 8 are object models (they are referenced by OB00 rows), not world.

Facts used (all from earlier batches, recorded in MODLOG):
- ALR container / group / chunk layout, mesh chunk 0x02 (+0x40 material, +0x44 descriptor, +0x46 topology 5 list /
  6 strip, +0x50 index count, +0x68 u16 indices), descriptor chunk 0x16 (28 bytes: +0 format, +1 stride, +8 count,
  +0x14 resource offset): ALR_CONTAINER_SPEC_BATCH012 (CONFIRMED corpus-wide), alr_transplant (byte-exact rebuild of
  the corpus); every vertex format starts with a FLOAT3 position (Batch 027, native input-layout builder 0x14001f1e0).
- strips: degenerate triangles skipped, winding flips every other triangle (st32_reference.write_obj; previews
  matched the game).
- world collision groups / pool: pd_stage_dat.parse_collision (in-game tested writers IGT-009..013).

Splitting: render triangles are joined into islands by shared vertex positions; islands that sit inside another
island's box (details on a building) join it unless that box is huge; a piece wider than TILE is cut into TILE x TILE
tiles by triangle centre (the floor of a whole arena becomes floor tiles). Collision triangles go to the piece with
the nearest render vertex; collision far from every render triangle is an invisible barrier and becomes its own
piece (collision only, drawn in the editor by its outline - see the loader).

Library: pieces(stage) -> list of dicts {name, box, centre, tris: [(material key, (v0, v1, v2))], col: [tri]}.
"""
import collections
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import alr_transplant as at  # noqa: E402
import pd_stage_dat as sd  # noqa: E402

import pd_paths  # noqa: E402
MAP = pd_paths.MAP
TILE = 400.0            # 40 m: floor slabs and long roads become pieces of at most this size
HUGE = 900.0            # an island wider than this does not swallow smaller islands inside its box
BARRIER_GAP = 25.0      # collision farther than 2.5 m from any render vertex = invisible barrier
SNAP = 0.05
SMALL = 60.0          # pieces under 6 m join a neighbour they touch
TOUCH = 15.0          # 1.5 m: close enough to count as touching
MAX_PIECES = 120


def world_groups(R):
    """Group 0 (arena) and group 1 (far scenery) when present."""
    return [g for g in (0, 1) if g < len(R['slot']) and R['slot'][g] is not None]


def group_triangles(R, gi):
    """[(descriptor index, material index, (i0, i1, i2))] plus the group's descriptors and vertex positions."""
    g = R['groups'][R['slot'][gi]]
    descs, tris, mats = [], [], None
    for cid, co, sp in at.walk(g):
        if cid == 0x16:
            for k in range(struct.unpack_from('<I', g, co + 8)[0]):
                d = co + 12 + 28 * k
                fmt, stride = g[d], g[d + 1]
                count, off = struct.unpack_from('<I', g, d + 8)[0], struct.unpack_from('<I', g, d + 0x14)[0]
                raw = bytes(R['resource'][off:off + stride * count])
                pos = [struct.unpack_from('<3f', raw, v * stride) for v in range(count)]
                descs.append({'format': fmt, 'stride': stride, 'raw': raw, 'pos': pos, 'entry': bytes(g[d:d + 28])})
        elif cid == 0x01:
            mats = bytes(g[co:co + sp])
        elif cid == 0x02:
            mat, _ref, desc, topo = struct.unpack_from('<4H', g, co + 0x40)
            n = struct.unpack_from('<I', g, co + 0x50)[0]
            idx = struct.unpack_from('<%dH' % n, g, co + 0x68)
            if topo == 5:
                tl = [tuple(idx[j:j + 3]) for j in range(0, n - 2, 3)]
            else:
                tl = []
                for j in range(n - 2):
                    a, b, c = idx[j], idx[j + 1], idx[j + 2]
                    if a == b or b == c or a == c:
                        continue
                    tl.append((a, b, c) if j % 2 == 0 else (b, a, c))
            tris += [(desc, mat, t) for t in tl]
    return descs, tris, mats


def _box(points):
    return ([min(p[k] for p in points) for k in range(3)], [max(p[k] for p in points) for k in range(3)])


def _inside(a, b, slack=2.0):
    return all(a[0][k] >= b[0][k] - slack and a[1][k] <= b[1][k] + slack for k in range(3))


def islands(descs, tris):
    """Union-find over triangles that share a (snapped) vertex position."""
    parent = list(range(len(tris)))

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x
    first = {}
    for t, (d, _m, ids) in enumerate(tris):
        for i in ids:
            p = descs[d]['pos'][i]
            key = (round(p[0] / SNAP), round(p[1] / SNAP), round(p[2] / SNAP))
            o = first.setdefault(key, t)
            if o != t:
                ra, rb = find(o), find(t)
                if ra != rb:
                    parent[ra] = rb
    out = collections.defaultdict(list)
    for t in range(len(tris)):
        out[find(t)].append(t)
    return list(out.values())


def tri_points(descs, tri):
    d, _m, ids = tri
    return [descs[d]['pos'][i] for i in ids]


def split(descs, tris):
    """Islands -> pieces (lists of triangle numbers)."""
    isl = [(lst, _box([p for t in lst for p in tri_points(descs, tris[t])])) for lst in islands(descs, tris)]
    isl.sort(key=lambda x: -max(x[1][1][0] - x[1][0][0], x[1][1][2] - x[1][0][2]))
    pieces = []                                      # [triangles, box]
    for lst, box in isl:
        host = None
        for p in pieces:
            w = max(p[1][1][0] - p[1][0][0], p[1][1][2] - p[1][0][2])
            if w <= HUGE and _inside(box, p[1]):
                host = p
                break
        if host:
            host[0] += lst
            host[1] = ([min(host[1][0][k], box[0][k]) for k in range(3)], [max(host[1][1][k], box[1][k]) for k in range(3)])
        else:
            pieces.append([list(lst), box])
    out = []
    for lst, box in pieces:                          # wide pieces -> tiles
        w = max(box[1][0] - box[0][0], box[1][2] - box[0][2])
        if w <= TILE:
            out.append(lst)
            continue
        cells = collections.defaultdict(list)
        for t in lst:
            pts = tri_points(descs, tris[t])
            cx, cz = sum(p[0] for p in pts) / 3, sum(p[2] for p in pts) / 3
            cells[(int(math.floor((cx - box[0][0]) / TILE)), int(math.floor((cz - box[0][2]) / TILE)))].append(t)
        out += [cells[k] for k in sorted(cells)]
    return out


def _gap(a, b):
    """Distance between two boxes (0 when they touch or overlap)."""
    return math.sqrt(sum(max(0.0, a[0][k] - b[1][k], b[0][k] - a[1][k]) ** 2 for k in range(3)))


def _join(a, b):
    return ([min(a[0][k], b[0][k]) for k in range(3)], [max(a[1][k], b[1][k]) for k in range(3)])


def merge_small(descs, tris, pieces, limit=MAX_PIECES):
    """Small pieces (details, rails, single panels) join the nearest piece they touch, so the menu lists buildings,
    walls and floor tiles; the size that counts as small grows until the stage has at most `limit` pieces."""
    items = [[lst, _box([p for t in lst for p in tri_points(descs, tris[t])])] for lst in pieces]
    small = SMALL
    while True:
        changed = True
        while changed:
            changed = False
            items.sort(key=lambda it: max(it[1][1][k] - it[1][0][k] for k in range(3)))
            for i, it in enumerate(items):
                w = max(it[1][1][k] - it[1][0][k] for k in range(3))
                if w >= small:
                    break
                best, bv = None, None
                for j, other in enumerate(items):
                    if j == i or _gap(it[1], other[1]) > TOUCH:
                        continue
                    jb = _join(it[1], other[1])
                    if max(jb[1][0] - jb[0][0], jb[1][2] - jb[0][2]) > TILE * 1.5:
                        continue
                    v = (jb[1][0] - jb[0][0]) * (jb[1][1] - jb[0][1] + 1) * (jb[1][2] - jb[0][2])
                    if bv is None or v < bv:
                        best, bv = j, v
                if best is not None:
                    items[best][0] += it[0]
                    items[best][1] = _join(items[best][1], it[1])
                    del items[i]
                    changed = True
                    break
        if len(items) <= limit or small > TILE:
            return [it[0] for it in items]
        small *= 1.5


def collision_triangles(dat):
    c = sd.parse_collision(dat, struct.unpack_from('<i', dat, 0x94)[0])
    pool = c['pool']
    out = []
    for g in c['groups']:
        for t in g['tris']:
            out.append(tuple(pool[i] for i in t[:3]))
    return out, c


def assign_collision(col, pieces_pts):
    """Each collision triangle -> nearest piece by render vertex (grid hash), or None = barrier."""
    grid = collections.defaultdict(list)
    G = BARRIER_GAP
    for pi, pts in enumerate(pieces_pts):
        for p in pts:
            grid[(int(math.floor(p[0] / G)), int(math.floor(p[1] / G)), int(math.floor(p[2] / G)))].append((p, pi))
    out = []
    for tri in col:
        best, bd = None, G * G
        samples = list(tri) + [tuple(sum(v[k] for v in tri) / 3 for k in range(3))]
        for s in samples:
            cx, cy, cz = (int(math.floor(s[k] / G)) for k in range(3))
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    for dz in (-1, 0, 1):
                        for p, pi in grid.get((cx + dx, cy + dy, cz + dz), ()):
                            dd = (p[0] - s[0]) ** 2 + (p[1] - s[1]) ** 2 + (p[2] - s[2]) ** 2
                            if dd < bd:
                                best, bd = pi, dd
        out.append(best)
    return out


def analyse(stage):
    dat = open(os.path.join(MAP, stage + '.dat'), 'rb').read()
    R = at.parse(open(os.path.join(MAP, stage + '.alr'), 'rb').read())
    res = {'stage': stage, 'groups': {}}
    allpts = []
    for gi in world_groups(R):
        descs, tris, _ = group_triangles(R, gi)
        ps = merge_small(descs, tris, split(descs, tris))
        boxes = [_box([p for t in lst for p in tri_points(descs, tris[t])]) for lst in ps]
        res['groups'][gi] = {'triangles': len(tris), 'pieces': len(ps), 'formats': sorted({d['format'] for d in descs}),
                             'sizes': sorted((round(b[1][0] - b[0][0]), round(b[1][1] - b[0][1]), round(b[1][2] - b[0][2])) for b in boxes)[-5:]}
        allpts += [[p for t in lst for p in tri_points(descs, tris[t])] for lst in ps]
    col, _c = collision_triangles(dat)
    a = assign_collision(col, allpts)
    res['collision'] = len(col)
    res['barrier_triangles'] = sum(1 for x in a if x is None)
    return res


if __name__ == '__main__':
    for st in sys.argv[1:]:
        print(analyse(st))
