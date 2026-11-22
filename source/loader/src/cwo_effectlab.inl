// ---- v5.194.0 EFFECT LAB on a real stage (owner 2026-11-22; on the first, viewer-based build: "This looks extremely sloppy ... it has to do
// everything that the PDPEFECCT did ... it will then load up any stage that we choose ... and see everything work as intended").
// What it is now: the Lab row (tools/build_lab_effectlab.py, native 2.8 op 126) goes into the game's Single Play mode with the
// SINGLE PLAY lobby in its EFFECT LAB form (stage, character, outfit; One-on-One against a target that stands still). In that
// match the Effect Lab panel fires any skill: the skill is put in one of your four hand slots the game's own way and used by
// the game's own skill pipeline (motion, projectile, hit), with the parts of its effect listed as they play
// (pdpEffect: START, SHOT, HIT, HITEFFECT, DEFENCE, DESTROY; 02_Xbox_Alpha_Research/pdpeffect_repo/PDPEFFECT_TOOL_KNOWLEDGE.md).
// Game facts (Ghidra full pass 065, recorded export + capstone on the clean exe, 2026-11-22):
//   - a skill's effect is loaded when its card enters a hand and released when it leaves: FUN_140189e80(mgr *0x1405bb2e0,
//     request, 0) loads (the loader entry *(0x1405bb228)+0xC0, 0x88 each: +0 kind 2 loading / 3 loaded / 0 free, +8 request,
//     +0xC the count of holders); FUN_14018d280(request) takes one holder off and frees the entry at 0. The Character viewer's
//     skill page (FUN_14015d980) does exactly that pair around its own capture FUN_140261a00. In game 2026-11-22: a card
//     captured WITHOUT that load draws freed textures from its second use (the test caster's fault) - so the load comes first.
//   - request of a skill = FUN_1401272b0(profile i16 at record +0x14, skill); hand slots fighter +0x388 + 0x50 * k
//     (cwo_newskills.inl sc_capture / sc_slot_has), the queued use fighter +0x61f4 = slot when the fighter is free.
//   - a CPU fighter runs its brain while fighter +0x142d0 != 0 (FUN_14024ec80); 0 = it stands (the Mirror's parked double).
//   - status block fighter +0x40: +0x0C / +0x10 life, +0x14 its full value, +0x18 aura level, +0x1C aura.
// Offline only: from the Xbox Live Lab the row says so (the test match is a local one).
// g_el (the Effect Lab is on: lobby + match) and g_el_panel (its panel has the buttons) are defined in cwo_loader.cpp.
static const char *EL_SCHOOL[7] = {"Aura", "Psycho", "Optical", "Nature", "Ki", "Faith", "Illusion"};
static const char *EL_PART[6] = {"START", "SHOT", "HIT", "HITEFFECT", "DEFENCE", "DESTROY"};
static const char *EL_DUMMY[2] = {"stands still", "fights back"};
static volatile LONG g_el_skill = 45, g_el_slot, g_el_loop, g_el_fire, g_el_dummy, g_el_refill = 1;
// v5.195.0 the MOTION page (pdpEffect's MotionIndex: the fighter plays a chosen motion) and the map switch (pdpEffect's
// "Map display"): L3 switches the list between SKILLS and MOTIONS; on MOTIONS A plays animation slot N through the game's
// motion player FUN_140257310 (as the Create a Skill "Animation from" preview), Y loops it, B hides / shows the stage
// (the world's task groups 9 = the stage and its objects, 29 = the sky: bit 0x20000 = suspended, cwo_stageview.inl's fact).
static volatile LONG g_el_page, g_el_motion, g_el_mfire, g_el_mloop, g_el_map = 1;
// v5.196.0 the SETUP page = pdpEffect's settings panel (rows with values): Character, Map, Map display, Grid display,
// Background, Force loop, Target. Character / Map are the lobby's own values; A reloads the match with them (the game's Give
// Up, the result screens passed, START GAME again). Grid: floor points every 5 m around you, projected with the game's view
// matrix 0x1405a5ca0 and its projection (FUN_140291570: horizontal fov 75 deg, aspect 853 / 480; cwo_ultrawide.inl's facts).
// The settings are kept in CwoEffectLab.txt. Wind (pdpEffect rows 12-15) has no counterpart found in this build of the game.
static volatile LONG g_el_row, g_el_grid, g_el_pending;
// v5.197.0 the rest of pdpEffect's panel (Ghidra: the alpha's settings handler FUN_00069180 and fire FUN_00069c60, batch_x002;
// the final game's effect manager, pass 065):
//   BaseIndex (alpha row 4, range 6) = the PART of the effect the fire button starts (FUN_00067c40(BaseIndex, matrix)):
//     0 START, 1 SHOT, 2 HIT, 3 HITEFFECT, 4 DEFENCE, 5 DESTROY - here "Part": ALL = the whole skill, or one part on you.
//   MotionIndex (row 6) = the motion played with the fire (FUN_00068490) - here "Motion on fire" (the MOTIONS list's motion).
//   Force infinite loop (row 8) = the loop flag of the started part (FUN_000663c0; final game FUN_14018d410).
//   Pre-render (row 7) = effect manager +0xd984 in the alpha = +0x11590 in the final game (FUN_140189830 sets 1; FUN_140188fd0
//     runs the effects' pre-pass while it is set).
//   Wind (rows 12-15: strength 0-1000, X / Y / Z 0-100, 50 = none; FUN_00067ab0) = the effect manager's wind: final game
//     +0x11594 "follow the stage's wind" (1), +0x11598 direction, +0x115a4 strength, +0x115a8 vector; FUN_1401899b0 copies the
//     stage's wind object (*0x140533308 +0xC8, x 0.3) into them each frame while +0x11594 is set; every particle emitter adds
//     the vector (FUN_1401a2050, FUN_14010da90: x 0.01) or direction x strength (FUN_1401a03c0).
//   EffectFileSize / TotalFileSize = the effect's span in Effect<pack>.e (e.i entry +8) / all loaded effects' spans.
static volatile LONG g_el_part = -1, g_el_mot_fire, g_el_prerender = 1, g_el_wind_s, g_el_wind[3] = {50, 50, 50};
static const int EL_ROWS = 15;
static unsigned g_el_part_h = 0xFFFFFFFFu;
static int g_el_part_req, g_el_part_ld;                          // the part's effect, and the request the lab holds loaded
static bool g_el_wind_set, g_el_pre_set;
// v5.198.0 the last four pieces of pdpEffect (alpha fire / menu handlers FUN_00069c60 / FUN_00069180, batch_x002):
//  - EFFECTS list: every effect of the game's effect index (Effect\e.i, *0x1405bb2e8: u16 count +0x8a, 12-byte entries from +0x8c
//    {u16 request, u16 pack, u32 offset, u32 span}) - pdpEffect played any entry of its name.lst, not only skills' effects.
//  - CAMERA MODE / PLAYER MOVE MODE with Pos / Rot of the moved thing (alpha draw FUN_00068560): the free camera (its position
//    and angles) or your fighter.
//  - RTRIGGER (alpha: speed = (255 - right trigger) / 255 for the effects and the motion): RT slows the fight, LT held = frozen,
//    a RT press while frozen = one step - the photo mode's world pace (cwo_freecam.inl ph_want / ph_step, *0x140530034 bit 0).
//  - the alpha's "+RIGHT / +LEFT" = 10 at a time (Left / Right), its stop-all button (0x40: every effect off, motion 0) = X here.
static volatile LONG g_el_fxi, g_el_camera, g_el_stopall;
static int g_el_held[64], g_el_nheld;                           // effects the lab asked for (held to the match end)
static unsigned g_el_hs[8];                                      // the instances the lab started (parts)
static int g_el_nhs, g_el_hreq;
static short g_el_owner[8192];                                   // effect request -> the first skill that uses it (0 = none)
static bool g_el_owner_made;
static float g_el_speed = 1.0f;
static bool g_el_paused;
static int el_ei_count() { unsigned char *ei = fx_ei(); __try { return ei ? *(unsigned short *)(ei + 0x8a) : 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; } }
static int el_ei_req(int i) { unsigned char *ei = fx_ei(); __try { return ei && i >= 0 && i < el_ei_count() ? *(unsigned short *)(ei + 0x8c + 12 * i) : 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; } }
static unsigned el_ei_span(int i) { unsigned char *ei = fx_ei(); __try { return ei && i >= 0 && i < el_ei_count() ? *(unsigned *)(ei + 0x8c + 12 * i + 8) : 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; } }
static int el_effect(int id);
static int el_fx_kind(int req);
static int g_el_count = 375;
static void el_make_owners() {
    if (g_el_owner_made) return;
    g_el_owner_made = true;
    memset(g_el_owner, 0, sizeof(g_el_owner));
    for (int id = g_el_count - 2; id >= 1; id--) { int r = el_effect(id); if (r > 0 && r < 8192) g_el_owner[r] = (short)id; }
}
static bool el_hold(ULONG_PTR mgr, int req) {                    // ask for an effect once per match; true when it is loaded
    bool held = false;
    for (int i = 0; i < g_el_nheld; i++) held |= g_el_held[i] == req;
    if (!held && g_el_nheld < 64) {
        typedef INT64(__fastcall *PFN_FX_LOAD3)(ULONG_PTR, int, int);
        ((PFN_FX_LOAD3)(g_base + 0x189e80))(mgr, req, 0);
        g_el_held[g_el_nheld++] = req;
    }
    return el_fx_kind(req) == 3;
}
static bool g_el_cfg_loaded;
static void el_cfg_save() {
    wchar_t p[MAX_PATH]; swprintf_s(p, MAX_PATH, L"%sCwoEffectLab.txt", g_dir);
    FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"w") == 0 && f) {
        fprintf(f, "skill %ld\nmotion %ld\nloop %ld\ngrid %ld\nwhite %ld\ntarget %ld\npart %ld\nmotfire %ld\nprerender %ld\nwind %ld\nwindx %ld\nwindy %ld\nwindz %ld\n",
                (long)g_el_skill, (long)g_el_motion, (long)g_el_loop, (long)g_el_grid, (long)g_el_white, (long)g_el_dummy, (long)g_el_part,
                (long)g_el_mot_fire, (long)g_el_prerender, (long)g_el_wind_s, (long)g_el_wind[0], (long)g_el_wind[1], (long)g_el_wind[2]);
        fclose(f);
    }
}
static void el_cfg_load() {
    if (g_el_cfg_loaded) return;
    g_el_cfg_loaded = true;
    wchar_t p[MAX_PATH]; swprintf_s(p, MAX_PATH, L"%sCwoEffectLab.txt", g_dir);
    FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"r") != 0 || !f) return;
    char k[32]; long v;
    while (fscanf_s(f, "%31s %ld", k, (unsigned)sizeof(k), &v) == 2) {
        if (!strcmp(k, "skill") && v > 0 && v < 4096) g_el_skill = v;
        if (!strcmp(k, "motion") && v >= 0 && v < 130) g_el_motion = v;
        if (!strcmp(k, "loop")) g_el_loop = v != 0;
        if (!strcmp(k, "grid")) g_el_grid = v != 0;
        if (!strcmp(k, "white")) g_el_white = v != 0;
        if (!strcmp(k, "target")) g_el_dummy = v != 0;
        if (!strcmp(k, "part") && v >= -1 && v <= 5) g_el_part = v;
        if (!strcmp(k, "motfire")) g_el_mot_fire = v != 0;
        if (!strcmp(k, "prerender")) g_el_prerender = v != 0;
        if (!strcmp(k, "wind") && v >= 0 && v <= 1000) g_el_wind_s = v;
        if (!strcmp(k, "windx") && v >= 0 && v <= 100) g_el_wind[0] = v;
        if (!strcmp(k, "windy") && v >= 0 && v <= 100) g_el_wind[1] = v;
        if (!strcmp(k, "windz") && v >= 0 && v <= 100) g_el_wind[2] = v;
    }
    fclose(f);
}
static int g_el_map_set;                                          // the lab suspended those groups (put back when it ends)
static ULONGLONG g_el_last_motion;
static int g_el_type_was = -1, g_el_cpu_was = -1;
static int g_el_slot_skill[4] = {-1, -1, -1, -1}, g_el_slot_req[4];   // what the lab put in each hand slot (and loaded)
static int g_el_brain;                                           // the target's own brain value while it stands
static ULONG_PTR g_el_target;
static bool g_el_was_in;
static ULONGLONG g_el_last_fire;
static char g_el_note[96];
struct ElPart { unsigned h; int req; ULONGLONG t0; };
static ElPart g_el_parts[24];
static int g_el_np;
static float g_el_pos[4];

