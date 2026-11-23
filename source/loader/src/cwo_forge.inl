static bool hf_show();                                         // v5.112 cwo_hudface.inl
static void hf_set_show(bool on);
// ---- v2.0 FORGE MODE (owner 2026-10-24) ----
// Included by cwo_loader.cpp (one translation unit). Our own design: a palette of categories -> items with a
// per-item copy limit, grab / place / delete / duplicate, physics "fixed" = locked, snap angles 90/45/30/15/5, reach 5 m
// clamped 2..22 m, saves as a map variant.
//
// How it enters the game (Phantom Dust's own mechanisms, Ghidra batch_015, 074a/b):
//   - the main-menu script gets a FORGE MODE item (tools/build_forge_menu.py). Choosing it calls script native 2.8,
//     an empty slot of the menu natives table (0x1403eede0 + 8*8, zero in this build and called by no game script),
//     which this loader fills at start-up; then the item starts the game's Single Play mode (native 2.22(260),
//     mode object *0x1404c01c8: +0x7C mode 4, +0x08 bit 0x40000 = left the mode, +0x60 = its state object whose
//     +0x00 is 0 stage list / 1 loading / 2 deck list / 3 preset list / 4 in a match);
//   - while that Forge session lasts, a Single Play match is a Forge match: the editor below runs instead of the
//     stage creator panel.
// The editor works on the stage's own object instances (instance n = placement row n, Ghidra batch_031/068):
// position +0x1E0 through method +0x158, rotation f32 radians through method +0x168 (FUN_1401da020 stores them x
// 5215.19 as ints at +0x1EC). "Storage" = instances whose stage-file position lies below the stage's storage line
// (Map\<stage>.forge); the Forge canvas stage keeps its spare copies there, so placing / duplicating takes one out.
// Saved maps: <TEMP>\CwoLoader\forge\<stage>.fvar (every instance's state), markers in markers_stNN.csv as before;
// tools/forge_bake.py builds a stage file from them (installed only with an owner go, game closed).

static const ULONG_PTR RVA_NATIVES_G2 = 0x3eede0, RVA_MODE_OBJ = 0x4c01c8, RVA_STAGE_LIST = 0x509ba0, RVA_STAGE_LIST_N = 0x50a5d0;
static const int FORGE_NATIVE = 8, MODE_SINGLE_PLAY = 4, SP_MATCH = 4;
static const ULONG_PTR VT_SET_ROTATION = 0x168, OBJ_ROT = 0x1EC, ACTOR_CAMERA = 0x12E5C;
static const int FMAX = 1024;
static volatile LONG g_fmade_nomem;                     // v5.32 the game said no memory for one more copy
static const float F_DIST_DEF = 50.0f, F_DIST_MIN = 20.0f, F_DIST_MAX = 220.0f;   // Reach: 5 m, 2..22 m (PD units = 0.1 m)
static const float F_HIDE_DROP = 3000.0f;                                          // deleted stock objects: 300 m below
static const int F_SNAPS[] = {90, 45, 30, 15, 5, 1, 0};                            // 0 = free (turn while held)

typedef void(__fastcall *PFN_SETROT)(void *, const float *);
typedef INT64(__fastcall *PFN_NATIVE)(INT64 *);

struct FObj { float orig_pos[3], orig_rot[3], pos[3], rot[3]; int obj; unsigned char placed, locked, stored, moved; };
struct FDesign { int obj, total, placed, cat; const char *disp; const float *box; };
struct FMark { char type[12]; float p[3]; int skill, track; float size, speed, hold; };   // v2.9: EFFECT markers carry skill / part / size; v5.36 hold: the step it is held on (-1 none)
struct FReq { int op, a; };
enum { F_GRAB = 1, F_PLACE, F_PLACE_LOCK, F_DELETE, F_COPY, F_LOCK, F_SPAWN, F_MARK, F_SAVE, F_LOAD, F_RESET, F_TURN,
       F_ROT_RESET, F_MARK_DELETE, F_UNDO, F_REDO, F_MARK_CLEAR, F_GOTO, F_CLEAR_DESIGN, F_ISO, F_CLEAR_ALL, F_GOTO_OBJ, F_SKY,
       F_GOTO_SP };                                      // v5.36 fly to one of the stage's own start points
static const char *F_MARK_TYPES[4] = {"SPAWN", "CAPSULE", "KILLZONE", "EFFECT"};
static const char *F_MARK_NAMES[4] = {"Spawn point", "Capsule spot", "Kill zone point", "Stage effect"};
static const char *F_MARK_DESC[4] = {"Where a player starts. The first 4 are players 1-4", "Where a skill capsule appears (goes to the nearest spawn)",
                                     "A 5 m pit: falling in sends you home, -3 health",
                                     "A skill's effect plays here (skill, size, part: Stage Creator menu)"};

static volatile LONG g_forge_seen_sp;                // the session has reached Single Play
static volatile LONG g_forge_sp_state = -1;          // Single Play state, for the banners
static FObj g_fo[FMAX];
static int g_fo_n = -1;
static void *g_fo_array;                             // instance array the capture belongs to
static FDesign g_fd[256];
static int g_fd_n;
static char g_fcat[32][32];
static int g_fcat_n;
// descriptor lines: "object <stage-file name>|<display name>" (exact, case-insensitive) or "match <part of a name>"
struct FMatch { char key[32]; char disp[32]; int cat; bool exact; bool has_box; float box[6]; };   // box: object space min/max
static FMatch g_fmatch[512];
static int g_fmatch_n;
static float g_storage_below = -1e30f;
static float g_floor_y = -1e30f;                     // descriptor "floor_y": magnets also snap to the floor
static char g_fstem[32];                             // stage file stem ("st20", "mx008")
static FMark g_mk[256];
static int g_mk_n;
static FReq g_fq[32];
static volatile LONG g_fq_head, g_fq_tail;
// editor state: hold (game thread), menus (worker thread)
static int g_hold = -1;
static float g_hold_dist = F_DIST_DEF, g_hold_hoff, g_hold_rot[3];
static volatile LONG g_ftarget = -1, g_fmark_near = -1;
static volatile LONG g_fmark_any = -1;               // nearest marker at any distance (HUD compass)
static float g_fmark_dist, g_fmark_rel;              // its distance (units) and direction relative to the camera (radians)
static float g_faim[3];
static volatile LONG g_fheld;                        // buttons held now (worker -> game thread)
static volatile LONG g_faxis, g_fsnap = 3, g_ffreeze = 1, g_fplay;
// g_fmenu (declared with the loader globals): 0 none, 1 object menu, 2 Forge menu
static volatile LONG g_fmenu_level, g_fmenu_cat;   // g_fmenu_sel: cwo_loader.cpp (v4.2, the Skill Creator reads it)
static float g_ffrozen[4][3];
// v2.1 polish (owner test 2026-10-24): can't be hurt, hide own character, highlight, undo/redo, show only one kind
static volatile LONG g_fgod = 1, g_fhide_player, g_fbob = 1;
static volatile LONG g_fmenu_mtype;                  // markers: the type the placed-markers list shows (menu level 2)
static int g_fiso_mode, g_fiso_obj = -1;             // 0 all shown, 1 only g_fiso_obj, 2 all but g_fiso_obj (game thread)
static bool g_fhidden[FMAX];                          // moved out of sight by "show only" (game thread)
static int g_fbob_inst = -1;                          // the target being bobbed (game thread)
struct FSnap { unsigned char placed[FMAX], locked[FMAX]; float pos[FMAX][3], rot[FMAX][3]; int mk_n; FMark mk[256]; int fo_n; };
static FSnap g_hist[24];                              // undo history: g_hist[g_hcur] = the current state
// v2.2: coordinates editor (menu 3: the held object stays where the numbers say), magnets, pause menu (menu 4),
// test the map, exit Stage Creator, capsules and effects at the markers
static volatile LONG g_hold_fixed;
static float g_hold_fixpos[3];
static volatile LONG g_fmagnet = 1, g_fmagnet_hit;
static volatile LONG g_fexit_phase, g_fexit_save;    // 0 none; 1 forfeit (View held); 2 wait for the lists; 3 B taps
static ULONGLONG g_fexit_t;
static volatile LONG g_ftest_start;                  // game thread: put players on the Forge spawns for a test
static bool g_ftest_keep_fly, g_ftest_keep_hud;
static volatile LONG g_fmk_skill[2] = {122, 120};    // spawn / kill-zone marker look: the effect of this skill (Photon / Paralyze Barrier)
static volatile LONG g_fmk_fx = 1, g_fmk_change = 1;
static volatile LONG g_fcaps = 1;                    // real capsules at the 3 nearest capsule spots
static int g_hn, g_hcur;
static bool g_ffrozen_ok[4];
static LARGE_INTEGER g_flast;
static bool g_fapplied;                              // play-in-matches variant applied for this match
static bool g_fplay_match;                           // this match plays a saved Forge map (kill zones on)
// self-test stand-ins
static void **g_ft_inst;
static int g_ft_n;
static unsigned char *g_ft_rows;
static float g_ft_storage = -1e30f;
static const wchar_t *g_ft_desc;                     // self-test: a descriptor file to read instead of Map\<stage>.forge
static int g_fd_names = -2;                          // name count the categories were made with
static ULONG_PTR g_ft_mode;

static float forge_speed() { return g_fprecise ? 0.25f : 1.0f; }

// ---- native 2.8 ----
static volatile LONG g_cs_hold_cover;                         // v5.32 Create a Stage: black while the loader passes the lists
static volatile LONG g_cs_empty_pending, g_lv_reload;          // v5.32 a stage template's objects put away; the view reloads
static volatile LONG g_cr_entry;                                  // v5.127 the session began from Edgar's Dream > CREATE (cwo_create.inl)
static int g_cs_go = -1;                                      // v5.32 the CREATE A STAGE entry being built (cwo_createstage.inl)
static bool cs_auto_step(LONG st, ULONGLONG now);
static void f_post(int op, int a);                              // below
static int f_stage_spawns(float out[][3], int max);             // v5.36 below: the stage's own start points
static volatile LONG g_ed_direct = -1, g_ed_auto_choose;   // v5.27 an Edgar's Dream page chosen in the game's menu
static INT64 edm_native(int op, INT64 *args);
static void edm_session_start();
static INT64 lv_native(int op, INT64 *args);
static INT64 sl_native(int op, INT64 *args);
static INT64 sg_native(int op, INT64 *args);
static INT64 ls_native(INT64 *args);                              // v5.91 cwo_lobstrip.inl                        // v5.87 cwo_selgrid.inl
static INT64 epg_native(int op, INT64 *args);
static INT64 bn_native(int op, INT64 *args);                     // v5.57 cwo_bfrnet.inl
static INT64 bn_pause_lock_native(int op);                       // v5.152 cwo_bfrnet.inl
static INT64 ars_made_native(INT64 *args);                       // v5.152 cwo_arsenal.inl
static INT64 fb_native(INT64 *args);                             // v5.154 cwo_feedback.inl
static INT64 ars_title_native(INT64 *args);                    // v5.59 cwo_arsenal.inl
static INT64 ars_room_native();                                 // v5.64 cwo_arsenal.inl
static INT64 ad_native(int op, INT64 *args);                    // v5.64 cwo_arsdata.inl
static INT64 lt_native(int op, INT64 *args);
static INT64 mask6_native(int op, INT64 *args);                 // cwo_schoolcheck.inl (included later)
static INT64 mask6_icon_native(INT64 *args);                   // cwo_schoolcheck.inl
static INT64 st_native(int op);
static INT64 el_native(int op);
static INT64 dn_native(int op);                                 // cwo_daynight.inl (included later)                                 // cwo_effectlab.inl (included later)                                 // cwo_stagetheme.inl (included later)
static int st_next();
static const char *st_name(int t);
static INT64 __fastcall forge_native(INT64 *args) {
    int op = 0;
    __try { op = args ? (int)args[0] : 0; } __except (EXCEPTION_EXECUTE_HANDLER) { op = 0; }
#ifdef CWO_EXPIRES
    if (g_expired && !((op >= 35 && op <= 43) || (op >= 55 && op <= 60))) return ex_native(op, args);   // v5.37
#endif
    if (op >= 21 && op <= 28) return lob3_native(op, args);       // v5.3 the 3 VS 3 LOBBY (tools/build_lobby3.py)
    if (op >= 35 && op <= 38) return ars_native(op, args);        // v5.9 the Xbox Live lobby's rule list (build_arsenal_menus.py)
    if (op == 39) return ls_native(op, args);                     // v5.16 SESSION INFO's 'This Session' row
    if (op >= 40 && op <= 43) return gt_native(op, args);        // v5.18 the split-screen lobby's game type (build_game_type_menu.py)
    if (op >= 57 && op <= 60) return gt_native(op, args);        // v5.31 its lobby rows for the CPUs (build_lobby_slots.py)
    if (op == 44) return on_native(args);                         // v5.19 the COLORS list's outfit names (build_colors_names.py)
    if (op >= 46 && op <= 48) return edm_native(op, args);        // v5.27 the Edgar's Dream menu (build_edgar_menu.py)
    if (op == 49 || op == 50) return lv_native(op, args);         // v5.28 the stage behind the level list (build_stage_view.py)
    if (op == 123) return mask6_icon_native(args);                // v5.178 the MASK Illusion row's icon (build_ars_mask6.py v2)
    if (op == 121 || op == 122) return mask6_native(op, args);   // v5.166 Edit Arsenal's MASK: the Illusion row (build_ars_mask6.py)
    if (op == 120) return sl_native(op, args);                    // v5.157 compact split-screen STAGE LIST (build_compact_list.py; 80 is the Lab's BFR row)
    if (op >= 70 && op <= 79) return sl_native(op, args);        // v5.43 (+77 v5.73 select portraits) the Map Slot 7 packs as stage-list rows (build_stage_rows.py)
    if (op >= 51 && op <= 54) return epg_native(op, args);        // v5.29 the Edgar's Dream pages in the game's menu (build_ed_pages.py)
    if (op == 61) return epg_native(op, args);                    // v5.32 CREATE A STAGE: the menu background or the stage (build_create_stage_page.py)
    if (op == 55 || op == 56) return lt_native(op, args);         // v5.29 the Xbox Live lobby's game type (build_live_gametype.py)
    if (op >= 80 && op <= 82) return bfr_native(op, args);        // v5.56 the Lab's Battle for Reality rows (build_lab_bfr.py)
    if (op == 116 || op == 117) return bn_pause_lock_native(op);
    if (op == 128 || op == 129) return dn_native(op);   // v5.200.0 the pause menu's Day and Night row (build_pause_daynight.py)
    if (op == 126 || op == 127) return el_native(op);   // v5.193.0 the Lab's Effect Lab row / 127: its page hides while it shows                          // v5.193.0 the Lab's Effect Lab row (build_lab_effectlab.py)
    if (op == 124 || op == 125) return st_native(op);              // v5.192.0 the pause menu's Theme row (build_pause_theme.py)
    if (op == 119) return fb_native(args);                       // v5.154 main menu FEEDBACK / TO-DO LIST (build_mainmenu_feedback.py)
    if (op == 118) return ars_made_native(args);                 // v5.152 the Xbox Live arsenal list: skills you made stay offline (build_live_madeskills.py)   // v5.152 the pause menu's Teammate Lock-on row (build_pause_lockon.py)
    if (op == 113 || op == 114) {                                  // v5.112 split-screen OPTIONS "HUD Faces" (build_vs_faces_row.py)
        static char t[32];
        if (op == 114) hf_set_show(!hf_show());
        sprintf_s(t, "HUD Faces: %s", hf_show() ? "ON" : "OFF");
        return (INT64)(ULONG_PTR)t;
    }
    if ((op >= 89 && op <= 93) || op == 111 || op == 112) return bn_native(op, args);   // v5.95 + the rule cycle         // v5.62 the lobby lists (build_live_bfr.py v2)
    if (op == 88) return ars_title_native(args);                  // v5.59 the arsenal list's title with its number (build_ars_title.py)
    if (op == 95) { ml_open(); return 0; }
    if (op == 115) { InterlockedExchange(&g_vt_req, 1); return 0; }   // v5.136 the Lab's View Skill Vault row (build_lab_vault.py)
    if (op == 103 || op == 104) return hp_native(op, args);
    if (op == 110) return ls_native(args);                       // v5.91 the lobby face strips (build_lobby_rows.py)
    if (op >= 106 && op <= 109) return sg_native(op, args);       // v5.87 the select grid (build_select_grid.py)       // v5.86.2 OPTIONS > HUD PRESET (build_hud_option.py v2)
    if (op == 101) {                                              // v5.86 the COLORS window's side (build_colors_side.py)
        // args[2] = the highlighted grid slot, args[1] = 1 on the Xbox Live select (its top row has 3 faces, then rows of 4);
        // 1 = the window on the right (the face is in the left half), 0 = on the left
        int slot = (int)args[2], live = (int)args[1];
        int col = live ? (slot < 3 ? slot : (slot - 3) % 4) : slot % 4;
        return col <= 1 ? 1 : 0;
    }                       // v5.80 the main menu's MOD LIST (build_modlist_menu.py)
    if (op == 94) return ars_room_native();                       // v5.64 Add Arsenal: the next list with room (build_ars_options.py)
    if (op >= 96 && op <= 99) return ad_native(op, args);         // v5.64 Arsenal Data per stage (build_ars_data.py)
    if (op >= 83 && op <= 87) return bn_native(op, args);         // v5.57 the lobby's modes, tie-breaker vote, Next Map row (build_live_bfr.py)
    if (op == 1) {
        InterlockedExchange(&g_forge_seen_sp, 0);
        InterlockedExchange(&g_forge_session, 1);
        InterlockedExchange(&g_ed_wait_release, 1);              // v3.5.1 the main menu's A must not choose here too
        InterlockedExchange(&g_ed_open, 1);                       // v3.3 the Edgar's Dream chooser first
        InterlockedExchange(&g_ed_mode, 0);
        logf("FORGE: session started from the main menu (native 2.8(1))");
        edm_session_start();                                       // v5.27 a page chosen in the game's menu
    } else if (op == 0) {
        InterlockedExchange(&g_forge_session, 0);
        logf("FORGE: session ended by script (native 2.8(0))");
    }
    return 2;                                        // loader major version (scripts may test it)
}

// explicit check: the slot must be empty (this build: zero, never called); otherwise the native is refused
static bool forge_native_install(ULONG_PTR slot_addr) {
    void **slot = (void **)slot_addr;
    if (*slot != NULL) {
        logf("FORGE: native 2.8 refused, slot %p holds %p (expected empty)", (void *)slot, *slot);
        return false;
    }
    DWORD old;
    if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old)) return false;
    *slot = (void *)(PFN_NATIVE)forge_native;
    VirtualProtect(slot, sizeof(void *), old, &old);
    return true;
}

// ---- game data access (stand-ins in the self-test) ----
static void **f_game_array() { return g_ft_inst ? g_ft_inst : *(void ***)(g_base + RVA_INSTANCES); }
// v2.7: the editor's own table = the game's instances (captured) + copies made on demand (owner)
static void *g_fptr[FMAX];
static int g_fbase_n, g_fmade;
static void **f_array() { return g_fo_n > 0 && g_fo_array == (void *)f_game_array() ? g_fptr : f_game_array(); }
static int f_count() { return g_ft_inst ? g_ft_n : *(unsigned short *)(g_base + RVA_INSTANCE_COUNT); }
static int f_row_obj(int n) {
    if (g_ft_rows) return *(unsigned short *)(g_ft_rows + 0x24 * n);
    int base_row = 0;
    for (int t = 0; t < 2; t++) {
        const unsigned char *rows = *(const unsigned char **)(g_base + RVA_ROW_TABLES + 8 * t);
        int cnt = *(unsigned short *)(g_base + RVA_ROW_COUNTS + 2 * t);
        if (rows && n < base_row + cnt) return *(unsigned short *)(rows + 0x24 * (n - base_row));
        base_row += cnt;
    }
    return -1;
}
static ULONG_PTR f_actor(int k) { return g_actor_test && k == 0 ? g_actor_test : cwo_actors()[k]; }
static ULONG_PTR f_mode_obj() { return g_ft_mode ? g_ft_mode : *(ULONG_PTR *)(g_base + RVA_MODE_OBJ); }
static const char *instance_label(int n) {               // the stage file's object name
    if (g_ft_inst) { static char t[16]; sprintf_s(t, "obj%d", g_fo[n].obj); return t; }
    return instance_name(n);
}
static int f_design_index(int obj);
static const char *f_name(int n) {                       // the display name from Map\<stage>.forge, else the file name
    int k = n >= 0 && n < g_fo_n ? f_design_index(g_fo[n].obj) : -1;
    if (k >= 0 && g_fd[k].disp) return g_fd[k].disp;
    return instance_label(n);
}

// ---- v2.3: collision follows the object (owner test: a deleted table still collided where it stood) ----
// Game facts (Ghidra full pass, reviewed 2026-10-24): an instance's 3 collision objects (intact / broken / third, as
// stored in the stage file) are at +0x90; their world-space copies at +0xA8, made once at setup by FUN_1401d4e10(copy,
// source) from the matrix the global matrix-stack pointer *0x1405a6788 points to (FUN_1401fbbc0). The copy of the
// current state (index u32 +0x58) is filed in the collision quadtree *0x140530200 by FUN_1401d4ff0 when +0x134 == 0.
// The game's own state change (FUN_1401fdbb0, e.g. a breakable breaking) takes it out with FUN_1401d50b0 and files it
// again. The loader does the same around a move: out of the tree, copy remade from the object's new matrix (built as
// the setup builds it: identity, the position as row 3, rows turned by FUN_140290a40's order Z, Y, X), filed again.
static const ULONG_PTR RVA_MATRIX_TOP = 0x5a6788, RVA_COL_UNFILE = 0x1d50b0, RVA_COL_REMAKE = 0x1d4e10, RVA_COL_FILE = 0x1d4ff0;
static const ULONG_PTR OBJ_COL_SRC = 0x90, OBJ_COL_COPY = 0xA8, OBJ_COL_STATE = 0x58, OBJ_COL_UNFILED = 0x134;
typedef void(__fastcall *PFN_COL1)(void *);
typedef void(__fastcall *PFN_COL2)(void *, void *);
// v2.4 (owner: moved lifts were invisible and the old ones still glowed): an object's effects (emitter block per
// state at +0xC8, e.g. a lift's glow pdvkEbJmpPoint and its sound pdvkEbSE) are started by FUN_1401efd10(obj, block,
// 0, 0, 1, 0, &list +0xE0) and take their place from the object's +0x1E0 / +0x1EC when they start (FUN_1401f00f0);
// they never follow. The game's own state change (e.g. FUN_1401e7aa0) ends the list with FUN_1401efe50(list) and
// starts the new state's block the same way; ended emitters unlink themselves (FUN_1401f0070). The loader does that
// after a move: effects restart at the new place; picked up = effects end until it is put down.
typedef void *(__fastcall *PFN_FXSTART)(void *, void *, void *, void *, unsigned char, char, void *);
static const ULONG_PTR RVA_OBJ_FX_END = 0x1efe50, RVA_OBJ_FX_START = 0x1efd10, OBJ_FX_BLOCK = 0xC8, OBJ_FX_LIST = 0xE0;
struct FColApi { PFN_COL1 unfile, file; PFN_COL2 remake; float **matrix_top; PFN_COL1 fx_end; PFN_FXSTART fx_start; };
static FColApi *g_ft_col;                            // self-test stand-ins
static volatile LONG g_fcol = 1;                     // Stage Creator menu: collision follows moved objects
static volatile LONG g_fcopies = 1;                  // Stage Creator menu: make more copies when all are in use
static volatile LONG g_fsfx_skill = 104, g_fsfx_size = 2, g_fsfx_part = 1, g_fsfx_speed = 3;   // next stage effect: skill, size, part, speed
static volatile LONG g_fsfx_hold = -1;               // v5.36 the step the next stage effect is held on (-1: it plays)
static const int F_SFX_NSIZES = 13, F_SFX_NSPEEDS = 8;
static const float F_SFX_SIZES[F_SFX_NSIZES] = {0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f, 5.0f, 8.0f, 12.0f, 20.0f, 30.0f, 50.0f, 80.0f};
static const float F_SFX_SPEEDS[F_SFX_NSPEEDS] = {0.1f, 0.25f, 0.5f, 1.0f, 1.5f, 2.0f, 3.0f, 0.0f};   // 0 = frozen
// v3.0 (owner): what each part (EFT track) is, from the census of all 496
// effects and the game code that starts them (07_Combat_and_Skills/AURA_FX_2026-10-24.md section 2)
static const char *F_SFX_PART[6] = {"0: the move (on the player)", "1: flying (projectile)", "2: extra", "3: the hit",
                                    "4: held / charging", "5: release"};
static volatile LONG g_fbox_alpha = 2;               // v3.3 menu backing: 0 none (stroked text), 1 black box; v5.36: 2 see-through grey (the game's rectangle)
static void f_rotmat(const float r[3], float L[9]);

static unsigned char g_fx_due[FMAX];                 // effects to start again next frame (placed objects only)

static void f_col_api(FColApi &api) {
    if (g_ft_col) api = *g_ft_col;
    else {
        api.unfile = (PFN_COL1)(g_base + RVA_COL_UNFILE);
        api.file = (PFN_COL1)(g_base + RVA_COL_FILE);
        api.remake = (PFN_COL2)(g_base + RVA_COL_REMAKE);
        api.matrix_top = (float **)(g_base + RVA_MATRIX_TOP);
        api.fx_end = (PFN_COL1)(g_base + RVA_OBJ_FX_END);
        api.fx_start = (PFN_FXSTART)(g_base + RVA_OBJ_FX_START);
    }
}

static void f_col_sync(int n, float dy) {
    if (g_ft_inst && !g_ft_col) return;              // self-test without the collision stand-ins
    char *o = (char *)f_array()[n];
    if (!o || !g_fcol) return;
    FColApi api;
    f_col_api(api);
    __declspec(align(16)) static float m[16];
    float r[3], L[9];
    const int *ri = (const int *)(o + OBJ_ROT);
    for (int k = 0; k < 3; k++) r[k] = (float)ri[k] / ANGLE_UNITS;
    f_rotmat(r, L);
    const float *pos = (const float *)(o + OBJ_POS);
    for (int i = 0; i < 3; i++) { m[4 * i] = L[3 * i]; m[4 * i + 1] = L[3 * i + 1]; m[4 * i + 2] = L[3 * i + 2]; m[4 * i + 3] = 0.0f; }
    m[12] = pos[0]; m[13] = pos[1] + dy; m[14] = pos[2]; m[15] = 1.0f;
    unsigned state = *(unsigned *)(o + OBJ_COL_STATE);
    bool filed = *(int *)(o + OBJ_COL_UNFILED) == 0;
    for (int k = 0; k < 3; k++) {
        void *copy = *(void **)(o + OBJ_COL_COPY + 8 * k), *src = *(void **)(o + OBJ_COL_SRC + 8 * k);
        if (!copy || !src) continue;
        if (filed && k == (int)state) api.unfile(copy);
        float *keep = *api.matrix_top;
        *api.matrix_top = m;
        api.remake(copy, src);
        *api.matrix_top = keep;
        if (filed && k == (int)state) api.file(copy);
    }
    void **list = (void **)(o + OBJ_FX_LIST);
    if (*list) api.fx_end(*list);                    // the effects end where they were
    g_fx_due[n] = dy == 0.0f;                        // and start again next frame where it now is, if it is placed
}

// v2.4.1 (owner: a deleted lift kept glowing): effects start one frame after the move, only for placed objects that
// are not being carried, so an effect is never started and ended in the same frame and storage stays dark
static void f_fx_step() {
    if (g_ft_inst && !g_ft_col) return;
    FColApi api;
    f_col_api(api);
    for (int i = 0; i < g_fo_n; i++) {
        if (!g_fx_due[i] || i == g_hold) continue;
        g_fx_due[i] = 0;
        char *o = (char *)f_array()[i];
        if (!o || !g_fo[i].placed) continue;
        unsigned state = *(unsigned *)(o + OBJ_COL_STATE);
        void *block = state < 3 ? *(void **)(o + OBJ_FX_BLOCK + 8 * state) : NULL;
        if (block) api.fx_start(o, block, NULL, NULL, 1, 0, (void **)(o + OBJ_FX_LIST));
    }
}

static void f_set(int n, const float pos[3], const float rot[3]) {
    char *o = (char *)f_array()[n];
    if (!o) return;
    PFN_SETPOS sp = *(PFN_SETPOS *)(*(char **)o + VT_SET_POSITION);
    PFN_SETROT sr = *(PFN_SETROT *)(*(char **)o + VT_SET_ROTATION);
    sp(o, pos);
    sr(o, rot);
    memcpy(g_fo[n].pos, pos, 12);
    memcpy(g_fo[n].rot, rot, 12);
    if (n != g_hold) f_col_sync(n, 0.0f);            // held objects: collision parked away until placed
}

static float f_heading(ULONG_PTR actor) { return (float)*(int *)(actor + ACTOR_CAMERA) / ANGLE_UNITS; }

