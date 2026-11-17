"""The Map Slot 7 stage packs as rows of the stage lists (owner 2026-10-31 "Build it"). Independently
authored; patches copies of the installed Menu/vsmenu_en.ssb (f927a79f) and Menu/livestagesel_en.ssb (0b407be6) with the
Script / Asm tools of build_lobby3.py (loader 5.43, cwo_stagelist.inl, native 2.8 ops 70-76).

Game facts (our reading, 25_Menu_Research/stage_view_2026-10-27 + the installed vsmenu listing, MODLOG 2026-10-31):
- the list builder (vsmenu 0x21dc / livestagesel 0x15cc): clears a 32-entry row table, makes the window, then for i in
  0..16: id = table17[i] (0x20ac / 0x149c); if 2.151(id, 56) and 2.151(id, 57): 2.123(2.151(id, 51), id + 100);
  rowtable[row] = id (0.17 at 0x22ec-0x22f8 / 0x16dc-0x16e8); row++. Locals: -1 i, -2 unused, -3 id, -4 row.
  The loop's exit jz (0x2280 / 0x1670) goes to the tail (0x2310 / 0x1700).
- 0x211c / 0x150c: row -> id (0.16 on the row table); 0x2134 / 0x1524: id -> row (a search over 32 entries).
- the picture (0x23dc / 0x17cc): push 261; push 48; call global-1 getter; pushret; pushaddr table; 0.16; pushret;
  ldl 0 (image slot); 2.82(4).
- the confirm: vsmenu assel_ta 0x6440: call 0x2050; pushret; push 1; 2.154(2) (menu var 1 = id); livestagesel astsel_ta
  0x18d8: call 0x1440; pushret; 4.576(1) (the session's stage).
Patches (both scripts): the row-table store -> op 72(row, id); the row -> id routine -> op 73; the id -> row routine ->
op 74; after the stock loop a second loop appends op 70() rows: text op 71(k), row id 200 + k (was 1100 + k), value 1000 + k; the
picture's table lookup -> op 76(slot, value); the confirm's value -> op 75(value) first.
Usage: python -B -I build_stage_rows.py IN_vsmenu IN_livestagesel OUT_vsmenu OUT_livestagesel
"""
import hashlib
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import build_lobby3 as bl  # noqa: E402
from build_lobby3 import Script, Asm, p16, nat, ldl, stl, ret, ADD, LT, PUSHRET, NOP, LOADER  # noqa: E402

VS_SHA, LS_SHA = 'f927a79f', '0b407be6'
OP_COUNT, OP_NAME, OP_STORE, OP_ROW2ID, OP_ID2ROW, OP_CONFIRM, OP_PIC = 70, 71, 72, 73, 74, 75, 76
CODE, ID_BASE = 1000, 130
# 2026-11-01 (second fix, after the in-game check of 200 + k): the scripts' own move routine (vsmenu 0x2374, livestagesel
# 0x1780) accepts the engine's A mode only while 100 <= mode < 356, and the stage list sets mode = item id + 100
# (FUN_14024b000), so item ids must stay below 256: 130 + k fits 126 packs (81 listed) and never meets 102..126.
# 2026-11-01 item ids 1100 + k -> 200 + k: the menu engine (FUN_140245f00, native 2.71's name builder, Ghidra batch_065)
# makes a list item's handler '<state>_S<id>' only for ids 100..611 (`if (0x1ff < id - 100) return`), so A on a pack row
# (id >= 1100) ran nothing (owner 2026-11-01: 'You can hear the selection button ... nothing happens'). 200 + k stays in
# range for 412 packs and never meets the stock ids 102..126.


def addl(i):                                                 # local i += pop
    return bl.w(0x0A, 1, i)


