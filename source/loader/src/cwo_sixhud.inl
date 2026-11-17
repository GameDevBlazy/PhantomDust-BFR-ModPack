// v5.7 fighters 5 and 6 on the game's own HUD (owner 2026-10-25 19:20; screenshot of the Player panel: name, life, hand, deck count, aura - "you should be pulling from that, not
// creating your own"). Independently authored from the game's own code (Ghidra, clean PDUWP.exe 9ba7411b):
// - FUN_140150c30 draws the side panels with FUN_140155310(HUD, slot, battle data i, name i, opponent, x, y, locked
//   target) for battle data 0-3 only (its list is a 4-entry local); each panel adds its height to HUD +0x2a4, so a call
//   right after the game's loop (detour hud_more_hook at 0x1401516ea, r14 = HUD, r12 = layout row) stacks under the
//   last panel; x / y come from the layout tables 0x1402f1250 / 0x1402f1254 (8 bytes a row), as the game's call takes them;
// - the panel keeps state in the HUD object (CBattle2DMenuClass, 0xc50 bytes) in arrays of 4: +0x2ac / +0x2bc + slot * 4
//   (the life bar's drain), +0x2cc + i * 4 (reset, set by FUN_14012a110 / FUN_14012d200), +0x38c + i * 0x1c (status icon
//   cycle, FUN_1401509f0 from FUN_140150640; flash +0x3a4 from FUN_140262fc0); the floating damage figures are
//   +0x138 + i * 0x34 (FUN_140157b80 adds, FUN_140157c20 draws and counts down). Index 4 and 5 of those arrays are other
//   HUD state (+0x208 flags, +0x3fc FUN_1401585f0's data), so the loader keeps fighters 5 / 6's entries itself and lends
//   them to the game's own functions for one call: save the HUD bytes, put the entry there, call, take the entry back,
//   restore the HUD bytes (game thread, inside the HUD pass). The three writes the game makes for index 4+ (detours
//   hud_popup_guard, hud_present_guard / hud_revive_guard, hud_flash_guard) land in those entries.
// Fighters 7 / 8 are not drawn: their lent bytes would cover HUD +0x2a0 / +0x2a4, which these functions read.
struct SixHud { int slot_t, slot_life, present; unsigned char ent[0x1c]; unsigned char pop[0x34]; };
static SixHud g_sh[6];                                         // entries 4 and 5 used
static int g_sh_log, g_sh_flog;

static void sixhud_reset() {                                   // as FUN_140150360 sets entries 0-3 when a battle starts
    for (int k = 0; k < 6; k++) {
        SixHud &h = g_sh[k];
        memset(&h, 0, sizeof(h));
        h.present = 1;
        const int e[7] = {-2, 0x10, -1, -1, -1, -1, 0};
        memcpy(h.ent, e, sizeof(e));
    }
    g_sh_log = g_sh_flog = 0;
}

// the game's writes for battle data 4 and 5 (from the detours; every other index is the game's own as before)
static void sixf_hud_popup(ULONG_PTR, ULONG_PTR idx, ULONG_PTR value, ULONG_PTR extra) {   // FUN_140157b80, mirrored
    int k = (int)idx;
    if (k < 4 || k > 5) return;
    unsigned char *b = g_sh[k].pop;
    int *cnt = (int *)b, *t = (int *)(b + 4), *v = (int *)(b + 0x14), *q = (int *)(b + 0x24);
    int at = *cnt;
    if (at < 4) *cnt = at + 1;
    else {                                                     // full: the game moves entries 1 and 2 down, writes 3
        at = 3;
        for (int j = 0; j < 2; j++) { t[j] = t[j + 1]; v[j] = v[j + 1]; q[j] = q[j + 1]; }
    }
    v[at] = (int)value;
    t[at] = 0x1e;
    q[at] = (int)extra;
}
static void sixf_hud_flash(ULONG_PTR idx) {                    // FUN_140262fc0: HUD + i * 0x1c + 0x3a4 = 0x10
    int k = (int)idx;
    if (k >= 4 && k <= 5) *(int *)(g_sh[k].ent + 0x18) = 0x10;
}
static void sixf_hud_present(ULONG_PTR idx) {                  // FUN_14012a110 / FUN_14012d200: HUD + 0x2cc + i * 4 = 1
    int k = (int)idx;
    if (k >= 4 && k <= 5) g_sh[k].present = 1;
}

