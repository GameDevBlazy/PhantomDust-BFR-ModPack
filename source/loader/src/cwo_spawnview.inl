// ---- v5.187.0 spawn view (owner 2026-11-15). Offline matches only, switched on by
// <data>\CwoSpawnView.txt ("on"); nothing changes in the match itself.
// Game facts (recorded, cwo_forge.inl / MODLOG 2026-10-20 batch 026-029): the start block in use = entries *0x1405300a8,
// count u8 *0x14052dd62, 0x50-byte entries (+0x00 position f32[3], +0x14 / +0x20 / +0x2C the 3 capsule spots); a stage .dat
// keeps its blocks behind header words 0xA4..0xBC ('CP00', u16 n, u16 size, n entries) (piece_stage v17 / start_fix.py).
// Effects through the game's own effect API (cwo_loader.cpp FxGame, as the downed glow / Forge markers): every start shows
// skill 122's effect (the Forge spawn marker look), every capsule spot skill 1's (the downed-glow look), looping, pinned.
// Camera: player 1's own view matrix (fighter + 0x13f50) rebuilt with the game's look-at - an overhead view, then one from
// each side, 7 s each, repeating; the game builds its own again when the spawn view is off.
static bool g_sv_on;
// CwoSpawnView.txt: "on [start skill] [start part] [start size] [capsule skill] [capsule part] [capsule size] [camera 0/1]"
static int g_sv_sk[2] = {122, 1}, g_sv_tr[2] = {0, 3}, g_sv_cam = 1; static float g_sv_sc[2] = {0.3f, 0.2f};
static int g_sv_n, g_sv_keep_flag = -1, g_sv_view;
struct SvPt { float p[3]; int kind; unsigned h; int fail; };   // kind 0 start, 1 capsule spot
static SvPt g_sv[64];
static __declspec(align(16)) float g_sv_m[64][16];
static ULONGLONG g_sv_t0, g_sv_tv;
static bool g_sv_fc_asked;
static float g_sv_c[3], g_sv_r;

