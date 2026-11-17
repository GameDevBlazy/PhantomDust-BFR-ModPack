// ---- v5.99 the NPC page in the match (owner 2026-11-03; "The created character should be labeled as experimental and only be selectable for the scoreboard, campaign, and
// split screen ... not multiplayer live lobby").
// The select screens show one cell (charaset row 28, "NPC") with ten colours: 1-9 the NPCs (npc00a0..8.alr, for the preview),
// 10 the created character (npc00a9.alr, the character in use when there is one). Each NPC's own charaset row (Spokesman 30,
// Ubiquitous 31, Kajikawa 32, Tetsuya 33, Mac 36, Baroness 37, Mikan 38, Maniac 40, Kei 41) carries its height and its base
// character's evade, voice and player values (tools/build_npc_page.py). Game facts (MODLOG; cwo_lobby.inl): a fighter's model is
// "<stem><outfit>.alr" from its setup record's word +8 (low 16 bits the row, high 16 the outfit), set before FUN_140128cc0
// creates the fighters - so at that call (sixf_before_create_main) row 28 colour k (k < 9) becomes row NPC_ROWS[k] colour 0.
// Native 2.150(character, 43) = the colour count the select screens list (FUN_14024c410, thunk 0x140208330: ecx = args[0],
// edx = args[1]); in an Xbox Live session the NPC cell lists 9 (the created character is offline / scoreboard only).
static const int NPC_ROWS[9] = {30, 31, 32, 33, 36, 37, 38, 40, 41};
// v5.182 (owner 2026-11-14):
// the NPC cell is EXTRAS; colours 10+ are the new characters, each on its own charaset row (47 Sammira; tools/build_extras.py)
static const int EXTRA_ROWS[] = {47, 48, 49};  // 47 Sammira, 48 Manique (v5.183), 49 JR (v5.184)
static const int N_EXTRA = sizeof(EXTRA_ROWS) / sizeof(EXTRA_ROWS[0]);
static void npc_swap_records() {                                   // game thread, FUN_140128cc0's entry
    if (!g_npc_page) return;
    for (int i = 0; i < 4; i++) {
        unsigned *w = (unsigned *)(cwo_gaddr(0x4bc420) + 0x48 * (ULONG_PTR)i + 8);
        int row = (int)(*w & 0xFFFF), k = (int)(*w >> 16);
        if (row != 28 || k < 0 || k == 9 || k >= 10 + N_EXTRA) continue;
        if (k >= 10) {                                             // v5.182 an EXTRAS character: its own row, outfit 0
            // v5.184.3 (the Store-mode boot race seen 2026-11-14 on the stage catalog): the game's charaset count is
            // *(exe+0x4c0174) (parse FUN_14017d300); when this session read a catalog without the new rows, a row past it
            // would be an empty record - keep the first NPC row instead of a crash
            int have = *(int *)cwo_gaddr(0x4c0174);
            if (EXTRA_ROWS[k - 10] >= have) {
                *w = (unsigned)NPC_ROWS[0];
                logf("EXTRAS: fighter %d = extra %d, but this session's character catalog has %d rows (row %d missing) - NPC 1 instead",
                     i + 1, k - 9, have, EXTRA_ROWS[k - 10]);
                continue;
            }
            *w = (unsigned)EXTRA_ROWS[k - 10];
            logf("EXTRAS: fighter %d = extra %d -> its own row %d", i + 1, k - 9, EXTRA_ROWS[k - 10]);
            continue;
        }
        *w = (unsigned)NPC_ROWS[k];
        logf("NPC page: fighter %d = NPC %d -> its own row %d", i + 1, k + 1, NPC_ROWS[k]);
    }
}

typedef INT64 (__fastcall *PFN_NPC_NAT)(INT64 *args);
static PFN_NPC_NAT g_npc_cc_orig;
static INT64 __fastcall npc_cc_native(INT64 *args) {               // native 2.150
    INT64 r = g_npc_cc_orig(args);
    __try {
        if (g_npc_page && (int)args[1] == 43 && ((int)args[0] & 0xFFFF) == 28 && r > 9 && ed_online()) r = 9;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return r;
}
static void npc_page_install() {
    ULONG_PTR *cell = (ULONG_PTR *)(g_base + 0x3eede0 + 150 * 8);
    if (*cell != g_base + 0x208330) { logf("NPC page: native 2.150 is not the game's - the created character row stays online"); return; }
    g_npc_cc_orig = (PFN_NPC_NAT)*cell;
    DWORD old;
    if (!VirtualProtect(cell, 8, PAGE_READWRITE, &old)) return;
    *cell = (ULONG_PTR)&npc_cc_native;
    VirtualProtect(cell, 8, old, &old);
}

static bool npc_is_created_file(LPCWSTR f) {                      // ...\npc00a9.alr: the NPC page's created-character colour
    size_t n = f ? wcslen(f) : 0;
    static const wchar_t tail[] = L"npc00a9.alr";
    const size_t t = sizeof(tail) / sizeof(wchar_t) - 1;
    return n > t && _wcsicmp(f + n - t, tail) == 0 && (f[n - t - 1] == L'\\' || f[n - t - 1] == L'/');
}
