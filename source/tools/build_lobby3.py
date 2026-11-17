"""3 VS 3 LOBBY inside the game's own lobby (owner 2026-10-25: "take this whole lobby menu, UI layout, all this exactly how
it is, and add it into the 3v3 mode. Not create your own"; reference: the owner's screenshots of the split-screen LOBBY
and its OPTIONS window). Independently authored; patches copies of the live menu scripts Menu/vsmenu_en.ssb (the
split-screen multiplayer menu) and Menu/mainmenu_en.ssb, as build_select_slot.py and build_forge_menu.py do.

Game facts used (our reading of the scripts with ssb_annotate.py; natives via the exe table 0x1402ddd00, Ghidra batch_065;
VM: SSB_VM_ISA_BATCH006 - 64-bit value stack, a native sees the last pushed value as args[0]):
- vsmenu pages: 2.76(name) switches page, the engine runs exports m<page> / d<page>, 2.70("a<PAGE>") hands the page's
  button events to a<page>_tb (B), _tx (X), _s<n> (item n); 2.64(code) leaves the menu. Windows: 2.97(style, index),
  2.100(width, h) (window +0x44/+0x46), 2.101(x, y) (+0x40/+0x42), 2.102 title, 2.104 plain row, 2.123(text, id) item,
  2.116 cursor row (-1 = none; the input routine FUN_14024b000 then sends no A event), 2.118(name, 514) row draw callback
  draw_<name>(x, y, row, text), 2.107 focus, 2.106 close. Menu variables: 32 x 64 bit (2.153 / 2.154, index & 0x1f,
  *0x140533c58 + 0x10430), cleared when the menu starts (FUN_140245960).
- the LOBBY page builder 0x5b34 (rows window 7 + SESSION INFO window 8 + help 14); its row callback draw_lobb draws the
  game's red row bar 2.93, the face (0x6de4, our v4.3 helper), the colour number and 2.87(x, y, text, name, arsenal);
  var 0 = rule, 1 = stage (a row of the stage list 0x140509ba0: 2.151(row, 51) = *(0x140509ba8 + row * 0x28)), 2 / 4 =
  player 1 character word (colour << 16 | charaset row) / arsenal (FUN_14024c220 0x23 = *(*0x1404bec10 + 0x3a30) + i *
  100), 6 = the lobby was reached: every picker (CSEL 0x557c, ASEL 0x565c, stage list 0x54d0, rule 0x536c) goes back to
  0x5b34 when var 6 is set (acsel_ta, aasel_ta, assel_ta, arule_ta, their _tb).
- mlobb fires A itself (2.73(5, 0)) because the rows window has no cursor; OPTIONS (0x5730) = window 12 at (56, 108),
  2.125(4097), items 2.123, DRAW_LOPT, page LOPT.
- the menu MENU page builder 0x5250 is the first page of a fresh split-screen menu (initmenu 0x5e0c); B there leaves
  with 2.64(5) after the fade (avsme_tb 0x5e88).
- mainmenu: the MAIN page's frame export mmain hands events with 2.70("aMAIN") at 0xc38; alive_s1 (0xd40) = Split
  Screen (fade, 2.64(5)).
- Single Play from a menu script: 2.22(260); 2.107(-1); 2.76(0); 2.64(1) (build_forge_menu.py, batch_074).
- native 2.8 = CwoLoader's own (the empty slot of the group-2 table); ops 21-27 are the 3 vs 3 lobby's (cwo_lobby.inl).

Flow: Edgar's Dream > Create a Mode > 3 VS 3 LOBBY -> the loader leaves Single Play to the main menu -> mmain asks 2.8(21)
and runs Split Screen -> vsmenu's MENU builder asks 2.8(22) and opens the lobby instead -> the LOBBY builder asks 2.8(23)
and builds the 3 vs 3 lobby (6 slot rows: face, colour, player, arsenal, team, rating; SESSION INFO: game type, rule,
stage, START GAME) -> A on a slot: OPTIONS for it (the game's own pickers on player 1's variables, the slot swapped in
and out by the loader), A on START GAME: 2.8(26, rule, stage) then Single Play (the loader builds the 6 fighters).
Usage: python -B build_lobby3.py IN_vsmenu IN_mainmenu OUT_vsmenu OUT_mainmenu
"""
import hashlib
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', '13_Independent_Research_Tools'))
import ssb_disasm  # noqa: E402
import ssb_names  # noqa: E402

VS_SHA = None          # filled from the file actually installed (checked by the install script); the code checks bytes
MM_SHA = '2740d670'    # live mainmenu (Edgar's Dream item, build_forge_menu.py)
OP_MAIN, OP_VS, OP_LOBB, OP_GET, OP_SET, OP_START, OP_LEAVE, OP_LIVE = 21, 22, 23, 24, 25, 26, 27, 28
K_RULE, K_STAGE = 100, 101
STYLE = 'offline'                                       # 'offline' (installed 5.3) or 'live' (the Xbox Live lobby's layout)
PANEL_Y = 262                                           # live: the arsenal panel between LOBBY and SESSION INFO
V_EDIT, V_CUR = 20, 21                                  # free menu variables (0-14 are the game's)


