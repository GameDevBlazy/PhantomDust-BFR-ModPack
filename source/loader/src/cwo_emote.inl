// ---- v5.188.55 EMOTES (owner voice memo 2026-11-21, item 10: "own idle animations, motions and emotes"). Offline matches,
// MODS edition only. Hold View (Back) and press the D-pad, while you stand:
//   Up    = our own "Look around" (animation slot 1, written by tools/idle_v1.py / idle_all.py - our own keys)
//   Right = the victory pose (EXMOT2, slot 132)      Left = the intro pose (EXMOT0, slot 130)
//   Down  = the second intro pose (EXMOT1, slot 131)
// Game facts (cwo_intro.inl, Ghidra pass 065, MODLOG 2026-11-02 / 11-03): FUN_140252510(fighter, slot, 2, -1) leaves a motion
// request that the fighter's step (FUN_1402526c0) plays once while the fighter stands (+0x12f04 == 0); slots >= 130 are loaded
// first (exmot<n>.ala); FUN_140252510(fighter, 0, 0, -1) = back to standing. Which EXMOT is which: the owner watched the probe
// 2026-11-03 (130 intro, 131 another intro, 132 victory, 133 losing, 134 another intro, 135 a T pose).
// Any move, skill or hit takes over at once, as the game does for its own motions.
// v5.188.62 OUR OWN emotes (no game motion replaced): Wave and Bow (v5.188.64: View + D-pad Right / Left). Their motions are our own .ala files
// (30_PD_UE_Stage_Bridge/tools/emote_ala.py: our keys on each fighter's standing stance) in the loader's game folder,
// Assets\Data\Player\pcNNa\cwowave.ala / cwobow.ala. Game facts (Ghidra pass 065, pd_fn.py, 2026-11-21): FUN_140252510 with
// slot >= 130 calls FUN_140259de0, which - only while no EXMOT load is pending (fighter +0x12f00 +0xa68 == 0) - asks for the
// file named by the table 0x1402dcff0 [slot - 130] in the fighter's folder (*(fighter +0x12f00 +0xbd0) + 0x12cb8) through
// FUN_140196ee0, which COPIES the name into its request. So the loader points table entry 5 (slot 135) at our name for that
// one call and puts the game's pointer back at once; the game's own EXMOT5 loads as before on its next request. A fighter
// without our file (pc11a, monsters) is refused before the request (a missing EXMOT file is how NPC attacks crashed).
static const ULONG_PTR EM_EXMOT_TABLE_RVA = 0x2dcff0;
static const char EM_WAVE[] = "cwowave.ala", EM_BOW[] = "cwobow.ala";
static bool em_own_file(const char *folder, const char *name) {
    // v5.188.65: the folder text is "player\<name>" with NO trailing backslash (FUN_140250f70: sprintf "player\\%s"), relative to
    // Assets\Data\ - the game adds the separator itself (its own exmot*.ala load from there), so the check adds it too
    wchar_t rel[MAX_PATH], p[MAX_PATH];
    size_t fl = strlen(folder);
    bool sep = fl > 0 && (folder[fl - 1] == '\\' || folder[fl - 1] == '/');
    if (swprintf_s(rel, MAX_PATH, sep ? L"%S%S" : L"%S\\%S", folder, name) < 0) return false;
    for (wchar_t *c = rel; *c; c++) if (*c == L'/') *c = L'\\';
    static const wchar_t *mids[3] = {L"Assets\\Data\\", L"Assets\\", L""};
    const wchar_t *roots[2] = {g_over, g_install};
    for (int r = 0; r < 2; r++) {
        if (!roots[r][0]) continue;
        for (int m = 0; m < 3; m++)
            if (swprintf_s(p, MAX_PATH, L"%s%s%s", roots[r], mids[m], rel) >= 0 && GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES) return true;
    }
    return false;
}
static volatile LONG g_em_req;                                   // the slot asked for (pad thread), 0 = none
// v5.192.3 trace (owner 2026-11-21): for 4 s after an emote request,
// log the fighter's motion fields whenever they change. Game facts (Ghidra pass 065e): FUN_140252510 slot >= 130 stores
// +0x171a0 = slot - 130, asks FUN_140259de0 (fighter +0x12f00: +0xa68 load pending, +0xa6c queued, +0xa70 loaded index) and
// sets +0x17198 = 130; FUN_1402526c0 plays it only when +0x12f04 == 0, +0x1396c == -1 and +0x13968 == 0 (the load done),
// frame count = +0x13014 + slot * 0x10.
static ULONG_PTR g_em_watch_a;
static ULONGLONG g_em_watch_until;
static void em_trace() {
    if (!g_em_watch_a || GetTickCount64() > g_em_watch_until) { g_em_watch_a = 0; return; }
    static int last[9];
    int v[9];
    __try {
        ULONG_PTR a = g_em_watch_a;
        v[0] = *(int *)(a + 0x12f04); v[1] = *(int *)(a + 0x13968); v[2] = *(int *)(a + 0x1396c); v[3] = *(int *)(a + 0x13970);
        v[4] = *(int *)(a + 0x17198); v[5] = *(int *)(a + 0x1719c); v[6] = *(int *)(a + 0x12fdc); v[7] = *(int *)(a + 0x13014 + 130 * 0x10);
        v[8] = *(int *)(a + 0x13014 + 135 * 0x10);
    } __except (EXCEPTION_EXECUTE_HANDLER) { g_em_watch_a = 0; return; }
    if (!memcmp(v, last, sizeof(v))) return;
    memcpy(last, v, sizeof(v));
    logf("emote trace: state %d load %d queued %d loaded %d asked %d mode %x motion %d frames130 %d frames135 %d",
         v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8]);
}
// v5.192.4 (in game 2026-11-22, owner session: the trace showed Bow loaded - frames130 72, load 0 - but "asked" stayed 130 and the
// fighter never moved): FUN_1402526c0 (the step that plays a requested motion) is called by FUN_14024ec80 only when the fighter is
// NOT player-driven or the match is not fighting (*0x140533744 != 3), and by the script command FUN_140212380 (intro poses) - so in
// a fight nobody plays it. The loader calls that same step once the load is done (the same tests the game makes first).
typedef void(__fastcall *PFN_EM_PLAY)(ULONG_PTR);
static ULONG_PTR g_em_play_a;
static ULONGLONG g_em_play_until;
// v5.196.2 (in game 2026-11-22: a pose played once was back to the stance in under a second; the Effect Lab's motions hold
// when set again every frame at the frame reached): the pose is held the same way for its own length (+0x13014 + 130 * 0x10)
static ULONG_PTR g_em_hold_a;
static ULONGLONG g_em_hold_t0;
static int g_em_hold_len;
static void em_hold_step() {
    if (!g_em_hold_a) return;
    if (!g_in_match) { g_em_hold_a = 0; return; }
    __try {
        ULONG_PTR a = g_em_hold_a;
        float fr = (float)(GetTickCount64() - g_em_hold_t0) * 30.0f / 1000.0f;
        if (*(int *)(a + 0x12f04) != 0 || fr >= (float)g_em_hold_len) { g_em_hold_a = 0; return; }   // a move / a hit takes over at once
        typedef void(__fastcall *PFN_EM_MOT2)(ULONG_PTR, int, int, float, int, float, float);
        ((PFN_EM_MOT2)(g_base + 0x257310))(a + 0x12f00, 130, 0, 0.0f, 1, fr, (float)g_em_hold_len);
    } __except (EXCEPTION_EXECUTE_HANDLER) { g_em_hold_a = 0; }
}
static void em_play_step() {
    em_hold_step();
    if (!g_em_play_a) return;
    if (GetTickCount64() > g_em_play_until || !g_in_match) { g_em_play_a = 0; return; }
    __try {
        ULONG_PTR a = g_em_play_a;
        if (*(int *)(a + 0x17198) == -1) { g_em_play_a = 0; return; }                 // played (or cleared) already
        if (*(int *)(a + 0x12f04) != 0 || *(int *)(a + 0x1396c) != -1 || *(int *)(a + 0x13968) != 0) return;   // still loading / busy
        g_em_play_a = 0;
        // v5.192.4b (in game 09:28: FUN_1402526c0 set state 28 and the fight step FUN_14024ec80 ended it 28 ms later - for a fighter
        // the player drives it always ends state 0x1c, as at START): play the loaded motion 130 straight through the game's motion
        // player FUN_140257310 in the standing state, as the Create a Skill "Animation from" preview does (cwo_newskills.inl), and
        // clear the request
        typedef void(__fastcall *PFN_EM_MOT)(ULONG_PTR, int, int, float, int, float, float);
        ((PFN_EM_MOT)(g_base + 0x257310))(a + 0x12f00, 130, 0, *(float *)(g_base + 0x2f7f18), 1, -1.0f, -1.0f);
        *(int *)(a + 0x12fdc) = 130; *(ULONGLONG *)(a + 0x12fe0) = 0; *(ULONGLONG *)(a + 0x12fe8) = 0;   // v5.196.0 as FUN_1402526c0 mode 2 does after it
        *(int *)(a + 0x17198) = -1;
        g_em_hold_len = *(int *)(a + 0x13014 + 130 * 0x10);
        if (g_em_hold_len < 1 || g_em_hold_len > 3000) g_em_hold_len = 60;
        g_em_hold_a = a; g_em_hold_t0 = GetTickCount64();
        logf("emote: motion 130 played by the loader in the standing state (state %d)", *(int *)(a + 0x12f04));
    } __except (EXCEPTION_EXECUTE_HANDLER) { g_em_play_a = 0; }
}
static void em_step() {                                          // CwoOverlayFrame, game thread
    em_trace();
    em_play_step();
    LONG slot = InterlockedExchange(&g_em_req, 0);
    if (!slot) return;
    if (!g_in_match || ed_online()) return;                     // v5.199.1 BFR too (owner 2026-11-22: the players' intros only)
    if (slot == 1001 || slot == 1002 || slot == 1) return;      // v5.199.1 our own Wave / Bow / Look around: never (owner)
    __try {
        ULONG_PTR *acts = cwo_actors();
        ULONG_PTR a = 0;
        for (int p = 0; p < 4 && !a; p++)
            if (acts[p] && *(int *)(acts[p] + 0x20) && *(int *)(acts[p] + 0x30)) a = acts[p];   // the first local human
        if (!a) return;
        if (*(int *)(a + 0x12f04) != 0) { set_status("Poses play while you stand still"); return; }
        PFN_IN_REQ req = (PFN_IN_REQ)(g_base + 0x252510);
        if (slot == 1001 || slot == 1002) {                     // v5.188.62 our own Wave / Bow
            const char *nm = slot == 1001 ? EM_WAVE : EM_BOW;
            const char *folder = (const char *)(*(ULONG_PTR *)(a + 0x12f00 + 0xbd0) + 0x12cb8);
            char fd[132]; strncpy_s(fd, folder, _TRUNCATE);
            if (*(int *)(a + 0x12f00 + 0xa68) != 0) { set_status("Emote: a motion is still loading - try again"); return; }
            if (!em_own_file(fd, nm)) {
                set_status("Emote: this fighter has no Wave / Bow yet");
                logf("emote: %s not found for folder '%s' - not asked for", nm, fd);
                return;
            }
            ULONG_PTR *ent = (ULONG_PTR *)(g_base + EM_EXMOT_TABLE_RVA + 5 * sizeof(ULONG_PTR));
            ULONG_PTR keep = *ent, mine = (ULONG_PTR)nm;
            if (!mp_write((ULONG_PTR)ent, &mine, sizeof(mine))) { logf("emote: name table not writable"); return; }
            g_em_watch_a = a; g_em_watch_until = GetTickCount64() + 4000; g_em_play_a = a; g_em_play_until = GetTickCount64() + 3000;
            req(a, 135, 2, -1);
            mp_write((ULONG_PTR)ent, &keep, sizeof(keep));
            char m[64]; sprintf_s(m, "Emote: %s", slot == 1001 ? "Wave" : "Bow"); set_status(m);
            logf("emote: own %s asked for player 1 (folder '%s', EXMOT5 name restored)", nm, fd);
            return;
        }
        const char *nm = slot == 1 ? "Look around" : slot == 130 ? "intro pose" : slot == 131 ? "second intro pose" : slot == 132 ? "victory pose" : slot == 134 ? "third intro pose" : "pose";
        if (slot >= 130) {                                      // v5.192.4 only when this fighter has that EX motion file
            const char *folder = (const char *)(*(ULONG_PTR *)(a + 0x12f00 + 0xbd0) + 0x12cb8);
            char fd[132], fn[16]; strncpy_s(fd, folder, _TRUNCATE); sprintf_s(fn, "exmot%ld.ala", slot - 130);
            // v5.200.1 (owner 2026-11-22): the hero (folder nasi) and
            // every other folder without the game's exmot files gets no pose - only the playable characters (pc..) and enemies (enm..)
            const char *leaf = strrchr(fd, '\\'); leaf = leaf ? leaf + 1 : fd;
            bool kind_ok = !_strnicmp(leaf, "pc", 2) || !_strnicmp(leaf, "enm", 3);
            if (*(int *)(a + 0x12f00 + 0xa68) != 0) { set_status("Pose: a motion is still loading - try again"); return; }
            if (!kind_ok || !em_own_file(fd, fn)) {
                set_status("Poses: playable characters and enemies only");
                logf("pose: %s not there for '%s' (%s) - not asked for", fn, fd, kind_ok ? "file missing" : "not a playable character or enemy");
                return;
            }
        }
        g_em_watch_a = a; g_em_watch_until = GetTickCount64() + 4000; g_em_play_a = a; g_em_play_until = GetTickCount64() + 3000;
        req(a, (int)slot, 2, -1);
        char m[64]; sprintf_s(m, "Pose: %s", nm); set_status(m);
        logf("emote: motion %ld (%s) asked for player 1", slot, nm);
    } __except (EXCEPTION_EXECUTE_HANDLER) { logf("emote: the fighter could not be read - skipped"); }
}