// ---- per-stage setup: names, categories (Map\<stem>.forge), markers ----
static void f_stage_stem(int stage) {
    sprintf_s(g_fstem, "st%02d", stage);
    if (g_ft_inst) return;
    __try {
        const char *dat = *mx_entry(2 * stage + 1);          // the stage file table: Map Slot 7 may hold a stage pack
        if (dat && dat[0]) {
            char tmp[32];
            strncpy_s(tmp, sizeof(tmp), dat, _TRUNCATE);
            char *dot = strrchr(tmp, '.');
            if (dot) *dot = 0;
            strcpy_s(g_fstem, tmp);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static void f_load_descriptor() {
    g_fcat_n = 0;
    g_fmatch_n = 0;
    g_storage_below = -1e30f;
    g_floor_y = -1e30f;
    strcpy_s(g_fcat[g_fcat_n++], "Markers");
    if (g_ft_inst) g_storage_below = g_ft_storage;
    wchar_t exe[MAX_PATH], path[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    wchar_t *slash = wcsrchr(exe, L'\\');
    if (slash) slash[1] = 0;
    swprintf_s(path, MAX_PATH, L"%sAssets\\Data\\Map\\%S.forge", exe, g_fstem);
    FILE *f = NULL;
    if (g_ft_desc) wcscpy_s(path, g_ft_desc);
    if ((!g_ft_inst || g_ft_desc) && cwo_wfopen(&f, path, L"r") == 0 && f) {
        char line[160];
        while (fgets(line, sizeof(line), f)) {
            line[strcspn(line, "\r\n")] = 0;
            float y;
            if (sscanf_s(line, "storage_below %f", &y) == 1) g_storage_below = y;
            else if (sscanf_s(line, "floor_y %f", &y) == 1) g_floor_y = y;
            else if (!strncmp(line, "category ", 9) && g_fcat_n < 30) strncpy_s(g_fcat[g_fcat_n++], 32, line + 9, _TRUNCATE);
            else if ((!strncmp(line, "match ", 6) || !strncmp(line, "object ", 7)) && g_fmatch_n < 512 && g_fcat_n > 1) {
                FMatch &m = g_fmatch[g_fmatch_n];
                m.exact = line[0] == 'o';
                const char *k = line + (m.exact ? 7 : 6);
                const char *bar = strchr(k, '|');
                size_t kl = bar ? (size_t)(bar - k) : strlen(k);
                if (kl >= sizeof(m.key)) kl = sizeof(m.key) - 1;
                memcpy(m.key, k, kl);
                m.key[kl] = 0;
                _strlwr_s(m.key, sizeof(m.key));
                strncpy_s(m.disp, sizeof(m.disp), bar ? bar + 1 : "", _TRUNCATE);
                char *bar2 = strchr(m.disp, '|');                // "|box x0 y0 z0 x1 y1 z1" after the display name
                m.has_box = false;
                if (bar2) {
                    *bar2 = 0;
                    const char *bx = strstr(line, "|box ");
                    m.has_box = bx && sscanf_s(bx, "|box %f %f %f %f %f %f", &m.box[0], &m.box[1], &m.box[2], &m.box[3], &m.box[4], &m.box[5]) == 6;
                }
                m.cat = g_fcat_n - 1;
                g_fmatch_n++;
            }
        }
        fclose(f);
        logf("FORGE: %s.forge: storage below y %.1f, %d categories", g_fstem, g_storage_below, g_fcat_n - 1);
    }
    strcpy_s(g_fcat[g_fcat_n++], g_fcat_n > 1 ? "Other objects" : "Objects on this map");
}

// descriptor entry for an object name: exact entries first, then the first "match" part; -1 = none
static int f_match_of(const char *name) {
    char low[40];
    strncpy_s(low, sizeof(low), name, _TRUNCATE);
    _strlwr_s(low, sizeof(low));
    for (int i = 0; i < g_fmatch_n; i++)
        if (g_fmatch[i].exact && !strcmp(low, g_fmatch[i].key)) return i;
    for (int i = 0; i < g_fmatch_n; i++)
        if (!g_fmatch[i].exact && strstr(low, g_fmatch[i].key)) return i;
    return -1;
}

static void f_load_markers(int stage) {
    g_mk_n = 0;
    wchar_t path[MAX_PATH];
    marker_path(stage, path);
    FILE *f = NULL;
    if (cwo_wfopen(&f, path, L"r") != 0 || !f) return;
    char line[160];
    while (g_mk_n < 256 && fgets(line, sizeof(line), f)) {
        FMark &m = g_mk[g_mk_n];
        int st;
        m.skill = -1; m.track = 0; m.size = 1.0f; m.speed = 1.0f; m.hold = -1.0f;
        if (sscanf_s(line, "%11[^,],%d,%f,%f,%f,%d,%f,%d,%f,%f", m.type, (unsigned)sizeof(m.type), &st, &m.p[0], &m.p[1], &m.p[2], &m.skill,
                     &m.size, &m.track, &m.speed, &m.hold) >= 5)
            g_mk_n++;
    }
    fclose(f);
}

static void f_save_markers(int stage) {
    wchar_t path[MAX_PATH];
    marker_path(stage, path);
    FILE *f = NULL;
    if (cwo_wfopen(&f, path, L"w") != 0 || !f) return;
    fprintf(f, "type,stage,x,y,z\n");
    for (int i = 0; i < g_mk_n; i++) {
        if (!strcmp(g_mk[i].type, "EFFECT"))                 // v2.9: skill, size, part after the place
            fprintf(f, "%s,%d,%.3f,%.3f,%.3f,%d,%.2f,%d,%.2f,%.1f\n", g_mk[i].type, stage, g_mk[i].p[0], g_mk[i].p[1], g_mk[i].p[2], g_mk[i].skill,
                    g_mk[i].size, g_mk[i].track, g_mk[i].speed, g_mk[i].hold);
        else fprintf(f, "%s,%d,%.3f,%.3f,%.3f\n", g_mk[i].type, stage, g_mk[i].p[0], g_mk[i].p[1], g_mk[i].p[2]);
    }
    fclose(f);
    recount(stage);
}

static void f_designs() {
    g_fd_n = 0;
    g_fd_names = g_name_count;
    for (int i = 0; i < g_fo_n; i++) {
        int k = 0;
        while (k < g_fd_n && g_fd[k].obj != g_fo[i].obj) k++;
        if (k == g_fd_n) {
            if (g_fd_n >= 256) continue;
            g_fd[k].obj = g_fo[i].obj;
            g_fd[k].total = g_fd[k].placed = 0;
            int m = f_match_of(instance_label(i));         // names come from the worker's load_names
            g_fd[k].cat = m >= 0 ? g_fmatch[m].cat : g_fcat_n - 1;
            g_fd[k].disp = m >= 0 && g_fmatch[m].disp[0] ? g_fmatch[m].disp : NULL;
            g_fd[k].box = m >= 0 && g_fmatch[m].has_box ? g_fmatch[m].box : NULL;
            g_fd_n++;
        }
        g_fd[k].total++;
        if (g_fo[i].placed) g_fd[k].placed++;
    }
}

// capture every instance as the stage file placed it (game thread, once per match / instance array)
static bool f_capture(int stage) {
    void **inst = f_array();
    int n = f_count();
    // v5.36 (owner on an empty stage, log 18:39 "stage 34 has no objects": "it won't let me place any down ... fly there
    // does absolutely nothing"): a stage with no objects is captured with 0, so the editor's actions still run there
    if (n < 0 || (!inst && n > 0)) return false;
    if (!inst) n = 0;
    if (n > FMAX) n = FMAX;
    f_stage_stem(stage);
    if (!g_ft_inst && !names_current(stage)) load_names(stage);   // v2.8: names of THIS stage file before matching
    f_load_descriptor();
    for (int i = 0; i < n; i++) {
        FObj &o = g_fo[i];
        memset(&o, 0, sizeof(o));
        o.obj = f_row_obj(i);
        if (inst[i]) {
            memcpy(o.orig_pos, (char *)inst[i] + OBJ_POS, 12);
            const int *r = (const int *)((char *)inst[i] + OBJ_ROT);
            for (int k = 0; k < 3; k++) o.orig_rot[k] = (float)r[k] / ANGLE_UNITS;
        }
        memcpy(o.pos, o.orig_pos, 12);
        memcpy(o.rot, o.orig_rot, 12);
        o.stored = inst[i] && o.orig_pos[1] < g_storage_below;
        o.placed = inst[i] && !o.stored;
    }
    for (int i = 0; i < n; i++) g_fptr[i] = inst[i];
    g_fbase_n = n;
    g_fmade = 0; g_fmade_nomem = 0;
    g_fo_n = n;
    g_fo_array = inst;
    g_hold = -1;
    memset(g_fhidden, 0, sizeof(g_fhidden));
    g_fiso_mode = 0;
    g_fbob_inst = -1;
    f_designs();
    f_load_markers(stage);
    logf("FORGE: captured %d objects of %s (%d designs, %d in storage)", n, g_fstem, g_fd_n,
         [] { int s = 0; for (int i = 0; i < g_fo_n; i++) s += g_fo[i].stored; return s; }());
    if (g_cs_empty_pending && g_ed_mode == 0) {                 // v5.32 a stage template: an empty copy of the stage
        InterlockedExchange(&g_cs_empty_pending, 0);
        f_post(F_CLEAR_ALL, 0);
        logf("Create a Stage: stage template - its objects are put away (every one can be placed again)");
    }
    return true;
}

// ---- saved maps ----
static void f_variant_path(wchar_t *out, const wchar_t *ext) {
    wchar_t dir[MAX_PATH];
    swprintf_s(dir, MAX_PATH, L"%sforge", g_dir);
    CreateDirectoryW(dir, NULL);
    swprintf_s(out, MAX_PATH, L"%s\\%S%s", dir, g_fstem, ext);
}

static bool f_save(int stage) {
    wchar_t path[MAX_PATH], tmp[MAX_PATH], bak[MAX_PATH];
    f_variant_path(path, L".fvar");
    f_variant_path(tmp, L".tmp");
    f_variant_path(bak, L".fvar.bak");
    FILE *f = NULL;
    if (cwo_wfopen(&f, tmp, L"w") != 0 || !f) return false;
    fprintf(f, "CWO-FORGE-VARIANT 1\nstage %d\nfile %s\ninstances %d\n", stage, g_fstem, g_fbase_n);
    if (g_fo_n > g_fbase_n) fprintf(f, "# lines past %d are copies Stage Creator made; they are made again when the map loads\n", g_fbase_n - 1);
    fprintf(f, "# inst obj placed locked x y z rx ry rz\n");
    for (int i = 0; i < g_fo_n; i++) {
        const FObj &o = g_fo[i];
        fprintf(f, "o %d %d %d %d %.3f %.3f %.3f %.6f %.6f %.6f\n", i, o.obj, o.placed, o.locked, o.pos[0], o.pos[1], o.pos[2],
                o.rot[0], o.rot[1], o.rot[2]);
    }
    fclose(f);
    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) CopyFileW(path, bak, FALSE);
    bool ok = MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING) != 0;
    logf("FORGE: saved %s (%d objects) -> %ls: %s", g_fstem, g_fo_n, path, ok ? "ok" : "FAILED");
    return ok;
}

// ---- v2.7 copies on demand ----
// Game facts (Ghidra full pass, reviewed 2026-10-24): the stage-object spawner FUN_140203dc0 makes each placement row's
// object with: constructor = class table 0x1402dc4f0[row +4][row +5]; object +0x4C/+0x4D = class, +0x4A = object
// index (row +0), +0x48 = its slot in the instance array; then methods +0xF8 (k, def +0x18 + 8k: models),
// +0xB0 (def + 2), +0x108 (k, def +0x30 + 8k: collision), +0x128 (k, def +0x60 + 8k), +0x118 (k, def +0x48 + 8k:
// emitters), +0x138 (def +7), +0x140 (row +6), +0x150 (row +7), +0x158 (row +0xC: position), +0x148 (0), +0x168
// (row +0x18: rotation), +0x178 (def +0x78), +0x180 (def); +0x190 finishes it, and FUN_1401cbbc0 files it as a child
// process of the stage manager's object parent (*0x140530ca8 +0x40) with 0x904E28 / 0x6E (which also runs its setup
// FUN_1401fbbc0: collision, effects); last +0x1F8 (array, count) and, when *0x14052ad0f == 1, +0x200 (0x30000).
// def = object definitions *0x1405322b0 + 0x80 * object index. The game's own object array keeps its size: a copy's
// slot +0x48 is 0xFFFF, and every reader of +0x48 (the destructors 0x1401da1c0 ...) checks it against the count first.
// At the end of the stage the game ends the parent, and its process manager ends every child with it
// (FUN_1401cbef0 -> FUN_1401cbe30), copies included; the editor forgets them when the next stage's array appears.
static const ULONG_PTR RVA_CLASS_TABLE = 0x2dc4f0, RVA_OBJ_DEFS = 0x5322b0, RVA_STAGE_MGR = 0x530ca8, RVA_PROC_ADD = 0x1cbbc0,
                       RVA_SPAWN_FLAG = 0x52ad0f;
// v5.32 (owner 2026-10-27): no count of its own - a copy is made until the editor's table is full or the game has no memory left for it
// (Ghidra 065: the class constructors take their object from the pool allocator FUN_140285aa0, which grows the pool from
// the system and returns NULL when it cannot; the collision FUN_1401d3ef0 and the process list FUN_1401cbbc0 have no
// fixed size either; a NULL ends the copy cleanly in f_make_copy)
static const int F_MADE_MAX = FMAX;
typedef void *(__fastcall *PFN_MAKE)();
typedef ULONG_PTR(__fastcall *PFN_M1)(void *);
typedef void(__fastcall *PFN_M2)(void *, ULONG_PTR);
typedef void(__fastcall *PFN_M3)(void *, ULONG_PTR, ULONG_PTR);
typedef ULONG_PTR(__fastcall *PFN_PROC_ADD)(ULONG_PTR, ULONG_PTR, void *, ULONG_PTR, int, int);
struct FMakeApi { const ULONG_PTR *classes; char *defs; ULONG_PTR parent; PFN_PROC_ADD proc_add; bool flag; };
static FMakeApi *g_ft_make;                           // self-test stand-ins

static bool g_stage_family1() { return *(int *)(g_base + 0x3e0304) % 10 == 1; }   // spawner: DAT_1403e0304 % 10 == 1

static const unsigned char *f_row_ptr(int n) {
    if (g_ft_rows) return g_ft_rows + 0x24 * n;
    int base_row = 0;
    for (int t = 0; t < 2; t++) {
        const unsigned char *rows = *(const unsigned char **)(g_base + RVA_ROW_TABLES + 8 * t);
        int cnt = *(unsigned short *)(g_base + RVA_ROW_COUNTS + 2 * t);
        if (rows && n < base_row + cnt) return rows + 0x24 * (n - base_row);
        base_row += cnt;
    }
    return NULL;
}

// a new object like instance `tmpl` (same row: class, parameters, look), appended to the editor's table; -1 if not
static int f_make_copy(int tmpl) {
    if (g_fo_n >= FMAX || g_fmade >= F_MADE_MAX || tmpl < 0 || tmpl >= g_fbase_n) return -1;
    if (!g_ft_make && (g_ft_inst || g_stage_family1())) return -1;   // stand-ins without a maker; Edgar's Lab family loads its rows twice
    const unsigned char *row = f_row_ptr(tmpl);
    if (!row) return -1;
    FMakeApi a;
    if (g_ft_make) a = *g_ft_make;
    else {
        if (g_ft_inst) return -1;
        a.classes = (const ULONG_PTR *)(g_base + RVA_CLASS_TABLE);
        a.defs = *(char **)(g_base + RVA_OBJ_DEFS);
        ULONG_PTR mgr = *(ULONG_PTR *)(g_base + RVA_STAGE_MGR);
        a.parent = mgr ? *(ULONG_PTR *)(mgr + 0x40) : 0;
        a.proc_add = (PFN_PROC_ADD)(g_base + RVA_PROC_ADD);
        a.flag = *(unsigned char *)(g_base + RVA_SPAWN_FLAG) == 1;
    }
    if (!a.defs || !a.parent) return -1;
    unsigned c0 = row[4], c1 = row[5];
    const PFN_MAKE *list = (const PFN_MAKE *)a.classes[c0];
    if (!list || !list[c1]) return -1;
    char *o = (char *)list[c1]();
    if (!o) { InterlockedExchange(&g_fmade_nomem, 1); return -1; }
    unsigned short objn = *(const unsigned short *)row;
    o[0x4C] = (char)c0;
    o[0x4D] = (char)c1;
    *(unsigned short *)(o + 0x4A) = objn;
    *(unsigned short *)(o + 0x48) = 0xFFFF;
    char *def = a.defs + 0x80 * objn;
    void **vt = *(void ***)o;
    auto m2 = [&](int at, ULONG_PTR v) { ((PFN_M2)vt[at / 8])(o, v); };
    auto m3 = [&](int at, ULONG_PTR k, ULONG_PTR v) { ((PFN_M3)vt[at / 8])(o, k, v); };
    for (int k = 0; k < 3; k++) m3(0xF8, k, *(ULONG_PTR *)(def + 0x18 + 8 * k));
    m2(0xB0, (ULONG_PTR)(def + 2));
    for (int k = 0; k < 3; k++) m3(0x108, k, *(ULONG_PTR *)(def + 0x30 + 8 * k));
    for (int k = 0; k < 3; k++) m3(0x128, k, *(ULONG_PTR *)(def + 0x60 + 8 * k));
    for (int k = 0; k < 3; k++) m3(0x118, k, *(ULONG_PTR *)(def + 0x48 + 8 * k));
    m2(0x138, (unsigned char)def[7]);
    m2(0x140, row[6]);
    m2(0x150, row[7]);
    float place[6];
    memcpy(place, row + 0xC, 24);
    m2(0x158, (ULONG_PTR)place);
    m2(0x148, 0);
    m2(0x168, (ULONG_PTR)(row + 0x18));
    m2(0x178, *(ULONG_PTR *)(def + 0x78));
    m2(0x180, (ULONG_PTR)def);
    ULONG_PTR fin = ((PFN_M1)vt[0x190 / 8])(o);
    a.proc_add(fin, a.parent, o, 0x904E28, 0x6E, 0);
    m3(0x1F8, (ULONG_PTR)f_game_array(), (ULONG_PTR)f_count());
    if (a.flag) m2(0x200, 0x30000);
    int n = g_fo_n;
    FObj &c = g_fo[n];
    c = g_fo[tmpl];
    c.placed = c.locked = c.stored = 0;
    c.moved = 1;
    memcpy(c.pos, (char *)o + OBJ_POS, 12);
    const int *r = (const int *)((char *)o + OBJ_ROT);
    for (int k = 0; k < 3; k++) c.rot[k] = (float)r[k] / ANGLE_UNITS;
    g_fptr[n] = o;
    g_fo_n = n + 1;
    g_fmade++;
    logf("FORGE: made copy %d of object %d (%s) like %d: class %u/%u, %p", n, c.obj, f_name(n), tmpl, c0, c1, (void *)o);
    return n;
}

// read a saved map; every line must match this stage's instances (object index per row), else nothing is applied
static int f_load(int stage, bool apply) {
    wchar_t path[MAX_PATH];
    f_variant_path(path, L".fvar");
    FILE *f = NULL;
    if (cwo_wfopen(&f, path, L"r") != 0 || !f) return 0;
    static FObj want[FMAX];
    static bool have[FMAX];
    memset(have, 0, sizeof(have));
    char line[256], file[32] = {0};
    int ver = 0, st = -1, n = -1, bad = 0;
    while (fgets(line, sizeof(line), f)) {
        int i, obj, placed, locked;
        float p[3], r[3];
        if (sscanf_s(line, "CWO-FORGE-VARIANT %d", &ver) == 1) continue;
        if (sscanf_s(line, "stage %d", &st) == 1) continue;
        if (sscanf_s(line, "file %31s", file, (unsigned)sizeof(file)) == 1) continue;
        if (sscanf_s(line, "instances %d", &n) == 1) continue;
        if (sscanf_s(line, "o %d %d %d %d %f %f %f %f %f %f", &i, &obj, &placed, &locked, &p[0], &p[1], &p[2], &r[0], &r[1], &r[2]) == 10) {
            if (apply && i >= g_fbase_n && i == g_fo_n && i < FMAX) {   // a copy: make it again (same order as saved)
                int t = -1;
                for (int j = 0; j < g_fbase_n && t < 0; j++) if (g_fo[j].obj == obj && f_array()[j]) t = j;
                if (t >= 0) f_make_copy(t);
            }
            if (i < 0 || i >= g_fo_n || g_fo[i].obj != obj) { bad++; continue; }
            want[i] = g_fo[i];
            memcpy(want[i].pos, p, 12);
            memcpy(want[i].rot, r, 12);
            want[i].placed = (unsigned char)(placed != 0);
            want[i].locked = (unsigned char)(locked != 0);
            have[i] = true;
        }
    }
    fclose(f);
    if (ver != 1 || st != stage || n != g_fbase_n || strcmp(file, g_fstem) || bad) {
        logf("FORGE: %ls refused (version %d, stage %d/%d, file %s/%s, instances %d/%d, %d lines for other objects)", path,
             ver, st, stage, file, g_fstem, n, g_fbase_n, bad);
        return -1;
    }
    if (!apply) return 1;
    int moved = 0;
    for (int i = 0; i < g_fo_n; i++) {
        if (!have[i] || !f_array()[i]) continue;
        if (memcmp(want[i].pos, g_fo[i].pos, 12) || memcmp(want[i].rot, g_fo[i].rot, 12)) {
            f_set(i, want[i].pos, want[i].rot);
            moved++;
        }
        g_fo[i].placed = want[i].placed;
        g_fo[i].locked = want[i].locked;
        g_fo[i].moved = (unsigned char)(memcmp(want[i].pos, g_fo[i].orig_pos, 12) != 0);
    }
    f_designs();
    logf("FORGE: loaded %ls: %d objects moved", path, moved);
    return 1;
}

static bool f_play_flag() {
    wchar_t path[MAX_PATH];
    f_variant_path(path, L".play");
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

static void f_set_play_flag(bool on) {
    wchar_t path[MAX_PATH];
    f_variant_path(path, L".play");
    if (on) {
        FILE *f = NULL;
        if (cwo_wfopen(&f, path, L"w") == 0 && f) { fprintf(f, "play this saved map in every match on %s\n", g_fstem); fclose(f); }
    } else
        DeleteFileW(path);
}

// ---- Forge spawns in "Play this map in matches" (game thread, first match frame) ----
// Game facts (Ghidra batch_029/029b/029c, 2026-10-20_BATCH_026_029.md): after the stage loads, the start block in use
// is the entry pointer *0x1405300a8 with the entry count u8 0x14052dd62 (0x50-byte entries: +0x00 position f32[3],
// +0x0C heading radians, +0x14/+0x20/+0x2C the 3 starting capsule spots); slot s uses entry (s % 4) % count, for the
// start (0x14024e190) and every respawn (0x140251f80). The starting capsules sit in the capsule table 0x140584f80,
// player p's spots at +0xD10 + (p*3+k)*0x48 (position f32[3], +0xC state). Spawns come from the Forge SPAWN markers in
// the order they were placed (the first 4 = players 1-4), 1 m above the marker; heading toward their centre; CAPSULE
// markers go to the nearest spawn with room, the rest 3 in a row in front (3 / 6 / 9 m), as apply_spawn_markers does.
static const ULONG_PTR RVA_CP_ENTRIES = 0x5300a8, RVA_CP_COUNT = 0x52dd62, CP_ENTRY = 0x50;
static const ULONG_PTR ACTOR_SPAWN_HEADING = 0x12E58;
struct FSpawn { float pos[3], heading, caps[3][3]; };

static int f_forge_spawns(FSpawn out[4]) {
    int sp[4], n = 0;
    for (int i = 0; i < g_mk_n && n < 4; i++)
        if (!strcmp(g_mk[i].type, "SPAWN")) sp[n++] = i;
    if (n < 4) return n;                                     // fewer than 4: the stage's own spawns stay
    float cx = 0, cz = 0;
    for (int k = 0; k < 4; k++) { cx += g_mk[sp[k]].p[0] / 4; cz += g_mk[sp[k]].p[2] / 4; }
    int used[4] = {0, 0, 0, 0};
    for (int k = 0; k < 4; k++) {
        const float *m = g_mk[sp[k]].p;
        out[k].pos[0] = m[0]; out[k].pos[1] = m[1] + 10.0f; out[k].pos[2] = m[2];
        out[k].heading = atan2f(cx - m[0], cz - m[2]);       // x = sin, z = cos (the game's heading convention)
    }
    for (int i = 0; i < g_mk_n; i++) {                       // owner capsule spots: nearest spawn with room
        if (strcmp(g_mk[i].type, "CAPSULE")) continue;
        int best = -1;
        float bd = 1e30f;
        for (int k = 0; k < 4; k++) {
            float dx = g_mk[i].p[0] - out[k].pos[0], dz = g_mk[i].p[2] - out[k].pos[2], d = dx * dx + dz * dz;
            if (used[k] < 3 && d < bd) { bd = d; best = k; }
        }
        if (best < 0) continue;
        float *c = out[best].caps[used[best]++];
        c[0] = g_mk[i].p[0]; c[1] = g_mk[i].p[1] + 10.0f; c[2] = g_mk[i].p[2];
    }
    for (int k = 0; k < 4; k++)
        for (; used[k] < 3; used[k]++) {
            float d = 30.0f * (float)(used[k] + 1);
            float *c = out[k].caps[used[k]];
            c[0] = out[k].pos[0] + sinf(out[k].heading) * d; c[1] = out[k].pos[1]; c[2] = out[k].pos[2] + cosf(out[k].heading) * d;
        }
    return 4;
}

// writes the start entries, moves the players to them once and puts the capsule spots there; every write is logged
static int f_apply_spawns(char *entries, int count, char *caps, ULONG_PTR *actors) {
    FSpawn s[4];
    if (!entries || count <= 0 || f_forge_spawns(s) < 4) return 0;
    if (count > 4) count = 4;
    for (int e = 0; e < count; e++) {
        char *en = entries + CP_ENTRY * e;
        float old[3];
        memcpy(old, en, 12);
        memcpy(en, s[e].pos, 12);
        *(float *)(en + 0x0C) = s[e].heading;
        for (int k = 0; k < 3; k++) memcpy(en + 0x14 + 12 * k, s[e].caps[k], 12);
        logf("FORGE spawns: start entry %d at %p (%.1f, %.1f, %.1f) -> (%.1f, %.1f, %.1f) heading %.3f, capsules set", e,
             (void *)en, old[0], old[1], old[2], s[e].pos[0], s[e].pos[1], s[e].pos[2], s[e].heading);
    }
    for (int p = 0; p < 4; p++) {
        ULONG_PTR a = actors[p];
        if (!a || *(int *)(a + ACTOR_ACTIVE) == 0) continue;
        const FSpawn &t = s[(p % 4) % count];
        float *pos = (float *)(a + ACTOR_POS), *prev = (float *)(a + ACTOR_PREV_POS);
        float old[3];
        memcpy(old, pos, 12);
        memcpy(pos, t.pos, 12);
        memcpy(prev, t.pos, 12);
        int h = (int)(t.heading * ANGLE_UNITS);
        *(int *)(a + ACTOR_SPAWN_HEADING) = h;
        *(int *)(a + ACTOR_CAMERA) = h;
        *(int *)(a + ACTOR_FACING) = h;
        logf("FORGE spawns: player %d moved (%.1f, %.1f, %.1f) -> (%.1f, %.1f, %.1f)", p + 1, old[0], old[1], old[2], t.pos[0], t.pos[1], t.pos[2]);
        if (caps)                                            // v5.1 caps = capsule spot 0 (+0xD10): spots stay 0x48 apart
            for (int k = 0; k < 3; k++) memcpy(caps + (p * 3 + k) * 0x48, t.caps[k], 12);
    }
    return count;
}

// ---- kill zones in "Play this map in matches" (owner rule: falling in sends you to your own spawn, -3 health) ----
// Game facts: respawn 0x140251f80 takes the start entry of the actor's slot (actor +0x18) through 0x14012a5e0, the
// same entries as above; current health is actor +0x4C (int; < 1 = knocked out, e.g. 0x140251d80, 0x1402523a0),
// +0x50 the maximum (respawn halves it, then copies it to +0x4C). A KILLZONE marker is a pit 5 m around the point:
// a player within that radius and no higher than 1 m above the point is sent to their start entry and loses 3
// health, never below 1 (the loader does not knock anyone out), once per 3 s.
static const ULONG_PTR ACTOR_SLOT = 0x18, ACTOR_HP = 0x4C;
static ULONGLONG g_fkill_last[4];

static int f_kill_zones(char *entries, int count, ULONG_PTR *actors, ULONGLONG now) {
    int hits = 0;
    if (!entries || count <= 0) return 0;
    for (int p = 0; p < 4; p++) {
        ULONG_PTR a = actors[p];
        if (!a || *(int *)(a + ACTOR_ACTIVE) == 0 || now - g_fkill_last[p] < 3000) continue;
        float *pos = (float *)(a + ACTOR_POS);
        for (int i = 0; i < g_mk_n; i++) {
            if (strcmp(g_mk[i].type, "KILLZONE")) continue;
            float dx = pos[0] - g_mk[i].p[0], dz = pos[2] - g_mk[i].p[2];
            if (dx * dx + dz * dz > 50.0f * 50.0f || pos[1] > g_mk[i].p[1] + 10.0f) continue;
            int slot = *(int *)(a + ACTOR_SLOT);
            const char *en = entries + CP_ENTRY * (((slot % 4) + 4) % 4 % count);
            float old[3];
            memcpy(old, pos, 12);
            memcpy(pos, en, 12);
            memcpy((float *)(a + ACTOR_PREV_POS), en, 12);
            int h = (int)(*(const float *)(en + 0x0C) * ANGLE_UNITS);
            *(int *)(a + ACTOR_SPAWN_HEADING) = h;
            *(int *)(a + ACTOR_CAMERA) = h;
            *(int *)(a + ACTOR_FACING) = h;
            int hp = *(int *)(a + ACTOR_HP), nhp = hp - 3 < 1 ? 1 : hp - 3;
            if (hp > 0) *(int *)(a + ACTOR_HP) = nhp;
            g_fkill_last[p] = now;
            hits++;
            logf("FORGE kill zone: player %d at (%.1f, %.1f, %.1f) -> start (%.1f, %.1f, %.1f), health +0x4C %d -> %d", p + 1,
                 old[0], old[1], old[2], pos[0], pos[1], pos[2], hp, hp > 0 ? nhp : hp);
            break;
        }
    }
    return hits;
}

// ---- v2.2 helpers (game thread) ----
// The game turns an object's matrix with FUN_140290a40(m, rx, ry, rz) (Ghidra full pass): rows are mixed by Z, then
// Y, then X, so a point in object space p goes to the world as p * Rx * Ry * Rz + position (row vectors), with
// Rz = [c s 0; -s c 0; 0 0 1], Ry = [c 0 -s; 0 1 0; s 0 c], Rx = [1 0 0; 0 c s; 0 -s c]; 32768 steps per turn.
static void f_rotmat(const float r[3], float L[9]) {
    float cx = cosf(r[0]), sx = sinf(r[0]), cy = cosf(r[1]), sy = sinf(r[1]), cz = cosf(r[2]), sz = sinf(r[2]);
    float Rx[9] = {1, 0, 0, 0, cx, sx, 0, -sx, cx}, Ry[9] = {cy, 0, -sy, 0, 1, 0, sy, 0, cy}, Rz[9] = {cz, sz, 0, -sz, cz, 0, 0, 0, 1};
    float A[9];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            A[3 * i + j] = 0;
            for (int k = 0; k < 3; k++) A[3 * i + j] += Rx[3 * i + k] * Ry[3 * k + j];
        }
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            L[3 * i + j] = 0;
            for (int k = 0; k < 3; k++) L[3 * i + j] += A[3 * i + k] * Rz[3 * k + j];
        }
}

static void f_world_box(const float box[6], const float pos[3], const float rot[3], float out[6]) {
    float L[9];
    f_rotmat(rot, L);
    for (int k = 0; k < 3; k++) { out[k] = 1e30f; out[3 + k] = -1e30f; }
    for (int c = 0; c < 8; c++) {
        float p[3] = {box[(c & 1) ? 3 : 0], box[(c & 2) ? 4 : 1], box[(c & 4) ? 5 : 2]};
        for (int j = 0; j < 3; j++) {
            float w = p[0] * L[j] + p[1] * L[3 + j] + p[2] * L[6 + j] + pos[j];
            if (w < out[j]) out[j] = w;
            if (w > out[3 + j]) out[3 + j] = w;
        }
    }
}

static const float *f_box_of(int i) {
    int k = f_design_index(g_fo[i].obj);
    return k >= 0 ? g_fd[k].box : NULL;
}

// Magnets: the held object's box snaps flush (face to face, or edge to edge) to a nearby object's box, and
// to the floor, on each axis where it is within 0.5 m
static int f_magnet(int held, float pos[3], const float rot[3]) {
    const float *hb = f_box_of(held);
    if (!hb) return 0;
    float H[6];
    f_world_box(hb, pos, rot, H);
    float best[3] = {5.0f, 5.0f, 5.0f}, d[3] = {0, 0, 0};
    for (int i = 0; i < g_fo_n; i++) {
        if (i == held || !g_fo[i].placed || g_fhidden[i] || !f_array()[i]) continue;
        const float *ob = f_box_of(i);
        if (!ob) continue;
        float dx = g_fo[i].pos[0] - pos[0], dz = g_fo[i].pos[2] - pos[2];
        if (dx * dx + dz * dz > 400.0f * 400.0f) continue;
        float O[6];
        f_world_box(ob, g_fo[i].pos, g_fo[i].rot, O);
        bool close_by = true;
        for (int k = 0; k < 3; k++) if (H[k] > O[3 + k] + 10.0f || O[k] > H[3 + k] + 10.0f) close_by = false;
        if (!close_by) continue;
        for (int k = 0; k < 3; k++) {
            const float c[4] = {O[3 + k] - H[k], O[k] - H[3 + k], O[k] - H[k], O[3 + k] - H[3 + k]};
            for (int j = 0; j < 4; j++) if (fabsf(c[j]) < best[k]) { best[k] = fabsf(c[j]); d[k] = c[j]; }
        }
    }
    if (g_floor_y > -1e29f && fabsf(g_floor_y - H[1]) < best[1]) { best[1] = fabsf(g_floor_y - H[1]); d[1] = g_floor_y - H[1]; }
    int hit = 0;
    for (int k = 0; k < 3; k++) if (best[k] < 5.0f) { pos[k] += d[k]; hit |= 1 << k; }
    return hit;
}

// real capsules at the 3 capsule markers nearest to you (player 1's capsule spots, as the in-game-tested skill drop:
// a waiting spot gets the position and countdown 1, and the game spawns the capsule itself)
static void f_caps_show(ULONG_PTR actor) {
    if (!g_fcaps || g_ftesting) return;
    static ULONGLONG last;
    ULONGLONG now = GetTickCount64();
    if (now - last < 500) return;
    last = now;
    const float *pp = (const float *)(actor + ACTOR_POS);
    int pick[3] = {-1, -1, -1};
    float pd[3] = {1e30f, 1e30f, 1e30f};
    for (int i = 0; i < g_mk_n; i++) {
        if (strcmp(g_mk[i].type, "CAPSULE")) continue;
        float dx = g_mk[i].p[0] - pp[0], dz = g_mk[i].p[2] - pp[2], dd = dx * dx + dz * dz;
        for (int k = 0; k < 3; k++)
            if (dd < pd[k]) {
                for (int m = 2; m > k; m--) { pd[m] = pd[m - 1]; pick[m] = pick[m - 1]; }
                pd[k] = dd; pick[k] = i;
                break;
            }
    }
    for (int k = 0; k < 3; k++) {
        if (pick[k] < 0) continue;
        char *sp = cwo_caps(0xD10 + (ULONG_PTR)k * 0x48);
        float to[3] = {g_mk[pick[k]].p[0], g_mk[pick[k]].p[1] + 10.0f, g_mk[pick[k]].p[2]};
        float *cur = (float *)sp;
        if (fabsf(cur[0] - to[0]) < 0.5f && fabsf(cur[1] - to[1]) < 0.5f && fabsf(cur[2] - to[2]) < 0.5f) continue;
        logf("FORGE capsules: spot %d at %p (%.1f, %.1f, %.1f) -> (%.1f, %.1f, %.1f), state %d", k, (void *)sp, cur[0], cur[1], cur[2],
             to[0], to[1], to[2], *(int *)(sp + 0xC));
        memcpy(sp, to, 12);
        if (*(int *)(sp + 0xC) == 0) *(int *)(sp + 0x10) = 1;       // waiting: the game spawns it at the new spot
    }
}

// spawn points and kill zones show as a looping effect (a skill's effect, chosen in the menu), up to 16 nearest
struct FMkFx { float p[3]; int type; unsigned h; int fails, track; };
static FMkFx g_fmkfx[16];
static int g_fmkfx_n;

static void f_mkfx_stop() {
    FxGame g;
    if (fx_game(&g))
        for (int i = 0; i < g_fmkfx_n; i++)
            if (fx_alive(g, g_fmkfx[i].h)) g.stop(g_fmkfx[i].h);
    g_fmkfx_n = 0;
}

static void f_mkfx_step(ULONG_PTR actor) {
    FxGame g;
    if (!fx_game(&g) || g_sk_count <= 0) return;
    static int last_n = -1;
    static float last_sum;
    float sum = 0;
    for (int i = 0; i < g_mk_n; i++) sum += g_mk[i].p[0] + 3 * g_mk[i].p[1] + 7 * g_mk[i].p[2];
    bool want = g_fmk_fx && !g_ftesting;
    if (InterlockedExchange(&g_fmk_change, 0) || last_n != g_mk_n || sum != last_sum || (!want && g_fmkfx_n)) {
        f_mkfx_stop();
        last_n = g_mk_n;
        last_sum = sum;
        if (!want) return;
        const float *pp = (const float *)(actor + ACTOR_POS);
        bool used[256] = {false};
        for (int n = 0; n < 16; n++) {                          // the 16 nearest spawn / kill-zone markers
            int best = -1;
            float bd = 1e30f;
            for (int i = 0; i < g_mk_n; i++) {
                bool sp = !strcmp(g_mk[i].type, "SPAWN"), kz = !strcmp(g_mk[i].type, "KILLZONE");
                if (used[i] || !(sp || kz)) continue;
                float dx = g_mk[i].p[0] - pp[0], dz = g_mk[i].p[2] - pp[2], d = dx * dx + dz * dz;
                if (d < bd) { bd = d; best = i; }
            }
            if (best < 0) break;
            used[best] = true;
            FMkFx &f = g_fmkfx[g_fmkfx_n++];
            memcpy(f.p, g_mk[best].p, 12);
            f.type = !strcmp(g_mk[best].type, "SPAWN") ? 0 : 1;
            f.h = FX_NONE;
            f.fails = 0;
            f.track = 0;
        }
    }
    if (!want) return;
    for (int i = 0; i < g_fmkfx_n; i++) {
        FMkFx &f = g_fmkfx[i];
        if (fx_alive(g, f.h) || f.track > 5) continue;
        LONG id = g_fmk_skill[f.type];
        if (id < 0 || id >= 753) continue;
        short prof = *(short *)(g.skills + 0x14 + (ULONG_PTR)id * 0x90);
        int req = g.effect_of(prof, id);
        if (req <= 0) { f.track = 6; continue; }
        __declspec(align(16)) float m[16];
        memcpy(m, g.identity, sizeof(m));
        m[12] = f.p[0]; m[13] = f.p[1] + 5.0f; m[14] = f.p[2];
        unsigned h = g.start(g.mgr, *(int *)(actor + 0x14), req, f.track, m, 0);
        if (h == FX_NONE) { if (++f.fails > 120) { f.fails = 0; f.track++; } continue; }
        g.loop(h);
        f.h = h;
    }
}

// ---- v2.9 stage effects (owner): every EFFECT marker plays its skill's effect, looping, through the same effect API as the aura FX
// (FUN_14018a040 start(manager, owner, request, part, matrix), FUN_14018d410 loop, stop FUN_14018d3d0); the matrix is
// the identity scaled by the marker's size, at the marker. In the editor, while testing, and in matches that play the
// saved map. Visual only: a skill's harm comes from its attack, which is not made here.
struct FSfx { int mk; unsigned h; int fails; float t; __declspec(align(16)) float mt[16]; };   // v5.36 mt: the pinned matrix
static FSfx g_fsfx[32];
static void sfx_pin(const FxGame &g, unsigned h, float *m);     // cwo_sfx.inl (included later): slot +0x40 / +0x90 / +0xD0
static int g_fsfx_n;
static float g_fsfx_sum = -1.0f;

static void f_sfx_stop() {
    FxGame g;
    if (fx_game(&g))
        for (int i = 0; i < g_fsfx_n; i++)
            if (fx_alive(g, g_fsfx[i].h)) g.stop(g_fsfx[i].h);
    g_fsfx_n = 0;
}

static void f_sfx_step(ULONG_PTR actor, bool run) {
    FxGame g;
    if (!actor || !fx_game(&g)) return;
    float sum = 0;
    int n = 0;
    for (int i = 0; i < g_mk_n; i++)
        if (!strcmp(g_mk[i].type, "EFFECT")) { n++; sum += g_mk[i].p[0] + 3 * g_mk[i].p[1] + 7 * g_mk[i].p[2] + 11 * g_mk[i].skill + 13 * g_mk[i].size + 17 * g_mk[i].track + 19 * g_mk[i].speed + 23 * g_mk[i].hold; }
    sum += 1000.0f * n;
    if (!run || sum != g_fsfx_sum) {
        f_sfx_stop();
        g_fsfx_sum = run ? sum : -1.0f;
        if (!run) return;
        for (int i = 0; i < g_mk_n && g_fsfx_n < 32; i++)
            if (!strcmp(g_mk[i].type, "EFFECT")) { g_fsfx[g_fsfx_n].mk = i; g_fsfx[g_fsfx_n].h = FX_NONE; g_fsfx[g_fsfx_n].fails = 0; g_fsfx[g_fsfx_n].t = 0; g_fsfx_n++; }
    }
    for (int k = 0; k < g_fsfx_n; k++) {
        FSfx &f = g_fsfx[k];
        if (fx_alive(g, f.h)) {
            // v3.0 animation speed: an instance's clock is slot +0x0C, which the effect update FUN_1401899b0 moves on by
            // the frame step each frame (slot = mgr + 0x60 + 0xF0 * (handle & 0xFFF)); the frame's step is scaled here
            float sp = g_mk[f.mk].speed, hold = g_mk[f.mk].hold;
            if (hold >= 0.0f && !g_fx_test) {                        // v5.36 held on one step (owner)
                float *clock = (float *)(g.mgr + 0x60 + 0xF0 * (ULONG_PTR)(f.h & 0xFFF) + 0x0C);
                *clock = hold;
                f.t = hold;
            } else if (sp != 1.0f && !g_fx_test) {
                float *clock = (float *)(g.mgr + 0x60 + 0xF0 * (ULONG_PTR)(f.h & 0xFFF) + 0x0C);
                float now = *clock;
                if (now >= f.t) *clock = f.t + (now - f.t) * sp;   // (a loop back to 0 is left alone)
                f.t = *clock;
            }
            // v5.36 (owner): a track-0
            // instance is drawn from *(slot + 0x40) at +0xD0 (FUN_14018a8b0), not from the start matrix, so the size never
            // reached a wall. The slot is pinned to the marker's scaled matrix every frame, as the school effect is (sfx_pin)
            if (!g_fx_test) sfx_pin(g, f.h, f.mt);
            continue;
        }
        if (f.fails > 600) continue;
        const FMark &m = g_mk[f.mk];
        if (m.skill <= 0 || m.skill >= 753) continue;
        short prof = *(short *)(g.skills + 0x14 + (ULONG_PTR)m.skill * 0x90);
        int req = g.effect_of(prof, m.skill);
        if (req <= 0) { f.fails = 601; continue; }
        float *mt = f.mt;
        memcpy(mt, g.identity, 16 * sizeof(float));
        float sz = m.size > 0.05f ? m.size : 1.0f;
        for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) mt[4 * r + c] *= sz;
        mt[12] = m.p[0]; mt[13] = m.p[1] + 5.0f; mt[14] = m.p[2];
        unsigned h = g.start(g.mgr, *(int *)(actor + 0x14), req, m.track, mt, 0);
        if (h == FX_NONE) { f.fails++; continue; }                  // not loaded yet: the game queues it; try next frame
        g.loop(h);
        f.h = h;
        f.t = 0;
        if (!g_fx_test) sfx_pin(g, h, f.mt);                        // v5.36 the size on walls too (see above)
    }
}

