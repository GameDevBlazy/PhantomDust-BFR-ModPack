"""Edgar's Dream Forge: the Forge canvas stage (owner 2026-10-24: "the whole forge mode built inside of phantom dust";
earlier: "use the Phantom Dust assets as objects for the Forge mode"). Independent research tool; read-only on all
inputs; writes a fixture for Map Slot 8 (stage 20, the Workshop slot, tested in game as MAP-SLOTS-03 / EDGAR-WORKSHOP).

Like a forge-style map editor, the canvas is an open build floor whose objects all start in storage:
- base = the Workshop (build_workshop.build: 60 m grid floor at y 200, 3 m wall, Dawn City sky, 4 starts + capsules);
- objects = every placeable Phantom Dust object of st01-st08 (the in-game-tested museum v2 selection, IGT-027:
  classes 3/x and 4/x except 4/16 lifts left out, and 2/58 FallCrm, the museum crash cause), textures at -2 mips;
- each object gets a few copies (a per-item limit): lifts 4, rails / cones / fences 3, pieces wider than
  20 m 1, others 2, all placed in storage UNDER the floor (tops at y 180; layers go deeper when a layer is full);
- Map\\st20.forge tells CwoLoader 2.0 the storage line and the menu: categories and display names from the
  source stages, one `object <stage-file name>|<display name>` line per object.
The catalog row of Map Slot 8 (row 29) is renamed "Edgar's Dream Forge".
Usage: python -B -I build_forge_canvas.py OUT_DIR [--max-rows N]
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
import build_museum_st32 as mu  # noqa: E402
import build_st32_objects as bo  # noqa: E402
import build_workshop as ws  # noqa: E402
import pd_common as pc  # noqa: E402
import pd_paths  # noqa: E402
import stage_sections as ss  # noqa: E402

NAME = "Edgar's Dream Stage Creator"          # owner 2026-10-24 (was Edgar's Dream Forge)
STORE_TOP = ws.FLOOR_Y - 20.0          # storage tops sit 1 m under the floor slab (y 190..200)
STORAGE_BELOW = ws.FLOOR_Y - 15.0      # CwoLoader: a stage-file y below this = in storage
HALF = ws.TILES * ws.TILE / 2 - 10.0   # storage stays inside the floor's footprint
MARGIN = 512 * 1024
PD_STAGES = ['st0%d' % i for i in range(1, 9)]
STAGE_TAG = {'st01': "Edgar's", 'st02': 'Highway', 'st03': 'Palace', 'st04': 'Panorama', 'st05': 'Dawn City',
             'st06': 'Refinery', 'st07': 'Lane', 'st08': 'Sein'}
MAX_PLACEMENTS = 360                   # stock stages carry up to 269 placement rows (st06)


def sha(b):
    return hashlib.sha256(b).hexdigest()


def bank():
    names = {}
    for r in csv.DictReader(open(ws.BANK, encoding='utf-8')):
        if r['game'] == 'Phantom Dust' and r['pd_kind'] == 'placed object':
            names.setdefault(r['original_name'].lower().rstrip('_'), (r['category'], r['english_name'], r['pd_category']))
    return names


def copies_for(cls, size):
    if tuple(cls) == (4, 16):
        return 4
    if max(size[0], size[2]) > 200.0:
        return 1
    return 2


def limit_copies(info):
    """Per-item limits, then the biggest pieces drop to one copy until the canvas has at most
    MAX_PLACEMENTS rows."""
    for it in info:
        mn, mx = it['bounds']
        it['copies'] = copies_for(it['class'], (mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2]))
    by_size = sorted(range(len(info)), key=lambda i: -max(info[i]['bounds'][1][0] - info[i]['bounds'][0][0],
                                                             info[i]['bounds'][1][2] - info[i]['bounds'][0][2]))
    for i in by_size:
        if sum(it['copies'] for it in info) <= MAX_PLACEMENTS:
            break
        if info[i]['copies'] > 1 and tuple(info[i]['class']) != (4, 16):
            info[i]['copies'] = 1


def pd_objects(alr, drop_mips):
    """The museum v2 selection (build_museum_st32.build loop), transplanted into this canvas' ALR."""
    objects, info = [], []
    known = bank()
    for st in PD_STAGES:
        dat = open(os.path.join(mu.MAP, st + '.dat'), 'rb').read()
        donor = open(os.path.join(mu.MAP, st + '.alr'), 'rb').read()
        D = at.parse(donor)
        r = ss.decode_stage(dat)
        rows = mu.first_rows(dat)
        chosen = []
        for k, name in enumerate(r['names']):
            if k not in rows:
                continue
            cls = struct.unpack_from('<BB', rows[k], 4)
            if cls[0] == 3 or (cls[0] == 4 and cls != (4, 16)) or tuple(cls) == (2, 58):
                continue
            chosen.append(k)
        groups = sorted({g for k in chosen for g in r['ob00'][k][:3] if g is not None and g >= 0
                         and g < len(D['slot']) and D['slot'][g] is not None})
        gmap = {}
        if groups:
            alr, rep = at.transplant(alr, donor, groups, tex_drop_mips=drop_mips)
            gmap = dict(zip(groups, rep['new_group_indices']))
        for k in chosen:
            parts = bo.object_parts(dat, k)
            parts['row'] = [g if (w > 2 or g < 0 or g in gmap) else -1 for w, g in enumerate(parts['row'])]
            b = None
            for w in (0, 1, 2):
                g = r['ob00'][k][w]
                if g is not None and g >= 0 and g in gmap:
                    b = mu.model_bounds(D, g)
                    if b:
                        break
            if b is None and 6 in parts['oc']:
                b = mu.collision_bounds(parts['oc'][6])
            if b is None:
                b = ([-20.0, 0.0, -20.0], [20.0, 20.0, 20.0])
            cls = list(struct.unpack_from('<BB', rows[k], 4))
            cat, eng, kind = known.get(parts['name'].lower().rstrip('_'), ('Other', parts['name'].rstrip('_'), ''))
            eng = '%s: %s' % (STAGE_TAG[st], eng)
            if cls == [4, 16]:
                cat, eng = 'Gravity lifts', 'Lift %s (power %d)' % (STAGE_TAG[st], rows[k][6])
            objects.append((parts, gmap))
            info.append({'from': '%s object %d' % (st, k), 'name': parts['name'], 'display': eng, 'category': cat,
                         'kind': kind, 'class': cls, 'bounds': b, 'template': rows[k]})
    return alr, objects, info


