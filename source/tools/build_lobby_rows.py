"""Lobby face strips per outfit, part 2 (owner 2026-11-03: the lobby portrait pack, "match up with all of their outfits ...
Offline split screen and multiplayer lobby"). Independently authored.
1) The menu images (VsMode.alr image slot 0, MNet.alr image slot 3) get one 128 x 256 DXT1 texture of 8 row cells (76 x 24
   each, cell k at y 24k) and 8 sprites "lobby_row_<k>.tga" (74 x 24) - the loader fills each cell with the strip of the
   member drawn in that row (cwo_lobstrip.inl, atlas from build_lobby_strips.py).
2) The scripts: the split-screen lobby's face routine (vsmenu: `alloc 1; ldloc 0; push16 28; cmp ...` = draw_face(char,
   x, y) -> 2.82(0, 32 + char, x, y)) first asks op 110(0, word, x, y) and returns when it drew; its three callers pass the
   member word (character | outfit << 16) instead of the character; the Xbox Live lobby (livelobby draw_lobby: `push16 0;
   ldloc -2; add; push16 3; callnative 2.82`) calls op 110(3, 4.560(member), x, y) instead (the loader draws the stock strip
   when it has none).
Usage: python -B build_lobby_rows.py image IN.alr OUT.alr TEXNAME
       python -B build_lobby_rows.py vsmenu|livelobby IN.ssb OUT.ssb
"""
import hashlib
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import alr_transplant as at                  # noqa: E402
import pd_common as pc      # noqa: E402
import build_stage_portraits as bsp2         # noqa: E402
import build_lobby3 as bl                    # noqa: E402
from build_lobby3 import Script, Asm, p16, nat, ldl, ret, PUSHRET, AND  # noqa: E402

TW, TH, ROWS, CW, CH, SW, SH = 128, 256, 8, 76, 24, 74, 24
OP = 110


