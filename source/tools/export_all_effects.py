"""Export every Phantom Dust skill effect (Effect/e.i -> aN.e spans) into open files, read-only on the clean data.

Per effect request N -> OUT/eff_NNNN/:
  textures/<name>.png        every DXT1/DXT3/DXT5 texture of the span (other formats: raw .bin + note)
  gN_mM_<joint>.obj          every mesh (positions, UVs, vertex colours as 'v x y z r g b'), per group / mesh
  skeleton_gN.json           joint records of each group (translation, rotation, scale, id, parent)
  nodes_gN.json              the 0x13 node chunks and 0x14 emitter chunks (raw floats / words) before each mesh
  summary.json               groups, materials (shader, flags, textures), vertex formats, mesh table, EFT tail
Plus OUT/skills_effects_index.csv: every skill record -> its effect request -> files, and OUT/vertex_formats.csv.
Vertex layouts: ALR_STAGE_VERTEX_FORMATS.md (position first in every code; UV = i16/4096; colour D3DCOLOR).
Usage: python -B -I export_all_effects.py OUT_DIR [first_request last_request]
"""
import csv, json, os, struct, sys, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import alr_transplant as at
import effect_packs as ep
import ssb_names as sn
import dxt1_tools

import pd_paths  # noqa: E402
DATA = pd_paths.DATA
E = os.path.join(DATA, 'Effect')
# format code -> (stride, uv offset or None, colour offset or None)   (ALR_STAGE_VERTEX_FORMATS.md; 9 from effects)
LAYOUT = {1: (24, 16, 20), 3: (32, 16, 20), 5: (28, 16, 20), 7: (16, None, None), 8: (20, None, 16), 9: (20, 12, 16),
          0x10: (12, None, None), 0x15: (24, 12, 16), 0x16: (28, 12, 16), 0x17: (32, 12, 16), 0x18: (16, 12, None),
          0x1A: (40, 16, None), 0x1D: (36, 16, 20), 0x1F: (32, 16, None), 0x21: (32, 16, 20), 0x25: (28, 16, None)}


