// ---- v5.19 outfit names in the COLORS list (owner 2026-10-27: idea 1 of the 8, "outfit names in the COLORS menu";
// "I like every single one of those ideas ... go ahead and work on adding all 8"). The select screen's COLORS window
// (vsmenu 0xe54: one row "COLOR \x071f%d" per outfit, 2.150(character, 43) of them; its row draw _draw_plcolo passes the
// row + 1 for the %d) asks the loader for each row's text instead (tools/build_colors_names.py, native 2.8 op 44 with the
// character - the script word the builder hands 2.150 - and the row). The names come from CwoOutfitNames.txt in the
// loader's data folder ("<stem> <outfit from 0> <name>", written by 13_Independent_Research_Tools/write_outfit_names.py
// from what was installed); an outfit without a name keeps the game's own "COLOR n".
// The charaset image the game keeps (*0x140590190: +4 rows, +8 pool, rows of (pool - 16) / rows bytes from +16, +0 the
// stem's pool offset) gives the character's stem.
struct OutfitName { char stem[12]; int k; char name[25]; };
static OutfitName *g_on;
static int g_on_n = -1;
static char g_on_row[32][48];                                  // the rows' texts (the window keeps the pointers)
static const char ON_GAME[] = "COLOR \x07" "1f%d";             // the game's own row text
static char *g_on_t_chara;                                     // self-test: a stand-in charaset image

static void on_load() {
    if (g_on_n >= 0) return;
    g_on_n = 0;
    wchar_t p[MAX_PATH];
    swprintf_s(p, MAX_PATH, L"%sCwoOutfitNames.txt", g_dir);
    FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"r") != 0 || !f) { logf("outfit names: no CwoOutfitNames.txt - the COLORS list keeps 'COLOR n'"); return; }
    static OutfitName tab[1024];
    char line[128];
    while (fgets(line, sizeof(line), f) && g_on_n < 1024) {
        if (line[0] == '#') continue;
        OutfitName e = {};
        int used = 0;
        if (sscanf_s(line, "%11s %d %n", e.stem, (unsigned)sizeof(e.stem), &e.k, &used) < 2 || used <= 0) continue;
        char *nm = line + used;
        size_t n = strcspn(nm, "\r\n");
        nm[n] = 0;
        for (char *c = nm; *c; c++) if ((unsigned char)*c < 0x20 || *c == '%') *c = ' ';   // no layout codes, no formats
        strncpy_s(e.name, nm, _TRUNCATE);
        if (e.name[0] && e.k >= 0 && e.k < 32) tab[g_on_n++] = e;
    }
    fclose(f);
    g_on = tab;
    logf("outfit names: %d names from CwoOutfitNames.txt", g_on_n);
}

static const char *on_stem(int row) {                          // the charaset row's stem, or ""
    static char s[12];
    s[0] = 0;
    // v5.30.2 (the Xbox Live COLORS list kept "COLOR n": there *0x140590190 is not the charaset image): the game's own
    // table of loaded charaset rows, 0xa0 bytes a row from 0x1404c01e0, +0 = the stem (FUN_140250f70 builds the model
    // name with sprintf("%s%d.alr", table[row].stem, ...)); filled in every mode
    if (!g_on_t_chara && row >= 0 && row < 64) {
        __try {
            const char *t = *(const char **)(g_base + 0x4c01e0 + 0xa0 * (ULONG_PTR)row);
            if (t && isalnum((unsigned char)t[0])) { strncpy_s(s, t, _TRUNCATE); return s; }
        } __except (EXCEPTION_EXECUTE_HANDLER) { s[0] = 0; }
    }
    __try {
        char *b = g_on_t_chara ? g_on_t_chara : lob_chara();
        if (!b) return s;
        int rows = *(int *)(b + 4), pool = *(int *)(b + 8), rs = rows > 0 ? (pool - 16) / rows : 0;
        if (row < 0 || row >= rows || rs < 0x3C || rs > 0x100) return s;
        strncpy_s(s, b + pool + *(int *)(b + 16 + rs * row), _TRUNCATE);
    } __except (EXCEPTION_EXECUTE_HANDLER) { s[0] = 0; }
    return s;
}

