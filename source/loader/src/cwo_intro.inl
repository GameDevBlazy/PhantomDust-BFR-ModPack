// ---- v5.71 intro poses at the countdown (owner 2026-11-02). Offline matches, and since v5.83.1 online too (AGENTS.md "Intro poses online").
// Game facts (PD Ghidra full pass 065, MODLOG 2026-11-02): FUN_14012ade0 starts the countdown - phase *0x1404bc950 = 5 and the
// clock *0x1404bc8c0 = 0x5a (90 ticks = 3 s) - and plays each fighter's standing motion with FUN_140257310(fighter + 0x12f00,
// *(int *)(0x1403faed0 + 0x2c * *(int *)(fighter + 0x12f00)), 1, 0, 0, *0x1402f82e0, *0x1402f82e0). Animation slots 130-135
// are EXMOT0-5 (the character folder's exmot0-5.ala; slot names table 0x1402db400).
// v5.72 (after the 13:33 crash: slot 130 played directly is not loaded for versus fighters): the quests ask for their intro
// motions through script native 4.434 (FUN_140212140 -> FUN_140252510(fighter, slot, a, -1)), which for a slot >= 130 starts
// loading exmot<slot-130>.ala (FUN_140259de0) and leaves the motion as a request (+0x17198) the fighter plays once loaded.
// The quest scripts (Event/en/quest*.ssb) call 4.434(fighter, 135, 3) for the intro and 4.434(fighter, 0, 0) to stand
// again. FUN_1402526c0 (each fighter step) plays a request when a & 0xffff is 1 (loop) or 2 (once) - 3 = load and wait
// (the quests start it later) - with the fighter standing (+0x12f04 == 0). The loader asks with 2: play once.
// What this does: when the phase becomes 5, every fighter plays the intro slot (CwoIntro.txt: "slot 130" .. "slot 135", or
// "off"); when the phase leaves 5 (the fight starts), the standing motion is played again the game's way.
static const ULONG_PTR RVA_IN_PHASE = 0x4bc950, RVA_IN_ACTORS = 0x4bc9d8, RVA_IN_MOTION_TBL = 0x3faed0, RVA_IN_MINUS1 = 0x2f82e0;
typedef void(__fastcall *PFN_IN_REQ)(ULONG_PTR, int, int, int);    // FUN_140252510(fighter, slot, a, b)
static int g_in_slot = 135, g_in_last_phase = -1;         // v5.72 the quests' intro (EXMOT5) through the game's request path
static bool g_in_posed;
static ULONGLONG g_in_cfg_t;
static bool g_in_cycle;                                           // v5.86 probe: CwoIntro.txt "cycle" (each EXMOT in turn)
// v5.90 the victory pose (owner 2026-11-02; 2026-11-03 "I APPROVE ALL OF THIS"). Game facts (capstone + Ghidra 2026-11-03, MODLOG): the battle-over
// check FUN_14012c510 sets the phase *0x1404bc950 to 7 (fewer than 2 teams in); the game itself plays no pose then (the
// quest scripts' gameend only returns the player to motion 0 through native 4.434); the six extra motions 130-135 are the
// quests' scene gestures (natives 4.434 / 4.455). So at phase 7 every fighter still standing (fighter +0x20 in, +0x24 1)
// plays CwoIntro.txt's "win N" (default 132) and every fighter who lost plays "lose N" (default 133). Which motion is which:
// the owner watched the probe 2026-11-03: "the first one is the intro. The second one is another intro. The third one is a
// victory pose. The fourth one is a losing pose. The fifth one is another intro. The sixth one, it's like a T pose" (130-135;
// 135 is the game's own file, unchanged - a scene motion, left out). Offline (online is not in the intro-poses exception).
static int g_in_win = 132, g_in_lose = 133;
static void in_cfg() {                                            // CwoIntro.txt, once a second
    ULONGLONG now = GetTickCount64();
    if (now < g_in_cfg_t) return;
    g_in_cfg_t = now + 1000;
    wchar_t p[MAX_PATH];
    swprintf_s(p, MAX_PATH, L"%sCwoIntro.txt", g_dir);
    FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"r") != 0 || !f) return;
    char line[64] = "";
    bool cyc = false;
    while (fgets(line, sizeof(line), f)) {                       // v5.90 several lines: "slot N" / "off", "win N" / "win off", "cycle"
        int s = 0;
        if (!strncmp(line, "cycle", 5)) cyc = true;
        else if (!strncmp(line, "win off", 7)) g_in_win = 0;
        else if (sscanf_s(line, "win %d", &s) == 1 && s >= 130 && s <= 135) g_in_win = s;
        else if (!strncmp(line, "lose off", 8)) g_in_lose = 0;
        else if (sscanf_s(line, "lose %d", &s) == 1 && s >= 130 && s <= 135) g_in_lose = s;
        else if (!strncmp(line, "off", 3)) g_in_slot = 0;
        else if (sscanf_s(line, "slot %d", &s) == 1 && s >= 130 && s <= 135) g_in_slot = s;
    }
    fclose(f);
    g_in_cycle = cyc;
}
static void in_play_all(bool intro) {
    PFN_IN_REQ req = (PFN_IN_REQ)(g_base + 0x252510);
    int n = 0;
    // v5.146 (owner 2026-11-10): the fighters from cwo_actors() - with six fighters on the table at
    // 0x1404bc9d8 is moved (cwo_sixf.inl), the old address held no fighters (log 'intro motion for 0 fighters')
    ULONG_PTR *acts = cwo_actors();
    int nf = cwo_nfighters(); if (nf > 8) nf = 8;
    for (int p = 0; p < nf; p++) {
        ULONG_PTR a = acts[p];
        if (!a || *(int *)(a + 0x10) != 0) continue;
        __try { if (intro) req(a, g_in_slot, 2, -1); else req(a, 0, 0, -1); n++; } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    logf("intro poses: %s for %d fighters", intro ? "the intro motion" : "back to standing", n);
}
static void in_step() {                                           // CwoOverlayFrame, game thread
    __try {
        in_cfg();
        int ph = *(int *)(g_base + RVA_IN_PHASE);
        if (ph != g_in_last_phase) logf("intro poses: match phase %d -> %d", g_in_last_phase, ph);   // v5.86 (the victory pose's phase)
        if (g_in_cycle && g_in_match && ph != 5) {               // every 4 s the next of the six extra motions, everybody
            static ULONGLONG t; static int k;
            if (GetTickCount64() - t > 4000) {
                t = GetTickCount64();
                PFN_IN_REQ req = (PFN_IN_REQ)(g_base + 0x252510);
                int slot = 130 + (k++ % 6);
                ULONG_PTR *acts = cwo_actors();
                for (int p = 0; p < cwo_nfighters() && p < 8; p++) {
                    ULONG_PTR a = acts[p];
                    if (a && *(int *)(a + 0x10) == 0) __try { req(a, slot, 2, -1); } __except (EXCEPTION_EXECUTE_HANDLER) {}
                }
                logf("intro poses: cycle - motion %d", slot);
            }
        }
        if (ph != g_in_last_phase) {
            br_phase(g_in_last_phase, ph);                       // v5.114 Boss Slayer reward (cwo_bossraid.inl)
            // v5.83.1 online too (AGENTS.md "Intro poses online", owner 2026-11-02)
            if (ph == 7 && g_in_last_phase == 6 && !ed_online()) {   // v5.90 the victory / losing poses
                // v5.90.2 (owner): fighters who are down do nothing; if the standing fighters are all one
                // team it was a KO win (victory pose), if two or more teams still stand the time ran out (everyone: losing pose)
                PFN_IN_REQ req = (PFN_IN_REQ)(g_base + 0x252510);
                ULONG_PTR *acts = cwo_actors();
                int nf = cwo_nfighters(); if (nf > 8) nf = 8;
                int team0 = -1000; bool draw = false;
                for (int p = 0; p < nf; p++) {
                    ULONG_PTR a = acts[p];
                    if (!a || !*(int *)(a + 0x20) || *(int *)(a + 0x24) != 1) continue;
                    int t = *(int *)(a + 0x1c);
                    if (team0 == -1000) team0 = t; else if (t != team0) draw = true;
                }
                int slot = draw ? g_in_lose : g_in_win, n = 0;
                for (int p = 0; p < nf && slot; p++) {
                    ULONG_PTR a = acts[p];
                    if (!a || !*(int *)(a + 0x20) || *(int *)(a + 0x24) != 1) continue;
                    __try { req(a, slot, 2, -1); n++; } __except (EXCEPTION_EXECUTE_HANDLER) {}
                }
                logf("intro poses: %s - motion %d for %d standing fighters", draw ? "time out (draw): the losing pose" : "KO win: the victory pose", slot, n);
            }
            if (ph == 5 && ed_online() && bn_rule() == 7) {       // v5.95 the lobby rule Sudden Death: everyone at 5 life
                ULONG_PTR *acts = cwo_actors();
                for (int p = 0; p < 4; p++) { ULONG_PTR a = acts[p]; if (a && *(int *)(a + 0x20)) { *(int *)(a + 0x4c) = 5; *(int *)(a + 0x50) = 5; } }
                logf("lobby rule Sudden Death: everyone at 5 life");
            }
            if (ph == 5 && g_in_slot) { in_play_all(true); g_in_posed = true; }
            else if (g_in_last_phase == 5 && g_in_posed) { in_play_all(false); g_in_posed = false; }
            g_in_last_phase = ph;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
