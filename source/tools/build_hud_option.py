"""HUD Preset on the main menu's OPTIONS (owner 2026-11-03: "in the options on the main menu, it should have a section that
says HUD preset that you can select from with a visual of exactly where what the HUD preset looks like"; v2, owner: "You
must retain the theme of Phantom Dust ... you can use the font text and box and everything that's in the options").
Independently authored; patches a copy of Menu/option_en.ssb (clean 9d509e9f or the MODS copy) with build_lobby3.py's
Script / Asm. Game facts (our reading with ssb_annotate.py, scratchpad opt_ann.txt):
- the OPTIONS window (routine 0x42c, also what every sub-menu's Back runs) = 2.97(1, 1) window 1, 2.100(170, 256) height /
  width, 2.101(55, 48) y / x, 2.102 title, rows 2.104 - Controller .. $KEYBOARD - then `push16 10; call 0x178` (help window
  10: 2.97(3, n) 640 x 104 at y 374) and the help lines (2.104), the button bar `call 0x13c("8#Select ...")`, 2.152(0,
  "OPTIONS", 1), 2.107(1), 2.76("MAIN"): menu MAIN = exports mmain (enter: 2.70("aMAIN"), 2.117(1, 10)), dmain (leave:
  call 0x120, 2.89, 2.106(1/10/14)), amain_s<row> (A on a row), amain_tb (B);
- a sub-menu's side window (DETAILS' note: 2.97(2, 9), 2.118("DRAW_NOTE", 514) = its row-draw routine, called with
  (x, y, row, ...) - draw_note returns at once unless row 0, `ret 5`).
New: a 7th OPTIONS row "HUD Preset" (+ help line; export amain_s6 = the HUD PRESET menu): the game's own window 1 with the
7 presets as rows, help window 10 with a line per preset, the button bar, and side window 9 "PREVIEW" whose row-draw
routine draw_hudv tells the loader where to draw the highlighted preset's layout (2.8 op 104(x, y)); menu HUDP:
mhudp / dhudp as MAIN's, ahudp_s<k> = 2.8 op 103(k) (the loader uses preset k), ahudp_tb = back to OPTIONS (0x42c).
Usage: python -B build_hud_option.py IN OUT
"""
import hashlib
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import build_lobby3 as bl  # noqa: E402
from build_lobby3 import Script, Asm, p16, nat, ret, S, ldl, alloc  # noqa: E402
from build_modlist_menu import find, pool_bytes  # noqa: E402

OP_SET, OP_DRAW = 103, 104
PRESETS = [("Game's own", "The game's own battle HUD."),
           ('Split Look', 'Life bar on top, black bars like split screen.'),
           ('Wide Bars', 'Black bars top and bottom, skills on the left.'),
           ('Centre Stage', 'Capsules and skills in the middle.'),
           ('Low Profile', 'Life bar and capsules low on the screen.'),
           ('Mirror', 'Everything on the other side.'),
           ('Minimal', 'Only your life bar and skills.')]


