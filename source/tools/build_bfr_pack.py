"""Builds the BFR mod pack (owner 2026-11-01: "It's gonna be called BFR"; selection in
26_Public_Release/BFR_MOD_PACK_SELECTION_2026-11-01.md; "this phantom dust mod must be private github repo and place it on the
desktop just liek the other phantom dust mods"; AGENTS.md: the pack is prepared "without the game's own files (patches
applied on each player's install)" and the owner publishes it himself).

The pack folder (a local git repository the owner pushes to his private GitHub repository):
  launcher/CwoLaunch.exe, launcher/CwoLoader.dll   the launcher and loader (the shortcut starts them with --edition BFR)
  data/CwoBFR.txt                                  the Battle for Reality skill pack (from the owner's Omni Arsenal Lab)
  data/CwoNextMap.txt                              Next Map, off until the host switches it on in the lobby
  data/patches/<game path>.cwopatch                the changed menu scripts as patches against the clean game's files;
                                                   the loader builds them on the player's machine (cwo_patchfiles.inl)
  INSTALL BFR.bat / UNINSTALL BFR.bat, install_bfr.ps1 / uninstall_bfr.ps1, README.md, MANIFEST.json, VERSION
No file of the game is in the pack: a patch holds only our changed bytes plus the SHA-256 of the clean file it applies to.
Usage: python -B build_bfr_pack.py OUT_DIR
"""
import hashlib
import json
import os
import shutil
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
import pd_paths  # noqa: E402
CLEAN = pd_paths.GAME
MENUS = os.path.join(ROOT, 'packs', 'bfr_menus')
VERSION = '1.50.73'
PARITY = os.path.join(ROOT, 'packs', 'parity_BFR_2026-11-11')      # 1.49 build_data_parity.py output (the installed BFR data folder)
# (game path, our result, its SHA-256 prefix, what it carries); since 1.42.8 every result is built on the clean game file
# (livelobby, mainmenu and vsmenu moved off the older community versions with tools/rebase_ssb.py)
PATCHES = [
    (r'Assets\Data\Menu\livelobby_en.ssb', 'livelobby_en_optup.ssb', 'de9423cb',   # 1.50.32 OPTIONS window (window 4) y 109 -> 50 (build_live_options_up.py)
       # 1.50.10 pd-pack-sync: BFR OPTIONS with 8 rows again (build_bfr_rows8.py = the 11-07 file)
     'Xbox Live / system link lobby: 3v3 layout, Edit Arsenal and lists 1-3, Change Rule with Most KOs, First to 5 KOs and '
     'Survival (3 revives), Change Game Type, session wins and losses, BFR Options (Teammate Lock-on, Next Map, Tiebreaker Vote); '
     '1.46 the members\' face strips follow their outfits'),
    (r'Assets\Data\Menu\laboratory_en.ssb', 'laboratory_en_vault_og.ssb', '7915e0ed',   # 1.50.40 switch text (375+)
     'Lab: Enable Battle for Reality skill pack (375+) / Revert back to OG Phantom Dust skills (0-374); 1.48 View Skill Vault '
     '(build_lab_vault.py on the 1.47 Lab script)'),
    (r'Assets\Data\Menu\arsenaledit_en.ssb', 'arsenaledit_en.ssb', 'a371fe31',
     'arsenal list title with its number; ARSENAL OPTIONS: Add Arsenal, Erase Arsenal, Reset All Arsenals, Acquire All Skills;'
     ' Arsenal Data: Y picks the stage of the Xbox Live results'),
    (r'Assets\Data\Menu\livearsel_en.ssb', 'livearsel_en_made.ssb', '50b92aa6', 'Xbox Live arsenal list title with its number; 1.50.10: an arsenal holding a created skill is refused online (the MODS file, same base)'),
    (r'Assets\Data\Menu\option_en.ssb', 'option_en_more.ssb', 'b9a0b5ee',   # 1.50.12 pd-pack-sync: + rows 7-9 (build_option_more.py on option_en_hud ae5168de)
     'main menu OPTIONS: HUD Preset, the game\'s own sub-menu with a PREVIEW window (1.45.2, build_hud_option.py v2); 1.50.12 '
     'Records, Visions and Stage Music (build_option_more.py)'),
    (r'Assets\Data\Menu\mainmenu_en.ssb', 'mainmenu_en_create_fb8.ssb', '7d2784f0',   # 1.50.14: all 8 rows drawn (build_mainmenu_rows.py --draw 8 32)
       # 1.50.13 pd-pack-sync: build_mainmenu_single_create.py + build_mainmenu_feedback.py on mainmenu_en_modlist 82fc62e0
     "main menu: EDGAR'S DREAM, MOD LIST (1.43, build_modlist_menu.py) and the BFR MOD PACK line; 1.50.13 EDGAR'S DREAM is CREATE, "
     'MULTIPLAYER > Single Play, FEEDBACK and TO-DO LIST'),
    (r'Assets\Data\Menu\pausemenu_en.ssb', 'pausemenu_en_lockon.ssb', 'a48302a2',   # 1.50.13 pd-pack-sync (build_pause_lockon.py on the CLEAN file; the MODS copy carries a debug menu)
     'pause menu (offline match / 2P VS): Teammate Lock-on row, A turns it on / off'),
    (r'Assets\Data\Menu\vsmenu_en.ssb', 'vsmenu_en_bfr_v3.ssb', '36c861cc',   # 1.50.51 split-screen lobby OPTIONS > BFR Options (build_vs_bfr_options.py)
       # 1.50.20 NPC cell locked too + compact stage list (build_select_skip.py --both, build_compact_list.py --pack-loop 0x8eb8)
       # 1.50.16 the monster page's COMING SOON cell cannot be highlighted (build_select_skip.py)
       # 1.50.10 pd-pack-sync: COLORS shows 11 rows and scrolls (build_colors_rows.py at 0xea4)
     'split screen: 3 VS 3 LOBBY, Edit Arsenal and lists, Change Rule with the Create a Mode modes, game type, stage view, '
     "arsenal list title, the extra stage rows (Edgar's Dream: Summit); 1.43: monster portraits on X, COMING SOON, "
     'the portrait follows the highlighted COLORS outfit; 1.45.6: COLORS 140 wide, the loader moves it into the left black bar'),
    (r'Assets\Data\com\single2.csv', 'single2.csv', '8861ca9f', "Single Play's first opponent: the practice dummy"),
    (r'Assets\Data\Menu\livecharasel_en.ssb', 'livecharasel_en_bar2.ssb', '46a107ca',
     'Xbox Live COLORS: 1.43 monster portraits on X, the portrait follows the highlighted outfit; 1.45.6 COLORS moved into the left black bar'),
    (r'Assets\Data\Menu\VsMode.alr', 'VsMode_col.alr', 'cd59b32e',
     'split-screen select: new player faces, the monster portraits, the outfit cell (1.43); 1.46 the lobby row cells; 1.47 the lobby portraits in colour'),
    (r'Assets\Data\Menu\MNet.alr', 'MNet_col.alr', 'abbecb73',
     'Xbox Live select: new player faces, the monster portraits, the outfit cell (1.43; display only, AGENTS.md exception); 1.46 the lobby row cells; 1.47 the lobby portraits in colour'),
    (r'Assets\Data\Menu\liverulesel_en.ssb', 'liverulesel_en_bfr.ssb', '80a2cb73',
     'Create Game Session: the BFR rules after Annihilate the Odd One (King of the Hill, Shrinking Arena, Aura Rush, Sudden Death, Realistic Collision ...) (1.47)'),
    (r'Assets\Data\Menu\Mission.alr', 'Mission_portraits.alr', 'e4c06aac',
     'mission select: the new character portraits (1.47)'),
    (r'Assets\Data\Menu\livestagesel_en.ssb', 'livestagesel_en_compact.ssb', '6130295a',   # 1.50.26 one row per family online (build_compact_list.py --exit-jz 0x1670 --pack-loop 0x1a20)
    
     'Xbox Live level list: the highlighted stage shown behind the list; 1.50.3 the added stages as rows (build_stage_rows.py '
     'on the 1.50 script, the same patch as MODS - AGENTS.md "Pack rows in the Xbox Live LEVEL LIST")'),
    (r'Assets\Data\Menu\BtMenuEn.alr', 'BtMenuEn_faces6.alr', '5cae6067',
     "battle HUD: face spots for each fighter's face beside its name - 1.50.15: 6 (fighters 5 and 6 of a 3 vs 3 too; MODS build_hud_faces v3)"),
    # 1.44 (owner 2026-11-02: "everybody has like 4 outfits"): the loader's in-memory raise to 15 never ran in the BFR
    # edition; the character table itself now says 15 for every playable and enemy fighter (row +0x3A, 27 rows)
    # 1.50: charaset_en.dat comes from packs/bfr_150 (the MODS table: these same 15-outfit rows + the NPC page rows)
]
# v1.42.16 (owner 2026-11-02 "I confirm"): the HUD face pictures (renders of each fighter, 28 x 23 states) go with the pack
FACE_FILES = [(r'Assets\Data\Menu\cwo_faces.dxt', os.path.join(ROOT, 'packs', 'hud_faces_2026-11-14_v7', 'cwo_faces.dxt')),
              (r'Assets\Data\Menu\cwo_faces.txt', os.path.join(ROOT, 'packs', 'hud_faces_2026-11-14_v7', 'cwo_faces.txt')),
              # 1.43: the select portraits per outfit (11 players x 15 outfits) and where their cell is in the pack's images
              (r'Assets\Data\Menu\cwo_select_outfits.dxt', os.path.join(ROOT, 'packs', 'select_atlas_bfr_2026-11-04', 'cwo_select_outfits.dxt')),
              (r'Assets\Data\Menu\cwo_select_outfits.txt', os.path.join(ROOT, 'packs', 'select_atlas_bfr_2026-11-04', 'cwo_select_outfits.txt')),
              # 1.46: the lobby face strips per outfit (build_lobby_strips.py) and the row cells in the pack's images
              (r'Assets\Data\Menu\cwo_lobby_rows.txt', os.path.join(ROOT, 'packs', 'lobby_rows_2026-11-03', 'bfr_cwo_lobby_rows.txt'))]