// ---- editing (game thread) ----
static void f_post(int op, int a = 0) {
    LONG h = g_fq_head;
    if (((h + 1) & 31) == g_fq_tail) return;              // full: drop (32 pending presses)
    g_fq[h].op = op;
    g_fq[h].a = a;
    InterlockedExchange(&g_fq_head, (h + 1) & 31);
}

static int f_design_index(int obj) {
    for (int k = 0; k < g_fd_n; k++)
        if (g_fd[k].obj == obj) return k;
    return -1;
}

static int f_free_copy(int obj) {
    int any = -1;
    for (int i = 0; i < g_fo_n; i++) {
        if (g_fo[i].obj != obj || !f_array()[i]) continue;
        if (!g_fo[i].placed) return i;
        if (any < 0 && i < g_fbase_n) any = i;
    }
    return g_fcopies && any >= 0 ? f_make_copy(any) : -1;     // v2.7: all in use - make one more
}

static void f_store(int i) {                                // back to storage (canvas copies) or 300 m below
    FObj &o = g_fo[i];
    float p[3] = {o.orig_pos[0], o.orig_pos[1], o.orig_pos[2]};
    if (!o.stored) p[1] -= F_HIDE_DROP;
    f_set(i, p, o.orig_rot);
    o.placed = 0;
    o.locked = 0;
    o.moved = 1;
}

static void f_begin_hold(int i, ULONG_PTR actor) {
    f_col_sync(i, -F_HIDE_DROP);                     // nothing to bump into while you carry it
    if (!g_fly_landed && !g_actor_test) {             // v3.3 (owner): snap to the piece - you come to it, it stays put
        int k = f_design_index(g_fo[i].obj);
        float r = 30.0f;
        if (k >= 0 && g_fd[k].box) {
            const float *b = g_fd[k].box;
            float sx = b[3] - b[0], sz = b[5] - b[2];
            r = 0.5f * sqrtf(sx * sx + sz * sz);
        }
        float d = r * 1.3f + 15.0f;
        d = d < F_DIST_MIN ? F_DIST_MIN : d > F_DIST_MAX ? F_DIST_MAX : d;
        float h = f_heading(actor), to[3] = {g_fo[i].pos[0] - sinf(h) * d, g_fo[i].pos[1] + 5.0f, g_fo[i].pos[2] - cosf(h) * d};
        memcpy(g_ghost, to, 12);
        memcpy((float *)(actor + ACTOR_POS), to, 12);
        memcpy((float *)(actor + ACTOR_PREV_POS), to, 12);
    }
    const float *pp = (const float *)(actor + ACTOR_POS);
    float dx = g_fo[i].pos[0] - pp[0], dz = g_fo[i].pos[2] - pp[2];
    float d = sqrtf(dx * dx + dz * dz);
    g_hold_dist = d < F_DIST_MIN ? F_DIST_MIN : d > F_DIST_MAX ? F_DIST_MAX : d;
    g_hold_hoff = g_fo[i].pos[1] - pp[1];
    memcpy(g_hold_rot, g_fo[i].rot, 12);
    g_hold = i;
}

static void f_aim(ULONG_PTR actor, float out[3]) {
    const float *pp = (const float *)(actor + ACTOR_POS);
    float h = f_heading(actor);
    out[0] = pp[0] + sinf(h) * g_hold_dist;
    out[1] = pp[1] + g_hold_hoff;
    out[2] = pp[2] + cosf(h) * g_hold_dist;
}

static void f_status_obj(const char *what, int i) {
    char msg[96];
    sprintf_s(msg, "%s %.40s", what, f_name(i));
    set_status(msg);
}

// raw moves for the view only (highlight, show only): the object's real place stays in g_fo
static void f_rawpos(int n, const float pos[3]) {
    char *o = (char *)f_array()[n];
    if (!o) return;
    PFN_SETPOS sp = *(PFN_SETPOS *)(*(char **)o + VT_SET_POSITION);
    sp(o, pos);
}

static void f_bob_off() {
    if (g_fbob_inst >= 0 && g_fbob_inst < g_fo_n && !g_fhidden[g_fbob_inst] && g_fbob_inst != g_hold) f_rawpos(g_fbob_inst, g_fo[g_fbob_inst].pos);
    g_fbob_inst = -1;
}

static void f_iso_apply() {
    for (int i = 0; i < g_fo_n; i++) {
        bool hide = g_fo[i].placed && i != g_hold && ((g_fiso_mode == 1 && g_fo[i].obj != g_fiso_obj) || (g_fiso_mode == 2 && g_fo[i].obj == g_fiso_obj));
        if (hide && !g_fhidden[i]) {
            float p[3] = {g_fo[i].pos[0], g_fo[i].pos[1] - F_HIDE_DROP, g_fo[i].pos[2]};
            f_rawpos(i, p);
            g_fhidden[i] = true;
        } else if (!hide && g_fhidden[i]) {
            f_rawpos(i, g_fo[i].pos);
            g_fhidden[i] = false;
        }
    }
}

static void f_unview() {                                     // everything back where it really is
    f_bob_off();
    int keep = g_fiso_mode;
    g_fiso_mode = 0;
    f_iso_apply();
    g_fiso_mode = keep;
}

static void f_snap_take(FSnap &h) {
    for (int i = 0; i < g_fo_n; i++) {
        h.placed[i] = g_fo[i].placed;
        h.locked[i] = g_fo[i].locked;
        memcpy(h.pos[i], g_fo[i].pos, 12);
        memcpy(h.rot[i], g_fo[i].rot, 12);
    }
    h.mk_n = g_mk_n;
    memcpy(h.mk, g_mk, sizeof(FMark) * g_mk_n);
    h.fo_n = g_fo_n;
}

static void f_hist_reset() { g_hn = 1; g_hcur = 0; f_snap_take(g_hist[0]); }

static void f_hist_push() {                                  // after every change: the new state, redo history dropped
    if (g_hcur + 1 >= 24) { memmove(&g_hist[0], &g_hist[1], sizeof(FSnap) * 23); g_hcur--; }
    g_hcur++;
    g_hn = g_hcur + 1;
    f_snap_take(g_hist[g_hcur]);
}

static void f_store(int i);
static void f_snap_apply(const FSnap &h, int stage) {
    f_unview();
    for (int i = 0; i < g_fo_n; i++) {
        if (!f_array()[i]) continue;
        if (i >= h.fo_n) {                                   // a copy made after that state: back below the map
            if (g_fo[i].placed) f_store(i);
            continue;
        }
        if (memcmp(h.pos[i], g_fo[i].pos, 12) || memcmp(h.rot[i], g_fo[i].rot, 12)) f_set(i, h.pos[i], h.rot[i]);
        g_fo[i].placed = h.placed[i];
        g_fo[i].locked = h.locked[i];
    }
    bool marks = h.mk_n != g_mk_n || memcmp(h.mk, g_mk, sizeof(FMark) * g_mk_n);
    if (marks) {
        g_mk_n = h.mk_n;
        memcpy(g_mk, h.mk, sizeof(FMark) * g_mk_n);
        f_save_markers(stage);
    }
    f_designs();
    f_iso_apply();
}

static void f_do(const FReq &q, int stage, ULONG_PTR actor) {
    LONG t = g_ftarget, mk = g_fmark_near;
    char msg[96];
    f_bob_off();
    switch (q.op) {
    case F_GRAB:
        if (t < 0) { set_status("Nothing to grab here (D-pad Up: object menu)"); return; }
        if (g_fo[t].locked) { f_status_obj("Locked (B to unlock):", t); return; }
        f_begin_hold(t, actor);
        f_status_obj("Holding", t);
        return;
    case F_PLACE:
    case F_PLACE_LOCK:
        if (g_hold < 0) return;
        f_col_sync(g_hold, 0.0f);                    // solid where it was put down
        g_fo[g_hold].placed = 1;
        g_fo[g_hold].moved = 1;
        g_fo[g_hold].locked = (unsigned char)(q.op == F_PLACE_LOCK);
        f_status_obj(q.op == F_PLACE_LOCK ? "Placed and locked" : "Placed", g_hold);
        logf("FORGE: placed %d (%s) at (%.1f, %.1f, %.1f) rot (%.3f, %.3f, %.3f)%s", g_hold, f_name(g_hold), g_fo[g_hold].pos[0],
             g_fo[g_hold].pos[1], g_fo[g_hold].pos[2], g_fo[g_hold].rot[0], g_fo[g_hold].rot[1], g_fo[g_hold].rot[2],
             q.op == F_PLACE_LOCK ? " locked" : "");
        g_hold = -1;
        g_hold_fixed = 0;
        f_designs();
        f_hist_push();
        return;
    case F_DELETE: {
        int i = g_hold >= 0 ? g_hold : t;
        if (i < 0) {
            if (mk >= 0 && mk < g_mk_n) { FReq m = {F_MARK_DELETE, mk}; f_do(m, stage, actor); return; }
            set_status("Nothing to delete here");
            return;
        }
        if (g_fo[i].locked && g_hold < 0) { f_status_obj("Locked (B to unlock):", i); return; }
        g_hold = -1;
        f_store(i);
        f_designs();
        f_hist_push();
        f_status_obj("Deleted (LB: undo)", i);
        logf("FORGE: deleted %d (%s)%s", i, f_name(i), g_fo[i].stored ? " -> storage" : " -> hidden below the map");
        return;
    }
    case F_COPY: {
        int src = g_hold >= 0 ? g_hold : t;
        if (src < 0) { set_status("Look at an object to copy it"); return; }
        int c = f_free_copy(g_fo[src].obj);
        if (c < 0) {
            int k = f_design_index(g_fo[src].obj);
            if (g_fmade_nomem || g_fo_n >= FMAX) sprintf_s(msg, "The stage's data is full: %d pieces on it", g_fo_n);
            else sprintf_s(msg, "Limit reached: all %d copies of %.28s are placed", k >= 0 ? g_fd[k].total : 0, f_name(src));
            set_status(msg);
            return;
        }
        if (g_hold >= 0) { f_col_sync(g_hold, 0.0f); g_fo[g_hold].placed = 1; g_fo[g_hold].moved = 1; f_hist_push(); }   // drop the original where it is
        float rot[3];
        memcpy(rot, g_fo[src].rot, 12);
        f_set(c, g_fo[src].pos, rot);
        g_fo[c].placed = 1;
        g_fo[c].moved = 1;
        f_begin_hold(c, actor);
        g_hold_dist = g_hold_dist + 30.0f > F_DIST_MAX ? F_DIST_MAX : g_hold_dist + 30.0f;   // a little in front
        f_designs();
        f_status_obj("Copy of", src);
        logf("FORGE: copied %d -> %d (%s)", src, c, f_name(c));
        return;
    }
    case F_LOCK:
        if (t < 0) { set_status("Look at an object to lock it"); return; }
        g_fo[t].locked = (unsigned char)!g_fo[t].locked;
        f_hist_push();
        f_status_obj(g_fo[t].locked ? "Locked" : "Unlocked", t);
        return;
    case F_SPAWN: {
        int c = f_free_copy(q.a);
        int k = f_design_index(q.a);
        if (c < 0) {
            if (g_fmade_nomem || g_fo_n >= FMAX) sprintf_s(msg, "The stage's data is full: %d pieces on it", g_fo_n);
            else sprintf_s(msg, "Limit reached: all %d copies are placed", k >= 0 ? g_fd[k].total : 0);
            set_status(msg);
            return;
        }
        g_hold_dist = F_DIST_DEF;
        g_hold_hoff = 0.0f;
        float zero[3] = {0, 0, 0}, p[3];
        f_aim(actor, p);
        g_hold = c;                                  // carried from the start: no effects until it is put down
        f_set(c, p, zero);
        g_hold = -1;
        g_fo[c].placed = 1;
        g_fo[c].moved = 1;
        f_begin_hold(c, actor);
        f_designs();
        f_status_obj("New", c);
        logf("FORGE: spawned %d (%s) from storage", c, f_name(c));
        return;
    }
    case F_MARK: {
        if (q.a < 0 || q.a > 3 || g_mk_n >= 256) return;
        float p[3];
        f_aim(actor, p);
        if (q.a == 3) {                                      // v2.9 stage effect: the menu's skill / size / part
            FMark &m = g_mk[g_mk_n++];
            memset(&m, 0, sizeof(m));
            strcpy_s(m.type, "EFFECT");
            memcpy(m.p, p, 12);
            m.skill = g_fsfx_skill; m.size = F_SFX_SIZES[g_fsfx_size]; m.track = g_fsfx_part; m.speed = F_SFX_SPEEDS[g_fsfx_speed];
            m.hold = g_fsfx_hold >= 0 ? (float)g_fsfx_hold : -1.0f;
            f_save_markers(stage);
        } else add_marker(F_MARK_TYPES[q.a], stage, p);
        f_load_markers(stage);
        recount(stage);
        f_hist_push();
        int n = 0;
        for (int i = 0; i < g_mk_n; i++) n += !strcmp(g_mk[i].type, F_MARK_TYPES[q.a]);
        sprintf_s(msg, "%s %d placed (%.1f, %.1f, %.1f) - drops to the floor below", F_MARK_NAMES[q.a], n, p[0] / 10, p[1] / 10, p[2] / 10);
        set_status(msg);
        return;
    }
    case F_MARK_DELETE:
        if (q.a < 0 || q.a >= g_mk_n) return;
        sprintf_s(msg, "Removed a %.10s marker (LB: undo)", g_mk[q.a].type);
        logf("FORGE: deleted marker %s (%.1f, %.1f, %.1f)", g_mk[q.a].type, g_mk[q.a].p[0], g_mk[q.a].p[1], g_mk[q.a].p[2]);
        memmove(&g_mk[q.a], &g_mk[q.a + 1], sizeof(FMark) * (g_mk_n - q.a - 1));
        g_mk_n--;
        f_save_markers(stage);
        f_hist_push();
        set_status(msg);
        return;
    case F_MARK_CLEAR: {
        if (q.a < 0 || q.a > 2) return;
        int k = 0, n = 0;
        for (int i = 0; i < g_mk_n; i++) {
            if (!strcmp(g_mk[i].type, F_MARK_TYPES[q.a])) { n++; continue; }
            g_mk[k++] = g_mk[i];
        }
        g_mk_n = k;
        f_save_markers(stage);
        f_hist_push();
        sprintf_s(msg, "Removed all %d %ss (LB: undo)", n, F_MARK_NAMES[q.a]);
        logf("FORGE: removed all %d %s markers", n, F_MARK_TYPES[q.a]);
        set_status(msg);
        return;
    }
    case F_GOTO: {                                           // fly to a marker: it ends up 4 m ahead of you
        if (q.a < 0 || q.a >= g_mk_n) return;
        float h = f_heading(actor);
        float to[3] = {g_mk[q.a].p[0] - sinf(h) * 40.0f, g_mk[q.a].p[1] + 20.0f, g_mk[q.a].p[2] - cosf(h) * 40.0f};
        memcpy(g_ghost, to, 12);
        memcpy((float *)(actor + ACTOR_POS), to, 12);
        memcpy((float *)(actor + ACTOR_PREV_POS), to, 12);
        sprintf_s(msg, "At the %.10s marker (it is 4 m ahead)", g_mk[q.a].type);
        set_status(msg);
        return;
    }
    case F_GOTO_SP: {                                        // v5.36 fly to the stage's own start point q.a (4 m ahead)
        float sp[8][3];
        int n = f_stage_spawns(sp, 8);
        if (q.a < 0 || q.a >= n) return;
        float h = f_heading(actor);
        float to[3] = {sp[q.a][0] - sinf(h) * 40.0f, sp[q.a][1] + 20.0f, sp[q.a][2] - cosf(h) * 40.0f};
        memcpy(g_ghost, to, 12);
        memcpy((float *)(actor + ACTOR_POS), to, 12);
        memcpy((float *)(actor + ACTOR_PREV_POS), to, 12);
        sprintf_s(msg, "At the stage's own start point %d (it is 4 m ahead)", q.a + 1);
        set_status(msg);
        return;
    }
    case F_CLEAR_DESIGN: {
        int n = 0;
        for (int i = 0; i < g_fo_n; i++)
            if (g_fo[i].obj == q.a && g_fo[i].placed && !g_fo[i].locked && i != g_hold && f_array()[i]) { f_store(i); n++; }
        f_designs();
        if (n) f_hist_push();
        sprintf_s(msg, n ? "Removed %d (locked ones stay; LB: undo)" : "None of these are placed", n);
        set_status(msg);
        logf("FORGE: removed all %d placed copies of object %d", n, q.a);
        return;
    }
    case F_CLEAR_ALL: {                                      // every placed, unlocked object back to storage
        int n = 0;
        if (g_hold >= 0) { int was = g_hold; g_hold = -1; f_store(was); n++; }
        for (int i = 0; i < g_fo_n; i++)
            if (g_fo[i].placed && !g_fo[i].locked && f_array()[i]) { f_store(i); n++; }
        f_designs();
        if (n) f_hist_push();
        sprintf_s(msg, n ? "Removed all %d placed objects (locked ones stay; LB: undo)" : "Nothing placed to remove", n);
        set_status(msg);
        logf("FORGE: removed all %d placed objects", n);
        return;
    }
    case F_GOTO_OBJ: {                                       // fly to the next / previous placed copy of a kind
        static int last_obj = -1, last_i = -1;
        int obj = q.a >> 1, dir = (q.a & 1) ? -1 : 1, n = g_fo_n, start = last_obj == obj ? last_i : -1, found = -1;
        for (int step = 1; step <= n && found < 0; step++) {
            int i = ((start + dir * step) % n + n) % n;
            if (g_fo[i].obj == obj && g_fo[i].placed && f_array()[i]) found = i;
        }
        if (found < 0) { set_status("None of these are placed yet (A takes one)"); return; }
        last_obj = obj; last_i = found;
        float h = f_heading(actor);
        float to[3] = {g_fo[found].pos[0] - sinf(h) * 60.0f, g_fo[found].pos[1] + 25.0f, g_fo[found].pos[2] - cosf(h) * 60.0f};
        memcpy(g_ghost, to, 12);
        memcpy((float *)(actor + ACTOR_POS), to, 12);
        memcpy((float *)(actor + ACTOR_PREV_POS), to, 12);
        f_status_obj("At", found);
        return;
    }
    case F_SKY: {                                              // v2.9: the chosen sky at the centre, the others far below
        int cat = -1;
        for (int c = 0; c < g_fcat_n; c++) if (!strcmp(g_fcat[c], "Skies")) cat = c;
        if (cat < 0) { set_status("This stage has no Skies category"); return; }
        int shown = -1;
        for (int i = 0; i < g_fo_n; i++) {
            int k = f_design_index(g_fo[i].obj);
            if (k < 0 || g_fd[k].cat != cat || !f_array()[i]) continue;
            float zero[3] = {0, 0, 0}, away[3] = {0, -200000.0f, 0};
            bool on = g_fo[i].obj == q.a && shown < 0;
            f_set(i, on ? zero : away, zero);
            g_fo[i].placed = (unsigned char)on;
            g_fo[i].moved = 1;
            if (on) shown = i;
        }
        f_designs();
        f_hist_push();
        if (shown >= 0) f_status_obj("Sky:", shown);
        return;
    }
    case F_ISO:
        if (g_fiso_mode == 0 || g_fiso_obj != q.a) { g_fiso_mode = 1; g_fiso_obj = q.a; set_status("Showing only this kind (Y again: hide it)"); }
        else if (g_fiso_mode == 1) { g_fiso_mode = 2; set_status("Hiding this kind (Y again: show everything)"); }
        else { g_fiso_mode = 0; set_status("Showing everything"); }
        f_iso_apply();
        return;
    case F_UNDO:
    case F_REDO: {
        if (g_hold >= 0) {                                   // undo while holding: put it back where you grabbed it
            int was = g_hold;
            g_hold = -1;
            f_col_sync(was, 0.0f);
            f_snap_apply(g_hist[g_hcur], stage);
            set_status("Put back");
            return;
        }
        int to = q.op == F_UNDO ? g_hcur - 1 : g_hcur + 1;
        if (to < 0 || to >= g_hn) { set_status(q.op == F_UNDO ? "Nothing to undo" : "Nothing to redo"); return; }
        g_hcur = to;
        f_snap_apply(g_hist[g_hcur], stage);
        sprintf_s(msg, "%s (%d step%s left)", q.op == F_UNDO ? "Undone" : "Redone", q.op == F_UNDO ? g_hcur : g_hn - 1 - g_hcur,
                  (q.op == F_UNDO ? g_hcur : g_hn - 1 - g_hcur) == 1 ? "" : "s");
        set_status(msg);
        return;
    }
    case F_SAVE:
        set_status(f_save(stage) ? "Map saved (Forge menu: Play in matches to use it)" : "Could not save the map");
        return;
    case F_LOAD: {
        g_hold = -1;
        f_unview();
        int r = f_load(stage, true);
        if (r > 0) f_hist_push();
        f_iso_apply();
        set_status(r > 0 ? "Saved map loaded" : r == 0 ? "No saved map for this stage yet" : "Saved map does not match this stage (not loaded)");
        return;
    }
    case F_RESET:
        g_hold = -1;
        f_unview();
        for (int i = 0; i < g_fo_n; i++)
            if (f_array()[i] && (memcmp(g_fo[i].pos, g_fo[i].orig_pos, 12) || memcmp(g_fo[i].rot, g_fo[i].orig_rot, 12))) f_set(i, g_fo[i].orig_pos, g_fo[i].orig_rot);
        for (int i = 0; i < g_fo_n; i++) { g_fo[i].placed = !g_fo[i].stored && f_array()[i]; g_fo[i].locked = 0; g_fo[i].moved = 0; }
        f_designs();
        f_hist_push();
        f_iso_apply();
        set_status("Map reset to the stage file (LB: undo)");
        logf("FORGE: reset %s", g_fstem);
        return;
    case F_TURN: {
        if (g_hold < 0) return;
        int snap = F_SNAPS[g_fsnap];
        if (!snap) return;
        float step = (float)snap * 3.14159265f / 180.0f * (float)q.a;
        float &r = g_hold_rot[g_faxis == 0 ? 1 : g_faxis == 1 ? 0 : 2];
        r += step;
        float sn = (float)snap * 3.14159265f / 180.0f;
        r = floorf(r / sn + 0.5f) * sn;                       // stay on the snap grid
        if (r > 3.14159265f) r -= 6.2831853f;
        if (r < -3.14159265f) r += 6.2831853f;
        return;
    }
    case F_ROT_RESET:
        if (g_hold >= 0) { g_hold_rot[0] = g_hold_rot[1] = g_hold_rot[2] = 0.0f; set_status("Rotation reset"); }
        return;
    }
}