static int el_count() {
    __try { char *h = *(char **)(g_base + 0x4bdc40); int n = h ? *(int *)(h + 4) : 0; return n > 2 && n < 4096 ? n : 375; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 375; }
}
static char *el_rec(int id) {                                   // the live skill table (0x90 per record)
    char *t = NULL;
    __try { t = *(char **)(g_base + 0x4bdc50); } __except (EXCEPTION_EXECUTE_HANDLER) { t = NULL; }
    return t && id >= 0 && id < g_el_count ? t + 0x90 * (ULONG_PTR)id : NULL;
}
static int el_effect(int id) {                                  // the skill's effect request, 0 = none (FUN_1401272b0)
    char *r = el_rec(id);
    if (!r) return 0;
    typedef short(__fastcall *PFN_EOF)(int, int);
    __try {
        short prof = *(short *)(r + 0x14);
        if (prof <= 0) return 0;
        return ((PFN_EOF)(g_base + 0x1272b0))(prof, id);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
static int el_step_skill(int from, int d) {                     // the next skill that has an effect
    int n = g_el_count, id = from;
    for (int k = 0; k < n; k++) {
        id += d;
        if (id < 1) id = n - 2;
        if (id > n - 2) id = 1;
        if (el_effect(id) > 0) return id;
    }
    return from;
}
static int el_fx_kind(int req) {                                // the loader entry of a request: 3 loaded, 2 loading, 0 free, -1 none
    __try {
        ULONG_PTR ld = *(ULONG_PTR *)(g_base + RVA_FX_LOADER);
        char *ent = ld ? *(char **)(ld + 0xC0) : NULL;
        unsigned cnt = ld ? *(unsigned *)(ld + 0xC8) : 0;
        for (unsigned e = 0; ent && e < cnt && e < 2048; e++) if (*(int *)(ent + 0x88 * e + 8) == req) return *(short *)(ent + 0x88 * e);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return -1;
}

// the Lab row (script thread = game thread): 1 = go into Single Play (the script then does the main menu's own sequence)
static INT64 el_native(int op) {
    if (op == 127) return 0;                                    // (the first build's page switch: nothing to hide now)
    if (g_in_match) { set_status("Effect Lab: open it from a Lab menu, not in a match"); return 0; }
    if (ed_online()) {
        set_status("Effect Lab runs offline: open it from the Split Screen or mission Lab");
        logf("Effect Lab: asked from an online Lab - not opened (the test match is a local one)");
        return 0;
    }
    if (!g_lob_loaded) lob_load();
    el_cfg_load();
    if (!g_el) { g_el_type_was = g_sp_type; g_el_cpu_was = g_lob[1].cpu; }
    g_sp_type = SP_ONE;
    g_lob[0].cpu = 0; g_lob[1].cpu = 1;
    InterlockedExchange(&g_el, 1);
    InterlockedExchange(&g_ed_direct, ED_GO_SINGLE);            // as the main menu's SINGLE PLAY (op 47 + op 1)
    InterlockedExchange(&g_forge_seen_sp, 0);
    InterlockedExchange(&g_forge_session, 1);
    InterlockedExchange(&g_ed_wait_release, 1);
    InterlockedExchange(&g_ed_open, 1);
    InterlockedExchange(&g_ed_mode, 0);
    edm_session_start();
    logf("Effect Lab: asked from the Lab - into Single Play, the lobby in its Effect Lab form");
    return 1;
}
static void el_off(const char *why) {                           // the lobby is the player's own again
    if (!g_el) return;
    InterlockedExchange(&g_el, 0); InterlockedExchange(&g_el_panel, 0);
    if (g_el_type_was >= 0) g_sp_type = g_el_type_was;
    if (g_el_cpu_was >= 0) g_lob[1].cpu = g_el_cpu_was;
    g_el_type_was = g_el_cpu_was = -1;
    lob_save();
    logf("Effect Lab: off (%s)", why);
}

static void el_release_slot(int s) {                            // the lab's holder of that slot's effect goes back
    if (g_el_slot_skill[s] < 0) return;
    typedef void(__fastcall *PFN_REL)(int);
    __try { if (g_el_slot_req[s] > 0) ((PFN_REL)(g_base + 0x18d280))(g_el_slot_req[s]); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    g_el_slot_skill[s] = -1; g_el_slot_req[s] = 0;
}

static void el_step() {                                         // game thread (CwoOverlayFrame)
    if (!g_el) { g_el_was_in = false; return; }
    bool in = g_in_match && !g_forge_session;
    __try {
        if (!in) {
            if (g_el_was_in) {                                   // the match ended: nothing of it is kept (the game freed its effects)
                for (int s = 0; s < 4; s++) { g_el_slot_skill[s] = -1; g_el_slot_req[s] = 0; }
                g_el_np = 0; g_el_target = 0; g_hide_hud = false;
                if (g_el_map_set) { for (int k : {0, 5}) { unsigned *f = cc_group_flags(k); if (f) *f &= ~0x20000u; } g_el_map_set = 0; }
                InterlockedExchange(&g_el_map, 1);
                InterlockedExchange(&g_el_panel, 0); InterlockedExchange(&g_el_fire, 0);
                el_cfg_save();
                g_el_part_h = 0xFFFFFFFFu; g_el_part_req = g_el_part_ld = 0;     // the game freed its effects
                g_el_nheld = 0; g_el_nhs = 0; g_el_hreq = 0; g_el_owner_made = false;
                if (g_el_paused || g_el_speed < 1.0f) { ph_end(); g_el_paused = false; g_el_speed = 1.0f; }
                __try {
                    ULONG_PTR mg = *(ULONG_PTR *)(g_base + 0x5bb2e0);
                    if (mg && g_el_wind_set) *(int *)(mg + 0x11594) = 1;           // the stage's wind again
                    if (mg && g_el_pre_set) *(int *)(mg + 0x11590) = 1;
                } __except (EXCEPTION_EXECUTE_HANDLER) {}
                g_el_wind_set = g_el_pre_set = false;
                logf("Effect Lab: the match ended");
            }
            g_el_was_in = false;
            return;
        }
        ULONG_PTR *acts = cwo_actors();
        ULONG_PTR me = acts[0], tg = acts[1];
        if (!me) return;
        ULONGLONG now = GetTickCount64();
        if (!g_el_was_in) {
            g_el_was_in = true;
            g_el_count = el_count();
            if (el_effect(g_el_skill) <= 0) InterlockedExchange(&g_el_skill, el_step_skill(g_el_skill, 1));
            el_make_owners();
            InterlockedExchange(&g_el_panel, 1);
            g_el_target = 0; g_el_note[0] = 0;
            logf("Effect Lab: in the match (%d skills, player %p, target %p)", g_el_count, (void *)me, (void *)tg);
        }
        if (*(int *)(g_base + 0x4bc950) != 6) {                  // not the fight (the countdown / the result)
            static ULONGLONG tp;                                 // a reload: the result screens are passed for the player
            if (g_el_restart == 1 && now - tp > 1200) { f_press(0x4, 150); tp = now; }
            return;
        }
        // the target: stands (its brain kept aside) or fights
        if (tg && *(int *)(tg + 0x30) == 0) {
            if (g_el_target != tg) { g_el_target = tg; g_el_brain = 0; }
            int br = *(int *)(tg + 0x142d0);
            if (!g_el_dummy && br) { g_el_brain = br; *(int *)(tg + 0x142d0) = 0; logf("Effect Lab: the target stands (brain %d kept aside)", br); }
            if (g_el_dummy && !br && g_el_brain) { *(int *)(tg + 0x142d0) = g_el_brain; logf("Effect Lab: the target fights (brain back)"); }
        }
        if (g_el_refill) {                                      // nobody runs out: life back to full, your aura full
            for (int i = 0; i < 2; i++) {
                ULONG_PTR a = acts[i];
                if (!a) continue;
                int full = *(int *)(a + 0x54);
                if (full > 0 && full <= 99 && *(int *)(a + 0x4C) > 0 && *(int *)(a + 0x4C) < full) { *(int *)(a + 0x4C) = full; *(int *)(a + 0x50) = full; }
            }
            if (*(int *)(me + 0x58) < 10) *(int *)(me + 0x58) = 10;
            if (*(float *)(me + 0x5C) < 10.0f) *(float *)(me + 0x5C) = 10.0f;
        }
        { int full = *(int *)(g_base + 0x4bc638), *clock = (int *)(g_base + 0x4bc8c0);   // no time limit (ed_clock_hold's fact)
          if (full > 600 && *clock >= 300 && *clock < full) *clock = full; }
        g_hide_hud = g_el_panel != 0;                            // the list replaces the battle HUD while it is open
        memcpy(g_el_pos, (const void *)(me + ACTOR_POS), 12);
        g_el_pos[3] = (float)(*(unsigned short *)(me + 0x12E68)) * 360.0f / 65536.0f;
        // wind and pre-render on the effect manager (pdpEffect rows 12-15 and 7)
        {
            FxGame wg;
            if (fx_game(&wg)) {
                ULONG_PTR mg = wg.mgr;
                if (g_el_wind_s > 0) {
                    float d[3] = {(float)(g_el_wind[0] - 50), (float)(g_el_wind[1] - 50), (float)(g_el_wind[2] - 50)};
                    float l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                    float st = l > 0.0f ? (float)g_el_wind_s * 0.1f : 0.0f;
                    *(int *)(mg + 0x11594) = 0;                   // stop following the stage's wind
                    for (int k = 0; k < 3; k++) {
                        float u = l > 0.0f ? d[k] / l : 0.0f;
                        *(float *)(mg + 0x11598 + 4 * k) = u;
                        *(float *)(mg + 0x115a8 + 4 * k) = u * st;
                    }
                    *(float *)(mg + 0x115a4) = st;
                    if (!g_el_wind_set) logf("Effect Lab: wind %ld toward (%ld %ld %ld)", (long)g_el_wind_s, (long)g_el_wind[0] - 50, (long)g_el_wind[1] - 50, (long)g_el_wind[2] - 50);
                    g_el_wind_set = true;
                } else if (g_el_wind_set) { *(int *)(mg + 0x11594) = 1; g_el_wind_set = false; logf("Effect Lab: wind back to the stage's"); }
                int pr = g_el_prerender ? 1 : 0;
                if (*(int *)(mg + 0x11590) != pr) { *(int *)(mg + 0x11590) = pr; g_el_pre_set = true; logf("Effect Lab: pre-render %s", pr ? "ON" : "OFF"); }
            }
        }
        // the pace (pdpEffect's RTRIGGER): LT held = frozen, a RT press then = one step; else RT slows (100 / 50 / 25 / 10 %)
        if (!g_fc_on) {
            float lt = g_el_lt / 1000.0f, rt = g_el_rt / 1000.0f;
            static bool rt_was;
            bool rt_new = rt > 0.5f && !rt_was;
            rt_was = rt > 0.5f;
            bool pause = lt > 0.9f;
            int sp = rt < 0.15f ? 0 : rt < 0.5f ? 1 : rt < 0.85f ? 2 : 3;
            // v5.198.2 (in game: a step released for one drawn frame did nothing at 60 fps - no game tick ran in it): a RT press while
            // frozen lets the world run until the game's tick counter (*0x14052dd6c, FUN_1401c7630 +1 each) has moved on by one
            static int step_tick = -1;
            int tick = *(int *)(g_base + 0x52dd6c);
            if (pause && rt_new) step_tick = tick;
            if (step_tick >= 0 && (tick != step_tick || !pause)) step_tick = -1;
            if (pause) ph_want(step_tick < 0, 0); else ph_want(false, sp);
            if (pause || sp || g_el_paused || g_el_speed < 1.0f) ph_step(false, 0);
            g_el_paused = pause; g_el_speed = pause ? 0.0f : (sp == 0 ? 1.0f : sp == 1 ? 0.5f : sp == 2 ? 0.25f : 0.1f);
        }
        // one PART (pdpEffect's BaseIndex fire) of the skill's effect, or any effect of the EFFECTS list (every part when ALL),
        // asked for, then started at your feet facing your way
        if (g_el_fire && (g_el_part >= 0 || g_el_page == 3)) {
            FxGame pg;
            int req = g_el_page == 3 ? el_ei_req(g_el_fxi) : el_effect(g_el_skill);
            if (req <= 0 || !fx_game(&pg)) { InterlockedExchange(&g_el_fire, 0); sprintf_s(g_el_note, "No effect to show"); }
            else if (el_hold(pg.mgr, req)) {
                for (int k = 0; k < g_el_nhs; k++) if (fx_alive(pg, g_el_hs[k])) pg.stop(g_el_hs[k]);
                g_el_nhs = 0;
                static __declspec(align(16)) float M[16];
                memcpy(M, pg.identity, sizeof(M));
                memcpy(M + 12, (const float *)(me + ACTOR_POS), 12);
                pg.turn(M, (unsigned)(*(int *)(me + ACTOR_FACING) + 0x4000));
                int k0 = g_el_part >= 0 ? g_el_part : 0, k1 = g_el_part >= 0 ? g_el_part : 7;
                for (int k = k0; k <= k1 && g_el_nhs < 8; k++) {
                    unsigned h = pg.start(pg.mgr, *(int *)(me + 0x14), req, k, M, 0);
                    if (h == FX_NONE) continue;
                    if (g_el_loop) pg.loop(h);
                    g_el_hs[g_el_nhs++] = h;
                }
                InterlockedExchange(&g_el_fire, 0);
                g_el_last_fire = now; g_el_hreq = req; g_el_part_req = req;
                if (!g_el_nhs) sprintf_s(g_el_note, "Effect %d has no part to start here", req);
                else {
                    g_el_note[0] = 0;
                    if (g_el_mot_fire) InterlockedExchange(&g_el_mfire, 1);
                }
                logf("Effect Lab: effect %d - %d part(s) started (%s%s)", req, g_el_nhs, g_el_part >= 0 ? EL_PART[g_el_part] : "every part", g_el_loop ? ", looping" : "");
            }
        }
        if (g_el_stopall) {                                      // the alpha's stop-all: the lab's parts off, the motion back to standing
            InterlockedExchange(&g_el_stopall, 0);
            FxGame sg;
            if (fx_game(&sg)) for (int k = 0; k < g_el_nhs; k++) if (fx_alive(sg, g_el_hs[k])) sg.stop(g_el_hs[k]);
            g_el_nhs = 0;
            typedef void(__fastcall *PFN_EL_MOT0)(ULONG_PTR, int, int, float, int, float, float);
            if (*(int *)(me + 0x12f04) == 0) ((PFN_EL_MOT0)(g_base + 0x257310))(me + 0x12f00, 0, 0, *(float *)(g_base + 0x2f7f18), 1, -1.0f, -1.0f);
            logf("Effect Lab: every lab effect stopped");
        }
        // the camera mode row: the free camera on / off
        { static bool fc_was;                                     // the free camera's own B / View + RB ends CAMERA MODE too
          if (fc_was && !g_fc_on && g_el_camera) { InterlockedExchange(&g_el_camera, 0); logf("Effect Lab: player move mode"); }
          fc_was = g_fc_on != 0; }
        if (g_el_camera && !g_fc_on && !g_fc_req) InterlockedExchange(&g_fc_req, 1);
        if (!g_el_camera && g_fc_on && !g_fc_req) InterlockedExchange(&g_fc_req, 1);
        // the stage shown / hidden
        if (!g_el_map && !g_el_map_set) { for (int k : {0, 5}) { unsigned *f = cc_group_flags(k); if (f) *f |= 0x20000u; } g_el_map_set = 1; logf("Effect Lab: the stage is hidden"); }
        if (g_el_map && g_el_map_set) { for (int k : {0, 5}) { unsigned *f = cc_group_flags(k); if (f) *f &= ~0x20000u; } g_el_map_set = 0; logf("Effect Lab: the stage is shown"); }
        // a motion: the game's motion player, while the fighter stands. v5.196.1 (in game: a motion played once was back to the
        // stance within half a second - the fight's own standing logic takes the fighter back): while it runs the motion is
        // set again every frame at the frame it has reached (FUN_140257310's last two values = first and last frame, as
        // FUN_1402526c0 mode 4 passes them), for its own length (+0x13014 + slot * 0x10 = its frames)
        {
            static int run_slot = -1; static ULONGLONG run_t0; static int run_len;
            typedef void(__fastcall *PFN_EL_MOT)(ULONG_PTR, int, int, float, int, float, float);
            bool standing = *(int *)(me + 0x12f04) == 0 && *(int *)(me + 0x1396c) == -1 && *(int *)(me + 0x13968) == 0;
            if ((g_el_mfire || (g_el_mloop && g_el_page == 1 && run_slot < 0 && now - g_el_last_motion > 800)) && standing) {
                run_slot = g_el_motion; run_t0 = now;
                run_len = *(int *)(me + 0x13014 + (ULONG_PTR)run_slot * 0x10);
                if (run_len < 1 || run_len > 3000) run_len = 60;
                ((PFN_EL_MOT)(g_base + 0x257310))(me + 0x12f00, run_slot, 0, *(float *)(g_base + 0x2f7f18), 1, -1.0f, -1.0f);
                if (InterlockedExchange(&g_el_mfire, 0)) logf("Effect Lab: motion %d %s played (%d frames)", run_slot, ED_ANIM[run_slot], run_len);
            } else if (run_slot >= 0) {
                float fr = (float)(now - run_t0) * 30.0f / 1000.0f;
                if (!standing || fr >= (float)run_len || g_el_fire) { run_slot = -1; g_el_last_motion = now; }
                else ((PFN_EL_MOT)(g_base + 0x257310))(me + 0x12f00, run_slot, 0, 0.0f, 1, fr, (float)run_len);
            }
        }
        // fire: the highlighted skill into the chosen slot (its effect loaded first), then the game's own queued use
        bool want = g_el_part < 0 && g_el_page != 3 && (g_el_fire || (g_el_loop && now - g_el_last_fire > 3500));
        if (want) {
            int s = g_el_slot & 3, skill = g_el_skill, req = el_effect(skill);
            char *sl = sc_hand_slot(me, s);
            if (req <= 0) { InterlockedExchange(&g_el_fire, 0); sprintf_s(g_el_note, "That skill has no effect to show"); }
            else if (*(int *)(me + SCP_USE_REQ) != -1 && g_el_last_fire && now - g_el_last_fire > 1500) {
                // the game did not take the queued use (a skill that works by itself: an auto shield, a counter): taken back
                *(int *)(me + SCP_USE_REQ) = -1;
                sprintf_s(g_el_note, "That one works by itself (not fired)");
                logf("Effect Lab: the queued use of slot was not taken by the game in 1.5 s - cleared");
            }
            else if (!sc_fighter_free(me)) { /* still in a move: next frame */ }
            else if (g_el_slot_skill[s] != skill || !sc_slot_has(sl, skill)) {
                if (*(int *)(sl + 0x3c) != -1 || *(int *)(sl + 0x40) != -1) { /* that card is in use: wait */ }
                else {
                    if (g_el_slot_skill[s] != skill) {            // a new skill for the slot: the old holder back, the new effect asked for
                        el_release_slot(s);
                        typedef INT64(__fastcall *PFN_FX_LOAD)(ULONG_PTR, int, int);
                        FxGame g;
                        if (fx_game(&g)) ((PFN_FX_LOAD)(g_base + 0x189e80))(g.mgr, req, 0);
                        g_el_slot_skill[s] = skill; g_el_slot_req[s] = req;
                        logf("Effect Lab: slot %d <- %d '%s' (effect %d asked)", s + 1, skill, sc_skill_name(skill), req);
                    }
                    if (el_fx_kind(req) == 3) {
                        if (!sc_capture(sl, skill)) sprintf_s(g_el_note, "Slot %d is busy - try again", s + 1);
                    }
                }
            } else if (el_fx_kind(req) == 3) {
                *(int *)(me + SCP_USE_REQ) = s;
                g_el_last_fire = now; g_el_note[0] = 0;
                InterlockedExchange(&g_el_fire, 0);
                logf("Effect Lab: fired %d '%s' from slot %d (effect %d)", skill, sc_skill_name(skill), s + 1, req);
            }
        }
        // the parts playing: every live instance of the four slots' effects, with its frames
        FxGame g;
        if (fx_game(&g)) {
            ElPart np[24]; int nn = 0;
            for (int i = 0; i < 256 && nn < 24; i++) {
                unsigned char *sl = (unsigned char *)(g.mgr + 0x60 + (ULONG_PTR)i * 0xF0);
                unsigned h = *(unsigned *)sl;
                if (!h || h == 0xFFFFFFFFu || (h & 0xFFF) != (unsigned)i) continue;
                int rq = *(int *)(sl + 0x20);
                bool mine = false;
                for (int s = 0; s < 4; s++) mine |= g_el_slot_skill[s] >= 0 && g_el_slot_req[s] == rq;
                mine |= g_el_part_req > 0 && rq == g_el_part_req;
                mine |= g_el_hreq > 0 && rq == g_el_hreq;
                if (!mine) continue;
                ULONGLONG t0 = now;                              // when the part was first seen (this step runs more than once a frame)
                for (int k = 0; k < g_el_np; k++) if (g_el_parts[k].h == h) t0 = g_el_parts[k].t0;
                np[nn++] = {h, rq, t0};
            }
            memcpy(g_el_parts, np, sizeof(np)); g_el_np = nn;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { logf("Effect Lab: fault in the match step - skipped"); }
}

// pad thread, in the Effect Lab match. true = the press was the lab's (nothing else looks at it)
static bool el_input(WORD b, WORD pressed) {
    (void)b;
    if (pressed & BTN_BACK) { InterlockedExchange(&g_el_panel, !g_el_panel); rumble(1); return true; }
    if (pressed & BTN_RTHUMB) { InterlockedExchange(&g_fc_req, 1); rumble(1); return true; }   // the free camera (its own keys while on)
    if (!g_el_panel) return false;                               // play: every other button is the game's (and RB + D-pad the emotes)
    if (pressed & BTN_LTHUMB) { static const LONG NX[4] = {3, 2, 0, 1}; InterlockedExchange(&g_el_page, NX[g_el_page & 3]); rumble(1); return true; }
    if (g_el_page == 3) {                                        // EFFECTS: any effect of the game
        int n = el_ei_count(), i = g_el_fxi;
        if (n > 0) {
            if (pressed & BTN_UP) i -= 1;
            if (pressed & BTN_DOWN) i += 1;
            if (pressed & BTN_LEFT) i -= 10;
            if (pressed & BTN_RIGHT) i += 10;
            if (pressed & BTN_LB) i -= 100;
            if (pressed & BTN_RB) i += 100;
            InterlockedExchange(&g_el_fxi, ((i % n) + n) % n);
        }
        if (pressed & BTN_A) InterlockedExchange(&g_el_fire, 1);
        if (pressed & BTN_X) InterlockedExchange(&g_el_stopall, 1);
        if (pressed & BTN_Y) InterlockedExchange(&g_el_loop, !g_el_loop);
        if (pressed & BTN_B) InterlockedExchange(&g_el_dummy, !g_el_dummy);
        return true;
    }
    if (g_el_page == 2) {                                        // SETUP: Up / Down a row, Left / Right its value, A reload
        int r = g_el_row;
        if (pressed & BTN_UP) r = (r + EL_ROWS - 1) % EL_ROWS;
        if (pressed & BTN_DOWN) r = (r + 1) % EL_ROWS;
        InterlockedExchange(&g_el_row, r);
        int d = (pressed & BTN_LEFT) ? -1 : (pressed & BTN_RIGHT) ? 1 : 0;
        if (d) {
            if (r == 0 && g_lob_nch > 0) { InterlockedExchange(&g_lob_sel, 0); lob_change(2, d); InterlockedExchange(&g_el_pending, 1); }
            if (r == 1) { lob_change(0, d); InterlockedExchange(&g_el_pending, 1); }
            if (r == 2) InterlockedExchange(&g_el_map, !g_el_map);
            if (r == 3) InterlockedExchange(&g_el_grid, !g_el_grid);
            if (r == 4) InterlockedExchange(&g_el_white, !g_el_white);
            if (r == 5) InterlockedExchange(&g_el_loop, !g_el_loop);
            if (r == 6) InterlockedExchange(&g_el_dummy, !g_el_dummy);
            if (r == 7) InterlockedExchange(&g_el_part, ((g_el_part + 1 + d) % 7 + 7) % 7 - 1);
            if (r == 8) InterlockedExchange(&g_el_mot_fire, !g_el_mot_fire);
            if (r == 9) InterlockedExchange(&g_el_prerender, !g_el_prerender);
            if (r == 10) { int v = g_el_wind_s + d * 50; InterlockedExchange(&g_el_wind_s, v < 0 ? 0 : v > 1000 ? 1000 : v); }
            if (r == 14) InterlockedExchange(&g_el_camera, !g_el_camera);
            if (r >= 11 && r <= 13) { int v = g_el_wind[r - 11] + d * 5; InterlockedExchange(&g_el_wind[r - 11], v < 0 ? 0 : v > 100 ? 100 : v); }
        }
        if ((pressed & BTN_A) && g_el_pending && !g_el_restart) {
            InterlockedExchange(&g_el_pending, 0);
            lob_save();
            InterlockedExchange(&g_el_restart, 1);
            InterlockedExchange(&g_fexit_giveup, 1);            // the game's own Give Up (ed_giveup_step)
            set_status("Effect Lab: reloading with the new character / map ...");
            logf("Effect Lab: reload asked (character / map changed)");
        }
        return true;
    }
    if (g_el_page == 1) {                                        // MOTIONS
        int m = g_el_motion;
        if (pressed & BTN_UP) m = (m + 129) % 130;
        if (pressed & BTN_DOWN) m = (m + 1) % 130;
        if (pressed & BTN_LEFT) m = (m + 120) % 130;
        if (pressed & BTN_RIGHT) m = (m + 10) % 130;
        InterlockedExchange(&g_el_motion, m);
        if (pressed & BTN_A) InterlockedExchange(&g_el_mfire, 1);
        if (pressed & BTN_Y) InterlockedExchange(&g_el_mloop, !g_el_mloop);
        if (pressed & BTN_B) InterlockedExchange(&g_el_map, !g_el_map);
        if (pressed & BTN_X) InterlockedExchange(&g_el_dummy, !g_el_dummy);
        return true;
    }
    if (pressed & BTN_UP) InterlockedExchange(&g_el_skill, el_step_skill(g_el_skill, -1));
    if (pressed & BTN_DOWN) InterlockedExchange(&g_el_skill, el_step_skill(g_el_skill, 1));
    if (pressed & (BTN_LEFT | BTN_RIGHT)) {
        int id = g_el_skill, d = (pressed & BTN_RIGHT) ? 1 : -1;
        for (int k = 0; k < 10; k++) id = el_step_skill(id, d);
        InterlockedExchange(&g_el_skill, id);
    }
    if (pressed & (BTN_LB | BTN_RB)) {                            // the next school
        int id = g_el_skill, d = (pressed & BTN_RB) ? 1 : -1;
        char *r0 = el_rec(id); int sch = r0 ? *(short *)(r0 + 0x12) : -1;
        for (int k = 0; k < g_el_count; k++) { id = el_step_skill(id, d); char *r = el_rec(id); if (r && *(short *)(r + 0x12) != sch) break; }
        InterlockedExchange(&g_el_skill, id);
    }
    if (pressed & BTN_A) InterlockedExchange(&g_el_fire, 1);
    if (pressed & BTN_X) InterlockedExchange(&g_el_slot, (g_el_slot + 1) & 3);
    if (pressed & BTN_Y) InterlockedExchange(&g_el_loop, !g_el_loop);
    if (pressed & BTN_B) InterlockedExchange(&g_el_dummy, !g_el_dummy);
    return true;
}

static void el_draw() {                                         // render thread: the panel on its own dark box
    if (!g_el || !g_in_match || g_forge_session) return;
    __try { if (*(int *)(g_base + 0x4bc950) != 6 || el_game_menu()) return; } __except (EXCEPTION_EXECUTE_HANDLER) { return; }   // the fight only
    int glyphs = *(int *)(g_base + RVA_GLYPHS), cap = *(int *)(g_base + RVA_GLYPH_CAP);
    if (glyphs > cap - 640) return;
    if (*(int *)(g_base + RVA_OFS_X) != 0 || *(int *)(g_base + RVA_OFS_Y) != 0) return;
    int *space = (int *)(g_base + RVA_FONT_SPACE), *gap = (int *)(g_base + RVA_FONT_GAP);
    int *fh = (int *)(g_base + RVA_FONT_H), *fw = (int *)(g_base + RVA_FONT_W), *x0 = (int *)(g_base + RVA_FONT_X0);
    DWORD *rgba = (DWORD *)(g_base + RVA_FONT_RGBA);
    int s_space = *space, s_gap = *gap, s_h = *fh, s_w = *fw, s_x0 = *x0;
    DWORD s_rgba[4];
    memcpy(s_rgba, rgba, sizeof(s_rgba));
    PFN_TEXT text = (PFN_TEXT)(g_base + RVA_TEXT);
    const DWORD GOLD = 0xFFFFD040, WHITE = 0xFFFFFFFF, BLUE = 0xFFA0E0FF, GREEN = 0xFF80FF80, GREY = 0xFFB0B0B0;
    auto tiny = [&]() { *space = 0; *gap = 1; *fh = 11; *fw = 5; };
    auto box = [&](int px, int py, int w, int h, DWORD col) {     // a filled box: the square glyph (Shift-JIS 81 A1) draws about
        int gw = (int)(w / 1.55f), gh = (int)(h / 0.70f);          // 1.55 x its width and 0.70 x its height, from 0.04 h down
        *space = 0; *gap = 0; *fh = gh; *fw = gw;                  // (measured in game 2026-11-22, shot d1)
        int gy = py - (int)(0.04f * gh);
        set_rgba(rgba, col); text(px < 2 ? 2 : px, gy < 2 ? 2 : gy, "\x81\xa1");
        tiny();
    };
    auto put = [&](int px, int py, const char *s, DWORD col) { set_rgba(rgba, col); text(px, py, s); };
    char line[112];
    __try {
        tiny();
        if (g_fc_on) {                                           // CAMERA MODE (pdpEffect): the camera's Pos / Rot
            box(40, 455, 600, 20, 0xD0000810);
            sprintf_s(line, "CAMERA MODE  Pos (%.0f %.0f %.0f)  Rot (%.0f %.0f)   sticks move   B: player move mode",
                      g_fc_pos[0], g_fc_pos[1], g_fc_pos[2], g_fc_yaw * 57.2958f, g_fc_pitch * 57.2958f);
            put(50, 461, line, GOLD);
            memcpy(rgba, s_rgba, sizeof(s_rgba));
            *space = s_space; *gap = s_gap; *fh = s_h; *fw = s_w; *x0 = s_x0;
            return;
        }
        if (g_el_grid) {                                         // the floor grid: points every 2 m around the player
            const float *V = (const float *)(g_base + 0x5a5ca0);
            const float cot = 1.0f / tanf(1.309f * 0.5f), asp = 853.0f / 480.0f;
            float bx = floorf(g_el_pos[0] / 20.0f) * 20.0f, bz = floorf(g_el_pos[2] / 20.0f) * 20.0f, by = g_el_pos[1];
            *fh = 9; *fw = 5;
            set_rgba(rgba, g_el_white ? 0xFF000000 : 0xFF40FF80);
            for (int ix = -5; ix <= 5; ix++)
                for (int iz = -5; iz <= 5; iz++) {
                    float px = bx + ix * 20.0f, pz = bz + iz * 20.0f;
                    float cx = px * V[0] + by * V[4] + pz * V[8] + V[12], cy = px * V[1] + by * V[5] + pz * V[9] + V[13];
                    float cz = px * V[2] + by * V[6] + pz * V[10] + V[14];
                    if (cz < 5.0f) continue;
                    int sx = (int)(320.0f + cx * cot / cz * 320.0f), sy = (int)(240.0f - cy * asp * cot / cz * 240.0f);
                    if (sx < 4 || sx > 630 || sy < 4 || sy > 470) continue;
                    text(sx - 2, sy - 4, "+");
                }
            tiny();
        }
        if (!g_el_panel) {                                       // play: one quiet line
            box(40, 455, 300, 20, 0xB0000000);
            put(50, 461, "EFFECT LAB  View: lists  R3: free camera  RB + D-pad: poses", GOLD);
        } else {                                                 // the game's text queue holds 1000 glyphs: the panel stays under ~480
            const int X = 22, Y = 30, W = 200, LN = 13;
            box(X - 8, Y - 6, W + 20, 330, 0xD0000810);
            put(X, Y, "EFFECT LAB", GOLD);
            if (g_el_page == 3) {                                // EFFECTS: every effect of the game's index
                put(X + 70, Y, "- EFFECTS", GOLD);
                int n = el_ei_count(), cur = g_el_fxi;
                for (int k = 0; k < 9 && n > 0; k++) {
                    int i = ((cur - 4 + k) % n + n) % n, rq = el_ei_req(i);
                    int ow = rq > 0 && rq < 8192 ? g_el_owner[rq] : 0;
                    sprintf_s(line, "%s%5d %4uK %.15s", i == cur ? ">" : " ", rq, el_ei_span(i) / 1024, ow ? sc_skill_name(ow) : "-");
                    put(X, Y + 16 + k * LN, line, i == cur ? GOLD : WHITE);
                }
                int y = Y + 16 + 9 * LN + 6;
                sprintf_s(line, "%d of %d effects", cur + 1, n); put(X, y, line, GREY); y += LN;
                sprintf_s(line, "Part: %s", g_el_part < 0 ? "ALL" : EL_PART[g_el_part]); put(X, y, line, BLUE); y += LN;
                put(X, y, "PARTS PLAYING", GOLD); y += LN;
                if (!g_el_np) { put(X, y, " none - A plays it", GREY); y += LN; }
                for (int i = 0; i < g_el_np && i < 5; i++, y += LN) {
                    int tr = (g_el_parts[i].h >> 12) & 0xF;
                    sprintf_s(line, " %d %s %d f", tr, tr < 6 ? EL_PART[tr] : "PART", (int)((GetTickCount64() - g_el_parts[i].t0) * 30 / 1000));
                    put(X, y, line, GREEN);
                }
                sprintf_s(line, "Loop %s  %s", g_el_loop ? "ON" : "OFF", g_el_paused ? "PAUSED" : g_el_speed < 1.0f ? "SLOW" : "");
                put(X, y + 2, line, BLUE);
                if (g_el_note[0]) put(X, y + 2 + LN, g_el_note, GOLD);
                box(40, 455, 600, 20, 0xD0000810);
                put(50, 461, "Up/Down 1  L/R 10  LB/RB 100  A play  X stop all  Y loop  RT slow  LT freeze  L3 next", WHITE);
            } else if (g_el_page == 2) {                         // SETUP (pdpEffect's settings panel)
                put(X + 70, Y, "- SETUP", GOLD);
                char stn[64] = "";
                lob_stage_name(g_lob_stage, g_lob_var, stn, sizeof(stn));
                char wv[4][12];
                sprintf_s(wv[0], "%ld", (long)g_el_wind_s);
                for (int k = 0; k < 3; k++) sprintf_s(wv[k + 1], "%+ld", (long)g_el_wind[k] - 50);
                const char *val[EL_ROWS] = {g_lob_nch > 0 ? g_lob_ch[g_lob[0].ch].name : "-", stn, g_el_map ? "ON" : "OFF", g_el_grid ? "ON" : "OFF",
                                            g_el_white ? "WHITE" : "BLACK", g_el_loop ? "ON" : "OFF", EL_DUMMY[g_el_dummy ? 1 : 0],
                                            g_el_part < 0 ? "ALL (the skill)" : EL_PART[g_el_part], g_el_mot_fire ? ED_ANIM[g_el_motion] : "OFF",
                                            g_el_prerender ? "ON" : "OFF", wv[0], wv[1], wv[2], wv[3], g_el_camera ? "CAMERA" : "PLAYER MOVE"};
                static const char *lab[EL_ROWS] = {"Character", "Map", "Map display", "Grid display", "Background", "Force loop", "Target",
                                                   "Part", "Motion w/ fire", "Pre-render", "Wind", "Wind X", "Wind Y", "Wind Z", "Mode"};
                for (int k = 0; k < EL_ROWS; k++) {
                    sprintf_s(line, "%s%-14s %.16s", k == g_el_row ? ">" : " ", lab[k], val[k]);
                    put(X, Y + 18 + k * LN, line, k == g_el_row ? GOLD : WHITE);
                }
                int y = Y + 18 + EL_ROWS * LN + 4;
                if (g_el_pending) { put(X, y, "A: reload with these", GREEN); y += LN; }
                if (g_el_white && !g_fp_on) { put(X, y, "White needs 60 FPS on", GOLD); y += LN; }
                if (g_el_white && g_el_map) { put(X, y, "(seen with Map display OFF)", GREY); y += LN; }
                sprintf_s(line, "Pos %.0f %.0f %.0f  Rot %.0f", g_el_pos[0], g_el_pos[1], g_el_pos[2], g_el_pos[3]);
                put(X, y, line, GREY);
                box(40, 455, 600, 20, 0xD0000810);
                put(50, 461, "Up/Down row  Left/Right value  A reload (character / map)  L3 skills  R3 camera  View play", WHITE);
            } else if (g_el_page == 1) {                         // MOTIONS
                put(X + 70, Y, "- MOTIONS", GOLD);
                int cm = g_el_motion;
                for (int k = 0; k < 9; k++) {
                    int m = ((cm - 4 + k) % 130 + 130) % 130;
                    sprintf_s(line, "%s%4d %.20s", m == cm ? ">" : " ", m, ED_ANIM[m]);
                    put(X, Y + 16 + k * LN, line, m == cm ? GOLD : WHITE);
                }
                int y = Y + 16 + 9 * LN + 8;
                sprintf_s(line, "Loop %s", g_el_mloop ? "ON" : "OFF"); put(X, y, line, BLUE); y += LN;
                sprintf_s(line, "Stage %s", g_el_map ? "shown" : "hidden"); put(X, y, line, BLUE); y += LN;
                sprintf_s(line, "Target %s", EL_DUMMY[g_el_dummy ? 1 : 0]); put(X, y, line, BLUE); y += LN;
                sprintf_s(line, "Pos %.0f %.0f %.0f  Rot %.0f", g_el_pos[0], g_el_pos[1], g_el_pos[2], g_el_pos[3]);
                put(X, y, line, GREY);
                box(40, 455, 600, 20, 0xD0000810);
                put(50, 461, "D-pad motion  A play  Y loop  B stage on/off  X target  L3 skills  R3 camera  View play", WHITE);
            } else {
            put(X + 70, Y, "- SKILLS", GOLD);
            int cur = g_el_skill, ids[7], n = 0, id = cur;
            for (int k = 0; k < 3; k++) id = el_step_skill(id, -1);
            for (int k = 0; k < 7; k++) { ids[n++] = id; id = el_step_skill(id, 1); }
            for (int k = 0; k < n; k++) {
                sprintf_s(line, "%s%4d %.20s", ids[k] == cur ? ">" : " ", ids[k], sc_skill_name(ids[k]));
                put(X, Y + 16 + k * LN, line, ids[k] == cur ? GOLD : WHITE);
            }
            int y = Y + 16 + 7 * LN + 4;
            char *r = el_rec(cur);
            if (r) {
                int sch = *(short *)(r + 0x12);
                sprintf_s(line, "%s  Cost %d  STR %d  Fx %d", sch >= 0 && sch < 7 ? EL_SCHOOL[sch] : "", *(short *)(r + 0x28), *(short *)(r + 0x58), el_effect(cur));
                put(X, y, line, BLUE);
            }
            y += LN;
            {                                                    // pdpEffect's EffectFileSize / TotalFileSize
                unsigned span = 0, total = 0;
                __try {
                    unsigned char *e = fx_ei_find(el_effect(cur));
                    if (e) span = *(unsigned *)(e + 8);
                    ULONG_PTR ld = *(ULONG_PTR *)(g_base + RVA_FX_LOADER);
                    char *ent = ld ? *(char **)(ld + 0xC0) : NULL;
                    unsigned cnt = ld ? *(unsigned *)(ld + 0xC8) : 0;
                    for (unsigned k = 0; ent && k < cnt && k < 2048; k++)
                        if (*(short *)(ent + 0x88 * k) == 3) { unsigned char *ee = fx_ei_find(*(int *)(ent + 0x88 * k + 8)); if (ee) total += *(unsigned *)(ee + 8); }
                } __except (EXCEPTION_EXECUTE_HANDLER) {}
                sprintf_s(line, "File %u KB  Loaded %u KB", span / 1024, total / 1024);
                put(X, y, line, GREY); y += LN + 4;
            }
            sprintf_s(line, "Part: %s", g_el_part < 0 ? "ALL (the skill)" : EL_PART[g_el_part]);
            put(X, y, line, g_el_part < 0 ? BLUE : GREEN); y += LN;
            put(X, y, "HAND SLOTS", GOLD); y += LN;
            for (int s = 0; s < 4; s++, y += LN) {
                sprintf_s(line, "%s%d %.20s", s == (g_el_slot & 3) ? ">" : " ", s + 1, g_el_slot_skill[s] >= 0 ? sc_skill_name(g_el_slot_skill[s]) : "-");
                put(X, y, line, s == (g_el_slot & 3) ? GOLD : WHITE);
            }
            y += 4;
            put(X, y, "PARTS PLAYING", GOLD); y += LN;
            if (!g_el_np) put(X, y, " none - A plays it", GREY);
            for (int i = 0; i < g_el_np && i < 4; i++) {
                int tr = (g_el_parts[i].h >> 12) & 0xF;
                sprintf_s(line, " %d %s %d f", tr, tr < 6 ? EL_PART[tr] : "PART", (int)((GetTickCount64() - g_el_parts[i].t0) * 30 / 1000));
                put(X, y + i * LN, line, GREEN);
            }
            y += 4 * LN + 2;
            sprintf_s(line, "Loop %s   Speed %s", g_el_loop ? "ON" : "OFF", g_el_paused ? "PAUSED (RT: step)" : g_el_speed >= 1.0f ? "100%" : g_el_speed >= 0.5f ? "50%" : g_el_speed >= 0.25f ? "25%" : "10%");
            put(X, y, line, BLUE); y += LN;
            sprintf_s(line, "Target %s", EL_DUMMY[g_el_dummy ? 1 : 0]);
            put(X, y, line, BLUE); y += LN;
            sprintf_s(line, "Pos %.0f %.0f %.0f  Rot %.0f", g_el_pos[0], g_el_pos[1], g_el_pos[2], g_el_pos[3]);
            put(X, y, line, GREY); y += LN;
            if (g_el_note[0]) put(X, y, g_el_note, GOLD);
            box(40, 455, 600, 20, 0xD0000810);
            put(50, 461, "D-pad skill  LB/RB school  A play  X slot  Y loop  B target  RT slow  LT freeze  L3 next  View play", WHITE);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    memcpy(rgba, s_rgba, sizeof(s_rgba));
    *space = s_space; *gap = s_gap; *fh = s_h; *fw = s_w; *x0 = s_x0;
}

static int el_self_test() {                                     // the list stepping stays in range on any table
    int a = el_step_skill(45, 1), b = el_step_skill(a, -1);
    bool ok = a >= 1 && b >= 1 && el_fx_kind(-12345) == -1;
    logf("self-test: Effect Lab list %s", ok ? "ok" : "FAILED");
    return ok ? 1 : -93;
}