// the locked target's battle data number, as FUN_140150c30 works it out for its panels (-1: none)
static unsigned sixhud_locked(ULONG_PTR f) {
    if (!f || *(int *)(f + 0x10) != 0) return 0xFFFFFFFF;
    ULONG_PTR l = *(ULONG_PTR *)(f + 0x14210);
    if (!*(int *)(f + 0x13f1c) || *(int *)(f + 0x13f10) == 1 || !l || !*(int *)(l + 0x20) || !*(int *)(l + 0x24))
        return 0xFFFFFFFF;
    int i = *(int *)(f + 0x13f20);
    if (i < 0 || i >= 32) return 0xFFFFFFFF;
    ULONG_PTR e = ((ULONG_PTR *)cwo_gaddr(0x4bcb50))[i];
    ULONG_PTR ob = e ? *(ULONG_PTR *)(e + 0xb8) : 0;
    return ob ? *(unsigned *)(ob + 4) : 0xFFFFFFFF;
}

struct SixHudSave { int t, life, pres; unsigned char ent[0x1c]; };
static void sixhud_lend(ULONG_PTR hud, int k, SixHudSave &s) {
    int *t = (int *)(hud + 0x2ac + 4 * k), *life = (int *)(hud + 0x2bc + 4 * k), *pres = (int *)(hud + 0x2cc + 4 * k);
    unsigned char *ent = (unsigned char *)(hud + 0x38c + 0x1c * k);
    s.t = *t; s.life = *life; s.pres = *pres; memcpy(s.ent, ent, 0x1c);
    *t = g_sh[k].slot_t; *life = g_sh[k].slot_life; *pres = g_sh[k].present; memcpy(ent, g_sh[k].ent, 0x1c);
}
static void sixhud_return(ULONG_PTR hud, int k, const SixHudSave &s) {
    int *t = (int *)(hud + 0x2ac + 4 * k), *life = (int *)(hud + 0x2bc + 4 * k), *pres = (int *)(hud + 0x2cc + 4 * k);
    unsigned char *ent = (unsigned char *)(hud + 0x38c + 0x1c * k);
    g_sh[k].slot_t = *t; g_sh[k].slot_life = *life; g_sh[k].present = *pres; memcpy(g_sh[k].ent, ent, 0x1c);
    *t = s.t; *life = s.life; *pres = s.pres; memcpy(ent, s.ent, 0x1c);
}

typedef void (*SixHudPanelFn)(ULONG_PTR hud, int slot, unsigned idx, int name, int opponent, int x, int y, unsigned locked);
typedef void (*SixHudStatusFn)(ULONG_PTR hud, int idx);
typedef void (*SixHudDamageFn)(ULONG_PTR hud, int pos, unsigned idx, unsigned name);

// v5.181 (2026-11-14, seen in a Single Play 3 vs 3: Chunky and Cuff Button had no face): fighters 5-6's panels go through the
// HUD faces hook too (cwo_hudface.inl hf_panel: the face, then the game's panel), with face cells 4-5
static bool hf_ready();
static void __fastcall hf_panel(INT64 hud, int idx, unsigned player, int p4, int flag, int x, int y, unsigned p8);
static void sixhud_one(ULONG_PTR hud, int k, int opponent, int x, int y, unsigned locked) {
    SixHudSave s;
    unsigned char popsave[0x34];
    unsigned char *pop = (unsigned char *)(hud + 0x138 + 0x34 * k);
    int lent = 0;
    __try {
        sixhud_lend(hud, k, s);
        lent = 1;
        ULONG_PTR b = cwo_bdata()[k];
        if (b && *(int *)b) ((SixHudStatusFn)(g_base + 0x1509f0))(hud, k);         // as FUN_140150640 does for 0-3
        if (hf_ready()) hf_panel((INT64)hud, k, (unsigned)k, k, opponent, x, y, locked);   // v5.181 with its face
        else ((SixHudPanelFn)(g_base + 0x155310))(hud, k, (unsigned)k, k, opponent, x, y, locked);
        sixhud_return(hud, k, s);
        lent = 0;
        memcpy(popsave, pop, 0x34);
        memcpy(pop, g_sh[k].pop, 0x34);
        lent = 2;
        ((SixHudDamageFn)(g_base + 0x157c20))(hud, k, (unsigned)k, (unsigned)k);
        memcpy(g_sh[k].pop, pop, 0x34);
        memcpy(pop, popsave, 0x34);
        lent = 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (lent == 1) sixhud_return(hud, k, s);
        if (lent == 2) memcpy(pop, popsave, 0x34);
        if (g_sh_flog < 3) { g_sh_flog++; logf("six fighters HUD: fighter %d's panel faulted - HUD bytes put back", k + 1); }
    }
}