def add_rows(src, texname):
    R = at.parse(src)
    R['resource_dirty'] = True
    g = bytes(R['groups'][R['slot'][1]])
    cid, span, ntex, nspr = struct.unpack_from('<IIII', g, 0)
    assert nspr + ROWS <= 128, nspr
    enc = bsp2.dxt1_block([(10, 14, 18)] * 16) * ((TW // 4) * (TH // 4))
    pos = at.align(len(R['resource']), 0x100)
    R['resource'] += b'\0' * (pos - len(R['resource'])) + bytes(enc)
    R['resource'] += b'\0' * (at.align(len(R['resource']), 0x80) - len(R['resource']))
    te = at.tex_entries(R)
    assert len(te) == ntex
    te.append(bytearray(pc.tex_entry(texname, pos, TW, TH, 1, 0x0C)))
    at.set_tex_entries(R, te)
    names = g[0x20:0x20 + 0x20 * ntex]
    descs = g[0x20 + 0x20 * ntex:0x20 + 0x34 * ntex]
    recs = g[0x20 + 0x34 * ntex:span]
    name = bytearray(names[-0x20:]); name[0:0x10] = (texname.capitalize() + '.dds').encode().ljust(0x10, b'\0')
    d0 = None
    for t in range(ntex):
        if struct.unpack_from('<I', te[t], 12)[0] >> 8 & 0xff == 0x0C:
            d0 = bytearray(descs[0x14 * t:0x14 * t + 0x14]); break
    assert d0 is not None
    struct.pack_into('<hhI', d0, 0, TW, TH, TW * TH // 2 + 128)
    struct.pack_into('<I', d0, 8, ROWS)
    new = b''
    for k in range(ROWS):
        new += struct.pack('<I', ntex) + ('lobby_row_%d.tga' % k).encode().ljust(0x20, b'\0') + struct.pack(
            '<4f2i', 0.25 / TW, (CH * k + 0.25) / TH, (SW + 0.25) / TW, (CH * k + SH + 0.25) / TH, SW, SH)
    body = bytes(g[0x10:0x20]) + bytes(names) + bytes(name) + bytes(descs) + bytes(d0) + bytes(recs) + new
    R['groups'][R['slot'][1]] = bytearray(struct.pack('<IIII', 0x10, 0x10 + len(body), ntex + 1, nspr + ROWS) + body + struct.pack('<II', 0, 8))
    alr = at.build(R)
    C = at.parse(alr)
    gc = bytes(C['groups'][C['slot'][1]])
    nt2, ns2 = struct.unpack_from('<II', gc, 8)
    per = [struct.unpack_from('<I', gc, 0x20 + 0x20 * nt2 + 0x14 * t + 8)[0] for t in range(nt2)]
    owners = [struct.unpack_from('<I', gc, 0x20 + 0x34 * nt2 + 0x3c * i)[0] for i in range(ns2)]
    assert per == [owners.count(t) for t in range(nt2)] and ns2 == nspr + ROWS <= 128, (per, ns2)
    e = at.tex_entries(C)[ntex]
    h0, h1 = struct.unpack_from('<II', e, 0x14)
    return alr, ntex + 1, nspr + ROWS, ntex, nspr, h0, h1


def patch_vsmenu(src):
    sc = Script(src)
    # the face routine: alloc 1; ldloc 0; push16 28; cmp(3); jmp-if ... ; ldloc 2; ldloc 1; push16 32; ldloc 0; add; push16 0; 2.82
    f = [a for a in range(0, len(sc.code) - 48, 4) if sc.word(a) == bl.w(0x06, 0, 1) and sc.word(a + 4) == ldl(0) and
         sc.word(a + 8) == p16(28) and sc.word(a + 12) == bl.w(0x17, 0, 3) and sc.word(a + 20) == ldl(2) and
         sc.word(a + 24) == ldl(1) and sc.word(a + 28) == p16(32) and sc.word(a + 44) == nat(2, 82, 4)]
    assert len(f) == 1, ('face routine', [hex(a) for a in f])
    fr = f[0]
    calls = [a for a in range(0, len(sc.code), 4) if sc.word(a)[0] == 0x03 and sc.word(a)[1] == 3 and
             a + 4 + struct.unpack_from('<h', sc.word(a), 2)[0] * 4 == fr]
    assert len(calls) == 3, ('face routine callers', [hex(a) for a in calls])
    for c in calls:
        if sc.word(c - 4) == ldl(-3):                              # the players' rows: ldloc -3 = word & 0xffff, -1 = the word
            sc.code[c - 4:c] = ldl(-1)
        else:                                                      # ldloc -2; push32 0xffff; and; call
            assert sc.word(c - 4) == AND and sc.word(c - 12)[0] == 0x0F and sc.word(c - 16) == ldl(-2), hex(c)
            sc.code[c - 12:c] = bl.w(0x00) * 3
    a = Asm(sc)
    a(ldl(2), ldl(1), ldl(0), p16(0), p16(OP), nat(2, 8, 5), PUSHRET, ('jz', 'old'), ret(4))
    a.label('old')
    a(ldl(0), bl.w(0x0F) + struct.pack('<I', 0xffff), AND, bl.w(0x0A, 0, 0), ldl(0), ('jmp', fr + 8))
    blk = a.place()
    sc.code[fr + 4:fr + 8] = bl.w(0x07, 0, (blk - (fr + 8)) // 4)
    out = sc.build()
    bl.validate(out, 'vsmenu', sc.base, [])
    return out


def patch_livelobby(src):
    sc = Script(src)
    h = [a for a in range(0, len(sc.code) - 20, 4) if sc.word(a) == p16(0) and sc.word(a + 4) == ldl(-2) and
         sc.word(a + 8) == bl.w(0x12) and sc.word(a + 12) == p16(3) and sc.word(a + 16) == nat(2, 82, 4)]
    assert len(h) == 1, ('live face draw', [hex(a) for a in h])
    at_ = h[0]
    a = Asm(sc)
    a(ldl(-1), nat(4, 560, 1), PUSHRET, p16(3), p16(OP), nat(2, 8, 5), ('jmp', at_ + 20))
    blk = a.place()
    sc.code[at_:at_ + 4] = bl.w(0x07, 0, (blk - (at_ + 4)) // 4)
    for k in range(1, 5):
        sc.code[at_ + 4 * k:at_ + 4 * k + 4] = bl.w(0x00)
    out = sc.build()
    bl.validate(out, 'livelobby', sc.base, [])
    return out


if __name__ == '__main__':
    kind = sys.argv[1]
    if kind == 'image':
        alr, nt, ns, t, s, h0, h1 = add_rows(open(sys.argv[2], 'rb').read(), sys.argv[4])
        open(sys.argv[3], 'wb').write(alr)
        print('%s %s textures %d sprites %d texture %d first sprite %d hash %u %u' % (os.path.basename(sys.argv[3]), hashlib.sha256(alr).hexdigest()[:8], nt, ns, t, s, h0, h1))
    else:
        o = (patch_vsmenu if kind == 'vsmenu' else patch_livelobby)(open(sys.argv[2], 'rb').read())
        open(sys.argv[3], 'wb').write(o)
        print(os.path.basename(sys.argv[3]), hashlib.sha256(o).hexdigest()[:8])
