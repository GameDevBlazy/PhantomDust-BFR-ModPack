"""Maze stage on the st32 template (owner request 2026-10-22). Independent research tool; read-only on all inputs.

Owner brief: a maze, 4 spawn points, places to fall off, a skybox, 1 gravity lift, and the Dawn City floor TV
(the screen that shows the players). Variant b adds TV screens as walls (owner: "walls that are that huge tv screen").

What it builds (all geometry generated here; every copied part is byte-for-byte from the clean reference):
- st32.alr: group 0 = maze render mesh (floor slab, walls; Dawn City textures `romen`, `window_a`, `metal`);
  group 2 = Dawn City sky dome (st05 group 2; group 2 is the sky in every stock stage); new groups = the st05 floor
  screen (st05 group 83, CRT-monitor material, flattened and scaled) and the lift glow models (st05 groups 85/86).
- st32.dat: world collision (floor with pits and open edges, wall boxes), 5 CP00 blocks (4 starts + 3 capsules each),
  the st05 lift object (st05_jump_, class 4,16, strength 120) as one placement, a stage EF00 block of type-13 model
  emitters for the screen(s), an MD00 with the screen and lift-glow slots (14/15, the stock lift-glow slots in st05
  and st06), and the +0xF4 resource list extended with the two lift-glow animation groups.

Facts used: MINIMAL_STAGE_ST32.md, STAGE_DAT_SPEC_BATCH009.md, STAGE_OBJECTS_AND_PLACEMENT_BATCH031.md (IGT-021),
our earlier stage tools, and this session's st05 reading: MD00 slot 10 = st05_mdl_screen ->
ALR group 83 (a 4-vertex quad, material shader word 0x80c18fc6, textures e3o_vision01/03), placed by stage EF00
entry 0 (type 13, param 0 = MD00 slot).
Usage: python -B -I build_maze_st32.py OUT_ROOT
"""
import hashlib
import json
import math
import os
import random
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import alr_transplant as at  # noqa: E402
import build_st32_objects as bo  # noqa: E402
import pd_stage_dat as sd  # noqa: E402
import stage_sections as ss  # noqa: E402
import ssb_names  # noqa: E402
import pd_common as pc  # noqa: E402  (collision grouping + floor test)
import pd_paths  # noqa: E402
from pd_common import pack_normal, bounds  # noqa: E402

MAP = pd_paths.MAP
CLEAN = {'st32.dat': 'f6434dca', 'st32.alr': 'dfa293a1'}

N = 9                 # cells per side
CELL = 120.0          # 12 m corridors (10 PD units ~ 1 m, MINIMAL_STAGE_ST32 §3)
WALL_H = 60.0         # 6 m: above a jump, below a strength-120 lift throw
WALL_T = 6.0
FLOOR_Y = 200.0       # high enough for a real drop before the stock fall-out (the 053d roof at ~705 held)
SLAB = 10.0           # visible floor thickness
HUB = range(3, 6)     # 3 x 3 centre room
SEED = 22102026
BRAID = 0.12          # share of remaining inner walls removed to make loops
STAND = 10.0
CAPSULE_STEP = (40.0, 80.0, 120.0)
COLOUR = 0xFF7F7F7F
SCREEN_SCALE = 1.5    # floor TV: 59 x 76 -> 89 x 115
LIFT_ROT = (-1.5708, 1.5074, -1.4859)   # st05 placement row 5 (stock, upright)
# test variants (owner 2026-10-23: "a bunch of tests"): falls = pits + edge drops; lifts = named lift sets;
# tv = floor screen; panels = how many centre-room TV walls (4 = left of each door, 8 = both sides of each door)
VARIANTS = {
    'a': dict(name='IGT-070a_maze_floor_tv', falls=True, lifts='centre', tv=True, panels=0),
    'b': dict(name='IGT-070b_maze_tv_walls', falls=True, lifts='centre', tv=True, panels=4),
    'c': dict(name='IGT-070c_maze_control', falls=False, lifts='none', tv=False, panels=0),
    'd': dict(name='IGT-070d_maze_tv_room', falls=True, lifts='centre', tv=True, panels=8),
    'e': dict(name='IGT-070e_maze_eight_lifts', falls=True, lifts='eight', tv=True, panels=0),
}


