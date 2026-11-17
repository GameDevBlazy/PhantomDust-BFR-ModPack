"""Stage Creator template stages (owner 2026-10-24: "We need those unbreakable pieces ... the template stage just has
to be empty ... one floor"; "a blank stage ... called skill test stage"). Independent research tool; read-only on all
inputs; writes Map Slot 7 list files (Map\\<file>.alr/.dat/.forge + an mx_list.txt row each).

- base: the in-game-tested Workshop floor (build_workshop.build) with walls=False - one open grid floor at y 200,
  4 starts + capsules; the sky dome (ALR group 2) of the source stage, so each template carries its stage's sky;
- pieces: the source stage's world model (ALR group 0) cut into pieces by pd_world_pieces (islands, details joined
  to what they touch, wide floors cut into 40 m tiles). Each piece becomes one ALR group (the world group's own
  materials, skeleton chunk and vertex formats; vertices copied byte for byte with the position moved so the pivot is
  the piece's bottom centre; triangle lists) and one HARD object (template st03_bridgeX_, class 2/4,
  pd_common.template) whose OC00 is the world collision that belongs to it (nearest render vertex, pd_world_pieces);
- invisible barriers: world collision that has no render triangle near it becomes collision-only objects (no
  model, as the stock lifts have none);
- every piece starts in storage under the floor; Map\\<file>.forge lists them for CwoLoader with their object box,
  and <file>_home.csv keeps each piece's original place in its stage (for "build the original stage" later).
Usage: python -B -I build_pieces_canvas.py OUT_DIR st02 [st03 ...] [--blank] [--drop-mips N]
"""
import csv
import hashlib
import json
import os
import shutil
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import alr_transplant as at  # noqa: E402
import build_forge_canvas as fc  # noqa: E402
import build_museum_st32 as mu  # noqa: E402
import build_st32_objects as bo  # noqa: E402
import build_workshop as ws  # noqa: E402
import pd_common as pc  # noqa: E402
import pd_stage_dat as sd  # noqa: E402
import pd_world_pieces as wp  # noqa: E402
import stage_sections as ss  # noqa: E402

STAGE_NAME = {'st01': "Edgar's Lab", 'st02': 'Highway', 'st03': 'Lost Palace', 'st04': 'Panorama', 'st05': 'Dawn City',
              'st06': 'Refinery', 'st07': 'Twilight Lane', 'st08': 'Sein', 'st09': 'Underground'}
# Twilight Lane templates crashed in the game's draw loop with Twilight Lane's own built-in sky (owner tests 6, 2026-10-24,
# at 9.4 MB and again at 3.7 MB): its templates use Dawn City's sky, which loads fine as a built-in sky
SKY_FOR = {'st07': 'st05'}
SKY_STAGES = ['st05', 'st01', 'st02', 'st03', 'st04', 'st06', 'st07', 'st08']
TILES = 30                       # 180 m x 180 m floor: big pieces stay inside the footprint in storage
MAX_VERTS = 60000
# owner test 6 (2026-10-24): Twilight Lane A (9.4 MB) crashed in the draw loop right after loading; every template that
# worked was 7.2 MB or less, so templates stay under 7.5 MB (bigger stages are cut into more slices)
TEMPLATE_MAX = int(7.5 * 1024 * 1024)


def sha(b):
    return hashlib.sha256(b).hexdigest()


