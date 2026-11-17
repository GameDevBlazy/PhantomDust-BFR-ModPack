// ---- v5.28 the stage shown live behind the LEVEL LIST (owner 2026-10-27; "Right behind the level list is where the level should actually render and load up.
// That way we can have a clear visual of the actual stage instead of just the image"). Split screen's STAGE LIST (vsmenu
// page SSEL) and Xbox Live's LEVEL LIST (livestagesel page STSEL; the 2026-10-26 online / scoreboard exception, item 2,
// which the owner agreed to on 2026-10-27): the highlighted stage is loaded the way Single Play loads its stage, drawn behind
// the list with a slow camera circling it, and the striped background picture and the small preview picture are left out
// while it shows (tools/build_stage_view.py patches the two pages: their frame passes the highlighted row, op 49; their
// draw asks what to draw, op 50).
// Game facts (Ghidra full pass 065: pcode.py / pd_fn.py, capstone on PDUWP 9ba7411b, 2026-10-27; listings in
// 25_Menu_Research/stage_view_2026-10-27):
//  - Single Play's states (table 0x1403e8920): the stage list FUN_140192780 sets *0x1403e0304 = *0x1404c01d8 = stage and
//    the variant *0x1404c51f0 / *(u8 *)0x140531dcf, then calls FUN_1401d1950(*0x1404c01d0, stage); FUN_1401928c0 waits
//    while FUN_140202820() says the stage files are still loading, then calls FUN_1401d1b90(field, 0) (objects spawned,
//    bit 0x800 of *0x140530034 cleared); its arsenal and enemy lists then draw over the stage. Leaving it:
//    *0x1404c01d8 = 0x7777 and FUN_1401d1c80().
//  - the field (pdStage *0x1404c01d0, update FUN_1401d1870): +0x40 state (0 none, 1 loading, 2 loaded, 3 / 4 shown,
//    5 unloading -> 0 nine updates later), +0x50 = 1: wait to be activated (FUN_1401d1a60).
//  - loading and unloading set bit 0x20000 (suspended) on the world's task groups 9-13 and 29-31 (scheduler *0x14052dfe8,
//    +0xa0 + group * 0x10 + 8) and bit 0x800 of *0x140530034; while *(u8 *)0x1403dd3d5 is set they also fade the screen
//    to black (fader *0x14052df20).
//  - the frame FUN_1401c8110 draws the world (FUN_1401c78c0) with the view matrix 0x1405a5ca0 and rebuilds the projection
//    from its own globals; FUN_1402912d0(out, target, eye) is the game's look-at (the battle camera's 0x140584f40, eye
//    0x14058bdf0).
//  - nothing on that path lifts the groups' suspension (FUN_1401c9c00 / FUN_1401ae8e0 / FUN_1401d15b0(0) do it for a
//    battle): in the menu the stage stayed undrawn until the 8 groups' bit 0x20000 was cleared (2026-10-27 in-game check:
//    group 9 = pdStage, pdStageSet, pdStageDraw, the stage objects, pdCharaSelClass; 29 = pdMapSky; 30 = water / glow /
//    sprites; 31 = pdBattleCrash), so once the stage is up they are put back as they were before the load.
//  - com\st<stage % 10>\camdata.cam (COM_CAD_CAM_SPEC_BATCH010): u32 count, then up to 64 f32[3] points above the floor
//    (st03: y 80-140 while its objects reach down to y 0); the camera frames those points when the file is there.
//  - the rows: Single Play's stage list 0x140509ba0 (0x28 per row, +0x18 stage, +0x1c variants, *0x14050a5d0 rows); the
//    stage file table 0x1403f2b60 (entry 2N = stage N's .alr, 2N+1 its .dat, relative to Map\); stage objects
//    *0x1405322c8[u16 *0x1405322bc], position at +0x1e0.
static const ULONG_PTR RVA_LV_FIELD = 0x4c01d0, RVA_LV_SPSTAGE = 0x4c01d8, RVA_LV_VAR = 0x4c51f0, RVA_LV_VAR8 = 0x531dcf,
                       RVA_LV_FADE = 0x3dd3d5, RVA_LV_SCHED = 0x52dfe8, RVA_LV_LIST = 0x509ba0, RVA_LV_ROWS = 0x50a5d0,
                       RVA_LV_VIEW = 0x5a5ca0, RVA_LV_CAMVIEW = 0x584f40, RVA_LV_EYE = 0x58bdf0, RVA_LV_FLAGS = 0x530034;