def sha(b):
    return hashlib.sha256(b).hexdigest()


def cx(i):
    return -N * CELL / 2 + CELL * (i + 0.5)


# ------------------------------------------------------------------ maze layout
def make_maze():
    rnd = random.Random(SEED)
    # walls: ('v', i, j) between (i, j) and (i + 1, j); ('h', i, j) between (i, j) and (i, j + 1)
    walls = {('v', i, j) for i in range(N - 1) for j in range(N)} | {('h', i, j) for i in range(N) for j in range(N - 1)}

    def wall(a, b):
        (i, j), (k, l) = sorted([a, b])
        return ('v', i, j) if k == i + 1 else ('h', i, j)

    def nbrs(c):
        i, j = c
        return [(a, b) for a, b in ((i + 1, j), (i - 1, j), (i, j + 1), (i, j - 1)) if 0 <= a < N and 0 <= b < N]
    hub = {(i, j) for i in HUB for j in HUB}
    for a in hub:                                   # open the centre room inside
        for b in nbrs(a):
            if b in hub:
                walls.discard(wall(a, b))
    # perfect maze over the non-hub cells plus the hub as one node, by randomized DFS
    seen, stack = set(hub), [(0, 0)]
    seen.add((0, 0))
    doors = []
    while stack:
        c = stack[-1]
        opts = [b for b in nbrs(c) if b not in seen]
        if not opts:
            stack.pop()
            continue
        b = rnd.choice(opts)
        walls.discard(wall(c, b))
        seen.add(b)
        stack.append(b)
    # centre room doorways: the middle cell of each side opens outward
    for a, b in (((4, 3), (4, 2)), ((4, 5), (4, 6)), ((3, 4), (2, 4)), ((5, 4), (6, 4))):
        walls.discard(wall(a, b))
        doors.append((a, b))
    # braid: remove some inner walls (never around the hub, so it keeps exactly 4 doorways)
    inner = sorted(w for w in walls if not any(c in hub for c in wall_cells(w)))
    for w in rnd.sample(inner, int(len(inner) * BRAID)):
        walls.discard(w)
    return walls, hub, doors


def wall_cells(w):
    k, i, j = w
    return [(i, j), (i + 1, j)] if k == 'v' else [(i, j), (i, j + 1)]


def open_dirs(walls, c):
    i, j = c
    out = []
    for a, b in ((i + 1, j), (i - 1, j), (i, j + 1), (i, j - 1)):
        if 0 <= a < N and 0 <= b < N:
            (p, q), (r, s) = sorted([c, (a, b)])
            w = ('v', p, q) if r == p + 1 else ('h', p, q)
            if w not in walls:
                out.append((a, b))
    return out


def plan(walls, hub):
    corners = [(0, 0), (N - 1, 0), (0, N - 1), (N - 1, N - 1)]
    keep = set(corners) | hub
    for c in corners:                      # spawn cell, its exit and the next cell stay solid
        for b in open_dirs(walls, c):
            keep.add(b)
            keep |= set(open_dirs(walls, b))
    dead = [c for c in sorted((i, j) for i in range(N) for j in range(N))
            if c not in keep and len(open_dirs(walls, c)) == 1]
    rnd = random.Random(SEED + 1)
    pits = set(rnd.sample(dead, min(4, len(dead))))
    # outer-edge drops: the middle cell of each side loses its outer wall (floor ends -> fall off)
    edges = [((4, 0), 'z-'), ((4, N - 1), 'z+'), ((0, 4), 'x-'), ((N - 1, 4), 'x+')]
    return corners, pits, edges


# ------------------------------------------------------------------ geometry
def box(x0, x1, y0, y1, z0, z1):
    """Quads (4 points, outward winding) of a box: +y top, -y bottom, and 4 sides."""
    return {
        'top': [(x0, y1, z0), (x0, y1, z1), (x1, y1, z1), (x1, y1, z0)],
        'bottom': [(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)],
        'x-': [(x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0)],
        'x+': [(x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1)],
        'z-': [(x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0)],
        'z+': [(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)],
    }