def piece_group(R, g0, descs, tris, ids, pivot):
    """One ALR group for the triangles `ids` of world group bytes g0; vertex data appended to R['resource']."""
    chunks = {cid: bytes(g0[co:co + sp]) for cid, co, sp in at.walk(g0) if cid in (0x01, 0x03, 0x0D)}
    # one joint (owner test 6, 2026-10-24): every Twilight Lane template crashed in the game's process draw loop right
    # after loading; its world skeleton has 403 joints (Highway 6, Sein 65, Lost Palace 145 - those work), copied into
    # every piece. The piece meshes all hang on joint 0 (+0x42 = 0 below), so a piece gets joint 0 only:
    # chunk 0x03 = {id, span, u32 count, count x 64-byte joints, 4 bytes} (sizes check out: 6 -> 400, 145 -> 9296, 403 -> 25808)
    sk = chunks[0x03]
    assert struct.unpack_from('<I', sk, 8)[0] * 64 + 16 == len(sk), 'skeleton chunk layout'
    chunks[0x03] = struct.pack('<III', 0x03, 12 + 64 + 4, 1) + sk[12:12 + 64] + bytes(4)
    mesh_hdr = next(bytes(g0[co:co + 0x68]) for cid, co, sp in at.walk(g0) if cid == 0x02)
    mat_chunk = chunks[0x01]
    mat_count, mat_flags = struct.unpack_from('<HH', mat_chunk, 8)
    used_mats = sorted({tris[t][1] for t in ids})
    mat_map = {m: k for k, m in enumerate(used_mats)}
    new_mats = b''.join(mat_chunk[12 + 0x4C * m:12 + 0x4C * (m + 1)] for m in used_mats)
    # vertices: per source descriptor, new buffers of at most MAX_VERTS
    vmap, new_descs, by_key = {}, [], {}
    for t in ids:
        d, m, tri = tris[t]
        out = []
        for i in tri:
            key = (d, i)
            if key not in vmap:
                cur = by_key.get(d)
                if cur is None or len(new_descs[cur]['verts']) >= MAX_VERTS:
                    new_descs.append({'src': d, 'verts': []})
                    cur = by_key[d] = len(new_descs) - 1
                vmap[key] = (cur, len(new_descs[cur]['verts']))
                new_descs[cur]['verts'].append(i)
            out.append(vmap[key])
        if len({o[0] for o in out}) != 1:            # a triangle split across buffers: copy its vertices again
            cur = by_key[d]
            out = []
            for i in tri:
                out.append((cur, len(new_descs[cur]['verts'])))
                new_descs[cur]['verts'].append(i)
        by_mesh = (mat_map[m], out[0][0])
        vmap.setdefault(('mesh', by_mesh), []).extend(o[1] for o in out)
    desc_entries, pts_of = [], []
    for nd in new_descs:
        src = descs[nd['src']]
        stride = src['stride']
        buf = bytearray()
        pts = []
        for i in nd['verts']:
            v = bytearray(src['raw'][i * stride:(i + 1) * stride])
            p = src['pos'][i]
            q = (p[0] - pivot[0], p[1] - pivot[1], p[2] - pivot[2])
            struct.pack_into('<3f', v, 0, *q)
            buf += v
            pts.append(q)
        pos = at.align(len(R['resource']), 0x80)
        R['resource'] += b'\0' * (pos - len(R['resource'])) + buf
        e = bytearray(src['entry'])
        struct.pack_into('<I', e, 8, len(nd['verts']))
        struct.pack_into('<I', e, 0x14, pos)
        desc_entries.append(bytes(e))
        pts_of.append(pts)
    meshes = []
    for key in sorted(k for k in vmap if isinstance(k, tuple) and k[0] == 'mesh'):
        mat, di = key[1]
        idx = vmap[key]
        lo, hi = min(idx), max(idx)
        hdr = bytearray(mesh_hdr)
        body = struct.pack('<%dH' % len(idx), *idx)
        span = at.align(0x68 + len(body), 4)
        struct.pack_into('<II', hdr, 0, 0x02, span)
        hdr[0x08:0x40] = pc.bounds([pts_of[di][i] for i in idx])
        struct.pack_into('<4H3I', hdr, 0x40, mat, 0, di, 5, lo, hi - lo + 1, len(idx))
        hdr[0x54:0x68] = b'\0' * 20
        meshes.append(bytes(hdr) + body + b'\0' * (span - 0x68 - len(body)))
    g = (struct.pack('<IIHH', 0x01, 12 + len(new_mats), len(used_mats), mat_flags) + new_mats + chunks[0x03] +
         struct.pack('<III', 0x16, 12 + 28 * len(desc_entries), len(desc_entries)) + b''.join(desc_entries) +
         b''.join(meshes) + chunks[0x0D] + struct.pack('<II', 0, 8))
    return bytearray(g), sum(len(x) for x in pts_of)


