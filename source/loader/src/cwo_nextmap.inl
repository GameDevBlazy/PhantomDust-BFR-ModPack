// ---- v5.54 Next Map (owner 2026-11-01 ~21:xx: "i want a next map mode where it selects the next map automaticaly so that we
// dont always have to do it"): in the Xbox Live / system link lobby, after each match the host's session moves to the next
// stage of a rotation by itself.
// Game facts (Ghidra full pass 065, pd_fn; the v5.16 Change Rule research, cwo_arsenal.inl): the session's match copy holds
// the stage at *0x140527bc8 (SESSION INFO 4.593), next to the type 0x1405277b4 and rule 0x1405277fc; the host's player info
// FUN_1401910a0 sends all three (local + 0x28 / +0x30 / +0x34 of the packet) and every other member's FUN_140191a60 copies
// them back into the same globals; FUN_140191210 is the game's own resend of this machine's player info (the Change Rule
// route). The host is local member *0x140527bc4 == 0.
// What this does: with the switch on (<data>\CwoNextMap.txt first line "on"), for the host only: each match start records
// the stage being played (a rotation of the stages this session has used, in order), and each match end writes the next
// stage of the rotation into the match copy and resends - the same write + resend as Change Rule. Lines after "on" may list
// stage numbers to rotate through instead (the numbers SESSION INFO shows; the loader logs every stage it sees). It never
// writes a number it has not seen played or been given.
// Online gate: AGENTS.md has no exception for this yet, so NM_ONLINE stays false and the module only logs in a Live session.
static const bool NM_ONLINE = true;                             // v5.55 AGENTS.md "Multiplayer rules online" (owner 2026-11-01)
static const ULONG_PTR RVA_NM_STAGE = 0x527bc8, RVA_NM_ME = 0x527bc4, RVA_NM_SEND = 0x191210;   // as cwo_arsenal.inl
static int g_nm_on = -1, g_nm_rot[32], g_nm_n, g_nm_given, g_nm_cur = -1;
static bool g_nm_was_match;
static int *g_nm_test;                                          // self-test: me, stage, sends

static void nm_load() {
    g_nm_on = 0; g_nm_n = 0; g_nm_given = 0;
    wchar_t path[MAX_PATH];
    swprintf_s(path, MAX_PATH, L"%sCwoNextMap.txt", g_dir);
    FILE *f = NULL;
    if (cwo_wfopen(&f, path, L"r") != 0 || !f) return;
    char line[64];
    if (fgets(line, sizeof(line), f) && !_strnicmp(line, "on", 2)) g_nm_on = 1;
    int v;
    while (g_nm_n < 32 && fgets(line, sizeof(line), f)) if (sscanf_s(line, "%d", &v) == 1 && v >= 0 && v < 4096) g_nm_rot[g_nm_n++] = v;
    g_nm_given = g_nm_n;
    fclose(f);
    logf("next map: %s, %d stages given", g_nm_on ? "on" : "off", g_nm_given);
}

static void nm_save_switch() {                                  // v5.57 the lobby's Next Map row: the switch into CwoNextMap.txt
    wchar_t path[MAX_PATH];
    swprintf_s(path, MAX_PATH, L"%sCwoNextMap.txt", g_dir);
    FILE *f = NULL;
    if (cwo_wfopen(&f, path, L"w") != 0 || !f) return;
    fprintf(f, "%s\n", g_nm_on > 0 ? "on" : "off");
    for (int i = 0; i < g_nm_given; i++) fprintf(f, "%d\n", g_nm_rot[i]);
    fclose(f);
    logf("next map: switched %s", g_nm_on > 0 ? "on" : "off");
}

static int nm_next(int cur) {                                   // the stage after cur in the rotation (-1: none)
    if (g_nm_n < 2) return -1;
    for (int i = 0; i < g_nm_n; i++) if (g_nm_rot[i] == cur) return g_nm_rot[(i + 1) % g_nm_n];
    return g_nm_rot[0];
}

static void nm_seen(int stage) {                                // a stage played: into the rotation (not with a given list)
    if (g_nm_given) return;
    for (int i = 0; i < g_nm_n; i++) if (g_nm_rot[i] == stage) return;
    if (g_nm_n < 32) g_nm_rot[g_nm_n++] = stage;
}

static void nm_step() {                                         // game thread, every frame
    if (g_ed_test_gs && !g_nm_test) return;
    if (g_nm_on < 0) nm_load();
    bool live = g_nm_test ? true : ed_online();
    bool start = g_in_match && !g_nm_was_match, end = !g_in_match && g_nm_was_match;
    g_nm_was_match = g_in_match != 0;
    if (!g_nm_on || !live || (!start && !end)) return;
    __try {
        int me = g_nm_test ? g_nm_test[0] : *(int *)(g_base + RVA_NM_ME);
        int *stage = g_nm_test ? &g_nm_test[1] : (int *)(g_base + RVA_NM_STAGE);
        if (me != 0) return;                                    // the host only
        if (start) { g_nm_cur = *stage; nm_seen(*stage); logf("next map: match on stage %d (rotation %d stages)", *stage, g_nm_n); return; }
        int nx = nm_next(g_nm_cur);
        if (nx < 0) { logf("next map: one stage in the rotation so far - stays on %d", g_nm_cur); return; }
        if (!NM_ONLINE && !g_nm_test) { logf("next map: would move to stage %d - off online until the AGENTS.md exception", nx); return; }
        *stage = nx;
        if (g_nm_test) g_nm_test[2]++;
        else ((void(__fastcall *)())(g_base + RVA_NM_SEND))();
        char msg[64]; sprintf_s(msg, "NEXT MAP: stage %d", nx); set_status(msg);
        logf("next map: stage %d -> %d, sent to the other players", g_nm_cur, nx);
    } __except (EXCEPTION_EXECUTE_HANDLER) { logf("next map: update failed"); }
}

static int nm_self_test() {                                     // learned rotation, host only, wrap-around
    int t[3] = {0, 4, 0};
    int keep_on = g_nm_on, keep_n = g_nm_n, keep_g = g_nm_given, keep_cur = g_nm_cur, keep_rot[32];
    memcpy(keep_rot, g_nm_rot, sizeof(keep_rot));
    LONG keep_m = g_in_match; bool keep_w = g_nm_was_match;
    g_nm_test = t; g_nm_on = 1; g_nm_n = 0; g_nm_given = 0; g_nm_was_match = false;
    g_in_match = 1; nm_step(); g_in_match = 0; nm_step();               // stage 4 only: stays
    bool one = t[1] == 4 && t[2] == 0;
    t[1] = 7; g_in_match = 1; nm_step(); g_in_match = 0; nm_step();     // 4, 7: after 7 comes 4
    bool wrap = t[1] == 4 && t[2] == 1;
    t[0] = 1; g_in_match = 1; nm_step(); g_in_match = 0; nm_step();     // not the host: nothing
    bool guest = t[1] == 4 && t[2] == 1;
    g_nm_test = NULL; g_nm_on = keep_on; g_nm_n = keep_n; g_nm_given = keep_g; g_nm_cur = keep_cur; memcpy(g_nm_rot, keep_rot, sizeof(keep_rot));
    g_in_match = keep_m; g_nm_was_match = keep_w;
    bool ok = one && wrap && guest;
    logf("self-test: next map one stage %s, rotation %s, guest untouched %s", one ? "ok" : "FAILED", wrap ? "ok" : "FAILED", guest ? "ok" : "FAILED");
    return ok ? 1 : -88;
}