def wall_boxes(walls, edges):
    h = N * CELL / 2
    out = []
    for k, i, j in sorted(walls):
        if k == 'v':
            x = -h + CELL * (i + 1)
            out.append((x - WALL_T / 2, x + WALL_T / 2, -h + CELL * j, -h + CELL * (j + 1)))
        else:
            z = -h + CELL * (j + 1)
            out.append((-h + CELL * i, -h + CELL * (i + 1), z - WALL_T / 2, z + WALL_T / 2))
    gaps = {(c, side) for c, side in edges}
    for t in range(N):                         # perimeter, minus the four edge drops
        for c, side, rect in (((t, 0), 'z-', (-h + CELL * t, -h + CELL * (t + 1), -h - WALL_T / 2, -h + WALL_T / 2)),
                              ((t, N - 1), 'z+', (-h + CELL * t, -h + CELL * (t + 1), h - WALL_T / 2, h + WALL_T / 2)),
                              ((0, t), 'x-', (-h - WALL_T / 2, -h + WALL_T / 2, -h + CELL * t, -h + CELL * (t + 1))),
                              ((N - 1, t), 'x+', (h - WALL_T / 2, h + WALL_T / 2, -h + CELL * t, -h + CELL * (t + 1)))):
            if (c, side) not in gaps:
                out.append(rect)
    return out


def tris_of(quad, both=False):
    a, b, c, d = quad
    t = [(a, b, c), (a, c, d)]
    if both:
        t += [(a, c, b), (a, d, c)]
    return t


def collision_triangles(walls, pits, edges):
    h = N * CELL / 2
    tris = []
    for i in range(N):
        for j in range(N):
            if (i, j) in pits:
                continue
            x0, z0 = -h + CELL * i, -h + CELL * j
            q = [(x0, FLOOR_Y, z0), (x0, FLOOR_Y, z0 + CELL), (x0 + CELL, FLOOR_Y, z0 + CELL), (x0 + CELL, FLOOR_Y, z0)]
            tris += tris_of(q)
    for x0, x1, z0, z1 in wall_boxes(walls, edges):
        b = box(x0, x1, FLOOR_Y, FLOOR_Y + WALL_H, z0, z1)
        for side in ('top', 'x-', 'x+', 'z-', 'z+'):
            tris += tris_of(b[side], both=True)    # both windings, as our tested walls
    return tris


def render_quads(walls, pits, edges):
    """[(material, quad, (u-length, v-length))]: material 0 floor, 1 wall face, 2 wall top / slab edge."""
    h = N * CELL / 2
    out = []
    solid = {(i, j) for i in range(N) for j in range(N)} - pits
    for i, j in sorted(solid):
        x0, z0 = -h + CELL * i, -h + CELL * j
        b = box(x0, x0 + CELL, FLOOR_Y - SLAB, FLOOR_Y, z0, z0 + CELL)
        out.append((0, b['top'], (1, 1)))
        out.append((2, b['bottom'], (1, 1)))
        for side, nb in (('x-', (i - 1, j)), ('x+', (i + 1, j)), ('z-', (i, j - 1)), ('z+', (i, j + 1))):
            if nb not in solid:
                out.append((2, b[side], (1, SLAB / CELL)))
    for x0, x1, z0, z1 in wall_boxes(walls, edges):
        b = box(x0, x1, FLOOR_Y, FLOOR_Y + WALL_H, z0, z1)
        L = max(x1 - x0, z1 - z0) / CELL
        for side in ('x-', 'x+', 'z-', 'z+'):
            long_side = (side[0] == 'x') == (z1 - z0 > x1 - x0)
            out.append((1, b[side], (L if long_side else WALL_T / CELL, 1)))
        out.append((2, b['top'], (L, WALL_T / CELL)))
    return out


# ------------------------------------------------------------------ DAT helpers
def encode_md00(md):
    alm = b''.join(n.encode('latin-1') + b'\0' for n in md['alm'])
    ala = b''.join(n.encode('latin-1') + b'\0' for n in md['ala'])
    out = b'MD00' + struct.pack('<III', len(alm), len(ala), md['slots']) + alm + ala
    out += b''.join(struct.pack('<2i', *p) for p in md['pairs']) + bytes.fromhex(md['trailer'])
    return out


