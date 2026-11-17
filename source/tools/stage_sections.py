"""Read-only decoder for Stage DAT (ST00) sections, Batch 009.

Independently authored from measurements on the clean reference corpus.
Grammars (offsets relative to the section payload):

  collision (OC00/OC01/OC02, untagged +0x94): objects until section end;
    object = records until terminator ff ff cc cc 00000000 cccccccc;
    record = u8 kind, u8 b1, u8 b2, u8 b3 (0xff in all but 3 records), u32 payload size, u32 0, payload;
    b1 == 3: triangle group: f32 sphere centre[3], f32 radius, f32 aabb min[3],
             f32 aabb max[3], u16 tri count, u16 group flags, u32 0,
             tri count x {u16 v0, v1, v2, u16 tri flags, f32 normal[3]};
    b1 == 8: vertex pool: payload/12 x f32 xyz.
  OB00 (+0x90): rows of 14 x i32 (56 bytes), one per NM00 name.
  EF00 chains (OE00/OE01, +0x98): 'EF00', u16 count, u16 bytes, count x 92-byte entries.
  SP00 (+0x9c): 'SP00', u32 groups, u32 ?, groups x {u32 n, n x 32 bytes},
    u32 pool bytes, u32 names, pool, then 20-byte slot records.
  MD00 (+0xa0): 'MD00', u32 alm bytes, u32 ala bytes, u32 slots, slots x alm
    name, slots x ala name, slots x (i32, i32), 8 bytes.
  animation groups (OA01 sequence; +0xf4 = u32 count + {u32 size, group}):
    group = chunks {u32 id, u32 span incl. header} ending with {0, 8}.
  +0xf8: rows of 8 x i16, one per NM00 name (-1 = none).

Usage: python -B stage_sections.py <Map dir> <out.json>
"""
import json
import os
import re
import struct
import sys

import stage_dat_inspect as sdi

TERM = bytes.fromhex('ffffcccc00000000cccccccc')
TAGS = re.compile(rb'^[A-Z]{2}\d\d$')


def section_bounds(b):
    """field -> (payload offset, end). End excludes a following 4-byte tag."""
    out = {}
    for sec in sdi.section_map(b):
        o, end = sec['offset'], sec['offset'] + sec['span']
        if end + 0 <= len(b) and end - 4 >= o and TAGS.match(b[end - 4:end] or b''):
            end -= 4
        out[sec['field']] = (o, end, sec['tag_before'] or sec['tag_at'])
    return out


