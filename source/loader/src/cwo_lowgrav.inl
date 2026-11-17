// ---- v5.121 LOW GRAVITY STAGES (owner 2026-11-09). Offline matches only, on the Map Slot 7 packs
// listed in CwoLowGravity.txt ("mxNNN percent [fall cap]", one per line; default: mx329 Driftstone Sanctum 25 2.2).
// Game facts (PD Ghidra full pass 065 via pd_fn.py, MODLOG 2026-11-09):
//   FUN_140254590 (a fighter's step): velocity y actor +0x12E84 -= gravity actor +0x12E80; if below -7.5 (DAT_1402f8330) it
//   is set to -7.5; then y +0x12E44 += velocity. The normal gravity 0.12 (0x3DF5C28F) is written by FUN_14024e190 (spawn)
//   and FUN_140254c80 / 140258690 / 14025c260; a skill motion may set its own (FUN_140257b90 param_5) - left alone.
// In game 2026-11-09: the jump sets its own gravity 0.288 (not 0.12), so every gravity the game gives a fighter is scaled.
// What this does: while a fighter is FALLING (velocity < 0), its gravity (whatever the game set: 0.12, the jump's 0.288, a
// skill's) becomes percent of that value and its fall speed is held at the cap; rising puts the game's value back, so jumps
// reach their usual height and float down.
static bool ed_online();                                        // cwo_edgar.inl
static const ULONG_PTR ACT_VY = 0x12E84, ACT_GRAV = 0x12E80;
struct LgStage { char file[16]; float pct; float cap; };
static LgStage g_lg[16];
static int g_lg_n = -1, g_lg_cur = -2;
static float g_lg_set[6], g_lg_orig[6];                                       // the value we wrote per fighter (to put 0.12 back on rising)
static unsigned g_lg_logs;

static void lg_load() {
    g_lg_n = 0;
    wchar_t path[MAX_PATH];
    swprintf_s(path, MAX_PATH, L"%sCwoLowGravity.txt", g_dir);
    FILE *fp = NULL;
    if (_wfopen_s(&fp, path, L"r") == 0 && fp) {
        char line[128];
        while (fgets(line, sizeof(line), fp) && g_lg_n < 16) {
            LgStage s = {}; s.cap = 2.2f;
            if (line[0] == '#' || sscanf_s(line, "%15s %f %f", s.file, (unsigned)sizeof(s.file), &s.pct, &s.cap) < 2) continue;
            if (s.pct < 5 || s.pct > 100 || s.cap < 0.5f || s.cap > 7.5f) { logf("low gravity: line '%s' refused (percent 5-100, cap 0.5-7.5)", s.file); continue; }
            g_lg[g_lg_n++] = s;
        }
        fclose(fp);
    } else {
        strcpy_s(g_lg[0].file, "mx329"); g_lg[0].pct = 25; g_lg[0].cap = 2.2f; g_lg_n = 1;
    }
    logf("low gravity: %d stage(s)%s", g_lg_n, g_lg_n ? "" : " - off");
}
static void lg_step() {                                          // game thread, every drawn frame
    if (g_lg_n < 0) lg_load();
    int idx = -1;
    if (g_in_match && !ed_online() && *(int *)(g_base + RVA_STAGE) == LIST_STAGE) {
        LONG k = g_mx_cur;
        if (k >= 0 && k < g_mx_count)
            for (int i = 0; i < g_lg_n; i++) if (!_stricmp(g_mx[k].file, g_lg[i].file)) { idx = i; break; }
    }
    if (idx != g_lg_cur) {
        if (idx >= 0) logf("low gravity: on for %s (%.0f%% while falling, fall speed at most %.1f)", g_lg[idx].file, g_lg[idx].pct, g_lg[idx].cap);
        else if (g_lg_cur >= 0) logf("low gravity: off");
        g_lg_cur = idx;
        for (int p = 0; p < 6; p++) g_lg_set[p] = 0;
    }
    if (idx < 0) return;
    const float cap = -g_lg[idx].cap;
    __try {
        ULONG_PTR *acts = cwo_actors();
        for (int p = 0; p < 4; p++) {
            ULONG_PTR a = acts[p];
            if (!a || !*(int *)(a + 0x20)) continue;
            float *g = (float *)(a + ACT_GRAV), *vy = (float *)(a + ACT_VY);
            bool ours = g_lg_set[p] != 0 && *g == g_lg_set[p];
            if (!ours) { g_lg_set[p] = 0; g_lg_orig[p] = *g; }        // the game's own value (0.12, the jump's 0.288, a skill's)
            if (g_lg_orig[p] <= 0 || g_lg_orig[p] > 2.0f) continue;     // none, or not a fall (left alone)
            if (*vy < 0) {
                float low = g_lg_orig[p] * g_lg[idx].pct / 100.0f;
                *g = low; g_lg_set[p] = low;
                if (*vy < cap) *vy = cap;
            } else if (ours) {
                *g = g_lg_orig[p]; g_lg_set[p] = 0;                      // rising again: the game's own gravity
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (g_lg_logs++ < 3) logf("low gravity: fighter read failed - skipped this frame");
    }
}
