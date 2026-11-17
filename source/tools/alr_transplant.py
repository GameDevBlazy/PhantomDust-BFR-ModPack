"""ALR container rebuild and model-group transplant (independent research tool, 2026-10-20).

Layout facts used (Batch 012 + 2026-10-20 census of the clean corpus, 980 containers with groups):
  header  u32 0x11, u32 header_span (= 0x20 + 4*group_count), u16 v0, u16 v1, u32 resource_offset,
          u32 group_count, u32 resource_span, 8 zero bytes, i32[group_count] group offsets
          (<= 0 absent; repeated values = aliases)
  then    texture table chunk 0x15 (u16 count at +8, 28-byte entries at +0x0C; entry +0x04 =
          pixel-data offset in the resource buffer)
  then    groups, tightly packed in ascending offset order (chunks ending with {0, 8})
  then    zero padding up to resource_offset (0x1000-aligned in the corpus)
  then    resource buffer (resource_span bytes), then any trailing bytes kept verbatim.
  Inside groups the only resource references are vertex-descriptor data offsets (chunk 0x16 entry
  +0x14) and material texture indices (chunk 0x01 entry +0x18, slot mask +0x16, Batch 030).

transplant(): copy donor groups into a recipient container as NEW group indices (appended), copying
the vertex buffers they reference and the textures their materials use (new texture-table entries),
and remapping descriptor offsets and material texture indices. Everything else is copied verbatim.

Usage:
  python -B -I alr_transplant.py rebuild-check ROOT
  python -B -I alr_transplant.py transplant RECIPIENT DONOR OUT group[,group...]
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import alr_chunks  # noqa: E402

TEX_ENTRY = 28
MAT_ENTRY = 0x4C


def align(n, a):
    return (n + a - 1) // a * a


def parse(b):
    c = alr_chunks.read_container(b)
    hs = c['header_span']
    if b[hs] != 0x15:
        raise ValueError('texture table does not follow the header')
    tspan = struct.unpack_from('<I', b, hs + 4)[0]
    ro = c['resource_offset']
    offs = c['offsets']
    present = sorted({o for o in offs if o > 0})
    limit = ro if ro else len(b)
    groups, prev = [], hs + tspan
    for k, o in enumerate(present):
        if o != prev:
            raise ValueError('groups not tightly packed at 0x%x' % o)
        nxt = present[k + 1] if k + 1 < len(present) else None
        _chunks, end = alr_chunks.walk_group(b, o, nxt or limit)
        if nxt is not None and end != nxt:
            raise ValueError('group at 0x%x does not end at the next group' % o)
        groups.append(bytearray(b[o:end]))
        prev = end
    group_end = prev
    if ro and b[group_end:ro].count(0) != ro - group_end:
        raise ValueError('non-zero padding before the resource buffer')
    m = {'version': struct.unpack_from('<HH', b, 8), 'resource_span': c['resource_span'],
         'raw_offsets': offs, 'present': present, 'tex': bytearray(b[hs:hs + tspan]),
         'groups': groups, 'resource_offset': ro, 'pad_to': ro - group_end if ro else 0,
         'resource': bytearray(b[ro:ro + c['resource_span']]) if ro else bytearray(),
         'trailing': bytes(b[ro + c['resource_span']:]) if ro else bytes(b[group_end:])}
    # group index -> unique group slot (or None for absent entries, keeping the raw value)
    m['slot'] = [present.index(o) if o > 0 else None for o in offs]
    return m


def build(m, res_align=0x1000, keep_resource_offset=True):
    n = len(m['slot'])
    hs = 0x20 + 4 * n
    starts, pos = [], hs + len(m['tex'])
    for g in m['groups']:
        starts.append(pos)
        pos += len(g)
    group_end = pos
    if m['resource_offset'] or m['resource']:
        ro = group_end + m['pad_to'] if keep_resource_offset else align(group_end, res_align)
    else:
        ro = 0
    offs = [starts[s] if s is not None else raw for s, raw in zip(m['slot'], m['raw_offsets'] + [0] * (n - len(m['raw_offsets'])))]
    # some Effect containers declare resource_span 0 and keep their data after it (Batch 012 note):
    # the declared span is kept unless the resource buffer was changed
    span = len(m['resource']) if m.get('resource_dirty') else m['resource_span']
    out = bytearray(struct.pack('<IIHHIII', 0x11, hs, m['version'][0], m['version'][1], ro, n, span))
    out += b'\0' * 8
    out += struct.pack('<%di' % n, *offs)
    out += m['tex']
    for g in m['groups']:
        out += g
    if ro:
        out += b'\0' * (ro - len(out))
        out += m['resource']
    out += m['trailing']
    return bytes(out)


def rebuild_check(root):
    ok = bad = skipped = 0
    fails = []
    for dp, _, fs in os.walk(root):
        for f in fs:
            if not f.lower().endswith('.alr') or 'Copy' in f:
                continue
            p = os.path.join(dp, f)
            b = open(p, 'rb').read()
            try:
                m = parse(b)
            except Exception as exc:     # containers the strict parser does not accept
                skipped += 1
                fails.append((p, 'parse: %s' % exc))
                continue
            if build(m) == b:
                ok += 1
            else:
                bad += 1
                fails.append((p, 'mismatch'))
    return ok, bad, skipped, fails


# ------------------------------------------------------------------ texture sizes
def texture_bytes(packed):
    w, h = 1 << ((packed >> 24) & 15), 1 << ((packed >> 20) & 15)
    mips, fmt, flags = (packed >> 16) & 15, (packed >> 8) & 0xFF, packed & 0xFF
    total = 0
    for k in range(max(mips, 1)):
        mw, mh = max(w >> k, 1), max(h >> k, 1)
        if fmt == 0x0C:
            sz = max(mw // 4, 1) * max(mh // 4, 1) * 8
        elif fmt in (0x0E, 0x0F):
            sz = max(mw // 4, 1) * max(mh // 4, 1) * 16
        elif fmt == 0x86:
            sz = mw * mh * 4
        elif fmt in (0x02, 0x82, 0x84, 0x85, 0xA8):   # 0x02: nasi.alr normal map; same bytes as pc00a0's 0x82 copy
            sz = mw * mh * 2                            # 0x84: B4G4R4A4 (FUN_1400bcac0 case 4 -> DXGI 115, 2026-10-26)
        else:
            raise ValueError('texture format 0x%02x has no confirmed size rule' % fmt)
        total += sz
    total = align(total, 0x80)      # whole mip chain rounded up to 0x80 (Batch 012 size rule)
    if flags & 4:
        total *= 6
    return total


def tex_entries(m):
    n = struct.unpack_from('<h', m['tex'], 8)[0]
    return [bytearray(m['tex'][12 + TEX_ENTRY * k:12 + TEX_ENTRY * (k + 1)]) for k in range(n)]


def set_tex_entries(m, entries):
    m['tex'] = bytearray(struct.pack('<IIhH', 0x15, 12 + TEX_ENTRY * len(entries), len(entries),
                                     struct.unpack_from('<H', m['tex'], 10)[0])) + b''.join(entries)


def walk(g):
    """Chunks of a stand-alone group bytearray: [(id, offset, span)]."""
    out, i = [], 0
    while True:
        cid, span = struct.unpack_from('<II', g, i)
        if cid == 0:
            return out
        out.append((cid, i, span))
        i += span


def mip_bytes(w, h, fmt):
    if fmt == 0x0C:
        return max(w // 4, 1) * max(h // 4, 1) * 8
    if fmt in (0x0E, 0x0F):
        return max(w // 4, 1) * max(h // 4, 1) * 16
    if fmt == 0x86:
        return w * h * 4
    if fmt in (0x02, 0x82, 0x84, 0x85, 0xA8):
        return w * h * 2
    raise ValueError('texture format 0x%02x has no confirmed size rule' % fmt)


def drop_top_mips(packed, data, n):
    """Remove the n largest mip levels (mips are stored largest first, contiguously; width exponent =
    bits 20-23, height = bits 24-27). Returns (packed, data) unchanged when the texture has too few
    mips, is a cubemap, or would drop below 4x4."""
    we, he = (packed >> 20) & 15, (packed >> 24) & 15
    mips, fmt, flags = (packed >> 16) & 15, (packed >> 8) & 0xFF, packed & 0xFF
    if n <= 0 or flags & 4 or mips <= n or we - n < 2 or he - n < 2:
        return packed, data
    skip = sum(mip_bytes(max(1 << (we - k), 1), max(1 << (he - k), 1), fmt) for k in range(n))
    new = (packed & ~0x0FFF0000) | ((he - n) << 24) | ((we - n) << 20) | ((mips - n) << 16)
    size = texture_bytes(new)
    out = data[skip:skip + size]
    if len(out) < size:
        out = out + b'\0' * (size - len(out))
    return new, out


# ------------------------------------------------------------------ transplant
def transplant(recipient, donor, donor_groups, tex_align=0x100, vb_align=0x80, tex_drop_mips=0):
    R, D = parse(recipient), parse(donor)
    for M in (R, D):
        if not M['resource_offset'] or M['trailing'] or M['resource_span'] != len(M['resource']):
            raise ValueError('transplant needs plain containers (resource buffer = declared span, no trailing data)')
    R['resource_dirty'] = True
    rtex, dtex = tex_entries(R), tex_entries(D)
    tex_map, vb_map, new_indices, report = {}, {}, [], {'textures': [], 'vertex_buffers': []}
    for gi in donor_groups:
        s = D['slot'][gi]
        if s is None:
            raise ValueError('donor group %d is absent' % gi)
        g = bytearray(D['groups'][s])
        for cid, co, sp in walk(g):
            if cid == 0x01:                        # materials: remap active texture slots
                cnt = struct.unpack_from('<H', g, co + 8)[0]
                for k in range(cnt):
                    e = co + 12 + MAT_ENTRY * k
                    mask = struct.unpack_from('<H', g, e + 0x16)[0]
                    for slot in range(4):
                        if not (mask >> (4 * slot)) & 15:
                            continue
                        t = struct.unpack_from('<H', g, e + 0x18 + 2 * slot)[0]
                        if t not in tex_map:
                            ent = bytearray(dtex[t])
                            off, packed = struct.unpack_from('<I', ent, 4)[0], struct.unpack_from('<I', ent, 12)[0]
                            size = texture_bytes(packed)
                            data = D['resource'][off:off + size]
                            if len(data) != size:
                                raise ValueError('texture %d data runs past the donor resource buffer' % t)
                            if tex_drop_mips:
                                packed, data = drop_top_mips(packed, bytes(data), tex_drop_mips)
                                struct.pack_into('<I', ent, 12, packed)
                                size = len(data)
                            pos = align(len(R['resource']), tex_align)
                            R['resource'] += b'\0' * (pos - len(R['resource'])) + data
                            struct.pack_into('<I', ent, 4, pos)
                            tex_map[t] = len(rtex)
                            rtex.append(ent)
                            report['textures'].append({'donor_index': t, 'new_index': tex_map[t], 'bytes': size, 'offset': pos})
                        struct.pack_into('<H', g, e + 0x18 + 2 * slot, tex_map[t])
            elif cid == 0x16:                      # vertex descriptors: copy buffers, remap offsets
                for k in range(struct.unpack_from('<I', g, co + 8)[0]):
                    d = co + 12 + 28 * k
                    stride, count = g[d + 1], struct.unpack_from('<I', g, d + 8)[0]
                    off = struct.unpack_from('<I', g, d + 0x14)[0]
                    key = (off, stride * count)
                    if key not in vb_map:
                        data = D['resource'][off:off + stride * count]
                        if len(data) != stride * count:
                            raise ValueError('vertex buffer runs past the donor resource buffer')
                        pos = align(len(R['resource']), vb_align)
                        R['resource'] += b'\0' * (pos - len(R['resource'])) + data
                        vb_map[key] = pos
                        report['vertex_buffers'].append({'donor_offset': off, 'bytes': stride * count, 'offset': pos,
                                                         'format': g[d], 'stride': stride, 'vertices': count})
                    struct.pack_into('<I', g, d + 0x14, vb_map[key])
        R['groups'].append(g)
        R['slot'].append(len(R['groups']) - 1)
        R['raw_offsets'].append(0)
        new_indices.append(len(R['slot']) - 1)
    set_tex_entries(R, rtex)
    out = build(R, keep_resource_offset=False)
    # self-check: the result parses, new groups walk, every material slot / descriptor is in range
    chk = parse(out)
    nt = len(tex_entries(chk))
    for gi in new_indices:
        g = chk['groups'][chk['slot'][gi]]
        for cid, co, sp in walk(g):
            if cid == 0x01:
                for k in range(struct.unpack_from('<H', g, co + 8)[0]):
                    e = co + 12 + MAT_ENTRY * k
                    mask = struct.unpack_from('<H', g, e + 0x16)[0]
                    for slot in range(4):
                        if (mask >> (4 * slot)) & 15:
                            assert struct.unpack_from('<H', g, e + 0x18 + 2 * slot)[0] < nt
            if cid == 0x16:
                for k in range(struct.unpack_from('<I', g, co + 8)[0]):
                    d = co + 12 + 28 * k
                    assert struct.unpack_from('<I', g, d + 0x14)[0] + g[d + 1] * struct.unpack_from('<I', g, d + 8)[0] <= len(chk['resource'])
    report['new_group_indices'] = new_indices
    report['group_count'] = len(chk['slot'])
    report['size'] = len(out)
    return out, report


if __name__ == '__main__':
    if sys.argv[1] == 'rebuild-check':
        ok, bad, skipped, fails = rebuild_check(sys.argv[2])
        print('byte-exact', ok, 'mismatch', bad, 'not parsed', skipped)
        for f in fails[:20]:
            print('  ', f)
    elif sys.argv[1] == 'transplant':
        out, rep = transplant(open(sys.argv[2], 'rb').read(), open(sys.argv[3], 'rb').read(),
                              [int(x) for x in sys.argv[5].split(',')])
        open(sys.argv[4], 'wb').write(out)
        print(rep)