def stage_pieces(stage):
    """[(kind, triangles of the piece (render ids or None), world box, collision triangles)] for one stage."""
    dat = open(os.path.join(wp.MAP, stage + '.dat'), 'rb').read()
    alr = open(os.path.join(wp.MAP, stage + '.alr'), 'rb').read()
    R = at.parse(alr)
    descs, tris, _ = wp.group_triangles(R, 0)
    pieces = wp.merge_small(descs, tris, wp.split(descs, tris))
    pts = [[p for t in lst for p in wp.tri_points(descs, tris[t])] for lst in pieces]
    col, _c = wp.collision_triangles(dat)
    owner = wp.assign_collision(col, pts)
    cols = [[] for _ in pieces]
    barrier = []
    for tri, o in zip(col, owner):
        (barrier if o is None else cols[o]).append(tri)
    out = [('piece', lst, wp._box(p), c) for lst, p, c in zip(pieces, pts, cols)]
    # barriers: joined by touching boxes
    groups = []
    for tri in barrier:
        b = wp._box(list(tri))
        for g in groups:
            if wp._gap(g[0], b) <= wp.TOUCH:
                g[0] = wp._join(g[0], b)
                g[1].append(tri)
                break
        else:
            groups.append([b, [tri]])
    out += [('barrier', None, g[0], g[1]) for g in groups]
    return R, descs, tris, out


