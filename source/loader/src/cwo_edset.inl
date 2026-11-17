// ---- v5.165.0 Create a Skill / TEST: the arsenal list shows your own arsenals (owner 2026-11-13 quality task: menus; seen
// in game: the list showed "[SHORT(0)]", "(24)", "(4)" and no names). Game facts (MODLOG 2026-11-13: Ghidra full pass 065,
// capstone on the clean exe, memory read): the profile *0x1404bec10 keeps two arsenal sets, +0 and +0x3fb8 (the 16 arsenals of
// the Lab / multiplayer); +0x33a0 = the active set, read by every arsenal list (+0x3a30 = active + 4), the game's debug deck
// list FUN_1401759a0 too. From the main menu set +0 is active (memory: 'SHORT', unused 0xFF records). FUN_14015bd50 = the
// game's own switch to +0x3fb8 (keeps the old set's cursor +0x19cc, FUN_14015bcd0 points the lists and recounts every arsenal
// with FUN_14015a730); FUN_14015bd90 = back to +0 (FUN_1401cc550 uses the pair the same way). Game thread (CwoOverlayFrame).
static const ULONG_PTR RVA_ES_PROFILE = 0x4bec10, RVA_ES_TO_B = 0x15bd50, RVA_ES_TO_A = 0x15bd90;
static const unsigned char ES_TO_B[] = {0x48, 0x8b, 0x0d, 0xb9, 0x2e, 0x36, 0x00, 0x8b, 0x05, 0x7b, 0xec, 0x43, 0x00, 0x48, 0x8b, 0xd1, 0x4c, 0x8d,
                                        0x81, 0xb8, 0x3f, 0x00, 0x00, 0x4c, 0x39, 0x81, 0xa0, 0x33, 0x00, 0x00, 0x49, 0x0f, 0x44, 0xd0, 0x89, 0x82,
                                        0xcc, 0x19, 0x00, 0x00, 0x4c, 0x89, 0x81, 0xa0, 0x33, 0x00, 0x00, 0xe9, 0x4c, 0xff, 0xff, 0xff};
static const unsigned char ES_TO_A[] = {0x48, 0x8b, 0x0d, 0x79, 0x2e, 0x36, 0x00, 0x48, 0x8b, 0xd1, 0x48, 0x8d, 0x81, 0xb8, 0x3f, 0x00, 0x00, 0x48,
                                        0x39, 0x81, 0xa0, 0x33, 0x00, 0x00, 0x48, 0x0f, 0x44, 0xd0, 0x8b, 0x05, 0x26, 0xec, 0x43, 0x00, 0x89, 0x82,
                                        0xcc, 0x19, 0x00, 0x00, 0x48, 0x89, 0x89, 0xa0, 0x33, 0x00, 0x00, 0xe9, 0x0c, 0xff, 0xff, 0xff};
static int g_es_state;                                          // 0 not checked, 1 ok, -1 refused
static bool g_es_switched;

static void ed_set_step() {
    if (g_es_state == 0) {
        bool ok = !memcmp((const void *)(g_base + RVA_ES_TO_B), ES_TO_B, sizeof(ES_TO_B)) && !memcmp((const void *)(g_base + RVA_ES_TO_A), ES_TO_A, sizeof(ES_TO_A));
        g_es_state = ok ? 1 : -1;
        if (!ok) logf("Create a Skill arsenals: the game's set switches are not the expected code - left alone");
    }
    if (g_es_state != 1) return;
    __try {
        ULONG_PTR mgr = *(ULONG_PTR *)(g_base + RVA_ES_PROFILE);
        if (!mgr) return;
        ULONG_PTR active = *(ULONG_PTR *)(mgr + 0x33a0);
        bool want = g_forge_session && (g_ed_mode == 1 || g_ed_mode == 2) && !ed_online();
        if (want && active == mgr) {
            ((void (*)())(g_base + RVA_ES_TO_B))();
            g_es_switched = true;
            logf("Create a Skill arsenals: your own arsenal set (profile +0x3fb8) for the test list (the game's FUN_14015bd50)");
        } else if (!want && g_es_switched && !g_in_match && !g_forge_session) {
            if (active == mgr + 0x3fb8) ((void (*)())(g_base + RVA_ES_TO_A))();
            g_es_switched = false;
            logf("Create a Skill arsenals: session over - the main menu's set is active again (the game's FUN_14015bd90)");
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
