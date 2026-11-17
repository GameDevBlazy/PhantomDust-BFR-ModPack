"""OPTIONS > Records, Visions, Stage Music (owner 2026-11-12: "Do all of these", answering "Into Options: Records, Visions, Stage
Music" - they leave the CREATE menu). Independently authored; patches the installed option_en.ssb that already has the HUD
Preset row (build_hud_option.py v2: row 6, export amain_s6).

Facts used (our reading with ssb_annotate.py, 2026-11-12): the HUD Preset row was added as `pushstr "HUD Preset"; 2.104;
push16 10; jmp back` and its help line as `pushstr "Choose where ..."; 2.104; pushstr <legend>; jmp back` (build_hud_option.py);
the OPTIONS window is 2.100(170, 256) right before its title "OPTIONS" (window 1); "into Single Play" = 2.8(1); 2.22(260);
2.107(-1); 2.76(0); 2.64(1) (the main menu's routine, build_edgar_menu.py).
New: rows 7-9 Records / Visions / Stage Music with their help lines; amain_s7..9 = 2.8(47, 21..23) (the loader opens that page
once the session starts, cwo_lobby.inl ed_go_page) and into Single Play, as CREATE's rows do; the window 60 taller.
Usage: python -B build_option_more.py IN OUT
"""
import hashlib
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import build_lobby3 as bl  # noqa: E402
from build_lobby3 import Script, Asm, w, p16, nat, ret, LOADER  # noqa: E402
from build_modlist_menu import pool_bytes  # noqa: E402

ROWS = [('Records', 'Your wins, losses, streaks and the skills you use most.', 21),
        ('Visions', 'Graphics looks: sharpen, colours, contrast, vignette, blur.', 22),
        ('Stage Music', 'Pick the music that plays on each stage.', 23)]


def find_str(sc, text):
    return [at for at in range(0x20, sc.base, 4) if sc.word(at)[0] == 0x11 and pool_bytes(sc, at) == text]


def patch(src):
    sc = Script(src)
    names = {bl.ssb_names.decode(a, b): i for i, (a, b, _) in enumerate(sc.exports)}
    assert 'amain_s6' in names and 'amain_s7' not in names, 'the HUD Preset OPTIONS (and not patched yet)'
    hud = find_str(sc, b'HUD Preset')
    hlp = find_str(sc, b'Choose where the battle HUD sits, with a preview.')
    assert len(hud) == 1 and len(hlp) == 1, (hud, hlp)
    r_site, h_site = hud[0] + 8, hlp[0] + 8
    assert sc.word(hud[0] + 4) == nat(2, 104, 1) and sc.word(r_site) == p16(10) and sc.word(r_site + 4)[0] == 0x07, 'row'
    assert sc.word(hlp[0] + 4) == nat(2, 104, 1) and sc.word(h_site)[0] == 0x11 and sc.word(h_site + 4)[0] == 0x07, 'help'
    title = [at for at in find_str(sc, b'OPTIONS') if sc.word(at + 4) == nat(2, 102, 1)]
    hsite = None
    for t in title:
        for at in range(t - 40, t, 4):
            if sc.word(at) == p16(170) and sc.word(at + 4) == p16(256) and sc.word(at + 8) == nat(2, 100, 2):
                hsite = at
                break
        if hsite:
            break
    assert hsite, 'the OPTIONS window size'
    for site, k in ((r_site, 0), (h_site, 1)):
        old = sc.word(site)
        a = Asm(sc)
        for row in ROWS:
            a(bl.S(row[k]), nat(2, 104, 1))
        a(old, ('jmp', site + 4))
        blk = a.place()
        sc.code[site:site + 4] = w(0x07, 0, (blk - (site + 4)) // 4)
    sc.code[hsite:hsite + 4] = p16(230)
    for i, (_, _, code) in enumerate(ROWS):
        a = Asm(sc)
        a(LOADER(47, [p16(code)]), w(0x0D, 0, 1),
          p16(1), nat(2, 8, 1), p16(260), nat(2, 22, 1), p16(-1), nat(2, 107, 1), p16(0), nat(2, 76, 1), p16(1), nat(2, 64, 1), ret(0))
        sc.export('amain_s%d' % (7 + i), a.place())
    out = sc.build()
    n = bl.validate(out, 'option', sc.base, [r_site, h_site])
    return out, n


if __name__ == '__main__':
    o, n = patch(open(sys.argv[1], 'rb').read())
    open(sys.argv[2], 'wb').write(o)
    print(os.path.basename(sys.argv[2]), hashlib.sha256(o).hexdigest()[:8], len(o), n)
