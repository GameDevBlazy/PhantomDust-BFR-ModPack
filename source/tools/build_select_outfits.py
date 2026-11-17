"""Select portraits that follow the highlighted outfit (owner 2026-11-02: "the portrait should follow the highlighted
outfit"; "Complete number 8"). Independently authored.
Game facts (MODLOG 2026-10-27 / 2026-11-02, Ghidra + our script readings): a menu image holds at most 128 sprites (a
0x200-byte handle buffer), so the 165 outfit faces are not sprites: like the stage pictures (cwo_portrait.inl, 5.42) the
menu image carries ONE extra cell - a 256 x 128 DXT1 texture with one 134 x 96 sprite - and the loader overwrites that
cell (its own DEFAULT-usage copy, UpdateSubresource) with the highlighted outfit's face from its own atlas file; the grid
draws that sprite in the COLORS character's cell (op 77 split screen / op 78 Xbox Live). The faces: the player sprites
player_00 .. player_10 = pc00a .. pc10a (VsMode sprites 21-31, MNet 38-48).
Builds, from the installed-to-be menu images:
  VsMode.alr / MNet.alr  + texture "vsmode0952" / "mnet0952" (256 x 128 DXT1) + sprite "outfit_cell.tga"
  cwo_select_outfits.dxt  raw DXT1 atlas, cells 136 x 96: row = character (pc00a .. pc10a), column = outfit 0-14
  cwo_select_outfits.txt  #atlas / #image lines for the loader (cwo_selout.inl)
Usage: python -B build_select_outfits.py IN_VsMode.alr IN_MNet.alr FACES_DIR OUT_DIR
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
import build_stage_portraits as bsp2         # noqa: E402  (dxt1_block)
import build_select_portraits as bsp         # noqa: E402  (PW, PH)
import face_sprite_pack as fsp               # noqa: E402  (load_png)

STEMS = ['pc%02da' % k for k in range(11)]
# v5.86 the selectable monsters too (owner 2026-11-03), framed as their select portraits
STEMS += ['enm00a', 'enm01a', 'enm02a', 'enm03a', 'enm05a', 'enm08a', 'enm09a', 'enm10a', 'enm11a', 'enm12a', 'enm13a']
STEMS += ['npc00a']                             # v5.100 the NPC page (colours 1-9 the NPCs, 10 the created character)
OUTFITS = 15
CW, CH = 136, 96                              # atlas cell (DXT1 blocks); the sprite shows 134 x 96 of it
TW, TH = 256, 128


def face(path):
    """the front render (512 x 512 RGBA) -> 136 x 96 RGB on the select backdrop (same crop as build_select_portraits)"""
    im = fsp.load_png(path)
    ch = int(512 * bsp.PH / bsp.PW)
    y0 = (512 - ch) // 2 - 20
    out = []
    for y in range(CH):
        for x in range(CW):
            xx = min(bsp.PW - 1, x)
            acc = [0.0, 0.0, 0.0]
            n = 0
            sx0, sx1 = xx * 512 / bsp.PW, (xx + 1) * 512 / bsp.PW
            sy0, sy1 = y0 + y * ch / CH, y0 + (y + 1) * ch / CH
            for sy in range(int(sy0), max(int(sy0) + 1, int(sy1))):
                for sx in range(int(sx0), max(int(sx0) + 1, int(sx1))):
                    p = im.px[min(511, max(0, sy)) * 512 + min(511, sx)]
                    a = p[3] / 255.0
                    for k in range(3):
                        acc[k] += p[k] * a + (16 + 8 * k) * (1 - a)
                    n += 1
            out.append((int(acc[0] / n), int(acc[1] / n), int(acc[2] / n)))
    return out


def atlas(faces):
    W, H = CW * OUTFITS, CH * len(STEMS)
    px = [(16, 24, 32)] * (W * H)
    missing = []
    for r, stem in enumerate(STEMS):
        for k in range(OUTFITS):
            p = os.path.join(faces, stem, 'outfits', 'o%d.png' % k)
            if not os.path.exists(p) and stem == 'npc00a':      # the NPC page has 10 colours: the rest repeat colour 1
                p = os.path.join(faces, stem, 'outfits', 'o0.png')
            if not os.path.exists(p):
                missing.append('%s %d' % (stem, k))
                p = os.path.join(faces, stem, 'raw', 's_front.png')
                if not os.path.exists(p): p = os.path.join(faces, stem, 'raw', 'v_front.png')
            pic = face(p)
            for y in range(CH):
                px[(r * CH + y) * W + k * CW:(r * CH + y) * W + k * CW + CW] = pic[y * CW:(y + 1) * CW]
    enc = bytearray()
    for by in range(0, H, 4):
        for bx in range(0, W, 4):
            enc += bsp2.dxt1_block([px[(by + j) * W + bx + i] for j in range(4) for i in range(4)])
    return W, H, bytes(enc), missing, px


def add_cell(src, texname):
    """the image + one 256 x 128 DXT1 texture (dark) + one sprite; returns (alr bytes, ntex, nspr, tex, sprite, h0, h1)"""
    R = at.parse(src)
    R['resource_dirty'] = True
    g = bytes(R['groups'][R['slot'][1]])
    cid, span, ntex, nspr = struct.unpack_from('<IIII', g, 0)
    assert nspr + 1 <= 128, nspr
    enc = bsp2.dxt1_block([(16, 24, 32)] * 16) * ((TW // 4) * (TH // 4))
    pos = at.align(len(R['resource']), 0x100)
    R['resource'] += b'\0' * (pos - len(R['resource'])) + bytes(enc)
    R['resource'] += b'\0' * (at.align(len(R['resource']), 0x80) - len(R['resource']))
    te = at.tex_entries(R)
    assert len(te) == ntex
    entry = bytearray(pc.tex_entry(texname, pos, TW, TH, 1, 0x0C))
    te.append(entry)
    at.set_tex_entries(R, te)
    names = g[0x20:0x20 + 0x20 * ntex]
    descs = g[0x20 + 0x20 * ntex:0x20 + 0x34 * ntex]
    recs = g[0x20 + 0x34 * ntex:span]
    name = bytearray(names[-0x20:]); name[0:0x10] = (texname.capitalize() + '.dds').encode().ljust(0x10, b'\0')
    d0 = None
    for t in range(ntex):
        if struct.unpack_from('<I', te[t], 12)[0] >> 8 & 0xff == 0x0C:
            d0 = bytearray(descs[0x14 * t:0x14 * t + 0x14]); break
    assert d0 is not None, 'no DXT1 texture to copy a descriptor from'
    struct.pack_into('<hhI', d0, 0, TW, TH, TW * TH // 2 + 128)
    struct.pack_into('<I', d0, 8, 1)
    new = struct.pack('<I', ntex) + b'outfit_cell.tga'.ljust(0x20, b'\0') + struct.pack(
        '<4f2i', 0.25 / TW, 0.25 / TH, (bsp.PW + 0.25) / TW, (bsp.PH + 0.25) / TH, bsp.PW, bsp.PH)
    body = bytes(g[0x10:0x20]) + bytes(names) + bytes(name) + bytes(descs) + bytes(d0) + bytes(recs) + new
    R['groups'][R['slot'][1]] = bytearray(struct.pack('<IIII', 0x10, 0x10 + len(body), ntex + 1, nspr + 1) + body + struct.pack('<II', 0, 8))
    alr = at.build(R)
    C = at.parse(alr)
    gc = bytes(C['groups'][C['slot'][1]])
    nt2, ns2 = struct.unpack_from('<II', gc, 8)
    per = [struct.unpack_from('<I', gc, 0x20 + 0x20 * nt2 + 0x14 * t + 8)[0] for t in range(nt2)]
    owners = [struct.unpack_from('<I', gc, 0x20 + 0x34 * nt2 + 0x3c * i)[0] for i in range(ns2)]
    assert per == [owners.count(t) for t in range(nt2)] and ns2 == nspr + 1 <= 128, (per, ns2)
    e = at.tex_entries(C)[ntex]
    h0, h1 = struct.unpack_from('<II', e, 0x14)
    return alr, ntex + 1, nspr + 1, ntex, nspr, h0, h1


def main(vs, mnet, faces, out):
    os.makedirs(out, exist_ok=True)
    W, H, enc, missing, _ = atlas(faces)
    open(os.path.join(out, 'cwo_select_outfits.dxt'), 'wb').write(enc)
    lines = ['# select portraits per outfit (build_select_outfits.py): atlas w h cell_w cell_h columns; image menu-slot textures sprites texture sprite hash0 hash1 first-player-sprite',
             '#atlas\t%d\t%d\t%d\t%d\t%d' % (W, H, CW, CH, OUTFITS), '#stems\t' + '\t'.join(STEMS)]
    for fn, texname, slot, first in (('VsMode.alr', 'vsmode0952', 0, 21), ('MNet.alr', 'mnet0952', 3, 38)):
        alr, nt, ns, t, s, h0, h1 = add_cell(open(vs if fn == 'VsMode.alr' else mnet, 'rb').read(), texname)
        open(os.path.join(out, fn), 'wb').write(alr)
        lines.append('#image\t%d\t%d\t%d\t%d\t%d\t%u\t%u\t%d' % (slot, nt, ns, t, s, h0, h1, first))
        print(fn, hashlib.sha256(alr).hexdigest()[:8], len(alr), 'texture', t, 'sprite', s, 'hash %08x %08x' % (h0, h1))
    open(os.path.join(out, 'cwo_select_outfits.txt'), 'w', newline='\n').write('\n'.join(lines) + '\n')
    print('atlas %d x %d (%d bytes); missing renders: %s' % (W, H, len(enc), ', '.join(missing) or 'none'))


if __name__ == '__main__':
    main(*sys.argv[1:5])
