// ---- v5.86 CPUs revive their downed teammates (owner 2026-11-02).
// Offline team matches only.
// Game facts (TAG_REVIVE_RESEARCH_2026-10-21, Ghidra batches 055-055d; MODLOG 2026-10-25): a player picks a downed partner
// up into a skill slot (FUN_140261be0) and uses the slot (FUN_140261de0) to revive them with FUN_140251f80(fighter): back to
// their spawn, a 3 s guard, life halved; fighter +0x20 active, +0x24 1 standing / 0 down, +0x1c team, +0x30 controller
// (1 = the pad), +0x12e40 position. The CPU programs never do it.
// What this does: a CPU standing within reach of a downed teammate for 1.5 s revives them through the game's own revive.
typedef void(__fastcall *PFN_REVIVE)(ULONG_PTR);
static const ULONG_PTR RVA_CR_REVIVE = 0x251f80;
static const float CR_REACH = 30.0f;                                // PD units (a fighter is about 34 tall)
static ULONGLONG g_cr_near[8], g_cr_done[8];

static void cr_step() {                                            // CwoOverlayFrame, game thread
    if (!g_in_match || ed_online()) { memset(g_cr_near, 0, sizeof(g_cr_near)); return; }
    __try {
        ULONG_PTR *act = cwo_actors();
        int n = cwo_nfighters();
        if (n > 8) n = 8;
        ULONGLONG now = GetTickCount64();
        for (int d = 0; d < n; d++) {
            ULONG_PTR a = act[d];
            if (!a || !*(int *)(a + 0x20) || *(int *)(a + 0x24) != 0) { g_cr_near[d] = 0; continue; }   // standing or gone
            if (now - g_cr_done[d] < 5000) continue;
            const float *pd = (const float *)(a + ACTOR_POS);
            bool close_by = false;
            int who = -1;
            for (int c = 0; c < n && !close_by; c++) {
                ULONG_PTR b = act[c];
                if (c == d || !b || !*(int *)(b + 0x20) || *(int *)(b + 0x24) != 1) continue;
                if (*(int *)(b + 0x1c) != *(int *)(a + 0x1c) || *(int *)(b + 0x30) == 1) continue;   // a CPU of the same team
                const float *pc = (const float *)(b + ACTOR_POS);
                float dx = pc[0] - pd[0], dy = pc[1] - pd[1], dz = pc[2] - pd[2];
                if (dx * dx + dy * dy + dz * dz < CR_REACH * CR_REACH) { close_by = true; who = c; }
            }
            if (!close_by) { g_cr_near[d] = 0; continue; }
            if (!g_cr_near[d]) { g_cr_near[d] = now; continue; }
            if (now - g_cr_near[d] < 1500) continue;
            ((PFN_REVIVE)(g_base + RVA_CR_REVIVE))(a);
            g_cr_near[d] = 0; g_cr_done[d] = now;
            logf("CPU revive: fighter %d revived by its CPU teammate %d", d + 1, who + 1);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
