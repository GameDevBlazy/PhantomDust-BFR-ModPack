"""The Lab's "View Skill Vault" row (owner 2026-11-10: "in the stock mode, offline and online, there needs to be a skill vault.
You know how you can view stock lists? You should be able to view the vault"). Independently authored; patches a copy of the
installed Menu/laboratory_en.ssb that already carries the two Battle for Reality rows (build_lab_bfr.py), with the same
Script / Asm tools (build_lobby3.py). Sites are found by their words, not by address.
Facts used (build_lab_bfr.py, MODLOG 2026-11-01/02): the MAIN builder adds items with 2.123(text, id) in a placed block that
ends "3, 7, 2.97(2), ret"; the help rows block ends "0, 2.107(1), ret"; the Lab runs the handler of the row's POSITION
(amain_s<row>), and the movie row (item 2) exists only when 5.119() == 1 - so with the movie row Enable / Revert / Vault are
positions 3 / 4 / 5, without it 2 / 3 / 4. Today amain_s4 = Revert only.
What this adds: item 5 "View Skill Vault" + its help row; amain_s4 = movie shown ? Revert : Vault; amain_s5 = Vault. Vault =
native 2.8 op 115 (the loader opens the Skill Vault, cwo_vault.inl), then MAIN again.
Usage: python -B build_lab_vault.py IN_laboratory_en.ssb OUT_laboratory_en.ssb"""
import hashlib
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import build_lobby3 as bl  # noqa: E402
from build_lobby3 import Script, Asm, w, p16, nat, ret, NOP, LOADER, PUSHRET  # noqa: E402
from build_lab_bfr import find, OP_BFR_OFF, WIDTH  # noqa: E402

OP_VAULT = 115
ROW = 'View Skill Vault'
HELP = 'Skills not in your stock: swap one in for a stock skill.'


def patch(src):
    sc = Script(src)
    code = bytes(sc.code)
    start = find(code, [p16(1), p16(0), nat(2, 97, 2), p16(81), p16(WIDTH), nat(2, 100, 2)])
    assert len(start) == 1, 'MAIN page builder (BFR-row width) start: %d matches' % len(start)
    b0 = start[0]
    names = {bl.ssb_names.decode(a, b): i for i, (a, b, _) in enumerate(sc.exports)}
    assert 'amain_s4' in names and 'amain_s5' not in names, 'BFR rows present, no vault yet'
    # the BFR items block: "..., 4, 2.123(2), 3, 7, 2.97(2), ret"
    tail = find(code, [p16(4), nat(2, 123, 2), p16(3), p16(7), nat(2, 97, 2), ret(0)])
    assert len(tail) == 1, 'the BFR items block end: %d' % len(tail)
    t = tail[0] + 8
    items = Asm(sc)
    items(bl.S(ROW), p16(5), nat(2, 123, 2), p16(3), p16(7), nat(2, 97, 2), ret(0))
    h_items = items.place()
    sc.code[t:t + 12] = w(0x03, 0, (h_items - (t + 4)) // 4) + NOP + NOP
    # the BFR help block: "..., 2.104(1), 0, 2.107(1), ret" (the last such block in the file is ours)
    hb = find(code, [nat(2, 104, 1), p16(0), nat(2, 107, 1), ret(0)])
    assert hb, 'the help block end'
    h = hb[-1] + 4
    helps = Asm(sc)
    helps(bl.S(HELP), nat(2, 104, 1), p16(0), nat(2, 107, 1), ret(0))
    h_helps = helps.place()
    sc.code[h:h + 8] = w(0x03, 0, (h_helps - (h + 4)) // 4) + NOP
    movie = [nat(5, 119, 0), PUSHRET, p16(1), bl.EQ]
    def op_then_main(op):
        return [LOADER(op), w(0x0D, 0, 1), ('call', b0, 0), ret(0)]
    s4 = Asm(sc)
    s4(*movie, ('jz', 'hidden'), *op_then_main(OP_BFR_OFF), ('label', 'hidden'), *op_then_main(OP_VAULT))
    sc.exports[names['amain_s4']] = sc.exports[names['amain_s4']][:2] + (s4.place() // 4,)
    s5 = Asm(sc)
    s5(*op_then_main(OP_VAULT))
    sc.export('amain_s5', s5.place())
    out = sc.build()
    n = bl.validate(out, 'laboratory', sc.base, [t, h])
    return out, {'builder': hex(b0), 'items_site': hex(t), 'help_site': hex(h), 'checked': n}


if __name__ == '__main__':
    src = open(sys.argv[1], 'rb').read()
    out, rep = patch(src)
    open(sys.argv[2], 'wb').write(out)
    print('laboratory', hashlib.sha256(src).hexdigest()[:8], '->', hashlib.sha256(out).hexdigest()[:8], rep)