def w(op, sub=0, imm=0):
    return struct.pack('<I', op | (sub & 0xFF) << 8 | (imm & 0xFFFF) << 16)


class Script:
    """the code part of an SSB file, its export table and text pool; new code goes after the original code"""

    def __init__(self, src):
        hdr = list(struct.unpack_from('<8I', src, 0))
        table, text = hdr[1], hdr[2]
        assert hdr[0] == 0x20 and hdr[2] == hdr[3] and (text - table) % 12 == 0
        self.hdr, self.code, self.pool = hdr, bytearray(src[:table]), bytearray(src[text:])
        self.exports = [struct.unpack_from('<3I', src, table + 12 * k) for k in range((text - table) // 12)]
        self.base = table
        self.blk = bytearray()

    def here(self):
        return self.base + len(self.blk)

    def s(self, text):                                  # pool offset of a string (added when missing)
        b = text.encode('cp932')
        i = self.pool.find(b'\0' + b + b'\0')
        if i >= 0:
            return i + 1
        if self.pool.startswith(b + b'\0'):
            return 0
        o = len(self.pool)
        self.pool.extend(b + b'\0')
        assert o < 0x8000, 'text pool full'
        return o

    def export(self, name, at):
        a0, a1 = ssb_names.encode(name)
        assert ssb_names.decode(a0, a1) == name.lower(), name
        assert all(ssb_names.decode(x, y) != name.lower() for x, y, _ in self.exports), name
        self.exports.append((a0, a1, at // 4))

    def word(self, at):
        return bytes(self.code[at:at + 4])

    def build(self):
        out = bytearray(self.code) + self.blk
        table = len(out)
        for e in self.exports:
            out += struct.pack('<3I', *e)
        text = len(out)
        out += self.pool
        hdr = list(self.hdr)
        hdr[1], hdr[2], hdr[3] = table, text, text
        struct.pack_into('<8I', out, 0, *hdr)
        return bytes(out)


class Asm:
    """a small assembler: instructions with labels, resolved when placed"""

    def __init__(self, sc):
        self.sc, self.items, self.labels = sc, [], {}

    def __call__(self, *ins):
        for x in ins:                                   # nested lists of instructions are flattened
            if isinstance(x, list):
                self(*x)
            else:
                self.items.append(x)
        return self

    def label(self, name):
        self.items.append(('label', name))
        return self

    def place(self):
        at = self.sc.here()
        pos, labels = at, {}
        for it in self.items:                           # pass 1: label addresses
            if isinstance(it, tuple) and it[0] == 'label':
                labels[it[1]] = pos
            elif isinstance(it, bytes):
                pos += len(it)
            else:
                pos += 8 if it[0] == 'push32' else 4
        self.labels = labels

        def addr(t):
            return labels[t] if isinstance(t, str) else t
        out = bytearray()
        for it in self.items:                           # pass 2: words
            here = at + len(out)
            if isinstance(it, bytes):
                out += it
                continue
            k = it[0]
            if k == 'label':
                continue
            if k == 'push32':
                out += w(0x0F) + struct.pack('<I', it[1] & 0xFFFFFFFF)
            elif k in ('jmp', 'jz', 'jnz'):
                sub = {'jmp': 0, 'jz': 2, 'jnz': 1}[k]
                out += w(0x07, sub, (addr(it[1]) - (here + 4)) // 4)
            elif k == 'call':
                out += w(0x03, it[2], (addr(it[1]) - (here + 4)) // 4)
            elif k == 'case':
                out += w(0x08, it[1], (addr(it[2]) - (here + 4)) // 4)
            elif k == 'str':
                out += w(0x11, 0, self.sc.s(it[1]))
            else:
                raise ValueError(it)
        self.sc.blk += out
        return at


# instruction helpers
def p16(v):
    return w(0x0E, 0, v)


def nat(g, i, argc):
    return w(0x01, argc, g << 12 | i)


def ldl(i):
    return w(0x09, 0, i)


def stl(i):
    return w(0x0A, 0, i)


ADD, SUB, MUL, AND, PUSHRET, NOP = w(0x12), w(0x13), w(0x14), w(0x18), w(0x10), w(0x00)
EQ, LT = w(0x17, 0, 0), w(0x17, 0, 3)
SHR = w(0x1E, 0, 1)


def alloc(n):
    return w(0x06, 0, n)


def ret(drop=0, val=False):
    return w(0x05, 1 if val else 0, drop)


def S(text):
    return ('str', text)


def K(text):                                             # a string with the game's layout codes: '^' = 0x07 before each
    return S(text.replace('^', chr(7)).replace('|', chr(12)))   # code (12s, 98w, 580r, 1f, 4# ...), '|' = 0x0c column end
                                                         # (livelobby_en.ssb strings, e.g. 0x07 '12s' 0x07 '98wCharacter' 0x0c)


def VAR(i):                                              # push menu variable i
    return [p16(i), nat(2, 153, 1), PUSHRET]


def SETVAR(i):                                           # var i = pop
    return [p16(i), nat(2, 154, 2)]


def LOADER(op, *args):                                   # 2.8(op, args...) -> pushret; each arg = a list of words that
    code = []                                            # pushes one value (the loader sees args[0] = op, args[1] = the
    for a in args:                                       # last argument, ...)
        code += a if isinstance(a, list) else [a]
    return code + [p16(op), nat(2, 8, 1 + len(args)), PUSHRET]


def GET(key_code):                                       # key_code: words that push the key
    return list(key_code) + [p16(OP_GET), nat(2, 8, 2), PUSHRET]


def flat(*parts):
    out = []
    for p in parts:
        if isinstance(p, list):
            out.extend(p)
        else:
            out.append(p)
    return out


def patch_vsmenu(src):
    sc = Script(src)
    c = sc.code
    # the original words this patch replaces or calls (checked)
    assert sc.word(0x5250) == p16(7) and sc.word(0x5254) == nat(4, 857, 1), 'MENU builder'
    assert sc.word(0x5b34) == p16(1) and sc.word(0x5b38) == p16(7) and sc.word(0x5b3c) == nat(2, 97, 2), 'LOBBY builder'
    assert sc.word(0x5c48) == p16(109), 'back helper'
    assert sc.word(0x557c) == w(0x11, 0, struct.unpack_from('<H', sc.word(0x557c), 2)[0]) and sc.word(0x5580)[0] == 0x03, 'CSEL'
    assert sc.word(0x565c) == p16(4) and sc.word(0x54d0) == alloc(2) and sc.word(0x536c) == alloc(2), 'pickers'
    assert sc.word(0x599c)[0] == 0x03, 'face helper call'
    face = 0x599c + 4 + 4 * struct.unpack_from('<h', sc.word(0x599c), 2)[0]
    assert sc.word(face) == alloc(1), 'face helper %x' % face
    rule_name = 0x2dd0
    assert sc.word(0x5acc)[0] == 0x03 and 0x5acc + 4 + 4 * struct.unpack_from('<h', sc.word(0x5acc), 2)[0] == rule_name
    help14, legend, after = 0x3ba0, 0x78, 0x60
    assert sc.word(0x5be8)[0] == 0x03 and 0x5be8 + 4 + 4 * struct.unpack_from('<h', sc.word(0x5be8), 2)[0] == help14
    assert sc.word(0x5c00)[0] == 0x03 and 0x5c00 + 4 + 4 * struct.unpack_from('<h', sc.word(0x5c00), 2)[0] == legend
    assert sc.word(0x5c40)[0] == 0x03 and 0x5c40 + 4 + 4 * struct.unpack_from('<h', sc.word(0x5c40), 2)[0] == after

    def key(slot_code, field):                           # slot * 8 + field
        return flat(slot_code, [p16(8), MUL, p16(field), ADD])

    # ---- the 3 vs 3 lobby windows (rows 7, session 8, help 14) and page LOB3
    build = Asm(sc)
    if STYLE == 'live':
        # the Xbox Live lobby's layout (livelobby_en.ssb 0x3754 / 0x5020 / 0x54b0, clean 9d425dd9): LOBBY 544 wide at
        # (48, 55), Character / Players / Rating / Ping / Team, SESSION INFO Game Type / Game Rule / Stage, the help line
        # "Preparations are complete ..." and Change Settings / Back; the arsenal panel (2.164 ops 6 / 7) in between
        # v5.3.3 (owner photo 12:55: raw codes and the help over SESSION INFO): the strings carry the 0x07 / 0x0c bytes;
        # 6 rows need 50 more than the original 4 (153 at y 55): LOBBY 203 at y 48, SESSION INFO 105 at y 256 (to 361),
        # so the help window (y 374, as the original) is clear of it; no Ping column offline
        build(p16(1), p16(7), nat(2, 97, 2), p16(203), p16(544), nat(2, 100, 2), p16(48), p16(48), nat(2, 101, 2),
              p16(-1), nat(2, 116, 1), S('LOBBY'), nat(2, 102, 1),
              K('^12s^98wCharacter|^190wPlayers|^70wRating|^65w|Team'), nat(2, 104, 1))
        for _ in range(6):
            build(K('^12s^98s^190w%s|^370r%d|'), nat(2, 104, 1))
        build(S('DRAW_LOB3'), p16(514), nat(2, 118, 2),
              p16(1), p16(8), nat(2, 97, 2), p16(105), p16(544), nat(2, 100, 2), p16(256), p16(48), nat(2, 101, 2),
              p16(-1), nat(2, 116, 1), S('SESSION INFO'), nat(2, 102, 1),
              K('^128wGame Type:|%s'), nat(2, 104, 1), K('^128wGame Rule:|%s'), nat(2, 104, 1), K('^128wStage:|%s'), nat(2, 104, 1),
              S('DRAW_LOB3S'), p16(514), nat(2, 118, 2),
              p16(14), ('call', help14, 1),
              S('Preparations are complete.\nTo start the game, press the A button.'), nat(2, 104, 1),
              p16(0), nat(2, 116, 1),
              K('^580r^4#Change Settings^3#Back'), ('call', legend, 1))
    else:
        build(p16(1), p16(7), nat(2, 97, 2), p16(170), p16(543), nat(2, 100, 2), p16(54), p16(48), nat(2, 101, 2),
              S('LOBBY'), nat(2, 102, 1),
              K('^20s^100wCharacter|^120wPlayers|^170wArsenal|^60wTeam|^80wRating'), nat(2, 104, 1))
        for _ in range(6):
            build(K('^20s^100w|^120w%s|^170w%s|^60w%s|^80w%d'), nat(2, 104, 1))
        build(p16(-1), nat(2, 116, 1), S('DRAW_LOB3'), p16(514), nat(2, 118, 2),
              p16(1), p16(8), nat(2, 97, 2), p16(170), p16(543), nat(2, 100, 2), p16(240), p16(48), nat(2, 101, 2),
              S('SESSION INFO'), nat(2, 102, 1),
              K('^100wGame Type:|3 vs 3'), nat(2, 104, 1), K('^100wGame Rule:|%s'), nat(2, 104, 1),
              K('^100wStage:|%s'), nat(2, 104, 1), K('^200wSTART GAME'), nat(2, 104, 1),
              p16(-1), nat(2, 116, 1), S('DRAW_LOB3S'), p16(514), nat(2, 118, 2),
              p16(14), ('call', help14, 1),
              S('Up / Down: choose a slot or START GAME.  A: change the slot / start.'), nat(2, 104, 1),
              p16(0), nat(2, 116, 1),
              K('^8#Select ^580r^2#Confirm ^4#Change Settings ^3#Back'), ('call', legend, 1))
    build(p16(0), S('LOBBY'), p16(1), nat(2, 152, 3),
          p16(7), nat(2, 107, 1), S('LOB3'), nat(2, 76, 1),
          flat(p16(1), SETVAR(6)),
          p16(0), p16(3), p16(0), nat(2, 152, 3), ('call', after, 0), ret(0))
    B_LOB3 = build.place()

    # ---- hook: the LOBBY builder 0x5b34 (every picker comes back through it)
    hook = Asm(sc)
    hook(flat(LOADER(OP_LOBB)), ('jz', 'orig'),
         flat(VAR(V_EDIT)), ('jz', 'noedit'),                                        # a slot was being edited:
         flat(VAR(2), key(flat(VAR(V_EDIT), [p16(1), SUB]), 0), [p16(OP_SET), nat(2, 8, 3)]),   # its character
         flat(VAR(4), key(flat(VAR(V_EDIT), [p16(1), SUB]), 1), [p16(OP_SET), nat(2, 8, 3)]),   # its arsenal
         flat(p16(0), SETVAR(V_EDIT)),
         ('label', 'noedit'),
         flat(VAR(0), [p16(K_RULE), p16(OP_SET), nat(2, 8, 3)]),                      # the rule and the stage
         flat(VAR(1), [p16(K_STAGE), p16(OP_SET), nat(2, 8, 3)]),
         flat(GET([p16(0)]), SETVAR(2)), flat(GET([p16(1)]), SETVAR(4)),             # player 1's own back in var 2 / 4
         ('call', B_LOB3, 0), ret(0),
         ('label', 'orig'), p16(1), ('jmp', 0x5b38))
    H_LOBB = hook.place()
    c[0x5b34:0x5b38] = w(0x07, 0, (H_LOBB - (0x5b34 + 4)) // 4)

    # ---- hook: the MENU builder 0x5250 (fresh split-screen menu) -> the 3 vs 3 lobby when the loader asks for it
    hm = Asm(sc)
    hm(flat(LOADER(OP_VS)), ('jz', 'orig'),
       flat(GET([p16(K_RULE)]), SETVAR(0)), flat(GET([p16(K_STAGE)]), SETVAR(1)),
       flat(p16(0), SETVAR(V_EDIT)), flat(p16(0), SETVAR(V_CUR)),
       ('call', 0x5b34, 0), ret(0),
       ('label', 'orig'), p16(7), nat(4, 857, 1), ('jmp', 0x5258))
    H_MENU = hm.place()
    c[0x5250:0x5254] = w(0x07, 0, (H_MENU - (0x5250 + 4)) // 4)

    # ---- draw_lob3(x, y, row, text): row 0 the column header, rows 1-6 the slots
    d = Asm(sc)
    # locals: -1 slot, -2 word, -3 arsenal, -4 name, -5 team text
    d(alloc(5), ldl(2), ('case', 0, 'slots'),
      w(0x0F) + struct.pack('<I', 0xff123e35), p16(22), ldl(0), p16(470), ADD, ldl(1), p16(2), SUB, ldl(0), p16(20), SUB,
      nat(2, 93, 5),
      ldl(3), ldl(1), ldl(0), nat(2, 88, 3), ret(9),
      ('label', 'slots'), w(0x0D, 0, 1),                                                   # pop the row kept by case
      ldl(2), p16(1), SUB, stl(-1),
      flat(GET(key([ldl(-1)], 0))), stl(-2),
      flat(GET(key([ldl(-1)], 1))), stl(-3),
      # the row bar: the game's red, brighter on the cursor's slot
      flat(VAR(V_CUR)), ldl(-1), EQ, ('jz', 'dark'),
      ('push32', 0xff8f2a3a), ('jmp', 'bar'),
      ('label', 'dark'), ('push32', 0xff63212d),
      ('label', 'bar'), p16(23), ldl(0), p16(470), ADD, ldl(1), p16(3), SUB, ldl(0), p16(20), SUB, nat(2, 93, 5),
      ldl(1), p16(3), SUB, ldl(0), p16(13 if STYLE == 'live' else 20), ADD, ldl(-2), w(0x0F) + struct.pack('<I', 0xffff), AND,
      ('call', face, 3),
      p16(0), p16(0), p16(4), nat(2, 152, 3),
      p16(-256), p16(-256), p16(-256), p16(-256), nat(2, 91, 4),
      p16(0), p16(0), p16(0), p16(0), ldl(-2), p16(16), SHR, p16(1), ADD, S('%d'), ldl(1), p16(5), ADD, ldl(0), p16(84), ADD,
      nat(2, 87, 8),
      # the player: PLAYER 1 (you) or CPU n
      ldl(-1), ('jnz', 'cpu'), flat(GET(key([ldl(-1)], 3))), ('jnz', 'cpu'),
      S('PLAYER 1'), stl(-4), ('jmp', 'named'),
      ('label', 'cpu'), ldl(-1))
    names = ['CPU 1', 'CPU 2', 'CPU 3', 'CPU 4', 'CPU 5', 'CPU 6']
    for k in range(6):
        d(('case', k, 'n%d' % (k + 1)), w(0x0D, 0, 1) if False else NOP, S(names[k]), stl(-4), ('jmp', 'named'),
          ('label', 'n%d' % (k + 1)))
    d(w(0x0D, 0, 1), S('CPU'), stl(-4),
      ('label', 'named'),
      flat(GET(key([ldl(-1)], 2))), ('jnz', 'teamb'), S('A'), stl(-5), ('jmp', 'teamed'),
      ('label', 'teamb'), S('B'), stl(-5),
      ('label', 'teamed'))
    if STYLE == 'live':                                  # name + rating, the team letter in the gold tint
        d(p16(0), p16(0), p16(0), flat(GET(key([ldl(-1)], 4))), ldl(-4), ldl(3), ldl(1), ldl(0), nat(2, 87, 8),
          ('push32', 0xffd8b918), ('push32', 0xffd8b918), ('push32', 0xffd8b918), ('push32', 0xffd8b918), nat(2, 91, 4),
          p16(0), p16(0), p16(0), p16(0), ldl(-5), K('^1f%s'), ldl(1), ldl(0), p16(470), ADD, nat(2, 87, 8),
          p16(-256), p16(-256), p16(-256), p16(-256), nat(2, 91, 4), ret(9))
    else:
        d(p16(0), flat(GET(key([ldl(-1)], 4))), ldl(-5),
          p16(0), ldl(-3), p16(34), nat(2, 149, 3), PUSHRET,                              # the arsenal's name
          ldl(-4), ldl(3), ldl(1), ldl(0), nat(2, 87, 8), ret(9))
    D_LOB3 = d.place()
    sc.export('draw_lob3', D_LOB3)

    # ---- draw_lob3s(x, y, row, text): 0 game type, 1 rule, 2 stage, 3 START GAME (a bar under it on the cursor)
    ds = Asm(sc)
    ds(alloc(1), ldl(2),
       ('case', 0, 'r1'), S('3 vs 3' if STYLE == 'live' else ''), stl(-1), ('jmp', 'out'),
       ('label', 'r1'), ('case', 1, 'r2'), flat(VAR(0)), ('call', rule_name, 1), PUSHRET, stl(-1), ('jmp', 'out'),
       ('label', 'r2'), ('case', 2, 'r3'), flat(VAR(1)), p16(51), nat(2, 151, 2), PUSHRET, stl(-1), ('jmp', 'out'),
       ('label', 'r3'), w(0x0D, 0, 1),
       flat(VAR(V_CUR)), p16(6), EQ, ('jz', 'nobar'),
       ('push32', 0xff8f2a3a), p16(23), ldl(0), p16(470), ADD, ldl(1), p16(3), SUB, ldl(0), p16(20), SUB, nat(2, 93, 5),
       ('label', 'nobar'), S(''), stl(-1),
       ('label', 'out'),
       p16(0), p16(0), p16(0), p16(0), ldl(-1), ldl(3), ldl(1), ldl(0), nat(2, 87, 8), ret(5))
    D_LOB3S = ds.place()
    sc.export('draw_lob3s', D_LOB3S)

    # ---- OPTIONS for the cursor's slot (window 12, the game's look: items + DRAW_LOPT), page LOP3
    op = Asm(sc)
    if STYLE == 'live':                                  # the Xbox Live OPTIONS window (livelobby 0x3688): rows, cursor
        op(p16(1), p16(12), nat(2, 97, 2), p16(105), p16(256), nat(2, 100, 2), p16(109), p16(56), nat(2, 101, 2),
           S('OPTIONS'), nat(2, 102, 1),
           S('Change Character'), nat(2, 104, 1), S('Change Arsenal'), nat(2, 104, 1), S('Change Team'), nat(2, 104, 1),
           S('Change Player (You / CPU)'), nat(2, 104, 1), S('Change Stage'), nat(2, 104, 1), S('Change Rule'), nat(2, 104, 1),
           S('Close Game Session'), nat(2, 104, 1), p16(0), nat(2, 116, 1),
           S('DRAW_LOPT'), p16(514), nat(2, 118, 2),
           K('^8#Select ^580r^2#Confirm ^3#Back'), ('call', legend, 1),
           p16(12), nat(2, 107, 1), S('LOP3'), nat(2, 76, 1),
           p16(0), p16(3), p16(0), nat(2, 152, 3), ('call', after, 0), ret(0))
        B_LOP3 = op.place()
    else:
        op(p16(1), p16(12), nat(2, 97, 2), p16(170), p16(256), nat(2, 100, 2), p16(4097), nat(2, 125, 1),
           p16(108), p16(56), nat(2, 101, 2), flat(VAR(V_CUR)))
        titles = ['OPTION [PLAYER 1]', 'OPTION [CPU 2]', 'OPTION [CPU 3]', 'OPTION [CPU 4]', 'OPTION [CPU 5]', 'OPTION [CPU 6]']
        for k in range(6):
            op(('case', k, 't%d' % k), S(titles[k]), nat(2, 102, 1), ('jmp', 'titled'), ('label', 't%d' % k))
        op(w(0x0D, 0, 1), S('OPTION'), nat(2, 102, 1), ('label', 'titled'),
       S('Change Character'), p16(0), nat(2, 123, 2), S('Change Arsenal'), p16(1), nat(2, 123, 2),
       S('Change Team'), p16(2), nat(2, 123, 2), S('Change Player (You / CPU)'), p16(3), nat(2, 123, 2),
       S('Change Stage'), p16(4), nat(2, 123, 2), S('Change Rule'), p16(5), nat(2, 123, 2),
       S('Close'), p16(6), nat(2, 123, 2),
       S('DRAW_LOPT'), p16(514), nat(2, 118, 2),
       p16(14), ('call', help14, 1),
       S('Choose a different character.'), nat(2, 104, 1), S('Choose a different arsenal.'), nat(2, 104, 1),
       S('Team A or team B.'), nat(2, 104, 1), S('Slot 1: you or a CPU (all CPUs: you watch).'), nat(2, 104, 1),
       S('Choose a different stage.'), nat(2, 104, 1), S("Choose a different rule (your Edgar's Dream modes too)."),
       nat(2, 104, 1), S('Back to the lobby.'), nat(2, 104, 1),
       K('^8#Select ^580r^2#Confirm ^3#Back'), ('call', legend, 1),
       p16(12), nat(2, 107, 1), S('LOP3'), nat(2, 76, 1),
           p16(0), p16(3), p16(0), nat(2, 152, 3), ('call', after, 0), ret(0))
        B_LOP3 = op.place()

    def handler(name, *ins):
        a = Asm(sc)
        a(*ins)
        sc.export(name, a.place())

    # LOB3 page: frame (cursor, A), B, X
    last = 5 if STYLE == 'live' else 6                  # live: the cursor stays on the 6 slots, A starts
    panel = (flat(p16(0), p16(0), GET(key(VAR(V_CUR), 1)), p16(0), p16(6), nat(2, 164, 5),     # the slot's arsenal
                  p16(0), p16(0), p16(PANEL_Y), p16(48), p16(7), nat(2, 164, 5)) if STYLE == 'live' else [])
    handler('mlob3', alloc(1), p16(0), nat(1, 37, 1), PUSHRET, stl(-1),             # pad 1's new presses
            ldl(-1), p16(1), AND, ('jz', 'down'),                                          # up
            flat(VAR(V_CUR)), ('jnz', 'up1'), p16(last), SETVAR(V_CUR)[0], SETVAR(V_CUR)[1], ('jmp', 'down'),
            ('label', 'up1'), flat(VAR(V_CUR)), p16(1), SUB, SETVAR(V_CUR)[0], SETVAR(V_CUR)[1],
            ('label', 'down'), ldl(-1), p16(2), AND, ('jz', 'a'),
            flat(VAR(V_CUR)), p16(last), EQ, ('jz', 'dn1'), p16(0), SETVAR(V_CUR)[0], SETVAR(V_CUR)[1], ('jmp', 'a'),
            ('label', 'dn1'), flat(VAR(V_CUR)), p16(1), ADD, SETVAR(V_CUR)[0], SETVAR(V_CUR)[1],
            ('label', 'a'), ldl(-1), p16(4112), AND, ('jz', 'ev'),
            p16(0), nat(1, 43, 1), p16(0), p16(5), nat(2, 73, 2),
            ('label', 'ev'), panel, S('aLOB3'), nat(2, 70, 1), ret(1))
    handler('dlob3', ('call', 0x20, 0), nat(2, 89, 0), p16(7), nat(2, 106, 1), p16(8), nat(2, 106, 1),
            p16(14), nat(2, 106, 1), ret(0))
    handler('alob3_ta', *(flat(VAR(V_CUR), p16(6), EQ, ('jz', 'slot')) if STYLE != 'live' else []),
            flat(LOADER(OP_START, VAR(0), VAR(1))), ('jnz', 'go'),                        # START GAME (rule, stage)
            p16(2), nat(1, 43, 1), ret(0),                                                  # refused: the game's error sound
            ('label', 'go'), p16(260), nat(2, 22, 1), p16(-1), nat(2, 107, 1), p16(0), nat(2, 76, 1), p16(1), nat(2, 64, 1), ret(0),
            ('label', 'slot'), ('call', B_LOP3, 0), ret(0))
    handler('alob3_tx', flat(VAR(V_CUR)), p16(6), EQ, ('jz', 'slot'), p16(0), SETVAR(V_CUR)[0], SETVAR(V_CUR)[1],
            ('label', 'slot'), p16(0), nat(1, 43, 1), ('call', B_LOP3, 0), ret(0))
    handler('alob3_tb', flat(LOADER(OP_LEAVE)), w(0x0D, 0, 1),                            # back to the main menu
            ('call', 0x5c48, 0), p16(-1), nat(2, 107, 1), p16(0), nat(2, 67, 1), p16(2), nat(2, 85, 1),
            ('label', 'w'), p16(2), nat(2, 86, 1), PUSHRET, ('jmp', 'w') if False else w(0x07, 3, -4),
            p16(5), nat(2, 64, 1), ret(0))
    # LOP3 page: frame, close, items
    handler('mlop3', p16(12), p16(14), nat(2, 117, 2), S('aLOP3'), nat(2, 70, 1), ret(0))
    handler('dlop3', ('call', 0x20, 0), p16(7), nat(2, 106, 1), p16(8), nat(2, 106, 1), nat(2, 89, 0),
            ('call', 0x38, 0), p16(12), nat(2, 106, 1), p16(14), nat(2, 106, 1), ret(0))
    edit = flat(VAR(V_CUR), [p16(1), ADD], SETVAR(V_EDIT),                                   # the slot into player 1's vars
                GET(key(VAR(V_CUR), 0)), SETVAR(2), GET(key(VAR(V_CUR), 1)), SETVAR(4))
    handler('alop3_s0', *edit, ('call', 0x557c, 0), ret(0))                                  # the game's character select
    handler('alop3_s1', *edit, ('call', 0x565c, 0), ret(0))                                  # its arsenal select
    flip = lambda field: flat(p16(1), GET(key(VAR(V_CUR), field)), [SUB], key(VAR(V_CUR), field),
                              [p16(OP_SET), nat(2, 8, 3)])
    handler('alop3_s2', *flip(2), ('call', 0x5b34, 0), ret(0))                               # team A / B
    handler('alop3_s3', *flat(VAR(V_CUR)), ('jnz', 'no'), *flip(3), ('label', 'no'), ('call', 0x5b34, 0), ret(0))
    handler('alop3_s4', ('call', 0x54d0, 0), ret(0))                                          # the stage list
    handler('alop3_s5', ('call', 0x536c, 0), ret(0))                                          # the rule list
    if STYLE == 'live':                                                                       # Close Game Session
        handler('alop3_s6', flat(LOADER(OP_LEAVE)), w(0x0D, 0, 1), ('call', 0x5c48, 0), p16(-1), nat(2, 107, 1), p16(0),
                nat(2, 67, 1), p16(2), nat(2, 85, 1), p16(2), nat(2, 86, 1), PUSHRET, w(0x07, 3, -4),
                p16(5), nat(2, 64, 1), ret(0))
    else:
        handler('alop3_s6', ('call', 0x5b34, 0), ret(0))                                      # Close
    handler('alop3_tb', ('call', 0x5b34, 0), ret(0))
    out = sc.build()
    return out, {'start': sc.base, 'table': hex(struct.unpack_from('<I', out, 4)[0]), 'lob3': hex(B_LOB3), 'hook_lobby': hex(H_LOBB), 'hook_menu': hex(H_MENU), 'draw': hex(D_LOB3),
                 'draws': hex(D_LOB3S), 'options': hex(B_LOP3), 'face': hex(face), 'added': len(sc.blk)}


def patch_mainmenu(src):
    sc = Script(src)
    assert hashlib.sha256(src).hexdigest().startswith(MM_SHA), 'not the expected live mainmenu_en.ssb (%s)' % MM_SHA
    o_main = struct.unpack_from('<H', sc.word(0xc38), 2)[0]
    assert sc.word(0xc38) == w(0x11, 0, o_main) and sc.word(0xc3c) == nat(2, 70, 1), 'mmain events'
    assert sc.word(0xd40) == p16(0) and sc.word(0xd98) == p16(5) and sc.word(0xd9c) == nat(2, 64, 1), 'alive_s1'
    assert sc.word(0x650) == p16(110) and sc.word(0x654) == nat(2, 123, 2), 'MULTIPLAYER MODE = item 110 of window 1'
    o_live = struct.unpack_from('<H', sc.word(0x12b0), 2)[0]
    assert sc.word(0x12b0) == w(0x11, 0, o_live) and sc.word(0x12b4) == nat(2, 70, 1), 'mlive events'
    # v5.3.1 (owner: "it took me back to the main menu and then the game froze" - 5.3 ran Split Screen's handler inside the
    # MAIN page's frame, where its fade wait never finished): the items are 'pressed' as events instead, 2.73(event,
    # window) = the input routine's own pending event (menu +0x10414 / +0x10418, FUN_1402071d0); the page's next 2.70
    # runs the handler: MAIN item 110 (event 210, window 1) -> amain_s110 -> the MULTIPLAYER list; its row 1 (event 101,
    # window 2) -> alive_s1 Split Screen
    a = Asm(sc)
    a(flat(LOADER(OP_MAIN)), ('jz', 'main'), p16(1), p16(210), nat(2, 73, 2),
      ('label', 'main'), w(0x11, 0, o_main), nat(2, 70, 1), ret(0))
    at = a.place()
    sc.code[0xc38:0xc40] = w(0x03, 0, (at - (0xc38 + 4)) // 4) + NOP
    b = Asm(sc)
    b(flat(LOADER(OP_LIVE)), ('jz', 'live'), p16(2), p16(101), nat(2, 73, 2),
      ('label', 'live'), w(0x11, 0, o_live), nat(2, 70, 1), ret(0))
    at2 = b.place()
    sc.code[0x12b0:0x12b8] = w(0x03, 0, (at2 - (0x12b0 + 4)) // 4) + NOP
    return sc.build(), {'start': sc.base, 'check': hex(at), 'check_live': hex(at2)}


def validate(data, name, start, sites):
    """the added code (start .. export table) and the patched sites decode cleanly; every branch / call / pushaddr lands on
    an instruction of the added code or inside the original code"""
    hdr = struct.unpack_from('<8I', data, 0)
    ins, bad = ssb_disasm.decode(data[start:hdr[1]], start)
    assert not bad, '%s: %d invalid words in the added code, first at %x' % (name, len(bad), bad[0]['off'])
    starts = {i['off'] for i in ins}
    for at in sites:
        i = ssb_disasm.decode_one(data, at)
        ins.append(i)
    for i in ins:
        t = i.get('target')
        if t is None:
            continue
        assert 0x20 <= t < hdr[1], '%s: branch at %x leaves the code (%x)' % (name, i['off'], t)
        if t >= start:
            assert t in starts, '%s: branch at %x lands inside an instruction (%x)' % (name, i['off'], t)
    return len(ins)


if __name__ == '__main__':
    if len(sys.argv) > 5:
        STYLE = sys.argv[5]
    vs, mm = open(sys.argv[1], 'rb').read(), open(sys.argv[2], 'rb').read()
    vs_out, r1 = patch_vsmenu(vs)
    mm_out, r2 = patch_mainmenu(mm)
    n1 = validate(vs_out, 'vsmenu', r1['start'], [0x5250, 0x5b34])
    n2 = validate(mm_out, 'mainmenu', r2['start'], [0xc38, 0x12b0])
    open(sys.argv[3], 'wb').write(vs_out)
    open(sys.argv[4], 'wb').write(mm_out)
    print('vsmenu', hashlib.sha256(vs_out).hexdigest()[:8], r1, n1, 'instructions')
    print('mainmenu', hashlib.sha256(mm_out).hexdigest()[:8], r2, n2, 'instructions')