def parse_collision(b, o, end):
    objects, i = [], o
    while i < end:
        groups, verts = [], None
        while True:
            if b[i:i + 12] == TERM:
                i += 12
                break
            kind, b1, b2, b3, size, zero = struct.unpack_from('<4BII', b, i)
            p = i + 12
            if zero != 0 or p + size > end:
                raise ValueError('bad collision record at 0x%x' % i)
            if b1 == 3:
                fl = struct.unpack_from('<10f', b, p)
                ntri, gflags, z2 = struct.unpack_from('<HHI', b, p + 40)
                if 48 + 20 * ntri != size:
                    raise ValueError('triangle group size mismatch at 0x%x' % i)
                tris = [struct.unpack_from('<4H3f', b, p + 48 + 20 * t) for t in range(ntri)]
                groups.append({'kind': kind, 'b2': b2, 'b3': b3, 'centre': fl[0:3], 'radius': fl[3],
                               'aabb_min': fl[4:7], 'aabb_max': fl[7:10],
                               'group_flags': gflags, 'reserved_u32': z2, 'tris': tris})
            elif b1 == 8:
                if size % 12:
                    raise ValueError('vertex pool size at 0x%x' % i)
                verts = [struct.unpack_from('<3f', b, p + 12 * v) for v in range(size // 12)]
                vkind = kind
            else:
                raise ValueError('unknown record b1=%d at 0x%x' % (b1, i))
            i = p + size
        objects.append({'offset': i, 'groups': groups, 'verts': verts,
                        'vertex_record_kind': vkind if verts is not None else None})
    return objects, end - i


def parse_ob00(b, o, end):
    n = (end - o) // 56
    rows = [struct.unpack_from('<14i', b, o + 56 * r) for r in range(n)]
    return rows, (end - o) - 56 * n


def parse_ef00_chain(b, o, end):
    blocks, i = [], o
    while i + 8 <= end and b[i:i + 4] == b'EF00':
        cnt, nbytes = struct.unpack_from('<HH', b, i + 4)
        stride = nbytes // cnt if cnt else 0
        ents = []
        for e in range(cnt):
            q = i + 8 + e * stride
            ents.append({'type': b[q], 'sub': b[q + 1], 'b2': b[q + 2], 'b3': b[q + 3],
                         'pos': struct.unpack_from('<3f', b, q + 4),
                         'rot': struct.unpack_from('<3f', b, q + 16),
                         'scale': struct.unpack_from('<3f', b, q + 28),
                         'params': b[q + 40:q + stride].hex()})
        blocks.append({'count': cnt, 'bytes': nbytes, 'stride': stride, 'entries': ents})
        i += 8 + nbytes
    return blocks, end - i


def parse_sp00(b, o, end):
    if b[o:o + 4] != b'SP00':
        raise ValueError('no SP00 tag')
    ngroups, w2 = struct.unpack_from('<II', b, o + 4)
    i, groups = o + 12, []
    for _ in range(ngroups):
        n = struct.unpack_from('<I', b, i)[0]
        frames = [struct.unpack_from('<8f', b, i + 4 + 32 * k) for k in range(n)]
        groups.append(frames)
        i += 4 + 32 * n
    pool_bytes, nnames = struct.unpack_from('<II', b, i)
    names = b[i + 8:i + 8 + pool_bytes].split(b'\0')[:nnames]
    return {'w2': w2, 'groups': groups, 'texture_names': [x.decode('latin-1') for x in names],
            'slot_table_bytes': end - (i + 8 + pool_bytes)}


def parse_md00(b, o, end):
    if b[o:o + 4] != b'MD00':
        raise ValueError('no MD00 tag')
    alm_bytes, ala_bytes, slots = struct.unpack_from('<III', b, o + 4)
    i = o + 16

    def names(i, n, nbytes):
        out, start = [], i
        for _ in range(n):
            e = b.index(b'\0', i)
            out.append(b[i:e].decode('latin-1'))
            i = e + 1
        if i - start != nbytes:
            raise ValueError('MD00 name list size mismatch')
        return out, i
    alm, i = names(i, slots, alm_bytes)
    ala, i = names(i, slots, ala_bytes)
    pairs = [struct.unpack_from('<2i', b, i + 8 * k) for k in range(slots)]
    i += 8 * slots
    return {'slots': slots, 'alm': alm, 'ala': ala, 'pairs': pairs,
            'trailer': b[i:i + 8].hex(), 'leftover': end - (i + 8)}


def parse_anim_group(b, i, end):
    chunks = []
    while True:
        cid, span = struct.unpack_from('<II', b, i)
        if cid == 0:
            if span != 8:
                raise ValueError('group terminator span %d at 0x%x' % (span, i))
            return chunks, i + 8
        if span < 8 or i + span > end:
            raise ValueError('chunk span at 0x%x' % i)
        info = {'id': cid, 'span': span}
        if cid == 5:
            info['length'] = struct.unpack_from('<f', b, i + 8)[0]
            info['joint'] = struct.unpack_from('<H', b, i + 12)[0]
        chunks.append(info)
        i += span


def parse_oa_sequence(b, o, end):
    groups, i = [], o
    while i < end:
        g, i = parse_anim_group(b, i, end)
        groups.append(g)
    return groups, end - i


def parse_f4(b, o, end):
    count = struct.unpack_from('<I', b, o)[0]
    i, groups = o + 4, []
    for _ in range(count):
        size = struct.unpack_from('<I', b, i)[0]
        g, nxt = parse_anim_group(b, i + 4, i + 4 + size)
        if nxt != i + 4 + size:
            raise ValueError('+0xf4 group size mismatch at 0x%x' % i)
        groups.append(g)
        i = nxt
    return groups, end - i


def parse_f8(b, o, end):
    n = (end - o) // 16
    return [struct.unpack_from('<8h', b, o + 16 * r) for r in range(n)], (end - o) - 16 * n


def name_pool(b, o, count):
    out, i = [], o
    for _ in range(max(count, 0)):
        e = b.index(b'\0', i)
        out.append(b[i:e].decode('latin-1'))
        i = e + 1
    return out


def decode_stage(b):
    if b[:4] != b'ST00':
        raise ValueError('not ST00')
    sec = section_bounds(b)
    name_count = struct.unpack_from('<i', b, 0x60)[0]
    r = {'size': len(b), 'name_count': name_count, 'sections': {}}
    if '0x5c' in sec:
        r['names'] = name_pool(b, sec['0x5c'][0], name_count)
    for fld, (o, end, tag) in sec.items():
        key = '%s:%s' % (fld, tag or 'untagged')
        try:
            if fld in ('0x64', '0x68', '0x6c', '0x94') and end > o:
                objs, left = parse_collision(b, o, end)
                r['sections'][key] = {'objects': len(objs), 'leftover': left,
                                      'triangles': sum(len(g['tris']) for ob in objs for g in ob['groups']),
                                      'vertices': sum(len(ob['verts'] or []) for ob in objs)}
            elif fld == '0x90' and end > o:
                # v2 (2026-11-06): one row per NM00 name (the header count +0x60), not the span to the next section - a stage
                # whose later sections were moved to the end (the composer, the .dat generator) has a longer span
                if 0 < name_count and o + 56 * name_count <= end:
                    end = o + 56 * name_count
                rows, left = parse_ob00(b, o, end)
                r['sections'][key] = {'rows': len(rows), 'leftover': left}
                r['ob00'] = rows
            elif fld in ('0x70', '0x74', '0x78', '0x98') and end > o:
                blocks, left = parse_ef00_chain(b, o, end)
                r['sections'][key] = {'blocks': len(blocks), 'entries': sum(x['count'] for x in blocks),
                                      'strides': sorted({x['stride'] for x in blocks}), 'leftover': left}
            elif fld == '0x9c':
                sp = parse_sp00(b, o, end)
                r['sections'][key] = {'groups': [len(g) for g in sp['groups']],
                                      'texture_names': len(sp['texture_names']),
                                      'slot_table_bytes': sp['slot_table_bytes']}
            elif fld == '0xa0':
                md = parse_md00(b, o, end)
                r['sections'][key] = {'slots': md['slots'], 'leftover': md['leftover'],
                                      'alm_named': sum(x != 'NOUSE' for x in md['alm']),
                                      'ala_named': sum(x != 'NOUSE' for x in md['ala'])}
                r['md00'] = md
            elif fld in ('0x7c', '0x80', '0x84') and end > o:
                groups, left = parse_oa_sequence(b, o, end)
                r['sections'][key] = {'groups': len(groups), 'leftover': left,
                                      'lengths': sorted({round(c['length'], 3) for g in groups for c in g if c['id'] == 5})}
            elif fld == '0xf4':
                groups, left = parse_f4(b, o, end)
                ids = sorted({c['id'] for g in groups for c in g})
                r['sections'][key] = {'groups': len(groups), 'chunk_ids': ids, 'leftover': left}
            elif fld == '0xf8':
                rows, left = parse_f8(b, o, end)
                r['sections'][key] = {'rows': len(rows), 'leftover': left}
                r['f8'] = rows
            else:
                r['sections'][key] = {'bytes': end - o, 'decoded': False}
        except (ValueError, struct.error) as exc:
            r['sections'][key] = {'error': str(exc)}
    return r


def main(map_dir, out_path):
    files = sorted(f for f in os.listdir(map_dir) if re.fullmatch(r'st\d\d\.dat', f) and f != 'st00.dat')
    result = {}
    for f in files:
        with open(os.path.join(map_dir, f), 'rb') as fh:
            r = decode_stage(fh.read())
        r.pop('ob00', None)
        r.pop('f8', None)
        r.pop('md00', None)
        result[f] = r
    with open(out_path, 'w', encoding='utf-8') as fh:
        json.dump(result, fh, indent=1)
    errors = [(f, k, v['error']) for f, r in result.items() for k, v in r['sections'].items() if 'error' in v]
    left = [(f, k, v['leftover']) for f, r in result.items() for k, v in r['sections'].items() if v.get('leftover')]
    print('stages', len(result), 'errors', errors, 'nonzero leftovers', left)


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
