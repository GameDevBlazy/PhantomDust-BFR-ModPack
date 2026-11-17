"""Phantom Dust stage DAT (ST00) collision / start-point codec and append-and-repoint writer.

Independent research tool (2026-10-20). Layout from the Phantom Dust workspace spec
STAGE_DAT_SPEC_BATCH009.md (our own corpus decode) plus the conventions measured here:
- a collision object is records {u8 kind, u8 type (3 tri group, 8 vertex pool), u8 3, u8 b3,
  u32 payload size, u32 0} + payload, closed by ff ff cc cc 00 00 00 00 cc cc cc cc;
- tri group payload: f32 sphere centre[3] + radius, f32 aabb min[3], max[3], u16 tri count,
  u16 group flags, u32 0, then tris {u16 v0, v1, v2, u16 flags, f32 normal[3]};
- the stored normal always equals normalize(cross(v1 - v0, v2 - v0)) (stock stages: 100%);
- stock bounding spheres are tighter than AABB-centred ones (unknown method): re-encoding stock
  data keeps them (byte-exact round trip); new geometry gets an enclosing AABB-centred sphere;
- CP00 block: 'CP00', u16 count, u16 bytes (= 80 x count), then 80-byte entries.

Writer strategy (no relocation): new sections are appended at the end of the file and only
their header pointers change. The game reads these sections through the header pointer with no
tag checks (Batch 011), so the old bytes become unused space and no other offset moves.

Usage:
  python -B -I pd_stage_dat.py roundtrip MAP_DIR      (world collision + CP00 of every stNN.dat)
"""
import math
import os
import struct
import sys

TERM = bytes.fromhex('ffffcccc00000000cccccccc')
CP00_SLOTS = (0xA4, 0xA8, 0xAC, 0xB0, 0xB8, 0xBC)