# new game files made from OTHER game files of the player (target, source, our result, SHA-256 prefix, what); the loader
# builds them from <target>.cwopatch + <target>.cwofrom (v5.64.3)
SUMMIT = os.path.join(MENUS, 'mx401_summit.dat')
DERIVED = [
    (r'Assets\Data\Map\mx401.dat', r'Assets\Data\Map\st01.dat', SUMMIT, '7d60f667',   # 1.50.29 own capsule spots per start + 8 starts on the ring (summit_capsules.py)
    
     "Edgar's Dream: Summit - the top of the stairs (ruin pieces out, 8 starts on the summit ring, each with its own 3 capsule spots)"),
    (r'Assets\Data\Map\mx401.alr', r'Assets\Data\Map\st01.alr', None, None, "Edgar's Dream: Summit - Edgar's Dream's own model"),
    (r'Assets\Data\Player\pc09a\pc09a3.alr', r'Assets\Data\Player\pc09a\pc09a0.alr', None, None, "JD colour 4: JD's own model (the game's colour 4 is Mac) (1.47.6)"),
    (r'Assets\Data\Map\st00cam.alr', r'Assets\Data\Map\st02cam.alr', None, None, 'Map Slot 7 (stage 10): its camera file'),
    (r'Assets\Data\com\st00\camdata.cam', r'Assets\Data\com\st02\camdata.cam', None, None, 'Map Slot 7: CPU data'),
    (r'Assets\Data\com\st00\comdata0.cad', r'Assets\Data\com\st02\comdata0.cad', None, None, 'Map Slot 7: CPU data'),
    (r'Assets\Data\com\st00\comdata1.cad', r'Assets\Data\com\st02\comdata1.cad', None, None, 'Map Slot 7: CPU data'),
    # 1.44: the Preview Stage (build_preview_stage.py): stage 32's grid room, the four starts on a ring in the middle
    (r'Assets\Data\Map\mx402.dat', r'Assets\Data\Map\st32.dat', os.path.join(MENUS, 'mx402_preview_caps.dat'), 'b842d30c',   # 1.50.11 pd-pack-sync: the MODS file (capsule spots raised)
     'Preview Stage - the grid room of stage 32, the four starts on a ring in the middle (to show characters)'),
    (r'Assets\Data\Map\mx402.alr', r'Assets\Data\Map\st32.alr', None, None, "Preview Stage - stage 32's own model"),
    # 1.50.34 Edgar's new outfits (owner 2026-11-15: "replace Edgar's outfit 4 ... entirely new outfits for outfits 6 through 15")
    (r'Assets\Data\Player\pc01a\pc01a3.alr', r'Assets\Data\Player\pc01a\pc01a0.alr', os.path.join(ROOT, 'packs', 'edgar_outfits_maps_2026-11-15', 'pc01a3.alr'), '36bbcb5f',
     "Edgar outfit 4: Royal Navy (1.50.34, edgar_outfits.py)"),
]
# 1.50.38 (owner 2026-11-15/16: "out of 15, we want 6 to 15 to be based on the maps" - for every character): outfits 6-15 of the
# other playable characters from the maps' own looks (map_outfits.py v2, head / hair of each atlas audit kept)
_MO = os.path.join(ROOT, 'packs', 'theme_outfits_2026-11-16')   # 1.50.39: colour themes for every character + enemy
_MAPS = ['Crimson', 'Ember Orange', 'Goldenrod', 'Forest Green', 'Turquoise', 'Cyan', 'Royal Blue', 'Violet', 'Magenta', 'Midnight LED']
for _st in sorted(os.listdir(_MO)) if os.path.isdir(_MO) else []:
    for _k in range(5, 15):
        _f = os.path.join(_MO, _st, '%s%d.alr' % (_st, _k))
        if os.path.isfile(_f):
            DERIVED.append((r'Assets\Data\Player\%s\%s%d.alr' % (_st, _st, _k), r'Assets\Data\Player\%s\%s0.alr' % (_st, _st), _f,
                            hashlib.sha256(open(_f, 'rb').read()).hexdigest()[:8], '%s outfit %d: %s (1.50.39 colour themes)' % (_st, _k + 1, _MAPS[_k - 5])))
MX_LIST = "mx401\tEdgar's Dream: Summit\t1\t4\nmx402\tPreview Stage\t1\t4\n"
# 1.49.1 pack parity: the owner's own BFR stages (installed with the Stage Bridge's "Install a stage", 2026-11-05 / 06),
# dropped from the list by the 1.48.0 install - rows as the owner's list had them. RoofTop Day (mx305) stays out: it
# carries another game's assets (other games' assets never go in the pack).
MX_LIST += "mx300\tThe Bunker\t3\t6\n"     # 1.50: mx301 / mx302 = the 2026-11-12 remakes, their rows from packs/bfr_150
# 1.48 (owner 2026-11-11: "the mods that I want you to add are all the mods that we've currently went through today"): the
# 2026-11-10 stages, rows as in MODS (name, floors, spawns, height column)
MX_LIST += ("mx330\tPalace Ascended\t3\t12\t-150.1\nmx331\tSky Ring Highway\t3\t6\t200.0\nmx332\tHighway to the Alley\t3\t6\t150.0\n"
            "mx333\tJungle Cave\t2\t6\nmx334\tFive-Story Building\t5\t6\nmx335\tRuins\t3\t6\nmx336\tAntarctic Outpost\t2\t6\t-50.0\n")
# their files as patch format 2 (a new file from up to 8 of the player's own game files; loader 5.149.0+), made by
# build_mods_pack.py --only from the installed MODS files (each checked there by applying it to the clean files)
# 1.50.4 (owner 2026-11-12: copy what MODS has onto BFR): the stages as installed in MODS (tools/make_bfr_stages.py)
STAGE_PATCHES = os.path.join(ROOT, 'packs', 'bfr_stages_2026-11-16')    # 1.50.50: Grand Panorama (Sunset) capsules away from the lifts (caps_clear_lifts.py)    # 1.50.33: + The Summit mx403 (make_bfr_stages_1115.py)
# was bfr_stages_2026-11-14    # 1.50.20: + RoofTop Day mx337, the Highway to the Alley start (make_bfr_stages_1114.py)
#    # 1.50.7: make_bfr_stages_1113.py (MODS stages + twin order)
STAGE_IDS = ('mx331', 'mx332', 'mx333', 'mx334', 'mx335', 'mx336', 'mx301', 'mx302', 'mx337', 'mx403', 'mx329')
MX_LIST += 'mx337\tRoofTop Day\t6\t8\t140.0\n'
MX_LIST += 'mx403\tThe Summit\t3\t8\t60.0\n'     # 1.50.33 (owner 2026-11-15: Sein + the Summit as one stage, a Sein version in BFR)     # 1.50.20 (owner 2026-11-14: "why ... are we missing the rooftop map")
MX_LIST += 'mx329\tDriftstone Sanctum\t3\t4\t30.0\n'     # 1.50.35 (owner 2026-11-15: the low-gravity stage in BFR)
# 1.50.7 (owner 2026-11-13: "completely remove Palace Ascended" / "Delete the bunker as well"): out of the list and the pack
DELETED = ('mx300', 'mx330')
# 1.50.7 (owner 2026-11-13, skill pd-pack-sync: "always move the mod pack stuff that's up to date from Phantom Dust mods onto
# the BFR mod pack"): the newer MODS data files, applied after every other source
SYNC_DATA = os.path.join(ROOT, 'packs', 'bfr_sync_2026-11-13')
# 1.49.1 (build_bfr_extra_2026_11_11.py): The Bunker, Grand Panorama, Grand Panorama (Sunset) and Edgar's outfit 6 (the
# owner's own saved outfit) as patch format 2, each checked against the clean files and for other games' textures
EXTRA_PATCHES = os.path.join(ROOT, 'packs', 'bfr_stages_extra_2026-11-11')
# 1.50 (owner 2026-11-12, tools/build_bfr_150.py): Grand Panorama remakes (replace 1.49.1's mx301 / mx302), test and piece
# stages, NPC page, CPU levels, the MODS character table, Edgar's Dream data, new skills, the Bunker / Summit sound fix
P150 = os.path.join(ROOT, 'packs', 'bfr_150_2026-11-12')
P150_MAN = (json.load(open(os.path.join(P150, 'MANIFEST.json'))) if os.path.isfile(os.path.join(P150, 'MANIFEST.json'))
            else {'rows': '', 'files': {}, 'patches': []})          # (build_bfr_150.py imports this file before it exists)
MX_LIST += P150_MAN['rows']
REPLACED_150 = {p['file'].lower() for p in P150_MAN['patches']}
# data made from the MODS edition's files (only the 2026-11-10 mods): 1.48.1 NO new skills (owner: "I have to fully test those"; header-only files; was: Mirror at 467, the id
# its players already have; the Skill Creator test skills stay out), the new stages' effects and sounds, 60 FPS, Mega
# Photon Burst's size
BFR_DATA = os.path.join(ROOT, 'packs', 'bfr_data_2026-11-11')
DATA_FILES = [('CwoBFR.txt', os.path.join(ROOT, 'packs', 'bfr_skillpack_og', 'CwoBFR.txt'), 'd3c6ff9c')] + [
    (n, os.path.join(BFR_DATA, n), None) for n in ('CwoNewSkills.txt', 'CwoStageFx.txt', 'CwoStageSound.txt', 'CwoFps.txt', 'CwoFxSize.txt')]
# 1.50.1: the BFR loader stays 5.151.0 (the MODS edition's 5.152 work waits for the owner's test); the launcher is new (updater)
# 1.50.2: loader 5.151.1 = 5.151.0 + the owner's BFR test fixes only (level-list crash, packs without files, start-up volume)
# 1.50.71 (owner 2026-11-17): the launcher and loader are built from the PUBLIC source (tools/export_public_loader.py ->
# 26_Public_Release/BFR_source_staging/source/loader, its own build.bat), so the published source is exactly what players run
PUBLIC_BUILD = os.path.join(ROOT, 'packs', 'bfr_loader', 'public_5.188.41')
LAUNCHER = [('CwoLaunch.exe', os.path.join(PUBLIC_BUILD, 'CwoLaunch.exe')),
            ('CwoLoader.dll', os.path.join(PUBLIC_BUILD, 'CwoLoader.dll'))]   # 1.50.40 skill update version 144   # 1.50.36 the host's stage pack goes to the players who join   # 1.50.35 always 3 starting capsules   # 1.50.34 records per profile, OPTIONS pages back to OPTIONS, CPU routes per stage, King of the Hill 4 markers   # 1.50.33 The Summit folded into the Sein row (mx401 + mx403)   # 1.50.26 Xbox Live LEVEL LIST variants   # 1.50.25 locked skills only in the Skill Vault   # 1.50.23 stage rows (i/n), crash shield site 3, late refill on the game thread   # 1.50.22 sound guard v2 (junk bank pointers too - Sammira crashed on 5.186.2)   # 1.50.21 sound guard (no crash for a fighter without a sound bank), Underground objects kept   # 1.50.19 made skills offline at the match, community skills   # 1.50.18: the MOD LIST / What's New say what is in the pack now   # 1.50.15: the MODS fixes (owner 2026-11-14 "Bring the fix over" / "Always do that")   # 1.50.13: the MODS loader (MOD LIST entries per edition, six fighters on 4-start stages)
SIGNER = os.path.join(ROOT, 'build', 'CwoSign.exe')
SIGN_KEY = os.path.join(os.environ['USERPROFILE'], 'Documents', 'BFR Signing Key', 'bfr_update_private.key')   # never in a repository


def version_line(src):
    """the main menu's line "MODDED 1.40 - BFR MOD PACK" -> "... BFR MOD PACK <VERSION>" (every pushstr of it re-pointed)"""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import build_lobby3 as bl
    from build_modlist_menu import pool_bytes
    sc = bl.Script(src)
    old = b'\x07580rMODDED %d.%02d - BFR MOD PACK'
    off = sc.s('\x07580rMODDED %d.%02d - BFR MOD PACK ' + VERSION)
    n = 0
    for at in range(0x20, sc.base, 4):
        if sc.word(at)[0] == 0x11 and pool_bytes(sc, at) == old:
            sc.code[at:at + 4] = bl.w(0x11, 0, off)
            n += 1
    assert n >= 1, 'the BFR MOD PACK line'
    return sc.build()


def sha(b):
    return hashlib.sha256(b).digest()


def ranges(old, new):
    """byte ranges of NEW that differ from OLD (gaps under 16 bytes merged), plus NEW's tail past OLD"""
    out, i, n = [], 0, min(len(old), len(new))
    while i < n:
        if old[i] == new[i]:
            i += 1
            continue
        j = i
        while j < n:
            if old[j] != new[j]:
                j += 1
                continue
            k = j
            while k < n and k - j < 16 and old[k] == new[k]:
                k += 1
            if k - j >= 16 or k == n:
                break
            j = k
        out.append((i, new[i:j]))
        i = j
    if len(new) > len(old):
        out.append((len(old), new[len(old):]))
    return out