def f4_groups(b):
    o = struct.unpack_from('<i', b, 0xF4)[0]
    n = struct.unpack_from('<I', b, o)[0]
    o += 4
    out = []
    for _ in range(n):
        sz = struct.unpack_from('<I', b, o)[0]
        out.append(b[o + 4:o + 4 + sz])
        o += 4 + sz
    return out


def encode_f4(groups):
    return struct.pack('<I', len(groups)) + b''.join(struct.pack('<I', len(g)) + g for g in groups)


def ef00_block(template, entries):
    """entries: [(pos, md00_slot)]; every other byte from the st05 type-13 template entry."""
    out = bytearray()
    for pos, slot in entries:
        e = bytearray(template)
        struct.pack_into('<3f', e, 4, *pos)
        struct.pack_into('<i', e, 40, slot)
        out += e
    return b'EF00' + struct.pack('<HH', len(entries), 92 * len(entries)) + bytes(out)


# ------------------------------------------------------------------ ALR helpers
def vb_of(R, g):
    for cid, co, sp in at.walk(g):
        if cid == 0x16:
            d = co + 12
            return d, g[d], g[d + 1], struct.unpack_from('<I', g, d + 8)[0], struct.unpack_from('<I', g, d + 0x14)[0]
    raise ValueError('group has no vertex descriptor')


def screen_points(R, g):
    d, fmt, stride, cnt, off = vb_of(R, g)
    return [struct.unpack_from('<3f', R['resource'], off + stride * k) for k in range(cnt)]


def screen_variant(R, g_src, xform):
    """Copy of the screen group with every vertex position passed through xform; new vertex buffer appended."""
    g = bytearray(g_src)
    d, fmt, stride, cnt, off = vb_of(R, g)
    vb = bytearray(R['resource'][off:off + stride * cnt])
    pts = []
    for k in range(cnt):
        p = xform(struct.unpack_from('<3f', vb, stride * k))
        struct.pack_into('<3f', vb, stride * k, *p)
        pts.append(p)
    pos = at.align(len(R['resource']), 0x80)
    R['resource'] += b'\0' * (pos - len(R['resource'])) + vb
    struct.pack_into('<I', g, d + 0x14, pos)
    for cid, co, sp in at.walk(g):
        if cid == 0x02:
            g[co + 0x08:co + 0x40] = bounds(pts)
    return g, pts


def copy_texture(R, D, name):
    dt = at.tex_entries(D)
    for e in dt:
        if ssb_names.decode(*struct.unpack_from('<II', e, 20)) == name:
            ent = bytearray(e)
            off, packed = struct.unpack_from('<I', ent, 4)[0], struct.unpack_from('<I', ent, 12)[0]
            data = D['resource'][off:off + at.texture_bytes(packed)]
            pos = at.align(len(R['resource']), 0x100)
            R['resource'] += b'\0' * (pos - len(R['resource'])) + data
            struct.pack_into('<I', ent, 4, pos)
            rt = at.tex_entries(R)
            rt.append(ent)
            at.set_tex_entries(R, rt)
            return len(rt) - 1
    raise KeyError(name)


