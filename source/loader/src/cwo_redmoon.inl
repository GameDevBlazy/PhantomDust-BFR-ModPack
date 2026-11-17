// v5.188.23 Red Moon (BFR skill 434; owner 2026-11-16). Independently authored.
// Game facts used (capstone of the clean exe 9ba7411b, 2026-11-17, MODLOG):
// - on-hit effect 12 (switch FUN_140272bc0, case at 0x140272fcc) = FUN_14024d5c0(target status block, status 0, value +0x64,
//   frames +0x66); FUN_14024d5c0 with frames 0 adds the value to block +0x61b8 + status*12 (capped by 0x1402dcf30[status]) and
//   keeps it - a permanent +N to the fighter's attack STR (read back by FUN_14024d4d0(block, 0) in the damage FUN_140272400);
// - the status block of a fighter is actor +0x40 (mr_flag, cwo_mirror.inl);
// - a cast shows as a flying record of the fighter (actor +0x4E0, 48 x 0x1F0, +0x10 state, +0x14 skill);
// - the stage's start entries: *(0x1405300a8), count *(byte *)0x14052dd62, 0x50 bytes each, position at +0.
// The record 434 itself does nothing harmful (no STR, no on-hit, a 10-frame object: CwoBFR.txt); the loader makes the moon:
// Bloody Ritual's (158) looping flying effect, 2.5 m above the middle of the starts; the first time each fighter comes within
// 5 m of it, the game's own status call gives that fighter +2 attack STR for the match, once. Purify (182) or a skill named
// Armageddon (if the pack has one) put the moon out. The moon and the marks end with the match.
typedef void(__fastcall *PFN_SET_STATUS)(ULONG_PTR block, int status, int value, int frames);
static const ULONG_PTR RVA_SET_STATUS = 0x24d5c0;
static const int RMN_SKILL = 434, RMN_LOOK = 158, RMN_PURIFY = 182;
static unsigned g_rmn_h = FX_NONE;
static bool g_rmn_on, g_rmn_blessed[8];
static float g_rmn_pos[3];
static __declspec(align(16)) float g_rmn_m[16];               // the effect's matrix (the game keeps the pointer): 2x size, at the moon
static void rmn_matrix() { memset(g_rmn_m, 0, sizeof(g_rmn_m)); g_rmn_m[0] = g_rmn_m[5] = g_rmn_m[10] = 2.0f; g_rmn_m[15] = 1.0f; g_rmn_m[12] = g_rmn_pos[0]; g_rmn_m[13] = g_rmn_pos[1]; g_rmn_m[14] = g_rmn_pos[2]; }
static unsigned char g_rmn_seen[8][48];
static int g_rmn_armageddon = -2;                                // -2 not looked up yet, -1 none in the pack
static void rmn_off(FxGame *g, const char *why) {
    if (g && g_rmn_h != FX_NONE && fx_alive(*g, g_rmn_h)) g->stop(g_rmn_h);
    if (g_rmn_on) logf("Red Moon: out (%s)", why);
    g_rmn_h = FX_NONE; g_rmn_on = false;
}
static bool rmn_centre(float *out) {
    __try {
        const char *e = *(const char **)(g_base + RVA_CP_ENTRIES);
        int n = *(const unsigned char *)(g_base + RVA_CP_COUNT);
        if (!e || n <= 0) return false;
        float s[3] = {0, 0, 0}, top = -1e9f;
        for (int i = 0; i < n && i < 32; i++) {
            const float *p = (const float *)(e + (ULONG_PTR)i * CP_ENTRY);
            s[0] += p[0]; s[1] += p[1]; s[2] += p[2];
            if (p[1] > top) top = p[1];
        }
        int m = n < 32 ? n : 32;
        out[0] = s[0] / m; out[2] = s[2] / m; out[1] = (s[1] / m > top - 40.0f ? s[1] / m : top) + 25.0f;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static void rmn_step(bool in_match) {
    FxGame g;
    bool fx = fx_game(&g);
    if (!in_match || !g_bfr_applied || (ed_online() && !g_bfr_edition)) {
        if (g_rmn_on || g_rmn_h != FX_NONE) rmn_off(fx ? &g : NULL, "the match ended");
        memset(g_rmn_blessed, 0, sizeof(g_rmn_blessed)); memset(g_rmn_seen, 0, sizeof(g_rmn_seen));
        return;
    }
    if (!fx) return;
    if (g_rmn_armageddon == -2) {
        g_rmn_armageddon = -1;
        for (int i = 0; i < g_bfr_n; i++) if (!_strnicmp(g_bfr[i].name, "Armageddon", 10)) g_rmn_armageddon = g_bfr[i].id;
    }
    __try {
        ULONG_PTR *acts = cwo_actors();
        int nf = cwo_nfighters(); if (nf > 8) nf = 8;
        bool cast = false, clear = false;
        for (int p = 0; p < nf; p++) {                           // casts of Red Moon / Purify / Armageddon by anyone
            ULONG_PTR a = acts[p];
            if (!a || !*(int *)(a + 0x20)) continue;
            for (int k = 0; k < 48; k++) {
                ULONG_PTR sl = a + 0x4E0 + 0x1F0 * (ULONG_PTR)k;
                int st = *(int *)(sl + 0x10), sk = *(int *)(sl + 0x14);
                unsigned char now = st == 0 ? 0 : (sk == RMN_SKILL ? 1 : (sk == RMN_PURIFY || (g_rmn_armageddon > 0 && sk == g_rmn_armageddon)) ? 2 : 0);
                if (now && now != g_rmn_seen[p][k]) { if (now == 1) cast = true; else clear = true; }
                g_rmn_seen[p][k] = now;
            }
        }
        if (clear && g_rmn_on) rmn_off(&g, "Purify / Armageddon");
        if (cast && !g_rmn_on && rmn_centre(g_rmn_pos)) {
            rmn_matrix();
            short prof = *(short *)(g.skills + 0x14 + (ULONG_PTR)RMN_LOOK * 0x90);
            int req = g.effect_of(prof, RMN_LOOK);
            unsigned h = req > 0 ? g.start(g.mgr, spx_owner(g), req, 1, g_rmn_m, 0) : FX_NONE;
            if (h != FX_NONE) { g.loop(h); sfx_pin(g, h, g_rmn_m); }
            g_rmn_h = h; g_rmn_on = true;
            logf("Red Moon: up at (%.0f, %.0f, %.0f), effect %d handle %08X", g_rmn_pos[0], g_rmn_pos[1], g_rmn_pos[2], req, h);
        }
        if (!g_rmn_on) return;
        if (g_rmn_h != FX_NONE && fx_alive(g, g_rmn_h)) { g.move(g_rmn_h, g_rmn_m); sfx_pin(g, g_rmn_h, g_rmn_m); }
        else {                                                   // the game dropped it (effect slots): start it again
            short prof = *(short *)(g.skills + 0x14 + (ULONG_PTR)RMN_LOOK * 0x90);
            int req = g.effect_of(prof, RMN_LOOK);
            g_rmn_h = req > 0 ? g.start(g.mgr, spx_owner(g), req, 1, g_rmn_m, 0) : FX_NONE;
            if (g_rmn_h != FX_NONE) { g.loop(g_rmn_h); sfx_pin(g, g_rmn_h, g_rmn_m); }
        }
        for (int p = 0; p < nf; p++) {                           // first entry into the moon's light: +2 STR for the match
            ULONG_PTR a = acts[p];
            if (!a || !*(int *)(a + 0x20) || g_rmn_blessed[p]) continue;
            const float *ap = (const float *)(a + ACTOR_POS);
            float dx = ap[0] - g_rmn_pos[0], dz = ap[2] - g_rmn_pos[2], dy = ap[1] - g_rmn_pos[1];
            if (dx * dx + dz * dz > 50.0f * 50.0f || dy < -80.0f || dy > 40.0f) continue;
            ((PFN_SET_STATUS)(g_base + RVA_SET_STATUS))(a + 0x40, 0, 2, 0);
            g_rmn_blessed[p] = true;
            logf("Red Moon: fighter %d entered its light - attack STR +2 for the match (status 0 now %d)", p + 1, *(int *)(a + 0x40 + 0x61b8));
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { g_rmn_on = false; g_rmn_h = FX_NONE; }
}
