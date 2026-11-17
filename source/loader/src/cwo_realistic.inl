// ---- v5.81 REALISTIC MODE (owner 2026-11-02, item 12: campaign-style collision - "shots hit whoever is in the way,
// teammates too", even when locked on to someone else; shields then block shots aimed at a teammate behind you). A Create a
// Mode rule ("Realistic collision") or CwoRealistic.txt "on"; offline matches only.
// Game facts (PD Ghidra full pass 065 via pd_fn.py, MODLOG 2026-11-02; hit mask MODLOG 2026-10-25):
//   FUN_140263a00 starts a projectile: its kind's row {start, update, extra} = 0x1403fb240 + kind * 0x18 (or 0x1403fb190
//   + kind * 0x18 for the second family) is kept at projectile + 0x1e0, and the game calls row[1] (the update) each step.
//   In an update (FUN_140267410), a projectile WITHOUT a target (+0x30 == -1) walks the 32 battle objects *0x1404bcb50[i]
//   and runs the hit test FUN_14026c1c0(projectile, object) for each object whose battle data (object + 0xb8) still has its
//   bit in the projectile's mask (+0x24, bit = battle data + 4), life (+0xc) > 0 and *(bd + 0x61a0) + 0x34 != 0; a
//   projectile WITH a target only tests that target - the versus "passes through" everyone else. The hit test and the hit
//   result (FUN_14026f870) have no team check; a hit clears the fighter's bit, so nobody is hit twice.
// What this does: every row's update is wrapped (a small stub per row: the game's update, then ours); in a Realistic match a
// targeted projectile also runs the game's own hit test on every other fighter it still may hit - the first one in its way
// takes it (a teammate too, and a guarding fighter's shield answers it as it would its own shots).
// the two row tables (their own alignment: 0x1403fb190 + k * 0x18 for 6 rows, 0x1403fb240 + k * 0x18 for 21 rows; read from
// the clean exe's .data)
static const ULONG_PTR RVA_PJ_TAB[2][2] = {{0x3fb190, 0x3fb220}, {0x3fb240, 0x3fb438}}, RVA_PJ_OBJS = 0x4bcb50, RVA_PJ_HIT = 0x26c1c0;
typedef void(__fastcall *PFN_PJ)(INT64);
typedef void(__fastcall *PFN_PJ_HIT)(INT64, INT64);
static int g_rm_state;                                           // 0 not tried, 1 wrapped, -1 off
static volatile LONG g_rm_on;                                    // this match is Realistic (game thread)
static unsigned g_rm_hits;