// target = the placed, unlocked-or-locked object nearest the aim point (6 m); markers within 3 m
static void f_find_target(ULONG_PTR actor) {
    float a[3];
    f_aim(actor, a);
    memcpy(g_faim, a, 12);
    int best = -1;
    float bd = 60.0f * 60.0f;
    for (int i = 0; i < g_fo_n; i++) {
        if (!g_fo[i].placed || i == g_hold || !f_array()[i] || g_fhidden[i]) continue;
        float dx = g_fo[i].pos[0] - a[0], dy = g_fo[i].pos[1] - a[1], dz = g_fo[i].pos[2] - a[2];
        float d = dx * dx + dy * dy * 0.25f + dz * dz;
        if (d < bd) { bd = d; best = i; }
    }
    InterlockedExchange(&g_ftarget, best);
    int mk = -1;
    float md = 30.0f * 30.0f;
    for (int i = 0; i < g_mk_n; i++) {
        float dx = g_mk[i].p[0] - a[0], dz = g_mk[i].p[2] - a[2];
        float d = dx * dx + dz * dz;
        if (d < md) { md = d; mk = i; }
    }
    InterlockedExchange(&g_fmark_near, mk);
    const float *pp = (const float *)(actor + ACTOR_POS);
    int any = -1;
    float ad = 1e30f;
    for (int i = 0; i < g_mk_n; i++) {
        float dx = g_mk[i].p[0] - pp[0], dz = g_mk[i].p[2] - pp[2], d = dx * dx + dz * dz;
        if (d < ad) { ad = d; any = i; }
    }
    if (any >= 0) {
        g_fmark_dist = sqrtf(ad);
        float rel = atan2f(g_mk[any].p[0] - pp[0], g_mk[any].p[2] - pp[2]) - f_heading(actor);
        while (rel > 3.14159265f) rel -= 6.2831853f;
        while (rel < -3.14159265f) rel += 6.2831853f;
        g_fmark_rel = rel;
    }
    InterlockedExchange(&g_fmark_any, any);
}