def c565(c):
    return ((c >> 11 & 31) * 255 // 31, (c >> 5 & 63) * 255 // 63, (c & 31) * 255 // 31)


def dxt_rows(d, w, h, fmt):
    rows = [[(0, 0, 0, 0)] * w for _ in range(h)]
    bs = 8 if fmt == 0x0C else 16
    o = 0
    for by in range(0, h, 4):
        for bx in range(0, w, 4):
            if o + bs > len(d):
                return rows
            if fmt == 0x0C:
                c0, c1, bits = struct.unpack_from('<HHI', d, o)
                a, b = c565(c0), c565(c1)
                pal = [a, b, tuple((2 * x + y) // 3 for x, y in zip(a, b)), tuple((x + 2 * y) // 3 for x, y in zip(a, b))] if c0 > c1 else \
                      [a, b, tuple((x + y) // 2 for x, y in zip(a, b)), (0, 0, 0)]
                cols = [pal[bits >> 2 * i & 3] for i in range(16)]
                al = [255 if (c0 > c1 or (bits >> 2 * i & 3) != 3) else 0 for i in range(16)]
            else:
                if fmt == 0x0E:
                    al = [(d[o + i // 2] >> 4 * (i & 1) & 15) * 17 for i in range(16)]
                else:
                    a0, a1 = d[o], d[o + 1]
                    bits = int.from_bytes(d[o + 2:o + 8], 'little')
                    pal = [a0, a1] + ([((7 - i) * a0 + i * a1) // 7 for i in range(1, 7)] if a0 > a1 else
                                      [((5 - i) * a0 + i * a1) // 5 for i in range(1, 5)] + [0, 255])
                    al = [pal[bits >> 3 * i & 7] for i in range(16)]
                c0, c1, bits = struct.unpack_from('<HHI', d, o + 8)
                a, b = c565(c0), c565(c1)
                pal = [a, b, tuple((2 * x + y) // 3 for x, y in zip(a, b)), tuple((x + 2 * y) // 3 for x, y in zip(a, b))]
                cols = [pal[bits >> 2 * i & 3] for i in range(16)]
            for i in range(16):
                y, x = by + i // 4, bx + i % 4
                if y < h and x < w:
                    rows[y][x] = cols[i] + (al[i],)
            o += bs
    return rows


def write_png_rgba(path, rows):
    import zlib
    h, w = len(rows), len(rows[0])
    raw = b''.join(b'\0' + bytes(c for p in r for c in p) for r in rows)
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
                           + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


def export(req, pack, off, span, packs, out):
    blob = packs[pack][off:off + span]
    alr, tail = blob[:-0x800], blob[-0x800:]
    try:
        m = at.parse(alr)
    except Exception as exc:
        return {'request': req, 'error': 'parse: %s' % exc}
    d = os.path.join(out, 'eff_%04d' % req)
    os.makedirs(os.path.join(d, 'textures'), exist_ok=True)
    res = m['resource'] + m['trailing']
    summary = {'request': req, 'pack': 'a%d.e' % pack, 'offset': off, 'span': span, 'textures': [], 'groups': []}
    texnames = []
    for k, t in enumerate(at.tex_entries(m)):
        toff, packed = struct.unpack_from('<I', t, 4)[0], struct.unpack_from('<I', t, 12)[0]
        name = sn.decode(*struct.unpack_from('<II', t, 0x14))
        w, h = 1 << ((packed >> 20) & 15), 1 << ((packed >> 24) & 15)
        fmt, mips = (packed >> 8) & 255, (packed >> 16) & 15
        texnames.append(name)
        rec = {'index': k, 'name': name, 'w': w, 'h': h, 'format': hex(fmt), 'mips': mips, 'offset': toff}
        if fmt in (0x0C, 0x0E, 0x0F):
            size = (w // 4 * h // 4 if w >= 4 and h >= 4 else 1) * (8 if fmt == 0x0C else 16)
            rows = dxt_rows(res[toff:toff + size], w, h, fmt)
            fn = '%02d_%s.png' % (k, name)
            write_png_rgba(os.path.join(d, 'textures', fn), rows)
            rec['file'] = fn
        else:
            try:
                size = at.texture_bytes(packed)
            except ValueError:
                size = 0
            fn = '%02d_%s.bin' % (k, name)
            open(os.path.join(d, 'textures', fn), 'wb').write(res[toff:toff + size])
            rec['file'] = fn + ' (format not decoded)'
        summary['textures'].append(rec)
    for gi, s in enumerate(m['slot']):
        if s is None:
            continue
        g = m['groups'][s]
        G = {'group': gi, 'materials': [], 'joints': [], 'vertex_buffers': [], 'meshes': [], 'nodes': []}
        chunks = at.walk(g)
        vbs = []
        pending_nodes = []
        for cid, co, sp in chunks:
            if cid == 1:
                for k in range(struct.unpack_from('<H', g, co + 8)[0]):
                    e = co + 12 + 0x4C * k
                    mask = struct.unpack_from('<H', g, e + 0x16)[0]
                    slots = struct.unpack_from('<4H', g, e + 0x18)
                    G['materials'].append({'index': k, 'shader': sn.decode(*struct.unpack_from('<II', g, e)),
                                           'flags': hex(struct.unpack_from('<I', g, e + 0xC)[0]),
                                           'blend_word': hex(struct.unpack_from('<H', g, e + 0x14)[0]), 'mask': hex(mask),
                                           'textures': [texnames[slots[i]] if (mask >> 4 * i) & 15 and slots[i] < len(texnames) else None for i in range(4)]})
            elif cid == 3:
                for k in range(struct.unpack_from('<H', g, co + 8)[0]):
                    j = co + 0x10 + 0x40 * k
                    t = struct.unpack_from('<9f', g, j)
                    G['joints'].append({'index': k, 'translation': t[0:3], 'rotation': t[3:6], 'scale': t[6:9],
                                        'id': struct.unpack_from('<H', g, j + 0x26)[0], 'parent': struct.unpack_from('<h', g, j + 0x2A)[0]})
            elif cid == 0x16:
                for k in range(struct.unpack_from('<I', g, co + 8)[0]):
                    dd = co + 12 + 28 * k
                    vb = {'index': k, 'format': g[dd], 'stride': g[dd + 1], 'count': struct.unpack_from('<I', g, dd + 8)[0],
                          'offset': struct.unpack_from('<I', g, dd + 0x14)[0]}
                    vbs.append(vb)
                    G['vertex_buffers'].append(vb)
            elif cid in (0x13, 0x14, 0x12):
                body = g[co + 8:co + sp]
                nd = {'chunk': hex(cid), 'span': sp, 'hex': body.hex()}
                if cid == 0x13:
                    nd['floats'] = list(struct.unpack_from('<%df' % (len(body) // 4), body))
                if cid == 0x14:
                    nd['name_pair'] = sn.decode(*struct.unpack_from('<II', body, 8)) if len(body) >= 16 else None
                    nd['words'] = list(struct.unpack_from('<%dH' % ((len(body) - 16) // 2), body, 16)) if len(body) > 16 else []
                pending_nodes.append(nd)
                G['nodes'].append(nd)
            elif cid == 2:
                mat, refj, vbi, topo = struct.unpack_from('<4H', g, co + 0x40)
                bv, used, ic = struct.unpack_from('<3I', g, co + 0x48)
                bounds = struct.unpack_from('<14f', g, co + 8)
                idx = struct.unpack_from('<%dH' % ic, g, co + 0x68) if ic else ()
                mesh = {'material': mat, 'joint': refj, 'vertex_buffer': vbi, 'topology': topo, 'base_vertex': bv,
                        'used_vertices': used, 'index_count': ic, 'bounds': bounds, 'nodes_before': pending_nodes[-2:]}
                pending_nodes = []
                if vbi < len(vbs) and ic:
                    vb = vbs[vbi]
                    lay = LAYOUT.get(vb['format'])
                    if lay and lay[0] == vb['stride']:
                        stride, uvo, colo = lay
                        fn = 'g%d_m%d_j%d.obj' % (gi, len(G['meshes']), refj)
                        with open(os.path.join(d, fn), 'w') as f:
                            f.write('# effect %d group %d mesh %d joint %d material %d format %d topology %d\n' % (req, gi, len(G['meshes']), refj, mat, vb['format'], topo))
                            lo = min(idx)
                            hi = max(idx)
                            for v in range(lo, hi + 1):
                                o = vb['offset'] + stride * v
                                if o + stride > len(res):
                                    break
                                x, y, z = struct.unpack_from('<3f', res, o)
                                col = struct.unpack_from('<4B', res, o + colo) if colo is not None else (255, 255, 255, 255)
                                f.write('v %f %f %f %f %f %f\n' % (x, y, z, col[2] / 255, col[1] / 255, col[0] / 255))
                                if uvo is not None:
                                    u, vv = struct.unpack_from('<hh', res, o + uvo)
                                    f.write('vt %f %f\n' % (u / 4096, 1 - vv / 4096))
                            tris = []
                            if topo == 5:
                                tris = [idx[i:i + 3] for i in range(0, ic - 2, 3)]
                            else:
                                for i in range(ic - 2):
                                    a, b, c = idx[i:i + 3]
                                    if a != b and b != c and a != c:
                                        tris.append((a, b, c) if i % 2 == 0 else (b, a, c))
                            for a, b, c in tris:
                                a, b, c = a - lo + 1, b - lo + 1, c - lo + 1
                                f.write('f %d/%d %d/%d %d/%d\n' % (a, a, b, b, c, c) if uvo is not None else 'f %d %d %d\n' % (a, b, c))
                        mesh['file'] = fn
                        mesh['triangles'] = len(tris)
                    else:
                        mesh['file'] = None
                        mesh['note'] = 'vertex format %d stride %d not exported' % (vb['format'], vb['stride'])
                G['meshes'].append(mesh)
        json.dump(G['joints'], open(os.path.join(d, 'skeleton_g%d.json' % gi), 'w'), indent=1)
        json.dump(G['nodes'], open(os.path.join(d, 'nodes_g%d.json' % gi), 'w'), indent=1)
        summary['groups'].append(G)
    e = ep.parse_eft(tail)
    summary['eft'] = {'layers': [l['kind'] for l in e['layers']], 'tracks': [(t['enable'], t['a'], t['b']) for t in e['tracks']]}
    json.dump(summary, open(os.path.join(d, 'summary.json'), 'w'), indent=1)
    return summary


def main(out, lo=0, hi=99999):
    os.makedirs(out, exist_ok=True)
    _, ei = ep.read_index(open(os.path.join(E, 'e.i'), 'rb').read())
    packs = {p: open(os.path.join(E, 'a%d.e' % p), 'rb').read() for p in range(8)}
    fmts = collections.Counter()
    results = {}
    for req, pack, off, span in ei:
        if not lo <= req <= hi:
            continue
        s = export(req, pack, off, span, packs, out)
        results[req] = s
        for G in s.get('groups', []):
            for vb in G['vertex_buffers']:
                fmts[(vb['format'], vb['stride'])] += 1
        print(req, 'ok' if 'error' not in s else s['error'], flush=True)
    with open(os.path.join(out, 'vertex_formats.csv'), 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['format', 'stride', 'buffers'])
        for (fm, st), n in sorted(fmts.items()):
            w.writerow([fm, st, n])
    # skills -> effects
    rows = list(csv.DictReader(open(os.environ.get('PD_SKILL_CATALOG', 'skill_catalog.csv'), encoding='utf-8')))
    with open(os.path.join(out, 'skills_effects_index.csv'), 'w', newline='', encoding='utf-8') as f:
        w = csv.writer(f)
        w.writerow(['record', 'name', 'school', 'effect_request', 'folder', 'groups', 'meshes_exported', 'textures', 'joints'])
        for r in rows:
            q = int(r['effect_request_resolved'] or 0)
            s = results.get(q)
            if s and 'error' not in s:
                w.writerow([r['record'], r['name'], r['school'], q, 'eff_%04d' % q, len(s['groups']),
                            sum(1 for G in s['groups'] for mm in G['meshes'] if mm.get('file')),
                            len(s['textures']), sum(len(G['joints']) for G in s['groups'])])
            elif q:
                w.writerow([r['record'], r['name'], r['school'], q, '', '', '', '', ''])
    json.dump({k: v for k, v in results.items() if 'error' in v}, open(os.path.join(out, 'errors.json'), 'w'), indent=1)
    print('done', len(results), 'effects;', sum(1 for v in results.values() if 'error' in v), 'errors')


if __name__ == '__main__':
    main(sys.argv[1], *(int(x) for x in sys.argv[2:4]))
