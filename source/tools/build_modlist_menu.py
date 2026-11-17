"""MOD LIST on the main menu (owner 2026-11-02, item 11: "on the main menu, it should say Battle for Reality, Mod List, or
just Mod List ... show what every mod does"). Independently authored; patches a copy of the installed Menu/mainmenu_en.ssb
(19d0a84b) with the Script / Asm tools of build_lobby3.py.
Game facts used (our reading with ssb_annotate.py, scratchpad mm_ann.txt; build_forge_menu.py / build_edgar_menu.py):
- the main menu's rows are 2.123(label, id) (choosing id N runs export amain_s<N>), their help lines 2.104(text) in the same
  order; the block at 0x1308 adds OPTIONS MODE and EDGAR'S DREAM (id 160) and returns at 0x1320; the help block at 0x1324
  adds their two help lines and returns at 0x1334.
New: those two returns jump to blocks that add MOD LIST (id 170) and its help line, then return; export amain_s170 =
2.8(95) (the loader opens its Mod List window, cwo_modlist.inl); the main menu stays.
Usage: python -B build_modlist_menu.py IN_mainmenu OUT_mainmenu
"""
import hashlib
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import build_lobby3 as bl  # noqa: E402
from build_lobby3 import Script, Asm, p16, nat, ret, S  # noqa: E402

MM_SHA = '19d0a84b'
ID, OP = 170, 95


def pool_bytes(sc, at):
    import struct
    off = struct.unpack_from('<H', sc.word(at), 2)[0]
    return bytes(sc.pool[off:sc.pool.index(b'\0', off)])


def find(sc, text, *after):
    """the code address of `pushstr <text...>` followed by the words `after`"""
    for at in range(0, len(sc.code) - 4 * (len(after) + 1), 4):
        if sc.word(at)[0] != 0x11:
            continue
        try:
            if text not in pool_bytes(sc, at):
                continue
        except ValueError:
            continue
        if all(sc.word(at + 4 * (k + 1)) == x for k, x in enumerate(after)):
            return at
    return -1


def patch(src):
    # the installed main menu (19d0a84b) or the BFR pack's clean-based one: the rows found by their words, not fixed places
    sc = Script(src)
    names = {bl.ssb_names.decode(a, b) for a, b, _ in sc.exports}
    assert 'amain_s170' not in names, 'already patched'
    er = find(sc, b"EDGAR'S DREAM", p16(160), nat(2, 123, 2), ret())
    hr = find(sc, b'Create stages, skills and more.', nat(2, 104, 1), ret())
    assert er > 0 and hr > 0, ('EDGAR row / help line', er, hr)
    label = pool_bytes(sc, er)
    r_row, r_help = er + 12, hr + 8
    a = Asm(sc)
    a(S(label.replace(b"EDGAR'S DREAM", b'MOD LIST').decode('latin1')), p16(ID), nat(2, 123, 2), ret())   # the same layout codes
    rows = a.place()
    b = Asm(sc)
    b(S('Every installed mod and what it does.'), nat(2, 104, 1), ret())
    helps = b.place()
    c = Asm(sc)
    c(p16(OP), nat(2, 8, 1), ret())
    act = c.place()
    sc.export('amain_s170', act)
    sc.code[r_row:r_row + 4] = bl.w(0x07, 0, (rows - (r_row + 4)) // 4)
    sc.code[r_help:r_help + 4] = bl.w(0x07, 0, (helps - (r_help + 4)) // 4)
    out = sc.build()
    n = bl.validate(out, 'mainmenu', sc.base, [])
    return out, n


if __name__ == '__main__':
    o, n = patch(open(sys.argv[1], 'rb').read())
    open(sys.argv[2], 'wb').write(o)
    print(os.path.basename(sys.argv[2]), hashlib.sha256(o).hexdigest()[:8], len(o), n)