// v5.188.31 (hourly Create a Mode pass, owner): leaving the loader's Single Play pages (SINGLE PLAY lobby, CREATE / Edgar's Dream pages) ends the game's Single
// Play session, and the game's own exit FUN_140188c60 opens menu scene 7 on the TITLE (Ghidra, hour 3) - the player had to
// press Enter and pick the profile again. For 20 s after such an exit the loader confirms the title and the profile row the
// cursor is on (the player's own), the way the player would, and stops as soon as the menus show anything else.
static ULONGLONG g_b2m_t, g_b2m_last;
static volatile LONG g_sp_leave;                                 // v5.188.31 B on SINGLE PLAY: leave the game's Single Play list too
static int g_b2m_step;
static void f_press(LONG bits, ULONGLONG ms);
static void f_inject_keys_step();                               // v5.188.38
// v5.188.36 (in game 08:18, no controller: the title presses went nowhere - f_press goes through the pad's reading): with no
// pad the same press goes the game's own key way (Enter = A / Start, the B key = B)
static void b2m_press(LONG bits) {
    if (g_pad0) { f_press(bits, 150); return; }
    // no pad: the game's own key route (Ghidra, recorded in cwo_dreamprobe.inl: KeyDown FUN_140008490 sets bit n of
    // *0x14054ca48 and the held state 0x1405ae178[n] for the binding's button n). Read live 2026-11-17: Enter = button 12,
    // the B key = button 5 (key tables 0x1403dc890 / 0x1403dcd70). (keybd_event from inside the app did not reach it.)
    int btn = (bits & 0x8) ? 5 : 12;
    __try {
        volatile DWORD *keys = (volatile DWORD *)(g_base + 0x54ca48);
        volatile unsigned char *held = (volatile unsigned char *)(g_base + 0x5ae178);
        *keys |= 1u << btn; held[btn] = 1;
        Sleep(120);
        *keys &= ~(1u << btn); held[btn] = 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
static void back_to_main_step() {
    if (g_sp_leave) {                                             // B on the game's Single Play list (state 0), as the player would
        static ULONGLONG lastb, t0;
        ULONGLONG t = GetTickCount64();
        if (!t0) t0 = t;
        bool spm = false;
        __try {
            ULONG_PTR m = f_mode_obj();
            spm = m && *(int *)(m + 0x7C) == MODE_SINGLE_PLAY && !(*(unsigned *)(m + 0x08) & 0x40000);
            ULONG_PTR so = spm ? *(ULONG_PTR *)(m + 0x60) : 0;
            if (spm && so && *(int *)so == 0 && t - lastb > 1500) { b2m_press(0x8); lastb = t; logf("back to the main menu: B on the Single Play list"); }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        if (!spm || t - t0 > 15000) { InterlockedExchange(&g_sp_leave, 0); t0 = 0; }
    }
    if (!g_b2m_t) return;
    ULONGLONG now = GetTickCount64();
    if (now - g_b2m_t > 20000 || g_in_match || ed_online()) { if (g_b2m_step < 3) logf("back to the main menu: stopped (step %d)", g_b2m_step); g_b2m_t = 0; return; }
    if (now - g_b2m_last < 1200) return;
    int mid = -1, rows = 0;
    __try {
        ULONG_PTR m = *(ULONG_PTR *)(g_base + 0x533c58);
        mid = m ? *(short *)(m + 0x105f4) : -1;
        for (int k = 0; m && k < 16; k++) { short r = *(short *)(m + 0x40 + (ULONG_PTR)k * 0x103c + 0x16); if (r > 0 && r < 64) rows = r; }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return; }
    if (mid != 7) { if (g_b2m_step >= 2) { logf("back to the main menu: menu %d", mid); g_b2m_step = 3; g_b2m_t = 0; } return; }
    g_b2m_last = now;
    if (!rows) {                                                  // the title: A, then Start if it is still there
        static int s_tp;
        bool start = (s_tp++ & 1) != 0;
        b2m_press(start ? 0x1 : 0x4);
        if (g_b2m_step < 1) g_b2m_step = 1;
        logf("back to the main menu: title - %s", start ? "Start" : "A");
        return;
    }
    b2m_press(0x4);                                               // the profile list: A on the row the cursor is on
    g_b2m_step = 2;
    logf("back to the main menu: profile list - A");
}
// session / match tracking (worker thread)
static void forge_poll(bool in_match) {
    f_inject_keys_step();                                         // v5.188.38 no pad: presses by key
    back_to_main_step();
    if (!g_forge_session) { InterlockedExchange(&g_forge_active, 0); InterlockedExchange(&g_forge_sp_state, -1); return; }
    bool sp = false;
    int state = -1;
    __try {
        ULONG_PTR m = f_mode_obj();
        if (m) {
            unsigned flags = *(unsigned *)(m + 0x08);
            int mode = *(int *)(m + 0x7C);
            sp = mode == MODE_SINGLE_PLAY && !(flags & 0x40000);
            ULONG_PTR s = *(ULONG_PTR *)(m + 0x60);
            if (sp && s) state = *(int *)s;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { sp = false; }
    if (sp) InterlockedExchange(&g_forge_seen_sp, 1);
    if (g_forge_seen_sp && !sp) {
        InterlockedExchange(&g_forge_session, 0);
        InterlockedExchange(&g_forge_active, 0);
        InterlockedExchange(&g_forge_sp_state, -1);
        logf("FORGE: session ended (left Single Play)");
        if (!ed_online()) { g_b2m_t = GetTickCount64(); g_b2m_last = g_b2m_t; g_b2m_step = 0; }   // v5.188.31 on to the main menu
        InterlockedExchange(&g_cs_hold_cover, 0);                 // v5.33 nothing of CREATE A STAGE outlives the session
        if (g_ed_auto >= 31 && g_ed_auto <= 35) InterlockedExchange(&g_ed_auto, 0);
        g_cs_go = -1;
        return;
    }
    InterlockedExchange(&g_forge_sp_state, state);
    LONG gm = 0;
    // v2.3 (owner: the Give Up dialog could not be driven): the battle's menu machine *0x1404c0130 (FUN_140167580) opens
    // the pause scene (0x31) in its modes 9..14 (+0x50; mode table 0x1403e7b70 flag 2); either signal = a game menu
    __try {
        gm = *(int *)(g_base + 0x533744) != 3;
        ULONG_PTR bm = *(ULONG_PTR *)(g_base + 0x4c0130);
        if (bm && *(int *)(bm + 0x50) >= 9) gm = 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) { gm = 0; }
    if (g_ft_mode) gm = 0;                                   // self-test stand-in game
    if (gm != g_game_menu) InterlockedExchange(&g_game_menu, gm);
    LONG act = (sp && state == SP_MATCH && in_match) ? 1 : 0;
    if (act != g_forge_active) {
        InterlockedExchange(&g_forge_active, act);
        if (act) { g_fly_landed = false; InterlockedExchange(&g_fmenu, 0); InterlockedExchange(&g_ftesting, 0); }
        if (act && g_ed_mode == 1) { sc_open(); sc_snapshot(); g_scp_user_off = 0; InterlockedExchange(&g_fmenu_sel, 0); InterlockedExchange(&g_fmenu, 5); }   // Create a Skill
        if (act && g_ed_mode >= 1) {                         // v3.9 a school / character test (v4.2: and a skill): no editor, the game plays
            g_ftest_keep_fly = g_fly_landed; g_ftest_keep_hud = g_hide_hud;
            // v4.4 (owner): a skill test started with the game HUD hidden; v5.36 (owner): the HUD stays,
            // and steps aside by itself while the Skill Creator's rows are open (in_match_menu_open); L3 still toggles it
            g_fly_landed = true; g_hide_hud = false;
            InterlockedExchange(&g_ftesting, 1);
        }
        if (!act) InterlockedExchange(&g_ed_live, 0);
        if (!act && g_ftesting) { InterlockedExchange(&g_ftesting, 0); g_fly_landed = g_ftest_keep_fly; g_hide_hud = g_ftest_keep_hud; }
        logf("FORGE: editor %s", act ? "ON (Forge match)" : "off");
    }
}

static bool ed_online();                                        // cwo_edgar.inl
// v5.186.0 (owner 2026-11-14 "go ahead and work on A"): a saved Create a Stage map is for story, split screen and Single Play;
// in an Xbox Live / system link session or Download Battle (origin 2 / 3 / 5, battle type 4 / 5) it is not applied - the other
// players have the stock stage
static bool f_online_match() {
    __try { int o = *(int *)(g_base + 0x4bca04); return ed_online() || o == 2 || o == 3 || o == 5; } __except (EXCEPTION_EXECUTE_HANDLER) { return true; }
}

// game thread, every flush
static void forge_frame() {
    float pp[3];
    int stage = -1;
    bool in_match = g_actor_test ? true : read_player(0, pp, &stage);
    if (g_actor_test) stage = 99;
    if (!in_match) {
        g_fo_n = -1; g_fo_array = NULL; g_hold = -1; g_fapplied = false; g_fplay_match = false;
        g_fmkfx_n = 0; g_hold_fixed = 0;
        g_fsfx_n = 0; g_fsfx_sum = -1.0f;                               // the match is over: its effects are gone
        return;
    }
    if ((void *)f_game_array() != g_fo_array || g_fo_n < 0) {
        if (!f_capture(stage)) return;
        g_ffrozen_ok[0] = g_ffrozen_ok[1] = g_ffrozen_ok[2] = g_ffrozen_ok[3] = false;
        if (g_forge_session) {
            int r = f_load(stage, true);
            if (r > 0) set_status("Your saved map for this stage is loaded");
            InterlockedExchange(&g_fplay, f_play_flag() ? 1 : 0);
            f_hist_reset();
        } else if (!g_fapplied && f_play_flag() && !f_online_match()) {
            if (f_load(stage, true) > 0) logf("FORGE: saved map of %s applied to this match (play in matches is on)", g_fstem);
            int sp = f_apply_spawns(*(char **)(g_base + RVA_CP_ENTRIES), *(unsigned char *)(g_base + RVA_CP_COUNT),
                                    cwo_caps(0xD10), cwo_actors());
            if (sp) logf("FORGE: %d Forge spawns used in this match", sp);
            g_fplay_match = true;
            g_fapplied = true;
        } else if (!g_fapplied && f_play_flag()) {                // v5.186.0 online: the stock stage, like the other players
            g_fapplied = true;
            logf("FORGE: your saved map of %s is not used online (story, split screen and Single Play only)", g_fstem);
        }
    }
    if (g_fd_names != g_name_count) f_designs();           // the stage's object names arrived: categories again
    if (g_fplay_match && !g_forge_session && !g_actor_test) f_sfx_step(f_actor(0), true);   // v2.9 stage effects in matches
    if (g_fplay_match && !g_forge_session)
        f_kill_zones(*(char **)(g_base + RVA_CP_ENTRIES), *(unsigned char *)(g_base + RVA_CP_COUNT), cwo_actors(),
                     GetTickCount64());
    if (!g_forge_active && !g_actor_test) return;
    ULONG_PTR actor = f_actor(0);
    if (!actor) return;
    while (g_fq_tail != g_fq_head) {
        FReq q = g_fq[g_fq_tail];
        InterlockedExchange(&g_fq_tail, (g_fq_tail + 1) & 31);
        f_do(q, stage, actor);
    }
    f_fx_step();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (!g_qpf.QuadPart) QueryPerformanceFrequency(&g_qpf);
    float dt = g_flast.QuadPart ? (float)(now.QuadPart - g_flast.QuadPart) / (float)g_qpf.QuadPart : 0.0f;
    if (dt > 0.1f) dt = 0.1f;
    g_flast = now;
    LONG held = g_fheld;
    if (g_hold >= 0) {
        float sp = forge_speed();
        if (held & BTN_RB) g_hold_hoff += 40.0f * sp * dt;
        if (held & BTN_LB) g_hold_hoff -= 40.0f * sp * dt;
        if (held & BTN_UP) g_hold_dist += 40.0f * sp * dt;
        if (held & BTN_DOWN) g_hold_dist -= 40.0f * sp * dt;
        if (g_hold_dist < F_DIST_MIN) g_hold_dist = F_DIST_MIN;
        if (g_hold_dist > F_DIST_MAX) g_hold_dist = F_DIST_MAX;
        if (!F_SNAPS[g_fsnap] && (held & (BTN_LEFT | BTN_RIGHT))) {           // free turning: 90 degrees a second
            float &r = g_hold_rot[g_faxis == 0 ? 1 : g_faxis == 1 ? 0 : 2];
            r += ((held & BTN_RIGHT) ? 1.0f : -1.0f) * 1.5708f * sp * dt;
        }
        float p[3];
        if (g_hold_fixed) memcpy(p, g_hold_fixpos, 12);                    // coordinates editor: the numbers decide
        else {
            f_aim(actor, p);
            InterlockedExchange(&g_fmagnet_hit, g_fmagnet ? f_magnet(g_hold, p, g_hold_rot) : 0);
        }
        f_set(g_hold, p, g_hold_rot);
    }
    f_find_target(actor);
    if (!g_actor_test) {                                                     // game memory: not in the self-test
        f_caps_show(actor);
        f_mkfx_step(actor);
        f_sfx_step(actor, true);                                         // v2.9 stage effects (editor and testing)
    }
    if (InterlockedExchange(&g_ftest_start, 0)) {                         // test the map: Forge spawns, as in a match
        int sp = f_apply_spawns(*(char **)(g_base + RVA_CP_ENTRIES), *(unsigned char *)(g_base + RVA_CP_COUNT),
                                cwo_caps(0xD10), cwo_actors());
        set_status(sp ? "TESTING your map from spawn 1 (Start: Stage Creator menu)" : "TESTING (fewer than 4 spawn points: you start here)");
        if (sp) memcpy(g_ghost, (const float *)(actor + ACTOR_POS), 12);
    }
    if (g_ftesting && g_mk_n)
        f_kill_zones(*(char **)(g_base + RVA_CP_ENTRIES), *(unsigned char *)(g_base + RVA_CP_COUNT), cwo_actors(),
                     GetTickCount64());
    if (g_fgod && !g_ftesting) {                                             // health stays full while you build
        int mx = *(int *)(actor + 0x50), *hp = (int *)(actor + ACTOR_HP);
        if (mx > 0 && *hp > 0 && *hp < mx) *hp = mx;
    }
    LONG tg = g_ftarget;                                                      // highlight: the target bobs 0..0.3 m
    if (!g_fbob || g_hold >= 0 || tg != g_fbob_inst || g_ftesting) f_bob_off();
    if (g_fbob && !g_ftesting && g_hold < 0 && tg >= 0 && tg < g_fo_n && !g_fhidden[tg]) {
        float ph = (float)(now.QuadPart % (g_qpf.QuadPart * 2)) / (float)g_qpf.QuadPart * 3.14159265f;   // one bob per 2 s
        float p[3] = {g_fo[tg].pos[0], g_fo[tg].pos[1] + 1.5f + 1.5f * sinf(ph * 2.0f), g_fo[tg].pos[2]};
        f_rawpos(tg, p);
        g_fbob_inst = tg;
    }
    if (g_ffreeze && !g_ftesting) {                                          // other players stand still
        for (int k = 1; k < 4; k++) {
            ULONG_PTR a = g_actor_test ? 0 : f_actor(k);
            if (!a || *(int *)(a + ACTOR_ACTIVE) == 0) { g_ffrozen_ok[k] = false; continue; }
            float *p = (float *)(a + ACTOR_POS), *q = (float *)(a + ACTOR_PREV_POS);
            if (!g_ffrozen_ok[k]) { memcpy(g_ffrozen[k], p, 12); g_ffrozen_ok[k] = true; }
            memcpy(p, g_ffrozen[k], 12);
            memcpy(q, g_ffrozen[k], 12);
        }
    }
}

// ---- input (worker thread); true = the Forge session used this poll ----
// Menus: g_fmenu 2 = Forge menu; g_fmenu 1 = object menu, level 0 categories, level 1 the category's objects (or, for
// Markers, place / list rows), level 2 the placed markers of one type.
static const int F_MENU_ROWS = 32;                   // v5.36 + "Stage effect hold at" (row 27; the rows after it moved up one)
static const char *F_MENU_NAMES[F_MENU_ROWS] = {"Save map", "Load saved map", "Reset map to the stage file", "Undo last change",
    "Redo", "Snap angle", "Precision moves", "Fly", "Play this map in matches", "Freeze other players", "Hide the game HUD",
    "Hide my character", "Highlight what I look at", "Can't be hurt", "Magnets", "Spawn marker look", "Kill zone look",
    "Show markers", "Capsules at capsule spots", "Collision follows objects", "Skill capsule", "More copies on demand", "Menu background", "Stage effect skill", "Stage effect size",
    "Stage effect part", "Stage effect speed", "Stage effect hold at", "Place a stage effect here", "Sky", "Skill Creator", "Close"};
static const char *F_MENU_DESC[F_MENU_ROWS] = {"Save this layout (markers save as you place them)", "Go back to the last saved layout",
    "Every object back where the stage file has it", "Undo the last change (also LB)", "Redo what you undid (also RB)",
    "Step size when you turn with the D-pad", "Quarter speed, for fine placement", "Off = walk with gravity to test the layout",
    "Normal matches on this stage use your saved map", "CPU players stand still while you build",
    "Hide the battle HUD (the Forge text gets an outline)", "Hide your character's body while you build",
    "The object you look at bobs up and down", "Your health stays full while you build",
    "Held objects snap flush to nearby objects and the floor", "L/R: which skill's effect marks spawn points",
    "L/R: which skill's effect marks kill zones", "Spawn points and kill zones glow where they are",
    "Real capsules appear at the 3 capsule spots nearest you", "Moved and deleted objects take their collision with them",
    "L/R: any skill (new ones too)  A: a capsule in front of you",
    "When every copy is in use, Stage Creator makes another (up to 200)",
    "L/R or A: see-through gray box, black box, or no box behind menus (the text keeps its black stroke)",
    "L/R: the skill whose effect the next Stage effect plays", "L/R: how big the next Stage effect is",
    "L/R: which part (part 0 sits on the player; the others stay where placed)", "L/R: animation speed (0 = frozen)",
    "L/R: hold the next stage effect on one step of its animation (off = it plays); hold L/R to run",
    "A: puts the stage effect where you aim (no need for the object menu)",
    "L/R: the sky (stages with a Skies category)", "A: make and change your own skills", ""};
static const char *F_PAUSE_NAMES[6] = {"Resume building", "Test this map (play it now)", "Save map", "Save and exit Stage Creator",
    "Exit without saving", "Game pause menu (options, Give Up)"};
static const char *F_PAUSE_DESC[6] = {"Back to the editor", "Play your layout: spawns, capsules and kill zones work",
    "Save this layout", "Saves, then back to Edgar's Dream (Create a Stage)",
    "Back to Edgar's Dream (Create a Stage); changes since the last save are lost",
    "The game's own pause menu"};
static const char *F_EDIT_NAMES[8] = {"X", "Y (height)", "Z", "Pitch", "Yaw", "Roll", "Place it here", "Cancel"};

static int f_marks_of(int type) {
    int n = 0;
    for (int i = 0; i < g_mk_n; i++) n += !strcmp(g_mk[i].type, F_MARK_TYPES[type]);
    return n;
}

// v5.36 (owner while the list said 1): the stage's
// own start points, read as the game does (Ghidra batch_065 FUN_1401d1da0 / FUN_14024e190: spawn data *0x140530320,
// block b's offset at +4 + (b + 0x28) * 4 (0xFFFFFFFF = none), its count at +4 (byte), entries from +8, 0x50 bytes each:
// +0 x y z, +0xc heading); block 0 holds start points 1-4, block 1 a six-spawn stage's 5-6
static int f_stage_spawns(float out[][3], int max) {
    int n = 0;
    __try {
        char *d = *(char **)(g_base + 0x530320);
        for (int b = 0; d && b < 2 && n < max; b++) {
            unsigned o = *(unsigned *)(d + 4 + (unsigned)(b + 0x28) * 4);
            if (o == 0xFFFFFFFFu) continue;
            int cnt = *(unsigned char *)(d + o + 4);
            for (int k = 0; k < cnt && n < max; k++, n++) memcpy(out[n], d + o + 8 + 0x50 * k, 12);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { n = 0; }
    return n;
}
static int f_stage_spawn_n() { float p[8][3]; return f_stage_spawns(p, 8); }

static int f_mark_row(int type, int row) {                   // level 2 row (1..n) -> marker index
    int n = 0;
    for (int i = 0; i < g_mk_n; i++)
        if (!strcmp(g_mk[i].type, F_MARK_TYPES[type]) && ++n == row) return i;
    return -1;
}

static int f_menu_rows() {
    if (g_fmenu == 2) return F_MENU_ROWS;
    if (g_fmenu == 3) return 8;
    if (g_fmenu == 4) return 6;
    if (g_fmenu == 6) return 6;                              // v3.9 School / Character test menu
    if (g_fmenu == 7) return 7;                              // v4.2 Skill Creator pause
    if (g_fmenu == 5) return sc_rows_n();                       // v5.32 the rows follow the skill
    if (g_fmenu_level == 0) return g_fcat_n + 1;            // + "Remove ALL placed objects"
    if (g_fmenu_level == 2) return 1 + f_marks_of(g_fmenu_mtype) + (g_fmenu_mtype == 0 ? f_stage_spawn_n() : 0);   // v5.36 + the stage's own
    if (g_fmenu_cat == 0) return 8;
    int n = 0;
    for (int k = 0; k < g_fd_n; k++) n += g_fd[k].cat == g_fmenu_cat;
    return n;
}

static int f_menu_design(int row) {
    int n = 0;
    for (int k = 0; k < g_fd_n; k++)
        if (g_fd[k].cat == g_fmenu_cat && n++ == row) return k;
    return -1;
}

static void f_menu_turn(int dir);

static void f_forge_menu_choose(int row) {
    switch (row) {
    case 0: f_post(F_SAVE); break;
    case 1: f_post(F_LOAD); break;
    case 2: f_post(F_RESET); break;
    case 3: f_post(F_UNDO); return;
    case 4: f_post(F_REDO); return;
    case 5: InterlockedExchange(&g_fsnap, (g_fsnap + 1) % 7); return;
    case 6: InterlockedExchange(&g_fprecise, !g_fprecise); return;
    case 7: g_fly_landed = !g_fly_landed; return;
    case 8: {
        bool on = !g_fplay;
        f_set_play_flag(on);
        InterlockedExchange(&g_fplay, on ? 1 : 0);
        set_status(on ? "Play in matches ON: matches on this stage use your saved map" : "Play in matches OFF");
        return;
    }
    case 9: InterlockedExchange(&g_ffreeze, !g_ffreeze); return;
    case 10: g_hide_hud = !g_hide_hud; return;
    case 11: InterlockedExchange(&g_fhide_player, !g_fhide_player); return;
    case 12: InterlockedExchange(&g_fbob, !g_fbob); return;
    case 13: InterlockedExchange(&g_fgod, !g_fgod); return;
    case 14: InterlockedExchange(&g_fmagnet, !g_fmagnet); return;
    case 15: case 16: f_menu_turn(1); return;
    case 17: InterlockedExchange(&g_fmk_fx, !g_fmk_fx); InterlockedExchange(&g_fmk_change, 1); return;
    case 18: InterlockedExchange(&g_fcaps, !g_fcaps); return;
    case 19: InterlockedExchange(&g_fcol, !g_fcol); return;
    case 20: if (g_sk_count > 0) { InterlockedExchange(&g_drop_request, 1); rumble(1); } break;   // v2.6: drop_skill (game thread)
    case 21: InterlockedExchange(&g_fcopies, !g_fcopies); return;
    case 22: case 23: case 24: case 25: case 26: case 27: case 29: f_menu_turn(1); return;
    case 28: f_post(F_MARK, 3); rumble(1); break;               // v3.0: place a stage effect at the aim point
    case 30: InterlockedExchange(&g_fmenu_sel, 0); sc_open(); InterlockedExchange(&g_fmenu, 5); return;
    }
    InterlockedExchange(&g_fmenu, 0);
}

static ULONGLONG g_fturn_down, g_fturn_last;                 // D-pad L/R hold-to-repeat (worker thread)

// the loader presses buttons for the owner: `bits` held for `ms` (filtered_read adds them to every reading)
static void f_press(LONG bits, ULONGLONG ms) {
    InterlockedExchange64(&g_inject_until, (LONGLONG)(GetTickCount64() + ms));
    InterlockedExchange(&g_inject, bits);
}
// v5.188.38 (hourly pass 8, in game, no controller: "3 vs 3: the lists did not move on - pick the stage by hand" - the
// loader's presses through the game's lists go through the pad's reading, which never runs without a pad): with no pad the
// same presses go the game's own key way - bit n of *0x14054ca48 + held 0x1405ae178[n], the menu buttons read live from the
// key table 0x1403dcd70: 0 Up, 1 Down, 2 Left, 3 Right, 4 A, 5 B, 7 X, 8 Y, 12 Start (Enter). Worker thread, every step.
// v5.193.0 test helper (owner 2026-11-22: the game sits on the top monitor while the owner's other work keeps the focus): the
// auto pad's KEY=<name> presses the game's own key table for 150 ms even with a pad connected (keyboard = player 2 there)
static volatile LONG g_key_test;
static volatile LONGLONG g_key_test_until;
static void f_inject_keys_step() {
    static DWORD set;
    bool test = GetTickCount64() < (ULONGLONG)g_key_test_until;
    if (g_pad0 && !test && !set) return;

    static const struct { LONG wgi; int btn; } M[] = {{0x40, 0}, {0x80, 1}, {0x100, 2}, {0x200, 3}, {0x4, 4}, {0x8, 5},
                                                       {0x10, 7}, {0x20, 8}, {0x1, 12}};
    DWORD want = 0;
    if (!g_pad0 && GetTickCount64() < (ULONGLONG)g_inject_until) for (const auto &m : M) if (g_inject & m.wgi) want |= 1u << m.btn;
    if (test) for (const auto &m : M) if (g_key_test & m.wgi) want |= 1u << m.btn;
    if (want == set) return;
    __try {
        volatile DWORD *keys = (volatile DWORD *)(g_base + 0x54ca48);
        volatile unsigned char *held = (volatile unsigned char *)(g_base + 0x5ae178);
        for (int n = 0; n < 16; n++) {
            DWORD bit = 1u << n;
            if ((want & bit) && !(set & bit)) { *keys |= bit; held[n] = 1; }
            if (!(want & bit) && (set & bit)) { *keys &= ~bit; held[n] = 0; }
        }
        set = want;
    } __except (EXCEPTION_EXECUTE_HANDLER) { set = 0; }
}

// Exit Stage Creator: the game's own forfeit (View held 3.5 s, found by the owner 2026-10-23), then B on the preset,
// deck and stage lists (Single Play states 3 -> 2 -> 0 -> title, FUN_140192a50 / 140192980 / 140192780)
static void ed_reopen(int page);                            // v3.9 cwo_edgar.inl
static void f_exit_step() {
    if (!g_fexit_phase) return;
    ULONGLONG now = GetTickCount64();
    LONG st = g_forge_sp_state;
    if (g_fexit_phase == 1) {
        if (g_fexit_save) f_post(F_SAVE);
        // v4.6 (owner): the game's own Give Up, run on the game thread
        // (pause menu apame_s1 -> native 4.211 = FUN_140210d10: *0x1404bca2c = 5, FUN_14012dca0); View held only if the
        // match is still on after 3 s
        InterlockedExchange(&g_fexit_giveup, 1);
        g_fexit_t = now;
        InterlockedExchange(&g_fexit_phase, 2);
        logf("FORGE: exit - the game's Give Up asked");
        return;
    }
    if (g_fexit_phase == 2) {
        if (st != SP_MATCH) { InterlockedExchange(&g_fexit_phase, 3); g_fexit_t = now; return; }
        // v5.32: no buttons are held for the owner any more (the held View kept the Give Up box's cursor on Cancel); the
        // game's Give Up is asked again while a game menu is open (ed_giveup_step waits for it to close)
        static ULONGLONG asked_at;
        if (now - g_fexit_t > 8000 && now - asked_at > 8000 && !g_fexit_giveup) {   // v5.35: room for the battle's own end
            InterlockedExchange(&g_fexit_giveup, 1);
            asked_at = now;
            logf("FORGE: exit - the match is still on: Give Up asked again");
        }
        // v5.127.1 (in game 2026-11-09, CREATE's Exit: Give Up ended the battle, but "YOU LOSE" and RESULTS wait for
        // Enter / A and the exit timed out on them): in a CREATE session the loader presses A through those two screens
        static ULONGLONG a_at;
        if (g_cr_entry && now - g_fexit_t > 3000 && now - a_at > 1500) { f_press(0x4, 120); a_at = now; }
        if (now - g_fexit_t > 20000) {
            InterlockedExchange(&g_fexit_phase, 0);
            InterlockedExchange(&g_fexit_giveup, 0);
            set_status("Could not leave: Start > Game pause menu > Give Up");
            logf("FORGE: exit - Give Up did not end the match");
        }
        return;
    }
    if (!g_forge_session || now - g_fexit_t > 15000) { InterlockedExchange(&g_fexit_phase, 0); InterlockedExchange(&g_fexit_to_ed, 0); logf("FORGE: exit done"); return; }
    if (g_fexit_to_ed && st == 0) {                          // v3.9: back at the stage list - Edgar's Dream again
        InterlockedExchange(&g_fexit_phase, 0);
        InterlockedExchange(&g_fexit_to_ed, 0);
        InterlockedExchange(&g_ed_mode, 0);
        ed_reopen(g_ed_try_page);
        set_status("Back in Edgar's Dream");
        logf("FORGE: exit - back to Edgar's Dream");
        return;
    }
    static ULONGLONG last_tap;
    if (st >= 0 && st != SP_MATCH && st != 1 && now - last_tap > 700) { f_press(0x8, 150); last_tap = now; }   // B, then let go
}

// v2.7 (owner): the cursor jumps to Map Slot 7's row of the game's list
// (FUN_140192780: cursor block *0x140509b88 {+0 row, +0xC variant}; rows 0x28 at 0x140509ba0, stage +0x18, count
// *0x14050a5d0), so A loads it
static void f_cursor_slot7() {
    __try {
        int *cur = *(int **)(g_base + 0x509b88), rows = *(int *)(g_base + 0x50a5d0);
        for (int r = 0; cur && r < rows && r < 64; r++)
            if (*(int *)(g_base + 0x509ba0 + 0x28 * r + 0x18) == LIST_STAGE) { cur[0] = r; cur[3] = 0; break; }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// ---- v3.3 Edgar's Dream chooser (owner); v3.4: every creator is built (cwo_edgar.inl) ----
static volatile LONG g_ed_sel;
static const int ED_N = 14;                                     // v5.61 + Create an Outfit, v5.86 + Visions
static const char *ED_NAMES[ED_N] = {"Create a Stage", "Create a Skill", "Create a School (aura color + aura effect)",
                                     "Create a Character", "Create a Quest", "Create a Mode", "Spectator (watch CPU fights)",
                                     "Skill Fuser (fuse two close-range skills)", "Records (wins, losses, streaks, skills)",
                                     "Stage Music (pick each stage's music)", "Draft Arsenal (pick skills in turns)",
                                     "Boss Raid (you and CPU allies against a story boss)", "Create an Outfit (colors, skin, head accessory)",
                                     "Visions (graphics: clarity, colors, blur)"};
static const char *ED_DESC[ED_N] = {"Build a map: next pick the stage (LB/RB: templates)", "Opens the Skill Test Stage with the Skill Creator",
                                    "Aura color, a skill effect as the aura, the school's skills",
                                    "Name, body, school: your fighter", "Your own campaign quest: stage, time, opponents",
                                    "Time, life, lives, capsules - pick it in a lobby (Change Rule)", "Let the CPU play your fighter and watch (test)",
                                    "Both skills' effects on one move: a new skill in your fusion stock",
                                    "Every offline match counted: by character, by mode, your most-used skills",
                                    "Any soundtrack or track of the game on any stage (offline)",
                                    "You and the CPU (or player 2) draft 10 skills each, then fight",
                                    "Peccato, Thalasso, Carcino, Demented Freia: 1 or 2 CPU allies at your side",
                                    "A character's outfit: skin tone, head accessory, jacket and pants colors",
                                    "Graphics looks: sharpen, colors, contrast, vignette, blur (offline)"};

// ---- v5.27 Edgar's Dream as a real game menu (owner 2026-10-27). The main menu's EDGAR'S DREAM
// opens the game's own submenu window (built like MULTIPLAYER: tools/build_edgar_menu.py) with one row per entry and its
// help line; a row chosen there opens that page directly once the session starts (native 2.8 ops 46-48).
static const char *ED_SHORT[ED_N] = {"Create a Stage", "Create a Skill", "Create a School", "Create a Character", "Create a Quest",
                                     "Create a Mode", "Spectator", "Skill Fuser", "Records", "Stage Music", "Draft Arsenal", "Boss Raid",
                                     "Create an Outfit", "Visions"};
// v5.58 the BFR edition's Edgar's Dream (owner's BFR selection): Create a Stage, Create a Mode, Spectator - in that order
static const int ED_BFR[5] = {0, 5, 6, 12, 13};                // v5.61 + Create an Outfit, v5.86 + Visions (owner 2026-11-03: BFR first)
// v5.151.0 (owner 2026-11-12): the BFR edition has every entry, as MODS (ED_BFR kept
// for the record; the quick panel on Back stays MODS-only, owner 2026-11-04)
static const bool g_ed_bfr_short = false;
// v5.152 CREATE (owner 2026-11-12, "Do all of these": "CREATE on the main menu: Stage, Skill, Character, Outfit, School, Quest,
// Mode, Skill Fuser ... Into the lobbies: Boss Raid, Spectator, Draft Arsenal. Into Options: Records, Visions, Stage Music"): the
// menu (both editions) lists only the creators, CREATE (the full-screen editor) first; the other pages open from the SINGLE PLAY
// lobby and from OPTIONS through op 47's page codes ED_GO_* below
// v5.188.19 (owner 2026-11-16): a Create a Stage row (entry 0
// too) besides the Full-Screen Editor (row 0). v5.188.20: on row 2 - the menu script's row 1 handler goes straight into Single
// Play (the game's own 3-screen Stage Creator list, seen in game), rows 0 and 2.. ask op 51 first = the loader's CREATE A STAGE page
static const int ED_CREATE[9] = {0, 1, 0, 3, 12, 2, 4, 5, 7};
static const int ED_CREATE_N = 9;
static const int ED_GO_SINGLE = 20, ED_GO_RECORDS = 21, ED_GO_VISIONS = 22, ED_GO_MUSIC = 23;   // op 47 codes past the entries
static volatile LONG g_ed_from_opt;                              // v5.188.2 a page opened from OPTIONS (Records / Visions / Stage Music)
static bool ed_allowed(int k) {
    if (k < 0 || k >= ED_N) return false;
    for (int i = 0; i < ED_CREATE_N; i++) if (ED_CREATE[i] == k) return true;
    return false;
}
static int ed_row_entry(INT64 row) { return row >= 0 && row < ED_CREATE_N ? ED_CREATE[row] : (row >= ED_GO_SINGLE && row <= ED_GO_MUSIC ? (int)row : -1); }
static INT64 edm_native(int op, INT64 *args) {                   // game thread (the main menu script)
    INT64 k = ed_row_entry(args[1]);   // v5.58 a row's entry (the BFR edition shows only its 3)
    if (op == 46 && k == 0 && args[1] == 0) return (INT64)(ULONG_PTR)"Full-Screen Editor";   // v5.152 the menu itself is CREATE now (was "CREATE", v5.127)
    if (op == 48 && k == 0 && args[1] == 0) return (INT64)(ULONG_PTR)"The full-screen editor: stage, skill, outfit, character and more";
    if (op == 48 && k == 0) return (INT64)(ULONG_PTR)"Pick a stage, then build on it in the editor";
    if (op == 46) return k >= 0 && k < ED_N ? (INT64)(ULONG_PTR)ED_SHORT[k] : 0;
    if (op == 48) return k >= 0 && k < ED_N ? (INT64)(ULONG_PTR)ED_DESC[k] : 0;
    if (op == 47) {
        bool go = k >= ED_GO_SINGLE && k <= ED_GO_MUSIC;
        InterlockedExchange(&g_ed_direct, (k >= 0 && k < ED_N) || go ? (LONG)k : -1);
        if (k >= 0 && k < ED_N) { InterlockedExchange(&g_ed_from_opt, 0); logf("CREATE menu: %s chosen", ED_SHORT[k]); }   // v5.188.2
        el_off("another menu entry chosen");                      // v5.194.0
        if (go) logf("main menu: %s chosen", k == ED_GO_SINGLE ? "SINGLE PLAY" : k == ED_GO_RECORDS ? "Records" : k == ED_GO_VISIONS ? "Visions" : "Stage Music");
        return 1;
    }
    return 0;
}
static volatile LONG g_ed_ret_page;                              // v5.152 a page opened from the SINGLE PLAY lobby goes back there on B
static volatile LONG g_sp_entry;                                 // v5.152 the lobby was opened as SINGLE PLAY (main menu > MULTIPLAYER)
static void ed_go_page(LONG d);
static void edm_session_start() {                               // the session began: that page at once (the chooser's A)
    LONG d = InterlockedExchange(&g_ed_direct, -1);
    if (d >= ED_GO_SINGLE && d <= ED_GO_MUSIC) { ed_go_page(d); return; }   // v5.152 SINGLE PLAY / OPTIONS pages: no chooser (cwo_lobby.inl)
    if (d >= 0 && d < ED_N) {
        InterlockedExchange(&g_ed_sel, d); InterlockedExchange(&g_ed_auto_choose, 1);
    }
}
static int edm_self_test() {
    INT64 a[2] = {46, 3};                                        // v5.188.20 the CREATE list: row 3 = Create a Character, row 8 = Skill Fuser
    bool names = !strcmp((const char *)(ULONG_PTR)edm_native(46, a), "Create a Character");
    a[1] = 8; names &= !strcmp((const char *)(ULONG_PTR)edm_native(46, a), "Skill Fuser") && edm_native(48, a) == (INT64)(ULONG_PTR)ED_DESC[7];
    a[1] = ED_CREATE_N; names &= edm_native(46, a) == 0 && edm_native(48, a) == 0;
    a[1] = 2; names &= !strcmp((const char *)(ULONG_PTR)edm_native(46, a), "Create a Stage");
    a[1] = 0; names &= !strcmp((const char *)(ULONG_PTR)edm_native(46, a), "Full-Screen Editor");   // Records, Boss Raid ... are not CREATE rows
    LONG keep_sel = g_ed_sel, keep_auto = g_ed_auto_choose;
    a[1] = 8; edm_native(47, a); edm_session_start();
    bool open = g_ed_sel == 7 && g_ed_auto_choose == 1 && g_ed_direct == -1;
    InterlockedExchange(&g_ed_auto_choose, 0); edm_session_start();
    open &= g_ed_auto_choose == 0;                               // nothing chosen: the chooser as before
    InterlockedExchange(&g_ed_sel, keep_sel); InterlockedExchange(&g_ed_auto_choose, keep_auto);
    logf("self-test: Edgar's Dream menu names %s, open %s", names ? "ok" : "FAILED", open ? "ok" : "FAILED");
    return names && open ? 1 : -70;
}

#include "cwo_edgar.inl"

// v5.1.2 / v5.2 (owner; the 3 VS 3 LOBBY): the lobby has the
// stage, the characters and the arsenals, so the loader passes Single Play's lists for you - the stage list with its cursor
// on the lobby's stage (FUN_140192780 reads *0x140509b88 [0] row, [3] variant), the arsenal list and the battle list (A on
// the top row, as Create a Skill does); cwo_sixf2.inl then builds the 6 fighters from the lobby. Leaving Single Play ends it.
static void ed_six_step() {
    if (g_l3 == 1) {                                               // v5.3 from Edgar's Dream to the main menu: B on Single Play's list
        static ULONGLONG lastb;
        ULONGLONG t = GetTickCount64();
        __try {
            ULONG_PTR m = f_mode_obj();
            bool spm = m && *(int *)(m + 0x7C) == MODE_SINGLE_PLAY && !(*(unsigned *)(m + 0x08) & 0x40000);
            ULONG_PTR so = spm ? *(ULONG_PTR *)(m + 0x60) : 0;
            if (spm && so && *(int *)so == 0 && t - lastb > 1500) { f_press(0x8, 150); lastb = t; }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    // v5.3.7 (owner 16:49: the lobby's START went to a plain Single Play battle): the hand-over takes some frames to reach
    // Single Play; "left Single Play" now counts only after Single Play was seen (or 20 s without reaching it)
    static bool s_active, s_seen;
    static ULONGLONG s_t0;
    bool raid = g_sp_boss_row >= 0;                              // v5.152 SINGLE PLAY Boss Raid: the game's boss row
    if (!g_six_auto && !g_lob_go && !raid) { s_active = false; return; }
    ULONGLONG now = GetTickCount64();
    if (!s_active) { s_active = true; s_seen = false; s_t0 = now; }
    bool sp = false;
    int st = -1;
    __try {
        ULONG_PTR m = f_mode_obj();
        if (m) {
            sp = *(int *)(m + 0x7C) == MODE_SINGLE_PLAY && !(*(unsigned *)(m + 0x08) & 0x40000);
            ULONG_PTR s = *(ULONG_PTR *)(m + 0x60);
            if (sp && s) st = *(int *)s;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { sp = false; }
    if (sp) s_seen = true;
    if (!sp && !s_seen && now - s_t0 < 20000) return;              // still on the way to Single Play
    if (!sp) {
        logf(s_seen ? "3 vs 3: left Single Play - the lobby match is over" : "3 vs 3: Single Play not reached in 20 s - lobby start given up");
        s_active = false;
        InterlockedExchange(&g_six_auto, 0);
        InterlockedExchange(&g_lob_go, 0);
        InterlockedExchange(&g_sp_boss_row, -1);
        return;
    }
    // v5.188.45 (hourly pass 11, in game: after a SINGLE PLAY lobby match and its RESULTS the game went on to its own Single
    // Play list - "debug: Basic / Decoy / Edgar-Dodge-Bot", the old debug screen the owner never wants to see): once the
    // match is over, B on the game's lists (as the player would) back to its first list (state 0), then the SINGLE PLAY
    // lobby again. The result screens (YOU WIN / LOSE, RESULTS) are left to the player.
    {
        static bool s_played;
        static ULONGLONG s_tap, s_over, s_watch, s_wlast;
        if (st == SP_MATCH) { s_played = true; s_over = 0; }
        else if (s_played && !g_in_match && !g_six_auto) {
            if (!s_over) s_over = now;
            if (st == 0) {
                s_played = false; s_over = 0;
                InterlockedExchange(&g_sp_entry, 1);
                // v5.194.3 (in game 2026-11-22: "lobby check: page open 1, session 0" - the lobby was open but only the game's own
                // debug stage list showed): the lobby is drawn by the session's screen, and the match had ended the session
                InterlockedExchange(&g_forge_seen_sp, 1);
                InterlockedExchange(&g_forge_session, 1);
                ed_reopen(EDP_LOBBY);
                if (g_el_restart == 1) InterlockedExchange(&g_el_restart, 2);   // v5.196.0 Effect Lab reload: START GAME again (pad thread)
                set_status("Back in the SINGLE PLAY lobby");
                logf("SINGLE PLAY: the match is over - back in the lobby");
                s_watch = now;                                    // v5.188.49 the redraw check below
            } else if (st != 1 && now - s_tap > 900) { b2m_press(0x8); s_tap = now; logf("SINGLE PLAY: after the match - B on the game's list (state %d)", st); }
            if (s_played && now - s_over > 30000) { s_played = false; s_over = 0; logf("SINGLE PLAY: after the match - the lobby did not come back in 30 s"); }
        }
        // v5.188.49 (owner work order 2026-11-21: the lobby is not redrawn after a Single Play match): for 10 s after the
        // return, once a second, what decides whether the lobby page is drawn (cwo_loader.cpp filtered_read / the draw gate
        // g_ed_open && g_forge_session && !g_forge_active) - read-only, for the next in-game check
        if (s_watch && now - s_watch < 10000 && now - s_wlast >= 1000) {
            s_wlast = now;
            logf("SINGLE PLAY lobby check +%llus: list state %d, page open %ld, session %ld, editor %ld, in match %d, menu %ld",
                 (now - s_watch) / 1000, (int)st, (long)g_ed_open, (long)g_forge_session, (long)g_forge_active, (int)g_in_match, (long)g_fmenu);
        }
    }
    if (!g_six_auto) return;
    static ULONGLONG since, started;
    static int last = -2;
    static bool pressed;
    if (st != last) { last = st; since = now; pressed = false; if (!started) started = now; }
    if (st == SP_MATCH) { InterlockedExchange(&g_six_auto, 0); started = 0; logf("3 vs 3: match starting"); return; }
    if (started && now - started > 40000) {
        InterlockedExchange(&g_six_auto, 0); started = 0;
        set_status("3 vs 3: the lists did not move on - pick the stage by hand (the lobby's fighters still come)");
        logf("3 vs 3: walking the lists timed out in state %d", st);
        return;
    }
    // v5.2.2 (owner crash 10:47: A pressed twice on the battle list while the match loaded): one A per list; the stage and
    // arsenal lists get a second A after 3 s only if they are still showing; the battle list never gets a second one
    if (now - since < 600 || (pressed && (st == 3 || now - since < 3000))) return;
    if (st == 0 && (g_lob_go || raid)) {
        __try {
            int *cur = *(int **)(g_base + 0x509b88);
            if (cur) { cur[0] = g_lob_stage; cur[3] = g_lob_var; }
            ULONG_PTR m = f_mode_obj(), so = m ? *(ULONG_PTR *)(m + 0x60) : 0;
            if (so) *(int *)(so + 0x10) = 0;                  // FUN_140192780: +0x10 set = a random stage instead of the row
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        f_press(0x4, 150);
    } else if ((st == 2 && (g_lob_go || raid)) || st == 3) {
        if (st == 3 && raid) {                                   // v5.152 the boss preset: list cursor = state +4 (FUN_140192a50)
            __try {
                ULONG_PTR m = f_mode_obj(), so = m ? *(ULONG_PTR *)(m + 0x60) : 0;
                if (so) *(int *)(so + 4) = g_sp_boss_row;
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
            logf("SINGLE PLAY: Boss Raid - battle list row %d", (int)g_sp_boss_row);
        }
        f_press(0x4, 150);
    } else return;
    pressed = true;
    since = now;
    logf("3 vs 3: list %d passed", st);
}

static bool forge_input(WORD b, WORD pressed, bool in_match) {
    ed_six_step();
    forge_poll(in_match);
    f_exit_step();
    if (!g_forge_session) return false;
    InterlockedExchange(&g_fheld, b);
    if (g_ed_open && !g_forge_active) {                         // v3.3 Edgar's Dream chooser
        // v5.32 (owner 2026-10-27): a page chosen in the game's EDGAR'S DREAM window is opened at once - the old chooser waited
        // for the main menu's A to be let go and was drawn meanwhile; ed_close holds the game's buttons until all are up
        if (g_ed_auto_choose) {
            // v5.33: never a black screen for good - a choice that has not gone through in 6 s is dropped
            static ULONGLONG since;
            ULONGLONG now = GetTickCount64();
            if (!since) since = now;
            if (now - since > 6000) {
                since = 0;
                InterlockedExchange(&g_ed_auto_choose, 0);
                InterlockedExchange(&g_cs_hold_cover, 0);
                InterlockedExchange(&g_ed_page, EDP_CHOOSER);
                g_cs_go = -1;
                set_status("Edgar's Dream: pick again");
                logf("Edgar's Dream: the page chosen in the menu did not open in 6 s - the chooser instead");
                return true;
            }
            ed_input(0, 0);
            if (!g_ed_auto_choose) since = 0;
            return true;
        }
        if (!ed_release_gate(b)) ed_input(b, pressed);
        return true;
    }
    if (g_ed_auto >= 31 && g_ed_auto <= 35) { cs_auto_step(g_forge_sp_state, GetTickCount64()); return true; }   // v5.32 CREATE A STAGE
    if (g_ed_auto) {                                           // v3.9 Try it now: stage list A, deck A, 'debug' A
        static ULONGLONG t;
        ULONGLONG now = GetTickCount64();
        LONG st = g_forge_sp_state;
        if (g_ed_auto == 1) { t = now; InterlockedExchange(&g_ed_auto, 2); }
        else if (g_ed_auto == 2 && st == 0 && now - t > 700) { f_cursor_slot7(); f_press(0x4, 150); t = now; InterlockedExchange(&g_ed_auto, 3); }
        else if (g_ed_auto == 3 && st == 2 && now - t > 700) { f_press(0x4, 150); t = now; InterlockedExchange(&g_ed_auto, 4); }
        else if (g_ed_auto == 4 && st == 3 && now - t > 700) { f_press(0x4, 150); t = now; InterlockedExchange(&g_ed_auto, 5); }
        else if (g_ed_auto == 5 && st == SP_MATCH) InterlockedExchange(&g_ed_auto, 0);
        // v4.4 Create a Skill: stage list A, the arsenal is the player's, then 'debug' (top row) without asking
        else if (g_ed_auto == 11) { t = now; InterlockedExchange(&g_ed_auto, 12); }
        else if (g_ed_auto == 12 && st == 0 && now - t > 700) { f_cursor_slot7(); f_press(0x4, 150); t = now; InterlockedExchange(&g_ed_auto, 13); }
        else if (g_ed_auto == 13 && st == 2) { t = now; return false; }   // the arsenal list: the player chooses
        else if (g_ed_auto == 13 && st == 3 && now - t > 500) { f_press(0x4, 150); t = now; InterlockedExchange(&g_ed_auto, 15); }
        else if (g_ed_auto == 15 && st == SP_MATCH) InterlockedExchange(&g_ed_auto, 0);
        if (g_ed_auto && now - t > 20000) { InterlockedExchange(&g_ed_auto, 0); set_status("Could not open the Skill Test Stage on its own: pick it by hand"); }
        return true;
    }
    // v2.5 (owner: template stages with every stage's pieces, a blank Skill Test Stage): on the stage list LB / RB
    // choose what Map Slot 7 loads (Map\mx_list.txt: stage packs and the Stage Creator templates), as the quick
    // panel's Stages page does (mx_apply: the slot's two stage-file names, logged)
    if (!g_forge_active && g_forge_sp_state == 0 && (pressed & (BTN_LB | BTN_RB)) && g_mx_count > 0) {
        LONG k = g_mx_cur + ((pressed & BTN_RB) ? 1 : -1);
        if (k >= g_mx_count) k = -1;
        if (k < -1) k = g_mx_count - 1;
        if (k >= 0) InterlockedExchange(&g_mx_sel, k);
        mx_apply(k);
        f_cursor_slot7();
        rumble(1);
        return true;
    }
    if (!g_forge_active) return true;                    // Forge menus before the match: the game's own lists
    if (g_fexit_phase) return true;                      // leaving: the loader is pressing the buttons
    if (g_game_menu) {                                   // the game's pause menu is open: it gets every button
        if (g_fmenu) InterlockedExchange(&g_fmenu, 0);
        return true;
    }
    if (pressed & BTN_START) {                           // Start: the Stage Creator pause menu
        InterlockedExchange(&g_fmenu_sel, 0);
        if (g_ed_mode == 2) {                                // v3.9: the School / Character test menu
            InterlockedExchange(&g_ed_live, 0);
            InterlockedExchange(&g_fmenu, g_fmenu == 6 ? 0 : 6);
            return true;
        }
        if (g_ed_mode == 1) {                                // v4.2 (owner): the Skill Creator pause, not the Stage Creator's
            InterlockedExchange(&g_fmenu, g_fmenu == 7 ? 0 : 7);
            return true;
        }
        InterlockedExchange(&g_fmenu, g_fmenu == 4 ? 0 : 4);
        return true;
    }
    // v5.9 (owner): R3 in a school / character test
    // starts the aura effect again from its beginning (cwo_sfx.inl), in the page over the match or while playing
    if (g_ed_mode == 2 && (pressed & BTN_RTHUMB) && !g_fmenu) {
        InterlockedExchange(&g_sfx_restart, 1);
        set_status("Aura effect restarted (R3)");
        if (g_ed_live) return true;
    }
    // v5.33 (owner): L3 in a Create a Skill test shows / hides the game's HUD
    if (g_ed_mode == 1 && g_ftesting && (pressed & BTN_LTHUMB)) {
        g_hide_hud = !g_hide_hud;
        set_status(g_hide_hud ? "HUD hidden (L3: show it)" : "HUD shown (L3: hide it)");
        rumble(1);
    }
    // v5.188.49 (owner 2026-11-21): in Create a Skill's test (Keep testing, no menu open) B uses the
    // skill once, R3 loops it, X / Y undo / redo; Start = the pause menu and L3 = HUD as before. The game does not get B, X,
    // Y or R3 there (filtered_read); outside the test every button keeps its old job.
    if (g_ed_mode == 1 && g_ftesting && !g_fmenu) {
        if (pressed & BTN_B) { InterlockedExchange(&g_sct_play, 1); rumble(1); }
        if (pressed & BTN_RTHUMB) { sc_preview_toggle(); rumble(1); }
        if (pressed & BTN_X) { sc_undo(); rumble(1); }
        if (pressed & BTN_Y) { sc_redo(); rumble(1); }
    }
    if (g_ed_live) {                                         // v3.9 editing the school during the test (it applies at once)
        if (pressed & BTN_B) { InterlockedExchange(&g_ed_live, 0); InterlockedExchange(&g_fmenu_sel, 1); InterlockedExchange(&g_fmenu, 6); return true; }
        ed_input(b, pressed);
        if (g_ed_page != g_ed_try_page) InterlockedExchange(&g_ed_page, g_ed_try_page);
        return true;
    }
    if (g_ftesting && g_fmenu != 4 && g_fmenu != 6 && g_fmenu != 5 && g_fmenu != 7) return true;   // testing: the game plays
    ULONGLONG now = GetTickCount64();
    if (g_fmenu) {
        int rows = f_menu_rows();
        static ULONGLONG ud_down, ud_last;                   // v3.0: hold Up / Down to keep moving (owner)
        bool step_dn = (pressed & BTN_DOWN) != 0, step_up = (pressed & BTN_UP) != 0;
        if (step_dn || step_up) ud_down = ud_last = now;
        else if ((b & (BTN_UP | BTN_DOWN)) && now - ud_down > 350 && now - ud_last > 80) {
            step_dn = (b & BTN_DOWN) != 0; step_up = (b & BTN_UP) != 0; ud_last = now;
        }
        if (step_dn) InterlockedExchange(&g_fmenu_sel, rows ? (g_fmenu_sel + 1) % rows : 0);
        if (step_up) InterlockedExchange(&g_fmenu_sel, rows ? (g_fmenu_sel + rows - 1) % rows : 0);
        if (g_fmenu != 3) {
            if (pressed & BTN_RB) InterlockedExchange(&g_fmenu_sel, rows ? (g_fmenu_sel + 8 < rows ? g_fmenu_sel + 8 : rows - 1) : 0);
            if (pressed & BTN_LB) InterlockedExchange(&g_fmenu_sel, g_fmenu_sel >= 8 ? g_fmenu_sel - 8 : 0);
        }
        if (g_fmenu == 5 && (pressed & BTN_RTHUMB)) { sc_preview_toggle(); rumble(1); }   // v3.3 the skill itself, looping
        if (g_fmenu == 5 && (pressed & BTN_X)) { sc_undo(); rumble(1); }                 // v4.4 undo / redo
        if (g_fmenu == 5 && (pressed & BTN_Y)) { sc_redo(); rumble(1); }
        if (g_fmenu == 3 || g_fmenu == 2 || g_fmenu == 5) {   // left / right change a value (hold to repeat)
            if (pressed & (BTN_LEFT | BTN_RIGHT)) { g_sc_held_ms = 0; f_menu_turn((pressed & BTN_RIGHT) ? 1 : -1); g_fturn_down = g_fturn_last = now; }
            else if ((b & (BTN_LEFT | BTN_RIGHT)) && now - g_fturn_down > 350 && now - g_fturn_last > 90) {
                g_sc_held_ms = now - g_fturn_down;                    // v5.32 the Skill Creator's hue goes faster when held
                f_menu_turn((b & BTN_RIGHT) ? 1 : -1);
                g_fturn_last = now;
            }
        }
        if (pressed & BTN_B) {
            if (g_fmenu == 1 && g_fmenu_level == 2) { InterlockedExchange(&g_fmenu_level, 1); InterlockedExchange(&g_fmenu_sel, 4 + g_fmenu_mtype); }
            else if (g_fmenu == 1 && g_fmenu_level == 1) { InterlockedExchange(&g_fmenu_level, 0); InterlockedExchange(&g_fmenu_sel, g_fmenu_cat); }
            else if (g_fmenu == 5 && g_ed_mode == 1) { InterlockedExchange(&g_fmenu, 0); set_status("Testing: fire it.  Start: Skill Creator pause"); }   // v4.2
            else if (g_fmenu == 5) { InterlockedExchange(&g_fmenu, 2); InterlockedExchange(&g_fmenu_sel, 30); }
            else { if (g_fmenu == 3) InterlockedExchange(&g_hold_fixed, 0); InterlockedExchange(&g_fmenu, 0); }
        }
        if ((pressed & BTN_BACK) && (g_fmenu == 2 || g_fmenu == 3)) { if (g_fmenu == 3) InterlockedExchange(&g_hold_fixed, 0); InterlockedExchange(&g_fmenu, 0); }
        if (g_fmenu == 1 && g_fmenu_level == 1 && g_fmenu_cat != 0) {   // object rows: X remove all placed, Y show only / hide
            int k = f_menu_design(g_fmenu_sel);
            if (k >= 0 && (pressed & BTN_X)) { f_post(F_CLEAR_DESIGN, g_fd[k].obj); rumble(2); }
            if (k >= 0 && (pressed & BTN_Y)) { f_post(F_ISO, g_fd[k].obj); rumble(1); }
            if (k >= 0 && (pressed & (BTN_LEFT | BTN_RIGHT))) {           // fly to the next / previous placed one
                f_post(F_GOTO_OBJ, g_fd[k].obj << 1 | ((pressed & BTN_LEFT) ? 1 : 0));
                InterlockedExchange(&g_fmenu, 0);
                rumble(1);
            }
        }
        if (g_fmenu == 1 && g_fmenu_level == 2 && (pressed & BTN_X) && g_fmenu_sel > 0) {   // remove one placed marker
            int i = f_mark_row(g_fmenu_mtype, g_fmenu_sel);
            if (i >= 0) { f_post(F_MARK_DELETE, i); rumble(2); if (g_fmenu_sel >= rows - 1) InterlockedExchange(&g_fmenu_sel, rows - 2); }
        }
        if (pressed & BTN_A) {
            if (g_fmenu == 7) {                          // v4.2 Skill Creator pause
                int sel = g_fmenu_sel;
                InterlockedExchange(&g_fmenu, 0);
                switch (sel) {
                case 0: InterlockedExchange(&g_fmenu_sel, 0); InterlockedExchange(&g_fmenu, 5); break;
                case 1: break;
                case 2: sc_save_now(); break;
                case 3: sc_save_now(); InterlockedExchange(&g_ed_try_page, EDP_CHOOSER); InterlockedExchange(&g_fexit_to_ed, 1); InterlockedExchange(&g_fexit_save, 0); InterlockedExchange(&g_fexit_phase, 1); break;
                case 4: case 5:
                    sc_revert();
                    InterlockedExchange(&g_ed_try_page, EDP_CHOOSER);
                    InterlockedExchange(&g_fexit_to_ed, sel == 4); InterlockedExchange(&g_fexit_save, 0); InterlockedExchange(&g_fexit_phase, 1);
                    break;
                case 6: f_press(0x1, 150); break;        // the game's own pause menu (options, Give Up)
                }
                rumble(1);
            } else if (g_fmenu == 6) {                          // v3.9 School / Character test menu
                int pg = g_ed_try_page;
                EdCreator &c = g_edc[pg];
                switch (g_fmenu_sel) {
                case 0: break;
                case 1: InterlockedExchange(&g_ed_page, pg); InterlockedExchange(&g_ed_row, 2); InterlockedExchange(&g_ed_live, 1); break;
                case 2: ed_save(pg); g_ed_snap = c.item[c.cur]; set_status("Saved - keep testing"); break;
                case 3: ed_save(pg); InterlockedExchange(&g_fexit_to_ed, 1); InterlockedExchange(&g_fexit_save, 0); InterlockedExchange(&g_fexit_phase, 1); break;
                case 4: case 5:
                    c.item[c.cur] = g_ed_snap; ed_save(pg);  // back to the values the test began with
                    InterlockedExchange(&g_fexit_to_ed, g_fmenu_sel == 4); InterlockedExchange(&g_fexit_save, 0); InterlockedExchange(&g_fexit_phase, 1);
                    break;
                }
                InterlockedExchange(&g_fmenu, 0);
                rumble(1);
            } else if (g_fmenu == 4) {                          // Stage Creator pause menu
                switch (g_fmenu_sel) {
                case 0:                                  // resume building / back to building
                    if (g_ftesting) {
                        InterlockedExchange(&g_ftesting, 0);
                        g_fly_landed = g_ftest_keep_fly;
                        g_hide_hud = g_ftest_keep_hud;
                        memcpy(g_ghost, (const float *)(f_actor(0) + ACTOR_POS), 12);
                        set_status("Back to building");
                    }
                    break;
                case 1:                                  // test this map
                    if (!g_ftesting && g_hold < 0) {
                        g_ftest_keep_fly = g_fly_landed;
                        g_ftest_keep_hud = g_hide_hud;
                        g_fly_landed = true;
                        g_hide_hud = false;
                        InterlockedExchange(&g_ftesting, 1);
                        InterlockedExchange(&g_ftest_start, 1);
                    } else if (g_hold >= 0) set_status("Place what you are holding first");
                    break;
                case 2: f_post(F_SAVE); break;
                // v5.35 (owner 2026-10-28): both exits come back to Edgar's Dream on CREATE A STAGE
                case 3: case 4:
                    InterlockedExchange(&g_fexit_save, g_fmenu_sel == 3);
                    InterlockedExchange(&g_ed_try_page, EDP_STAGE);
                    InterlockedExchange(&g_fexit_to_ed, 1);
                    InterlockedExchange(&g_fexit_phase, 1);
                    break;
                case 5: f_press(0x1, 150); break;        // the game's own pause menu (options, Give Up)
                }
                InterlockedExchange(&g_fmenu, 0);
                rumble(1);
            } else if (g_fmenu == 3) {                   // coordinates editor: place here / cancel
                if (g_fmenu_sel == 6) { f_post(F_PLACE); InterlockedExchange(&g_fmenu, 0); rumble(1); }
                else if (g_fmenu_sel == 7) { InterlockedExchange(&g_hold_fixed, 0); InterlockedExchange(&g_fmenu, 0); }
            } else if (g_fmenu == 2) f_forge_menu_choose(g_fmenu_sel);
            else if (g_fmenu == 5) { sc_choose(g_fmenu_sel); if (g_fmenu == 2) InterlockedExchange(&g_fmenu_sel, 30); }
            else if (g_fmenu_level == 0) {
                if (g_fmenu_sel >= g_fcat_n) { f_post(F_CLEAR_ALL); InterlockedExchange(&g_fmenu, 0); rumble(2); }
                else {
                    InterlockedExchange(&g_fmenu_cat, g_fmenu_sel);
                    InterlockedExchange(&g_fmenu_level, 1);
                    InterlockedExchange(&g_fmenu_sel, 0);
                }
            } else if (g_fmenu_level == 2) {
                if (g_fmenu_sel == 0) { f_post(F_MARK_CLEAR, g_fmenu_mtype); InterlockedExchange(&g_fmenu_level, 1); InterlockedExchange(&g_fmenu_sel, 4 + g_fmenu_mtype); rumble(2); }
                else {
                    int i = f_mark_row(g_fmenu_mtype, g_fmenu_sel);
                    if (i >= 0) { f_post(F_GOTO, i); InterlockedExchange(&g_fmenu, 0); rumble(1); }
                    else if (g_fmenu_mtype == 0) { f_post(F_GOTO_SP, g_fmenu_sel - 1 - f_marks_of(0)); InterlockedExchange(&g_fmenu, 0); rumble(1); }   // v5.36 the stage's own
                }
            } else if (g_fmenu_cat == 0) {
                if (g_fmenu_sel < 4) { f_post(F_MARK, g_fmenu_sel); InterlockedExchange(&g_fmenu, 0); rumble(1); }
                else { InterlockedExchange(&g_fmenu_mtype, g_fmenu_sel - 4); InterlockedExchange(&g_fmenu_level, 2); InterlockedExchange(&g_fmenu_sel, f_marks_of(g_fmenu_sel - 4) ? 1 : 0); }
            } else {
                int k = f_menu_design(g_fmenu_sel);
                if (k >= 0) { f_post(F_SPAWN, g_fd[k].obj); InterlockedExchange(&g_fmenu, 0); rumble(1); }
            }
        }
        return true;
    }
    bool holding = g_hold >= 0;
    if (pressed & BTN_BACK) {                            // View: the Stage Creator menu, or with an object in hand its exact numbers
        InterlockedExchange(&g_fmenu_sel, 0);
        if (holding) {
            memcpy(g_hold_fixpos, g_fo[g_hold].pos, 12);
            InterlockedExchange(&g_hold_fixed, 1);
            InterlockedExchange(&g_fmenu, 3);
        } else InterlockedExchange(&g_fmenu, 2);
        return true;
    }
    if (!holding && (pressed & BTN_UP)) {
        InterlockedExchange(&g_fmenu_level, 0);
        InterlockedExchange(&g_fmenu_sel, 0);
        InterlockedExchange(&g_fmenu, 1);
        return true;
    }
    if (pressed & BTN_A) { f_post(holding ? F_PLACE : F_GRAB); rumble(1); }
    if (pressed & BTN_B) { f_post(holding ? F_PLACE_LOCK : F_LOCK); rumble(1); }
    if (pressed & BTN_X) { f_post(F_DELETE); rumble(2); }
    if (pressed & BTN_Y) { f_post(F_COPY); rumble(1); }
    if (!holding && (pressed & BTN_LB)) { f_post(F_UNDO); rumble(1); }        // LB / RB: undo / redo (lower / raise while holding)
    if (!holding && (pressed & BTN_RB)) { f_post(F_REDO); rumble(1); }
    if (holding && (pressed & (BTN_LEFT | BTN_RIGHT))) {               // one step now, then repeat while held
        f_post(F_TURN, (pressed & BTN_RIGHT) ? 1 : -1);
        g_fturn_down = g_fturn_last = now;
    } else if (holding && (b & (BTN_LEFT | BTN_RIGHT)) && now - g_fturn_down > 350 && now - g_fturn_last > 90) {
        f_post(F_TURN, (b & BTN_RIGHT) ? 1 : -1);
        g_fturn_last = now;
    }
    if (pressed & BTN_RTHUMB) {
        InterlockedExchange(&g_faxis, (g_faxis + 1) % 3);
        set_status(g_faxis == 0 ? "D-pad L/R now turns YAW (spin)" : g_faxis == 1 ? "D-pad L/R now turns PITCH (tip forward / back)"
                                                                     : "D-pad L/R now turns ROLL (tilt left / right)");
    }
    if (pressed & BTN_LTHUMB) f_post(F_ROT_RESET);
    return true;
}

// coordinates editor rows 0-2 = X Y Z (0.5 m, precision 0.1 m), 3-5 = pitch yaw roll (snap step, precision 1 degree);
// Stage Creator menu rows with a value change it with left / right
static void f_menu_turn(int dir) {
    if (g_fmenu == 3) {
        int r = g_fmenu_sel;
        if (r <= 2) g_hold_fixpos[r] += (g_fprecise ? 1.0f : 5.0f) * (float)dir;
        else if (r <= 5) {
            int deg = g_fprecise ? 1 : (F_SNAPS[g_fsnap] ? F_SNAPS[g_fsnap] : 5);
            float &a = g_hold_rot[r == 3 ? 0 : r == 4 ? 1 : 2];
            a += (float)deg * 3.14159265f / 180.0f * (float)dir;
            if (a > 3.14159265f) a -= 6.2831853f;
            if (a < -3.14159265f) a += 6.2831853f;
        }
        return;
    }
    if (g_fmenu == 5) { sc_turn(g_fmenu_sel, dir); return; }            // v2.9 Skill Creator values
    if (g_fmenu == 2 && g_fmenu_sel == 23 && g_sk_count > 0) {          // stage effect skill
        int at = 0;
        for (int i = 0; i < g_sk_count; i++) if (g_sk[i].id == g_fsfx_skill) at = i;
        InterlockedExchange(&g_fsfx_skill, g_sk[((at + dir) % g_sk_count + g_sk_count) % g_sk_count].id);
        return;
    }
    if (g_fmenu == 2 && g_fmenu_sel == 24) { InterlockedExchange(&g_fsfx_size, ((g_fsfx_size + dir) % F_SFX_NSIZES + F_SFX_NSIZES) % F_SFX_NSIZES); return; }
    if (g_fmenu == 2 && g_fmenu_sel == 25) { InterlockedExchange(&g_fsfx_part, ((g_fsfx_part + dir) % 6 + 6) % 6); return; }
    if (g_fmenu == 2 && g_fmenu_sel == 26) { InterlockedExchange(&g_fsfx_speed, ((g_fsfx_speed + dir) % F_SFX_NSPEEDS + F_SFX_NSPEEDS) % F_SFX_NSPEEDS); return; }
    if (g_fmenu == 2 && g_fmenu_sel == 27) {                           // v5.36 hold at: off (-1), then step 0 .. 600
        LONG h = g_fsfx_hold + dir;
        if (h < -1) h = 600; if (h > 600) h = -1;
        InterlockedExchange(&g_fsfx_hold, h);
        return;
    }
    if (g_fmenu == 2 && g_fmenu_sel == 29) {                           // sky: next / previous in the Skies category
        int cat = -1;
        for (int c = 0; c < g_fcat_n; c++) if (!strcmp(g_fcat[c], "Skies")) cat = c;
        int list[64], n = 0, cur = 0;
        for (int k = 0; cat >= 0 && k < g_fd_n && n < 64; k++) if (g_fd[k].cat == cat) { if (g_fd[k].placed) cur = n; list[n++] = g_fd[k].obj; }
        if (n) f_post(F_SKY, list[((cur + dir) % n + n) % n]);
        else set_status("This stage has no Skies category (try the Skill Test Stage)");
        return;
    }
    if (g_fmenu == 2 && g_fmenu_sel == 22) {                           // menu background: see-through grey / black / none
        InterlockedExchange(&g_fbox_alpha, (g_fbox_alpha + 2) % 3);
        return;
    }
    if (g_fmenu == 2 && g_fmenu_sel == 20 && g_sk_count > 0) {          // skill capsule: next / previous skill
        InterlockedExchange(&g_sk_sel, ((g_sk_sel + dir) % g_sk_count + g_sk_count) % g_sk_count);
        return;
    }
    if (g_fmenu == 2 && (g_fmenu_sel == 15 || g_fmenu_sel == 16) && g_sk_count > 0) {   // marker looks: next / previous skill
        int t = g_fmenu_sel - 15, at = 0;
        for (int i = 0; i < g_sk_count; i++) if (g_sk[i].id == g_fmk_skill[t]) at = i;
        at = ((at + dir) % g_sk_count + g_sk_count) % g_sk_count;
        InterlockedExchange(&g_fmk_skill[t], g_sk[at].id);
        InterlockedExchange(&g_fmk_change, 1);
    }
}

// v2.1 hide my character (owner): the player's draw is method 5 of pdNormalPlayerClass (method table 0x14048e3b0 in
// .data, slot 0x14048e3d8 = FUN_14024f2f0, Ghidra full pass). It draws the body only while the "captured by" field
// +0x17174 is -1 (tag-revive pickup); for player 1 in a Forge match the wrapper sets it to -2 for the duration of the
// call and puts it back at once (the draw is the only reader inside that call). No game code changes.
static const ULONG_PTR RVA_PLAYER_DRAW_SLOT = 0x48e3d8, RVA_PLAYER_DRAW = 0x24f2f0, PLAYER_CAPTURED_BY = 0x17174;
typedef void(__fastcall *PFN_PLAYER_DRAW)(void *);
static PFN_PLAYER_DRAW o_player_draw;

static volatile ULONG_PTR g_mr_hide_actor;                    // v5.34 the Mirror's parked double (cwo_mirror.inl)
static volatile ULONG_PTR g_ho_hide_actor;                    // v5.131 the Hologram's parked double (cwo_hologram.inl)
static volatile ULONG_PTR g_el_hide_actor;                    // v5.203.0 the Effect Lab's target with "none (hidden)"
static void __fastcall player_draw(void *self) {
    if (self && ((g_fhide_player && g_forge_active && (ULONG_PTR)self == f_actor(0)) || (g_mr_hide_actor && (ULONG_PTR)self == g_mr_hide_actor) || (g_ho_hide_actor && (ULONG_PTR)self == g_ho_hide_actor) || (g_el_hide_actor && (ULONG_PTR)self == g_el_hide_actor))) {
        int *cap = (int *)((char *)self + PLAYER_CAPTURED_BY);
        int keep = *cap;
        *cap = -2;
        o_player_draw(self);
        *cap = keep;
        return;
    }
    o_player_draw(self);
}

static bool player_draw_install(ULONG_PTR slot_addr, ULONG_PTR expect) {
    void **slot = (void **)slot_addr;
    if ((ULONG_PTR)*slot != expect) {
        logf("hide-player refused: draw slot holds %p, expected %p", *slot, (void *)expect);
        return false;
    }
    DWORD old;
    if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old)) return false;
    o_player_draw = (PFN_PLAYER_DRAW)*slot;
    *slot = (void *)player_draw;
    VirtualProtect(slot, sizeof(void *), old, &old);
    return true;
}

// ---- drawing (game thread) ----
static void f_box(PFN_TEXT text, DWORD *rgba, int x0, int y0, int w, int h) {
    // v3.0.1 (owner tests 12 / 25: "still fully black" at every setting): the game's debug font ignores the colour's
    // alpha (its own text shadows use colour 0 and still show black, e.g. FUN_140153a10), so a box can only be left
    // out (off: the text gets a black outline), drawn as thin bands with gaps (stripes), or drawn solid
    // v5.36 (owner): the game's own window backing instead, a
    // rectangle drawn at once with its alpha (game_box, cwo_loader.cpp) - the black glyph box stays as a choice
    if (g_fbox_alpha == 0) return;
    if (g_fbox_alpha == 2) { game_box(x0, y0, w, h, BOX_GREY); return; }
    set_rgba(rgba, 0xFF000000);
    for (int by = y0; by < y0 + h; by += 24)
        for (int bx = x0; bx < x0 + w; bx += 24)
            text(bx - 4, by - 4, "\x81\xa1");
}

static const char *f_stage_title(int stage) {
    static char t[48];
    sprintf_s(t, "%s", g_fstem[0] ? g_fstem : "stage");
    __try {
        int n = *(int *)(g_base + RVA_STAGE_LIST_N);
        for (int i = 0; i < n && i < 100; i++) {
            char *row = (char *)(g_base + RVA_STAGE_LIST + 0x28 * i);
            if (*(int *)(row + 0x18) != stage) continue;
            const char *nm = *(const char **)row;
            if (nm && nm[0] && (unsigned char)nm[0] < 0x80) { strncpy_s(t, sizeof(t), nm, _TRUNCATE); break; }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return t;
}

static bool in_match_menu_open() {                          // v5.9 a loader menu or Edgar's Dream page over the match
    return g_in_match && (g_ed_live || g_fmenu || g_ed_open);
}
static void forge_draw() {
    LONG state = g_forge_sp_state;
    char st[96] = {0};
    bool status = false;
    if (TryEnterCriticalSection(&g_ui_lock)) {
        if (GetTickCount64() < g_status_until) { strcpy_s(st, sizeof(st), g_status); status = true; }
        LeaveCriticalSection(&g_ui_lock);
    }
    // v2.1: with the game HUD hidden the Forge HUD has no box; every line gets a black outline instead (owner)
    if (g_forge_active && g_game_menu) return;               // the game's pause menu: leave the screen to it
    // v5.36 (owner): the text is stroked whenever it is not on the solid
    // black box (see-through grey box, no box, the HUD hidden)
    bool outline = g_forge_active && (g_hide_hud || g_fbox_alpha != 1);
    // v4.1.1 (owner):
    // the School / Character test menu and the live creator page have no box, only shadowed text
    bool see_through = g_ed_live || g_fmenu == 6 || g_fmenu == 7 || (g_fmenu == 5 && g_ed_mode == 1);   // v4.2 the Skill Creator too
    if (see_through) outline = true;
    if (g_forge_active && in_match_menu_open()) g_menu_hud_until = GetTickCount64() + 250;   // v5.9 the HUD steps aside
    int glyphs = *(int *)(g_base + RVA_GLYPHS), cap = *(int *)(g_base + RVA_GLYPH_CAP);
    if (glyphs > cap - (outline ? 760 : 640)) return;
    if (*(int *)(g_base + RVA_OFS_X) != 0 || *(int *)(g_base + RVA_OFS_Y) != 0) return;
    int *space = (int *)(g_base + RVA_FONT_SPACE), *gap = (int *)(g_base + RVA_FONT_GAP);
    int *fh = (int *)(g_base + RVA_FONT_H), *fw = (int *)(g_base + RVA_FONT_W), *x0 = (int *)(g_base + RVA_FONT_X0);
    DWORD *rgba = (DWORD *)(g_base + RVA_FONT_RGBA);
    int s_space = *space, s_gap = *gap, s_h = *fh, s_w = *fw, s_x0 = *x0;
    DWORD s_rgba[4];
    memcpy(s_rgba, rgba, sizeof(s_rgba));
    PFN_TEXT text = (PFN_TEXT)(g_base + RVA_TEXT);
    const DWORD GOLD = 0xFFFFD040, WHITE = 0xFFFFFFFF, BLUE = 0xFFA0E0FF, GREEN = 0xFF80FF80, GREY = 0xFFB0B0B0;
    int LINE = 16;
    char line[128];
    auto font_box = [&] { *space = 0; *gap = 0; *fh = 32; *fw = 16; };
    auto font_text = [&] { *space = 0; *gap = 2; *fh = 14; *fw = 7; };
    // v5.36 the stroke: 4 black copies (left, right, up, down) when the game's text budget (cap, 1000 glyphs a frame) has
    // room for them, else 2 (the diagonals), else the 1-pixel shadow of v3.3; the room is judged from last frame's count of
    // this screen's letters, so a screen keeps its look from frame to frame
    static int s_letters;
    int last_letters = s_letters, room = cap - glyphs - 40;
    int stroke = last_letters > 0 && 5 * last_letters <= room ? 4 : last_letters > 0 && 3 * last_letters <= room ? 2 : 1;
    s_letters = 0;
    auto put = [&](int px, int py, const char *str, DWORD col) {
        s_letters += (int)strlen(str);
        if (outline) {
            static const int D4[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}}, D2[2][2] = {{1, 1}, {-1, -1}}, D1[1][2] = {{1, 1}};
            const int (*d)[2] = stroke == 4 ? D4 : stroke == 2 ? D2 : D1;
            set_rgba(rgba, 0xFF000000);
            for (int i = 0; i < stroke; i++) text(px + d[i][0], py + d[i][1], str);
        }
        set_rgba(rgba, col);
        text(px, py, str);
    };
    if (!g_forge_active && g_ed_open && state == 0) {          // v3.3 the Edgar's Dream chooser
        // v3.3.1 (owner): a solid
        // cover over the game's stage list whatever the menu-background setting (the list is drawn before the loader)
        // v5.9 (owner): the whole screen, so
        // nothing of the game's lists shows beside a long page
        // v5.36 (owner): the game's
        // list letters are dropped from the text queue instead (the same debug font, queued before this call), and the
        // cover is the game's see-through rectangle over the whole screen (x 0, width 640 = its full-width case)
        *(int *)(g_base + RVA_GLYPHS) = 0;
        *(void **)(g_base + RVA_GLYPH_PTR) = *(void **)(g_base + RVA_GLYPH_BUF);
        game_box(0, 0, 640, 480, BOX_GREY);
        outline = true;
        font_text();
        auto box = [&](int px, int py, DWORD col, int w, int h) {   // v3.5.1 a coloured square (School colour slider)
            *space = 0; *gap = 0; *fh = h; *fw = w;
            set_rgba(rgba, col);
            text(px, py, "\x81\xa1");
            font_text();
        };
        if (!g_ed_auto_choose && !g_cs_hold_cover) ed_draw(put, box, 120, 76, status, st);   // v3.4 the chooser and every creator page
    } else if (!g_forge_active && (g_cs_hold_cover || (g_ed_auto && !(g_ed_auto == 13 && state == 2)))) {
        // v5.32 (owner): black while the loader presses through Single Play's lists (CREATE A STAGE, Create a Skill, Try it now);
        // only the arsenal list Create a Skill lets you choose from shows
        *space = 0; *gap = 0; *fh = 240; *fw = 120;
        set_rgba(rgba, 0xFF000000);
        for (int by = -30; by < 480; by += 160)
            for (int bx = -30; bx < 640; bx += 160)
                text(bx, by, "\x81\xa1");
        font_text();
    } else if (!g_forge_active) {
        // banners over the game's own Single Play lists
        const char *a = NULL, *b = NULL;
        if (state == 0) { a = "STAGE CREATOR - choose the stage to build on (A)"; b = "Your edits are kept per stage.  B: back to the title"; }
        char c3[96] = "";
        if (state == 0 && g_mx_count > 0) {
            LONG cur = g_mx_cur;
            sprintf_s(c3, "LB/RB: Map Slot 7 = %.30s  (then A)", cur >= 0 && cur < g_mx_count ? g_mx[cur].name : "pack list off");
        }
        else if (state == 1) { a = "STAGE CREATOR - loading the stage"; b = ""; }
        else if (state == 2) { a = "STAGE CREATOR - pick any deck (A)"; b = "You fly and build; the deck is not used"; }
        else if (state == 3) { a = "STAGE CREATOR - choose 'debug' (top row) to build alone (A)"; b = "Other rows add CPU players (frozen in place)"; }
        // v5.9 (owner): while the loader presses
        // through the game's lists for an Edgar's Dream test, no banner sits on them; where you choose, it says what for
        LONG au = g_ed_auto;
        if (au && !(au == 13 && state == 2)) { a = NULL; c3[0] = 0; }
        else if (g_ed_mode == 1 && state == 2) { a = "CREATE A SKILL - pick the arsenal you test with (A)"; b = "The Skill Creator opens in the match"; c3[0] = 0; }
        else if (g_ed_mode == 2 && state == 2) { a = "TEST - pick any arsenal (A)"; b = "Your school / character is on in the match"; c3[0] = 0; }
        if (a) {
            font_box();
            f_box(text, rgba, 100, 4, 440, (c3[0] ? 3 : 2) * LINE + 12);
            font_text();
            put(108, 10, a, GOLD);
            put(108, 10 + LINE, b, BLUE);
            if (c3[0]) put(108, 10 + 2 * LINE, c3, GREEN);
        }
    } else if (g_fmenu) {
        int rows = f_menu_rows();
        bool sc_small = see_through && g_fmenu == 5;             // v4.2: the Skill Creator in smaller text on the left, every row
        int win = sc_small ? 10 : g_fmenu == 5 ? 6 : 8;       // v3.3: 6 rows + the card; v4.6: 10 (the game draws <= 1000 letters a frame)
        int top = g_fmenu_sel >= win ? g_fmenu_sel - (win - 1) : 0;
        int shown = rows - top < win ? rows - top : win;
        if (!see_through) {
            font_box();
            f_box(text, rgba, 110, 60, 420, (shown + (g_fmenu == 5 ? 9 : 4)) * LINE + 12);
        }
        font_text();
        if (sc_small) { *space = 0; *gap = 1; *fh = 11; *fw = 5; LINE = 13; }
        int x = see_through ? 12 : 120, y = see_through ? (sc_small ? 24 : 40) : 66;   // v4.1.1: the test menu on the left, the fighter stays in view
        const char *desc = "";
        if (g_fmenu == 6) put(x, y, g_ed_try_page == EDP_SCHOOL ? "SCHOOL CREATOR PAUSE       (Start: close)" : "CHARACTER CREATOR PAUSE    (Start: close)", GOLD);
        else if (g_fmenu == 7) put(x, y, "SKILL CREATOR PAUSE        (Start: close)", GOLD);
        else if (g_fmenu == 5 && g_ed_mode == 1) put(x, y, "SKILL CREATOR   (B: back   R3: preview   X undo   Y redo)", GOLD);   // v5.188.49 the test keys moved to Keep testing
        else if (g_fmenu == 4) put(x, y, g_ftesting ? "STAGE CREATOR PAUSE - testing     (Start: close)" : "STAGE CREATOR PAUSE        (Start: close)", GOLD);
        else if (g_fmenu == 3) put(x, y, "EXACT POSITION AND ANGLE   (B: cancel)", GOLD);
        else if (g_fmenu == 2) put(x, y, "STAGE CREATOR MENU         (B: close)", GOLD);
        else if (g_fmenu == 5) put(x, y, "SKILL CREATOR              (B: back)", GOLD);
        else if (g_fmenu_level == 0) put(x, y, "OBJECT MENU                (B: close)", GOLD);
        else if (g_fmenu_level == 2) { sprintf_s(line, "PLACED %.20sS   (B: back)", F_MARK_NAMES[g_fmenu_mtype]); _strupr_s(line); put(x, y, line, GOLD); }
        else { sprintf_s(line, "%.24s   (B: back)", g_fcat[g_fmenu_cat]); put(x, y, line, GOLD); }
        y += LINE + 4;
        for (int r = top; r < top + shown; r++) {
            const char *cur = r == g_fmenu_sel ? ">" : " ";
            bool sel = r == g_fmenu_sel;
            if (g_fmenu == 5) {                                       // v2.9 Skill Creator
                char val[48];
                const char *dsc = "";
                sc_row(r, val, sizeof(val), &dsc);
                sprintf_s(line, "%s %-20s %s", cur, sc_name(r), val);
                if (sel) desc = dsc;
            } else if (g_fmenu == 7) {                                // v4.2 Skill Creator pause
                static const char *N7[7] = {"Edit the skill (changes show at once)", "Keep testing", "Save and keep testing",
                                            "Save and back to Edgar's Dream", "Back to Edgar's Dream without saving",
                                            "Exit to the main menu without saving", "The game's pause menu"};
                static const char *D7[7] = {"The Skill Creator over the match: L/R changes a value, B plays", "Close this menu",
                                            "Keep this skill (CwoNewSkills.txt)", "Keep it, then Edgar's Dream again",
                                            "Undo every change made in this test, then Edgar's Dream", "Undo every change made in this test",
                                            "Options, Give Up"};
                sprintf_s(line, "%s %s", cur, N7[r]);
                if (sel) desc = D7[r];
            } else if (g_fmenu == 6) {
                static const char *N6[6] = {"Keep testing", "Edit it (changes show at once)", "Save and keep testing",
                                            "Save and back to Edgar's Dream", "Back to Edgar's Dream without saving", "Exit to the main menu without saving"};
                static const char *D6[6] = {"Close this menu", "The creator page over the match: L/R changes a value, B back here",
                                            "Keep these values", "Keep these values, then the creator page again",
                                            "Undo the changes made in this test, then the creator page", "Undo the changes made in this test"};
                sprintf_s(line, "%s %s", cur, N6[r]);
                if (sel) desc = D6[r];
            } else if (g_fmenu == 4) {
                const char *nm = F_PAUSE_NAMES[r];
                if (r == 0 && g_ftesting) nm = "Back to building";
                sprintf_s(line, "%s %s", cur, nm);
                if (sel) desc = F_PAUSE_DESC[r];
            } else if (g_fmenu == 3) {
                if (r <= 2) sprintf_s(line, "%s %-14s %8.1f m", cur, F_EDIT_NAMES[r], g_hold_fixpos[r] / 10);
                else if (r <= 5) sprintf_s(line, "%s %-14s %8.0f deg", cur, F_EDIT_NAMES[r], g_hold_rot[r == 3 ? 0 : r == 4 ? 1 : 2] * 57.2958f);
                else sprintf_s(line, "%s %s", cur, F_EDIT_NAMES[r]);
                if (sel) desc = r <= 2 ? (g_fprecise ? "L/R: 0.1 m steps (hold to repeat)" : "L/R: 0.5 m steps (precision moves: 0.1 m)")
                              : r <= 5 ? "L/R: turn by the snap angle (precision moves: 1 degree)" : r == 6 ? "A: put it down exactly here" : "A: back to holding it";
            } else if (g_fmenu == 2) {
                char val[64] = "";                                  // v3.0.1: part names are up to 27 characters (crash at 24)
                if (r == 3) sprintf_s(val, "%d step%s", g_hcur, g_hcur == 1 ? "" : "s");
                if (r == 4) sprintf_s(val, "%d step%s", g_hn - 1 - g_hcur, g_hn - 1 - g_hcur == 1 ? "" : "s");
                if (r == 5) { if (F_SNAPS[g_fsnap]) sprintf_s(val, "%d deg", F_SNAPS[g_fsnap]); else strcpy_s(val, "free"); }
                if (r == 6) strcpy_s(val, g_fprecise ? "on" : "off");
                if (r == 7) strcpy_s(val, g_fly_landed ? "off (walk)" : "on");
                if (r == 8) strcpy_s(val, g_fplay ? "on" : "off");
                if (r == 9) strcpy_s(val, g_ffreeze ? "on" : "off");
                if (r == 10) strcpy_s(val, g_hide_hud ? "on" : "off");
                if (r == 11) strcpy_s(val, g_fhide_player ? (o_player_draw ? "on" : "not available") : "off");
                if (r == 12) strcpy_s(val, g_fbob ? "on" : "off");
                if (r == 13) strcpy_s(val, g_fgod ? "on" : "off");
                if (r == 14) strcpy_s(val, g_fmagnet ? "on" : "off");
                if (r == 15 || r == 16) {
                    const char *nm = "?";
                    for (int i = 0; i < g_sk_count; i++) if (g_sk[i].id == g_fmk_skill[r - 15]) nm = g_sk[i].name;
                    sprintf_s(val, "%.20s", nm);
                }
                if (r == 17) strcpy_s(val, g_fmk_fx ? "on" : "off");
                if (r == 18) strcpy_s(val, g_fcaps ? "on" : "off");
                if (r == 19) strcpy_s(val, g_fcol ? "on" : "off");
                if (r == 20 && g_sk_count > 0) sprintf_s(val, "%.22s", g_sk[g_sk_sel].name);
                if (r == 21) sprintf_s(val, "%s (%d made)", g_fcopies ? "on" : "off", g_fmade);
                if (r == 22) strcpy_s(val, g_fbox_alpha == 2 ? "see-through gray" : g_fbox_alpha == 1 ? "black box" : "none (stroked text)");
                if (r == 23) { const char *nm = "?"; for (int i = 0; i < g_sk_count; i++) if (g_sk[i].id == g_fsfx_skill) nm = g_sk[i].name; sprintf_s(val, "%.20s", nm); }
                if (r == 24) sprintf_s(val, "x%.2g", F_SFX_SIZES[g_fsfx_size]);
                if (r == 25) sprintf_s(val, "%s", F_SFX_PART[g_fsfx_part]);
                if (r == 26) { float sp = F_SFX_SPEEDS[g_fsfx_speed]; if (sp == 0.0f) strcpy_s(val, "frozen"); else sprintf_s(val, "x%.2g", sp); }
                if (r == 27) { if (g_fsfx_hold < 0) strcpy_s(val, "off (it plays)"); else sprintf_s(val, "step %ld", g_fsfx_hold); }   // v5.36
                if (r == 29) {
                    strcpy_s(val, "-");
                    for (int k = 0; k < g_fd_n; k++)
                        if (g_fd[k].placed && g_fd[k].cat >= 0 && g_fd[k].cat < g_fcat_n && !strcmp(g_fcat[g_fd[k].cat], "Skies") && g_fd[k].disp)
                            sprintf_s(val, "%.20s", g_fd[k].disp + (strncmp(g_fd[k].disp, "Sky: ", 5) ? 0 : 5));
                }
                sprintf_s(line, "%s %-28s %s", cur, F_MENU_NAMES[r], val);
                if (sel) desc = F_MENU_DESC[r];
            } else if (g_fmenu_level == 0 && r >= g_fcat_n) {
                sprintf_s(line, "%s Remove ALL placed objects", cur);
                if (sel) desc = "Placed objects back to storage (locked stay; LB undoes)";
            } else if (g_fmenu_level == 0) {
                int n = 0;
                if (r == 0) n = g_mk_n;
                else for (int k = 0; k < g_fd_n; k++) n += g_fd[k].cat == r;
                sprintf_s(line, "%s %-28.28s %3d", cur, g_fcat[r], n);
                if (sel) desc = r == 0 ? "Spawn points, capsule spots and kill zones" : "A: open this category";
            } else if (g_fmenu_level == 2) {
                if (r == 0) { sprintf_s(line, "%s Remove all %ss", cur, F_MARK_NAMES[g_fmenu_mtype]); if (sel) desc = "Removes every one on this stage (LB undoes)"; }
                else {
                    int i = f_mark_row(g_fmenu_mtype, r);
                    ULONG_PTR a = f_actor(0);
                    float d = 0;
                    if (i >= 0 && a) {
                        const float *pp = (const float *)(a + ACTOR_POS);
                        d = sqrtf((g_mk[i].p[0] - pp[0]) * (g_mk[i].p[0] - pp[0]) + (g_mk[i].p[2] - pp[2]) * (g_mk[i].p[2] - pp[2])) / 10;
                    }
                    if (i >= 0) {
                        sprintf_s(line, "%s %s %d   %.1f m away   (%.0f, %.0f, %.0f)", cur, F_MARK_NAMES[g_fmenu_mtype], r, d,
                                  g_mk[i].p[0] / 10, g_mk[i].p[1] / 10, g_mk[i].p[2] / 10);
                        if (sel) desc = "A: fly there     X: remove it";
                    } else if (g_fmenu_mtype == 0) {         // v5.36 the stage's own start points after the placed ones
                        float sp[8][3];
                        int k = r - 1 - f_marks_of(0), n = f_stage_spawns(sp, 8);
                        if (k >= 0 && k < n) {
                            if (a) { const float *pp = (const float *)(a + ACTOR_POS); d = sqrtf((sp[k][0] - pp[0]) * (sp[k][0] - pp[0]) + (sp[k][2] - pp[2]) * (sp[k][2] - pp[2])) / 10; }
                            sprintf_s(line, "%s Stage's own start %d   %.1f m away   (%.0f, %.0f, %.0f)", cur, k + 1, d, sp[k][0] / 10, sp[k][1] / 10, sp[k][2] / 10);
                        } else line[0] = 0;
                        if (sel) desc = "A: fly there (your 4 Spawn points replace the stage's)";
                    } else line[0] = 0;
                }
            } else if (g_fmenu_cat == 0) {
                if (r < 4) { sprintf_s(line, "%s Place a %s", cur, F_MARK_NAMES[r]); if (sel) desc = F_MARK_DESC[r]; }
                else { sprintf_s(line, "%s %ss placed: %d", cur, F_MARK_NAMES[r - 4], f_marks_of(r - 4)); if (sel) desc = "A: see them, fly to them, remove them"; }
            } else {
                int k = f_menu_design(r);
                int inst = -1;
                for (int i = 0; k >= 0 && i < g_fo_n && inst < 0; i++) if (g_fo[i].obj == g_fd[k].obj) inst = i;
                if (k >= 0) {
                    const char *v = g_fiso_obj == g_fd[k].obj ? (g_fiso_mode == 1 ? " only" : g_fiso_mode == 2 ? " hidden" : "") : "";
                    sprintf_s(line, "%s %-26.26s %3d/%-3d%s", cur, inst >= 0 ? f_name(inst) : "?", g_fd[k].placed, g_fd[k].total, v);
                } else line[0] = 0;
                if (sel) desc = "A take one  R/L go to one  X remove all  Y show only";
            }
            put(x, y, line, sel ? GOLD : WHITE);
            y += LINE;
        }
        put(x, y + 2, desc, GREY);
        if (sc_small) {                                       // v4.6: the card in two lines, the status under it
            char card[5][96];
            int nc = sc_card_want(420, 150) ? 0 : sc_card(card); // v5.8 the game's own card on the right when it shows
            for (int i = 0; i < nc && i < 2; i++) put(x, y + 2 + (1 + i) * LINE, card[i], i == 0 ? GOLD : WHITE);
            if (status) put(x, y + 2 + 3 * LINE, st, GREEN);
            int sid = sc_id(g_fmenu_sel);
            if ((sid >= 9 && sid <= 11) || sid == 36) {
                int sp = g_sc.sat == 1 ? 0 : g_sc.sat ? g_sc.sat : 100, vp = g_sc.val ? g_sc.val : 100, by = 452, bx = x;
                int s_sp = *space, s_gp = *gap, s_fh = *fh, s_fw = *fw;
                *space = 0; *gap = 0; *fh = 10; *fw = 8;
                for (int i = 0; i < 36; i++) { set_rgba(rgba, sc_tint(0xFFFF0000u, i * 10, sp, vp)); text(bx + 8 * i, by, "\x81\xa1"); }
                *fh = 22; *fw = 18;
                set_rgba(rgba, sc_tint(0xFFFF0000u, g_sc.hue, sp, vp)); text(bx + 300, by - 8, "\x81\xa1");
                *space = s_sp; *gap = s_gp; *fh = s_fh; *fw = s_fw;
                put(bx + 8 * (g_sc.hue / 10), by + 11, "^", WHITE);
            }
        } else {
        put(x, y + 2 + LINE, g_fmenu == 2 ? "D-pad choose   A change / do   L/R values" : g_fmenu == 3 ? "D-pad Up/Down choose   L/R change" :
                                g_fmenu == 4 || g_fmenu == 7 ? "D-pad choose   A select" : g_fmenu == 5 ? "D-pad choose   L/R change   A do   X undo   Y redo" :
                                "D-pad choose  LB/RB page  A select", BLUE);
        if (status) put(x, y + 2 + 2 * LINE, st, GREEN);
        }
        if (g_fmenu == 5 && !sc_small) {                          // v3.0 the skill card (owner)
            char card[5][96];
            int n = sc_card_want(x, y + 2 + 3 * LINE) ? 0 : sc_card(card);   // v5.8 the game's own card there when it shows
            for (int i = 0; i < n; i++) put(x, y + 2 + (3 + i) * LINE, card[i], i == 0 ? GOLD : WHITE);
            int sid = sc_id(g_fmenu_sel);
            if ((sid >= 9 && sid <= 11) || sid == 36) {                          // v4.4 (owner): the colour bar while a colour row is chosen
                int sp = g_sc.sat == 1 ? 0 : g_sc.sat ? g_sc.sat : 100, vp = g_sc.val ? g_sc.val : 100, by = 452, bx = x;
                int s_sp = *space, s_gp = *gap, s_fh = *fh, s_fw = *fw;
                *space = 0; *gap = 0; *fh = 10; *fw = 8;
                for (int i = 0; i < 36; i++) { set_rgba(rgba, sc_tint(0xFFFF0000u, i * 10, sp, vp)); text(bx + 8 * i, by, "\x81\xa1"); }
                *fh = 22; *fw = 18;
                set_rgba(rgba, sc_tint(0xFFFF0000u, g_sc.hue, sp, vp)); text(bx + 300, by - 8, "\x81\xa1");
                *space = s_sp; *gap = s_gp; *fh = s_fh; *fw = s_fw;
                put(bx + 8 * (g_sc.hue / 10), by + 11, "^", WHITE);
            }
        }
    } else if (g_ed_live) {                                   // v3.9 the creator page over the test match
        // v4.1.1: no box; smaller shadowed text on the left so the fighter and the effect stay in view
        *space = 0; *gap = 1; *fh = 11; *fw = 5;
        auto box = [&](int px, int py, DWORD col, int w, int h) {
            *space = 0; *gap = 0; *fh = h; *fw = w;
            set_rgba(rgba, col);
            text(px, py, "\x81\xa1");
            font_text();
        };
        *space = 0; *gap = 1; *fh = 11; *fw = 5;
        ed_draw(put, box, 6, 30, status, st);
    } else if (g_ftesting && g_ed_mode == 1) {                // v4.2 skill test banner
        font_text();
        sprintf_s(line, "TESTING SKILL: %.28s   (Start: Skill Creator pause)", g_sc.unsaved ? "new (not saved)" : g_sc_k >= 0 ? g_sc.name : sc_skill_name(g_sc.src));
        put(130, 6, line, GOLD);
        put(130, 6 + LINE, SCT_KEYS, WHITE);                   // v5.188.49 the test keys, on screen while testing only
        if (status) put(130, 6 + 2 * LINE, st, GREEN);
        char card[5][96];                                      // v4.4 (owner): the skill card stays on screen while testing
        int n = sc_card_want(12, 335) ? 0 : sc_card(card);       // v5.8 the game's own card there when it shows
        for (int i = 0; i < n; i++) put(12, 372 + i * LINE, card[i], i == 0 ? GOLD : WHITE);
    } else if (g_ftesting && g_ed_mode == 2) {                // v3.9 school / character test banner
        font_text();
        const EdCreator &c = g_edc[g_ed_try_page];
        sprintf_s(line, "TESTING %s: %.20s   (Start: menu   R3: effect again)", g_ed_try_page == EDP_SCHOOL ? "SCHOOL" : "CHARACTER", c.n ? c.item[c.cur].name : "");
        put(130, 6, line, GOLD);
        if (status) put(130, 6 + LINE, st, GREEN);
    } else if (g_ftesting) {
        font_text();
        put(200, 6, "TESTING YOUR MAP   (Start: Stage Creator pause)", GOLD);
        if (status) put(200, 6 + LINE, st, GREEN);
    } else {
        ULONG_PTR actor = f_actor(0);
        int placed = 0, total = 0;
        for (int i = 0; i < g_fo_n; i++) { total += f_array()[i] != NULL; placed += g_fo[i].placed; }
        if (!g_hide_hud) {                                   // v5.36: the box by its setting (f_box), the stroke by 'outline'
            font_box();
            f_box(text, rgba, 60, 2, 520, 8 * LINE + 12);
        }
        font_text();
        int x = 68, y = 8;
        int stage = *(int *)(g_base + RVA_STAGE);
        // the map limit: a Phantom Dust stage holds the objects its file was built with (every copy is loaded with the
        // stage), so "how much this map can hold" is the number of copies, the map's budget
        if (total > 0 && placed >= total) sprintf_s(line, "STAGE CREATOR  %.20s   MAP LIMIT REACHED %d/%d", f_stage_title(stage), placed, total);
        else sprintf_s(line, "STAGE CREATOR  %.20s   limit %d/%d (%d%%)", f_stage_title(stage), placed, total, total ? placed * 100 / total : 0);
        put(x, y, line, GOLD);
        y += LINE;
        const char *ax = g_faxis == 0 ? "yaw" : g_faxis == 1 ? "pitch" : "roll";
        char snap[12];
        if (F_SNAPS[g_fsnap]) sprintf_s(snap, "%d", F_SNAPS[g_fsnap]); else strcpy_s(snap, "free");
        sprintf_s(line, "Aim %.1f %.1f %.1f m  reach %.1f  snap %s  %s", g_faim[0] / 10, g_faim[1] / 10, g_faim[2] / 10,
                  g_hold_dist / 10, snap, ax);
        put(x, y, line, WHITE);
        y += LINE;
        int h = g_hold;
        LONG t = g_ftarget, mk = g_fmark_near;
        if (h >= 0) {
            sprintf_s(line, "Holding %.22s  turn %.0f/%.0f/%.0f", f_name(h), g_hold_rot[1] * 57.2958f, g_hold_rot[0] * 57.2958f,
                      g_hold_rot[2] * 57.2958f);
            put(x, y, line, GOLD);
            y += LINE;
            put(x, y, "A place B lock X delete Y copy LB/RB down/up", BLUE);
            y += LINE;
            sprintf_s(line, "D-pad L/R turns %s (R3: yaw/pitch/roll)  U/D far/near", ax);
            put(x, y, line, BLUE);
            y += LINE;
            LONG mh = g_fmagnet_hit;
            sprintf_s(line, "View: exact position/angle   L3 reset turn%s", mh ? "   MAGNET" : "");
            put(x, y, line, mh ? GREEN : BLUE);
        } else {
            if (t >= 0) sprintf_s(line, "Looking at %.20s%s  A grab X del Y copy B lock", f_name(t), g_fo[t].locked ? " (locked)" : "");
            else if (mk >= 0 && mk < g_mk_n) sprintf_s(line, "At a %.10s marker  (X delete)", g_mk[mk].type);
            else sprintf_s(line, "Nothing here  (D-pad Up: object menu)");
            put(x, y, line, GOLD);
            y += LINE;
            sprintf_s(line, "D-pad Up objects  View menu  Start pause  LB undo  RB redo%s", g_fprecise ? " [slow]" : "");
            put(x, y, line, BLUE);
            y += LINE;
            sprintf_s(line, "Spawns %ld  Capsules %ld  Kill zones %ld  %s", g_counts[0], g_counts[1], g_counts[2],
                      actor ? (g_fly_landed ? "walking" : "flying (RT/LT up/down)") : "");
            put(x, y, line, GREY);
            LONG any = g_fmark_any;
            if (any >= 0 && any < g_mk_n) {                    // compass to the nearest marker
                float r = g_fmark_rel;
                const char *dir = fabsf(r) < 0.6f ? "ahead" : fabsf(r) > 2.5f ? "behind" : "to the side";   // handedness not checked in game yet
                y += LINE;
                sprintf_s(line, "Nearest marker: %.8s %.1f m %s", g_mk[any].type, g_fmark_dist / 10, dir);
                put(x, y, line, GREY);
            }
        }
        if (g_fiso_mode) {
            int k = f_design_index(g_fiso_obj);
            int inst = -1;
            for (int i = 0; k >= 0 && i < g_fo_n && inst < 0; i++) if (g_fo[i].obj == g_fiso_obj) inst = i;
            y += LINE;
            sprintf_s(line, "%s %.26s  (object menu, Y to change)", g_fiso_mode == 1 ? "Showing only" : "Hiding", inst >= 0 ? f_name(inst) : "?");
            put(x, y, line, GREEN);
        }
        y += LINE;
        if (status) put(x, y, st, GREEN);
    }
    memcpy(rgba, s_rgba, sizeof(s_rgba));
    *space = s_space; *gap = s_gap; *fh = s_h; *fw = s_w; *x0 = s_x0;
}

// v3.4 Create a Mode: in an ordinary offline match the mode in use and each fighter's lives, at the top of the screen
// v5.6 (owner 18:30: "their names still don't appear. It only shows four of them ... we need to be able to account for
// their health and everything, like their name tag"): the game's battle HUD keeps 4 panels (CBattle2DMenuClass draws
// yours and up to 3 others from a 4-entry list, FUN_140150c30), so fighters 5 and 6 get panels of the loader's own under
// the game's three, in the same 640 x 480 HUD space: name (names store 0x4bca30, 0x41 each, FUN_1401276e0), team, life
// (+0x4c now / +0x50 most, as FUN_14012c8c0 / the lives mode read them), a life bar, DOWN (+0x24 == 0, as
// FUN_1402516a0 finds a downed teammate) or OUT (no longer in the fighter table, FUN_140129e70).
template <class PUT> static void six_draw_panels(PUT &put) {
    // v5.31 (owner 2026-10-27, screenshot of the LOADING screen with "5 Meister team A 20/20" on it): only once the battle
    // runs - its frame clock 0x1404bc63c (FUN_140129ac0 adds 1 a battle frame, FUN_14012a110 sets 0 at the set-up) has
    // moved past 30 in this match; a smaller value than last time is the next match's set-up
    static int s_last = -1;
    static bool s_run;
    if (!g_six_match) { s_last = -1; s_run = false; return; }
    int clk = 0;
    __try { clk = *(int *)(g_base + 0x4bc63c); } __except (EXCEPTION_EXECUTE_HANDLER) { return; }
    if (clk < s_last) s_run = false;
    if (s_last >= 0 && clk > s_last && clk >= 30) s_run = true;
    s_last = clk;
    if (!s_run) return;
    if (GetTickCount64() - g_sh_drawn < 500) return;           // v5.7 the game's own panels are up (cwo_sixhud.inl)
    // v5.146 (owner 2026-11-10 in the middle: "their name and
    // health shouldn't be displayed in the middle of the screen"): not in split screen (battle type *0x1404bc9d4 == 6)
    __try { if (*(int *)(g_base + 0x4bc9d4) == 6) return; } __except (EXCEPTION_EXECUTE_HANDLER) { return; }
    __try {
        ULONG_PTR *act = cwo_actors();
        const char *names = (const char *)cwo_gaddr(0x4bca30);
        int y = 246;
        for (int i = 4; i < 6; i++, y += 46) {
            ULONG_PTR a = act[i];
            char name[0x42] = "";
            memcpy(name, names + 0x41 * i, 0x41);
            name[0x41] = 0;
            char line[96];
            sprintf_s(line, "%d %.20s", i + 1, name[0] ? name : "?");
            put(38, y, line, 0xFFFFD040);
            if (!a) { put(38, y + 15, "OUT", 0xFFFF6050); continue; }
            int team = *(int *)(a + 0x1c), now = *(int *)(a + 0x4c), most = *(int *)(a + 0x50);
            bool down = *(int *)(a + 0x20) && !*(int *)(a + 0x24);
            if (most <= 0 || most > 999) most = 20;
            if (now < 0) now = 0;
            char bar[24] = "";
            int n = now * 10 / most;
            for (int k = 0; k < 10; k++) bar[k] = k < n ? '|' : '.';
            bar[10] = 0;
            sprintf_s(line, "team %c  %2d/%-2d %s%s", 'A' + (team & 7), now, most, bar, down ? "  DOWN" : "");
            put(38, y + 15, line, down ? 0xFFFF6050 : (team == 2 ? 0xFF70E070 : team & 1 ? 0xFFFF9060 : 0xFF60C0FF));   // v5.188.26 team C green
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static void ed_match_draw() {
    if (!g_in_match || g_forge_session) return;
    if (g_ed_mode_on < 0 && !g_six_match) return;
    int glyphs = *(int *)(g_base + RVA_GLYPHS), cap = *(int *)(g_base + RVA_GLYPH_CAP);
    if (glyphs > cap - 260) return;
    if (*(int *)(g_base + RVA_OFS_X) != 0 || *(int *)(g_base + RVA_OFS_Y) != 0) return;
    int *space = (int *)(g_base + RVA_FONT_SPACE), *gap = (int *)(g_base + RVA_FONT_GAP);
    int *fh = (int *)(g_base + RVA_FONT_H), *fw = (int *)(g_base + RVA_FONT_W), *x0 = (int *)(g_base + RVA_FONT_X0);
    DWORD *rgba = (DWORD *)(g_base + RVA_FONT_RGBA);
    int s_space = *space, s_gap = *gap, s_h = *fh, s_w = *fw, s_x0 = *x0;
    DWORD s_rgba[4];
    memcpy(s_rgba, rgba, sizeof(s_rgba));
    PFN_TEXT text = (PFN_TEXT)(g_base + RVA_TEXT);
    *space = 0; *gap = 2; *fh = 14; *fw = 7;
    auto put = [&](int px, int py, const char *str, DWORD col) {
        set_rgba(rgba, 0xFF000000);
        text(px + 1, py + 1, str);
        set_rgba(rgba, col);
        text(px, py, str);
    };
    // v5.3.6 (owner screenshot 16:26: the mode line and a status message on top of each other): while the panel shows a
    // status line (top box, y 8), the mode line goes one line below the box
    // v5.10.1 (owner 2026-10-26): the mode line (ed_draw_match) is no longer drawn; the mode's rules (lives) still work
    six_draw_panels(put);                                       // v5.6 fighters 5 and 6
    memcpy(rgba, s_rgba, sizeof(s_rgba));
    *space = s_space; *gap = s_gap; *fh = s_h; *fw = s_w; *x0 = s_x0;
}

// v5.1.2 while a 3 vs 3 from Create a Mode is on its way: the Single Play lists say so
static void ed_six_banner() {
    if (!g_six_auto || g_in_match || g_forge_session) return;
    int glyphs = *(int *)(g_base + RVA_GLYPHS), cap = *(int *)(g_base + RVA_GLYPH_CAP);
    if (glyphs > cap - 120) return;
    if (*(int *)(g_base + RVA_OFS_X) != 0 || *(int *)(g_base + RVA_OFS_Y) != 0) return;
    int *space = (int *)(g_base + RVA_FONT_SPACE), *gap = (int *)(g_base + RVA_FONT_GAP);
    int *fh = (int *)(g_base + RVA_FONT_H), *fw = (int *)(g_base + RVA_FONT_W), *x0 = (int *)(g_base + RVA_FONT_X0);
    DWORD *rgba = (DWORD *)(g_base + RVA_FONT_RGBA);
    int s_space = *space, s_gap = *gap, s_h = *fh, s_w = *fw, s_x0 = *x0;
    DWORD s_rgba[4];
    memcpy(s_rgba, rgba, sizeof(s_rgba));
    PFN_TEXT text = (PFN_TEXT)(g_base + RVA_TEXT);
    *space = 0; *gap = 2; *fh = 14; *fw = 7;
    auto put = [&](int px, int py, const char *str, DWORD col) {
        set_rgba(rgba, 0xFF000000);
        text(px + 1, py + 1, str);
        set_rgba(rgba, col);
        text(px, py, str);
    };
    // v5.2.2 (owner: the lists flashed by): a solid cover over Single Play's lists while the loader passes them
    *space = 0; *gap = 0; *fh = 34; *fw = 17;
    set_rgba(rgba, 0xFF000000);
    for (int by = 0; by < 480; by += 30)
        for (int bx = 0; bx < 640; bx += 30) { text(bx - 2, by - 2, "\x81\xa1"); }
    *space = 0; *gap = 2; *fh = 14; *fw = 7;
    put(250, 220, "LOADING  3 VS 3 ...", 0xFF60FF60);
    memcpy(rgba, s_rgba, sizeof(s_rgba));
    *space = s_space; *gap = s_gap; *fh = s_h; *fw = s_w; *x0 = s_x0;
}

#include "cwo_ccprev.inl"                                // v3.7 Create a Character live preview
#include "cwo_outfit.inl"                                // v5.61 Create an Outfit
#include "cwo_design.inl"                                // v5.66 designer outfits
#include "cwo_outfits15.inl"                             // v5.63 15 outfits built on the player's machine
#include "cwo_bodyshape.inl"                             // v5.180 body shape (CwoBodyShape.txt)

// ---- offline self-test on stand-ins (no game): returns 1 or a negative step number ----
struct FtObj { void *vt; unsigned char pad[OBJ_POS - 8]; float pos[3]; int rot[3]; };
static int g_ft_sets;
static void __fastcall ft_setpos(void *o, const float *p) { memcpy(((FtObj *)o)->pos, p, 12); g_ft_sets++; }
static void __fastcall ft_setrot(void *o, const float *r) { for (int k = 0; k < 3; k++) ((FtObj *)o)->rot[k] = (int)(r[k] * ANGLE_UNITS); }

static int forge_self_test(unsigned char *actor) {
    // native slot: empty -> installed, filled -> refused
    static void *slot[2] = {NULL, (void *)1};
    if (!forge_native_install((ULONG_PTR)&slot[0]) || slot[0] != (void *)(PFN_NATIVE)forge_native) return -16;
    if (forge_native_install((ULONG_PTR)&slot[1])) return -16;
    // session: native(1) -> Single Play state 4 + player in a match -> editor on; leaving the mode ends the session
    static unsigned char mode[0x100], spobj[0x40];
    *(int *)(mode + 0x7C) = MODE_SINGLE_PLAY;
    *(ULONG_PTR *)(mode + 0x60) = (ULONG_PTR)spobj;
    *(int *)spobj = 0;
    g_ft_mode = (ULONG_PTR)mode;
    INT64 one = 1;
    if (((PFN_NATIVE)slot[0])(&one) != 2 || !g_forge_session) return -17;
    forge_poll(true);
    bool banner = g_forge_sp_state == 0 && !g_forge_active;
    *(int *)spobj = SP_MATCH;
    forge_poll(true);
    bool on = g_forge_active != 0;
    // objects: designs 7 (placed + stored copy), 8 (placed), 9 (stored); storage line y < -100
    static void *vt[64];
    vt[VT_SET_POSITION / 8] = (void *)ft_setpos;
    vt[VT_SET_ROTATION / 8] = (void *)ft_setrot;
    static FtObj objs[4];
    static void *arr[4];
    static unsigned char rows[4 * 0x24];
    const int obj_of[4] = {7, 7, 8, 9};
    const float y_of[4] = {200, -150, 200, -150};
    for (int i = 0; i < 4; i++) {
        memset(&objs[i], 0, sizeof(objs[i]));
        objs[i].vt = vt;
        objs[i].pos[0] = 10.0f * i; objs[i].pos[1] = y_of[i]; objs[i].pos[2] = 0.0f;
        arr[i] = &objs[i];
        *(unsigned short *)(rows + 0x24 * i) = (unsigned short)obj_of[i];
    }
    g_ft_inst = arr; g_ft_n = 4; g_ft_rows = rows;
    LONG s_bob = g_fbob;
    g_fbob = 0;                                            // the highlight would bob the stand-in target
    g_fo_n = -1; g_fo_array = NULL;
    float *ap = (float *)(actor + ACTOR_POS);
    ap[0] = 0; ap[1] = 200; ap[2] = 0;
    *(int *)(actor + ACTOR_CAMERA) = 0;                    // camera looks along +z
    g_ft_storage = -100.0f;                                // the stage's storage line (Map\<stage>.forge on real stages)
    forge_frame();                                         // capture
    bool counts = g_fd_n == 3 && g_fd[0].obj == 7 && g_fd[0].total == 2 && g_fd[0].placed == 1 && g_fd[2].placed == 0;
    // spawn design 9 -> instance 3 comes out of storage, held 5 m ahead
    f_post(F_SPAWN, 9);
    forge_frame();
    bool spawned = g_hold == 3 && g_fo[3].placed && fabsf(objs[3].pos[2] - F_DIST_DEF) < 0.5f && fabsf(objs[3].pos[1] - 200.0f) < 0.5f;
    // turn 15 degrees (snap index 3), place
    InterlockedExchange(&g_fsnap, 3);
    InterlockedExchange(&g_faxis, 0);
    f_post(F_TURN, 1);
    forge_frame();
    f_post(F_PLACE);
    forge_frame();
    bool turned = g_hold < 0 && abs(objs[3].rot[1] - (int)(0.261799f * ANGLE_UNITS)) <= 2;
    // copy of design 7 (target = instance 0? aim is at z 50: nearest placed = 3; use instance 0 via grab-free copy)
    g_fo[3].locked = 0;
    InterlockedExchange(&g_ftarget, 0);
    FReq cp = {F_COPY, 0};
    f_do(cp, 99, (ULONG_PTR)actor);
    bool copied = g_hold == 1 && g_fo[1].placed && g_fd[0].placed == 2;
    f_post(F_PLACE);
    forge_frame();
    FReq cp2 = {F_COPY, 0};
    InterlockedExchange(&g_ftarget, 0);
    f_do(cp2, 99, (ULONG_PTR)actor);
    bool limit = g_hold < 0;                                // no spare copy left
    // delete instance 1 -> back to its storage spot
    g_hold = 1;
    FReq del = {F_DELETE, 0};
    f_do(del, 99, (ULONG_PTR)actor);
    bool deleted = !g_fo[1].placed && fabsf(objs[1].pos[1] + 150.0f) < 0.01f && g_hold < 0;
    // v2.1 undo / redo: undo the delete -> instance 1 placed again at the copy spot; redo -> back in storage
    FReq un = {F_UNDO, 0}, re = {F_REDO, 0};
    f_do(un, 99, (ULONG_PTR)actor);
    bool undone = g_fo[1].placed && fabsf(objs[1].pos[1] - 200.0f) < 0.01f;
    f_do(re, 99, (ULONG_PTR)actor);
    bool redone = !g_fo[1].placed && fabsf(objs[1].pos[1] + 150.0f) < 0.01f;
    // remove all placed copies of design 7 (instance 0), then undo
    FReq clr = {F_CLEAR_DESIGN, 7};
    f_do(clr, 99, (ULONG_PTR)actor);
    bool cleared = !g_fo[0].placed;
    f_do(un, 99, (ULONG_PTR)actor);
    cleared = cleared && g_fo[0].placed && fabsf(objs[0].pos[1] - 200.0f) < 0.01f;
    // show only design 8: instances 0 and 3 (placed, other designs) go out of sight, 2 stays; then show everything
    FReq iso = {F_ISO, 8};
    f_do(iso, 99, (ULONG_PTR)actor);
    bool isolated = g_fhidden[0] && g_fhidden[3] && !g_fhidden[2] && objs[0].pos[1] < 0 && g_fo[0].pos[1] == 200.0f;
    f_do(iso, 99, (ULONG_PTR)actor);
    isolated = isolated && !g_fhidden[0] && g_fhidden[2] && !g_fhidden[3];
    f_do(iso, 99, (ULONG_PTR)actor);
    isolated = isolated && !g_fhidden[0] && !g_fhidden[2] && !g_fhidden[3] && objs[0].pos[1] == 200.0f && objs[2].pos[1] == 200.0f;
    deleted = deleted && undone && redone && cleared && isolated;
    logf("self-test: forge undo %s, redo %s, remove all + undo %s, show only / hide / all %s", undone ? "ok" : "FAILED",
         redone ? "ok" : "FAILED", cleared ? "ok" : "FAILED", isolated ? "ok" : "FAILED");
    // save, change, load back
    wchar_t old_dir[MAX_PATH];
    wcscpy_s(old_dir, g_dir);
    bool saved = f_save(99);
    float keep = objs[3].pos[0];
    float far_p[3] = {500, 200, 500}, zero[3] = {0, 0, 0};
    f_set(3, far_p, zero);
    bool loaded = f_load(99, true) == 1 && fabsf(objs[3].pos[0] - keep) < 0.01f;
    // a saved map for other objects is refused
    g_fo[2].obj = 5;
    bool refused = f_load(99, false) == -1;
    g_fo[2].obj = 8;
    wchar_t vpath[MAX_PATH];
    f_variant_path(vpath, L".fvar");
    DeleteFileW(vpath);
    f_variant_path(vpath, L".fvar.bak");
    DeleteFileW(vpath);
    // the canvas descriptor: storage line, categories, exact names with display names, name parts
    wchar_t dpath[MAX_PATH];
    swprintf_s(dpath, MAX_PATH, L"%sforge_selftest.forge", g_dir);
    FILE *df = NULL;
    bool desc = false;
    if (cwo_wfopen(&df, dpath, L"w") == 0 && df) {
        fprintf(df, "# test\nstorage_below 185.0\ncategory Structure Highway\nobject st02_kabeA_|Highway: Wall A\n"
                    "category Lifts\nmatch _jump\n");
        fclose(df);
        g_ft_desc = dpath;
        f_load_descriptor();
        g_ft_desc = NULL;
        int a = f_match_of("ST02_KABEA_"), b2 = f_match_of("st05_jump_"), c = f_match_of("st02_kabeA_x");
        desc = g_storage_below == 185.0f && g_fcat_n == 4 && !strcmp(g_fcat[1], "Structure Highway") && a >= 0 &&
               g_fmatch[a].cat == 1 && !strcmp(g_fmatch[a].disp, "Highway: Wall A") && b2 >= 0 && g_fmatch[b2].cat == 2 &&
               c < 0 && !strcmp(g_fcat[3], "Other objects");
        DeleteFileW(dpath);
    }
    // Forge spawns: 4 SPAWN markers + 1 CAPSULE -> entries, players and capsule spots
    bool spawns = false;
    {
        static char entries[4 * 0x50], caps[0xD10 + 12 * 0x48];
        static unsigned char act2[0x13000];
        memset(entries, 0, sizeof(entries));
        memset(act2, 0, sizeof(act2));
        *(int *)(act2 + ACTOR_ACTIVE) = 1;
        ULONG_PTR actors[4] = {(ULONG_PTR)act2, 0, 0, 0};
        int mk_n = g_mk_n;
        FMark mk_save[8];
        memcpy(mk_save, g_mk, sizeof(mk_save));
        const float sp[4][2] = {{-100, -100}, {100, 100}, {-100, 100}, {100, -100}};
        for (int k = 0; k < 4; k++) { strcpy_s(g_mk[k].type, "SPAWN"); g_mk[k].p[0] = sp[k][0]; g_mk[k].p[1] = 200; g_mk[k].p[2] = sp[k][1]; }
        strcpy_s(g_mk[4].type, "CAPSULE"); g_mk[4].p[0] = -90; g_mk[4].p[1] = 200; g_mk[4].p[2] = -80;
        g_mk_n = 5;
        int r = f_apply_spawns(entries, 4, caps + 0xD10, actors);
        float *e0 = (float *)entries, *pa = (float *)(act2 + ACTOR_POS), *c0 = (float *)(caps + 0xD10);
        float h0 = *(float *)(entries + 0x0C);
        spawns = r == 4 && e0[0] == -100 && e0[1] == 210 && fabsf(h0 - 0.785398f) < 0.001f && pa[0] == -100 && pa[2] == -100 &&
                 c0[0] == -90 && c0[2] == -80 && abs(*(int *)(act2 + ACTOR_FACING) - 4096) <= 1 &&
                 fabsf(((float *)(entries + 0x14 + 12))[0] - (-100 + sinf(h0) * 60)) < 0.01f;
        // kill zone: player 1 stands in the pit next to marker 5 -> back to start entry 0 (slot 0), health 10 -> 7
        strcpy_s(g_mk[5].type, "KILLZONE"); g_mk[5].p[0] = 300; g_mk[5].p[1] = 150; g_mk[5].p[2] = 300;
        g_mk_n = 6;
        float *pk = (float *)(act2 + ACTOR_POS);
        pk[0] = 320; pk[1] = 140; pk[2] = 290;
        *(int *)(act2 + ACTOR_SLOT) = 0;
        *(int *)(act2 + ACTOR_HP) = 10;
        memset(g_fkill_last, 0, sizeof(g_fkill_last));
        int k1 = f_kill_zones(entries, 4, actors, 10000);
        bool kz = k1 == 1 && pk[0] == -100 && pk[1] == 210 && *(int *)(act2 + ACTOR_HP) == 7;
        pk[0] = 320; pk[1] = 140; pk[2] = 290;
        kz = kz && f_kill_zones(entries, 4, actors, 11000) == 0;            // 3 s cooldown
        *(int *)(act2 + ACTOR_HP) = 2;
        kz = kz && f_kill_zones(entries, 4, actors, 14000) == 1 && *(int *)(act2 + ACTOR_HP) == 1;   // never below 1
        pk[0] = 320; pk[1] = 175; pk[2] = 290;                            // 2.5 m above the point: no hit
        kz = kz && f_kill_zones(entries, 4, actors, 20000) == 0;
        spawns = spawns && kz;
        g_mk_n = 3;                                          // fewer than 4 spawns: nothing is written
        memset(entries, 0, sizeof(entries));
        spawns = spawns && f_apply_spawns(entries, 4, caps + 0xD10, actors) == 0 && ((float *)entries)[0] == 0;
        memcpy(g_mk, mk_save, sizeof(mk_save));
        g_mk_n = mk_n;
    }
    // v2.2 rotation order + magnets: a quarter turn of yaw takes +x to -z; a held box 1.2 m right of and 0.3 m above
    // another snaps flush beside it and level with it
    bool mag = false;
    {
        static float b7[6] = {-10, 0, -10, 10, 20, 10}, unit[6] = {10, 0, 0, 10, 0, 0};
        float W[6], q90[3] = {0, 1.5707963f, 0}, zero3[3] = {0, 0, 0};
        f_world_box(unit, zero3, q90, W);
        bool rot_ok = fabsf(W[0]) < 0.01f && fabsf(W[2] + 10.0f) < 0.01f;
        const float *keepbox[256];
        unsigned char keepplaced[FMAX];
        for (int k = 0; k < g_fd_n; k++) { keepbox[k] = g_fd[k].box; g_fd[k].box = b7; }
        for (int i = 0; i < g_fo_n; i++) { keepplaced[i] = g_fo[i].placed; g_fo[i].placed = i == 0; }
        float hp[3] = {22, 203, 0};
        int hit = f_magnet(3, hp, zero3);
        mag = rot_ok && hit == 7 && fabsf(hp[0] - 20) < 0.01f && fabsf(hp[1] - 200) < 0.01f && fabsf(hp[2]) < 0.01f;
        float far_[3] = {60, 203, 0};
        mag = mag && f_magnet(3, far_, zero3) == 0;                // 3 m away: no snap
        for (int k = 0; k < g_fd_n; k++) g_fd[k].box = keepbox[k];
        for (int i = 0; i < g_fo_n; i++) g_fo[i].placed = keepplaced[i];
        logf("self-test: rotation order %s, magnet %s (hit %d -> %.1f %.1f %.1f)", rot_ok ? "ok" : "FAILED", mag ? "ok" : "FAILED",
             hit, hp[0], hp[1], hp[2]);
    }
    // v2.3 collision follows: a stand-in object with one filed collision copy; a move takes it out of the tree, remakes it
    // from a matrix whose row 3 is the new position (and restores the matrix pointer), and files it again
    bool colok = false;
    {
        static int calls[5];
        static float seen_pos[3], *seen_top;
        static float base_top[16];
        static float *top_ptr = base_top;
        struct C {
            static void __fastcall unfile(void *) { calls[0]++; }
            static void __fastcall file(void *) { calls[2]++; }
            static void __fastcall remake(void *, void *) { calls[1]++; seen_top = top_ptr; memcpy(seen_pos, top_ptr + 12, 12); }
            static void __fastcall fx_end(void *) { calls[3]++; }
            static void *__fastcall fx_start(void *, void *b, void *, void *, unsigned char, char, void *l) { calls[4]++; *(void **)l = b; return b; }
        };
        FColApi fake = {&C::unfile, &C::file, &C::remake, &top_ptr, &C::fx_end, &C::fx_start};
        g_ft_col = &fake;
        static char copy0[64], src0[64];
        FtObj &o0 = objs[0];
        *(void **)((char *)&o0 + OBJ_COL_COPY) = copy0;
        *(void **)((char *)&o0 + OBJ_COL_SRC) = src0;
        *(unsigned *)((char *)&o0 + OBJ_COL_STATE) = 0;
        *(int *)((char *)&o0 + OBJ_COL_UNFILED) = 0;
        static char fxblock[16];
        *(void **)((char *)&o0 + OBJ_FX_BLOCK) = fxblock;
        *(void **)((char *)&o0 + OBJ_FX_LIST) = NULL;
        float to[3] = {33, 210, -44}, rz[3] = {0, 0, 0};
        int keep_hold = g_hold;
        g_hold = -1;
        unsigned char keep_placed = g_fo[0].placed;
        g_fo[0].placed = 1;
        f_set(0, to, rz);
        bool later = calls[4] == 0;                      // not in the same frame
        f_fx_step();
        colok = later && calls[0] == 1 && calls[1] == 1 && calls[2] == 1 && seen_pos[0] == 33 && seen_pos[1] == 210 && seen_pos[2] == -44 &&
                seen_top != base_top && top_ptr == base_top && calls[3] == 0 && calls[4] == 1 &&
                *(void **)((char *)&o0 + OBJ_FX_LIST) == fxblock;
        g_fo[0].placed = 0;                              // in storage: effects end, none start
        f_set(0, to, rz);
        f_fx_step();
        colok = colok && calls[3] == 1 && calls[4] == 1;
        g_fo[0].placed = 1;
        f_col_sync(0, -F_HIDE_DROP);                     // parked while held: remade 300 m down, effects ended, none started
        f_fx_step();
        colok = colok && fabsf(seen_pos[1] - (210 - F_HIDE_DROP)) < 0.01f && calls[3] == 2 && calls[4] == 1;
        g_fo[0].placed = keep_placed;
        *(void **)((char *)&o0 + OBJ_FX_BLOCK) = NULL;
        *(void **)((char *)&o0 + OBJ_FX_LIST) = NULL;
        *(void **)((char *)&o0 + OBJ_COL_COPY) = NULL;
        g_hold = keep_hold;
        g_ft_col = NULL;
        logf("self-test: collision + effects follow %s (unfile %d remake %d file %d, effects end %d start %d, at %.0f %.0f %.0f)",
             colok ? "ok" : "FAILED", calls[0], calls[1], calls[2], calls[3], calls[4], seen_pos[0], seen_pos[1], seen_pos[2]);
    }
    // v2.7 copies on demand: every copy of design 8 is placed -> a new one is made like it (same object, filed under the
    // parent, slot 0xFFFF, table + count grow); the limit and the "off" switch stop it; undo puts a later copy away
    bool makeok = false;
    {
        static int mcalls, padd;
        static ULONG_PTR seen_parent;
        struct M {
            static ULONG_PTR __fastcall any(void *, ULONG_PTR, ULONG_PTR) { mcalls++; return 0; }
            static ULONG_PTR __fastcall add(ULONG_PTR, ULONG_PTR parent, void *, ULONG_PTR code, int, int) { padd++; seen_parent = parent | (code == 0x904E28 ? 0 : 1); return 1; }
        };
        static void *mvt[0x48];
        for (int k = 0; k < 0x48; k++) mvt[k] = (void *)&M::any;
        mvt[VT_SET_POSITION / 8] = (void *)ft_setpos;
        mvt[VT_SET_ROTATION / 8] = (void *)ft_setrot;
        static FtObj made;
        struct C { static void *__fastcall make() { memset(&made, 0, sizeof(made)); made.vt = mvt; made.pos[1] = 200; return &made; } };
        static void *list4[32];
        list4[4] = (void *)&C::make;
        static ULONG_PTR classes[8];
        classes[2] = (ULONG_PTR)list4;
        static char defs[0x80 * 10];
        FMakeApi fake = {classes, defs, 0x5150, &M::add, false};
        rows[0x24 * 2 + 4] = 2; rows[0x24 * 2 + 5] = 4;          // design 8's row: class 2/4
        int keep_n = g_fo_n, cnt8 = 0;
        g_ft_make = &fake;
        g_fo[2].placed = 1;
        int c = f_free_copy(8);
        for (int i = 0; i < g_fo_n; i++) cnt8 += g_fo[i].obj == 8;
        makeok = c == keep_n && g_fo_n == keep_n + 1 && f_array()[c] == (void *)&made && g_fo[c].obj == 8 &&
                 *(unsigned short *)((char *)&made + 0x48) == 0xFFFF && *(unsigned short *)((char *)&made + 0x4A) == 8 &&
                 padd == 1 && seen_parent == 0x5150 && mcalls >= 20 && cnt8 == 2 && g_fmade == 1;
        g_fo[c].placed = 1;
        LONG keep_on = g_fcopies;
        g_fcopies = 0;
        makeok = makeok && f_free_copy(8) == -1;                 // switched off: no more
        g_fcopies = keep_on;
        int keep_made = g_fmade;
        g_fmade = F_MADE_MAX;
        makeok = makeok && f_free_copy(8) == -1;                 // stage limit
        g_fmade = keep_made;
        FSnap &old = g_hist[0];
        int keep_hn = old.fo_n;
        old.fo_n = keep_n;
        f_snap_apply(old, 99);                                   // undo to before the copy: it goes back below
        makeok = makeok && !g_fo[c].placed;
        old.fo_n = keep_hn;
        g_ft_make = NULL;
        g_fo_n = keep_n;
        g_fmade = 0;
        rows[0x24 * 2 + 4] = rows[0x24 * 2 + 5] = 0;
        g_fo[2].placed = 1;
        f_designs();
        logf("self-test: copies on demand %s (made %d, %d calls, filed under %llx)", makeok ? "ok" : "FAILED", c, mcalls,
             (unsigned long long)seen_parent);
    }
    // v2.1 hide my character: a stand-in draw slot; player 1's draw sees "captured by" -2, others -1, restored after
    bool hidep = false;
    {
        static int seen;
        struct P { static void __fastcall draw(void *o) { seen = *(int *)((char *)o + PLAYER_CAPTURED_BY); } };
        static void *slot2[1];
        slot2[0] = (void *)&P::draw;
        bool refused2 = !player_draw_install((ULONG_PTR)slot2, (ULONG_PTR)&P::draw + 1);
        bool inst = player_draw_install((ULONG_PTR)slot2, (ULONG_PTR)&P::draw);
        static unsigned char other[0x18000];
        *(int *)(actor + PLAYER_CAPTURED_BY) = -1;
        *(int *)(other + PLAYER_CAPTURED_BY) = -1;
        LONG s_hp = g_fhide_player, s_act = g_forge_active;
        g_fhide_player = 1; g_forge_active = 1;
        ((PFN_PLAYER_DRAW)slot2[0])(actor);
        int mine = seen, after = *(int *)(actor + PLAYER_CAPTURED_BY);
        ((PFN_PLAYER_DRAW)slot2[0])(other);
        int theirs = seen;
        g_fhide_player = 0;
        ((PFN_PLAYER_DRAW)slot2[0])(actor);
        int shown = seen;
        g_fhide_player = s_hp; g_forge_active = s_act; o_player_draw = NULL;
        hidep = refused2 && inst && mine == -2 && after == -1 && theirs == -1 && shown == -1;
        logf("self-test: hide my character refuse %s, installed %s, mine %d after %d, others %d, off %d", refused2 ? "ok" : "FAILED",
             inst ? "ok" : "FAILED", mine, after, theirs, shown);
    }
    // leaving Single Play ends the session
    *(unsigned *)(mode + 0x08) = 0x40000;
    forge_poll(true);
    bool ended = !g_forge_session && !g_forge_active;
    logf("self-test: forge native ok, banner %s, editor on %s, designs %s, spawn %s, turn %s, copy %s, copy limit %s, delete %s, "
         "save %s, load %s, refuse other map %s, descriptor %s, spawns %s, session end %s", banner ? "ok" : "FAILED", on ? "ok" : "FAILED",
         counts ? "ok" : "FAILED", spawned ? "ok" : "FAILED", turned ? "ok" : "FAILED", copied ? "ok" : "FAILED",
         limit ? "ok" : "FAILED", deleted ? "ok" : "FAILED", saved ? "ok" : "FAILED", loaded ? "ok" : "FAILED",
         refused ? "ok" : "FAILED", desc ? "ok" : "FAILED", spawns ? "ok" : "FAILED", ended ? "ok" : "FAILED");
    g_ft_inst = NULL; g_ft_rows = NULL; g_ft_mode = 0; g_fo_n = -1; g_fo_array = NULL; g_hold = -1;
    g_fbob = s_bob; g_fiso_mode = 0; memset(g_fhidden, 0, sizeof(g_fhidden));
    g_storage_below = g_ft_storage = -1e30f;
    if (!banner || !on) return -17;
    if (!counts || !spawned || !turned) return -18;
    if (!copied || !limit || !deleted) return -19;
    if (!saved || !loaded || !refused || !ended) return -20;
    if (!desc) return -21;
    if (!spawns) return -22;
    if (!hidep) return -23;
    if (!mag) return -24;
    if (!colok) return -25;
    if (!makeok) return -27;
    return 1;
}