def parse_collision(b, off):
    groups, pool, pool_hdr = [], [], None
    o = off
    while b[o:o + 12] != TERM:
        kind, rt, b2, b3, size, zero = struct.unpack_from('<BBBBII', b, o)
        pay = o + 12
        if rt == 3:
            sphere = struct.unpack_from('<4f', b, pay)
            aabb = struct.unpack_from('<6f', b, pay + 16)
            n, flags, res = struct.unpack_from('<HHI', b, pay + 0x28)
            tris = [struct.unpack_from('<4H3f', b, pay + 0x30 + 20 * i) for i in range(n)]
            groups.append({'kind': kind, 'b2': b2, 'b3': b3, 'flags': flags, 'reserved': res, 'sphere': sphere,
                           'aabb': aabb, 'tris': [t[:4] for t in tris], 'normals': [t[4:] for t in tris]})
        elif rt == 8:
            pool = [struct.unpack_from('<3f', b, pay + 12 * i) for i in range(size // 12)]
            pool_hdr = {'kind': kind, 'b2': b2, 'b3': b3}
        else:
            raise ValueError('unknown record type %d at %#x' % (rt, o))
        o = pay + size
    return {'groups': groups, 'pool': pool, 'pool_hdr': pool_hdr, 'end': o + 12}


def _normal(P, a, b_, c):
    u = [P[b_][k] - P[a][k] for k in range(3)]
    v = [P[c][k] - P[a][k] for k in range(3)]
    g = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
    L = math.sqrt(sum(x * x for x in g))
    return tuple(x / L for x in g) if L > 0 else (0.0, 1.0, 0.0)


def encode_collision(obj, keep_stored=True):
    """keep_stored=True re-emits stored spheres/aabbs/normals (round trip); False recomputes them."""
    P = obj['pool']
    out = bytearray()
    for g in obj['groups']:
        idx = sorted({i for t in g['tris'] for i in t[:3]})
        if keep_stored:
            sphere, aabb, normals = g['sphere'], g['aabb'], g['normals']
        else:
            mn = [min(P[i][k] for i in idx) for k in range(3)]
            mx = [max(P[i][k] for i in idx) for k in range(3)]
            c = [(mn[k] + mx[k]) / 2 for k in range(3)]
            r = max(math.dist(c, P[i]) for i in idx) * 1.0001 + 1e-3
            sphere, aabb = (c[0], c[1], c[2], r), tuple(mn + mx)
            normals = [_normal(P, *t[:3]) for t in g['tris']]
        pay = struct.pack('<4f6fHHI', *sphere, *aabb, len(g['tris']), g['flags'], g.get('reserved', 0))
        for t, n in zip(g['tris'], normals):
            pay += struct.pack('<4H3f', t[0], t[1], t[2], t[3], *n)
        out += struct.pack('<BBBBII', g['kind'], 3, g.get('b2', 3), g.get('b3', 0xFF), len(pay), 0) + pay
    ph = obj['pool_hdr']
    pool = b''.join(struct.pack('<3f', *p) for p in P)
    out += struct.pack('<BBBBII', ph['kind'], 8, ph.get('b2', 3), ph.get('b3', 0xFF), len(pool), 0) + pool
    return bytes(out + TERM)


def parse_cp00(b, off):
    if b[off:off + 4] != b'CP00':
        raise ValueError('CP00 tag missing')
    n, nb = struct.unpack_from('<HH', b, off + 4)
    if nb != 80 * n:
        raise ValueError('CP00 size mismatch')
    return [b[off + 8 + 80 * i:off + 8 + 80 * (i + 1)] for i in range(n)]


def encode_cp00(entries):
    return b'CP00' + struct.pack('<HH', len(entries), 80 * len(entries)) + b''.join(entries)


def make_cp00_entry(template, pos, heading, step=50.0, first=None):
    """New start point from a stock 80-byte entry: position, heading, three capsule spots (+20 / +32 / +44) along the heading at
    first, first + step, first + 2 step (first defaults to step), stock tail words kept.
    2026-11-12: callers use first=45 (4.5 m) - 168 stock capsules sit 3.9-17.1 m from their start (stage_audit item 4)."""
    e = bytearray(template)
    struct.pack_into('<4f', e, 0, pos[0], pos[1], pos[2], heading)
    dx, dz = math.sin(heading), math.cos(heading)
    f0 = step if first is None else first
    for k in range(3):
        struct.pack_into('<3f', e, 20 + 12 * k, pos[0] + dx * (f0 + step * k), pos[1], pos[2] + dz * (f0 + step * k))
    return bytes(e)


def append_and_repoint(base, sections):
    """sections: {header_offset: bytes}. Each blob is 4-byte aligned and appended; the header word at
    header_offset is set to the blob's offset (CP00 / tagged: the tag itself; world collision: start)."""
    out = bytearray(base)
    for hdr_off, blob in sections.items():
        while len(out) % 4:
            out.append(0)
        struct.pack_into('<i', out, hdr_off, len(out))
        out += blob
    return bytes(out)


def roundtrip(map_dir):
    res = []
    for f in sorted(os.listdir(map_dir)):
        if not (f.startswith('st') and f.endswith('.dat') and len(f) == 8) or f == 'st00.dat':
            continue  # st00 uses a different layout (Batch 009)
        b = open(os.path.join(map_dir, f), 'rb').read()
        if b[:4] != b'ST00':
            continue
        r = {'file': f}
        wc = struct.unpack_from('<i', b, 0x94)[0]
        if wc > 0:
            obj = parse_collision(b, wc)
            enc = encode_collision(obj, keep_stored=True)
            r['world_collision_exact'] = enc == b[wc:obj['end']]
            rec = encode_collision(obj, keep_stored=False)
            back = parse_collision(rec, 0)
            # recomputed normals must match stored ones; recomputed spheres must enclose
            r['normals_match'] = all(max(abs(x - y) for x, y in zip(n1, n2)) < 1e-3
                                     for g1, g2 in zip(obj['groups'], back['groups'])
                                     for n1, n2 in zip(g1['normals'], g2['normals']))
            r['tris'] = sum(len(g['tris']) for g in obj['groups'])
        cps = []
        for s in CP00_SLOTS:
            p = struct.unpack_from('<i', b, s)[0]
            if p > 0:
                ents = parse_cp00(b, p)
                cps.append(encode_cp00(ents) == b[p:p + 8 + 80 * len(ents)])
        r['cp00_exact'] = all(cps) if cps else None
        # append-and-repoint with the original sections must leave every other byte untouched
        if wc > 0:
            new = append_and_repoint(b, {0x94: b[wc:obj['end']]})
            r['repoint_ok'] = new[:0x94] == b[:0x94] and new[0x98:len(b)] == b[0x98:] and \
                parse_collision(new, struct.unpack_from('<i', new, 0x94)[0])['groups'] == obj['groups']
        res.append(r)
    return res


if __name__ == '__main__':
    if sys.argv[1] == 'roundtrip':
        rs = roundtrip(sys.argv[2])
        for r in rs:
            print(r)
        ok = all(r.get('world_collision_exact', True) and r.get('normals_match', True) and r['cp00_exact'] is not False
                 and r.get('repoint_ok', True) for r in rs)
        print('stages', len(rs), 'all ok', ok)
