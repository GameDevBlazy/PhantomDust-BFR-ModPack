// ---- v5.139 stage effects (owner 2026-11-10).
// Game facts (recorded): effects are the game's own instances - start FUN_14018a040(manager, owner, request, track, matrix, 0),
// loop / move / stop (cwo_sfx.inl FxGame; memory pd-effect-instances), the effect request of skill N = g.effect_of(its profile,
// N), loaded on demand with FUN_140189e80(manager, request, 0); the instance draws with the 4x4 matrix it is given (row 0-2 =
// axes with their scale, row 3 = position; the downed glow uses a 0.32 scale); effect parts by skill
// (07_Combat_and_Skills/effect_tracks_2026-10-24.csv): Glacial Wall 123 part 4 = the barrier, Ice Storm 104 part 1 = the
// falling ice (loops), Freeze Gas 183 part 1 = the cloud (loops). The pack on Map Slot 7 = g_mx[g_mx_cur] while the loaded
// stage is the list stage (cwo_bigstage.inl kill_step).
// What this does: CwoStageFx.txt lines "mxNNN skill part x y z yaw_deg sx sy sz" - while that pack is the match's stage, each
// effect is kept looping at its place with its own size (display only; nothing hits anybody). Offline only.
// v5.140 (owner 2026-11-10): an optional damage zone per line -
// "... sx sy sz [radius every_s [min_life]]": while its effect is out, every every_s seconds each standing fighter within
// radius (x / z) and 40 units of height of the place loses 1 life, never below min_life (default 1: the storm wears you
// down, a hit finishes you). Facts (recorded, cwo_edgar.inl Shrinking arena / Stage Creator kill zones, Ghidra batch_065):
// fighter +0x20 present, +0x24 standing, +0x4C life, +0x12E40 position; the world is held while bit 0 of *0x140530034 is set.
// v5.141 (owner 2026-11-10;
// before: "storm cells should not deal damage unless it's falling down"): a damage line is now a STRIKE every every_s seconds
// (staggered per line): the skill's own part 0 burst plays once at the place (Ice Storm 104 part 0 = its frost burst, seen in
// game 10:0x), and each standing fighter within radius takes 1 damage (never below min_life) and gets its skill buttons
// frozen for freeze_s seconds (5th field, default 5): no new strike on a fighter that is still frozen.
// (v5.141 only - replaced in v5.142 by the game status below:) skill-button lock = the per-fighter lock table (Ghidra batch_065): FUN_140256920(pad, slot) - the "is skill slot
// pressed" test used by the skill step FUN_1402517c0 for every slot 0-3 - returns false while the int at
// 0x1404bbd70 + fighter number (+0x14) * 0x24 + slot * 4 is non-zero; the table is 4 x 0x24 bytes, cleared at each match start
// (FUN_1401282f0), and nothing else in the game writes it (readers only: 140256920, 14024ec80, 1402517c0, 140252a20,
// 1402531d0, 140253880, 140256e80). CPUs press skills through the same test (their brain sets the pad's pressed bits,
// FUN_14020deb0), so they freeze too. Only fighter numbers 0-3 have rows. The lock is put back to 0 when it ends, when the
// match / stage ends (spx_stop_all) and the world hold (photo pause) holds the timer too.
struct SfxPlace { char pack[16]; int skill, part; float x, y, z, yaw, sx, sy, sz; unsigned h; int fail; float hr, every; int floor; ULONGLONG next; float freeze;
                  float orb_r, orb_t;      // v5.148.3 orbit=R:T: the effect circles its place (radius R, T seconds a turn)
                  __declspec(align(16)) float m[16]; };   // v5.179.4: the matrix the game keeps a pointer to (spx_keep)