def patch(src):
    sc = Script(src)
    names = {bl.ssb_names.decode(a, b) for a, b, _ in sc.exports}
    assert 'amain_s6' not in names and 'mhudp' not in names, 'already patched'
    kb = find(sc, b'$KEYBOARD', nat(2, 104, 1), p16(10))
    hint = find(sc, b'$KEYBOARD_HINT', nat(2, 104, 1))
    assert kb > 0 and hint > 0, (kb, hint)
    r_row, r_help = kb + 8, hint + 8                                # push16 10 / pushstr "8#Select ..."
    assert sc.word(r_row) == p16(10) and sc.word(r_help)[0] == 0x11 and b'#Select' in pool_bytes(sc, r_help)
    ctrl = pool_bytes(sc, r_help).decode('latin1')
    # the OPTIONS build (0x42c in the clean script): the routine that starts `push 1; push 1; 2.97; ... "OPTIONS"`
    opt = find(sc, b'OPTIONS', nat(2, 102, 1))
    assert opt > 0
    build = opt - 4 * 9                                             # push 1, push 1, 2.97, push 170, push 256, 2.100, push 55, push 48, 2.101
    assert sc.word(build) == p16(1) and sc.word(build + 8) == nat(2, 97, 2), hex(build)
    helpwin = sc.word(r_row + 4)                                    # `call 0x178` (help window n)
    assert helpwin[0] == 0x03, helpwin
    # --- the row and its help line in OPTIONS
    a = Asm(sc)
    a(S('HUD Preset'), nat(2, 104, 1), p16(10), ('jmp', r_row + 4))
    rows = a.place()
    b = Asm(sc)
    b(S('Choose where the battle HUD sits, with a preview.'), nat(2, 104, 1), S(ctrl), ('jmp', r_help + 4))
    helps = b.place()
    sc.code[r_row:r_row + 4] = bl.w(0x07, 0, (rows - (r_row + 4)) // 4)
    sc.code[r_help:r_help + 4] = bl.w(0x07, 0, (helps - (r_help + 4)) // 4)
    # --- the HUD PRESET menu (window 1 + help window 10 as OPTIONS, side window 9 = the preview)
    m = Asm(sc)
    m(p16(1), p16(1), nat(2, 97, 2), p16(170), p16(256), nat(2, 100, 2), p16(55), p16(48), nat(2, 101, 2),
      S('HUD PRESET'), nat(2, 102, 1))
    for nm, _ in PRESETS:
        m(S(nm), nat(2, 104, 1))
    m(p16(10), ('call', bl_call_target(sc, r_row + 4), 1))
    for _, hl in PRESETS:
        m(S(hl), nat(2, 104, 1))
    m(p16(2), p16(9), nat(2, 97, 2), p16(250), p16(300), nat(2, 100, 2), p16(55), p16(316), nat(2, 101, 2),
      S('PREVIEW'), nat(2, 102, 1), S(''), nat(2, 104, 1), S('DRAW_HUDV'), p16(514), nat(2, 118, 2),
      S(ctrl), ('call', 0x13c, 1), p16(0), S('OPTIONS'), p16(1), nat(2, 152, 3), p16(1), nat(2, 107, 1),
      S('HUDP'), nat(2, 76, 1), ret())
    menu = m.place()
    e = Asm(sc)
    e(('call', menu, 0), ret())
    sc.export('amain_s6', e.place())
    for k in range(len(PRESETS)):
        c = Asm(sc)
        c(p16(k), p16(OP_SET), nat(2, 8, 2), ret())
        sc.export('ahudp_s%d' % k, c.place())
    c = Asm(sc)
    c(('call', build, 0), ret())
    sc.export('ahudp_tb', c.place())
    c = Asm(sc)
    c(S('aHUDP'), nat(2, 70, 1), p16(1), p16(10), nat(2, 117, 2), ret())
    sc.export('mhudp', c.place())
    c = Asm(sc)
    c(('call', 0x120, 0), nat(2, 89, 0), p16(1), nat(2, 106, 1), p16(9), nat(2, 106, 1), p16(10), nat(2, 106, 1),
      p16(14), nat(2, 106, 1), ret())
    sc.export('dhudp', c.place())
    # the preview window's row draw (args x, y, row, .. as draw_note): row 0 only
    c = Asm(sc)
    c(alloc(1), ldl(2), ('jz', 'draw'), ret(5))
    c.label('draw')
    c(ldl(1), ldl(0), p16(OP_DRAW), nat(2, 8, 3), ret(5))
    sc.export('draw_hudv', c.place())
    out = sc.build()
    n = bl.validate(out, 'option', sc.base, [])
    return out, n


def bl_call_target(sc, at):
    import struct
    imm = struct.unpack_from('<h', sc.word(at), 2)[0]
    return at + 4 + imm * 4


if __name__ == '__main__':
    o, n = patch(open(sys.argv[1], 'rb').read())
    open(sys.argv[2], 'wb').write(o)
    print(os.path.basename(sys.argv[2]), hashlib.sha256(o).hexdigest()[:8], len(o), n)
