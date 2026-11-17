"""Minimal-stage reference: full read-only decode of a stage pair (default st32.dat + st32.alr).

Independently authored (2026-10-20) from the Batch 009/012/027 specs and our own readers.
Outputs (to OUT_DIR, private):
  reference.json   every header word, every section span with its decode, byte coverage,
                   ALR container, texture table, groups, chunks, meshes, vertex buffers;
  tex_<name>.png   decoded textures (DXT1/DXT3/DXT5 and 32-bit; others skipped);
  group<N>.obj     each model group's meshes (format 1/3 vertices: position, UV / 4096).

Usage: python -B -I st32_reference.py MAP_DIR OUT_DIR [stage=32]
"""
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import alr_chunks  # noqa: E402
import ssb_names  # noqa: E402
import stage_dat_inspect as sdi  # noqa: E402
import stage_sections as ss  # noqa: E402
from pd_common import _alpha_block, _color_block, png_bytes  # noqa: E402

HEADER_NAMES = {
    0x00: 'signature', 0x04: 'PS00 ptr', 0x08: 'variant-0 placement override', 0x0C: 'PS01 ptr',
    0x10: 'PS02 ptr', 0x30: 'PS00 count', 0x34: 'variant-0 count override', 0x38: 'PS01 count',
    0x3C: 'PS02 count', 0x5C: 'NM00 name pool', 0x60: 'name count', 0x64: 'OC00', 0x68: 'OC01',
    0x6C: 'OC02', 0x70: 'OE00', 0x74: 'OE01', 0x78: 'OE02', 0x7C: 'OA00', 0x80: 'OA01', 0x84: 'OA02',
    0x90: 'OB00 object table', 0x94: 'world collision', 0x98: 'EF00 stage emitters', 0x9C: 'SP00 sprites',
    0xA0: 'MD00 effect-model slots', 0xA4: 'CP00 block 1', 0xA8: 'CP00 block 2', 0xAC: 'CP00 block 3',
    0xB0: 'CP00 block 4', 0xB4: 'CP00 (never used)', 0xB8: 'CP00 block 5', 0xBC: 'CP00 block 6',
    0xD0: 'SSB slot 0', 0xD4: 'SSB slot 1', 0xD8: 'SSB slot 2', 0xF4: 'resource-group list',
    0xF8: 'per-object effect refs', 0xFC: 'light-like list', 0x138: 'file end', 0x13C: 'first tag',
}


def decode_cp00(b, o):
    count, nbytes = struct.unpack_from('<HH', b, o + 4)
    out = []
    for i in range(count):
        e = o + 8 + 80 * i
        f = struct.unpack_from('<20f', b, e)
        w = struct.unpack_from('<20I', b, e)
        out.append({'offset': e, 'position': f[0:3], 'heading_rad': f[3], 'flags': '0x%08x' % w[4],
                    'trail': [f[5:8], f[8:11], f[11:14]], 'tail_words': ['0x%x' % x for x in w[14:20]]})
    return {'count': count, 'bytes': nbytes, 'entries': out}


def decode_dat(b):
    r = {'size': len(b), 'header': []}
    for o in range(0, 0x140, 4):
        v = struct.unpack_from('<i', b, o)[0]
        if o == 0 or o == 0x13C:
            v = b[o:o + 4].decode('latin-1')
        r['header'].append({'offset': '0x%02x' % o, 'name': HEADER_NAMES.get(o, ''), 'value': v})
    secs = sorted(sdi.section_map(b), key=lambda s: s['offset'])
    r['sections'] = secs
    r['summary'] = ss.decode_stage(b)
    bounds = ss.section_bounds(b)
    if '0x94' in bounds:
        o, end, _ = bounds['0x94']
        objs, _left = ss.parse_collision(b, o, end)
        ob = objs[0]
        V = ob['verts']
        r['world_collision'] = {
            'objects': len(objs), 'groups': len(ob['groups']), 'vertices': len(V),
            'triangles': sum(len(g['tris']) for g in ob['groups']),
            'x_values': sorted({round(v[0], 3) for v in V}), 'y_values': sorted({round(v[1], 3) for v in V}),
            'z_values': sorted({round(v[2], 3) for v in V}),
            'group_flags': sorted({g['group_flags'] for g in ob['groups']}),
            'record_kinds': sorted({g['kind'] for g in ob['groups']}),
            'first_groups': [{k: v for k, v in g.items() if k != 'tris'} | {'tris': g['tris']} for g in ob['groups'][:2]],
        }
    if '0xa4' in bounds:
        r['cp00'] = decode_cp00(b, struct.unpack_from('<i', b, 0xA4)[0])
    if '0xa0' in bounds:
        r['md00'] = ss.parse_md00(b, *bounds['0xa0'][:2])
    if '0x9c' in bounds:
        sp = ss.parse_sp00(b, *bounds['0x9c'][:2])
        r['sp00'] = {k: v for k, v in sp.items() if k != 'groups'} | {'groups': sp['groups']}
    # byte coverage: header + every section span (tags included) must tile the file
    spans = [(0, 0x140)] + [(s['offset'] - (4 if s['tag_before'] else 0), s['offset'] + s['span']) for s in secs]
    spans.sort()
    gaps, pos = [], 0
    for a, z in spans:
        if a > pos:
            gaps.append((pos, a, b[pos:a].hex()))
        pos = max(pos, z)
    if pos < len(b):
        gaps.append((pos, len(b), b[pos:].hex()))
    r['uncovered'] = gaps
    return r


