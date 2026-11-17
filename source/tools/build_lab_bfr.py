"""The Lab's two Battle for Reality rows (owner 2026-11-01: "when they click on lab, it shows edit arsenal view stock list.
There should be another option that says enable Battle for Reality skill pack. And then there'll also be another option
that says revert back to OG Phantom Dust skills ... 0 through 374 for the OG skills ... 1 through 466").
Independently authored; patches a copy of Menu/laboratory_en.ssb (the clean game's f1849415, or the owner's installed
cb5cf47c which adds a debug row) with the Script / Asm tools of build_lobby3.py.

Our reading of the script (ssb_annotate.py; 25_Menu_Research): the MAIN page builder (0x30c4 in the clean file; no
arguments, called again by the Lab whenever it shows MAIN) adds list items with 2.123(text, id): 0 Edit Arsenal, 1 View
Stock List, 2 View Tutorial Movie (only when 5.119() == 1), then sets up window 3 (2.97(7, 3) ...) and the help rows
(2.104 each, the same order), then 2.107(0) and the page MAIN (2.76). A item id k runs amain_s<k>.
What this adds: items 3 "Enable Battle for Reality skill pack (1-466)" and 4 "Revert back to OG Phantom Dust skills
(0-374)" with their help rows; amain_s3 / amain_s4 ask the loader (native 2.8 ops 81 / 82, cwo_bfr.inl) and rebuild MAIN.
The sites are found by their words, not by address.
Usage: python -B build_lab_bfr.py IN_laboratory_en.ssb OUT_laboratory_en.ssb
"""
import hashlib
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import build_lobby3 as bl  # noqa: E402
from build_lobby3 import Script, Asm, w, p16, nat, ret, NOP, LOADER, PUSHRET  # noqa: E402

OP_BFR_ON, OP_BFR_OFF = 81, 82
WIDTH = 440                                     # v5.59 the MAIN window (game: 256)
ROW_ON = 'Enable Battle for Reality skill pack (1-466)'
ROW_OFF = 'Revert back to OG Phantom Dust skills (0-374)'
HELP_ON = 'Use the Battle for Reality skills (1-466) in every mode.'
HELP_OFF = 'Go back to the original Phantom Dust skills (0-374).'


def find(code, words, lo=0, hi=None):
    pat = b''.join(words)
    hits, at = [], code.find(pat, lo)
    while at >= 0 and (hi is None or at < hi):
        if at % 4 == 0:
            hits.append(at)
        at = code.find(pat, at + 4)
    return hits


def patch(src):
    sc = Script(src)
    code = bytes(sc.code)
    start = find(code, [p16(1), p16(0), nat(2, 97, 2), p16(81), p16(256), nat(2, 100, 2)])
    assert len(start) == 1, 'MAIN page builder start: %d matches' % len(start)
    b0 = start[0]
    assert sc.word(b0 - 4) == ret(0), 'the builder follows a function end'
    win = find(code, [p16(3), p16(7), nat(2, 97, 2)], b0, b0 + 0x200)
    assert len(win) == 1, 'window 3 set-up after the items: %d' % len(win)
    hlp = find(code, [p16(0), nat(2, 107, 1)], win[0], b0 + 0x300)
    assert hlp, 'the end of the help rows'
    hlp = hlp[0]
    assert b''.join(find(code, [nat(2, 123, 2)], b0, win[0]) and [b'ok']) == b'ok', 'items in the builder'
    names = {bl.ssb_names.decode(a, b): i for i, (a, b, _) in enumerate(sc.exports)}
    assert 'amain_s0' in names and 'amain_s3' not in names, 'MAIN handlers'
    items = Asm(sc)
    items(bl.S(ROW_ON), p16(3), nat(2, 123, 2), bl.S(ROW_OFF), p16(4), nat(2, 123, 2),
          p16(3), p16(7), nat(2, 97, 2), ret(0))
    h_items = items.place()
    sc.code[win[0]:win[0] + 12] = w(0x03, 0, (h_items - (win[0] + 4)) // 4) + NOP + NOP
    helps = Asm(sc)
    helps(bl.S(HELP_ON), nat(2, 104, 1), bl.S(HELP_OFF), nat(2, 104, 1), p16(0), nat(2, 107, 1), ret(0))
    h_helps = helps.place()
    sc.code[hlp:hlp + 8] = w(0x03, 0, (h_helps - (hlp + 4)) // 4) + NOP
    # v5.59 (owner 2026-11-02: "When I click on enable battle for reality skill pack, the movie list pops up"): the Lab's
    # list runs the handler of the row's POSITION (amain_s<row>), and the movie row (item 2) is only there when
    # 5.119() == 1 - without it our rows are positions 2 and 3. The handlers ask the same question the builder does:
    #   s2: movie row shown -> the game's own movie handler, else Enable;  s3: shown -> Enable, else Revert;  s4: Revert.
    movie = [nat(5, 119, 0), PUSHRET, p16(1), bl.EQ]
    orig_s2 = sc.exports[names['amain_s2']][2] * 4
    def on_off(op):
        return [LOADER(op), w(0x0D, 0, 1), ('call', b0, 0), ret(0)]
    s2 = Asm(sc)
    s2(*movie, ('jz', 'ours'), ('call', orig_s2, 0), ret(0), ('label', 'ours'), *on_off(OP_BFR_ON))
    sc.exports[names['amain_s2']] = sc.exports[names['amain_s2']][:2] + (s2.place() // 4,)
    s3 = Asm(sc)
    s3(*movie, ('jz', 'hidden'), *on_off(OP_BFR_ON), ('label', 'hidden'), *on_off(OP_BFR_OFF))
    sc.export('amain_s3', s3.place())
    s4 = Asm(sc)
    s4(*on_off(OP_BFR_OFF))
    sc.export('amain_s4', s4.place())
    # the window wide enough for the two rows (was 256: "Revert back to OG Phantom Dust skills (0-374)" ran past it)
    assert sc.word(b0 + 16) == p16(256), 'the MAIN window width'
    sc.code[b0 + 16:b0 + 20] = p16(WIDTH)
    out = sc.build()
    n = bl.validate(out, 'laboratory', sc.base, [win[0], hlp])
    return out, {'builder': hex(b0), 'items_site': hex(win[0]), 'help_site': hex(hlp), 'items': hex(h_items),
                 'helps': hex(h_helps), 'checked': n}


if __name__ == '__main__':
    src = open(sys.argv[1], 'rb').read()
    out, rep = patch(src)
    open(sys.argv[2], 'wb').write(out)
    print('laboratory', hashlib.sha256(src).hexdigest()[:8], '->', hashlib.sha256(out).hexdigest()[:8], rep)