COPY = 0x80000000
MIN_COPY = 12


def segments(old, rs):
    """each changed range split into copies from OLD (the game's own bytes that only moved: referenced, not shipped)
    and literal bytes (ours)"""
    index = {}
    for i in range(len(old) - (MIN_COPY - 1)):
        index.setdefault(old[i:i + MIN_COPY], []).append(i)
    out = []
    for off, d in rs:
        i, lit = 0, bytearray()
        while i < len(d):
            best, bsrc = 0, -1
            for src in index.get(d[i:i + MIN_COPY], [])[:64]:
                n = MIN_COPY
                while i + n < len(d) and src + n < len(old) and d[i + n] == old[src + n]:
                    n += 1
                if n > best:
                    best, bsrc = n, src
            if best >= MIN_COPY:
                if lit:
                    out.append((off + i - len(lit), bytes(lit), None))
                    lit = bytearray()
                out.append((off + i, best, bsrc))
                i += best
            else:
                lit.append(d[i])
                i += 1
        if lit:
            out.append((off + len(d) - len(lit), bytes(lit), None))
    return out


def make_patch(old, new):
    segs = segments(old, ranges(old, new))
    b = bytearray(b'CWOPATCH') + struct.pack('<II', 1, len(old)) + sha(old) + struct.pack('<I', len(new)) + sha(new)
    b += struct.pack('<I', len(segs))
    lit = cop = 0
    for off, data, src in segs:
        if src is None:
            b += struct.pack('<II', off, len(data)) + data
            lit += len(data)
        else:
            b += struct.pack('<III', off, COPY | data, src)
            cop += data
    return bytes(b), lit, cop


def apply_patch(p, old):                         # the same rules as the loader (cwo_patchfiles.inl), for the check here
    assert p[:8] == b'CWOPATCH' and struct.unpack_from('<I', p, 8)[0] == 1
    ssz, = struct.unpack_from('<I', p, 12)
    assert ssz == len(old) and p[16:48] == sha(old)
    rsz, = struct.unpack_from('<I', p, 48)
    r = bytearray(old[:rsz]) + bytes(max(0, rsz - len(old)))
    nr, = struct.unpack_from('<I', p, 84)
    at = 88
    for _ in range(nr):
        off, ln = struct.unpack_from('<II', p, at)
        at += 8
        if ln & COPY:
            ln &= ~COPY
            src, = struct.unpack_from('<I', p, at)
            at += 4
            r[off:off + ln] = old[src:src + ln]
            continue
        r[off:off + ln] = p[at:at + ln]
        at += ln
    assert sha(bytes(r)) == p[52:84]
    return bytes(r)


INSTALL_PS1 = r'''# BFR mod pack: installs the pack's data folder and the desktop shortcut "Phantom Dust BFR". Close the game first.
$ErrorActionPreference = 'Stop'
if (Get-Process PDUWP -ErrorAction SilentlyContinue) { throw 'Close Phantom Dust first, then run INSTALL BFR.bat again.' }
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$pkg = Join-Path $env:LOCALAPPDATA 'Packages\Microsoft.MSEsper_8wekyb3d8bbwe'
if (-not (Test-Path -LiteralPath $pkg)) { throw 'Phantom Dust (Microsoft Store) is not installed for this Windows user.' }
# 1.50.43 your own saved game (arsenals, ratings, story progress, wins and losses) is copied to your desktop first (1.50.46: the first install only)
& (Join-Path $here 'backup_save.ps1') -Why 'made by INSTALL BFR before installing' -Auto
$man = Get-Content -LiteralPath (Join-Path $here 'MANIFEST.json') -Raw | ConvertFrom-Json
foreach ($p in $man.files.PSObject.Properties) {
  $f = Join-Path $here $p.Name
  if (-not (Test-Path -LiteralPath $f)) { throw "missing pack file: $($p.Name)" }
  if ((Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash.ToLower() -ne $p.Value) { throw "damaged pack file: $($p.Name) - download the pack again" }
}
$data = Join-Path $pkg 'AC\Temp\CwoLoaderBFR'
New-Item -ItemType Directory -Force -Path $data | Out-Null
# 1.49: every file of yours that the pack replaces is kept first (AC\Temp\CwoLoaderBFR_backup_<date>), never deleted
$src = Join-Path $here 'data'
$bk = $data + '_backup_' + (Get-Date -Format 'yyyy-MM-dd_HHmmss')
$kept = 0
Get-ChildItem -LiteralPath $src -Recurse -File | ForEach-Object {
  $rel = $_.FullName.Substring($src.Length + 1)
  $t = Join-Path $data $rel
  if ((Test-Path -LiteralPath $t) -and ((Get-FileHash -LiteralPath $t -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash)) {
    $b = Join-Path $bk $rel
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $b) | Out-Null
    Copy-Item -LiteralPath $t -Destination $b -Force
    $kept++
  }
}
# 1.49.1: game files the loader builds from the pack's patches at the first start (stages, outfits, menus) - a different
# copy of yours is kept too (the loader itself keeps your data-folder models as <name>.before_pack)
foreach ($p in $man.patches) {
  if ($p.file.StartsWith('_data/')) { continue }
  $t = Join-Path $data ('game\' + ($p.file -replace '/', '\'))
  if ((Test-Path -LiteralPath $t) -and ((Get-FileHash -LiteralPath $t -Algorithm SHA256).Hash.ToLower() -ne $p.result_sha256)) {
    $b = Join-Path $bk ('game\' + ($p.file -replace '/', '\'))
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $b) | Out-Null
    Copy-Item -LiteralPath $t -Destination $b -Force
    $kept++
  }
}
# 1.50.9: a patch file an older pack version had and this one does not (a removed stage) goes to the backup too, so the
# loader does not build that file again
$srcp = Join-Path $src 'patches'; $dstp = Join-Path $data 'patches'
if (Test-Path -LiteralPath $dstp) {
  Get-ChildItem -LiteralPath $dstp -Recurse -File | ForEach-Object {
    $rel = $_.FullName.Substring($dstp.Length + 1)
    if (-not (Test-Path -LiteralPath (Join-Path $srcp $rel))) {
      $b = Join-Path $bk ('patches\' + $rel)
      New-Item -ItemType Directory -Force -Path (Split-Path -Parent $b) | Out-Null
      Move-Item -LiteralPath $_.FullName -Destination $b -Force
      $kept++
    }
  }
}
if ($kept) { Write-Host "$kept of your own files that the pack replaces were kept in $bk" }
Get-ChildItem -LiteralPath $src | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $data -Recurse -Force }
# the CREATE editor's font: made on this PC from Windows' own Segoe UI fonts (never shipped)
& (Join-Path $here 'launcher\CwoLaunch.exe') --bake-font (Join-Path $data 'CwoUiFont.bin') | Out-Null
$names = Join-Path $data 'CwoOutfitNames.txt'   # 1.45: COLORS shows COLOR 1-15 again
if (Test-Path -LiteralPath $names) { Move-Item -LiteralPath $names -Destination ($names + '.old') -Force }
$lnk = Join-Path ([Environment]::GetFolderPath('Desktop')) 'Phantom Dust BFR.lnk'
$s = (New-Object -ComObject WScript.Shell).CreateShortcut($lnk)
$s.TargetPath = Join-Path $here 'launcher\CwoLaunch.exe'
$s.Arguments = '--edition BFR'
$s.WorkingDirectory = Join-Path $here 'launcher'
$s.Description = 'Phantom Dust - BFR mod pack ' + $man.version
$s.Save()
Write-Host "BFR mod pack $($man.version) installed."
Write-Host "Start the game with 'Phantom Dust BFR' on your desktop. The first start builds the pack's menus from your own game files."
Write-Host "Loader log: $data\cwo_loader.log"
'''
UNINSTALL_PS1 = r'''# BFR mod pack: removes the desktop shortcut and moves the pack's data folder aside (kept, renamed, never deleted).
$ErrorActionPreference = 'Stop'
if (Get-Process PDUWP -ErrorAction SilentlyContinue) { throw 'Close Phantom Dust first, then run UNINSTALL BFR.bat again.' }
$pkg = Join-Path $env:LOCALAPPDATA 'Packages\Microsoft.MSEsper_8wekyb3d8bbwe'
$data = Join-Path $pkg 'AC\Temp\CwoLoaderBFR'
if (Test-Path -LiteralPath $data) {
  $aside = $data + '_removed_' + (Get-Date -Format 'yyyy-MM-dd_HHmmss')
  Move-Item -LiteralPath $data -Destination $aside
  Write-Host "Pack data moved to $aside (your BFR arsenals and stages are in there; delete it yourself if you do not need it)."
}
$lnk = Join-Path ([Environment]::GetFolderPath('Desktop')) 'Phantom Dust BFR.lnk'
if (Test-Path -LiteralPath $lnk) { Remove-Item -LiteralPath $lnk -Force }
Write-Host 'BFR mod pack removed. The game itself and your saves were never changed.'
'''
BAT = '@echo off\r\npowershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0{0}"\r\npause\r\n'

