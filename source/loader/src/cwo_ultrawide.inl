// ---- v5.189.0 ULTRAWIDE (owner 2026-11-21). MODS edition, offline only (online stays the game's own until the owner OKs it as display-only).
// Game facts (Ghidra full pass 065, pd_fn.py + capstone on the clean exe 9ba7411b, 2026-11-21):
//  - FUN_140014b00 (device set-up) makes the render target the whole window: viewport floats *0x1404bad70 x, +74 y, +78 w,
//    +7c h. Each frame FUN_140021e70 cuts the drawing AREA to 16:9 (*0x1402f7e70 = 1.777): when the screen is wider,
//    area w *0x1404baf98 = h x 1.777, left *0x1404baf90 = (w - area w) / 2 -> the side bars on a 21:9 / 32:9 screen.
//  - every view maps its 640 x 480 rectangle (x *0x14057f3c4, y *0x14057f3c0, w *0x14057f39c, h *0x14057f3a4; ints) into that
//    area: FUN_1402930d0 = FUN_14028dbc0 (FUN_140021710: RSSetViewports(x * area w / 640 + left, ...)) + FUN_140292d40
//    (projection, with the view's w / h) + FUN_140293060 (scissor - a 640-unit rect, not the clip that matters).
//  - the projection FUN_140291570: x scale = cot(fov / 2), y scale = (w / h) x cot(fov / 2) - the fov is HORIZONTAL; for every
//    landscape view the game forces w / h = 853 / 480 (*0x1402f8208 / *0x1402f8198) and fov 1.309 (75 deg, *0x1402f7df4);
//    FUN_140292d40 forces the same for its culling planes.
//  - the full-screen frame FUN_1401c8110 draws all 3D of a view (stage, sky, fighters, effects) in FUN_1401c78c0 - the call at
//    0x1401c848c (e8 2f f4 ff ff) in the full-screen branch; split screen (*0x140530034 & 8) calls it at 0x1401c835d
//    (e8 5e f5 ff ff, left half: view x 0, y 78, w 320, h 240) and 0x1401c83f1 (e8 ca f4 ff ff, right half: x 320, same size);
//    what comes after it (HUD, menus) is 2D in the 16:9 area.
// v5.189.1: split screen too - the left half reaches the screen's left edge (x = -X, w = 320 + X), the right half the right
// edge (x 320, w = 320 + X), each with its own Hor+ projection; the bars above / below the halves (names, HUD) are not touched.
// What this does: when the screen is wider than 16:9, around that one call the view rectangle is widened in 640-units to
// reach the screen's edges (x = -X, w = 640 + 2X, X = left x 640 / area w) and the two forced constants become the wide
// aspect (480 x A) and a wider horizontal fov that keeps the VERTICAL view the same ("Hor+": 2 atan(tan(75 deg / 2) x A / (16/9)));
// then everything is put back and re-applied, so the HUD and menus stay in the 16:9 centre unchanged. With the vertical view
// kept, the 16:9 centre of the wide picture matches the stock picture pixel for pixel.
// Switch: CwoUltrawide.txt in the loader folder, first word "off" turns it off (no file = on; nothing happens on 16:9).
static const ULONG_PTR UW_WORLD = 0x1c78c0, UW_APPLY = 0x2930d0;
static const ULONG_PTR UW_SITES[3] = {0x1c848c, 0x1c835d, 0x1c83f1};   // full screen, split left, split right
static const ULONG_PTR UW_K853 = 0x2f8208, UW_KFOV = 0x2f7df4;
static const ULONG_PTR UW_VX = 0x57f3c4, UW_VY = 0x57f3c0, UW_VW = 0x57f39c, UW_VH = 0x57f3a4;
static const ULONG_PTR UW_ALEFT = 0x4baf90, UW_AW = 0x4baf98, UW_AH = 0x4baf9c;
static const unsigned char UW_OLD[3][5] = {{0xe8, 0x2f, 0xf4, 0xff, 0xff}, {0xe8, 0x5e, 0xf5, 0xff, 0xff}, {0xe8, 0xca, 0xf4, 0xff, 0xff}};
static int g_uw_state = 0, g_uw_switch = -1, g_uw_logged;
static float g_uw_k853, g_uw_kfov;
static ULONGLONG g_uw_check_t;
typedef void(__fastcall *PFN_UW_WORLD)(ULONG_PTR);
typedef void(__fastcall *PFN_UW_APPLY)(void);