static int g_col_char = -1;                                    // v5.77 (cwo_stagelist.inl op 79)
static bool g_npc_page;                                          // v5.96 Menu\cwo_npc_page.txt present: row 28 = the NPC page
static INT64 on_native(INT64 *args) {                          // op 44 (game thread): args[1] = the row, args[2] = the character
    on_load();
    int k = (int)args[1], ch = (int)(args[2] & 0xFFFF);
    g_col_char = ch;                                           // v5.77 the character whose COLORS list is up
    if (k < 0 || k >= 32) return (INT64)(ULONG_PTR)ON_GAME;
    // v5.96 the NPC page (owner 2026-11-03; "I APPROVE ALL OF THIS"): row 28's ten colours are the NPCs
    // (packs/npc_page_2026-11-03: npc00a0..9.alr = these bodies with Nanashi's / Freia's moves)
    static const char *NPC_NAMES[13] = {   // v5.186.4 "CREATED" (was "CREATED EXP", cut at the window edge - owner's neat-screen rule)
                                         "SPOKESMAN", "UBIQUITOUS", "KAJIKAWA", "TETSUYA", "MAC", "BARONESS", "MIKAN", "MANIAC", "KEI", "CREATED", "SAMMIRA", "MANIQUE", "JR"};   // v5.182 + the EXTRAS characters   // v5.99 10 = the created character (experimental)
    if (ch == 28 && k < 13 && g_npc_page) return (INT64)(ULONG_PTR)NPC_NAMES[k];
    const char *stem = on_stem(ch);
    for (int i = 0; stem[0] && i < g_on_n; i++)
        if (g_on[i].k == k && !_stricmp(g_on[i].stem, stem)) {
            // v5.31 (owner 2026-10-27; their screenshot: "3 Agent Sm" cut at the window's edge): the game's own pattern "COLOR 7"
            // - the word in capitals, a space, the number in the game's number font - with the name in the word's place
            // ("FIRE 7"), only when the whole row is at most 12 characters (the 170-wide COLORS window holds about 13 of
            // these letters, measured on the owner's screenshot); a longer name keeps the game's "COLOR n"
            int digits = k + 1 >= 10 ? 2 : 1;
            if ((int)strlen(g_on[i].name) + 1 + digits > 12) break;
            char up[32];
            strncpy_s(up, g_on[i].name, _TRUNCATE);
            for (char *c = up; *c; c++) *c = (char)toupper((unsigned char)*c);
            sprintf_s(g_on_row[k], "%s \x07" "1f%%d", up);
            return (INT64)(ULONG_PTR)g_on_row[k];
        }
    return (INT64)(ULONG_PTR)ON_GAME;
}

static int on_self_test() {                                    // names from a stand-in table and charaset image
    static char chara[16 + 2 * 0x4c + 32];
    memset(chara, 0, sizeof(chara));
    *(int *)(chara + 4) = 2; *(int *)(chara + 8) = 16 + 2 * 0x4c;
    strcpy_s(chara + 16 + 2 * 0x4c, 8, "pc00a"); strcpy_s(chara + 16 + 2 * 0x4c + 8, 8, "pc02a");
    *(int *)(chara + 16) = 0; *(int *)(chara + 16 + 0x4c) = 8;
    static OutfitName tab[3] = {{"pc02a", 5, "Fire"}, {"pc00a", 2, "Agent Smith"}, {"PC02A", 14, "Spectral"}};
    OutfitName *keep = g_on; int keep_n = g_on_n;
    g_on = tab; g_on_n = 3; g_on_t_chara = chara;
    INT64 a[3] = {44, 5, 1};
    bool fire = !strcmp((const char *)(ULONG_PTR)on_native(a), "FIRE \x07" "1f%d");
    a[1] = 14;
    bool spectral = !strcmp((const char *)(ULONG_PTR)on_native(a), "SPECTRAL \x07" "1f%d");   // stem case ignored
    a[1] = 2; a[2] = 0x30000;                                  // row 0 with an outfit in the high word
    bool smith = on_native(a) == (INT64)(ULONG_PTR)ON_GAME;   // v5.31 "AGENT SMITH 3" is 13 characters: COLOR 3
    a[1] = 3; a[2] = 1;
    bool game = on_native(a) == (INT64)(ULONG_PTR)ON_GAME;   // no name: the game's own
    a[2] = 9;
    bool bad = on_native(a) == (INT64)(ULONG_PTR)ON_GAME;    // a row the image does not have
    g_on = keep; g_on_n = keep_n; g_on_t_chara = NULL;
    logf("self-test: outfit names fire %s, case %s, smith %s, game's own %s, unknown row %s", fire ? "ok" : "FAILED",
         spectral ? "ok" : "FAILED", smith ? "ok" : "FAILED", game ? "ok" : "FAILED", bad ? "ok" : "FAILED");
    return fire && spectral && smith && game && bad ? 1 : -62;
}
