// ---- v5.138 60 FPS display (owner 2026-11-10; "work on a 60 FPS").
// Route B of 19_Performance_60FPS/SIXTY_FPS_RESEARCH_2026-10-21.md: the game logic stays the original fixed 30 Hz (so speeds,
// timers and AI are untouched); only the picture gets a second frame between two ticks, blended.
// Game facts (Ghidra full pass 065 via pd_fn.py + capstone, clean 9ba7411b; MODLOG 2026-10-21 / 2026-11-10):
//  - FUN_1401c85c0 (main loop step): runs up to 3 ticks FUN_1401c7630 (tick counter *0x14052dd6c +1 each), then, if a tick ran
//    (frame object *0x1405b1798 +100 = 1), clears (FUN_140021e70) and builds the frame (FUN_1401c8110(frame)); the main loop
//    presents only when +100 is set; the present limiter FUN_140015e00 waits QPF/30 since the last present (`sar rdx, 4` at
//    0x140015ea9; 5 = QPF/60).
//  - what moves on screen, per fighter (actor table *0x1404bc9d8): the bone matrices at *(actor + 0x13940), one 0x40-byte
//    matrix per joint (+1 root), drawn by FUN_14024f5e0 -> FUN_140295420; the joint count = the u32 at +8 of the model's
//    joint chunk 0x03 (model container *(actor + 0x12c78), group at +offset [0x8a] when it is a 0x11 container); the view
//    (camera) matrix the stage draw uses is copied from the viewing fighter's +0x13F50..+0x13F8C (FUN_14012bfc0); the
//    position +0x12E40.
//  - hook sites (first bytes, no relative operands): 0x1401c8110 "40 56 48 83 ec 30" (push rsi; sub rsp,0x30),
//    0x1401c85c0 "48 89 5c 24 08 57" (mov [rsp+8],rbx; push rdi).
// What this does (CwoFps.txt "60"; offline only; anything else = the game's own 30): at each tick the moving state above is
// kept (previous + current); every frame drawn shows the blend of the last two ticks (one tick behind, so motion only goes
// forward): the game's own frame right after a tick at 0.0, our extra frame half a tick later at 0.5 - written just for the
// draw and put back at once, so the logic never sees it. Effects / projectiles still step at 30 (not blended yet).
typedef void(__fastcall *PFN_FB)(ULONG_PTR);
typedef char(__fastcall *PFN_LOOP)(void);                          // v5.138.1: returns 'a frame is ready' (setne al at 0x1401c86b1)
typedef void(__fastcall *PFN_CLEAR)(void);
static const ULONG_PTR FP_FB = 0x1c8110, FP_LOOP = 0x1c85c0, FP_CLEAR = 0x021e70, FP_LIMIT_SHIFT = 0x015eac,
                       FP_TICKS = 0x52dd6c, FP_FRAME = 0x5b1798, FP_ACTORS = 0x4bc9d8;
static const unsigned char FP_FB_OLD[6] = {0x40, 0x56, 0x48, 0x83, 0xec, 0x30};
static const unsigned char FP_LOOP_OLD[6] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57};
static PFN_FB g_fp_fb_orig;
static PFN_LOOP g_fp_loop_orig;
static int g_fp_state;                                             // 0 off, 1 hooked, -1 refused
static volatile LONG g_fp_on;                                      // CwoFps.txt "60"
static const int FP_MAXJ = 192;
struct FpFighter { ULONG_PTR actor; int nj; bool valid; float bones[2][(FP_MAXJ + 1) * 16]; float view[2][16]; float pos[2][3]; };
static FpFighter g_fp_f[8];
static int g_fp_last_tick = -1, g_fp_cur;                          // g_fp_cur = which of the 2 slots holds the newest tick
static LONGLONG g_fp_tick_qpc, g_fp_qpf;
static bool g_fp_mid_done, g_fp_in_mid;