// ---- v5.8 the Skill Creator's skill card = the game's own (owner). Ghidra: View Stock's
// skill window (menu window type 9, FUN_140249df0) draws FUN_140168410(-, skill, x, y, 0, -, 0, stock count, 1); the
// battle HUD draws the same card (FUN_140158930 with option 0, FUN_140155160 with HUD +0x208 hiding parts), so its
// pieces are loaded in a match. Option 5 = parts to hide (0: all shown); argument 9 = the backing panel, drawn by
// FUN_14016cf30 from the card object *0x1404c0140 (+0x470 its sprite set; FUN_140168410 reads +0x470 -> +0x58 and uses
// the panel only when (value & 0xff00) < 0xff00, the same test here). Drawn in the HUD pass, right before the game's own
// card call (FUN_140158930) in FUN_140150c30; the text style globals 0x14054c990..cf it changes are put back.
typedef void (*CwoCardFn)(ULONG_PTR, int skill, int x, int y, unsigned hide, ULONG_PTR, int fade, int count, int panel);
static int g_card_log;
static void cwo_card_draw() {
    ULONGLONG now = GetTickCount64();
    if (now - g_card_want > 250 || !g_in_match) return;
    int id = g_card_id;
    unsigned char *style = (unsigned char *)cwo_gaddr(0x54c990);
    unsigned char keep[0x40];
    bool kept = false;
    __try {
        char *gs = *(char **)(g_base + RVA_GSDATA);
        if (!gs || *(unsigned *)gs != 0x44004 || id <= 0 || id >= 753) return;
        if (*(char **)cwo_gaddr(0x4bdc50) != gs + 0x18) return;       // the skill table the card reads is this one
        ULONG_PTR obj = *(ULONG_PTR *)cwo_gaddr(0x4c0140), set = obj ? *(ULONG_PTR *)(obj + 0x470) : 0;
        if (!set) return;
        int panel = (*(unsigned *)(set + 0x58) & 0xff00) < 0xff00 ? 1 : 0;
        memcpy(keep, style, sizeof(keep));
        kept = true;
        ((CwoCardFn)(g_base + 0x168410))(0, id, g_card_x, g_card_y, 0, 0, 0, 0, panel);
        memcpy(style, keep, sizeof(keep));
        kept = false;
        g_card_drawn = now;
        if (g_card_log < 1) { g_card_log++; logf("skill card: the game's own card for skill %d at %d,%d (panel %d)", id, (int)g_card_x, (int)g_card_y, panel); }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (kept) memcpy(style, keep, sizeof(keep));
        g_card_want = 0;
        if (g_card_log < 3) { g_card_log = 3; logf("skill card: the game's card faulted for skill %d - the text card stays", id); }
    }
}