static void sv_add(const float *p, int kind) {
    if (g_sv_n >= 64 || (p[0] == 0 && p[1] == 0 && p[2] == 0)) return;
    for (int i = 0; i < g_sv_n; i++)
        if (g_sv[i].kind == kind && fabsf(g_sv[i].p[0] - p[0]) + fabsf(g_sv[i].p[1] - p[1]) + fabsf(g_sv[i].p[2] - p[2]) < 1.0f) return;
    SvPt &s = g_sv[g_sv_n++];
    memcpy(s.p, p, 12); s.kind = kind; s.h = FX_NONE; s.fail = 0;
}
static void sv_add_entries(const unsigned char *e, int n, int size) {
    for (int k = 0; k < n; k++) {
        const unsigned char *q = e + (size_t)k * size;
        sv_add((const float *)q, 0);
        for (int j = 0; j < 3; j++) sv_add((const float *)(q + 0x14 + 12 * j), 1);
    }
}
static void sv_collect() {                                       // the block in use + every block of a Map Slot 7 pack's file
    g_sv_n = 0;
    __try {
        const unsigned char *e = *(const unsigned char **)(g_base + 0x5300a8);
        int n = *(unsigned char *)(g_base + 0x52dd62);
        if (e && n > 0 && n <= 8) sv_add_entries(e, n, 0x50);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    int stage = *(int *)(g_base + 0x3e0304);
    if (stage == LIST_STAGE && g_mx_cur >= 0 && g_mx_cur < g_mx_count && g_dir[0]) {
        wchar_t path[MAX_PATH];
        swprintf_s(path, MAX_PATH, L"%sgame\\Assets\\Data\\Map\\%S.dat", g_dir, g_mx[g_mx_cur].file);
        FILE *f = NULL;
        if (cwo_wfopen(&f, path, L"rb") == 0 && f) {
            fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
            unsigned char *b = len > 0xC0 && len < 64 * 1024 * 1024 ? (unsigned char *)malloc(len) : NULL;
            if (b && fread(b, 1, len, f) == (size_t)len) {
                int seen[7], ns = 0;
                for (int w = 0xA4; w <= 0xBC; w += 4) {
                    int o = *(int *)(b + w);
                    bool dup = false;
                    for (int i = 0; i < ns; i++) dup |= seen[i] == o;
                    if (dup || o <= 0 || o + 8 > len || memcmp(b + o, "CP00", 4)) continue;
                    seen[ns++] = o;
                    int n = *(unsigned short *)(b + o + 4), size = *(unsigned short *)(b + o + 6);
                    if (n > 0 && n <= 8 && size / n >= 0x38 && o + 8 + size <= len) sv_add_entries(b + o + 8, n, size / n);
                }
            }
            free(b);
            fclose(f);
        }
    }
    float c[3] = {0, 0, 0};
    int ns = 0;
    for (int i = 0; i < g_sv_n; i++) if (g_sv[i].kind == 0) { for (int j = 0; j < 3; j++) c[j] += g_sv[i].p[j]; ns++; }
    if (ns) for (int j = 0; j < 3; j++) c[j] /= ns;
    float r = 100.0f;
    for (int i = 0; i < g_sv_n; i++) { float d = sqrtf((g_sv[i].p[0] - c[0]) * (g_sv[i].p[0] - c[0]) + (g_sv[i].p[2] - c[2]) * (g_sv[i].p[2] - c[2])); if (d > r) r = d; }
    memcpy(g_sv_c, c, 12); g_sv_r = r;
    int nc = 0;
    for (int i = 0; i < g_sv_n; i++) nc += g_sv[i].kind;
    logf("spawn view: %d starts, %d capsule spots (stage %d), centre (%.0f, %.0f, %.0f), radius %.0f", g_sv_n - nc, nc, stage, c[0], c[1], c[2], r);
    for (int i = 0; i < g_sv_n; i++)
        logf("spawn view:   %s %d (%.1f, %.1f, %.1f)", g_sv[i].kind ? "capsule" : "start", i, g_sv[i].p[0], g_sv[i].p[1], g_sv[i].p[2]);
}
static void sv_stop(const char *why) {
    FxGame g;
    if (fx_game(&g)) for (int i = 0; i < g_sv_n; i++) if (fx_alive(g, g_sv[i].h)) g.stop(g_sv[i].h);
    for (int i = 0; i < g_sv_n; i++) g_sv[i].h = FX_NONE;
    g_sv_keep_flag = -1;
    if (g_sv_on) logf("spawn view: off (%s)", why);
    g_sv_on = false;
}
static void sv_step() {                                          // game thread, every drawn frame
    static ULONGLONG s_cfg;
    static bool s_want;
    ULONGLONG now = GetTickCount64();
    if (now - s_cfg > 2000) {
        s_cfg = now;
        wchar_t path[MAX_PATH];
        swprintf_s(path, MAX_PATH, L"%sCwoSpawnView.txt", g_dir);
        FILE *f = NULL; char t[128] = "";
        s_want = false;
        if (cwo_wfopen(&f, path, L"r") == 0 && f) {
            if (fgets(t, sizeof(t), f)) {
                s_want = !_strnicmp(t, "on", 2);
                int sk0 = 122, tr0 = 0, sk1 = 1, tr1 = 3, cam = 1; float sc0 = 0.3f, sc1 = 0.2f;
                if (s_want) sscanf_s(t + 2, "%d %d %f %d %d %f %d", &sk0, &tr0, &sc0, &sk1, &tr1, &sc1, &cam);
                if (sk0 != g_sv_sk[0] || tr0 != g_sv_tr[0] || sc0 != g_sv_sc[0] || sk1 != g_sv_sk[1] || tr1 != g_sv_tr[1] || sc1 != g_sv_sc[1] || cam != g_sv_cam) {
                    if (g_sv_on) sv_stop("settings changed");
                    g_sv_sk[0] = sk0; g_sv_tr[0] = tr0; g_sv_sc[0] = sc0; g_sv_sk[1] = sk1; g_sv_tr[1] = tr1; g_sv_sc[1] = sc1; g_sv_cam = cam;
                    g_sv_t0 = 0;
                }
            }
            fclose(f);
        }
    }
    if (!g_in_match || !s_want || ed_online()) { if (g_sv_on) sv_stop(!g_in_match ? "the match ended" : "switched off"); g_sv_t0 = 0; return; }
    if (!g_sv_t0) g_sv_t0 = now;
    if (now - g_sv_t0 < 4000) return;                            // after the intro poses
    __try {
        if (!g_sv_on) {
            sv_collect();
            if (!g_sv_n) { g_sv_t0 = now + 600000; return; }
            g_sv_on = true; g_sv_view = 0; g_sv_tv = now;
        }
        FxGame g;
        ULONG_PTR a = cwo_actors()[0];
        if (fx_game(&g) && a) {
            for (int i = 0; i < g_sv_n; i++) {
                SvPt &s = g_sv[i];
                float *m = g_sv_m[i];
                memset(m, 0, 64);
                float sc = g_sv_sc[s.kind];
                m[0] = m[5] = m[10] = sc; m[15] = 1.0f;
                m[12] = s.p[0]; m[13] = s.p[1] + (s.kind ? 0.0f : 5.0f); m[14] = s.p[2];
                if (fx_alive(g, s.h)) { g.move(s.h, m); sfx_pin(g, s.h, m); continue; }
                if (s.fail > 200) continue;
                int id = g_sv_sk[s.kind];
                if (id < 0 || id >= 753 || sc <= 0) { s.fail = 999; continue; }
                short prof = *(short *)(g.skills + 0x14 + (ULONG_PTR)id * 0x90);
                int req = prof > 0 ? g.effect_of(prof, id) : 0;
                if (req <= 0) { s.fail = 999; continue; }
                unsigned h = g.start(g.mgr, *(int *)(a + 0x14), req, g_sv_tr[s.kind], m, 0);
                if (h == FX_NONE) {
                    if (++s.fail == 30) { typedef INT64(__fastcall * PFN_FX_LOAD)(ULONG_PTR, int, int); ((PFN_FX_LOAD)(g_base + 0x189e80))(g.mgr, req, 0); }
                    continue;
                }
                g.loop(h); sfx_pin(g, h, m); s.h = h; s.fail = 0;
            }
        }
        if (!g_sv_cam || !a) return;
        // v5.187.1 the camera: in split screen every view is drawn from its fighter's own camera (FUN_14012bfc0 copies
        // fighter + 0x13f50, the view matrix built from eye +0x13f2c / target +0x13f38) - so player 1's view matrix is
        // rebuilt here with the game's own look-at FUN_1402912d0(out, target, eye) after the game set it
        if (now - g_sv_tv > 7000) { g_sv_tv = now; g_sv_view = (g_sv_view + 1) % 5; logf("spawn view: camera view %d", g_sv_view); }
        float r = g_sv_r, eye[3], tgt[3];
        memcpy(tgt, g_sv_c, 12);
        if (g_sv_view == 0) { eye[0] = g_sv_c[0]; eye[1] = g_sv_c[1] + 2.3f * r; eye[2] = g_sv_c[2] - 0.45f * r; }
        else {
            float ang = 1.5707963f * (g_sv_view - 1) + 0.7853982f;
            eye[0] = g_sv_c[0] + sinf(ang) * 1.45f * r; eye[1] = g_sv_c[1] + 0.95f * r; eye[2] = g_sv_c[2] + cosf(ang) * 1.45f * r;
        }
        typedef void(__fastcall * PFN_LOOKAT)(float *, const float *, const float *);
        memcpy((void *)(a + 0x13f2c), eye, 12);
        memcpy((void *)(a + 0x13f38), tgt, 12);
        ((PFN_LOOKAT)(g_base + 0x2912d0))((float *)(a + 0x13f50), tgt, eye);
    } __except (EXCEPTION_EXECUTE_HANDLER) { logf("spawn view: fault - off"); g_sv_on = false; g_sv_t0 = now + 600000; }
}
