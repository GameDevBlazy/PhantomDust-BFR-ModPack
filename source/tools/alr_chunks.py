"""Read-only ALR container and group chunk walker (Batch 012).

Independently authored from measured facts:
  container: u32 0x11, u32 header span, u16 v0, u16 v1, u32 resource offset,
             u32 group count, u32 resource span, 8 bytes, i32[count] group offsets
             (offset <= 0 = absent; duplicates = aliases)
  group:     chunks {u32 id, u32 span incl. 8-byte header} ending with {0, 8}

Usage: python -B alr_chunks.py <root dir> <out.json>
"""
import collections
import json
import os
import struct
import sys


def read_container(b):
    if len(b) < 0x20:
        raise ValueError('too small')
    cid, hspan, v0, v1, roff, cnt, rspan = struct.unpack_from('<IIHHIII', b, 0)
    if cid != 0x11 or 0x20 + 4 * cnt > len(b):
        raise ValueError('not an ALR container')
    offs = list(struct.unpack_from('<%di' % cnt, b, 0x20))
    return {'header_span': hspan, 'version': (v0, v1), 'resource_offset': roff,
            'group_count': cnt, 'resource_span': rspan, 'offsets': offs}


def walk_group(b, o, limit):
    """Return (chunks, end) or raise. chunks = [(id, offset, span)]."""
    chunks, i = [], o
    while True:
        if i + 8 > limit:
            raise ValueError('group runs past limit at 0x%x' % i)
        cid, span = struct.unpack_from('<II', b, i)
        if cid == 0:
            if span != 8:
                raise ValueError('terminator span %d at 0x%x' % (span, i))
            return chunks, i + 8
        if span < 8 or i + span > limit:
            raise ValueError('chunk 0x%x span %d at 0x%x' % (cid, span, i))
        chunks.append((cid, i, span))
        i += span


def analyse(b):
    c = read_container(b)
    present = sorted({o for o in c['offsets'] if o > 0})
    data_end = min(c['resource_offset'] or len(b), len(b))
    groups, errors = {}, []
    for k, o in enumerate(present):
        limit = min(present[k + 1] if k + 1 < len(present) else data_end, len(b))
        try:
            chunks, end = walk_group(b, o, limit)
            groups[o] = {'chunks': chunks, 'end': end, 'slack': limit - end}
        except (ValueError, struct.error) as exc:
            errors.append(str(exc))
    return c, groups, errors


def main(root, out_path):
    files = []
    for dp, _, fs in os.walk(root):
        files += [os.path.join(dp, f) for f in fs if f.lower().endswith('.alr')]
    ids = collections.Counter()
    seqs = collections.Counter()
    slack = collections.Counter()
    summary = {'files': 0, 'not_container': 0, 'groups': 0, 'group_errors': 0, 'error_examples': []}
    for p in sorted(files):
        b = open(p, 'rb').read()
        try:
            c, groups, errors = analyse(b)
        except ValueError:
            summary['not_container'] += 1
            continue
        summary['files'] += 1
        summary['groups'] += len(groups)
        summary['group_errors'] += len(errors)
        if errors and len(summary['error_examples']) < 20:
            summary['error_examples'].append([os.path.relpath(p, root), errors[0]])
        for g in groups.values():
            seq = tuple(cid for cid, _, _ in g['chunks'])
            for cid in seq:
                ids[cid] += 1
            first = seq[0] if seq else None
            seqs[(first, tuple(sorted(set(seq))))] += 1
            slack[min(g['slack'], 4096) if g['slack'] < 4096 else '>=4096'] += 1
    summary['chunk_ids'] = {('0x%x' % k): v for k, v in sorted(ids.items())}
    summary['group_kinds'] = [[str(k[0]), ['0x%x' % x for x in k[1]], v] for k, v in seqs.most_common(30)]
    summary['slack_after_group'] = {str(k): v for k, v in sorted(slack.items(), key=lambda x: str(x[0]))[:40]}
    with open(out_path, 'w', encoding='utf-8') as fh:
        json.dump(summary, fh, indent=1)
    print(json.dumps({k: summary[k] for k in ('files', 'not_container', 'groups', 'group_errors')}))
    print('chunk ids', summary['chunk_ids'])
    for row in summary['group_kinds'][:15]:
        print(' ', row)
    print('errors', summary['error_examples'][:6])


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
