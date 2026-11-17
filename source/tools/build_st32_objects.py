"""Copy stage objects from stock stages into st32 (independent research tool, 2026-10-20).

Facts used (Batch 009 corpus, Batch 011e parser 0x140203720, Batch 031 spawner 0x140203dc0):
  OB00 row (14 x i32): w0-w2 ALR group index (models), w3-w5 absolute DAT offsets (w4 = OA anim),
  w6-w8 absolute offsets of OC00/OC01/OC02 collision objects, w9-w11 absolute offsets of
  OE00/OE01/OE02 EF00 blocks, w12-w13 fill. Name i = NM00 string i; count at header +0x60.
  Placement row (0x24): +0 object index, +4/+5 class, +6/+7 params, +0xC pos, +0x18 rot.
  +0xF8 per-object rows of 8 x i16 (one per object), read only when the header word is not -1.

Everything copied is byte-for-byte from the source stage; only offsets and group indices are
rewritten. New sections are appended; the header words that point at them are repointed;
+0x138 (file size) is kept true.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pd_stage_dat as sd  # noqa: E402
import stage_sections as ss  # noqa: E402


def object_parts(src, index):
    r = ss.decode_stage(src)
    row = list(r['ob00'][index])
    parts = {'name': r['names'][index], 'row': row, 'oc': {}, 'oe': {}, 'f8': None}
    for w in (6, 7, 8):
        if row[w] >= 0:
            end = sd.parse_collision(src, row[w])['end']
            parts['oc'][w] = src[row[w]:end]
    for w in (9, 10, 11):
        if row[w] >= 0:
            cnt, nb = struct.unpack_from('<HH', src, row[w] + 4)
            assert src[row[w]:row[w] + 4] == b'EF00' and nb == 92 * cnt
            parts['oe'][w] = src[row[w]:row[w] + 8 + nb]
    for w in (3, 5):
        if row[w] >= 0:
            raise ValueError('object %s uses w%d (never used in stock data); not supported' % (parts['name'], w))
    parts['oa'] = {}
    if row[4] >= 0:                         # OA01 animation group: chunks ending {0, 8} (Batch 009 §7)
        _chunks, end = ss.parse_anim_group(src, row[4], len(src))
        parts['oa'][4] = src[row[4]:end]
    if 'f8' in r and index < len(r['f8']):
        parts['f8'] = r['f8'][index]
    return parts


def build(base, objects, placements, with_f8=False):
    """objects: list of (parts, {old_group: new_group}); placements: list of (object_no, row_bytes).
    Returns the new DAT bytes. base must have no objects / placements of its own (st32)."""
    if struct.unpack_from('<i', base, 0x60)[0] not in (0, -1):
        raise ValueError('base already has objects')
    out = bytearray(base)

    def append(tag, payload):
        while len(out) % 4:
            out.append(0)
        if tag:
            out.extend(tag)
        at = len(out)
        out.extend(payload)
        return at
    names = b''.join(p['name'].encode('latin-1') + b'\0' for p, _ in objects)
    nm = append(b'NM00', names)
    rows, first = [], {}
    for sec, words in (('OC00', 6), ('OC01', 7), ('OE00', 9), ('OE01', 10), ('OA01', 4)):
        blob, offs = bytearray(), []
        for p, _ in objects:
            src = p.get('oa', {}) if words == 4 else (p['oc'] if words < 9 else p['oe'])
            offs.append(len(blob) if words in src else None)
            if words in src:
                blob += src[words]
        if blob:
            at = append(sec.encode(), bytes(blob))
            first[words] = at
            for k, o in enumerate(offs):
                if o is not None:
                    objects[k][0].setdefault('new', {})[words] = at + o
    for k, (p, gmap) in enumerate(objects):
        row = list(p['row'])
        for w in (0, 1, 2):
            if row[w] >= 0:
                row[w] = gmap[row[w]]
        for w in (4, 6, 7, 8, 9, 10, 11):
            row[w] = p.get('new', {}).get(w, -1)
        row[12], row[13] = (0, -1) if k == 0 else (-842150451, -842150451)
        rows.append(struct.pack('<14i', *row))
    ob = append(b'OB00', b''.join(rows))
    ps = append(None, b''.join(r for _, r in placements))
    words = {0x5C: nm, 0x60: len(objects), 0x90: ob, 0x04: ps, 0x30: len(placements)}
    for w, sec in ((6, 0x64), (7, 0x68), (9, 0x70), (10, 0x74), (4, 0x80)):
        if w in first:
            words[sec] = first[w]
    if with_f8:
        f8 = b''.join(struct.pack('<8h', *(p['f8'] or (-1,) * 8)) for p, _ in objects)
        words[0xF8] = append(None, f8)
    for w, v in words.items():
        struct.pack_into('<i', out, w, v)
    struct.pack_into('<I', out, 0x138, len(out))
    out = bytes(out)
    # self-checks
    assert out[0x140:len(base)] == base[0x140:]
    r = ss.decode_stage(out)
    assert r['names'] == [p['name'] for p, _ in objects]
    for k, (p, _) in enumerate(objects):
        got = struct.unpack_from('<14i', out, ob + 56 * k)
        for w in (6, 7, 8):
            if got[w] >= 0:
                assert sd.parse_collision(out, got[w])['end'] - got[w] == len(p['oc'][w])
        for w in (9, 10, 11):
            if got[w] >= 0:
                assert out[got[w]:got[w] + len(p['oe'][w])] == p['oe'][w]
    return out


def placement_row(template, obj_no, pos, rot=None, params=None):
    r = bytearray(template)
    struct.pack_into('<i', r, 0, obj_no)
    if params is not None:
        struct.pack_into('<BB', r, 6, *params)
    struct.pack_into('<3f', r, 0xC, *pos)
    if rot is not None:
        struct.pack_into('<3f', r, 0x18, *rot)
    return bytes(r)