static void __fastcall uw_world(ULONG_PTR frame) {            // replaces the full-screen world call (game thread)
    PFN_UW_WORLD world = (PFN_UW_WORLD)(g_base + UW_WORLD);
    bool wide = false;
    int keep[4] = {0, 0, 0, 0};
    __try {
        float left = *(float *)(g_base + UW_ALEFT), aw = *(float *)(g_base + UW_AW), ah = *(float *)(g_base + UW_AH);
        if (g_uw_switch == 1 && !ed_online() && left >= 1.0f && aw > 16.0f && ah > 16.0f) {
            int *vx = (int *)(g_base + UW_VX), *vy = (int *)(g_base + UW_VY), *vw = (int *)(g_base + UW_VW), *vh = (int *)(g_base + UW_VH);
            int X = (int)ceilf(left * 640.0f / aw), nx = 0, nw = 0;
            if (*vx == 0 && *vy == 0 && *vw == 640 && *vh == 480) { nx = -X; nw = 640 + 2 * X; }          // full screen
            else if (*vx == 0 && *vy == 78 && *vw == 320 && *vh == 240) { nx = -X; nw = 320 + X; }       // split, left half
            else if (*vx == 320 && *vy == 78 && *vw == 320 && *vh == 240) { nx = 320; nw = 320 + X; }    // split, right half
            if (nw) {
                float A = (nw * aw / 640.0f) / (*vh * ah / 480.0f);       // the widened viewport's aspect
                float w853 = 480.0f * A;
                float fov = 2.0f * atanf(tanf(g_uw_kfov * 0.5f) * A / (g_uw_k853 / 480.0f));
                keep[0] = *vx; keep[1] = *vy; keep[2] = *vw; keep[3] = *vh;
                int bit = *vw == 640 ? 1 : *vx == 0 ? 2 : 4;                   // log each view kind once
                if (mp_write(g_base + UW_K853, &w853, 4) && mp_write(g_base + UW_KFOV, &fov, 4)) {
                    *vx = nx; *vw = nw;
                    ((PFN_UW_APPLY)(g_base + UW_APPLY))();
                    wide = true;
                    if (!(g_uw_logged & bit)) {
                        g_uw_logged |= bit;
                        logf("ultrawide: screen area %.0f x %.0f, side bar %.0f px -> view x %d w %d (of %d x %d), aspect %.3f, horizontal fov %.1f deg (stock %.1f)",
                             aw + 2 * left, ah, left, nx, nw, keep[2], keep[3], A, fov * 57.29578f, g_uw_kfov * 57.29578f);
                    }
                } else {
                    mp_write(g_base + UW_K853, &g_uw_k853, 4); mp_write(g_base + UW_KFOV, &g_uw_kfov, 4);
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { wide = false; }
    world(frame);
    if (wide) {
        __try {
            mp_write(g_base + UW_K853, &g_uw_k853, 4);
            mp_write(g_base + UW_KFOV, &g_uw_kfov, 4);
            *(int *)(g_base + UW_VX) = keep[0]; *(int *)(g_base + UW_VY) = keep[1];
            *(int *)(g_base + UW_VW) = keep[2]; *(int *)(g_base + UW_VH) = keep[3];
            ((PFN_UW_APPLY)(g_base + UW_APPLY))();
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
}

static bool uw_install() {                                      // once: the three world calls -> a jump to uw_world
    if (g_uw_state) return g_uw_state == 1;
    g_uw_state = -1;
    for (int k = 0; k < 3; k++)
        if (memcmp((unsigned char *)(g_base + UW_SITES[k]), UW_OLD[k], 5)) { logf("ultrawide: world call %d is not the expected bytes - off", k); return false; }
    g_uw_k853 = *(float *)(g_base + UW_K853); g_uw_kfov = *(float *)(g_base + UW_KFOV);
    if (fabsf(g_uw_k853 - 853.0f) > 0.01f || fabsf(g_uw_kfov - 1.309f) > 0.001f) { logf("ultrawide: projection constants are not the expected ones - off"); return false; }
    ULONG_PTR page = sixf2_near_alloc(0x1000, PAGE_EXECUTE_READWRITE);
    if (!page) { logf("ultrawide: no stub page near the game - off"); return false; }
    unsigned char *s = (unsigned char *)page;
    s[0] = 0x48; s[1] = 0xb8;                                     // mov rax, uw_world
    ULONG_PTR fn = (ULONG_PTR)&uw_world; memcpy(s + 2, &fn, 8);
    s[10] = 0xff; s[11] = 0xe0;                                   // jmp rax (the game's call already pushed its return)
    FlushInstructionCache(GetCurrentProcess(), s, 12);
    unsigned char call[3][5];
    for (int k = 0; k < 3; k++) {
        LONGLONG rel = (LONGLONG)page - (LONGLONG)(g_base + UW_SITES[k] + 5);
        if (rel > 0x7fffffffLL || rel < -0x80000000LL) { VirtualFree((void *)page, 0, MEM_RELEASE); logf("ultrawide: stub out of reach - off"); return false; }
        call[k][0] = 0xe8; INT32 r32 = (INT32)rel; memcpy(call[k] + 1, &r32, 4);
    }
    for (int k = 0; k < 3; k++) {
        unsigned char *site = (unsigned char *)(g_base + UW_SITES[k]);
        if (!mp_write((ULONG_PTR)site, call[k], 5)) {
            for (int j = 0; j < k; j++) mp_write(g_base + UW_SITES[j], UW_OLD[j], 5);   // all or none
            logf("ultrawide: code not writable - off"); return false;
        }
        FlushInstructionCache(GetCurrentProcess(), site, 5);
    }
    g_uw_state = 1;
    logf("ultrawide: world calls exe+0x%llx / 0x%llx / 0x%llx -> %p (wide view only on screens wider than 16:9)",
         (unsigned long long)UW_SITES[0], (unsigned long long)UW_SITES[1], (unsigned long long)UW_SITES[2], (void *)page);
    return true;
}

static void uw_step() {                                         // game thread, every drawn frame
    ULONGLONG now = GetTickCount64();
    if (now - g_uw_check_t < 3000) return;
    g_uw_check_t = now;
    // v5.199.0 owner 2026-11-22 ("add the following to the BFR mods"): in the BFR edition too
    wchar_t p[MAX_PATH]; FILE *f = NULL; int on = 1;
    swprintf_s(p, MAX_PATH, L"%sCwoUltrawide.txt", g_dir);
    if (cwo_wfopen(&f, p, L"r") == 0 && f) {
        char w[16] = "";
        if (fscanf_s(f, "%15s", w, (unsigned)sizeof(w)) == 1 && !_stricmp(w, "off")) on = 0;
        fclose(f);
    }
    if (on && g_uw_state == 0) uw_install();
    if (on != g_uw_switch) { logf("ultrawide: %s (CwoUltrawide.txt)", on ? "ON (offline, screens wider than 16:9)" : "off"); g_uw_switch = on; g_uw_logged = 0; }
}