def texture_size(w, h, fmt, mips):
    total = 0
    for m in range(max(mips, 1)):
        mw, mh = max(w >> m, 1), max(h >> m, 1)
        if fmt == 0x0C:
            total += max(mw // 4, 1) * max(mh // 4, 1) * 8
        elif fmt in (0x0E, 0x0F):
            total += max(mw // 4, 1) * max(mh // 4, 1) * 16
        elif fmt == 0x86:
            total += mw * mh * 4
        else:
            total += mw * mh * 2
    return total


def decode_tex(b, o, w, h, fmt):
    pix = [(0, 0, 0, 255)] * (w * h)
    if fmt == 0x0C:
        for by in range(0, h, 4):
            for bx in range(0, w, 4):
                _color_block(b[o:o + 8], pix, bx, by, w, True)
                o += 8
        return pix
    if fmt in (0x0E, 0x0F):
        alphas = [255] * (w * h)
        for by in range(0, h, 4):
            for bx in range(0, w, 4):
                if fmt == 0x0F:
                    _alpha_block(b[o:o + 8], alphas, bx, by, w)
                else:
                    bits = int.from_bytes(b[o:o + 8], 'little')
                    for i in range(16):
                        alphas[(by + i // 4) * w + bx + i % 4] = ((bits >> (4 * i)) & 15) * 17
                _color_block(b[o + 8:o + 16], pix, bx, by, w, False)
                o += 16
        return [p[:3] + (a,) for p, a in zip(pix, alphas)]
    if fmt == 0x85:  # RGB565 (st32 grid renders correctly this way)
        vals = struct.unpack_from('<%dH' % (w * h), b, o)
        return [(((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31, 255) for v in vals]
    if fmt == 0x86:  # 32-bit, assumed B G R A byte order (HYPOTHESIS)
        return [(b[o + 4 * i + 2], b[o + 4 * i + 1], b[o + 4 * i], b[o + 4 * i + 3]) for i in range(w * h)]
    return None


def decode_alr(b, out_dir):
    c, groups, errors = alr_chunks.analyse(b)
    res = c['resource_offset']
    r = {'size': len(b), 'container': c, 'errors': errors, 'textures': [], 'groups': []}
    hs = c['header_span']
    cid, span = struct.unpack_from('<II', b, hs)
    n = struct.unpack_from('<h', b, hs + 8)[0]
    for k in range(n):
        w = struct.unpack_from('<7I', b, hs + 12 + 28 * k)
        packed = w[3]
        # width exponent = bits 20-23, height = bits 24-27 (2026-10-20: 15/16 non-square stock textures)
        he, we, mips, fmt = (packed >> 24) & 15, (packed >> 20) & 15, (packed >> 16) & 15, (packed >> 8) & 0xFF
        name = ssb_names.decode(w[5], w[6]) or 'tex%02d' % k
        t = {'index': k, 'name': name, 'data_offset': w[1], 'packed': '0x%08x' % packed,
             'width': 1 << we, 'height': 1 << he, 'mips': mips, 'format': '0x%02x' % fmt,
             'flags': '0x%02x' % (packed & 0xFF), 'bytes': texture_size(1 << we, 1 << he, fmt, mips)}
        pix = decode_tex(b, res + w[1], t['width'], t['height'], fmt)
        if pix is not None:
            fn = 'tex_%02d_%s.png' % (k, name)
            open(os.path.join(out_dir, fn), 'wb').write(png_bytes(t['width'], t['height'], pix))
            t['png'] = fn
        r['textures'].append(t)
    for gi, o in enumerate(c['offsets']):
        g = groups.get(o)
        if g is None:
            r['groups'].append({'index': gi, 'offset': o, 'absent': True})
            continue
        gr = {'index': gi, 'offset': o, 'chunks': [], 'descriptors': [], 'meshes': []}
        for cid, co, sp in g['chunks']:
            gr['chunks'].append({'id': '0x%x' % cid, 'offset': co, 'span': sp,
                                 'hex': b[co:co + sp].hex() if cid in (0x01, 0x03, 0x13) else None})
            if cid == 0x16:
                for k in range(struct.unpack_from('<I', b, co + 8)[0]):
                    d = co + 12 + 28 * k
                    gr['descriptors'].append({'format': b[d], 'stride': b[d + 1], 'raw': b[d:d + 28].hex(),
                                              'vertices': struct.unpack_from('<I', b, d + 8)[0],
                                              'data_offset': struct.unpack_from('<I', b, d + 0x14)[0]})
            if cid == 0x02:
                mat, ref, desc, topo = struct.unpack_from('<4H', b, co + 0x40)
                base, used, nidx = struct.unpack_from('<3I', b, co + 0x48)
                idx = list(struct.unpack_from('<%dH' % nidx, b, co + 0x68))
                gr['meshes'].append({'offset': co, 'bounds': [round(x, 3) for x in struct.unpack_from('<14f', b, co + 8)],
                                     'material': mat, 'reference_row': ref, 'descriptor': desc, 'topology': topo,
                                     'base_vertex': base, 'used_vertices': used, 'index_count': nidx,
                                     'header_hex': b[co:co + 0x68].hex(), 'indices': idx})
        if gr['meshes']:
            # material k -> PNG of its slot-0 texture (Batch 030 layout), for a textured OBJ preview
            mtl = {}
            for cid, co, sp in g['chunks']:
                if cid == 0x01:
                    for k in range(struct.unpack_from('<H', b, co + 8)[0]):
                        e = co + 12 + 0x4C * k
                        if struct.unpack_from('<H', b, e + 0x16)[0] & 0xF:
                            ti = struct.unpack_from('<H', b, e + 0x18)[0]
                            if ti < len(r['textures']) and 'png' in r['textures'][ti]:
                                mtl[k] = r['textures'][ti]['png']
            gr['material_png'] = mtl
            write_obj(b, res, gr, os.path.join(out_dir, 'group%02d.obj' % gi), mtl)
        r['groups'].append(gr)
    return r


def s11(v):
    return v - 2048 if v & 1024 else v


def write_obj(b, res, gr, path, mtl=None):
    lines, vbase = ['# decoded from stock stage ALR; private research output'], 0
    if mtl:
        mtl_name = os.path.splitext(os.path.basename(path))[0] + '.mtl'
        lines.append('mtllib ' + mtl_name)
        with open(os.path.join(os.path.dirname(path), mtl_name), 'w', newline='\n') as f:
            for k, png in sorted(mtl.items()):
                f.write('newmtl mat%d\nKd 1 1 1\nmap_Kd %s\n\n' % (k, png))
    for di, d in enumerate(gr['descriptors']):
        if d['format'] not in (1, 3):
            continue
        p0 = res + d['data_offset']
        verts = []
        for v in range(d['vertices']):
            p = p0 + v * d['stride']
            x, y, z = struct.unpack_from('<3f', b, p)
            u, vv = struct.unpack_from('<2h', b, p + 16)
            col = struct.unpack_from('<I', b, p + 20)[0]
            verts.append((x, y, z, u / 4096.0, vv / 4096.0, col))
        d['position_min'] = [round(min(v[i] for v in verts), 3) for i in range(3)]
        d['position_max'] = [round(max(v[i] for v in verts), 3) for i in range(3)]
        d['uv_min'] = [round(min(v[3 + i] for v in verts), 4) for i in range(2)]
        d['uv_max'] = [round(max(v[3 + i] for v in verts), 4) for i in range(2)]
        d['colours'] = sorted({'0x%08x' % v[5] for v in verts})[:8]
        for x, y, z, u, vv, _ in verts:
            lines.append('v %.4f %.4f %.4f' % (x, y, z))
        for x, y, z, u, vv, _ in verts:
            lines.append('vt %.5f %.5f' % (u, 1 - vv))
        for mi, m in enumerate(gr['meshes']):
            if m['descriptor'] != di:
                continue
            lines.append('g mesh%02d_mat%d' % (mi, m['material']))
            if mtl and m['material'] in mtl:
                lines.append('usemtl mat%d' % m['material'])
            idx = m['indices']
            if m['topology'] == 5:
                tris = [idx[j:j + 3] for j in range(0, len(idx) - 2, 3)]
            else:
                tris = []
                for j in range(len(idx) - 2):
                    a, b_, c = idx[j], idx[j + 1], idx[j + 2]
                    if a == b_ or b_ == c or a == c:
                        continue
                    tris.append((a, b_, c) if j % 2 == 0 else (b_, a, c))
            m['triangles'] = len(tris)
            for t in tris:
                lines.append('f ' + ' '.join('%d/%d' % (vbase + i + 1, vbase + i + 1) for i in t))
        vbase += d['vertices']
    open(path, 'w', newline='\n').write('\n'.join(lines) + '\n')


def main(map_dir, out_dir, stage=32):
    os.makedirs(out_dir, exist_ok=True)
    dat = open(os.path.join(map_dir, 'st%02d.dat' % stage), 'rb').read()
    alr = open(os.path.join(map_dir, 'st%02d.alr' % stage), 'rb').read()
    import hashlib
    r = {'stage': stage,
         'dat_sha256': hashlib.sha256(dat).hexdigest(), 'alr_sha256': hashlib.sha256(alr).hexdigest(),
         'dat': decode_dat(dat), 'alr': decode_alr(alr, out_dir)}
    json.dump(r, open(os.path.join(out_dir, 'reference.json'), 'w'), indent=1, default=list)
    return r


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 32)