def storage_layout(info):
    """Every copy of every object, packed in rows inside the floor footprint, tops at STORE_TOP; a full layer
    starts a deeper one."""
    out = []
    limit_copies(info)
    x, z, row_d, top, layer_h = -HALF, -HALF, 0.0, STORE_TOP, 0.0
    for i, it in enumerate(info):
        mn, mx = it['bounds']
        w, d, h = max(mx[0] - mn[0], 10.0), max(mx[2] - mn[2], 10.0), max(mx[1] - mn[1], 1.0)
        for c in range(it['copies']):
            if x + w > HALF:
                x, z, row_d = -HALF, z + row_d + 10.0, 0.0
            if z + d > HALF:
                x, z, row_d, top, layer_h = -HALF, -HALF, 0.0, top - layer_h - 20.0, 0.0
            px, pz = x - mn[0], z - mn[2]
            py = top - mx[1]
            out.append((i, (px, py, pz)))
            x += w + 10.0
            row_d, layer_h = max(row_d, d), max(layer_h, h)
    return out


def build(out_dir, drop_mips=2):
    tmp = os.path.join(out_dir, '_workshop_base')
    if os.path.exists(tmp):
        shutil.rmtree(tmp)
    ws.build(tmp)                                           # the in-game-tested Workshop floor, no objects
    dat = open(os.path.join(tmp, 'Map', 'st20.dat'), 'rb').read()
    alr = open(os.path.join(tmp, 'Map', 'st20.alr'), 'rb').read()
    base_rep = json.load(open(os.path.join(tmp, 'build_report.json')))
    alr, objects, info = pd_objects(alr, drop_mips)
    if len(alr) > mu.BUDGET:
        raise ValueError('canvas ALR %d over budget %d with the Phantom Dust objects alone' % (len(alr), mu.BUDGET))
    pd_alr = len(alr)
    skipped = []
    # long lists (Structure, Railings) are split by source stage so each menu page stays short
    counts = {}
    for it in info:
        counts[it['category']] = counts.get(it['category'], 0) + 1
    for it in info:
        if counts[it['category']] > 30:
            it['category'] = '%s %s' % (it['category'], STAGE_TAG[it['from'][:4]])
    places = storage_layout(info)
    placements = [(i, bo.placement_row(info[i]['template'], i, pos, (0.0, 0.0, 0.0))) for i, pos in places]
    dat = bo.build(dat, objects, placements, with_f8=False)
    for i, pos in places:
        assert pos[1] + info[i]['bounds'][1][1] <= STORE_TOP + 0.01 and pos[1] < STORAGE_BELOW
    # descriptor for the loader
    lines = ['# Edgar\'s Dream Forge canvas (build_forge_canvas.py): every object starts in storage under the floor',
             'storage_below %.1f' % STORAGE_BELOW, 'floor_y %.1f' % ws.FLOOR_Y]
    cats = []
    for it in info:
        if it['category'] not in cats:
            cats.append(it['category'])
    for c in cats:
        lines.append('category %s' % c[:31])
        seen = set()
        for it in info:
            if it['category'] == c and it['name'].lower() not in seen:
                seen.add(it['name'].lower())
                mn, mx = it['bounds']                       # object-space box: CwoLoader 2.2 magnets
                lines.append('object %s|%s|box %.1f %.1f %.1f %.1f %.1f %.1f' % (it['name'], it['display'][:28], mn[0], mn[1], mn[2],
                                                                               mx[0], mx[1], mx[2]))
    forge_txt = '\n'.join(lines) + '\n'
    # catalog: rename Map Slot 8
    cat_path = os.path.join(pd_paths.DATA, 'setup', 'stageset', 'stageset_en.dat')
    cat = open(cat_path, 'rb').read()
    _, _, _, rows = pc.catalog_rows(cat)
    row = rows[ws.CAT_ROW]
    assert row['stage'] == ws.STAGE, row
    new_cat = pc.catalog_set_row(cat, ws.CAT_ROW, NAME, row['family'], row['internal'], row['stage'], row['variants'], row['flag'])
    assert pc.catalog_rows(new_cat)[3][ws.CAT_ROW]['display'] == NAME
    files = {'Map/st20.dat': dat, 'Map/st20.alr': alr, 'Map/st20.forge': forge_txt.encode('latin-1', 'replace'),
             'setup/stageset/stageset_en.dat': new_cat}
    man = {'test': NAME + ': Forge canvas, every object in storage (Map Slot 8, stage 20)', 'files': {}}
    for rel, data in files.items():
        os.makedirs(os.path.dirname(os.path.join(out_dir, *rel.split('/'))), exist_ok=True)
        open(os.path.join(out_dir, *rel.split('/')), 'wb').write(data)
        live = os.path.join(pd_paths.DATA, *rel.split('/'))
        man['files'][rel] = {'action': 'replace' if os.path.exists(live) else 'add',
                             'base_sha256': sha(open(live, 'rb').read()) if os.path.exists(live) else None, 'new_sha256': sha(data)}
    json.dump(man, open(os.path.join(out_dir, 'manifest.json'), 'w'), indent=1)
    shutil.rmtree(tmp)
    for it in info:
        del it['template']
    rep = {'name': NAME, 'stage': ws.STAGE, 'objects': len(info), 'placements': len(placements),
           'pd_objects': len(info),
           'categories': cats,
           'alr_bytes': len(alr), 'alr_after_pd': pd_alr, 'alr_budget': mu.BUDGET, 'dat_bytes': len(dat),
           'storage_below': STORAGE_BELOW, 'deepest_storage_y': min(p[1] for _, p in places),
           'spawns': base_rep['spawns'], 'skipped': skipped, 'files': {k: v['new_sha256'][:8] for k, v in man['files'].items()},
           'catalogue': info}
    json.dump(rep, open(os.path.join(out_dir, 'build_report.json'), 'w'), indent=1)
    return rep


if __name__ == '__main__':
    if '--max-rows' in sys.argv:
        MAX_PLACEMENTS = int(sys.argv[sys.argv.index('--max-rows') + 1])
    rep = build(sys.argv[1])
    print(json.dumps({k: v for k, v in rep.items() if k != 'catalogue'}, indent=1))