static void __fastcall rm_extra(INT64 pj) {
    __try {
        if (*(int *)(pj + 0x10) == 4 || *(int *)(pj + 0x30) == -1) return;   // gone, or untargeted (the game tests everyone)
        ULONG_PTR *objs = (ULONG_PTR *)(g_base + RVA_PJ_OBJS);
        for (int i = 0; i < 32; i++) {
            ULONG_PTR o = objs[i];
            if (!o) continue;
            ULONG_PTR bd = *(ULONG_PTR *)(o + 0xb8);
            if (!bd) continue;                                    // fighters only
            unsigned bit = *(unsigned *)(bd + 4) & 31;
            if (!((*(unsigned *)(pj + 0x24) >> bit) & 1) || *(int *)(bd + 0xc) <= 0) continue;
            ULONG_PTR f = *(ULONG_PTR *)(bd + 0x61a0);
            if (!f || *(int *)(f + 0x34) == 0) continue;
            unsigned before = *(unsigned *)(pj + 0x24);
            ((PFN_PJ_HIT)(g_base + RVA_PJ_HIT))(pj, (INT64)o);
            if (*(unsigned *)(pj + 0x24) != before && g_rm_hits++ < 20) logf("realistic: a shot hit battle data %u on its way", bit);
            if (*(int *)(pj + 0x10) == 4) return;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
static void __fastcall rm_wrap(INT64 pj, INT64 orig) {           // every projectile update goes through here
    ((PFN_PJ)orig)(pj);
    if (g_rm_on) rm_extra(pj);
}
static void rm_install() {                                       // worker start
    if (g_rm_state) return;
    g_rm_state = -1;
    ULONG_PTR stubs = sixf2_near_alloc(0x1000, PAGE_EXECUTE_READWRITE);
    if (!stubs) { logf("realistic: no stub page - off"); return; }
    unsigned char *c = (unsigned char *)stubs;
    int n = 0;
    for (int t = 0; t < 2; t++)
    for (ULONG_PTR r = RVA_PJ_TAB[t][0]; r < RVA_PJ_TAB[t][1]; r += 0x18) {
        ULONG_PTR *slot = (ULONG_PTR *)(g_base + r + 8), orig = *slot;
        if (orig < g_base + 0x1000 || orig >= g_base + 0x2c0000 || orig == g_base + 0x4660) continue;   // code only, not the empty one
        if ((c + 22) - (unsigned char *)stubs > 0x1000) break;
        unsigned char *s = c;
        s[0] = 0x48; s[1] = 0xBA; *(ULONGLONG *)(s + 2) = orig;                                        // mov rdx, the game's update
        s[10] = 0x48; s[11] = 0xB8; *(ULONGLONG *)(s + 12) = (ULONGLONG)(ULONG_PTR)rm_wrap;           // mov rax, rm_wrap
        s[20] = 0xFF; s[21] = 0xE0;                                                                    // jmp rax
        c += 24;
        DWORD old;
        if (!VirtualProtect(slot, 8, PAGE_READWRITE, &old)) continue;
        *slot = (ULONG_PTR)s;
        VirtualProtect(slot, 8, old, &old);
        n++;
    }
    FlushInstructionCache(GetCurrentProcess(), (void *)stubs, 0x1000);
    g_rm_state = n > 0 ? 1 : -1;
    logf("realistic: %d projectile updates wrapped", n);
}
static void rm_step() {                                           // CwoOverlayFrame
    static ULONGLONG cfg_t;
    static bool file_on;
    ULONGLONG now = GetTickCount64();
    if (now >= cfg_t) {
        cfg_t = now + 3000;
        wchar_t p[MAX_PATH]; swprintf_s(p, MAX_PATH, L"%sCwoRealistic.txt", g_dir);
        FILE *f = NULL; char line[16] = "";
        file_on = false;
        if (cwo_wfopen(&f, p, L"r") == 0 && f) { if (fgets(line, sizeof(line), f)) file_on = !_strnicmp(line, "on", 2); fclose(f); }
    }
    LONG k = g_ed_mode_on;                                        // the Create a Mode in use: its "Realistic collision" row
    bool rule = k >= 0 && k < g_edc[EDP_MODE].n && g_edc[EDP_MODE].item[k].v[18] != 0;
    // v5.95 the lobby rule "Realistic Collision" (online too: AGENTS.md "New game rules ... online")
    LONG on = g_rm_state == 1 && g_in_match && (((file_on || rule) && !ed_online()) || (ed_online() && bn_rule() == 8));
    if (on != g_rm_on) { InterlockedExchange(&g_rm_on, on); g_rm_hits = 0; logf("realistic: %s", on ? "on for this match" : "off"); }
    // v5.92 Realistic Mode extras (owner's list 2026-11-03: "Realistic Mode's capsule formation particles and the last-life body
    // fading away 10 s after a KO"). Game facts (Ghidra batch_104 / 105 / 106):
    //  - the capsule table 0x140584f80: slot i = fighter * 3 + k at +0xd10 + i * 0x48 (position +0xd10, state +0xd1c: 0 forming,
    //    2 on the field, 3 none; the model +0xd48); FUN_14021b4f0(model, 4) is the capsule's own burst (its pickup sparkle)
    //    -> played when a slot turns from forming to on the field;
    //  - a downed fighter (+0x12F04 == 0x1B) is taken out of the battle by FUN_14025c670 when its counter +0x12F08 reaches 0
    //    (FUN_140129e70) -> with no teammate standing to revive it, the counter is set to 10 s (300 ticks, 30 a second) once.
    static int cap_prev[12]; static unsigned char down_set[8];
    if (!g_rm_on || g_sixf2_state == 1) { memset(cap_prev, 0, sizeof(cap_prev)); memset(down_set, 0, sizeof(down_set)); return; }
    __try {
        typedef void (__fastcall *PFN_CAP_STATE)(ULONG_PTR, int);
        unsigned char *caps = (unsigned char *)(g_base + 0x584f80);
        for (int i = 0; i < 12; i++) {
            int st = *(int *)(caps + 0xd1c + i * 0x48);
            if (cap_prev[i] == 0 && st == 2) {
                ULONG_PTR model = *(ULONG_PTR *)(caps + 0xd48 + i * 0x48);
                if (model) ((PFN_CAP_STATE)(g_base + 0x21b4f0))(model, 4);
            }
            cap_prev[i] = st;
        }
        ULONG_PTR *acts = cwo_actors();
        for (int p = 0; p < 4; p++) {
            ULONG_PTR a = acts[p];
            if (!a || !*(int *)(a + 0x20) || *(int *)(a + 0x12F04) != 0x1B) { down_set[p] = 0; continue; }
            if (down_set[p]) continue;
            bool mate = false;
            for (int q = 0; q < 4; q++) {
                ULONG_PTR b = acts[q];
                if (q != p && b && *(int *)(b + 0x20) && *(int *)(b + 0x24) == 1 && *(int *)(b + 0x1c) == *(int *)(a + 0x1c)) mate = true;
            }
            if (mate) continue;
            *(int *)(a + 0x12F08) = 300;
            down_set[p] = 1;
            logf("realistic: fighter %d down with nobody to revive it - it leaves the field in 10 s", p + 1);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