def build(out_dir, stage, drop_mips=1, blank=False, file=None, select=None):
    """select = (k, n): only the k-th of n west-to-east slices of the pieces (stages too big for one template)."""
    tag = '' if not select else 'abcdefgh'[select[0]]
    file = file or ('pcblank' if blank else 'pc' + stage[2:] + tag)
    name = 'Skill Test Stage' if blank else '%s pieces%s' % (STAGE_NAME.get(stage, stage), ' ' + tag.upper() if tag else '')
    tmp = os.path.join(out_dir, '_base_' + file)
    if os.path.exists(tmp):
        shutil.rmtree(tmp)
    ws.TILES = TILES
    ws.build(tmp, walls=False, sky=None if blank else SKY_FOR.get(stage, stage))
    dat = open(os.path.join(tmp, 'Map', 'st20.dat'), 'rb').read()
    alr = open(os.path.join(tmp, 'Map', 'st20.alr'), 'rb').read()
    shutil.rmtree(tmp)
    info, objects, homes = [], [], []
    rep = {'file': file, 'name': name, 'source': None if blank else stage}
    if not blank:
        R, descs, tris, pieces = stage_pieces(stage)     # fresh each time: the container is extended below
        if select:
            order = sorted(range(len(pieces)), key=lambda i: (pieces[i][2][0][0] + pieces[i][2][1][0]) / 2)
            n = len(order)
            keep = set(order[n * select[0] // select[1]:n * (select[0] + 1) // select[1]])
            pieces = [p for i, p in enumerate(pieces) if i in keep]
        g0 = R['groups'][R['slot'][0]]
        R['resource_dirty'] = True
        new_groups = []
        for k, (kind, ids, box, col) in enumerate(pieces):
            if kind != 'piece':
                new_groups.append(None)
                continue
            pivot = ((box[0][0] + box[1][0]) / 2, box[0][1], (box[0][2] + box[1][2]) / 2)
            g, nv = piece_group(R, g0, descs, tris, ids, pivot)
            R['groups'].append(g)
            R['slot'].append(len(R['groups']) - 1)
            R['raw_offsets'].append(0)
            new_groups.append(len(R['slot']) - 1)
        donor = at.build(R, keep_resource_offset=False)
        want = [g for g in new_groups if g is not None]
        alr, trep = at.transplant(alr, donor, want, tex_drop_mips=drop_mips)
        gmap = dict(zip(want, trep['new_group_indices']))
        tparts, trow = pc.template()
        oc_t = sd.parse_collision(tparts['oc'][6], 0)
        pc.KIND, pc.FLAGS, pc.POOL_KIND = oc_t['groups'][0]['kind'], oc_t['groups'][0]['flags'], oc_t['pool_hdr']['kind']
        n_piece = n_barrier = 0
        for k, (kind, ids, box, col) in enumerate(pieces):
            pivot = ((box[0][0] + box[1][0]) / 2, box[0][1], (box[0][2] + box[1][2]) / 2)
            parts = {kk: (dict(v) if isinstance(v, dict) else (list(v) if isinstance(v, list) else v)) for kk, v in tparts.items()}
            loc = [tuple((p[0] - pivot[0], p[1] - pivot[1], p[2] - pivot[2]) for p in tri) for tri in col]
            if loc:
                obj, _dropped = pc.build_object(loc)
                parts['oc'] = {6: sd.encode_collision(obj, keep_stored=False)} if obj['groups'] else {}
            else:
                parts['oc'] = {}
            if kind == 'piece':
                n_piece += 1
                g = gmap[new_groups[k]]
                parts['row'] = [g, -1, -1, -1, -1, -1] + list(parts['row'][6:])
                label, cat = '%s %d' % (STAGE_NAME.get(stage, stage), n_piece), '%s pieces' % STAGE_NAME.get(stage, stage)
            else:
                # model-less HARD objects are untested in game (the stock model-less object, the lift, is another
                # class): barriers are counted and kept out until a model or an in-game test covers them
                n_barrier += 1
                continue
                parts['row'] = [-1, -1, -1, -1, -1, -1] + list(parts['row'][6:])
                label, cat = 'Invisible wall %d' % n_barrier, 'Invisible walls'
            parts.update(name='%s_%s%03d' % (file, 'p' if kind == 'piece' else 'w', n_piece if kind == 'piece' else n_barrier),
                         oe={}, oa={}, f8=None)
            parts.pop('new', None)
            mn = [box[0][0] - pivot[0], 0.0, box[0][2] - pivot[2]]
            mx = [box[1][0] - pivot[0], box[1][1] - pivot[1], box[1][2] - pivot[2]]
            objects.append((parts, {g: g for g in parts['row'][:3] if g >= 0}))
            info.append({'name': parts['name'], 'display': label, 'category': cat, 'class': [2, 4], 'bounds': (mn, mx),
                         'template': trow, 'kind': kind, 'collision_triangles': len(col)})
            homes.append((parts['name'], label, pivot))
        rep.update(pieces=n_piece, barriers=n_barrier, slice=select, textures=len(trep['textures']), drop_mips=drop_mips)
    sky_rows = []
    if blank:
        # Skies (owner: "am I not able to choose the sky?"): every stage's sky dome (ALR group 2, copied whole with
        # alr_transplant, as the template's own sky is) as a model-only object; CwoLoader's "Sky" row shows one at
        # the centre and parks the rest far below. The built-in sky of this stage is empty (group 3 of st32).
        tparts, trow = pc.template()
        for i, st in enumerate(SKY_STAGES):
            donor = open(os.path.join(wp.MAP, st + '.alr'), 'rb').read()
            D = at.parse(donor)
            if len(D['slot']) < 3 or D['slot'][2] is None:
                continue
            alr2, trep = at.transplant(alr, donor, [2], tex_drop_mips=1)
            if len(alr2) > mu.BUDGET - 256 * 1024:
                break
            alr = alr2
            g = trep['new_group_indices'][0]
            parts = {kk: (dict(v) if isinstance(v, dict) else (list(v) if isinstance(v, list) else v)) for kk, v in tparts.items()}
            parts['oc'] = {}
            parts['row'] = [g, -1, -1, -1, -1, -1] + list(parts['row'][6:])
            parts.update(name='%s_sky%s' % (file, st[2:]), oe={}, oa={}, f8=None)
            parts.pop('new', None)
            objects.append((parts, {g: g}))
            info.append({'name': parts['name'], 'display': 'Sky: %s' % STAGE_NAME.get(st, st), 'category': 'Skies', 'class': [2, 4],
                         'bounds': ([-12000.0, -4000.0, -12000.0], [12000.0, 5000.0, 12000.0]), 'template': trow, 'kind': 'sky',
                         'collision_triangles': 0})
            sky_rows.append(len(info) - 1)
        rep['skies'] = [info[i]['display'] for i in sky_rows]
    # atmosphere (owner): the map light of the source stage (stage .dat header +0xD0 = maplight data, read by
    # FUN_140199b00 before maplight<N>.ssb; the base st32 has none there)
    light = 'st05' if blank else stage
    ldat = open(os.path.join(wp.MAP, light + '.dat'), 'rb').read()
    lsec = ss.section_bounds(ldat).get('0xd0')
    if lsec and lsec[1] > lsec[0] and struct.unpack_from('<i', dat, 0xD0)[0] == -1:
        dat = sd.append_and_repoint(dat, {0xD0: ldat[lsec[0]:lsec[1]]})
        rep['light'] = '%s (%d bytes)' % (light, lsec[1] - lsec[0])
    if len(alr) > TEMPLATE_MAX:
        raise ValueError('%s ALR %d over the template budget %d; try --drop-mips %d' % (file, len(alr), TEMPLATE_MAX, drop_mips + 1))
    fc.HALF = TILES * ws.TILE / 2 - 10.0
    fc.copies_for = lambda cls, size: 1                  # each piece is one of a kind
    plain = [it for it in info if it['kind'] != 'sky']
    places = fc.storage_layout(plain) if plain else []
    places = [(info.index(plain[i]), pos) for i, pos in places]
    for n, i in enumerate(sky_rows):                     # first sky shown (Dawn City), the others parked far below
        places.append((i, (0.0, 0.0 if info[i]['display'].endswith('Dawn City') else -200000.0, 0.0)))
    placements = [(i, bo.placement_row(info[i]['template'], i, pos, (0.0, 0.0, 0.0))) for i, pos in places]
    if objects:
        dat = bo.build(dat, objects, placements, with_f8=False)
    lines = ['# %s (build_pieces_canvas.py): every piece starts in storage under the floor' % name,
             'storage_below %.1f' % fc.STORAGE_BELOW, 'floor_y %.1f' % ws.FLOOR_Y]
    cats = []
    for it in info:
        if it['category'] not in cats:
            cats.append(it['category'])
    for c in cats:
        lines.append('category %s' % c[:31])
        for it in info:
            if it['category'] == c:
                mn, mx = it['bounds']
                lines.append('object %s|%s|box %.1f %.1f %.1f %.1f %.1f %.1f' % (it['name'], it['display'][:28], mn[0], mn[1], mn[2],
                                                                               mx[0], mx[1], mx[2]))
    files = {'Map/%s.dat' % file: dat, 'Map/%s.alr' % file: alr, 'Map/%s.forge' % file: ('\n'.join(lines) + '\n').encode('latin-1', 'replace')}
    os.makedirs(os.path.join(out_dir, 'Map'), exist_ok=True)
    for rel, data in files.items():
        open(os.path.join(out_dir, *rel.split('/')), 'wb').write(data)
    if homes:
        with open(os.path.join(out_dir, file + '_home.csv'), 'w', newline='', encoding='utf-8') as f:
            w = csv.writer(f)
            w.writerow(['object', 'display', 'x', 'y', 'z'])
            for n, label, p in homes:
                w.writerow([n, label, round(p[0], 3), round(p[1], 3), round(p[2], 3)])
    rep.update(alr_bytes=len(alr), dat_bytes=len(dat), placements=len(placements),
               files={k: sha(v)[:8] for k, v in files.items()}, list_row='%s\t%s\t1\t4' % (file, name))
    return rep


def _one(out, stage, d, sel):
    """One template in a fresh process: a failed over-budget attempt left state behind that broke the next attempt
    in the same process (seen on st07; each attempt alone is fine), so every attempt starts clean."""
    import subprocess
    cmd = [sys.executable, '-B', '-I', os.path.abspath(__file__), '--one', out, stage, str(d), '%d,%d' % sel if sel else '-']
    r = subprocess.run(cmd, capture_output=True, text=True)
    last = [l for l in r.stdout.splitlines() if l.startswith('{')]
    if r.returncode == 0 and last:
        return json.loads(last[-1])
    if 'budget' in r.stderr:
        return None
    raise RuntimeError('%s failed: %s' % (stage, r.stderr[-2000:]))


if __name__ == '__main__':
    args = [a for a in sys.argv[1:]]
    if args[0] == '--one':
        _, out, st, d, sel = args
        sel = tuple(int(x) for x in sel.split(',')) if sel != '-' else None
        print(json.dumps(build(out, None if st == 'blank' else st, drop_mips=int(d), blank=st == 'blank', select=sel)))
        sys.exit(0)
    out = args.pop(0)
    drop = 1
    if '--drop-mips' in args:
        i = args.index('--drop-mips')
        drop = int(args[i + 1])
        del args[i:i + 2]
    reps = []
    if '--blank' in args:
        args.remove('--blank')
        reps.append(_one(out, 'blank', 0, None))
    for st in args:
        done = False
        for n in (1, 2, 3, 4, 5):                       # a stage too big for one template is cut into n slices
            for d in range(drop, 3):                    # textures lose mip levels until it fits its budget
                got = [_one(out, st, d, None if n == 1 else (k, n)) for k in range(n)]
                if all(got):
                    reps += got
                    done = True
                    break
                print('%s: %d slice(s) at -%d mips over budget' % (st, n, d))
            if done:
                break
        if not done:
            raise SystemExit('%s does not fit in 5 slices' % st)
        for r in reps[-n:]:
            print(json.dumps(r))
    json.dump(reps, open(os.path.join(out, 'build_report.json'), 'w'), indent=1)