// v5.142 (owner 10:3x: "there was no indication ... It has to be a visual cue ... just like the skill Ice Sword or Frostbite"):
// the freeze is now the game's own: Ice Storm 104 / Ice Sword 9 carry on-hit effect 7 at skill record +0x62 with +0x64 = 300
// (frames: "Freezes target's skill buttons for 10 sec"; read live 10:3x); the hit code FUN_140272bc0 (called by the hit
// applier FUN_14026f870) turns effect 7 into FUN_14024d5c0(status block, 5, 1, frames); the status block = fighter + 0x40
// (the skill step FUN_1402517c0 tests FUN_14024d4d0(fighter + 0x40, 5) before any slot; the block's +0x61a0 points back at
// the fighter). The game counts the timer down itself and shows it the way it shows Ice Sword.
typedef void(__fastcall *PFN_STATUS_SET)(ULONG_PTR, int, int, int);
static const ULONG_PTR RVA_STATUS_SET = 0x24d5c0;
static const int ST_SKILLS_FROZEN = 5;
static ULONGLONG g_spx_frozen_until[4];                          // per fighter number: frozen by us until then (no new strike)
static bool spx_freeze(ULONG_PTR fighter, float secs) {
    __try {
        if (*(ULONG_PTR *)(fighter + 0x40 + 0x61a0) != fighter) return false;      // not the status block we know
        ((PFN_STATUS_SET)(g_base + RVA_STATUS_SET))(fighter + 0x40, ST_SKILLS_FROZEN, 1, (int)(secs * 30.0f));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// v5.148 (owner 2026-11-10): the game's own hit reaction. Ghidra (clean 9ba7411b): the hit reaction FUN_14024fd80 ends in FUN_140258690(fighter +
// 0x12F00, profile, -1, angle, -, effect): profile = 0x1B (the default reaction) when fighter +0x12E74 == 0 and the skill
// record's +0x52 / +0x76 are 0 (Ice Storm 104: both 0), else the record's +0x6E (Ice Storm: 46) for a low-level attacker;
// angle = the fighter's own facing +0x12E68 for kind-4 hits (as the game does); the 6th argument (the skill's effect,
// FUN_140259310) = 0 here: the reaction only. The motion block's +0xBD0 points back at the fighter (checked first).
typedef void(__fastcall *PFN_HIT_REACT)(ULONG_PTR, int, int, int, ULONG_PTR, int);
static const ULONG_PTR RVA_HIT_REACT = 0x258690;
static bool spx_react(ULONG_PTR fighter) {
    __try {
        ULONG_PTR m = fighter + 0x12F00;
        if (*(ULONG_PTR *)(m + 0xBD0) != fighter) return false;
        int prof = *(int *)(fighter + 0x12E74) == 0 ? 0x1B : 46;
        ((PFN_HIT_REACT)(g_base + RVA_HIT_REACT))(m, prof, -1, *(int *)(fighter + 0x12E68), 0, 0);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
struct SpxDrop { bool on; int place; float x, y, z; ULONGLONG t0; unsigned h; __declspec(align(16)) float m[16]; };
// v5.143.1 (in game 11:15: Ice Storm part 3 did not read as falling ice; the landing burst flashed the screen white): the drop is
// Glacial Wall's ice crystal (skill 123 part 4, the barrier - loaded on this stage by its placed walls) small and point down
// v5.147 (owner 11:4x: "I see the glacial wall dropping when you should have been using the ice storm. Its rain effect is what
// needs to be dropping"): the drop = Ice Storm's own part 1 (groups 2 / 3: group 3 = ONE long ice shard lying along z, group 2
// its small burst - placed still it lay flat (the "ring" of the 10:0x survey); the skill's rain code (kind 20, record
// +0x86 radius, +0x88 height, +0x8A drop speed: SKILL_KIND_PARAMETERS) moves each drop down), turned so z points down and
// moved down every frame; 6 drops a strike
// v5.179.6 (owner 2026-11-13; in game 18:42-18:44: drops landed and froze, none was SEEN falling).
// Game facts (pd_fn full pass 065, clean exe; SKILL_KIND_PARAMETERS kind 20): the rain's drop object FUN_14026b480 starts
// the skill's effect with FUN_14018d2f0(owner, request, TRACK 5, its own matrix) - track 5 = group 8, a particle emitter
// (effect_tracks: 104 track 5 = group 8, not looping); the drop starts +0x88 x 0.1 = 30 units above its spot and falls at
// +0x8A x 0.0926 (DAT_1402f7cd8) = 1.39 units a frame (30 frames a second) - about 0.72 s, upright (the caster's matrix).
// Before: part 1 (the projectile track, groups 2 / 3) turned point-down, 120 / 300 units over 0.7 / 1 s - never drawn moving.
static const int SPX_MAXDROPS = 48, SPX_DROPS = 6, SPX_FALL_SKILL = 104, SPX_FALL_PART = 5;
static const float SPX_FALL_SCALE = 1.0f, SPX_BURST_SCALE = 0.35f;
static const float SPX_FALL_H = 30.0f, SPX_FALL_T = 30.0f / (15.0f * 0.0926f * 30.0f), SPX_HIT_R = 28.0f;
static SpxDrop g_spx_drop[SPX_MAXDROPS];
// v5.179.4 the cause: sfx_pin stores the matrix POINTER in the instance (slot +0x40, read by the effect's emitters every frame;
// memory pd-effect-instances). The stage effects handed it a local array - once the call returned the game read whatever the
// stack held there, so a drop was drawn where it started and then from garbage. Places and drops now keep their own matrix
// (SfxPlace / SpxDrop .m, static), one-shot bursts the next of a ring of 128 (a burst is gone long before its turn comes back).
static __declspec(align(16)) float g_spx_bm[128][16];
static int g_spx_bm_i;
static float *spx_keep(const float *m) { float *d = g_spx_bm[g_spx_bm_i++ & 127]; memcpy(d, m, 64); return d; }
// v5.144 (owner): the name in the game's own font (RVA_TEXT, the
// battle HUD's text routine; 640 x 480 units; split screen (*0x1404bc9d4 == 6) = player 1 left half, player 2 right half)
static ULONGLONG g_spx_pop_at[8];
static const char *g_spx_pop_text[8];                            // v5.147 STORM CELL / VAPOR CLOUD
static void spx_pop_draw() {
    if (!g_in_match || ed_online()) return;
    ULONGLONG now = GetTickCount64();
    __try {
        bool split = *(int *)(g_base + 0x4bc9d4) == 6;
        int glyphs = *(int *)(g_base + RVA_GLYPHS), cap = *(int *)(g_base + RVA_GLYPH_CAP);
        if (glyphs > cap - 60 || *(int *)(g_base + RVA_OFS_X) != 0 || *(int *)(g_base + RVA_OFS_Y) != 0) return;
        int *space = (int *)(g_base + RVA_FONT_SPACE), *gap = (int *)(g_base + RVA_FONT_GAP);
        int *fh = (int *)(g_base + RVA_FONT_H), *fw = (int *)(g_base + RVA_FONT_W), *x0 = (int *)(g_base + RVA_FONT_X0);
        DWORD *rgba = (DWORD *)(g_base + RVA_FONT_RGBA);
        int s_space = *space, s_gap = *gap, s_h = *fh, s_w = *fw, s_x0 = *x0;
        DWORD s_rgba[4]; memcpy(s_rgba, rgba, sizeof(s_rgba));
        PFN_TEXT text = (PFN_TEXT)(g_base + RVA_TEXT);
        for (int view = 0; view < (split ? 2 : 1); view++) {
            ULONGLONG at = g_spx_pop_at[view];
            if (!at || now - at > 2200) continue;
            unsigned age = (unsigned)(now - at), a = age < 1200 ? 255u : 255u - (age - 1200) * 255u / 1000u;
            int X = (split ? view * 320 : 0) + 24, Y = 446;
            *space = 0; *gap = 2; *fh = 18; *fw = 10;
            const char *tx = g_spx_pop_text[view] ? g_spx_pop_text[view] : "STORM CELL";
            set_rgba(rgba, (a << 24) | 0x000000); text(X + 2, Y + 2, tx);
            set_rgba(rgba, (a << 24) | 0x9FE6FF); text(X, Y, tx);
        }
        memcpy(rgba, s_rgba, sizeof(s_rgba));
        *space = s_space; *gap = s_gap; *fh = s_h; *fw = s_w; *x0 = s_x0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
// v5.146 (owner 2026-11-10):
// effects started with owner 0 hung their joint-named nodes on fighter 0 = player 1. Facts (pd-effect-instances, Ghidra
// FUN_14018a7d0 / FUN_14018aa50): owner = fighter number; each owner's joint table at manager + 0xF070 + owner * 0x800, its
// count at + 0x11070 + owner * 4 (4 owners), built when the fighter is set up; a node's joint hash is looked up there and an
// unknown hash gives 0 (the node then uses the instance's own matrix). So the stage effects take an owner number no fighter
// of this match has, its count set to 0 (nothing matches: every node at the effect's own place); with all 4 numbers taken,
// a CPU's number (never a human player's).
static int g_spx_owner = -1;
static int spx_owner(FxGame &g) {
    if (g_spx_owner >= 0) return g_spx_owner;
    bool used[4] = {false, false, false, false}; int cpu = -1;
    ULONG_PTR *acts = cwo_actors();
    int nf = cwo_nfighters(); if (nf > 8) nf = 8;
    __try {
        for (int p = 0; p < nf; p++) {
            ULONG_PTR a = acts[p];
            if (!a) continue;
            int num = *(int *)(a + 0x14);
            if (num >= 0 && num < 4) {
                used[num] = true;
                bool human = *(int *)(cwo_gaddr(0x4bc420) + 0x48 * (ULONG_PTR)p + 0x10) != 0;
                if (!human) cpu = num;
            }
        }
        for (int n = 3; n >= 0; n--) if (!used[n]) {
            *(int *)(g.mgr + 0x11070 + 4 * (ULONG_PTR)n) = 0;
            g_spx_owner = n;
            logf("stage effects: owner %d (a free fighter number, empty joint table) - no effect part on a fighter", n);
            return n;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    g_spx_owner = cpu >= 0 ? cpu : 0;
    logf("stage effects: all 4 owner numbers in use - owner %d (%s)", g_spx_owner, cpu >= 0 ? "a CPU" : "player 1, no CPU");
    return g_spx_owner;
}
static void spx_unfreeze_all() { memset(g_spx_frozen_until, 0, sizeof(g_spx_frozen_until)); }
static SfxPlace g_spx[64];
static int g_spx_n = -1;
static LONG g_spx_pack = -2;
static bool g_spx_loaded[64];

static void spx_load() {
    g_spx_n = 0;
    wchar_t p[MAX_PATH]; swprintf_s(p, MAX_PATH, L"%sCwoStageFx.txt", g_dir);
    FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"r") != 0 || !f) return;
    char line[200];
    while (g_spx_n < 64 && fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        SfxPlace s;
        memset(&s, 0, sizeof(s));
        s.floor = 1;
        s.freeze = 5.0f;
        int got = sscanf_s(line, "%15s %d %d %f %f %f %f %f %f %f %f %f %d %f", s.pack, (unsigned)sizeof(s.pack), &s.skill, &s.part, &s.x, &s.y, &s.z,
                           &s.yaw, &s.sx, &s.sy, &s.sz, &s.hr, &s.every, &s.floor, &s.freeze);
        if (s.freeze < 0 || s.freeze > 30) s.freeze = 5.0f;
        if (const char *o = strstr(line, "orbit=")) { if (sscanf_s(o, "orbit=%f:%f", &s.orb_r, &s.orb_t) != 2 || s.orb_r <= 0 || s.orb_t < 5) s.orb_r = s.orb_t = 0; }
        if (got >= 10 && s.skill > 0 && s.part >= 0 && s.part <= 5) {
            if (got < 12 || s.hr <= 0 || (s.every < 0.25f && s.every > -0.25f)) { s.hr = 0; s.every = 0; }   // v5.147 < 0 = a frost mine
            if (s.floor < 0) s.floor = 0;
            s.h = FX_NONE;
            g_spx[g_spx_n++] = s;
        }
    }
    fclose(f);
    logf("stage effects: %d placed effects in CwoStageFx.txt", g_spx_n);
}

static void spx_stop_all(FxGame &g) {
    for (int i = 0; i < (g_spx_n > 0 ? g_spx_n : 0); i++) {
        if (g_spx[i].h != FX_NONE && fx_alive(g, g_spx[i].h)) g.stop(g_spx[i].h);
        g_spx[i].h = FX_NONE; g_spx[i].fail = 0; g_spx[i].next = 0;
    }
    memset(g_spx_loaded, 0, sizeof(g_spx_loaded));
    for (int q = 0; q < SPX_MAXDROPS; q++) { if (g_spx_drop[q].on && g_spx_drop[q].h != FX_NONE && fx_alive(g, g_spx_drop[q].h)) g.stop(g_spx_drop[q].h); g_spx_drop[q].on = false; }
    spx_unfreeze_all();
    g_spx_owner = -1;                                             // chosen again for the next match
}

static void spx_damage(FxGame &g, LONG k) {                       // v5.141 strikes (game thread, after the effects)
    bool held = false;
    __try { held = (*(unsigned *)(g_base + 0x530034) & 1) != 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return; }
    ULONGLONG now = GetTickCount64();
    static ULONGLONG held_at;
    if (held) { if (!held_at) held_at = now; return; }
    if (held_at) {
        ULONGLONG d = now - held_at; held_at = 0;
        for (int i = 0; i < g_spx_n; i++) if (g_spx[i].next) g_spx[i].next += d;
        for (int n = 0; n < 4; n++) if (g_spx_frozen_until[n]) g_spx_frozen_until[n] += d;
        for (int q = 0; q < SPX_MAXDROPS; q++) if (g_spx_drop[q].on) g_spx_drop[q].t0 += d;
    }
    for (int n = 0; n < 4; n++) if (g_spx_frozen_until[n] && now >= g_spx_frozen_until[n]) {
        g_spx_frozen_until[n] = 0; logf("stage hazard: fighter number %d freeze over", n); }
    static int hits_logged;
    int nf = cwo_nfighters(); if (nf > 8) nf = 8;
    // v5.143 (owner): a strike now drops SPX_DROPS ice
    // clusters from SPX_FALL_H above the place, each at its own spot inside the radius; each is the skill's own part
    // SPX_FALL_PART (Ice Storm group 5: its cluster of ice shards) moved down every frame, falling faster and faster (the
    // instance draws where its matrix says, as every placed effect here); on landing the part 0 frost burst plays there and
    // only a fighter under that drop (SPX_HIT_R, +-40 height) takes the 1 damage + the game's freeze. Pauses with the world.
    for (int i = 0; i < g_spx_n; i++) {
        SfxPlace &s = g_spx[i];
        if (s.hr <= 0 || s.every <= 0 || s.h == FX_NONE || _stricmp(s.pack, g_mx[k].file)) continue;
        ULONGLONG ev = (ULONGLONG)(s.every * 1000.0f);
        if (!s.next) { s.next = now + ev / 2 + (ULONGLONG)(i * 1597) % ev; continue; }   // staggered: the cells do not all strike at once
        if (now < s.next) continue;
        s.next = now + ev;
        for (int d = 0; d < SPX_DROPS; d++) {                      // the drops of this strike, a little apart in time
            int slot = -1;
            for (int q = 0; q < SPX_MAXDROPS; q++) if (!g_spx_drop[q].on) { slot = q; break; }
            if (slot < 0) break;
            SpxDrop &dr = g_spx_drop[slot];
            unsigned rnd = (unsigned)(now / 7 + i * 7919 + d * 104729) * 2654435761u;
            float a = (float)(rnd % 6283) / 1000.0f, rr = s.hr * (0.15f + 0.85f * (float)((rnd >> 13) % 1000) / 1000.0f) * 0.8f;
            dr.on = true; dr.place = i; dr.h = FX_NONE;
            dr.x = s.x + rr * sinf(a); dr.z = s.z + rr * cosf(a); dr.y = s.y;
            dr.t0 = now + (ULONGLONG)(d * 220);
        }
    }
    // v5.147 FROST MINES (owner): a line with every_s < 0 = a mine: its looping part (Vapor
    // Cloud 362 = effect 233, part 1 loops = the set mine) is the armed mine; a standing fighter within the radius sets it
    // off: part 2 (the blast) plays, every standing fighter in the radius gets the game's freeze (status 5) for freeze_s and
    // VAPOR CLOUD on its screen corner (damage only if min_life allows: 99 = none), the mine goes away and comes back armed
    // after -every_s seconds (SKILLS_FULL_INDEX 362: "Detonates when anyone enters blast area. Freezes targets skill buttons for
    // 3 sec.")
    for (int i = 0; i < g_spx_n; i++) {
        SfxPlace &s = g_spx[i];
        if (s.hr <= 0 || s.every >= 0 || s.h == FX_NONE || _stricmp(s.pack, g_mx[k].file)) continue;
        bool hit = false;
        for (int p = 0; p < nf && !hit; p++) {
            __try {
                ULONG_PTR r = ed_player(p);
                if (!r || !*(int *)(r + 0x20) || !*(int *)(r + 0x24)) continue;
                const float *pos = (const float *)(r + ACTOR_POS);
                float dx = pos[0] - s.x, dz = pos[2] - s.z, dy = pos[1] - s.y;
                if (dx * dx + dz * dz <= s.hr * s.hr && dy > -20.0f && dy < 30.0f) hit = true;
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        if (!hit) continue;
        __try {
            short prof = *(short *)(g.skills + 0x14 + (ULONG_PTR)s.skill * 0x90);
            int req = prof > 0 ? g.effect_of(prof, s.skill) : 0;
            // v5.147.2 (owner): effect 233's
            // parts by their own meshes (effect 233): part 0 = g0 emitter + g1 32 meshes 60 x 23 x 60 = THE CLOUD
            // that fills the blast area; part 1 = the set mine (g2 / g3, loops); part 2 = a 1.2-unit disc; part 3 = 44 small
            // sparkles. The detonation = part 0 + part 3 at the mine (5.147.1 played the two small parts only)
            float mb[16] = {s.sx, 0, 0, 0,   0, s.sy, 0, 0,   0, 0, s.sz, 0,   s.x, s.y + 2.0f, s.z, 1};
            static const int BLAST[2] = {0, 3};
            float *pb = spx_keep(mb);                             // v5.179.4 lives as long as the blast
            for (int bi = 0; bi < 2 && req > 0; bi++) { unsigned hb = g.start(g.mgr, spx_owner(g), req, BLAST[bi], pb, 0); if (hb != FX_NONE) sfx_pin(g, hb, pb); }
            if (fx_alive(g, s.h)) g.stop(s.h);
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        s.h = FX_NONE;
        s.next = now + (ULONGLONG)(-s.every * 1000.0f);               // disarmed until then (spx_step does not restart it)
        for (int p = 0; p < nf; p++) {
            __try {
                ULONG_PTR r = ed_player(p);
                if (!r || !*(int *)(r + 0x20) || !*(int *)(r + 0x24)) continue;
                const float *pos = (const float *)(r + ACTOR_POS);
                float dx = pos[0] - s.x, dz = pos[2] - s.z, dy = pos[1] - s.y;
                if (dx * dx + dz * dz > s.hr * s.hr || dy < -20.0f || dy > 30.0f) continue;
                int num = *(int *)(r + 0x14);
                int life = *(int *)(r + 0x4C), nl = life > s.floor ? life - 1 : life;
                *(int *)(r + 0x4C) = nl;
                bool fz = s.freeze > 0 && spx_freeze(r, s.freeze);
                if (fz && num >= 0 && num < 4) g_spx_frozen_until[num] = now + (ULONGLONG)(s.freeze * 1000.0f);
                if (p < 8) { g_spx_pop_at[p] = now; g_spx_pop_text[p] = "VAPOR CLOUD"; }
                if (hits_logged < 200) { hits_logged++; logf("stage hazard: %s frost mine at (%.0f, %.0f) - fighter %d life %d -> %d, game freeze %s %.0f s",
                                                           s.pack, s.x, s.z, p + 1, life, nl, fz ? "on" : "NOT SET", s.freeze); }
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
    }
    for (int q = 0; q < SPX_MAXDROPS; q++) {
        SpxDrop &dr = g_spx_drop[q];
        if (!dr.on || now < dr.t0) continue;
        SfxPlace &s = g_spx[dr.place];
        float t = (float)(now - dr.t0) / 1000.0f;
        float fall = t < SPX_FALL_T ? SPX_FALL_H * (1.0f - t / SPX_FALL_T) : 0.0f;   // v5.179.6 the drop's own steady speed
        __try {
            short prof = *(short *)(g.skills + 0x14 + (ULONG_PTR)s.skill * 0x90);
            int req = prof > 0 ? g.effect_of(prof, s.skill) : 0;
            short fprof = *(short *)(g.skills + 0x14 + (ULONG_PTR)SPX_FALL_SKILL * 0x90);
            int freq = fprof > 0 ? g.effect_of(fprof, SPX_FALL_SKILL) : 0;
            float sc = SPX_FALL_SCALE;                             // rows = axes: x stays, y -> +z, z -> down (the shard points down)
            float m[16] = {sc, 0, 0, 0,   0, sc, 0, 0,   0, 0, sc, 0,   dr.x, dr.y + fall, dr.z, 1};   // v5.179.6 upright, as the game's drop
            memcpy(dr.m, m, 64);                                  // v5.179.4 the drop's own matrix
            if (t < SPX_FALL_T) {
                if (freq > 0 && (dr.h == FX_NONE || !fx_alive(g, dr.h))) { dr.h = g.start(g.mgr, spx_owner(g), freq, SPX_FALL_PART, dr.m, 0); }   // v5.179.6 plays once (track 5 does not loop)
                if (dr.h != FX_NONE) { g.move(dr.h, dr.m); sfx_pin(g, dr.h, dr.m); }
                continue;
            }
            if (dr.h != FX_NONE && fx_alive(g, dr.h)) g.stop(dr.h);
            dr.h = FX_NONE; dr.on = false;
            float mb[16] = {SPX_BURST_SCALE, 0, 0, 0,   0, SPX_BURST_SCALE, 0, 0,   0, 0, SPX_BURST_SCALE, 0,   dr.x, dr.y, dr.z, 1};
            if (req > 0) { float *pb = spx_keep(mb); unsigned hb = g.start(g.mgr, spx_owner(g), req, 0, pb, 0); if (hb != FX_NONE) sfx_pin(g, hb, pb); }   // the landing burst
        } __except (EXCEPTION_EXECUTE_HANDLER) { dr.on = false; continue; }
        for (int p = 0; p < nf; p++) {
            __try {
                ULONG_PTR r = ed_player(p);
                if (!r || !*(int *)(r + 0x20) || !*(int *)(r + 0x24)) continue;
                const float *pos = (const float *)(r + ACTOR_POS);
                float dx = pos[0] - dr.x, dz = pos[2] - dr.z, dy = pos[1] - dr.y;
                if (dx * dx + dz * dz > SPX_HIT_R * SPX_HIT_R || dy < -40.0f || dy > 40.0f) continue;
                int num = *(int *)(r + 0x14);
                if (num >= 0 && num < 4 && g_spx_frozen_until[num]) continue;          // still frozen: not struck again
                int life = *(int *)(r + 0x4C), nl = life > s.floor ? life - 1 : life;
                *(int *)(r + 0x4C) = nl;
                bool rc = spx_react(r);                                                  // v5.148 the hit / stun animation
                bool fz = s.freeze > 0 && spx_freeze(r, s.freeze);
                if (fz && num >= 0 && num < 4) g_spx_frozen_until[num] = now + (ULONGLONG)(s.freeze * 1000.0f);
                if (p < 8) { g_spx_pop_at[p] = now; g_spx_pop_text[p] = "STORM CELL"; }
                if (hits_logged < 200) logf("stage hazard: hit reaction %s", rc ? "played (profile 0x1B / 46)" : "NOT played");   // v5.144 STORM CELL on that fighter's screen corner
                if (hits_logged < 200) { hits_logged++; logf("stage hazard: %s skill %d ice landed at (%.0f, %.0f) - fighter %d (number %d) life %d -> %d, game freeze (status 5) %s %.0f s",
                                                           s.pack, s.skill, dr.x, dr.z, p + 1, num, life, nl, fz ? "on" : "NOT SET", s.freeze); }
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
    }
}

static void spx_step() {                                           // game thread, every drawn frame (as dg_step)
    if (g_spx_n < 0) spx_load();
    if (g_spx_n <= 0) return;
    FxGame g;
    if (!fx_game(&g)) return;
    LONG k = g_mx_cur;
    // v5.148.11 (in game 16:16 / 16:22 / 16:27 / 16:47: access violation at exe+1e4ef in the particle draw, FUN_14001e4b0 <-
    // FUN_14001de70 = a texture entry marked created (-1) whose texture object was freed). Ghidra: the effect load FUN_140189e80
    // makes room by EVICTING the least-recently-used effect no instance uses (FUN_14018cf80) - its textures are freed while its
    // particles may still be flying. The loader loaded stage effects in the middle of a match. Now: at each new match (match state
    // 0 -> 1..5, before the fight, no particles yet) the pack's stage effects are loaded at once and the old marks cleared (each
    // match frees the effect data); the mid-match load stays only as a fallback.
    {
        static int lastph = -1;
        int ph = 0; __try { ph = *(int *)(g_base + 0x4bc950); } __except (EXCEPTION_EXECUTE_HANDLER) {}
        if (ph != lastph) {
            if (ph >= 1 && ph <= 5 && (lastph <= 0 || lastph >= 9)) {
                memset(g_spx_loaded, 0, sizeof(g_spx_loaded));
                if (!ed_online() && *(int *)(g_base + RVA_CUR_STAGE) == LIST_STAGE && k >= 0 && k < g_mx_count) {
                    int nl = 0;
                    __try {
                        for (int i = 0; i < g_spx_n; i++) {
                            if (_stricmp(g_spx[i].pack, g_mx[k].file)) continue;
                            short prof = *(short *)(g.skills + 0x14 + (ULONG_PTR)g_spx[i].skill * 0x90);
                            int req = prof > 0 ? g.effect_of(prof, g_spx[i].skill) : 0;
                            if (req <= 0) continue;
                            typedef INT64 (__fastcall *PFN_FX_LOAD0)(ULONG_PTR, int, int);
                            ((PFN_FX_LOAD0)(g_base + 0x189e80))(g.mgr, req, 0);
                            g_spx_loaded[i] = true; nl++;
                        }
                    } __except (EXCEPTION_EXECUTE_HANDLER) {}
                    if (nl) logf("stage effects: %s - %d effects loaded before the fight (match state %d)", g_mx[k].file, nl, ph);
                }
            }
            lastph = ph;
        }
    }
    bool on = g_in_match && !ed_online() && *(int *)(g_base + RVA_CUR_STAGE) == LIST_STAGE && k >= 0 && k < g_mx_count;
    { int ph = 0; __try { ph = *(int *)(g_base + 0x4bc950); } __except (EXCEPTION_EXECUTE_HANDLER) {}
      if (ph < 1 || ph > 8) on = false; }                             // 5.148.13: not at the match end (9-11) either                         // v5.148.12 only in a real match (not the stage preview behind the list)
    if (!on || k != g_spx_pack) { spx_stop_all(g); g_spx_pack = on ? k : -2; if (!on) return;
        int n = 0; for (int i = 0; i < g_spx_n; i++) if (!_stricmp(g_spx[i].pack, g_mx[k].file)) n++;
        if (n) logf("stage effects: %s - %d effects", g_mx[k].file, n); }
    __try {
        for (int i = 0; i < g_spx_n; i++) {
            SfxPlace &s = g_spx[i];
            if (_stricmp(s.pack, g_mx[k].file)) continue;
            float c = cosf(s.yaw * 0.0174533f), sn = sinf(s.yaw * 0.0174533f);
            float m[16] = {c * s.sx, 0, sn * s.sx, 0,   0, s.sy, 0, 0,   -sn * s.sz, 0, c * s.sz, 0,   s.x, s.y, s.z, 1};
            if (s.orb_r > 0) {                                       // v5.148.3 owner (Ruins): "a slowly moving tornado / twister"
                float an = (float)fmod(GetTickCount64() / 1000.0, (double)s.orb_t) / s.orb_t * 6.2831853f + i;
                m[12] += s.orb_r * cosf(an); m[14] += s.orb_r * sinf(an);
            }
            memcpy(s.m, m, 64);                                      // v5.179.4 the place's own matrix (the game keeps the pointer)
            if (s.h != FX_NONE && fx_alive(g, s.h)) { g.move(s.h, s.m); sfx_pin(g, s.h, s.m); continue; }
            if (s.fail > 400) continue;
            if (s.every < 0 && s.next && GetTickCount64() < s.next) continue;   // v5.147 a frost mine re-arming
            short prof = *(short *)(g.skills + 0x14 + (ULONG_PTR)s.skill * 0x90);
            int req = prof > 0 ? g.effect_of(prof, s.skill) : 0;
            if (req <= 0) { s.fail = 999; logf("stage effects: skill %d has no effect - left out", s.skill); continue; }
            unsigned h = g.start(g.mgr, spx_owner(g), req, s.part, s.m, 0);
            if (h == FX_NONE) {
                if (++s.fail == 20 && !g_spx_loaded[i]) {
                    typedef INT64 (__fastcall *PFN_FX_LOAD)(ULONG_PTR, int, int);
                    ((PFN_FX_LOAD)(g_base + 0x189e80))(g.mgr, req, 0);
                    g_spx_loaded[i] = true;
                    logf("stage effects: effect %d (skill %d) loaded", req, s.skill);
                }
                continue;
            }
            g.loop(h);
            sfx_pin(g, h, s.m);
            s.h = h; s.fail = 0;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    spx_damage(g, k);
}

// v5.148.1 (owner 2026-11-10): a watch
// that logs every change of the battlefield's environment. Ghidra (clean 9ba7411b): the current environment skill =
// *0x1404BBED8 while the match state *0x1404BC950 == 6 (FUN_140271660); its writers: the skill cast at 0x14027C5A9, the script
// native g4/472 FUN_140212A00 (quests 078-081 only), the Special Environment rule's timer FUN_14012DAB0 / 14012DC15 (rule 5;
// rule *0x1404BC5DC). Read only: the skill number, the rule, and each fighter's hand (slots actor + 0x388 + 0x50 k, skill
// +0x30) at that moment, so the log names who held it.
static void env_watch() {
    static int last = -2;
    __try {
        int st = *(int *)(g_base + 0x4bc950), env = st == 6 ? *(int *)(g_base + 0x4bbed8) : -1;
        if (env == last) return;
        if (last != -2 || env >= 0) {
            char hands[256] = ""; size_t n = 0;
            for (int p = 0; p < 4; p++) {
                ULONG_PTR r = ed_player(p);
                if (!r || !*(int *)(r + 0x20)) continue;
                n += sprintf_s(hands + n, sizeof(hands) - n, " | fighter %d:", p + 1);
                for (int k = 0; k < 5 && n < sizeof(hands) - 8; k++) n += sprintf_s(hands + n, sizeof(hands) - n, " %d", *(int *)(r + 0x388 + 0x50 * k + 0x30));
            }
            logf("environment: %d -> %d (rule %d)%s", last, env, *(int *)(g_base + 0x4bc5dc), hands);
        }
        last = env;
    } __except (EXCEPTION_EXECUTE_HANDLER) { last = -2; }
}