README = '''# BFR mod pack {ver}

A Phantom Dust (Microsoft Store, PC) mod pack for online play with friends. Every player in a match needs this same pack
and the same skill choice; the pack gives the game its own skill update version (1.42 with the Battle for Reality skills,
1.43 with the original skills), so Xbox Live only puts BFR players together.

## How to use it (where everything is)
1. Install: close Phantom Dust, run `INSTALL BFR.bat`, start the game with the "Phantom Dust BFR" desktop shortcut (always
   that shortcut - it checks for updates and starts the pack).
2. Battle for Reality skills: MULTIPLAYER > Split Screen or Xbox Live > Lab > "Enable Battle for Reality skill pack (375+)"
   to switch them on, "Revert back to OG Phantom Dust skills (0-374)" to go back to the original skills. Everyone in a match
   needs the same choice.
3. Your skills: Lab > View Stock List (Y sorts). Lab > View Skill Vault (or Back + Y on View Stock): swap a skill you don't
   own into your stock. Skills that are not playable yet are only shown in the vault.
4. Stages: in the STAGE LIST (split screen) and the LEVEL LIST (Xbox Live > Create Match) each stage family is one row -
   press Left / Right to pick the version (Lost / Light / Noble Palace, Sein / Edgar's Dream: Summit, Grand Panorama /
   Sunset ...); the stage is shown behind the list.
5. Lobbies (Xbox Live > Create Match, and Split Screen): X = OPTIONS - Edit Arsenal, Change Rule (game rules + the BFR
   rules), Change Game Type, BFR Options (Teammate Lock-on, Next Map, Tiebreaker, HUD Preset, Downed Glow, HUD Faces).
6. Main menu: CREATE (the creators, offline), MOD LIST (every mod + What's New + the shout-outs), FEEDBACK (send a bug,
   idea or a skill you made), TO-DO LIST; OPTIONS has HUD Preset, Records, Visions, Stage Music.
7. Pause menu (offline match): Teammate Lock-on row.
8. Uninstall: close the game, run `UNINSTALL BFR.bat` (your saves are never changed).

## What's in the pack
Everything below is installed with the pack, as it is in this version. "Online" = it also runs in Xbox Live / system link
sessions (every player in the match needs this same pack); "offline" = split screen, story and Single Play only.

### Maps
- **The stage lists** (Xbox Live LEVEL LIST and split-screen STAGE LIST, online and offline): the game's 17 stages - Lost
  Palace, Light Palace, Noble Palace, Spiral Highway, Lightning Highway, Sunlight Highway, Panorama Earthquake, Panorama
  Building, Dawn City, Dusk City, Midnight City, Storm Refinery, Sunset Refinery, Acid Refinery, Twilight Lane, Silent Lane,
  Sein - and after them 11 added maps (built on your PC from your own game files at the first start):
  - **Grand Panorama** and **Grand Panorama (Sunset)**: one designed map (office-roof plaza, refinery roof, broken highway),
    4 lifts, 8 spawns; the Sunset one under Sunset Refinery's sky;
  - **Sky Ring Highway**, **Highway to the Alley**, **Five-Story Building**, **RoofTop Day** (6 levels of rooftops);
  - **Jungle Cave**: tree house, bush border, solid tree limbs;
  - **Ruins**: three roaming tornadoes, ruined buildings at the edge and the desert beyond, no fog;
  - **Antarctic Outpost**: ice floor, frost mines, Ice Storm drops from the sky, breaking ice and bridges;
  - **Edgar's Dream: Summit**: the top of Edgar's Dream's stairs (the Demented Freia arena), 4 players;
  - **Preview Stage**: a plain grid room to show characters.
- Split-screen STAGE LIST: one row per stage family - D-pad Left / Right picks the version (Palace: Lost / Light / Noble,
  and so on; Sein's row holds Edgar's Dream: Summit). The Xbox Live LEVEL LIST works the same way (1.50.26).
- The highlighted stage is shown live behind both lists.
- Map rating at the end of an offline match on an added map, shown in the lobby's Level line.
- Not in the lists: the Skill Test Stage and the piece rooms (Edgar's Lab, Highway, Lost Palace, Panorama, Dawn City, Sein,
  Refinery, Twilight Lane A / B) are Create a Stage's starting rooms.

### Skills
- **Battle for Reality skill pack** (from 1.50.40): the original Phantom Dust skills 1-374 exactly as the game has them,
  except that every environment skill lasts 2 minutes, plus the pack's own skills 375-465 (online). Switch it in the Lab:
  "Enable Battle for Reality skill pack (375+)" or "Revert back to OG Phantom Dust skills (0-374)" (Revert also takes the new
  skills out of the stock).
- **8 more new skills** (online too - every player of the pack has them): Mirror, Hologram, Spiritual Projection (their
  doubles fight in offline matches only), Thunder Punch, Ice Punch, Fiery Tornado, Mega Photon Burst, Giant Meteor. They
  cannot be changed in Create a Skill (a new skill can still be based on them).
- **Skills you make** (Create a Skill, Skill Fuser) are for story, split screen and Single Play. Online, the Xbox Live
  arsenal list refuses an arsenal holding one, and any that still reach a match (Edit Arsenal in the lobby, Download
  Battle) play as Aura Particles for that match only - your saved arsenal is not changed. Saved Create a Stage maps are not
  used online either.
- **Community skills**: FEEDBACK > Category "Skill to share" sends one skill you made to the pack's author. Skills the
  author adds come to every player in a pack update (numbers 600-639) and work online.
- **Lab > View Skill Vault** (and Back + Y on View Stock): swap a skill you don't have into your stock (online too).
- A bigger skill table and stage memory for the added skills and stages.

### Xbox Live / system link lobby (online)
- Edit Arsenal and arsenal lists 1-3; this session's wins and losses; the members' portraits in colour.
- For the host: Change Rule (the game's rules and the BFR rules below), Change Game Type (One-on-One, Tag Team, Battle
  Royale); Create Game Session lists the BFR rules too.
- **BFR Options**: Teammate Lock-on (each player's own), Next Map (host), Tiebreaker, HUD Preset, Downed Glow (the host
  switches it for everyone), HUD Faces.

### Rules and modes
- **BFR rules** (online and offline; none of them changes your rating - Standard Battle stays rated): Most KOs (decided
  when the clock runs out), First to 5 KOs (the last fighter who did damage scores), Survival (3 revives a fighter), King
  of the Hill, Shrinking Arena, Aura Rush, Sudden Death, Realistic Collision (shots hit whoever is in the way).
- The KO tally beside each name; a downed fighter's name plate keeps flashing until revived.
- **Tiebreaker**: teams stay locked for two battles; after 1 win each with the same teams a vote opens (everyone YES = the
  tiebreaker round, a NO = the teams are free).
- **Next Map**: the host's session moves to the next stage after each match.
- **3 vs 3** and six fighters: the 3 VS 3 LOBBY in split screen, the 3 vs 3 lobby layout online, HUD faces for fighters 5
  and 6.
- **MULTIPLAYER > Single Play** (offline): you and CPU fighters - 1 vs 1, Tag Team, 3 vs 3, Battle Royale, Boss Raid; CPU
  levels with Tough and Brutal rows; CPUs move, fight and revive their downed teammates.
- **Download Battle** (Scoreboard): your stage, character, opponent and arsenal; line-ups 1 vs 1, 1 vs 2, 1 vs 3 or 2 vs 2
  with a CPU ally; the free camera. Download Battles send no rating or win count.
- **Create a Mode** modes in split screen's Change Rule (offline): Standard, 3 vs 3, Long fight, Quick KO, My Mode 5.

### HUD and look
- **HUD faces** (online too): every fighter's face beside their name and life bar - hurt at life 10 / 5 / 3, a red pulse at
  1, KO at 0, a flash on each hit, faces for poison, freeze, paralysis, reversed controls, invisible and power / speed /
  recovery up (CwoFaces.txt moves it).
- **HUD presets** (OPTIONS > HUD Preset with a preview, or the lobby's BFR Options; display only): 0 the game's own, 1 Split
  Look, 2 Wide Bars, 3 Centre Stage, 4 Low Profile, 5 Mirror, 6 Minimal.
- Split screen: every fighter's own panel (life bar, hand, deck, aura and face) under each player's panel.
- **Downed Glow** on every downed fighter until revived (online too, the host's switch).
- **Intro poses** at the 3-second countdown (online too); the winners' victory pose, and the losing pose for everyone
  standing at a time-out draw (offline).
- **Visions** graphics presets (OPTIONS > Visions, CwoVisions.txt); the 60 FPS display for fighters, effects and
  projectiles (CwoFps.txt: 60 / off).
- **Holiday themes** on the title screen and main menu (CwoTheme.txt); the white menu theme (CwoUiTheme.txt: white / off,
  off as shipped); the title-screen music at 70 % until you leave the title screen.

### Characters and outfits
- **15 outfits** for every playable and enemy fighter: the 5th is the OG outfit, then Neon Circuit, Royal Gold, Angelic,
  Street, Field Ops, Inferno, Frostbite, Hazard, Galaxy, Two-Tone Tide, Crimson Shadow (built from your own game files the
  first time one is picked); Edgar's outfit 6 as the pack's author saved it.
- **Create an Outfit** (CREATE, offline): a turning preview, color and skin sliders, an Aura color per outfit, outfit
  effects (a skill's flames, ice, thunder, psychic or vibration on the body: place, size, speed, when it shows), Save into
  any of the 15 slots, an outfit stock of up to 50 per character.
- **Character selects** (split screen and Xbox Live, display only): new faces for every player, monster portraits on X, the
  portrait follows the outfit highlighted in COLORS (COLOR 1-15, opened in the left black bar). The NPC cell (split screen)
  and COMING SOON on the monster page are decoration and cannot be highlighted.
- Mission select: the new character portraits.
- Freia and Gyne: a slightly fuller bust on every outfit (CwoBodyShape.txt).

### CREATE (main menu, offline)
- Create a Stage (Forge), Create a Skill, Create a School, Create a Character, Create a Quest, Create a Mode, Create an
  Outfit, Spectator, Skill Fuser, Records, Stage Music, Draft Arsenal, Boss Raid, Visions, and the full-screen CREATE editor
  (keyboard and mouse).

### Menus
- **Main menu** (8 rows): SCENARIO MODE, MULTIPLAYER MODE (Xbox Live, Split Screen, Single Play), OPTIONS MODE, CREATE,
  MOD LIST (every mod, What's New at the top), FEEDBACK, TO-DO LIST, HOW TO PLAY; the BFR MOD PACK version line.
- **OPTIONS**: HUD Preset, Records, Visions, Stage Music.
- **Pause menu** (offline match / 2P VS): Teammate Lock-on row.
- **Arsenal lists**: the title shows its number; Y > ARSENAL OPTIONS: Add Arsenal (a full list of 16 sends it to the next
  list), Erase Arsenal, Reset All Arsenals, Acquire All Skills.
- **Arsenal Data**: Y switches the Xbox Live results between All and each stage.

Automatic updates, what the pack sends and what stays offline: see the sections after the version history.

## Version history
- **Battle for Reality skill pack**: revised skills 1-374 and new skills 375-465. Switch it in the Lab: "Enable Battle for
  Reality skill pack (1-466)" or "Revert back to OG Phantom Dust skills (0-374)".
- **Xbox Live / system link lobby**: Edit Arsenal and arsenal lists 1-3; for the host, Change Rule (the game's rules plus
  Most KOs, First to 5 KOs and Survival with 3 revives a fighter) and Change Game Type; BFR Options for everyone:
  Teammate Lock-on, Tiebreaker Vote, and Next Map for the host; this session's wins and losses.
- **Game rules**: First to 5 KOs scores the last fighter who did damage (knocking yourself out scores nothing); the KO
  tally shows beside each name; a downed fighter's name plate keeps flashing until revived; Most KOs is decided when the
  match clock runs out.
- **Tie-breaker**: after a 1-1 session, SESSION INFO says so and players vote with the Tiebreaker Vote row (3 yes, or a
  tie of yes and no, means a tie-breaker).
- **3 vs 3** and six fighters, the 3 VS 3 LOBBY in split screen, HUD for fighters 5 and 6.
- **Split screen**: Edit Arsenal, Change Rule with the Create a Mode modes, game type, the stage shown behind the level
  list.
- **Arsenal lists**: the list title shows its number; Y > ARSENAL OPTIONS adds Add Arsenal (a full list of 16 sends it
  to the next list), Erase Arsenal, Reset All Arsenals and Acquire All Skills.
- **Arsenal Data**: Y switches the Xbox Live results between All and each stage (Palace, Highway, Panorama, City,
  Refinery, Lane, Ruins).
- **HUD presets** (lobby > OPTIONS > BFR Options > HUD Preset; A cycles): 0 the game's own, 1 Split Look (the split-screen
  look: black bars, the skill wheel in the bottom bar), 2 Wide Bars, 3 Centre Stage, 4 Low Profile, 5 Mirror, 6 Minimal.
  Display only, each player's own choice.
- **HUD faces**: every fighter's face beside their name and life bar (full screen, split screen, every line-up). It changes
  with the fight: hurt at life 10 / 5 / 3, a red pulse at 1, KO at 0, a flash on each hit, and faces for poison, freeze,
  paralysis, reversed controls, invisible, power / speed / recovery up. CwoFaces.txt in the data folder moves it.
- **Download Battle line-ups** (Scoreboard > Download Battle): 1 vs 1, 1 vs 2, 1 vs 3 or 2 vs 2 with a CPU ally, a
  character for each added fighter. Download Battles send no rating or win count.
- **Create an Outfit**: the model shown from the start beside the page (right stick turns it), color and skin sliders,
  Save and apply into any of the 15 slots, and an outfit stock of up to 50 per character (OutfitStock folder).
- **Edgar's Dream: Summit** (split screen STAGE LIST, after the stock stages): the top of Edgar's Dream's stairs - the
  arena of the Demented Freia fight - as a stage of its own, 4 players (built from your own Edgar's Dream files).
- **The Summit** (STAGE LIST: Sein row, press Right twice): Sein's ground and the Summit as one stage - doorways into the
  middle pillar on the first floor, a spiral stair up two turns to the Summit, balconies, bridges, rock platforms and floating
  rocks you can stand on, edges you cannot walk off, 8 starts, bright light.
- **Bigger skill table and stage memory**, Download Battle choices (stage, characters, arsenal).
- **15 outfits** for every playable and enemy fighter: the 5th is the OG outfit (alpha animations), then designer
  outfits - Neon Circuit, Royal Gold, Angelic, Street, Field Ops, Inferno, Frostbite, Hazard, Galaxy, Two-Tone Tide,
  Crimson Shadow (built from your own game files the first time one is picked).
- **Edgar's Dream** (offline): every page (1.50) - Create a Stage (Forge), Create a Skill, Create a School, Create a
  Character, Create a Quest, Create a Mode, Spectator, Skill Fuser, Records, Stage Music, Draft Arsenal, Boss Raid, Create an
  Outfit (skin tone, head accessory, jacket and pants colours, with a turning preview) and Visions.
- **1.43 - Outfit effects** (Create an Outfit, offline matches): a skill's flames, ice, thunder, psychic or vibration on the
  body - around the whole outline or in one spot; move it left / right, up / down, front / back from the middle of the body,
  size 25-300%, speed 0-300% (0 = paused), and show it always, as a reward at aura level 10, or growing with the aura.
  Outfits in the stock keep their effect.
- **1.43 - Character select**: new faces for every player at one framing, monster portraits on X, COMING SOON in the empty
  cell, and the portrait follows the outfit highlighted in COLORS (split screen and Xbox Live; display only).
- **1.43 - Intro poses**: every fighter strikes their quest intro pose during the 3-second countdown (1.43.1: online
  matches too).
- **1.43 - Holiday themes**: on New Year, Valentine's Day, St. Patrick's Day, Easter, the 4th of July, Halloween,
  Thanksgiving and Christmas the title screen and main menu take the holiday's colors (the CRT lines stay), with a
  greeting banner and a What's New window (CwoTheme.txt: auto / off / a holiday's name).
- **1.43 - MOD LIST** on the main menu: every mod and what it does (read-only).
- **1.43 - Realistic collision** (Create a Mode rule, offline): shots hit whoever is in the way, teammates too.
- **1.44 - 15 outfits really there**: the character table itself now lists 15 outfits for every fighter (before, the
  selects could still show 4); outfits 5-15 are built from your own files the first time each is picked.
- **1.44 - Visions** (offline): graphics presets in the data folder's CwoVisions.txt - clarity, vivid, cinematic, soft,
  noir or off (or "custom sharpen blur saturation contrast vignette warmth brightness").
- **1.44 - Preview Stage** (split screen STAGE LIST, last row): a plain grid room to show characters, built from your own
  stage 32.
- **1.44 - Create an Outfit fixes**: saving from outfits 5-15 works; the name keyboard shows the chosen letter in brackets.
- **1.45**: CPUs revive their downed teammates (offline); Edgar's Dream > Visions; Revert to OG skills also takes the new
  skills out of the stock; What's New is at the top of MOD LIST; COLORS opens on the side away from the highlighted face
  and reads COLOR 1-15; the hidden Jump / Dash copies ("Psycho Wave" #257 / #294) stay out of the stock.
- **1.45.1**: OPTIONS > HUD Preset (choose with a preview of where the HUD sits); the tiebreaker: teams stay locked for
  two battles, after 1 win each with the same teams a vote opens by itself (everyone YES = the tiebreaker round, a NO =
  the teams are free); monster select portraits follow COLORS.
- **1.45.5**: full-size select portraits again (the 82 % grid of 1.45.4 is undone); View Stock no longer shows the two empty
  extra rows 257 / 294 (the game's hidden Jump / Dash copies).
- **1.45.6**: COLORS opens in the left black bar (split screen and Xbox Live), every full-size portrait visible.
- **1.45.7**: Create an Outfit shows the outfit effect on the turning preview model right away, and follows every change.
- **1.45.8**: YOU WIN: the winners do the victory pose; a time-out draw: everyone standing does the losing pose (offline);   the split-screen lobby's Tag Team / Battle Royale CPUs now join the match.
- **1.46**: lobby face strips per outfit (replaced in 1.47 by the colour lobby portraits).
- **1.47**: the lobby portraits in colour (the MODS edition's player art, the monsters coloured in; no more red strips),
  in the split-screen and Xbox Live lobbies; the NPC page is MODS-only for now; split screen shows the other fighters' names and life;
  Create Game Session and the lobby rule list carry King of the Hill, Shrinking Arena, Aura Rush, Sudden Death and Realistic
  Collision (online too); Realistic Mode: capsule bursts and a 10 s exit for a body nobody can revive; Create an Outfit: an
  effect from your deck, an Aura color per outfit, fuller flames above 150 % size; mission select portraits; the Download
  Battle free camera.
- **1.47.1**: loader 5.99.0 (the MODS edition's NPC page code; off in BFR).
- **1.47.2**: loader 5.101.0: Create an Outfit lists every skill of your arsenal in use as an Outfit effect, Effect size /
  speed in 1 % steps (RT held: 25 %).
- **1.47.3**: loader 5.101.2: Preview live (turning model) back in Create an Outfit, showing only the model and its effect;
  holding left / right on Effect size / speed keeps stepping.
- **1.47.4**: loader 5.102.0: the BFR rules (Most KOs, First to 5 KOs, Survival, King of the Hill, Shrinking Arena, Aura
  Rush, Sudden Death, Realistic Collision) no longer change your rating - like the game's own Heavy, Special Environment and
  Annihilate the Odd One. Standard Battle (One-on-One, Tag Team, Battle Royale) stays rated.
- **1.47.5**: loader 5.103.0: Downed Glow - a glow on every downed fighter until revived (display only); the host switches
  it in the lobby's BFR Options (Downed Glow: ON / OFF, default ON) and everyone in the session follows.
- **1.47.6**: loader 5.104.0: Downed Glow fixed (it never started); split-screen name list moved below the item icons;
  skills whose text says "Aura cost +N per use" (Traveling Thorn ...) now really cost +N more each use; Blood Eclipse hits every fighter, as its text says; JD's colour 4 is JD (not Mac).
- **1.47.7**: loader 5.105.0: COMING SOON hides while a COLORS list is open; the split-screen text list of the other
  fighters is off (real life bars are a work in progress, as Battle Royale and the CPUs in split screen); What's New updated.
- **1.47.8**: loader 5.106.0: JD's colour 4 select portrait shows JD (it still showed the old Mac face); no Edgar's Dream quick
  panel on View / Back in this edition.
- **1.50.73**: loader 5.188.41: on the loader's own pages (SINGLE PLAY lobby, CREATE) the game's key bindings were switched
  off and on every frame - now once; SESSION INFO says "1 life".
- **1.50.72**: the source code is published in the `source/` folder (the loader, the launcher and the pack's tools); no
  change to the game.
- **1.50.71**: loader 5.188.40, built from the loader's own clean source (the one that will be published); SINGLE PLAY's
  SESSION INFO shows the rule you picked (it said Standard Battle); a lobby match at 00:00 ends as it should.
- **1.50.70**: README credits: a thank-you to the testers and bug reporters (the same names as MOD LIST > COMMUNITY
  SHOUT-OUT); no change to the game.
- **1.50.69**: With no controller, SINGLE PLAY's Start Game goes straight into the match (it stopped on the stage list);
  help cards read cleanly (a '>' sign was missing).
- **1.50.68**: README: the source code is not in this repository yet (it is coming); no change to the game.
- **1.50.67**: Keyboard players: SINGLE PLAY and the CREATE pages work with no controller (arrows, Enter / A, Esc / B, Space /
  X, Tab / Y, Q / E) - without a controller SINGLE PLAY used to show an old debug list; B goes back to the main menu either
  way. 1 vs 1 shows teams A / B in the lobby.
- **1.50.66**: Create a Mode's help says where to play a mode: X > Change Rule in any lobby (SINGLE PLAY too).
- **1.50.65**: SINGLE PLAY > Change Rule lists your Create a Mode modes too (after the game's rules); fixed a crash when
  the CPU played for you (View + LB) and its target was gone.
- **1.50.64**: B on SINGLE PLAY goes back to the main menu (it used to drop you on an old debug list, then the title).
- **1.50.63**: SINGLE PLAY: Battle Royale for 2 to 6 fighters (Fighters row), each alone; a new 2 vs 2 vs 2 game type (teams
  A, B, C) that ends when one team is left. View + LB (the CPU plays for you): X hides / shows the whole HUD. Fixes: a crash
  when the CPU played for you and had no target; team C showed as team A on the side panels.
- **1.50.62**: Title intro - BATTLE FOR REALITY / MODS ENABLED fades in on black before PRESS START (the game's own font).
- **1.50.61**: Red Moon - a red moon floats above the middle of the stage's starts; the first time each fighter steps into
  its light they get +2 attack STR for the match (once); Purify puts it out; it never takes health. Skill update version 146.
- **1.50.60**: Fiery Tornado is a real tornado of fire now (a big burning body rides with it, no flicker); Charged Ray's STR is
  its charge + 2, as its card says (was charge x 2).
- **1.50.59**: CREATE > Create a Stage (3rd row) opens the stage list and builds straight away (no deck screens). Stage
  editor: flying (RT / LT) moves your fighter again, and flying down stops on the floor (no more falling through the map).
- **1.50.57**: CREATE > Full-Screen Editor opens the editor straight away (on Dawn City) instead of a stage list and a
  stuck deck screen; Fiery Tornado no longer hurts its caster; Toxic Air uses an environment cast.
- **1.50.56**: Hologram: when its time is up it sinks through the floor and is out of the stage until it is used again; the
  Hologram's and the Mirror's double leave the stage at once when hit (no invisible double left standing).
- **1.50.55**: The start-up message "some game files did not match the pack" is gone: 10 outfit patches were built for a
  character the Store game does not have (removed). Thunder Punch and Ice Punch are Nature skills now.
- **1.50.54**: Fiery Tornado is painted fire-orange and 1.6x wider (flame trail kept); Giant Meteor is drawn 4.5x bigger.
- **1.50.53**: Thunder Punch and Ice Punch are limited to 3 uses each.
- **1.50.52**: Skill fixes - Blazing Sun (422) lasts 30 s; Toxic Air (442) costs 1 Health per skill use for 30 s (it was
  8800); Drop Charge (445) is a proper sensor mine (it had an invalid mine mode); Angry Spirit (451): STR = STR of all
  your equipped Spirit skills (Angry Spirit + Protective Spirit). New skill update version (145): play online with 1.50.52.
- **1.50.51**: The split-screen lobby has the same OPTIONS as the Xbox Live lobby: BFR Options (Teammate Lock-on, Next Map,
  Tiebreaker, HUD Preset, Downed Glow, HUD Faces) added.
- **1.50.50**: Grand Panorama (Sunset): three capsule spots sat next to a jump emitter (one only 1.7 m away) - moved
  6 m or more clear of every lift.
- **1.50.49**: The launcher keeps its window open when Phantom Dust does not start and says why (for example a game that
  froze in the background: Task Manager > Details > PDUWP.exe > End task).
- **1.50.48**: 3 vs 3: fighters 5 and 6 showed 00 capsules left on the HUD while they still had skills; the number is right now.
  (An empty arsenal drains 1 life every 3 seconds for all six fighters, as in the game.)
- **1.50.47**: Create a Mode: 3 VS 3 LOBBY no longer drops you on the title screen (B on the CREATE list goes on to the
  split-screen lobby; it needs 2 controllers - one controller: MULTIPLAYER > SINGLE PLAY, Game Type 3 vs 3); the refill row is tidy.
- **1.50.46**: Save backup: the installer makes it once; then MOD LIST > X (made at your next start) or BACKUP MY SAVE.bat.
  One desktop folder, "Phantom Dust save backups", keeps the newest 3.
- **1.50.45**: MOD LIST and this README say why the pack check-in is sent (so the pack's author can see who plays the pack).
- **1.50.44**: The pack's files are checked at every start: anything changed is put back (or downloaded again, signed),
  and files that are not part of the pack are moved out before the game starts (see "Only the pack's files").
- **1.50.43**: BACKUP MY SAVE.bat: copies your own saved game (arsenals, ratings, story progress, wins and losses) to
  your desktop, every file checked, with a RESTORE THIS SAVE.bat inside; INSTALL BFR.bat makes one before installing.
- **1.50.42**: Create a Mode: the arsenal refill row (it did nothing) is marked not built yet; one way into the 3 vs 3
  lobby; the help card says which modes run online.
- **1.50.41**: Loader 5.188.6: the pack check-in keeps its state in CwoCheckIn.txt (the old state file is renamed once).
- **1.50.40**: Battle for Reality skill pack: the original Phantom Dust skills 1-374 are back exactly as the game has them -
  only every environment skill now lasts 2 minutes; the pack adds its own skills 375-466. Everyone needs 1.50.40 to play
  together online (new skill update version).
- **1.50.39**: New outfits 6-15 for every character and every enemy / monster: colour themes - Crimson, Ember Orange, Goldenrod,
  Forest Green, Turquoise, Cyan, Royal Blue, Violet, Magenta and Midnight with LED seams - each character in its own shades,
  accent colours and LED outfits; leather keeps its folds and grain, buckles / studs / zippers are polished silver, faces, skin
  and hair stay as they are (replaces 1.50.38's map looks).
- **1.50.38**: Every playable character's outfits 6-15 take their look from the maps (The Summit, Grand Panorama Sunset, Sky
  Ring Highway, Highway to the Alley, Jungle Cave, Five-Story Building, Ruins, Antarctic Outpost, RoofTop Day, Driftstone
  Sanctum); faces, skin and hair stay as they are.
- **1.50.37**: Driftstone Sanctum - no more falling through: every island top you can see is solid now; the capsules of the
  west-island start sit apart and away from the edge.
- **1.50.36**: Five-Story Building - every wall on all five floors breaks (smash through to find the other players; the floors,
  stairs and the strips over doorways stay); a big jump lift on the plaza by the south-west face throws you up to the roof, which
  now has air-conditioning units, a water tank and the stair hut. Edgar's outfits 6-15 take their look from the maps: The
  Summit, Grand Panorama Sunset, Sky Ring Highway, Highway to the Alley, Jungle Cave, Five-Story Building, Ruins, Antarctic
  Outpost, RoofTop Day and Driftstone Sanctum.
  Online: players who join a lobby now get the host's added stage (its files and its name) - no more 'dummy' stage names.
- **1.50.35**: NEW STAGE Driftstone Sanctum - floating islands with low gravity (you fall slowly; offline matches - online it
  plays with normal gravity). Every match starts with 3 capsules again: the 2-capsule start is gone from Create a Mode (the
  3 vs 3 mode had it).
- **1.50.34**: OPTIONS > RECORDS shows your own profile's records (the pack no longer carries anyone else's); B on RECORDS,
  VISIONS and STAGE MUSIC goes back to OPTIONS (it opened the CREATE list).
- **1.50.34**: Edgar has new outfits - outfit 4 (Royal Navy) and outfits 6-15 (Inferno, Glacier, Jungle, Urban Digital, Tiger,
  Neon Circuit, Tartan, Serpent, Desert, Void), each its own look. King of the Hill is quieter (4 zone markers instead of 12).
- **1.50.34**: CPU fighters move on every added map - each stage now has its own CPU route map (before, every added map used
  Highway's, so CPUs stood still or barely moved); loader 5.188.
- **1.50.34**: The Summit - the ruined towers' stepped stone bases, buttresses and the rock blocks at their feet are solid
  now (fighters stood inside them).
- **1.50.33**: NEW STAGE The Summit - Sein's ground and the Summit as one stage: walk into the middle pillar on the first
  floor and climb the spiral stair all the way up to the Summit; balconies, bridges, rock platforms, floating rocks you can
  stand on, no walking off the edges, brighter light. In the stage list: Sein row, press Right twice.
- **1.50.32**: Xbox Live lobby OPTIONS - the list sits higher, so its last row (BFR Options) no longer runs into the button bar.
- **1.50.31**: the View Stock counter's first number no longer counts the empty cells after your last skill.
- **1.50.30**: View Stock ends at your last skill - no more pages of "000 Aura Particle" rows, and the counter shows your real
  number of skills (it read 1005 / 996).
- **1.50.29**: Edgar's Dream: Summit (Sein row, Right) - every start has its own three capsule spots 5 / 10 / 15 m away
  (they were shared by all players, up to 27 m away), and 8 starts on the summit ring for 6-8 fighters.
- **1.50.28**: README "How to use it" (where every feature is); MOD LIST shout-out: Sharky also asked for the fuller bust
  of Freia and Gyne.
- **1.50.27**: an added map chosen in the Xbox Live LEVEL LIST is the session's stage (it gave Sein).
- **1.50.26**: the Xbox Live LEVEL LIST has one row per stage family too - D-pad Left / Right picks the version (Lost /
  Light / Noble Palace and so on), as in split screen.
- **1.50.25**: View Stock no longer shows the blank rows of skills that are not playable (boss and unfinished skills, 701+);
  they are in the Skill Vault only and cannot be swapped in. The 8 new skills (Mirror ... Giant Meteor) say WORK IN PROGRESS on
  their card: test them, but do not rely on them in competitive play.
- **1.50.24**: MOD LIST > COMMUNITY SHOUT-OUT: Ebony Scarface, Murder Mastodon, Doomsday and Kanye, for helping test and
  rebalance the BFR mod pack.
- **1.50.23**: loader 5.186.8 - added-map rows in the STAGE LIST use the game's "(1/2)" style when every version's name
  fits (one style per row); a third crash-shield catch for a freed effect texture (no crash to desktop).
- **1.50.22**: loader 5.186.3 - the sound guard also catches a bank slot holding a junk value (seen: a new character still
  crashed on 1.50.21); NPCs and new characters now use their moves without crashing.
- **1.50.21**: loader 5.186.2 - a fighter whose sound bank is not loaded (NPCs, new characters) no longer crashes the game when
  it uses a skill; MOD LIST lists RoofTop Day and the one-row-per-family stage list.
- **1.50.20**: RoofTop Day is in the stage lists; the split-screen STAGE LIST has one row per stage family again (D-pad Left /
  Right picks the version, as in MODS); the NPC cell can no longer be chosen (an NPC crashed when it used a skill); Highway to
  the Alley's start that stood inside a closed building is on the open alley floor.
- **1.50.19**: loader 5.186.0 - skills you make play as Aura Particles online, however they reach a match; saved Create a
  Stage maps stay offline; FEEDBACK > Skill to share and community skills (600-639); the pack's own new skills cannot be
  changed in Create a Skill. Correction: 1.50.17 / 1.50.18 said the 8 new skills were offline - they work online.
- **1.50.18**: loader 5.185.2 - the in-game MOD LIST and What's New list what is in the pack now (What's New still said 1.47).
- **1.50.17**: this README lists everything in the pack as it is now (What's in the pack, above).
- **1.50.16**: the COMING SOON cell on the monster page can no longer be highlighted or chosen.
- **1.50.15**: CPUs move and fight in Single Play; the stage list, character list and CPU levels no longer go back to the originals after a slow start; faces for fighters 5 and 6 in a 3 vs 3.
- **1.50.14**: the main menu shows all 8 rows (TO-DO LIST and HOW TO PLAY were selectable but not drawn).
- **1.50.13**: main menu CREATE (was Edgar's Dream), MULTIPLAYER > Single Play, FEEDBACK and TO-DO LIST; the pause menu has
  a Teammate Lock-on row; MOD LIST lists them; a 3 vs 3 on a stage with 4 start points no longer crashes (loader 5.180.3).
- **1.50.12**: loader 5.180.0: Freia and Gyne have a slightly fuller bust on every outfit (made by the loader from your own
  game files; `CwoBodyShape.txt`); main menu OPTIONS has Records, Visions and Stage Music.
- **1.50.11**: FEEDBACK messages and the pack check-in now reach the pack author (the form settings were missing from the pack);
  the HUD faces sit clear of the life bar; the To-Do List is filled in; the Preview Stage's capsules sit above the floor.
- **1.50.10**: the lobby's BFR OPTIONS list has its 8 rows again (HUD Faces and Downed Glow were cut off); the
  split-screen COLORS list shows 11 rows and scrolls instead of covering the help line; online, an arsenal holding a skill made in
  the Skill Creator or the Skill Fuser is refused with a message.
- **1.50.9**: loader 5.179.6: Antarctic Outpost's Ice Storm drops are drawn with Ice Storm's own falling-ice effect, from
  the skill's own height and at its own speed.
- **1.50.8**: loader 5.179.5: no crash at the end of a match after a skill effect was pushed out of memory mid-match.
- **1.50.7**: loader 5.179.4 (the MODS edition's loader, with its start-up and crash fixes): mines (Boost Mine, Sensor
  Mine ...) go off on every added stage - they were seated under the floor; Ice Storm drops fall from the sky on Antarctic
  Outpost; the HUD faces in Scoreboard and with six fighters show each fighter's own face; Ruins without fog and gusts, three
  roaming tornadoes, ruined buildings at the edge and the desert beyond; Jungle Cave without the lightning storm; Sky Ring
  Highway, Highway to the Alley, Five-Story Building, Ruins, Antarctic Outpost and both Grand Panoramas as in the MODS
  edition; Palace Ascended and The Bunker removed.
- **1.50.6**: the one-colour tint on the added stages is gone - every stage has its own textures again (Palace
  Ascended, Jungle Cave, Sky Ring Highway, Highway to the Alley, Five-Story Building, Ruins, Antarctic Outpost, both Grand Panoramas).
- **1.50.5**: Palace Ascended and Jungle Cave as in the MODS edition; Sky Ring Highway, Highway to the Alley, Five-Story
  Building, Ruins and Antarctic Outpost back to their earlier versions with the capsules raised; every added stage has its own
  colour identity (its own texture colours); the stage effects as before, with Jungle Cave's storm as tall as the big tree.
- **1.50.4**: the added stages as they are in the MODS edition now: Palace Ascended (third level on the third level, Lost
  Palace's own look), Sky Ring Highway, Highway to the Alley, Jungle Cave (tree house, bush border, tree limbs solid, the storm
  as tall as the big tree), Five-Story Building, Ruins (no fog), Antarctic Outpost; capsules raised; The Bunker's capsules;
  the stage effects as in MODS; no stage file is played as a sound bank any more (loader 5.151.2).
- **1.50.3**: the added stages are in the Xbox Live LEVEL LIST too (they were only in the split-screen list).
- **1.50.2**: fixes from the pack author's test of 1.50.1 (loader 5.151.1):
  - the stage files were not built on players' games (their patches were made from files the game does not ship): Grand
    Panorama, the new stages, the piece rooms and the NPC models now build from your own game's files;
  - the level list no longer crashes when it shows one added stage after another (Highway Pieces Test, Grand Panorama);
    a stage whose files are missing is left out of the lists instead of loading the previous one;
  - the music is at 70 % from start-up until you leave the title screen (your own music level after that);
  - the Skill Fuser's test skills 693-699 and the test stages (UE Pieces Demo, Hard Pieces Plaza, Highway Pieces Test,
    Palace UE Bridge test) are gone.
- **1.50.1**: automatic updates - the launcher checks this pack's GitHub page each time it starts and installs a newer
  version by itself (only when it is signed by the pack's author; your own changed settings and saves are kept). The main
  menu shows the pack's version (BFR MOD PACK 1.50.1).
- **1.50.0**: loader 5.151.0 - the pack author's picks from the MODS edition:
  - Grand Panorama and Grand Panorama (Sunset) remade: one designed map (office-roof plaza, refinery roof, broken highway,
    4 lifts, 8 spawns); the Sunset one mirrored under Sunset Refinery's sky;
  - the test and piece stages in the stage list: Skill Test Stage, the piece rooms (Edgar's Lab, Highway, Lost Palace,
    Panorama, Dawn City, Sein, Refinery, Twilight Lane A / B) with their Forge layouts, Palace (UE Bridge test), Highway
    Pieces Test, Hard Pieces Plaza, UE Pieces Demo;
  - new skills: Mirror, Hologram, Spiritual Projection (their doubles fight in offline matches only), Thunder Punch, Ice
    Punch, Fiery Tornado, Mega Photon Burst, Giant Meteor;
  - the full Edgar's Dream (every page), the NPC page on the character selects (the ten NPCs), CPU levels (Tough and Brutal
    rows, the Brutal program), Boss Raid (off until you switch it on), the six-fighter switch, the minimalistic white menu
    theme (CwoUiTheme.txt: white / off), the title-screen music volume (CwoTitleVolume.txt), the community shout-out list
    (CwoShoutOut.txt), the Skill Fuser's fusions;
  - modes: Standard, 3 vs 3, Long fight, Quick KO, My Mode 5;
  - fix: no map sound bank for Edgar's Dream: Summit (the bank that crashed The Bunker in the test edition).
- **1.49.1**: the rest of the pack author's BFR edition:
  - three more stages in the stage list: The Bunker, Grand Panorama and Grand Panorama (Sunset) (built on your PC from
    your own game files at the first start);
  - Edgar's outfit 6 as the pack's author saved it (Create an Outfit);
  - the install also keeps your own copy of any game file the pack rebuilds (stages, outfits, menus).
- **1.49.0**: loader 5.150.0 - everyone gets the same pack:
  - every setting the pack's author plays with is in the pack: the BFR skill pack switch, HUD preset, outfits and outfit
    effects (and the saved outfits), modes, Edgar's Dream, Spectator, Download Battle, game type, schools, records,
    arsenal lists, Arsenal Data, the Map Slot choice; the install keeps your own copies first (CwoLoaderBFR_backup_<date>);
  - Edgar's Dream > CREATE: the full-screen editor (stage, skill, outfit, character and more) next to Create a Mode;
  - CREATE: while the keyboard and mouse drive the editor, your fighter no longer reacts to them (clicks and held keys
    used to reach the game too);
  - the CREATE editor's font is made on your PC from Windows' own Segoe UI fonts (install and first start).
- **1.48.1**: loader 5.149.2 - no new skills in this pack yet (Revive Mend, Body Pull, Mirror, Hologram, Spiritual
  Projection, Thunder Punch, Ice Punch, Fiery Tornado, Mega Photon Burst and Giant Meteor are still being tested); the
  60 FPS display is on (CwoFps.txt: 60, or off).
- **1.48.0**: loader 5.149.1 - the mods of 2026-11-10:
  - seven new stages in the stage list: Palace Ascended, Sky Ring Highway, Highway to the Alley, Jungle Cave (Noble Palace
    sky, rain), Five-Story Building, Ruins (roaming tornado, sandstorm) and Antarctic Outpost (ice floor, frost mines, ice
    storm cells, breaking ice and bridges);
  - Lab > View Skill Vault (and Back + Y on View Stock): swap a skill you don't have into your stock;
  - map rating at the end of a match on a pack stage (offline), shown in the lobby's Level line;
  - 60 FPS display for fighters, effects and projectiles (CwoFps.txt: 60 / off); pause-menu lock-on toggle;
  - fixes: the stage-effect crash, the match-end fault, the start-up race, the repeated "new skill" log lines.
- **1.47.9**: loader 5.107.0: split screen shows every fighter's own panel (the game's life bar, hand, deck and aura, with
  their face) stacked under each player's panel.

## Requirements
- Windows 10 or 11 and Phantom Dust from the Microsoft Store (game version 1.40, not modified).
- Nothing of the game is in this pack. The menus are patches: on the first start the loader reads your own game files,
  checks them and builds the changed menus in the pack's data folder. If your game files are not the expected ones,
  the loader refuses that file and the log says so.

## About the loader (CwoLoader) - is it safe?
The pack runs with its own loader: `CwoLaunch.exe` (the launcher behind the desktop shortcut) and `CwoLoader.dll` (the part
that runs inside Phantom Dust and adds the pack's menus, modes, stages, skills and outfits).
- **What "CWO" means:** Codex Work Order. The project numbered its jobs CWO-001, CWO-002 and so on; the loader was job
  CWO-LOADER-01, and the name stuck. The early work orders were done with Codex (see Credits).
- **It is not a virus.** It is this pack's own program, written for this project. It does not change the game's installed
  files, it does not collect anything beyond what "What the pack sends" lists, and it does not run anything else.
- **Windows may warn you.** The loader is not signed by Microsoft or a big publisher, so Windows SmartScreen or your
  antivirus may flag it as unknown the first time ("Windows protected your PC" -> More info -> Run anyway). That warning
  only means Windows does not know the publisher. Do not switch off your antivirus; if you want, scan the pack folder with
  Windows Security first. Every pack file's SHA-256 is listed in `MANIFEST.json`, which is signed by the pack's author.
- **Open source, free to use.** Anyone interested in Phantom Dust modding is welcome to read the loader's source and use any
  of it in their own projects. The source code (the loader, the launcher and the tools that build this pack) is in the
  `source/` folder of this repository (see `source/README.md`); the pack's loader and launcher are built from exactly it.

## Back up your save (recommended)
Worried about losing your arsenals, ratings, story progress or wins and losses? Your own saved game (the game's
`SystemAppData\\wgs` folder) can be copied to your desktop, folder "Phantom Dust save backups", every file checked:
- `INSTALL BFR.bat` makes the first backup by itself (once);
- after that, whenever you like: in the game, main menu > MOD LIST > **X** (the backup is made the next time you start the
  game, before it starts - the game itself cannot write to your desktop), or close the game and run `BACKUP MY SAVE.bat`.
The folder keeps your newest 3 backups. To put one back: close the game and run `RESTORE THIS SAVE.bat` inside that backup
(your save as it is then is kept first). The mods never change your saved game; this is only for peace of mind.

## Install
1. Close Phantom Dust.
2. Run `INSTALL BFR.bat`. It checks every pack file and puts a "Phantom Dust BFR" shortcut on your desktop.
3. Start the game with that shortcut.

Keep the pack folder where it is; the shortcut starts the launcher from it. If you move the folder, run the install again.

## Only the pack's files (checked at every start)
Everyone in a match must play the same pack, so the launcher checks the files every time you start with the shortcut:
- the pack folder against the author's signed `MANIFEST.json` - a changed or missing file is downloaded again from this
  page (signed and checked); offline, the game does not start with changed pack files;
- the files the game loads (the pack's patches, skill pack, stage and effect files, and everything in the data folder's
  `game` folder) - a changed one is put back; a file that is not part of the pack (a mod from another program, an old stage)
  is moved to `CwoLoaderBFR_not_in_pack_<date>` next to the data folder. Nothing is deleted.
The launcher window then says "Resynchronized" and starts the game with the pack's files. Your own settings, records,
arsenals, saved outfits, modes, created skills and your saved game are never touched.

## Updates
Install once. Every time you start the game with the "Phantom Dust BFR" shortcut, the launcher checks this pack's GitHub
page; when there is a newer version it downloads only the files that changed and installs them before the game starts.
- Every update is signed by the pack's author. The launcher refuses anything that is not signed with that key, so nobody
  else can push files to your PC through the pack's page.
- Your own changed settings, arsenals and records are kept; the old pack files go to `_backup_<version>` in the pack folder.
- Offline (or if anything goes wrong) the game simply starts with the version you have. The launcher window and the main
  menu (BFR MOD PACK x.y.z) show which version you are on.

## What the pack sends
- **Pack check-in**: once per install and after each pack update, the pack sends your Xbox gamertag, the edition and the
  pack version to the pack's author, so the pack's author can see who plays the pack. Nothing else is sent with it.
- **FEEDBACK** (main menu, next to MOD LIST): only when you send a message there - your message and its category, with your
  gamertag, the pack version and the last stage and mode you played. The page shows this before you send. With Category
  "Skill to share" the message is one skill you made (its base skill, name, description and settings).

## Uninstall
Close the game and run `UNINSTALL BFR.bat`. The shortcut is removed and the pack's data folder is renamed (kept) under
`%LOCALAPPDATA%\\Packages\\Microsoft.MSEsper_8wekyb3d8bbwe\\AC\\Temp\\`. The game and your saves are never changed.

## Troubleshooting
The loader's log is `%LOCALAPPDATA%\\Packages\\Microsoft.MSEsper_8wekyb3d8bbwe\\AC\\Temp\\CwoLoaderBFR\\cwo_loader.log`.

## Online
Play online only with players who run this same pack. Offline-only parts (CREATE's pages, Spectator, Create a Mode's own
modes, Single Play, Boss Raid, the six-fighter switch, the Mirror / Hologram / Spiritual Projection doubles, skills you make
and saved Create a Stage maps) stay offline.

## Credits
- Battle for Reality skill data: the pack's author.
- Loader, launcher, menu patches and tools: written for this project by Claude (Anthropic), working with the pack owner.
- Codex (OpenAI): the project's first work orders (the CWO jobs the loader is named after), early file-format readers, and
  the independent-loader rules the loader is built under.
- Testers and bug reporters - thank you (the same list as MOD LIST > COMMUNITY SHOUT-OUT in the game):
{testers}
- Phantom Dust belongs to Microsoft. This pack contains none of the game's files; you need your own copy of the game.
'''