static const ULONG_PTR FN_LV_LOAD = 0x1d1950, FN_LV_BUSY = 0x202820, FN_LV_ACTIVATE = 0x1d1b90, FN_LV_UNLOAD = 0x1d1c80,
                       FN_LV_LOOKAT = 0x2912d0;
static const int LV_GROUPS[8] = {0x98, 0xa8, 0xb8, 0xc8, 0xd8, 0x1d8, 0x1e8, 0x1f8};
static const int LV_REST = 10, LV_GONE = 12;                    // ticks on a row before it loads; frames without the page
typedef void(__fastcall *PFN_LV_LOAD)(ULONG_PTR field, int stage);
typedef char(__fastcall *PFN_LV_BUSY)();
typedef void(__fastcall *PFN_LV_ACT)(ULONG_PTR field, char wait);
typedef void(__fastcall *PFN_LV_VOID)();
typedef void(__fastcall *PFN_LV_LOOKAT)(float *out, const float *target, const float *eye);
enum { LV_IDLE, LV_LOADING, LV_SHOWN, LV_UNLOADING };
static int g_lv_phase, g_lv_want = -1, g_lv_rest, g_lv_row = -1, g_lv_stage = -1, g_lv_shown_ticks;
static int g_lv_last = -1, g_lv_hold;                           // v5.155 the stage just unloaded; ticks to wait before it loads again
static unsigned g_lv_clock, g_lv_seen;
static float g_lv_c[3], g_lv_r, g_lv_h, g_lv_ang;
static bool g_lv_framed;
struct LvSaved { int cur, sp, var; unsigned char var8, fade, mode, flag800; unsigned groups; float view[16]; };
static LvSaved g_lv_s;
static int g_lv_said_busy;
// self-test stand-ins (no game memory or code touched)
static unsigned char *g_lv_tm;                                  // game memory stand-in (see lv_at)
static int g_lv_t_busy, g_lv_t_loads, g_lv_t_acts, g_lv_t_unloads, g_lv_t_looks;

static ULONG_PTR lv_at(ULONG_PTR rva) {                         // test memory: the RVA's low 16 bits as the offset
    return g_lv_tm ? (ULONG_PTR)g_lv_tm + (rva & 0xffff) : g_base + rva;
}
static ULONG_PTR lv_field() { return *(ULONG_PTR *)lv_at(RVA_LV_FIELD); }
static bool lv_busy() {
    if (g_lv_tm) return g_lv_t_busy != 0;
    return ((PFN_LV_BUSY)(g_base + FN_LV_BUSY))() != 0;
}
static void lv_load(ULONG_PTR field, int stage) {
    if (g_lv_tm) { g_lv_t_loads++; *(unsigned char *)(field + 0x40) = 1; return; }
    ((PFN_LV_LOAD)(g_base + FN_LV_LOAD))(field, stage);
}
static void lv_activate(ULONG_PTR field) {
    if (g_lv_tm) { g_lv_t_acts++; *(unsigned char *)(field + 0x40) = 4; return; }
    ((PFN_LV_ACT)(g_base + FN_LV_ACTIVATE))(field, 0);
}
static void lv_unload() {
    if (g_lv_tm) { g_lv_t_unloads++; *(unsigned char *)(lv_field() + 0x40) = 5; return; }
    ((PFN_LV_VOID)(g_base + FN_LV_UNLOAD))();
}
static void lv_lookat(float *out, const float *target, const float *eye) {
    if (g_lv_tm) { g_lv_t_looks++; memset(out, 0, 64); out[0] = out[5] = out[10] = out[15] = 1; return; }
    ((PFN_LV_LOOKAT)(g_base + FN_LV_LOOKAT))(out, target, eye);
}
static unsigned *lv_group_flags(int k) {                        // NULL when the scheduler is missing
    ULONG_PTR s = *(ULONG_PTR *)lv_at(RVA_LV_SCHED);
    if (!s) return NULL;
    ULONG_PTR g = *(ULONG_PTR *)(s + 0xa0);
    return g ? (unsigned *)(g + LV_GROUPS[k]) : NULL;
}

