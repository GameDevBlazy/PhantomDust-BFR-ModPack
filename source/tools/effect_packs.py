"""Effect pack reader and validator (read-only, Batch 020).

Effect/e.i (CWO-003 / Rebuild EFFECT_RESOURCE_SPEC): u16 word0, u16 count,
count x {u16 request, u16 pack, u32 offset, u32 span}. Each span inside
Effect/a<pack>.e is:
  [ALR container (Batch 012)] [padding] [0x800-byte tail]
The tail starts with a 412-byte EFT record (the same layout as the loose
Effect/eftNNNN.eft files):
  12 x 20-byte layer slots: +0 u32, +4 u32 kind (0, 27, 29), +8 16 bytes
  +0xF0 u32
  6 x 28-byte track blocks from +0xF4: +0 u32 enable/count, +4 i16 a, +6 i16 b,
       +8 i32[5] (-1 = none)
The rest of the tail is preserved as opaque bytes.

Usage: python -B -I effect_packs.py EFFECT_DIR OUT.json
"""
import collections
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import alr_chunks  # noqa: E402

TAIL = 0x800
EFT_SIZE = 412


def read_index(ei):
    word0, count = struct.unpack_from('<HH', ei, 0)
    return word0, [struct.unpack_from('<HHII', ei, 4 + 12 * k) for k in range(count)]


def parse_eft(b):
    if len(b) < EFT_SIZE:
        raise ValueError('EFT record needs 412 bytes')
    layers = [{'w0': struct.unpack_from('<I', b, 20 * k)[0], 'kind': struct.unpack_from('<I', b, 20 * k + 4)[0],
               'rest': b[20 * k + 8:20 * k + 20].hex()} for k in range(12)]
    tracks = []
    for k in range(6):
        o = 0xF4 + 28 * k
        en, a, bb = struct.unpack_from('<Ihh', b, o)
        tracks.append({'enable': en, 'a': a, 'b': bb, 'refs': list(struct.unpack_from('<5i', b, o + 8))})
    return {'layers': layers, 'word_f0': struct.unpack_from('<I', b, 0xF0)[0], 'tracks': tracks}


def eft_grammar_ok(e):
    """Checks the regularities seen in the 476 loose files: kinds in {0,27,29},
    layer padding zero, disabled tracks all zero, enabled tracks use -1 refs."""
    if any(l['kind'] not in (0, 27, 29) or int(l['rest'], 16) or l['w0'] for l in e['layers']):
        return False
    for t in e['tracks']:
        if t['enable'] == 0 and (t['a'] or t['b'] or any(t['refs'])):
            return False
    return True


def census(effect_dir):
    word0, recs = read_index(open(os.path.join(effect_dir, 'e.i'), 'rb').read())
    packs = {}
    rep = collections.Counter()
    kinds = collections.Counter()
    loose_mismatch = []
    for req, pack, off, span in recs:
        if pack not in packs:
            packs[pack] = open(os.path.join(effect_dir, 'a%d.e' % pack), 'rb').read()
        blob = packs[pack][off:off + span]
        rep['spans'] += 1
        rep['span_0x800_aligned'] += span % 0x800 == 0 and off % 0x800 == 0
        try:
            info = alr_chunks.read_container(blob)
            rep['alr_container'] += 1
            end = info['resource_offset'] + info['resource_span']
            rep['alr_fits_before_tail'] += end <= span - TAIL
            _, groups, errors = alr_chunks.analyse(blob)
            rep['alr_groups_clean'] += not errors
        except ValueError:
            rep['not_container'] += 1
        tail = blob[span - TAIL:]
        e = parse_eft(tail)
        rep['tail_eft_grammar_ok'] += eft_grammar_ok(e)
        for l in e['layers']:
            kinds[l['kind']] += 1
        lp = os.path.join(effect_dir, 'eft%04d.eft' % req)
        if os.path.exists(lp):
            rep['has_loose_eft'] += 1
            same = open(lp, 'rb').read() == tail[:EFT_SIZE]
            rep['loose_eft_equals_tail'] += same
            if not same and len(loose_mismatch) < 10:
                loose_mismatch.append(req)
    loose = [f for f in os.listdir(effect_dir) if f.endswith('.eft')]
    loose_ok = sum(eft_grammar_ok(parse_eft(open(os.path.join(effect_dir, f), 'rb').read())) for f in loose)
    return {'e_i_word0': word0, 'records': len(recs), 'checks': dict(rep), 'layer_kinds': dict(kinds),
            'loose_eft_files': len(loose), 'loose_eft_grammar_ok': loose_ok, 'loose_mismatch_examples': loose_mismatch}


if __name__ == '__main__':
    res = census(sys.argv[1])
    with open(sys.argv[2], 'w', encoding='utf-8') as fh:
        json.dump(res, fh, indent=1)
    print(json.dumps(res, indent=1))