def _deleted(f):
    return os.path.splitext(os.path.basename(f.replace(chr(92), '/')))[0].lower() in DELETED


def list_1507():
    """the stage list without the deleted stages; the synced stages take their MODS rows (kill line, floors, spawns)"""
    new = {l.split('\t')[0]: l for l in open(os.path.join(STAGE_PATCHES, 'rows.txt'), encoding='utf-8').read().splitlines() if l.strip()}
    out = []
    for l in MX_LIST.splitlines():
        k = l.split('\t')[0]
        if not l.strip() or k in DELETED:
            continue
        out.append(new.get(k, l))
    return '\n'.join(out) + '\n'


def main(out):
    os.makedirs(out, exist_ok=True)
    files, notes = {}, []

    def put_bytes(rel, b):
        dst = os.path.join(out, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, 'wb') as f:
            f.write(b)
        files[rel.replace('\\', '/')] = hashlib.sha256(b).hexdigest()

    for name, src in LAUNCHER:
        put_bytes(os.path.join('launcher', name), open(src, 'rb').read())
    # 1.50.73 the published source/ = the staged public source the launcher and loader were just built from
    SRC_STAGE = os.path.join(os.path.dirname(ROOT), '26_Public_Release', 'BFR_source_staging', 'source')
    for d, _, fs in os.walk(SRC_STAGE):
        if os.path.relpath(d, SRC_STAGE).split(os.sep)[:2] == ['loader', 'build']:
            continue
        for fn in fs:
            q = os.path.join(d, fn)
            dst = os.path.join(out, 'source', os.path.relpath(q, SRC_STAGE))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copyfile(q, dst)
    for name, src, want in DATA_FILES:
        b = open(src, 'rb').read()
        assert want is None or hashlib.sha256(b).hexdigest().startswith(want), (src, want)
        put_bytes(os.path.join('data', name), b)
    put_bytes(os.path.join('data', 'CwoNextMap.txt'), b'off\n')
    put_bytes(os.path.join('data', 'CwoVisions.txt'), b'off\n')          # 1.44 Visions: off until the player picks one
    put_bytes(os.path.join('data', 'CwoTheme.txt'), b'auto\n')           # 1.43 holiday themes by date
    put_bytes(os.path.join('data', 'CwoCommunitySkills.txt'),             # 1.50.19 the approved community skills (600-639)
              open(os.path.join(ROOT, 'packs', 'bfr_community', 'CwoCommunitySkills.txt'), 'rb').read())
    # 1.45 (owner 2026-11-03: the names did not match the outfits - "colors 1 through 15 like we had before"): no outfit
    # names; the install removes an old CwoOutfitNames.txt from the data folder
    for rel, name, want, what in PATCHES:
        new = open(os.path.join(MENUS, name), 'rb').read()
        assert hashlib.sha256(new).hexdigest().startswith(want), (name, want)
        if rel.lower().endswith('mainmenu_en.ssb'):
            new = version_line(new)                                # 1.50.1 the main menu line shows the pack's version
        old = open(os.path.join(CLEAN, rel), 'rb').read()
        p, changed, copied = make_patch(old, new)
        assert apply_patch(p, old) == new, 'patch round trip ' + name
        put_bytes(os.path.join('data', 'patches', rel + '.cwopatch'), p)
        notes.append({'file': rel.replace('\\', '/'), 'clean_sha256': hashlib.sha256(old).hexdigest(),
                      'result_sha256': hashlib.sha256(new).hexdigest(), 'our_bytes': changed, 'copied_from_your_file': copied,
                      'carries': what})
    for rel, src_rel, ours, want, what in DERIVED:
        old = open(os.path.join(CLEAN, src_rel), 'rb').read()
        new = open(ours, 'rb').read() if ours else old
        if want:
            assert hashlib.sha256(new).hexdigest().startswith(want), (ours, want)
        p, changed, copied = make_patch(old, new)
        assert apply_patch(p, old) == new, 'patch round trip ' + rel
        put_bytes(os.path.join('data', 'patches', rel + '.cwopatch'), p)
        put_bytes(os.path.join('data', 'patches', rel + '.cwofrom'), src_rel.encode('ascii'))
        notes.append({'file': rel.replace('\\', '/'), 'made_from': src_rel.replace('\\', '/'),
                      'clean_sha256': hashlib.sha256(old).hexdigest(), 'result_sha256': hashlib.sha256(new).hexdigest(),
                      'our_bytes': changed, 'copied_from_your_file': copied, 'carries': what})
    sman = json.load(open(os.path.join(STAGE_PATCHES, 'MANIFEST.json')))
    for sp in sman['patches']:
        rel = os.path.normpath(sp['file'])
        if os.path.splitext(os.path.basename(rel))[0].lower() not in STAGE_IDS and                 os.path.basename(os.path.dirname(rel)).lower() not in STAGE_IDS + ('mx401', 'mx402'):     # 1.50.34 com\<stage>\comdataN.cad
            continue
        for ext in ('.cwopatch', '.cwofrom'):
            f = os.path.join(STAGE_PATCHES, 'data', 'patches', rel + ext)
            if os.path.isfile(f):
                put_bytes(os.path.join('data', 'patches', rel + ext), open(f, 'rb').read())
        notes.append({'file': sp['file'], 'made_from': sp['sources'], 'result_sha256': sp['result_sha256'],
                      'our_bytes': sp['our_bytes'], 'copied_from_your_file': sp['copied_from_your_files'],
                      'carries': 'new stage (1.48, patch format 2)'})
    STAGES_1112 = {p['file'].lower() for p in sman['patches']}           # 1.50.4: the MODS versions win
    for sp in json.load(open(os.path.join(EXTRA_PATCHES, 'MANIFEST.json')))['patches']:
        if sp['file'].lower() in REPLACED_150 or sp['file'].lower() in STAGES_1112 or _deleted(sp['file']) or                 sp['file'].lower() in {d[0].replace(chr(92), '/').lower() for d in DERIVED}:   # 1.50.34 Edgar's new outfit 6 wins
            continue
        for ext in ('.cwopatch', '.cwofrom'):
            put_bytes(os.path.join('data', 'patches', *sp['file'].split('/')) + ext,
                      open(os.path.join(EXTRA_PATCHES, 'data', 'patches', *sp['file'].split('/')) + ext, 'rb').read())
        notes.append({'file': sp['file'], 'made_from': sp['sources'], 'result_sha256': sp['result_sha256'],
                      'our_bytes': sp['our_bytes'], 'copied_from_your_file': sp['copied_from_your_files'],
                      'carries': sp['carries'] + ' (1.49.1, patch format 2)'})
    put_bytes(os.path.join('data', 'game', 'Assets', 'Data', 'Map', 'mx_list.txt'), list_1507().encode('utf-8'))
    for rel, src in FACE_FILES:
        put_bytes(os.path.join('data', 'game', rel), open(src, 'rb').read())
    # 1.49 pack parity (owner 2026-11-11: "they need to have the exact same stuff"): every settings / state file of the BFR
    # data folder as it is installed (it wins over the defaults above), and the models the loader keeps beside them as
    # patches\_data\ (build_data_parity.py, run on the installed CwoLoaderBFR just before this build)
    prep = json.load(open(os.path.join(PARITY, 'parity.json')))
    for rel, h in prep['files'].items():
        if rel.lower() == 'cworecords.txt':                 # 1.50.34 (owner 2026-11-15: records are each player's own profile)
            continue
        b = open(os.path.join(PARITY, 'data', *rel.split('/')), 'rb').read()
        assert hashlib.sha256(b).hexdigest() == h, rel
        put_bytes(os.path.join('data', *rel.split('/')), b)
    for pp in prep['patches']:
        if _deleted(pp['file']):
            continue
        for ext in ('.cwopatch', '.cwofrom'):
            put_bytes(os.path.join('data', 'patches', *pp['file'].split('/')) + ext,
                      open(os.path.join(PARITY, 'data', 'patches', *pp['file'].split('/')) + ext, 'rb').read())
        notes.append({'file': pp['file'], 'made_from': pp['sources'], 'result_sha256': pp['result_sha256'], 'our_bytes': pp['our_bytes'],
                      'copied_from_your_file': pp['copied_from_your_files'], 'carries': 'data folder model (1.49 pack parity)'})
    # 1.50 (packs/bfr_150): after the parity files, so the owner's 2026-11-12 picks win (modes, new skills, sound fix ...)
    for rel, h in P150_MAN['files'].items():
        b = open(os.path.join(P150, 'data', *rel.split('/')), 'rb').read()
        assert hashlib.sha256(b).hexdigest() == h, rel
        put_bytes(os.path.join('data', *rel.split('/')), b)
    for pp in P150_MAN['patches']:
        if _deleted(pp['file']):
            continue
        if pp['file'].lower() in STAGES_1112:                          # 1.50.5: the stage patches of make_bfr_stages win
            continue
        for ext in ('.cwopatch', '.cwofrom'):
            f = os.path.join(P150, 'data', 'patches', *pp['file'].split('/')) + ext
            if os.path.isfile(f):
                put_bytes(os.path.join('data', 'patches', *pp['file'].split('/')) + ext, open(f, 'rb').read())
        notes.append({'file': pp['file'], 'made_from': pp['sources'], 'result_sha256': pp['result_sha256'], 'our_bytes': pp['our_bytes'],
                      'copied_from_your_file': pp['copied_from_your_files'], 'carries': pp['carries'] + ' (1.50)'})
    RENAMED = {}
    for n in sorted(os.listdir(SYNC_DATA)):                            # 1.50.7 the newer MODS data files win
        put_bytes(os.path.join('data', RENAMED.get(n, n)), open(os.path.join(SYNC_DATA, n), 'rb').read())
    put_bytes('install_bfr.ps1', INSTALL_PS1.replace('\n', '\r\n').encode('utf-8'))
    import save_backup_ps1                                          # 1.50.43 BACKUP MY SAVE (owner 2026-11-16)
    put_bytes('backup_save.ps1', save_backup_ps1.BACKUP_PS1.replace(chr(10), chr(13) + chr(10)).encode('ascii'))
    put_bytes('BACKUP MY SAVE.bat', BAT.format('backup_save.ps1').encode())
    put_bytes('uninstall_bfr.ps1', UNINSTALL_PS1.replace('\n', '\r\n').encode('utf-8'))
    put_bytes('INSTALL BFR.bat', BAT.format('install_bfr.ps1').encode())
    put_bytes('UNINSTALL BFR.bat', BAT.format('uninstall_bfr.ps1').encode())
    # 1.50.70 (owner 2026-11-17: a testers and bug reporters shout-out in the credits, the names from the game's own list):
    # every active line of the pack's CwoShoutOut.txt ("Name|why"), as the game shows it
    testers = []
    so = os.path.join(out, 'data', 'CwoShoutOut.txt')
    if os.path.isfile(so):
        for line in open(so, encoding='utf-8'):
            line = line.strip()
            if not line or line.startswith('#') or '|' not in line:
                continue
            name, why = line.split('|', 1)
            testers.append('  - **%s**: %s' % (name.strip(), why.strip()))
    put_bytes('README.md', README.format(ver=VERSION, testers=chr(10).join(testers) or '  - (none yet)').encode('utf-8'))
    with open(os.path.join(out, 'VERSION'), 'w', newline='\n') as f:
        f.write(VERSION + '\n')
    man = {'name': 'BFR mod pack', 'version': VERSION, 'game': 'Phantom Dust (Microsoft Store) 1.40',
           'files': files, 'patches': notes}
    with open(os.path.join(out, 'MANIFEST.json'), 'w', newline='\n') as f:
        json.dump(man, f, indent=1)
    # 1.50.1 (owner 2026-11-12: "each update is signed with a key only I hold, and the launcher refuses anything unsigned"): the
    # manifest (every file's SHA-256) is signed with the owner's private key; no key on this PC = no pack
    import subprocess
    r = subprocess.run([SIGNER, 'sign', SIGN_KEY, os.path.join(out, 'MANIFEST.json'), os.path.join(out, 'MANIFEST.sig')],
                       capture_output=True, text=True)
    assert r.returncode == 0, 'MANIFEST not signed: ' + r.stdout + r.stderr
    r = subprocess.run([SIGNER, 'verify', os.path.join(ROOT, 'src', 'cwo_update_key.h'), os.path.join(out, 'MANIFEST.json'),
                        os.path.join(out, 'MANIFEST.sig')], capture_output=True, text=True)
    assert r.returncode == 0, 'signature does not verify with the launcher key: ' + r.stdout
    print('MANIFEST signed and verified with the launcher\'s public key')
    print('BFR pack %s: %d files, %d patches (%d bytes of ours, %d copied from the player own files) -> %s'
          % (VERSION, len(files), len(notes), sum(n['our_bytes'] for n in notes), sum(n['copied_from_your_file'] for n in notes), out))
    for n in notes:
        print('  %-36s ours %6d  copied %6d' % (n['file'], n['our_bytes'], n['copied_from_your_file']))


if __name__ == '__main__':
    main(sys.argv[1])