static int fp_joints(ULONG_PTR actor) {                            // the joint count of a fighter's model (0 = unknown)
    __try {
        const unsigned *g = *(const unsigned **)(actor + 0x12c78);
        if (!g) return 0;
        if (g[0] == 0x11) { if (!g[0x8a]) return 0; g = (const unsigned *)((const char *)g + g[0x8a]); }
        for (int k = 0; k < 64 && g[0]; k++) {
            // v5.138.2 (memory read 08:1x: fighters' joint chunks hold 0x00010073 / 0x0001008c - the count is the low 16 bits)
            if (g[0] == 0x03) { int n = (int)(g[2] & 0xFFFF); return n > 0 && n <= FP_MAXJ ? n : 0; }
            if (g[1] < 8 || g[1] > 0x400000) return 0;
            g = (const unsigned *)((const char *)g + g[1]);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}

// v5.148.20 (owner list: "60 FPS for effects / projectiles"): the effect instances (the manager *RVA_FX_MGR, 256 slots of 0xF0
// from +0x60; slot +0 handle, +0x50 the matrix FUN_14018d440 moves; recorded facts) and the fighters' flying records (fighter +
// 0x4E0, 48 x 0x1F0, +0x10 state, +0x14 skill, +0x80 the 4 x 4 matrix) get their positions blended in the extra frame too: the
// translation (row 3) of each one present in both ticks with the same handle / skill, put back right after the draw.
struct FpPos { unsigned key; float t[2][3]; bool both; };
static FpPos g_fp_fx[256];
static FpPos g_fp_pr[8][48];
static void fp_capture_moving() {
    ULONG_PTR mgr = 0; __try { mgr = *(ULONG_PTR *)(g_base + RVA_FX_MGR); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    for (int s = 0; s < 256; s++) {
        FpPos &P = g_fp_fx[s];
        __try {
            unsigned char *sl = mgr ? (unsigned char *)(mgr + 0x60 + (ULONG_PTR)s * 0xF0) : NULL;
            unsigned h = sl ? *(unsigned *)sl : 0;
            if (!h) { P.key = 0; P.both = false; continue; }
            P.both = P.key == h;
            P.key = h;
            memcpy(P.t[g_fp_cur], sl + 0x50 + 48, 12);
        } __except (EXCEPTION_EXECUTE_HANDLER) { P.key = 0; P.both = false; }
    }
    for (int i = 0; i < 8; i++) {
        ULONG_PTR a = i < cwo_nfighters() ? cwo_actors()[i] : 0;
        for (int k = 0; k < 48; k++) {
            FpPos &P = g_fp_pr[i][k];
            __try {
                ULONG_PTR sl = a ? a + 0x4E0 + 0x1F0 * (ULONG_PTR)k : 0;
                unsigned key = sl && *(int *)(sl + 0x10) ? (unsigned)(*(int *)(sl + 0x14) * 8 + *(int *)(sl + 0x10)) + 1 : 0;
                if (!key) { P.key = 0; P.both = false; continue; }
                P.both = P.key == key;
                P.key = key;
                memcpy(P.t[g_fp_cur], (const void *)(sl + 0x80 + 48), 12);
            } __except (EXCEPTION_EXECUTE_HANDLER) { P.key = 0; P.both = false; }
        }
    }
}
static void fp_apply_moving(float t, bool restore) {
    int c = g_fp_cur, p = c ^ 1;
    ULONG_PTR mgr = 0; __try { mgr = *(ULONG_PTR *)(g_base + RVA_FX_MGR); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    for (int s = 0; s < 256 && mgr; s++) {
        FpPos &P = g_fp_fx[s];
        if (!P.key || !P.both) continue;
        __try {
            unsigned char *sl = (unsigned char *)(mgr + 0x60 + (ULONG_PTR)s * 0xF0);
            if (*(unsigned *)sl != P.key) continue;
            float *m = (float *)(sl + 0x50 + 48);
            for (int q = 0; q < 3; q++) m[q] = restore ? P.t[c][q] : P.t[p][q] + (P.t[c][q] - P.t[p][q]) * t;
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    for (int i = 0; i < 8; i++) {
        ULONG_PTR a = i < cwo_nfighters() ? cwo_actors()[i] : 0;
        if (!a) continue;
        for (int k = 0; k < 48; k++) {
            FpPos &P = g_fp_pr[i][k];
            if (!P.key || !P.both) continue;
            __try {
                ULONG_PTR sl = a + 0x4E0 + 0x1F0 * (ULONG_PTR)k;
                if (!*(int *)(sl + 0x10)) continue;
                float *m = (float *)(sl + 0x80 + 48);
                for (int q = 0; q < 3; q++) m[q] = restore ? P.t[c][q] : P.t[p][q] + (P.t[c][q] - P.t[p][q]) * t;
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
    }
}

static void fp_capture() {                                         // a new tick: previous <- current, current <- live
    g_fp_cur ^= 1;
    for (int i = 0; i < 8; i++) {
        FpFighter &F = g_fp_f[i];
        ULONG_PTR a = i < cwo_nfighters() && i < 8 ? cwo_actors()[i] : 0;   // v5.146 the moved table when six fighters is on
        __try {
            if (!a || !*(int *)(a + 0x20)) { F.valid = false; F.actor = 0; continue; }
            int nj = fp_joints(a);
            const float *bones = *(const float **)(a + 0x13940);
            if (!nj || !bones) { F.valid = false; F.actor = 0; continue; }
            bool same = F.actor == a && F.nj == nj;
            memcpy(F.bones[g_fp_cur], bones, (size_t)(nj + 1) * 64);
            memcpy(F.view[g_fp_cur], (const void *)(a + 0x13F50), 64);
            memcpy(F.pos[g_fp_cur], (const void *)(a + 0x12E40), 12);
            F.valid = same;                                        // a previous tick of the same fighter is needed to blend
            F.actor = a; F.nj = nj;
        } __except (EXCEPTION_EXECUTE_HANDLER) { F.valid = false; F.actor = 0; }
    }
}

static void fp_apply(float t, bool restore) {                      // live <- blend(previous, current, t), or <- current
    int c = g_fp_cur, p = c ^ 1;
    for (int i = 0; i < 8; i++) {                                   // v5.146 all 8 (six fighters: 5 and 6 blend too)
        FpFighter &F = g_fp_f[i];
        if (!F.valid || !F.actor) continue;
        __try {
            float *bones = *(float **)(F.actor + 0x13940);
            if (!bones || fp_joints(F.actor) != F.nj) continue;
            int nb = (F.nj + 1) * 16;
            float *view = (float *)(F.actor + 0x13F50), *pos = (float *)(F.actor + 0x12E40);
            // v5.188.20 (BFR player feedback 2026-11-16, Stage Creator: flying did not move the fighter, seen in game: the position
            // stayed at the tick's copy while the fly wrote it): the editor's fly writes player 1's position on the draw path,
            // between this blend and its restore - while flying, that position is the fly's own (not blended, not put back)
            bool flypos = g_flying && i == 0;
            if (restore) { memcpy(bones, F.bones[c], (size_t)nb * 4); memcpy(view, F.view[c], 64); if (!flypos) memcpy(pos, F.pos[c], 12); continue; }
            for (int k = 0; k < nb; k++) bones[k] = F.bones[p][k] + (F.bones[c][k] - F.bones[p][k]) * t;
            for (int k = 0; k < 16; k++) view[k] = F.view[p][k] + (F.view[c][k] - F.view[p][k]) * t;
            if (!flypos) for (int k = 0; k < 3; k++) pos[k] = F.pos[p][k] + (F.pos[c][k] - F.pos[p][k]) * t;
        } __except (EXCEPTION_EXECUTE_HANDLER) { F.valid = false; }
    }
}

static void __fastcall fp_fb_hook(ULONG_PTR frame) {               // the frame builder: drawn blended, then put back
    if (!g_fp_on || !g_in_match) { g_fp_fb_orig(frame); return; }
    int tick = *(int *)(g_base + FP_TICKS);
    if (tick != g_fp_last_tick) { g_fp_last_tick = tick; fp_capture(); fp_capture_moving(); }
    fp_apply(g_fp_in_mid ? 0.5f : 0.0f, false);
    fp_apply_moving(g_fp_in_mid ? 0.5f : 0.0f, false);             // v5.148.20 effects / flying records: one tick behind like the fighters
    g_fp_fb_orig(frame);
    fp_apply(0, true);
    fp_apply_moving(0, true);
}

// v5.138.1 (in game 07:4x: no frame was presented after the title - the main loop FUN_1400087c0 presents only when this step
// RETURNS non-zero, `cmp [rdi+0x64],0; setne al`; the void hook dropped it): the original's answer is passed on, 1 after ours
static char __fastcall fp_loop_hook(void) {                        // the main loop step: an extra frame half a tick later
    int before = *(int *)(g_base + FP_TICKS);
    char ready = g_fp_loop_orig();
    if (!g_fp_on || !g_in_match || ed_online()) return ready;
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    if (*(int *)(g_base + FP_TICKS) != before) { g_fp_tick_qpc = now.QuadPart; g_fp_mid_done = false; return ready; }
    ULONG_PTR frame = *(ULONG_PTR *)(g_base + FP_FRAME);
    if (!frame || g_fp_mid_done || !g_fp_qpf) return ready;
    if (now.QuadPart - g_fp_tick_qpc < g_fp_qpf / 60) return ready;   // half of a 1/30 s tick
    g_fp_mid_done = true;
    __try {
        g_fp_in_mid = true;
        ((PFN_CLEAR)(g_base + FP_CLEAR))();
        fp_fb_hook(frame);
        g_fp_in_mid = false;
        *(int *)(frame + 100) = 1;
        return 1;                                                  // the main loop presents it (limiter at 1/60)
    } __except (EXCEPTION_EXECUTE_HANDLER) { g_fp_in_mid = false; InterlockedExchange(&g_fp_on, 0); logf("60 fps: fault in the extra frame - back to 30"); }
    return ready;
}

static bool fp_hook_one(ULONG_PTR site, const unsigned char *old, ULONG_PTR page, void *hook, void **orig, bool live) {
    unsigned char *s = (unsigned char *)site, *pg = (unsigned char *)page;
    if (memcmp(s, old, 6) != 0) return false;
    memcpy(pg, old, 6);                                            // the original's first 6 bytes, then back to site + 6
    pg[6] = 0xe9; int r = (int)((LONGLONG)(site + 6) - (LONGLONG)(page + 11)); memcpy(pg + 7, &r, 4);
    pg[0x40] = 0x48; pg[0x41] = 0xb8; ULONG_PTR h = (ULONG_PTR)hook; memcpy(pg + 0x42, &h, 8);   // mov rax, hook
    pg[0x4a] = 0xff; pg[0x4b] = 0xe0;                                                          // jmp rax
    DWORD o = 0;
    *orig = pg;                                                    // v5.148.17 BEFORE the site is patched (log 19:38: the game ran the hook
                                                                   // between the patch and this store and called address 0)
    if (live && !VirtualProtect(s, 6, PAGE_EXECUTE_READWRITE, &o)) { *orig = NULL; return false; }
    s[0] = 0xe9; r = (int)((LONGLONG)(page + 0x40) - (LONGLONG)(site + 5)); memcpy(s + 1, &r, 4); s[5] = 0x90;
    if (live) { VirtualProtect(s, 6, o, &o); FlushInstructionCache(GetCurrentProcess(), s, 6); FlushInstructionCache(GetCurrentProcess(), pg, 0x80); }
    return true;
}

static void fp_cfg() {                                             // every 3 s: CwoFps.txt
    static ULONGLONG t;
    ULONGLONG now = GetTickCount64();
    if (now < t) return;
    t = now + 3000;
    wchar_t p[MAX_PATH]; swprintf_s(p, MAX_PATH, L"%sCwoFps.txt", g_dir);
    FILE *f = NULL; char line[16] = "30";
    if (cwo_wfopen(&f, p, L"r") == 0 && f) { if (!fgets(line, sizeof(line), f)) strcpy_s(line, "30"); fclose(f); }
    else if (cwo_wfopen(&f, p, L"w") == 0 && f) { fprintf(f, "30\n# 60 = 60 FPS display (game logic stays 30 Hz; offline), 30 = the game's own\n"); fclose(f); }
    LONG want = atoi(line) == 60 && g_fp_state == 1 ? 1 : 0;
    if (want != g_fp_on) {
        unsigned char *sh = (unsigned char *)(g_base + FP_LIMIT_SHIFT);
        DWORD o = 0;
        __try {
            if (*sh == 4 || *sh == 5) {
                if (VirtualProtect(sh, 1, PAGE_EXECUTE_READWRITE, &o)) { *sh = want ? 5 : 4; VirtualProtect(sh, 1, o, &o); FlushInstructionCache(GetCurrentProcess(), sh, 1); }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        InterlockedExchange(&g_fp_on, want);
        logf("60 fps: %s (present limiter shift %d)", want ? "on - 30 Hz logic, 60 Hz picture" : "off - the game's own 30", *sh);
    }
}

static void fp_install() {                                         // worker start
    {   // v5.138.1 safety: the hooks are put in only when CwoFps.txt says 60 at start-up (else the game's code is not touched)
        wchar_t p[MAX_PATH]; swprintf_s(p, MAX_PATH, L"%sCwoFps.txt", g_dir);
        FILE *f = NULL; char line[16] = "30";
        if (cwo_wfopen(&f, p, L"r") == 0 && f) { if (!fgets(line, sizeof(line), f)) strcpy_s(line, "30"); fclose(f); }
        if (atoi(line) != 60) { logf("60 fps: off (CwoFps.txt is not 60) - no hooks"); return; }
    }
    LARGE_INTEGER q; QueryPerformanceFrequency(&q); g_fp_qpf = q.QuadPart;
    const unsigned char *sh = (const unsigned char *)(g_base + FP_LIMIT_SHIFT);
    if (sh[-3] != 0x48 || sh[-2] != 0xc1 || sh[-1] != 0xfa || *sh != 4) { g_fp_state = -1; logf("60 fps: the limiter's shift is not stock - off"); return; }
    ULONG_PTR page = sixf2_near_alloc(0x1000, PAGE_EXECUTE_READWRITE);
    if (!page) { g_fp_state = -1; logf("60 fps: no memory near the exe - off"); return; }
    if (!fp_hook_one(g_base + FP_FB, FP_FB_OLD, page, (void *)fp_fb_hook, (void **)&g_fp_fb_orig, true) ||
        !fp_hook_one(g_base + FP_LOOP, FP_LOOP_OLD, page + 0x100, (void *)fp_loop_hook, (void **)&g_fp_loop_orig, true)) {
        g_fp_state = -1; logf("60 fps: a hook site is not stock - off"); return;
    }
    g_fp_state = 1;
    logf("60 fps: frame builder 0x1401c8110 and loop 0x1401c85c0 hooked (CwoFps.txt \"60\" turns it on)");
}

static int fp_self_test() {                                        // the detour bytes on a stand-in, the blend
    static unsigned char fake[0x100], page[0x100];
    memset(fake, 0xcc, sizeof(fake)); memset(page, 0, sizeof(page));
    memcpy(fake + 0x10, FP_FB_OLD, 6);
    void *orig = NULL;
    bool ok = fp_hook_one((ULONG_PTR)(fake + 0x10), FP_FB_OLD, (ULONG_PTR)page, (void *)fp_fb_hook, &orig, false);
    int rel; memcpy(&rel, fake + 0x11, 4);
    ok = ok && fake[0x10] == 0xe9 && fake[0x15] == 0x90 && (ULONG_PTR)(fake + 0x15 + rel) == (ULONG_PTR)page + 0x40 && memcmp(page, FP_FB_OLD, 6) == 0;
    memcpy(&rel, page + 7, 4); ok = ok && page[6] == 0xe9 && (ULONG_PTR)(page + 11 + rel) == (ULONG_PTR)(fake + 0x16);
    ok = ok && *(ULONG_PTR *)(page + 0x42) == (ULONG_PTR)fp_fb_hook && page[0x4a] == 0xff && orig == page;
    ok = ok && !fp_hook_one((ULONG_PTR)(fake + 0x10), FP_FB_OLD, (ULONG_PTR)page, (void *)fp_fb_hook, &orig, false);   // not stock any more
    logf("self-test: 60 fps detour %s", ok ? "ok" : "FAILED");
    return ok ? 1 : -138;
}