def world_group(R, quads, tex_idx):
    """Group 0 = maze mesh, built like our earlier stage meshes (format 1 vertices, triangle lists)."""
    g0 = R['groups'][R['slot'][0]]
    chunks = {cid: bytes(g0[co:co + sp]) for cid, co, sp in at.walk(g0) if cid in (0x01, 0x03, 0x16, 0x0D)}
    mat_tmpl = chunks[0x01][12:12 + 0x4C]
    mat_flags = struct.unpack_from('<H', chunks[0x01], 10)[0]
    desc = bytearray(chunks[0x16][12:12 + 28])
    mesh_hdr = bytes(next(g0[co:co + 0x68] for cid, co, sp in at.walk(g0) if cid == 0x02))
    mats = []
    for k, t in enumerate(tex_idx):
        e = bytearray(mat_tmpl)
        struct.pack_into('<I', e, 0x10, 0x1001 | (k << 16))
        struct.pack_into('<H', e, 0x16, 0x0001)
        struct.pack_into('<4H', e, 0x18, t, 0, 0, 0)
        mats.append(bytes(e))
    verts, by_mat = [], {k: [] for k in range(len(tex_idx))}
    for m, q, (ul, vl) in quads:
        a, b, c = q[0], q[1], q[2]
        n = pc._n((a, b, c))
        uv = [(0, vl), (ul, vl), (ul, 0), (0, 0)]
        for side in (1, -1):                       # double-sided, as our tested meshes
            base_i = len(verts)
            for p, (u, v) in zip(q, uv):
                verts.append((p, pack_normal(tuple(side * c for c in n)), int(round(u * 4096)), int(round(v * 4096))))
            if side == 1:
                by_mat[m] += [base_i, base_i + 1, base_i + 2, base_i, base_i + 2, base_i + 3]
            else:
                by_mat[m] += [base_i, base_i + 2, base_i + 1, base_i, base_i + 3, base_i + 2]
    assert len(verts) <= 65535
    vb = b''.join(struct.pack('<3fIhhI', p[0], p[1], p[2], n, u, v, COLOUR) for p, n, u, v in verts)
    vpos = at.align(len(R['resource']), 0x80)
    R['resource'] += b'\0' * (vpos - len(R['resource'])) + vb
    struct.pack_into('<I', desc, 8, len(verts))
    struct.pack_into('<I', desc, 0x14, vpos)
    meshes = []
    for k, idx in by_mat.items():
        lo, hi = min(idx), max(idx)
        hdr = bytearray(mesh_hdr)
        body = struct.pack('<%dH' % len(idx), *idx)
        span = at.align(0x68 + len(body), 4)
        struct.pack_into('<II', hdr, 0, 0x02, span)
        hdr[0x08:0x40] = bounds([verts[i][0] for i in idx])
        struct.pack_into('<4H3I', hdr, 0x40, k, 0, 0, 5, lo, hi - lo + 1, len(idx))
        hdr[0x54:0x68] = b'\0' * 20
        meshes.append(bytes(hdr) + body + b'\0' * (span - 0x68 - len(body)))
    mat_chunk = struct.pack('<IIHH', 0x01, 12 + 0x4C * len(mats), len(mats), mat_flags) + b''.join(mats)
    desc_chunk = struct.pack('<III', 0x16, 12 + 28, 1) + bytes(desc)
    return bytearray(mat_chunk + chunks[0x03] + desc_chunk + b''.join(meshes) + chunks[0x0D] + struct.pack('<II', 0, 8)), len(verts)


