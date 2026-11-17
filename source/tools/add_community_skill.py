"""Add a player's shared skill to the pack's community skills (owner 2026-11-14: "go ahead and work on A, D"; AGENTS.md
"Community skills and created-content safety online"). Independently authored.

A player sends a skill from FEEDBACK > Category: Skill to share. It reaches the owner's form (category Feedback) as
    SKILL SUBMISSION v1: <their number> ^ <base skill> ^ <name> ^ <description> ^ <options>
When the owner approves it, this tool checks it and gives it a fixed community number (600-639, a block neither the Skill
Creator nor the Skill Fuser hands out - loader 5.186.0), then appends it to the pack's CwoCommunitySkills.txt; the next signed
BFR update brings it to every player, and it may then be used online.

Checks (refused unless noted):
- the base skill and every skill it borrows from (fx= snd= anim= trail=) are original game skills 1-374, so the skill is
  the same on every PC (a player's own skill or a pack-only record would differ);
- no effect copies made outside the game (OLD=NEW swaps, pal=) - their files exist only on that player's PC;
  the colour settings hue= sat= val= cm= are fine (the loader builds those itself);
- name at most 18 characters (owner rule); options only of the kinds the loader reads; cost 0-10.
The submitter's gamertag is NOT written into the pack file; the private record goes to
15_Evidence_and_Logs/community_skills_log.txt.

Usage: python -B add_community_skill.py "SKILL SUBMISSION v1: ..." --by GAMERTAG [--name "Short Name"] [--dry-run]
"""
import argparse
import datetime
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
PACK_FILE = os.path.join(ROOT, 'packs', 'bfr_community', 'CwoCommunitySkills.txt')
PRIVATE_LOG = os.path.join(os.path.dirname(ROOT), '15_Evidence_and_Logs', 'community_skills_log.txt')
LO, HI = 600, 639
HEADER = ('# Community skills (BFR / MODS packs): players\' skills the pack\'s author approved. Numbers 600-639.\n'
          '# new id\tsource\tname\tdescription\toptions - written by tools/add_community_skill.py, read by the loader (5.186.0+)\n')
OPT_RE = [
    (re.compile(r'^school=([1-5])$'), None),
    (re.compile(r'^cost=(\d+)$'), (0, 10)),
    (re.compile(r'^damage=(\d+)$'), (0, 999)),
    (re.compile(r'^(fx|snd|anim|trail)=(\d+)$'), 'skill'),
    (re.compile(r'^voice=([1-7])$'), None),
    (re.compile(r'^hue=(\d+)$'), (1, 359)),
    (re.compile(r'^sat=(\d+)$'), (0, 300)),
    (re.compile(r'^val=(\d+)$'), (10, 300)),
    (re.compile(r'^fxm=([01])$'), None),
    (re.compile(r'^cm=([0-2])$'), None),
    (re.compile(r'^r([0-9a-f]+)=(-?\d+)$'), 'field'),
]


def parse(text):
    t = text.strip()
    m = re.match(r'^SKILL SUBMISSION v1:\s*(.*)$', t)
    if not m:
        raise ValueError('not a skill submission (it must start "SKILL SUBMISSION v1:")')
    parts = [p.strip() for p in m.group(1).split(' ^ ')]
    if len(parts) != 5:
        raise ValueError('expected 5 fields separated by " ^ ", got %d' % len(parts))
    their, src, name, desc, opts = parts
    return int(their), int(src), name, desc, opts.split()


def check(src, name, desc, opts):
    problems, notes = [], []
    if not 1 <= src <= 374:
        problems.append('base skill %d is not an original game skill (1-374)' % src)
    if len(name) > 18:
        problems.append('name "%s" is %d characters (18 at most - give --name)' % (name, len(name)))
    if any(c in name + desc for c in '\t^|'):
        problems.append('name or description holds a tab, ^ or |')
    for o in opts:
        if re.match(r'^\d+=\d+$', o) or o.startswith('pal='):
            problems.append('%s: an effect copy made on the player\'s PC (not in the pack)' % o)
            continue
        for rx, rule in OPT_RE:
            mm = rx.match(o)
            if not mm:
                continue
            if rule == 'skill':
                v = int(mm.group(2))
                if not 1 <= v <= 374:
                    problems.append('%s borrows from skill %d (only original skills 1-374)' % (o, v))
            elif rule == 'field':
                off = int(mm.group(1), 16)
                if not (0x0E <= off < 0x90 and off % 2 == 0):
                    problems.append('%s: record field 0x%x is not one the loader writes' % (o, off))
                else:
                    notes.append('record field 0x%x = %s' % (off, mm.group(2)))
            elif isinstance(rule, tuple):
                v = int(mm.group(1))
                if not rule[0] <= v <= rule[1]:
                    problems.append('%s out of range %d-%d' % (o, rule[0], rule[1]))
            break
        else:
            problems.append('%s: an option the loader does not read' % o)
    return problems, notes


def used_ids(path):
    ids = set()
    if os.path.exists(path):
        for line in open(path, encoding='utf-8'):
            if line.startswith('#') or not line.strip():
                continue
            ids.add(int(line.split('\t')[0]))
    return ids


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('submission')
    ap.add_argument('--by', required=True, help='the submitter\'s gamertag (private log only, never in the pack)')
    ap.add_argument('--name', help='a shorter name (18 characters at most)')
    ap.add_argument('--dry-run', action='store_true')
    ap.add_argument('--file', default=PACK_FILE)
    a = ap.parse_args()
    their, src, name, desc, opts = parse(a.submission)
    if a.name:
        name = a.name
    problems, notes = check(src, name, desc, opts)
    print('skill "%s" (their number %d), base skill %d' % (name, their, src))
    print('  description: %s' % desc)
    print('  options: %s' % (' '.join(opts) or '(none)'))
    for n in notes:
        print('  note: %s' % n)
    if problems:
        print('REFUSED:')
        for p in problems:
            print('  - ' + p)
        return 2
    ids = used_ids(a.file)
    free = [i for i in range(LO, HI + 1) if i not in ids]
    if not free:
        print('REFUSED: all community numbers %d-%d are used' % (LO, HI))
        return 2
    new_id = free[0]
    line = '%d\t%d\t%s\t%s\t%s\n' % (new_id, src, name, desc or '-', ' '.join(opts))
    print('community number %d:\n  %s' % (new_id, line.rstrip()))
    if a.dry_run:
        print('(dry run - nothing written)')
        return 0
    os.makedirs(os.path.dirname(a.file), exist_ok=True)
    if not os.path.exists(a.file):
        open(a.file, 'w', encoding='utf-8', newline='\n').write(HEADER)
    with open(a.file, 'a', encoding='utf-8', newline='\n') as f:
        f.write(line)
    os.makedirs(os.path.dirname(PRIVATE_LOG), exist_ok=True)
    with open(PRIVATE_LOG, 'a', encoding='utf-8') as f:
        f.write('%s\t%d\t%s\tfrom %s (their number %d)\n' % (datetime.datetime.now().strftime('%Y-%m-%d %H:%M'), new_id, name, a.by, their))
    print('written to %s - build the next BFR pack to ship it' % a.file)
    return 0


if __name__ == '__main__':
    sys.exit(main())