// called by the detour after the game's own side panels, every frame of the HUD pass (r14 = HUD, r12 = layout row)
static void sixf_hud_more(ULONG_PTR hud, ULONG_PTR layout) {
    cwo_card_draw();                                           // v5.8 the Skill Creator's card (any match)
    if (!g_six_match || !g_in_match || !hud || layout > 2) return;
    __try {
        if (*(int *)(hud + 0x2a0) != 0) return;               // split screen: the game's 2-screen layout is not handled
        ULONG_PTR *bdt = cwo_bdata(), *act = cwo_actors();
        int own = *(int *)cwo_gaddr(0x4bbe00);
        if (own < 0 || own >= SIXF_MAX || !act[own]) return;
        int x = *(int *)(g_base + 0x2f1250 + layout * 8), y = *(int *)(g_base + 0x2f1254 + layout * 8);
        bool teams = *(int *)cwo_gaddr(0x4bc8b8) == 1;         // FUN_140150c30 sorts partners first only then
        int myteam = *(int *)(act[own] + 0x1c);
        bool crossed = false;                                  // has the game's list already passed to the opponents?
        for (int i = 0; i < 4; i++)
            if (i != own && bdt[i] && *(int *)bdt[i] && act[i] && *(int *)(act[i] + 0x1c) != myteam) crossed = true;
        unsigned locked = sixhud_locked(act[own]);
        for (int k = 4; k < 6; k++) {
            ULONG_PTR b = bdt[k];
            if (!b || !*(int *)b || k == own) continue;        // the game skips an empty entry the same way
            int opponent = 0;
            if (teams && act[k] && *(int *)(act[k] + 0x1c) != myteam) {
                opponent = 1;
                if (!crossed) { *(int *)(hud + 0x2a4) += 0x14; crossed = true; }   // the game's gap before opponents
            }
            sixhud_one(hud, k, opponent, x, y, locked);
        }
        g_sh_drawn = GetTickCount64();
        if (!g_sh_log) { g_sh_log = 1; logf("six fighters HUD: the game's own panels drawn for fighters 5 and 6 (x %d, y %d)", x, y); }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static int sixhud_self_test() {                                // the mirror of FUN_140157b80 and the lending, on a stand-in HUD
    sixhud_reset();
    for (int n = 1; n <= 6; n++) sixf_hud_popup(0, 4, 100 + n, 200 + n);       // six figures into a list of four
    const int *b = (const int *)g_sh[4].pop;
    // 1..4 fill entries 0..3 (101..104); then the game's two moves (0 <- 1, 1 <- 2) and entry 3: 102 103 103 105, then
    // 103 103 103 106 (entry 2 keeps its value, as FUN_140157b80's loop of two does)
    bool pop_ok = b[0] == 4 && b[5] == 103 && b[6] == 103 && b[7] == 103 && b[8] == 106 && b[4] == 0x1e && b[12] == 206 &&
                  b[9] == 203;
    sixf_hud_popup(0, 3, 1, 1); sixf_hud_popup(0, 6, 1, 1);    // other indexes: not the loader's
    pop_ok = pop_ok && *(const int *)g_sh[5].pop == 0;
    sixf_hud_flash(5); sixf_hud_present(4);
    bool flash_ok = *(const int *)(g_sh[5].ent + 0x18) == 0x10 && g_sh[4].present == 1 && *(const int *)g_sh[4].ent == -2;
    static unsigned char hud[0xc50];
    for (int i = 0; i < (int)sizeof(hud); i++) hud[i] = (unsigned char)(i * 7 + 3);
    unsigned char keep[sizeof(hud)];
    memcpy(keep, hud, sizeof(hud));
    SixHudSave sv;
    sixhud_lend((ULONG_PTR)hud, 5, sv);
    bool in_ok = *(int *)(hud + 0x2cc + 20) == g_sh[5].present && !memcmp(hud + 0x38c + 0x1c * 5, g_sh[5].ent, 0x1c);
    *(int *)(hud + 0x2bc + 20) = 77;                           // the game's call changes the drain value
    sixhud_return((ULONG_PTR)hud, 5, sv);
    bool lend_ok = in_ok && g_sh[5].slot_life == 77 && !memcmp(hud, keep, sizeof(hud));
    sixhud_reset();
    logf("self-test: six fighters HUD damage figures %s, flash / reset %s, lend + give back %s", pop_ok ? "ok" : "FAILED",
         flash_ok ? "ok" : "FAILED", lend_ok ? "ok" : "FAILED");
    return pop_ok && flash_ok && lend_ok ? 1 : -72;
}