def jz_word(at, target):
    return bl.w(0x07, 2, (target - (at + 4)) // 4)


def jmp_word(at, target):
    return bl.w(0x07, 0, (target - (at + 4)) // 4)


def call_word(at, target, argc):
    return bl.w(0x03, argc, (target - (at + 4)) // 4)


def put(sc, at, word):
    sc.code[at:at + 4] = word


def patch(src, sha, A):
    """A: the script's addresses: builder loop exit jz, its target, the store site (4 words), the loop's row++ site,
    row2id routine, id2row routine, getter (global 1), picture routine, confirm site, confirm kind."""
    assert hashlib.sha256(src).hexdigest().startswith(sha), 'wrong input script (%s wanted)' % sha
    sc = Script(src)
    # --- checks of what we patch
    assert sc.word(A['exit_jz']) == jz_word(A['exit_jz'], A['tail']), 'loop exit'
    st = A['store']
    assert sc.word(st) == ldl(-3) and sc.word(st + 4) == ldl(-4) and sc.word(st + 12) == nat(0, 17, 3), 'row store'
    assert sc.word(st + 16) == p16(1) and sc.word(st + 20) == addl(-4), 'row++'
    r2 = A['row2id']
    assert sc.word(r2) == ldl(0) and sc.word(r2 + 8) == nat(0, 16, 2) and sc.word(r2 + 12) == PUSHRET and sc.word(r2 + 16) == ret(1, True), 'row -> id'
    i2 = A['id2row']
    assert sc.word(i2) == bl.alloc(1) and sc.word(i2 + 4) == p16(0) and sc.word(i2 + 12) == ldl(-1) and sc.word(i2 + 16) == p16(32), 'id -> row'
    pc = A['pic']
    assert sc.word(pc) == p16(261) and sc.word(pc + 4) == p16(48) and sc.word(pc + 8) == call_word(pc + 8, A['getter'], 0), 'picture'
    assert sc.word(pc + 12) == PUSHRET and sc.word(pc + 20) == nat(0, 16, 2) and sc.word(pc + 24) == PUSHRET and sc.word(pc + 28) == ldl(0), 'picture lookup'
    assert sc.word(pc + 32) == nat(2, 82, 4), 'picture draw'
    cf = A['confirm']
    assert sc.word(cf) == call_word(cf, A['getter'], 0) and sc.word(cf + 4) == PUSHRET, 'confirm getter'
    if A['confirm_kind'] == 'var':
        assert sc.word(cf + 8) == p16(1) and sc.word(cf + 12) == nat(2, 154, 2), 'confirm var'
    else:
        assert sc.word(cf + 8) == nat(4, 576, 1), 'confirm session'
    # --- the store: jmp newstore; newstore = op 72(row, id) -> local -2; jmp row++
    m = Asm(sc)
    m(ldl(-4), ldl(-3), LOADER(OP_STORE, [ldl(-3)]) if False else [p16(OP_STORE), nat(2, 8, 3), PUSHRET], stl(-2), ('jmp', st + 16))
    new_store = m.place()
    put(sc, st, jmp_word(st, new_store))
    # --- the pack loop after the stock loop: k in local -1, row in local -4
    m = Asm(sc)
    m(p16(0), stl(-1),
      ('label', 'loop'), ldl(-1), LOADER(OP_COUNT), LT, ('jz', 'done'),
      LOADER(OP_NAME, [ldl(-1)]), ldl(-1), p16(ID_BASE), ADD, nat(2, 123, 2),
      ldl(-4), ldl(-1), p16(CODE), ADD, p16(OP_STORE), nat(2, 8, 3), PUSHRET, stl(-2),
      p16(1), addl(-4), p16(1), addl(-1), ('jmp', 'loop'),
      ('label', 'done'), ('jmp', A['tail']))
    new_loop = m.place()
    put(sc, A['exit_jz'], jz_word(A['exit_jz'], new_loop))
    # --- row -> id: ldl 0; op 73; ret 1 (in place: 5 words of the original 6)
    for k, wd in enumerate([ldl(0), p16(OP_ROW2ID), nat(2, 8, 2), PUSHRET, ret(1, True)]):
        put(sc, r2 + 4 * k, wd)
    # --- id -> row: ldl 0; op 74; ret 1 (in place over the search's first words; the rest is unreachable)
    for k, wd in enumerate([ldl(0), p16(OP_ID2ROW), nat(2, 8, 2), PUSHRET, ret(1, True)]):
        put(sc, i2 + 4 * k, wd)
    # --- the picture: [261, 48, value] -> op 76(slot, value) -> sprite; back to ldl 0 / 2.82
    m = Asm(sc)
    m(ldl(0), p16(OP_PIC), nat(2, 8, 3), PUSHRET, ('jmp', pc + 28))
    new_pic = m.place()
    put(sc, pc + 16, jmp_word(pc + 16, new_pic))
    put(sc, pc + 20, NOP)
    put(sc, pc + 24, NOP)
    # --- the confirm: value -> op 75 -> the game's stage id
    m = Asm(sc)
    if A['confirm_kind'] == 'var':
        m(p16(OP_CONFIRM), nat(2, 8, 2), PUSHRET, p16(1), nat(2, 154, 2), ('jmp', cf + 16))
        new_cf = m.place()
        put(sc, cf + 8, jmp_word(cf + 8, new_cf))
        put(sc, cf + 12, NOP)
    else:
        m(p16(OP_CONFIRM), nat(2, 8, 2), PUSHRET, nat(4, 576, 1), ('jmp', cf + 12))
        new_cf = m.place()
        put(sc, cf + 8, jmp_word(cf + 8, new_cf))
    return sc.build(), {'start': sc.base, 'store': hex(new_store), 'loop': hex(new_loop), 'pic': hex(new_pic), 'confirm': hex(new_cf)}


VSMENU = dict(exit_jz=0x2280, tail=0x2310, store=0x22ec, row2id=0x211c, id2row=0x2134, getter=0x2050, pic=0x23dc,
              confirm=0x6440, confirm_kind='var')
LIVESTAGESEL = dict(exit_jz=0x1670, tail=0x1700, store=0x16dc, row2id=0x150c, id2row=0x1524, getter=0x1440, pic=0x17cc,
                    confirm=0x18d8, confirm_kind='session')

if __name__ == '__main__':
    vs, ls = open(sys.argv[1], 'rb').read(), open(sys.argv[2], 'rb').read()
    vs_out, r1 = patch(vs, VS_SHA, VSMENU)
    ls_out, r2 = patch(ls, LS_SHA, LIVESTAGESEL)
    n1 = bl.validate(vs_out, 'vsmenu', r1['start'], [])
    n2 = bl.validate(ls_out, 'livestagesel', r2['start'], [])
    open(sys.argv[3], 'wb').write(vs_out)
    open(sys.argv[4], 'wb').write(ls_out)
    print('vsmenu', hashlib.sha256(vs_out).hexdigest()[:8], r1, n1, 'instructions')
    print('livestagesel', hashlib.sha256(ls_out).hexdigest()[:8], r2, n2, 'instructions')