// the stage of a row of the list, -1 when it is not one that can be shown (no row, a dummy, its files missing)
static int sl_pack(int value);                                  // v5.43 cwo_stagelist.inl
static int lv_stage_of(int row) {
    int rows = *(int *)lv_at(RVA_LV_ROWS);
    int st;
    if (row >= 1000) {                                          // v5.43 a pack row: Map Slot 7 with that pack
        int k = sl_pack(row);
        if (k < 0 || g_lv_tm) return -1;
        if (g_mx_cur != k) { InterlockedExchange(&g_mx_sel, k); mx_apply(k); }
        if (g_mx_cur != k) return -1;                           // the pack does not fit its buffer
        return LIST_STAGE;                                      // the pack's files were sized by mx_apply (they live in the override folder)
    } else {
        if (row < 0 || row >= rows || rows > 64) return -1;
        st = *(int *)(lv_at(RVA_LV_LIST) + (ULONG_PTR)row * 0x28 + 0x18);
    }
    if (st <= 0 || st >= 40) return -1;
    if (g_lv_tm) return st;
    wchar_t exe[MAX_PATH], p[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    wchar_t *sl = wcsrchr(exe, L'\\');
    if (sl) sl[1] = 0;
    for (int k = 0; k < 2; k++) {
        const char *name = *mx_entry(2 * st + k);
        if (!name || !*name) return -1;
        swprintf_s(p, MAX_PATH, L"%sAssets\\Data\\Map\\%hs", exe, name);
        // v5.184.2 (the Underground row showed no stage): in Store mode a stage only the loader installs (Map\st30.*) lives in the
        // override folder, not beside the exe - look there too, as the game's own reads are redirected (sm_redirect)
        wchar_t ov[MAX_PATH];
        if (GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES &&
            !(sm_redirect(p, ov, MAX_PATH, false) && GetFileAttributesW(ov) != INVALID_FILE_ATTRIBUTES)) return -1;
    }
    return st;
}

static void lv_save() {
    g_lv_s.cur = *(int *)lv_at(RVA_STAGE);
    g_lv_s.sp = *(int *)lv_at(RVA_LV_SPSTAGE);
    g_lv_s.var = *(int *)lv_at(RVA_LV_VAR);
    g_lv_s.var8 = *(unsigned char *)lv_at(RVA_LV_VAR8);
    g_lv_s.fade = *(unsigned char *)lv_at(RVA_LV_FADE);
    g_lv_s.mode = *(unsigned char *)(lv_field() + 0x50);
    g_lv_s.flag800 = (*(unsigned *)lv_at(RVA_LV_FLAGS) & 0x800) ? 1 : 0;
    g_lv_s.groups = 0;
    for (int k = 0; k < 8; k++) { unsigned *f = lv_group_flags(k); if (f && (*f & 0x20000)) g_lv_s.groups |= 1u << k; }
    memcpy(g_lv_s.view, (void *)lv_at(RVA_LV_VIEW), 64);
}
// v5.31 (owner 2026-10-27 - after a
// Create a Stage match): after any battle the game leaves these 8 groups suspended in the menus, and lv_groups_back put
// that suspension back the moment the preview showed, so its stage was never drawn. While a stage is shown all 8 run;
// the state from before the preview comes back only when it goes (lv_restore).
static void lv_groups_show() {
    int was = 0;
    for (int k = 0; k < 8; k++) { unsigned *f = lv_group_flags(k); if (f) { if (*f & 0x20000) was |= 1 << k; *f &= ~0x20000u; } }
    if (g_lv_s.groups) logf("stage view: world groups suspended before the preview (mask %02x) run while it shows", g_lv_s.groups);
    (void)was;
}
static void lv_groups_back() {                                  // the 8 groups' suspension as before the load
    for (int k = 0; k < 8; k++) {
        unsigned *f = lv_group_flags(k);
        if (f) *f = (g_lv_s.groups & (1u << k)) ? (*f | 0x20000) : (*f & ~0x20000u);
    }
}
static void lv_restore() {                                      // everything as it was before the preview
    *(int *)lv_at(RVA_STAGE) = g_lv_s.cur;
    *(int *)lv_at(RVA_LV_SPSTAGE) = g_lv_s.sp;
    *(int *)lv_at(RVA_LV_VAR) = g_lv_s.var;
    *(unsigned char *)lv_at(RVA_LV_VAR8) = g_lv_s.var8;
    *(unsigned char *)lv_at(RVA_LV_FADE) = g_lv_s.fade;
    *(unsigned char *)(lv_field() + 0x50) = g_lv_s.mode;
    unsigned *fl = (unsigned *)lv_at(RVA_LV_FLAGS);
    *fl = (*fl & ~0x800u) | (g_lv_s.flag800 ? 0x800u : 0);
    lv_groups_back();
    memcpy((void *)lv_at(RVA_LV_VIEW), g_lv_s.view, 64);
}

static void lv_sort(float *a, int m) {
    for (int i = 1; i < m; i++) { float v = a[i]; int j = i - 1; while (j >= 0 && a[j] > v) { a[j + 1] = a[j]; j--; } a[j + 1] = v; }
}
// the stage's camera points (com\stNN\camdata.cam, NN = stage % 10), 0 when there is no file
static int lv_cam_points(int stage, float *xs, float *ys, float *zs, int cap) {
    wchar_t exe[MAX_PATH], p[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    wchar_t *sl = wcsrchr(exe, L'\\');
    if (sl) sl[1] = 0;
    swprintf_s(p, MAX_PATH, L"%sAssets\\Data\\com\\st%02d\\camdata.cam", exe, stage % 10);
    FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"rb") != 0 || !f) return 0;
    unsigned char b[776];
    size_t got = fread(b, 1, sizeof(b), f);
    fclose(f);
    if (got < 4) return 0;
    unsigned cnt = *(unsigned *)b;
    int n = 0;
    for (unsigned i = 0; i < cnt && i < 64 && 4 + 12 * (i + 1) <= got && n < cap; i++) {
        const float *q = (const float *)(b + 4 + 12 * i);
        if (fabsf(q[0]) > 5000 || fabsf(q[1]) > 5000 || fabsf(q[2]) > 5000) continue;
        xs[n] = q[0]; ys[n] = q[1]; zs[n] = q[2]; n++;
    }
    return n;
}
// the circle the camera flies: over the stage's camera points (or its objects, the middle 80 %), looking down on their
// middle at their lower level from above their upper level
static void lv_frame_stage() {
    float xs[256], ys[256], zs[256];
    int n = 0;
    bool cam = false;
    if (!g_lv_tm) { n = lv_cam_points(g_lv_stage, xs, ys, zs, 256); cam = n >= 4; if (!cam) n = 0; }
    if (!g_lv_tm && !cam) {
        __try {
            ULONG_PTR arr = *(ULONG_PTR *)(g_base + RVA_INSTANCES);
            int cnt = *(unsigned short *)(g_base + RVA_INSTANCE_COUNT);
            for (int i = 0; arr && i < cnt && n < 256; i++) {
                ULONG_PTR o = *(ULONG_PTR *)(arr + 8 * (ULONG_PTR)i);
                if (!o) continue;
                const float *p = (const float *)(o + OBJ_POS);
                if (fabsf(p[0]) > 5000 || fabsf(p[1]) > 5000 || fabsf(p[2]) > 5000) continue;
                xs[n] = p[0]; ys[n] = p[1]; zs[n] = p[2]; n++;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) { n = 0; }
    }
    if (n < 4) { g_lv_c[0] = 0; g_lv_c[1] = 60; g_lv_c[2] = 0; g_lv_r = 260; g_lv_h = 150; g_lv_framed = true; return; }
    if (cam) {
        // 2026-10-27 in game (dARK Highway): a circle sized by the points' spread ran outside the spiral road and faced its
        // wall. The camera points lie on the play area, so the circle is their median distance from their middle (the
        // eye stays over the arena), 40 above the highest point, looking at the middle just above the lowest one.
        float cx = 0, cz = 0, ylo = ys[0], yhi = ys[0], d[256];
        for (int i = 0; i < n; i++) { cx += xs[i]; cz += zs[i]; ylo = fminf(ylo, ys[i]); yhi = fmaxf(yhi, ys[i]); }
        cx /= n; cz /= n;
        for (int i = 0; i < n; i++) d[i] = sqrtf((xs[i] - cx) * (xs[i] - cx) + (zs[i] - cz) * (zs[i] - cz));
        lv_sort(d, n);
        float r = d[n / 2];
        g_lv_c[0] = cx; g_lv_c[1] = ylo + 10; g_lv_c[2] = cz;
        g_lv_r = r < 120 ? 120 : r > 700 ? 700 : r;
        g_lv_h = yhi - g_lv_c[1] + fmaxf(60.0f, g_lv_r * 0.45f);  // 5.29.4: higher - looks down over the walls (dARK Highway)
        g_lv_framed = true;
        logf("stage view: stage %d framed on %d camera points: centre (%.0f, %.0f, %.0f), circle %.0f, height %.0f", g_lv_stage, n,
             g_lv_c[0], g_lv_c[1], g_lv_c[2], g_lv_r, g_lv_h);
        return;
    }
    lv_sort(xs, n); lv_sort(ys, n); lv_sort(zs, n);
    int lo = cam ? 0 : n / 10, hi = cam ? n - 1 : n - 1 - n / 10;
    g_lv_c[0] = (xs[lo] + xs[hi]) * 0.5f;
    g_lv_c[1] = ys[lo];
    g_lv_c[2] = (zs[lo] + zs[hi]) * 0.5f;
    float ext = fmaxf(xs[hi] - xs[lo], zs[hi] - zs[lo]) * 0.5f;
    if (ext < 120) ext = 120;
    if (ext > 700) ext = 700;
    g_lv_r = ext * 0.8f;                                        // 2026-10-27 in game: a flatter view shows more of the stage
    g_lv_h = (ys[hi] - ys[lo]) * 0.6f + ext * 0.15f;
    g_lv_framed = true;
    logf("stage view: stage %d framed on %d %s: centre (%.0f, %.0f, %.0f), circle %.0f, height %.0f", g_lv_stage, n,
         cam ? "camera points" : "objects", g_lv_c[0], g_lv_c[1], g_lv_c[2], g_lv_r, g_lv_h);
}
static void lv_camera() {                                       // one step of the circle (30 a second: 40 s a turn)
    g_lv_ang += 6.2831853f / (40.0f * 30.0f);
    if (g_lv_ang > 6.2831853f) g_lv_ang -= 6.2831853f;
    float eye[3] = {g_lv_c[0] + sinf(g_lv_ang) * g_lv_r, g_lv_c[1] + g_lv_h, g_lv_c[2] + cosf(g_lv_ang) * g_lv_r};
    float tgt[3] = {g_lv_c[0], g_lv_c[1], g_lv_c[2]};
    float *cam = (float *)lv_at(RVA_LV_CAMVIEW);
    lv_lookat(cam, tgt, eye);
    memcpy((void *)lv_at(RVA_LV_VIEW), cam, 64);
    memcpy((void *)lv_at(RVA_LV_EYE), eye, 12);
}

// one step; on_page: the list's frame is running (its highlighted row is g_lv_want)
static void lv_step(bool on_page) {
    ULONG_PTR field = lv_field();
    if (!field) return;
    unsigned char st = *(unsigned char *)(field + 0x40);
    int want = on_page ? g_lv_want : -1;
    switch (g_lv_phase) {
    case LV_IDLE: {
        if (want < 0 || g_lv_rest < LV_REST) return;
        if (want == g_lv_row && g_lv_stage < 0) return;          // this row cannot be shown (said once)
        int stage = lv_stage_of(want);
        if (stage < 0) { if (g_lv_row != want) logf("stage view: row %d has no stage to show", want); g_lv_row = want; g_lv_stage = -1; return; }
        if (stage == g_lv_last && g_lv_hold > 0) { g_lv_hold--; return; }   // v5.155 the same stage number (Map Slot 7 packs): let the
                                                                             // previous one's files and handles settle first
        if (st != 0) {
            if (g_lv_said_busy != st) logf("stage view: the game already has a stage up (field state %d) - no preview", st);
            g_lv_said_busy = st;
            return;
        }
        g_lv_said_busy = 0;
        lv_save();
        *(int *)lv_at(RVA_STAGE) = stage;
        *(int *)lv_at(RVA_LV_SPSTAGE) = stage;
        *(int *)lv_at(RVA_LV_VAR) = 0;
        *(unsigned char *)lv_at(RVA_LV_VAR8) = 0;
        *(unsigned char *)lv_at(RVA_LV_FADE) = 0;              // no fade to black over the menu
        *(unsigned char *)(field + 0x50) = 1;                  // wait for the activation, as Single Play
        g_lv_row = want; g_lv_stage = stage; g_lv_framed = false; g_lv_shown_ticks = 0;
        lv_load(field, stage);
        g_lv_phase = LV_LOADING;
        logf("stage view: row %d -> loading stage %d (field mode was %d, fade %d)", want, stage, g_lv_s.mode, g_lv_s.fade);
        return;
    }
    case LV_LOADING:
        if (st == 4 || st == 3) { lv_groups_show(); g_lv_phase = LV_SHOWN; logf("stage view: stage %d is up (state %d)", g_lv_stage, st); return; }
        if ((st == 1 || st == 2) && !lv_busy()) {
            lv_activate(field);
            lv_groups_show();                                    // v5.31 drawn and running (whatever they were before)
            g_lv_phase = LV_SHOWN;
            logf("stage view: stage %d loaded and shown", g_lv_stage);
        }
        return;
    case LV_SHOWN:
        if (want != g_lv_row || g_lv_reload) {                     // v5.32 or Map Slot 7 holds another canvas now
            InterlockedExchange(&g_lv_reload, 0);
            *(int *)lv_at(RVA_LV_SPSTAGE) = 0x7777;
            lv_unload();
            g_lv_phase = LV_UNLOADING;
            logf("stage view: stage %d away (%s)", g_lv_stage, want < 0 ? "the list is closed" : "another row");
            return;
        }
        if (++g_lv_shown_ticks == 4 || (!g_lv_framed && g_lv_shown_ticks > 4)) lv_frame_stage();
        return;
    case LV_UNLOADING:
        if (st != 0) return;
        lv_restore();
        g_lv_phase = LV_IDLE;
        g_lv_last = g_lv_stage; g_lv_hold = 30;
        g_lv_row = -1; g_lv_stage = -1;
        logf("stage view: unloaded, the game's settings put back");
        return;
    }
}
static bool lv_showing() { return g_lv_phase == LV_SHOWN && g_lv_framed; }

// native 2.8 ops: 49 = the list's frame (args[1] = the highlighted row), 50 = its draw (bit 0 background, bit 1 picture)
static INT64 lv_native(int op, INT64 *args) {
    if (op == 50) return lv_showing() ? 0 : 3;
    __try {
        int row = (int)args[1];
        InterlockedExchange(&g_sl_hl, row >= 1000 ? sl_pack(row) : -1);   // v5.43 the highlighted pack (its picture)
        g_lv_seen = g_lv_clock;
        if (row != g_lv_want) { g_lv_want = row; g_lv_rest = 0; } else if (g_lv_rest < 1000) g_lv_rest++;
        lv_step(true);
        if (lv_showing()) lv_camera();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        logf("stage view: fault - stopped");
        g_lv_phase = LV_IDLE;
    }
    return 0;
}
static void lv_frame() {                                        // game thread, every drawn frame: the list closed?
    g_lv_clock++;
    if (g_lv_phase == LV_IDLE || g_lv_clock - g_lv_seen <= (unsigned)LV_GONE) return;
    __try { lv_step(false); } __except (EXCEPTION_EXECUTE_HANDLER) { g_lv_phase = LV_IDLE; }
}

static int lv_self_test() {                                     // the whole round on stand-ins
    static unsigned char mem[0x10000], field[0x80], sched[0x100], groups[0x200];
    memset(mem, 0, sizeof(mem)); memset(field, 0, sizeof(field)); memset(groups, 0, sizeof(groups));
    g_lv_tm = mem;
    *(ULONG_PTR *)lv_at(RVA_LV_FIELD) = (ULONG_PTR)field;
    *(ULONG_PTR *)lv_at(RVA_LV_SCHED) = (ULONG_PTR)sched;
    *(ULONG_PTR *)(sched + 0xa0) = (ULONG_PTR)groups;
    *(int *)lv_at(RVA_LV_ROWS) = 3;
    *(int *)(lv_at(RVA_LV_LIST) + 2 * 0x28 + 0x18) = 5;         // row 2 = stage 5
    *(int *)lv_at(RVA_STAGE) = 7; *(int *)lv_at(RVA_LV_SPSTAGE) = 0x7777; *(unsigned char *)lv_at(RVA_LV_FADE) = 1;
    *(unsigned *)(groups + 0xa8) = 0x20000;                     // group 10 suspended before
    field[0x50] = 0;
    g_lv_phase = LV_IDLE; g_lv_want = -1; g_lv_row = -1; g_lv_stage = -1; g_lv_rest = 0; g_lv_last = -1; g_lv_hold = 0; g_lv_clock = g_lv_seen = 0;
    g_lv_t_busy = 1; g_lv_t_loads = g_lv_t_acts = g_lv_t_unloads = g_lv_t_looks = 0;
    INT64 a[2] = {49, 2};
    bool ok = true;
    for (int i = 0; i < LV_REST; i++) lv_native(49, a);         // resting on the row: not yet
    ok &= g_lv_t_loads == 0;
    lv_native(49, a);
    ok &= g_lv_t_loads == 1 && g_lv_phase == LV_LOADING && *(int *)lv_at(RVA_STAGE) == 5 && field[0x50] == 1 &&
          *(unsigned char *)lv_at(RVA_LV_FADE) == 0;
    lv_native(49, a);                                           // files still loading
    ok &= g_lv_t_acts == 0 && lv_native(50, a) == 3;
    g_lv_t_busy = 0;
    lv_native(49, a);
    ok &= g_lv_t_acts == 1 && g_lv_phase == LV_SHOWN;
    *(unsigned *)(groups + 0x98) |= 0x20000;                    // (the game's load suspends the groups)
    g_lv_t_busy = 1; field[0x40] = 1; g_lv_phase = LV_LOADING; g_lv_t_acts = 0;
    lv_native(49, a);
    g_lv_t_busy = 0;
    lv_native(49, a);
    ok &= g_lv_t_acts == 1 && !(*(unsigned *)(groups + 0x98) & 0x20000) && !(*(unsigned *)(groups + 0xa8) & 0x20000);   // v5.31 all run while shown
    for (int i = 0; i < 5; i++) lv_native(49, a);
    ok &= lv_showing() && g_lv_t_looks > 0 && lv_native(50, a) == 0;
    for (int i = 0; i <= LV_GONE; i++) lv_frame();              // the list closed
    ok &= g_lv_t_unloads == 1 && g_lv_phase == LV_UNLOADING && *(int *)lv_at(RVA_LV_SPSTAGE) == 0x7777;
    field[0x40] = 0;                                            // the game finished the unload
    lv_frame();
    ok &= g_lv_phase == LV_IDLE && *(int *)lv_at(RVA_STAGE) == 7 && *(unsigned char *)lv_at(RVA_LV_FADE) == 1 &&
          field[0x50] == 0 && (*(unsigned *)(groups + 0xa8) & 0x20000) && !(*(unsigned *)(groups + 0x98) & 0x20000) &&
          *(int *)lv_at(RVA_LV_SPSTAGE) == 0x7777;
    a[1] = 1;                                                   // a row without a stage: nothing loads
    for (int i = 0; i <= LV_REST + 1; i++) lv_native(49, a);
    ok &= g_lv_t_loads == 1 && g_lv_phase == LV_IDLE;
    g_lv_tm = NULL;
    g_lv_phase = LV_IDLE; g_lv_want = -1; g_lv_row = -1; g_lv_stage = -1; g_lv_rest = 0; g_lv_last = -1; g_lv_hold = 0;
    logf("self-test: stage view (load, show, camera, unload, put back) %s", ok ? "ok" : "FAILED");
    return ok ? 1 : -71;
}
