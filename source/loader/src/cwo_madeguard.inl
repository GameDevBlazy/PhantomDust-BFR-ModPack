// ---- v5.186.0 skills you made stay offline - at the match itself (owner 2026-11-14; then "go ahead and work on A, D"; AGENTS.md "Community skills
// and created-content safety online"). The Xbox Live arsenal list already refuses such an arsenal (op 118, v5.152); this
// closes the other ways in: the lobby's Edit Arsenal after the arsenal was taken, Download Battle's own arsenal choice, and
// anything else that reaches a match.
// Game facts (Ghidra 12.1.2 / full pass 065 via pd_fn.py, PDUWP 9ba7411b, 2026-11-14):
//  - FUN_14014f830(_, fighter, short arsenal[30]) fills the fighter's skill COUNTS (*(0x140585fe8 + fighter * 8))[id] from
//    the arsenal; count[0] = 30 - skills = the Aura Particles (skill 0); ids above the table's last (*(*0x1404bdc40 + 0x10))
//    are dropped;
//  - FUN_14014ee70(0x140584f80, fighter, 0) builds the fighter's 30-card deck from those counts (0x140584f80 + fighter * 400,
//    ints), shuffles it (FUN_14014f910) and sets the draw index 0x140585c00 + fighter * 4 back to 0;
//  - FUN_14014f320 (every frame) refills the 3 capsule slots only of a fighter whose driver (+0x61a0) +0x10 == 0 (this
//    console's own); FUN_14014fd10 takes each capsule's skill from deck[draw index] at that moment and keeps it at
//    0x140584f80 + 0xd28 + (fighter * 3 + slot) * 0x48;
//  - online each console owns its fighter and sends its capsules, cards and shots as items (memory pd-online-netcode).
// So, in an Xbox Live / system link session or Download Battle (origin 2 / 3 / 5, or battle type 4 / 5), for this console's own
// fighter (*0x1404bbe00, 0-3): every skill the player made (ns_player_made: a Skill Creator skill that is not one of the pack's
// own, or a Skill Fuser fusion) in its counts, its deck cards not drawn yet and its capsule slots becomes skill 0, the Aura
// Particle, before any capsule carries it to the other consoles. Only this match's copies change; the saved arsenal never does.
// Offline (story, split screen, Single Play) nothing changes.
static bool ns_player_made(int id);                              // cwo_arsenal.inl
static bool ed_online();                                         // cwo_edgar.inl
static int g_mg_total, g_mg_late;
static bool g_mg_was, g_mg_told, g_mg_six_said;
static bool mg_session() {
    __try {
        if (ed_online()) return true;
        int o = *(int *)(g_base + 0x4bca04);
        return o == 2 || o == 3 || o == 5;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static void mg_step() {                                          // pad thread, every tick (~16 ms)
    if (!mg_session()) {
        if (g_mg_was) {
            if (g_mg_total || g_mg_late) logf("made-skill guard: session over - %d card(s) played as Aura Particles (%d after a capsule had them)", g_mg_total, g_mg_late);
            g_mg_was = false; g_mg_total = g_mg_late = 0; g_mg_told = false;
        }
        return;
    }
    g_mg_was = true;
    int me = -1, n = 0, late = 0;
    __try {
        me = *(int *)(g_base + 0x4bbe00);
        if (me < 0 || me > 3) {
            if (!g_mg_six_said && me >= 4 && me < 8) { g_mg_six_said = true; logf("made-skill guard: own fighter %d (six fighters) - not covered", me); }
            return;
        }
        const char *hdr = *(const char **)(g_base + 0x4bdc40);
        int last = hdr ? *(const int *)(hdr + 0x10) : 0;
        if (last > 1199) last = 1199;
        int *cnt = *(int **)cwo_gaddr(0x585fe8 + 8 * (ULONG_PTR)me);
        if (cnt) {
            for (int id = 1; id <= last; id++) {
                if (cnt[id] > 0 && ns_player_made(id)) { cnt[0] += cnt[id]; n += cnt[id]; cnt[id] = 0; }
            }
        }
        if (g_in_match) {
            int *deck = (int *)cwo_gaddr(0x584f80 + 400 * (ULONG_PTR)me);
            int drawn = *(int *)cwo_caps(0xc80 + 4 * (ULONG_PTR)me);
            if (drawn < 0) drawn = 0;
            for (int i = drawn; i < 30; i++) if (deck[i] > 0 && ns_player_made(deck[i])) { deck[i] = 0; n++; }
            for (int s = 0; s < 3; s++) {
                int *k = (int *)cwo_caps(0xd28 + (ULONG_PTR)(me * 3 + s) * 0x48);
                if (*k > 0 && ns_player_made(*k)) { *k = 0; late++; }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return; }
    if (!n && !late) return;
    g_mg_total += n; g_mg_late += late;
    logf("made-skill guard: fighter %d - %d card(s) of skills you made -> Aura Particles%s", me, n + late,
         late ? " (a capsule already held one - changed in the slot)" : "");
    if (!g_mg_told) {
        g_mg_told = true;
        set_status("Skills you made play as Aura Particles online - they are for story, split screen and Single Play");
    }
}