# ------------------------------------------------------------------ build
def build(out_root):
    st32d = open(os.path.join(MAP, 'st32.dat'), 'rb').read()
    st32a = open(os.path.join(MAP, 'st32.alr'), 'rb').read()
    st05d = open(os.path.join(MAP, 'st05.dat'), 'rb').read()
    st05a = open(os.path.join(MAP, 'st05.alr'), 'rb').read()
    for n, b in (('st32.dat', st32d), ('st32.alr', st32a)):
        assert sha(b).startswith(CLEAN[n]), n + ' is not the clean reference'
    walls, hub, doors = make_maze()
    corners, all_pits, all_edges = plan(walls, hub)
    h = N * CELL / 2

    # where the screens go
    floor_tv = (0.0, FLOOR_Y + 0.3, 0.0)
    lift_cell = (4, 3)                             # centre room, south middle cell (in front of the south door)
    lift_pos = (cx(lift_cell[0]), FLOOR_Y, cx(lift_cell[1]) - 20.0)
    # variant e: one lift in front of each centre-room door (inside) and one in each quadrant
    lift_sets = {'none': [], 'centre': [lift_pos],
                 'eight': [lift_pos, (cx(4), FLOOR_Y, cx(5) + 20.0), (cx(3) - 20.0, FLOOR_Y, cx(4)),
                           (cx(5) + 20.0, FLOOR_Y, cx(4))] + [(cx(i), FLOOR_Y, cx(j)) for i, j in ((2, 2), (6, 2), (2, 6), (6, 6))]}
    for i, j in ((2, 2), (6, 2), (2, 6), (6, 6)):
        assert (i, j) not in all_pits
    # variant b: one TV wall on the inner face of each centre-room side (the corner segment left of each door);
    # variant d adds the segment right of each door (all 8 inner wall segments)
    r0, r1 = -h + CELL * 3, -h + CELL * 6        # room bounds
    tv_walls = [  # (centre, facing axis) - panels sit 0.5 inside the room, 108 x 54
        ((-h + CELL * 3.5, FLOOR_Y + 3 + 27, r0 + WALL_T / 2 + 0.5), '+z'),
        ((-h + CELL * 5.5, FLOOR_Y + 3 + 27, r1 - WALL_T / 2 - 0.5), '-z'),
        ((r0 + WALL_T / 2 + 0.5, FLOOR_Y + 3 + 27, -h + CELL * 5.5), '+x'),
        ((r1 - WALL_T / 2 - 0.5, FLOOR_Y + 3 + 27, -h + CELL * 3.5), '-x'),
        ((-h + CELL * 5.5, FLOOR_Y + 3 + 27, r0 + WALL_T / 2 + 0.5), '+z'),
        ((-h + CELL * 3.5, FLOOR_Y + 3 + 27, r1 - WALL_T / 2 - 0.5), '-z'),
        ((r0 + WALL_T / 2 + 0.5, FLOOR_Y + 3 + 27, -h + CELL * 3.5), '+x'),
        ((r1 - WALL_T / 2 - 0.5, FLOOR_Y + 3 + 27, -h + CELL * 5.5), '-x'),
    ]
    # the centre-room walls must exist where the panels hang
    for (i, j), (k, l) in (((3, 3), (3, 2)), ((5, 5), (5, 6)), ((3, 5), (2, 5)), ((5, 3), (6, 3)),
                           ((5, 3), (5, 2)), ((3, 5), (3, 6)), ((3, 3), (2, 3)), ((5, 5), (6, 5))):
        (p, q), (r, s) = sorted([(i, j), (k, l)])
        assert (('v', p, q) if r == p + 1 else ('h', p, q)) in walls

    reports = {}
    for variant, cfg in VARIANTS.items():
        pits, edges = (all_pits, all_edges) if cfg['falls'] else (set(), [])
        lifts = lift_sets[cfg['lifts']]
        # ---------------- ALR
        out1, trep = at.transplant(st32a, st05a, [2, 83, 85, 86])
        R = at.parse(out1)
        D = at.parse(st05a)
        R['resource_dirty'] = True
        g_sky, g_screen, g_jump1, g_jump2 = trep['new_group_indices']
        R['groups'][R['slot'][2]] = bytearray(R['groups'][R['slot'][g_sky]])     # sky in the stock sky slot
        screen_src = bytes(R['groups'][R['slot'][g_screen]])
        # floor TV: flatten (y = 0) and scale
        g, pts_floor = screen_variant(R, screen_src, lambda p: (p[0] * SCREEN_SCALE, 0.0, p[2] * SCREEN_SCALE))
        R['groups'][R['slot'][g_screen]] = g
        wall_groups = []
        if cfg['panels']:
            for _, face in tv_walls[:cfg['panels']]:
                want = {'+z': (0, 0, 1), '-z': (0, 0, -1), '+x': (1, 0, 0), '-x': (-1, 0, 0)}[face]
                src_pts = screen_points(R, screen_src)
                for mirror in (1, -1):
                    def xf(p, face=face, mirror=mirror):
                        u, v = mirror * p[2] / 38.32 * 54.0, p[0] / 29.54 * 27.0   # quad z -> panel width, x -> height
                        return {'+z': (u, v, 0.0), '-z': (-u, v, 0.0), '+x': (0.0, v, -u), '-x': (0.0, v, u)}[face]
                    # keep the stock front side: the first triangle's normal (the floor quad's +y) faces the room
                    n = pc._n([xf(p) for p in src_pts[:3]])
                    if sum(a * b for a, b in zip(n, want)) > 0:
                        break
                else:
                    raise AssertionError('no orientation faces the room')
                g, _ = screen_variant(R, screen_src, xf)
                R['groups'].append(g)
                R['slot'].append(len(R['groups']) - 1)
                R['raw_offsets'].append(0)
                wall_groups.append(len(R['slot']) - 1)
        for name in ('e3o_vision02', 'e3o_vision04'):        # the screen's sibling textures, in case they are looked up
            copy_texture(R, D, name)
        tex = [copy_texture(R, D, n) for n in ('romen', 'window_a', 'metal')]
        quads = render_quads(walls, pits, edges)
        g0, nverts = world_group(R, quads, tex)
        R['groups'][R['slot'][0]] = g0
        alr = at.build(R, keep_resource_offset=False)
        C = at.parse(alr)
        assert len(C['slot']) == len(R['slot'])

        # ---------------- DAT
        lift = bo.object_parts(st05d, 1)
        assert lift['name'] == 'st05_jump_'
        ps00 = struct.unpack_from('<i', st05d, 0x04)[0]
        tmpl_row = st05d[ps00 + 5 * 0x24:ps00 + 6 * 0x24]
        assert struct.unpack_from('<i', tmpl_row, 0)[0] == 1 and tmpl_row[4:6] == bytes([4, 16])
        if lifts:
            rows = [(0, bo.placement_row(tmpl_row, 0, lp, LIFT_ROT)) for lp in lifts]
            dat = bo.build(st32d, [(lift, {})], rows)
        else:
            dat = st32d

        stock = sd.parse_collision(st32d, struct.unpack_from('<i', st32d, 0x94)[0])
        pc.KIND, pc.FLAGS, pc.POOL_KIND = stock['groups'][0]['kind'], stock['groups'][0]['flags'], stock['pool_hdr']['kind']
        pc.CELL = 80.0
        obj, dropped = pc.build_object(collision_triangles(walls, pits, edges))
        coll = sd.encode_collision(obj, keep_stored=False)
        back = sd.parse_collision(coll, 0)

        cp_t = sd.parse_cp00(st32d, struct.unpack_from('<i', st32d, 0xA4)[0])[0]
        entries, pts = [], []
        for c in corners:
            ex = open_dirs(walls, c)[0]
            x, z = cx(c[0]), cx(c[1])
            hd = math.atan2(cx(ex[0]) - x, cx(ex[1]) - z)
            fy = pc.floor_height_below(back, x, z, FLOOR_Y + 5)
            assert fy is not None and abs(fy - FLOOR_Y) < 0.01
            e = bytearray(sd.make_cp00_entry(cp_t, (x, fy + STAND, z), hd))
            caps = []
            for k, d in enumerate(CAPSULE_STEP):
                px, pz = x + math.sin(hd) * d, z + math.cos(hd) * d
                cy = pc.floor_height_below(back, px, pz, FLOOR_Y + 5)
                assert cy is not None and abs(cy - FLOOR_Y) < 0.01, 'capsule over a pit'
                struct.pack_into('<3f', e, 20 + 12 * k, px, cy + STAND, pz)
                caps.append([round(px, 2), round(cy + STAND, 2), round(pz, 2)])
            entries.append(bytes(e))
            pts.append({'start': [round(x, 2), round(fy + STAND, 2), round(z, 2)], 'heading': round(hd, 3), 'capsules': caps})
        blocks = {w: sd.encode_cp00([entries[(i + k) % 4] for i in range(4)]) for k, w in enumerate((0xA4, 0xA8, 0xAC, 0xB8, 0xBC))}

        md = dict(ss.decode_stage(st32d)['md00'])
        md['alm'], md['ala'], md['pairs'] = list(md['alm']), list(md['ala']), list(md['pairs'])
        md5 = ss.decode_stage(st05d)['md00']
        f4 = f4_groups(st32d)
        f4_05 = f4_groups(st05d)
        ef = []
        if cfg['tv']:
            md['alm'][10], md['pairs'][10] = md5['alm'][10], (g_screen, -1)
            ef.append((floor_tv, 10))
        for slot, grp, src_f4 in ((14, g_jump1, md5['pairs'][14][1]), (15, g_jump2, md5['pairs'][15][1])) if lifts else ():
            f4.append(f4_05[src_f4])
            md['alm'][slot], md['ala'][slot], md['pairs'][slot] = md5['alm'][slot], md5['ala'][slot], (grp, len(f4) - 1)
        for k, grp in enumerate(wall_groups):
            slot = 16 + k
            md['alm'][slot], md['pairs'][slot] = md5['alm'][10], (grp, -1)
            ef.append((tv_walls[k][0], slot))
        e05 = struct.unpack_from('<i', st05d, 0x98)[0]
        ef_t = st05d[e05 + 8:e05 + 8 + 92]
        assert ef_t[0] == 13 and struct.unpack_from('<i', ef_t, 40)[0] == 10

        sections = dict(blocks)
        sections[0x94] = coll
        if ef:
            sections[0x98] = ef00_block(ef_t, ef)
        sections.update({0xA0: encode_md00(md), 0xF4: encode_f4(f4)})
        out = bytearray(sd.append_and_repoint(dat, sections))
        struct.pack_into('<I', out, 0x138, len(out))
        out = bytes(out)

        # self-checks on the result
        r = ss.decode_stage(out)
        assert r['names'] == (['st05_jump_'] if lifts else ss.decode_stage(st32d)['names'])
        m2 = r['md00']
        for s_ in ((10,) if cfg['tv'] else ()) + ((14, 15) if lifts else ()) + tuple(16 + k for k in range(len(wall_groups))):
            assert 0 <= m2['pairs'][s_][0] < len(C['slot']) and C['slot'][m2['pairs'][s_][0]] is not None
        assert encode_md00(ss.decode_stage(st05d)['md00']) == st05d[struct.unpack_from('<i', st05d, 0xA0)[0]:][:len(encode_md00(md5))]
        assert f4_groups(out) == f4
        for w in blocks:
            assert len(sd.parse_cp00(out, struct.unpack_from('<i', out, w)[0])) == 4
        sd.parse_collision(out, struct.unpack_from('<i', out, 0x94)[0])

        name = cfg['name']
        od = os.path.join(out_root, name)
        os.makedirs(os.path.join(od, 'Map'), exist_ok=True)
        open(os.path.join(od, 'Map', 'st32.dat'), 'wb').write(out)
        open(os.path.join(od, 'Map', 'st32.alr'), 'wb').write(alr)
        rep = {
            'variant': variant, 'seed': SEED, 'cells': N, 'cell_size': CELL, 'wall_height': WALL_H, 'floor_y': FLOOR_Y,
            'walls': len(walls), 'pits': sorted(pits), 'edge_drops': [list(c) + [s] for c, s in edges],
            'spawn_points': 4, 'spawn_blocks': 5, 'capsule_spots': 12, 'gravity_lifts': len(lifts),
            'lift_position': lifts[0] if lifts else None, 'lift_positions': lifts,
            'floor_tv': floor_tv if cfg['tv'] else None, 'tv_walls': [t[0] for t in tv_walls[:cfg['panels']]],
            'collision': {'groups': len(obj['groups']), 'pool_vertices': len(obj['pool']), 'bytes': len(coll),
                          'dropped_degenerate': dropped},
            'render_vertices': nverts, 'alr_groups': len(C['slot']), 'textures': len(at.tex_entries(C)),
            'cp00': pts,
            'files': {'Map/st32.dat': {'sha256': sha(out), 'size': len(out), 'base_sha256': sha(st32d)},
                      'Map/st32.alr': {'sha256': sha(alr), 'size': len(alr), 'base_sha256': sha(st32a)}},
        }
        json.dump(rep, open(os.path.join(od, 'build_report.json'), 'w'), indent=1)
        reports[variant] = rep
    return reports


if __name__ == '__main__':
    reps = build(sys.argv[1])
    for v, r in reps.items():
        print(v, json.dumps({k: r[k] for k in ('pits', 'edge_drops', 'collision', 'render_vertices', 'alr_groups',
                                                'textures', 'lift_position')}))
        print('  ', {k: v_['sha256'][:8] for k, v_ in r['files'].items()})
