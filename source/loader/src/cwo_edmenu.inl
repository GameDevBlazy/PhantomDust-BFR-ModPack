// ---- v5.29 the Edgar's Dream pages in the game's own windows (owner 2026-10-27; earlier: "make
// sure that it looks just like the game, because it's already built in the game. So from now on, we can make it be real
// selectable options"). A page chosen in the main menu's EDGAR'S DREAM window opens as a real menu page (mainmenu page
// EDPG, tools/build_ed_pages.py), laid out like the game's CREATE GAME SESSION: a list window with "label << value >>"
// rows, a box with the page's notes and the help line at the bottom; up / down are the window's own, Left / Right, A, B,
// X, Y and Start come as the menu's events (aEDPG_l / _r / _s<row> / _tb / _tx / _ty / _st) and go to the page's own input
// code. The rows come from the page's own drawing, read line by line, so every page (the creators, Skill Fuser, Records,
// Stage Music, Draft Arsenal, Boss Raid, the name keyboard, the help cards) shows in the same windows. Create a Stage,
// Create a Skill and the actions that need a match ("Try it now", "Preview live", "3 VS 3 ...") still go to Single Play.
// Game facts (Ghidra full pass 065 via pcode.py / pd_fn.py, capstone, 2026-10-27; listings 25_Menu_Research/stage_view_
// 2026-10-27/liverulesel.txt, edgar_look_2026-10-27/mainmenu_265d9d3b.txt):
//  - the menu *0x140533c58: window k at +0x40 + k * 0x103c: +0 x, +2 y, +4 width, +6 height, +8 style (class table
//    0x1402dcaf0: +0x10 draw, +0x18 update), +0x12 cursor, +0x16 rows, +0x18 rows shown, +0x1a first row shown, +0x24 the
//    rows' text pointers (2.104 = FUN_1402466f0 keeps the pointer, it does not copy), +0x1024 title pointer; 2.97 =
//    FUN_140246640 makes window k current and clears it; the current window index is menu +0x10402.
//  - the list window's update FUN_14024b000: B -> event 6, X 8, Y 9, Left 0x13, Right 0x14, A row + 100; the event goes to
//    menu +0x10414 / +0x10418 and FUN_140245f00 names the handler: the 2.70 prefix + "_S<row>", "_TB", "_TX", "_TY",
//    "_L", "_R", "_ST" (table 0x1402f6700); up / down move +0x12 and keep +0x1a in view (+0x18 rows shown).
//  - the text drawer FUN_1402477e0: \x07 + number + code: 'w' a field that wide, '#' an icon (1 << , 0 >>), 'c' centred in
//    that width, 'r' right-aligned, 'f' a font parameter; \x0c jumps to the field's end; \n a new line; no printf.
static const ULONG_PTR RVA_EPG_MENU = 0x533c58;
// v5.36 (owner: CREATE A STAGE "stops at Underground"): 172 rows / 176 lines (was 64 / 48: the 25 pack canvases after the
// 28 stages were cut off); the window keeps up to 512 row pointers (+0x24 .. +0x1024)
static const int EPG_ROWS_WIN = 13, EPG_DESC_WIN = 14, EPG_HELP_WIN = 15, EPG_MAXROW = 172, EPG_MAXDESC = 4, EPG_SEG = 24, EPG_MAXLINE = 176;
static volatile LONG g_epg_on;                                  // a page is shown in the game's menu
static volatile LONG g_epg_capturing;                           // v5.36 the page is being read for the window (no screen window)
static int g_epg_hcur;                                           // v5.171 the help card's own cursor
static int g_epg_cur = -1, g_epg_n, g_epg_nd;                   // the cursor the loader set, rows, notes shown
static const size_t EPG_HELP_W = 54;                             // v5.171 a help row's characters that fit the window
static char g_epg_title[64], g_epg_row[EPG_MAXROW][200], g_epg_desc[EPG_MAXDESC][170], g_epg_help[200];
// v5.31 (owner 2026-10-27, screenshots of Skill Fuser, Spectator, Draft Arsenal and Boss Raid: "I like that they're
// simplified, but we need to understand what we're doing with them. Also, there can be no overlapping text"): every text
// fits its place - a value at most 18 characters between << and >> (the 240-wide field), a label beside a value at most
// 19 (the 230-wide field), the title the page's own name, the help line one line - and the notes box says what the page is
// for, then the whole of a shortened value (or what the title line said after the page's name). Widths measured on the
// owner's screenshots of these windows (about 11.5 menu units a letter in the list, 16 in the title).
static const size_t EPG_VALUE_MAX = 18, EPG_LABEL_MAX = 19, EPG_ACTION_MAX = 40, EPG_TITLE_MAX = 26, EPG_LINE_MAX = 56;   // v5.172.2 56: the notes box shows ~57 of the menu font
static char g_epg_note1[200];                                     // v5.172.2 the page's first note, whole
static char g_epg_full[EPG_MAXROW][120], g_epg_label[EPG_MAXROW][64], g_epg_status[120], g_epg_note2[130];
static char g_epg_intro[80];
static volatile LONG g_epg_pend_page = -1, g_epg_pend_ai = -1;  // an action that needs Single Play, done there
static unsigned char *g_epg_test_menu;                          // self-test: a stand-in menu
struct EpgSeg { int x; char t[160]; };
struct EpgLine { int y; DWORD col; int ns; EpgSeg s[EPG_SEG]; char text[200]; };
static EpgLine g_epg_cap[EPG_MAXLINE];
static int g_epg_capn;

static void cs_frame();
static bool cr_start();                                         // v5.127 cwo_create.inl: Edgar's Dream > CREATE                                        // v5.32 cwo_createstage.inl: the CREATE A STAGE page
static bool cs_build();
static bool cs_back_row();
static void cs_reset_list();
static ULONG_PTR epg_menu() { return g_epg_test_menu ? (ULONG_PTR)g_epg_test_menu : *(ULONG_PTR *)(g_base + RVA_EPG_MENU); }
static ULONG_PTR epg_win(int k) { ULONG_PTR m = epg_menu(); return m ? m + 0x40 + 0x103c * (ULONG_PTR)k : 0; }

static void epg_put(int x, int y, const char *s, DWORD col) {   // the page's text, line by line (same y = one line)
    if (!s) return;
    int i = 0;
    while (i < g_epg_capn && g_epg_cap[i].y != y) i++;
    if (i == g_epg_capn) {
        if (g_epg_capn >= EPG_MAXLINE) return;
        g_epg_capn++;
        g_epg_cap[i].y = y; g_epg_cap[i].col = col; g_epg_cap[i].ns = 0;
    }
    EpgLine &l = g_epg_cap[i];
    if (l.ns >= EPG_SEG) return;
    l.s[l.ns].x = x;
    strncpy_s(l.s[l.ns].t, s, _TRUNCATE);
    l.ns++;
    if (col == 0xFFFFD040) l.col = col;                         // gold anywhere on the line: the line is the chosen one
}
static void epg_join(EpgLine &l) {                              // segments by x, gaps as spaces (8 px a character)
    for (int a = 1; a < l.ns; a++) { EpgSeg t = l.s[a]; int b = a - 1; while (b >= 0 && l.s[b].x > t.x) { l.s[b + 1] = l.s[b]; b--; } l.s[b + 1] = t; }
    l.text[0] = 0;
    int at = l.ns ? l.s[0].x : 0;
    for (int k = 0; k < l.ns; k++) {
        size_t len = strlen(l.text);
        for (int g = (l.s[k].x - at) / 8; g > 0 && len + 1 < sizeof(l.text); g--) { l.text[len++] = ' '; l.text[len] = 0; }
        strncat_s(l.text, l.s[k].t, _TRUNCATE);
        at = l.s[k].x + 8 * (int)strlen(l.s[k].t);
    }
}
static void epg_trim(char *s) {
    size_t n = strlen(s);
    while (n && s[n - 1] == ' ') s[--n] = 0;
    size_t i = 0;
    while (s[i] == ' ') i++;
    if (i) memmove(s, s + i, n - i + 1);
}
// v5.31 shorten to at most max letters: at the first " (", ", ", " - " or ": " when that is enough, else at a word + ".."
static void epg_shorten(char *s, size_t max) {
    if (strlen(s) <= max) return;
    static const char *CUTS[] = {"  ", " (", ", ", " - ", ": "};   // v5.175.1 "  " first: the value's own separator ("Psycho Burst  cost 2")
    for (const char *c : CUTS) {                                  // v5.172.2 the LAST cut that fits (keeps the most)
        char *best = NULL;
        for (char *p = strstr(s, c); p && (size_t)(p - s) <= max; p = strstr(p + 1, c)) if (p > s) best = p;
        if (best) { *best = 0; epg_trim(s); return; }
    }
    size_t take = max - 2;
    while (take > max / 2 && s[take] != ' ') take--;
    if (take <= max / 2) take = max - 2;
    s[take] = 0;
    epg_trim(s);
    for (size_t n = strlen(s); n > 1 && strchr("+-/&,:", s[n - 1]); n = strlen(s)) { s[n - 1] = 0; epg_trim(s); }   // v5.175.1 no "+.."
    strcat_s(s, max + 3, "..");
}
// v5.31 one line of at most max letters: cut at the last "; ", ". " or a wide gap before it, else at a word; no ':' / ','
// left hanging at the end
static void epg_fit_line(char *s, size_t max) {
    if (strlen(s) > max) {
        size_t cut = 0;
        for (size_t i = max / 2; i + 1 < max; i++)
            if ((s[i] == ';' || s[i] == '.') && s[i + 1] == ' ') cut = i + 1;
            else if (s[i] == ' ' && s[i + 1] == ' ' && s[i + 2] == ' ') cut = i;
        if (!cut) { cut = max; while (cut > max / 2 && s[cut] != ' ') cut--; if (cut <= max / 2) cut = max; }
        s[cut] = 0;
    }
    epg_trim(s);
    size_t n = strlen(s);
    while (n && (s[n - 1] == ':' || s[n - 1] == ',' || s[n - 1] == ';')) { s[--n] = 0; epg_trim(s); n = strlen(s); }
}
static bool epg_ends(const char *s, const char *tail) { size_t a = strlen(s), b = strlen(tail); return a > b && !_stricmp(s + a - b, tail); }

// "> Label        < value >"  ->  \x07 230w Label \x0c \x07 1# \x07 240c value \x0c \x07 0#   (the CREATE GAME SESSION row)
// idx: the row's number (its whole value is kept in g_epg_full[idx] when the row shows a shorter one); -1: not kept
static void epg_format_row(const char *src, char *out, size_t n, int idx = -1) {
    if (idx >= 0 && idx < EPG_MAXROW) { g_epg_full[idx][0] = 0; g_epg_label[idx][0] = 0; }
    char t[200];
    strncpy_s(t, src[0] && src[1] ? src + 2 : src, _TRUNCATE);
    for (char *p = t; *p; p++) if ((unsigned char)*p < 32) *p = ' ';
    char *gap = strstr(t, "  ");
    while (gap && gap == t) gap = strstr(gap + 1, "  ");
    if (!gap) {
        epg_trim(t);
        // v5.31 "Player 1 starts as a CPU off", "Use the draft in matches: off": the setting beside its label
        const char *val = epg_ends(t, " off") ? "Off" : epg_ends(t, " on") ? "On" : NULL;
        if (val) {
            t[strlen(t) - (val[1] == 'f' ? 4 : 3)] = 0;
            epg_trim(t);
            size_t tl = strlen(t);
            if (tl && t[tl - 1] == ':') t[tl - 1] = 0;
            epg_trim(t);
            epg_shorten(t, 28);
            sprintf_s(out, n, "\x07" "230w%s\x0c\x07" "240c%s\x0c", t, val);
            return;
        }
        if (strlen(t) > EPG_ACTION_MAX) {
            if (idx >= 0 && idx < EPG_MAXROW) strncpy_s(g_epg_full[idx], t, _TRUNCATE);
            epg_shorten(t, EPG_ACTION_MAX);
        }
        strncpy_s(out, n, t, _TRUNCATE);
        return;
    }
    *gap = 0;
    char label[120], value[120];
    strncpy_s(label, t, _TRUNCATE);
    strncpy_s(value, gap + 2, _TRUNCATE);
    epg_trim(label); epg_trim(value);
    size_t vl = strlen(value);
    size_t named = 0;                                             // v5.175.2 the bracketed name's length ("< Psycho Burst >  cost 3")
    if (vl >= 2 && value[0] == '<' && value[1] == ' ') {          // "< FROST >  1/1  IN USE": the page's own arrows go
        memmove(value, value + 2, vl - 1);
        char *close = strstr(value, " >");
        if (close) { named = (size_t)(close - value); memmove(close, close + 2, strlen(close + 2) + 1); }
    }
    for (char *p = value; *p; p++) while (p[0] == ' ' && p[1] == ' ') memmove(p, p + 1, strlen(p));   // one space between parts
    epg_trim(value);
    if (!value[0]) { strncpy_s(out, n, label, _TRUNCATE); return; }
    size_t ll = strlen(label);                                    // "CPU allies:" -> "CPU allies"
    if (ll && label[ll - 1] == ':') { label[ll - 1] = 0; epg_trim(label); }
    if (strlen(value) > EPG_VALUE_MAX || strlen(label) > EPG_LABEL_MAX) {   // v5.31 no text over the arrows
        if (idx >= 0 && idx < EPG_MAXROW) { strncpy_s(g_epg_full[idx], value, _TRUNCATE); strncpy_s(g_epg_label[idx], label, _TRUNCATE); }
        if (named >= 3 && named <= EPG_VALUE_MAX && strlen(value) > EPG_VALUE_MAX) { value[named] = 0; epg_trim(value); }   // v5.175.2 the name whole
        epg_shorten(value, EPG_VALUE_MAX);
        epg_shorten(label, EPG_LABEL_MAX);
    }
    sprintf_s(out, n, "\x07" "230w%s\x0c\x07" "1#\x07" "240c%s\x0c\x07" "0#", label, value);   // \x0c: >> at the field's end
}

// v5.31 the page's own name for the title, and one line on what the page is for (from each page's how-to card)
static const char *epg_page_name(int page) {
    switch (page) {
    case EDP_SCHOOL: return "CREATE A SCHOOL";   case EDP_CHAR: return "CREATE A CHARACTER";
    case EDP_QUEST: return "CREATE A QUEST";     case EDP_MODE: return "CREATE A MODE";
    case EDP_SPECT: return "SPECTATOR";          case EDP_FUSE: return "SKILL FUSER";
    case EDP_RECORDS: return "RECORDS";          case EDP_MUSIC: return "STAGE MUSIC";
    case EDP_DRAFT: return "DRAFT ARSENAL";      case EDP_RAID: return "BOSS RAID";
    case EDP_STAGE: return "CREATE A STAGE";     case EDP_OUTFIT: return "CREATE AN OUTFIT";
    case EDP_VISIONS: return "VISIONS";
    case EDP_VAULT: return "SKILL VAULT";
    }
    return NULL;
}
static const char *epg_page_intro(int page) {
    switch (page) {
    case EDP_SCHOOL: return "Make your own school: its aura color and its aura effect.";
    case EDP_CHAR: return "Build a fighter: body, head, outfit colors and moves.";
    case EDP_QUEST: return "Make a story quest: stage, opponents, rules and rewards.";
    case EDP_MODE: return "Make a match rule; play it: lobby X > Change Rule.";   // v5.188.34 any lobby (SINGLE PLAY too, 5.188.32)
    case EDP_SPECT: return "A CPU plays your fighter: in a match hold View, press LB.";
    case EDP_FUSE: return "Join two skills: one skill's move with both skills' effects.";
    case EDP_RECORDS: return "Your offline match results, counted after each match.";
    case EDP_MUSIC: return "Choose the music each stage plays in offline matches.";
    case EDP_DRAFT: return "Draft an arsenal: take turns picking from a dealt pool.";
    case EDP_RAID: return "Fight a story boss with CPU allies: pick a Boss, then any offline versus with a CPU.";
    case EDP_STAGE: return "Build a map: every stage's objects, or an empty stage.";
    case EDP_OUTFIT: return "Recolor an outfit: skin, head accessory, jacket, pants.";
    case EDP_VISIONS: return "Graphics looks for menus and offline matches.";
    case EDP_VAULT: return "Skills not in your stock: swap one in.";
    }
    return "";
}

static void epg_capture() {                                     // the page's own drawing, read instead of shown
    g_epg_capn = 0;
    auto put = [](int x, int y, const char *s, DWORD c) { epg_put(x, y, s, c); };
    auto box = [](int, int, DWORD, int, int) {};
    char st[96] = "";
    bool status = GetTickCount64() < g_status_until;
    if (status) { EnterCriticalSection(&g_ui_lock); strcpy_s(st, g_status); LeaveCriticalSection(&g_ui_lock); }
    InterlockedExchange(&g_epg_capturing, 1);
    ed_draw(put, box, 0, 0, status, st);
    InterlockedExchange(&g_epg_capturing, 0);
    for (int a = 1; a < g_epg_capn; a++) {                       // top to bottom
        EpgLine t = g_epg_cap[a]; int b = a - 1;
        while (b >= 0 && g_epg_cap[b].y > t.y) { g_epg_cap[b + 1] = g_epg_cap[b]; b--; }
        g_epg_cap[b + 1] = t;
    }
    for (int i = 0; i < g_epg_capn; i++) epg_join(g_epg_cap[i]);
}

// the captured lines -> the title, the rows (the chosen one = *sel), the notes and the help line
static void epg_build(int *sel) {
    *sel = 0; g_epg_n = 0; g_epg_nd = 0; g_epg_help[0] = 0; g_epg_status[0] = 0; g_epg_note1[0] = 0;
    strcpy_s(g_epg_title, "EDGAR'S DREAM");
    if (g_epg_capn == 0) { strcpy_s(g_epg_row[0], "..."); g_epg_n = 1; return; }
    char t[200];
    strncpy_s(t, g_epg_cap[0].text, _TRUNCATE);
    bool kb = g_ed_kb != 0, help = g_ed_help != 0;
    const char *own = kb || help ? NULL : epg_page_name(g_ed_page);
    if (own && !_strnicmp(t, own, strlen(own))) {               // v5.31 the page's name; what follows it goes to the notes
        strcpy_s(g_epg_title, own);
        strncpy_s(g_epg_status, t + strlen(own), _TRUNCATE);
        epg_trim(g_epg_status);
        if (g_epg_status[0] == '(' || g_epg_status[0] == '-') g_epg_status[0] = 0;   // "(B: back   Y: help)": the legend's
    } else {
        // v5.84: the keyboard's title keeps the name typed so far ("NAME THIS OUTFIT:  MY FIRE_")
        char *cut = kb ? NULL : strstr(t, "  (");
        if (!cut && !kb) cut = strstr(t, "  ");
        if (cut) *cut = 0;
        if (kb) { char *d = strstr(t, ":  "); if (d) memmove(d + 2, d + 3, strlen(d + 3) + 1); }   // one space after the colon
        epg_trim(t);
        if (t[0]) strncpy_s(g_epg_title, t, _TRUNCATE);
        if (!kb && strlen(g_epg_title) > EPG_TITLE_MAX) epg_shorten(g_epg_title, EPG_TITLE_MAX);
    }
    int last_row = -1;
    for (int i = 1; i < g_epg_capn; i++) {
        const EpgLine &l = g_epg_cap[i];
        const char *s = l.text;
        bool blank = true;
        for (const char *p = s; *p; p++) if (*p != ' ' && *p != '^') blank = false;
        if (blank) continue;
        bool marker = (s[0] == '>' || s[0] == ' ') && s[1] == ' ';
        bool hint = l.col == 0xFFA0E0FF;
        bool row = kb || help ? !hint : marker;
        if (row && g_epg_n < EPG_MAXROW) {
            if (kb || help) {
                strncpy_s(g_epg_row[g_epg_n], s, _TRUNCATE);
                // v5.84 (owner 2026-11-02): the keyboard's '>' before the chosen key becomes brackets around it ("[G]")
                char *g = kb ? strchr(g_epg_row[g_epg_n], '>') : NULL;
                if (g) {
                    char *k = g + 1;
                    while (*k == ' ') k++;
                    if (*k) {
                        char out[sizeof(g_epg_row[0])];
                        size_t pre = (size_t)(g - g_epg_row[g_epg_n]);
                        sprintf_s(out, "%.*s[%c]%s", (int)pre, g_epg_row[g_epg_n], *k, k + 1);
                        strncpy_s(g_epg_row[g_epg_n], out, _TRUNCATE);
                    }
                }
                for (char *p = g_epg_row[g_epg_n]; *p; p++) if (*p == '>') *p = ' ';
                // v5.171 (owner 2026-11-13; in game the help cards'
                // long lines ran past the window): a help line longer than the window wraps onto follow-on rows, indented
                // under its text (after the step number); the window scrolls as before
                while (help && !kb && strlen(g_epg_row[g_epg_n]) > EPG_HELP_W && g_epg_n + 1 < EPG_MAXROW) {
                    char *r = g_epg_row[g_epg_n];
                    size_t ind = 0;
                    while (r[ind] == ' ') ind++;
                    if (r[ind] >= '0' && r[ind] <= '9') { ind++; while (r[ind] == ' ') ind++; }
                    if (ind > 12) ind = 3;
                    size_t cut = EPG_HELP_W;
                    while (cut > ind + 10 && r[cut] != ' ') cut--;
                    if (cut <= ind + 10) cut = EPG_HELP_W;
                    char rest[sizeof(g_epg_row[0])];
                    const char *q = r + cut; while (*q == ' ') q++;
                    sprintf_s(rest, "%*s%s", (int)ind, "", q);
                    r[cut] = 0; { size_t e = strlen(r); while (e && r[e - 1] == ' ') r[--e] = 0; }   // the end only: the indent stays
                    g_epg_n++;
                    strncpy_s(g_epg_row[g_epg_n], rest, _TRUNCATE);
                }
            }
            else epg_format_row(s, g_epg_row[g_epg_n], sizeof(g_epg_row[0]), g_epg_n);
            if ((kb && strchr(s, '>')) || (!kb && !help && s[0] == '>')) *sel = g_epg_n;
            last_row = i;
            g_epg_n++;
            continue;
        }
        if (hint && !g_epg_help[0] && i > last_row) { strncpy_s(g_epg_help, s, _TRUNCATE); epg_trim(g_epg_help); continue; }
        char note[200];
        strncpy_s(note, s, _TRUNCATE);
        epg_trim(note);
        if (!g_epg_note1[0]) strcpy_s(g_epg_note1, note);           // v5.172.2 the first note whole (not its first wrapped piece)
        for (char *p = note; *p && g_epg_nd < EPG_MAXDESC; ) {     // wrapped at about 60 characters (the box is 544 wide)
            size_t len = strlen(p), take = len;
            if (len > 60) { take = 60; while (take > 20 && p[take] != ' ') take--; if (take <= 20) take = 60; }
            strncpy_s(g_epg_desc[g_epg_nd], p, take);
            epg_trim(g_epg_desc[g_epg_nd]);
            g_epg_nd++;
            p += take;
            while (*p == ' ') p++;
        }
    }
    if (!g_epg_help[0]) strcpy_s(g_epg_help, help ? "A / B / Y: close this help" : "Left / Right: change   A: choose   Y: help");
    if (g_epg_n == 0) { strcpy_s(g_epg_row[0], g_epg_nd ? g_epg_desc[0] : "..."); g_epg_n = 1; }
    epg_fit_line(g_epg_help, 62);                               // v5.31 one line at the bottom
    // v5.31 the notes box: what the page is for, then the chosen row's whole value (when its row shows less) or the page's
    // status (the title line's rest, else its first note)
    strcpy_s(g_epg_intro, kb || help ? "" : epg_page_intro(g_ed_page));
    g_epg_note2[0] = 0;
    if (!kb && !help && *sel >= 0 && *sel < g_epg_n && g_epg_full[*sel][0]) {
        if (g_epg_label[*sel][0]) sprintf_s(g_epg_note2, "%s: %s", g_epg_label[*sel], g_epg_full[*sel]);
        else strcpy_s(g_epg_note2, g_epg_full[*sel]);
    } else if (g_epg_status[0]) strcpy_s(g_epg_note2, g_epg_status);
    else if (g_epg_note1[0]) strcpy_s(g_epg_note2, g_epg_note1);   // v5.172.2 (in game: '... 1 life, 3' - a cut piece)
    else if (g_epg_nd) strcpy_s(g_epg_note2, g_epg_desc[0]);
    if (strlen(g_epg_note2) > EPG_LINE_MAX) epg_shorten(g_epg_note2, EPG_LINE_MAX);
}

static void epg_fill() {                                        // the three windows from the page, the cursor on its row
    int sel = 0;
    epg_capture();
    epg_build(&sel);
    // v5.171 (in game: Down never moved a help card, its last lines stayed out of sight): the card keeps the cursor the list
    // moved to; it starts on the first line each time a card opens
    { static LONG was; bool hc = g_ed_help && !g_ed_kb; if (hc && !was) g_epg_hcur = 0; was = hc;
      if (hc) { if (g_epg_hcur >= g_epg_n) g_epg_hcur = g_epg_n - 1; if (g_epg_hcur < 0) g_epg_hcur = 0; sel = g_epg_hcur; } }
    ULONG_PTR w = epg_win(EPG_ROWS_WIN), d = epg_win(EPG_DESC_WIN), h = epg_win(EPG_HELP_WIN);
    if (!w || *(short *)(w + 8) != 1) return;
    *(char **)(w + 0x1024) = g_epg_title;
    for (int r = 0; r < g_epg_n; r++) *(char **)(w + 0x24 + 8 * (ULONG_PTR)r) = g_epg_row[r];
    *(short *)(w + 0x16) = (short)g_epg_n;
    *(short *)(w + 0x12) = (short)sel;
    short shown = *(short *)(w + 0x18), &top = *(short *)(w + 0x1a);
    if (shown > 0) { if (sel < top) top = (short)sel; if (sel >= top + shown) top = (short)(sel - shown + 1); if (top < 0) top = 0; }
    g_epg_cur = sel;
    if (d && *(short *)(d + 8) == 2) {
        static char none[] = "";
        if (g_ed_kb || g_ed_help) {                             // the keyboard / help card: its own notes, as before
            int nd = g_epg_nd ? g_epg_nd : 1;
            for (int r = 0; r < nd; r++) *(char **)(d + 0x24 + 8 * (ULONG_PTR)r) = g_epg_nd ? g_epg_desc[r] : none;
            *(short *)(d + 0x16) = (short)nd;
        } else {                                                // v5.31 two lines: the page's purpose, then the detail
            *(char **)(d + 0x24) = g_epg_intro;
            *(char **)(d + 0x2c) = g_epg_note2[0] ? g_epg_note2 : none;
            *(short *)(d + 0x16) = 2;
        }
    }
    if (h && *(short *)(h + 8) == 3) {                          // the help window shows the row the list's cursor is on
        for (int r = 0; r < g_epg_n; r++) *(char **)(h + 0x24 + 8 * (ULONG_PTR)r) = g_epg_help;
        *(short *)(h + 0x16) = (short)g_epg_n;
    }
}

static const int EPG_PAGE_OF[ED_N] = {EDP_STAGE, -1, EDP_SCHOOL, EDP_CHAR, EDP_QUEST, EDP_MODE, EDP_SPECT, EDP_FUSE, EDP_RECORDS,
                                      EDP_MUSIC, EDP_DRAFT, EDP_RAID, EDP_OUTFIT, EDP_VISIONS};
static int epg_entry_of(int page) { for (int k = 0; k < ED_N; k++) if (EPG_PAGE_OF[k] == page) return k; return -1; }

static void epg_press(WORD bit) { ed_input(bit, bit); }

// an action row that needs a match: done in Single Play (the old way in), its page opened there first
static bool epg_needs_match(int page, int *ai) {
    if (page < EDP_SCHOOL || page >= EDP_N || g_ed_kb || g_ed_help || (g_ed_cat && page == EDP_CHAR)) return false;
    const EdCreator &c = g_edc[page];
    int a = g_ed_row - 2 - c.nf;
    if (a < 0 || a >= c.na) return false;
    const char *s = c.actions[a];
    if (strncmp(s, "Try it now", 10) && strncmp(s, "Preview live", 12) && strncmp(s, "3 VS 3", 6)) return false;
    *ai = a;
    return true;
}

// native 2.8 ops 51-54 (mainmenu, build_ed_pages.py): 51 a row of the EDGAR'S DREAM window chosen (args[1] = the row): 1 =
// shown here (the script builds page EDPG), 0 = the old way (Single Play); 52 fill the page's windows (end of the build);
// 53 every frame (args[1] = the list's cursor); 54 a key (args[1]: 1 A, 2 B, 3 X, 4 Y, 5 Left, 6 Right, 7 Start;
// args[2] = A's row): 0 stay, 2 back to the EDGAR'S DREAM window, 3 into Single Play
static INT64 epg_native(int op, INT64 *args) {
    // v5.32 61: the page's draw asks whether to draw the menu background (0 while CREATE A STAGE shows a stage behind it)
    if (op == 61) { __try { return g_epg_on && g_ed_page == EDP_STAGE && lv_showing() ? 0 : 1; } __except (EXCEPTION_EXECUTE_HANDLER) { return 1; } }
    __try {
        if (op == 51) {
            int k = ed_row_entry(args[1]);                   // v5.59 the row's entry (the BFR edition lists only its own)
            if (k < 0 || k >= ED_N || EPG_PAGE_OF[k] < 0) return 0;
            ed_load_all();
            int page = EPG_PAGE_OF[k];
            if (k == 0 && args[1] == 0 && !ed_online() && cr_start()) {   // v5.188.19 the Create a Stage row (2) goes to the stage list   // v5.127 CREATE: straight into the editor's session (v5.150.0 BFR too)
                InterlockedExchange(&g_epg_on, 0);
                ed_state_save();
                return 0;                                          // the session way in (as Create a Skill)
            }
            InterlockedExchange(&g_ed_help, 0);
            InterlockedExchange(&g_ed_kb, 0);
            InterlockedExchange(&g_ed_page, page);
            if (page < EDP_N) InterlockedExchange(&g_ed_row, g_ed_rows_sel[page]);
            if (page == EDP_STAGE) cs_reset_list();              // v5.32 the stage list read again
            if (page == EDP_CHAR) { InterlockedExchange(&g_ed_cat_sel, g_edc[EDP_CHAR].cur); InterlockedExchange(&g_ed_cat, 1); }   // v5.35 My characters
            if (page == EDP_OUTFIT) {                            // v5.112 (owner 2026-11-07): straight into "Preview live" (the action that
                // goes through Single Play, as A on its row did) - the old way in (0) with the action pending
                int ai = -1;
                for (int a = 0; a < g_edc[page].na; a++) if (!strncmp(g_edc[page].actions[a], "Preview live", 12)) ai = a;
                if (ai >= 0 && !ed_online()) {
                    InterlockedExchange(&g_epg_pend_page, page);
                    InterlockedExchange(&g_epg_pend_ai, ai);
                    InterlockedExchange(&g_ed_direct, k);
                    InterlockedExchange(&g_epg_on, 0);
                    ed_state_save();
                    logf("Edgar's Dream page in the game's menu: Create an Outfit - straight into the live preview");
                    return 0;
                }
            }
            InterlockedExchange(&g_epg_on, 1);
            g_epg_cur = -1;
            logf("Edgar's Dream page in the game's menu: %s", ED_SHORT[k]);
            return 1;
        }
        if (!g_epg_on) return op == 54 ? 2 : 0;
        if (op == 52) { epg_fill(); return 0; }
        if (op == 53) {
            int cur = (int)args[1];
            if (g_ed_help && !g_ed_kb && g_epg_cur >= 0 && cur != g_epg_cur) g_epg_hcur = cur;   // v5.171 the help card scrolls (its own cursor)
            else if (g_epg_cur >= 0 && cur != g_epg_cur && g_epg_n > 1) {   // the list's own up / down: the page's
                bool down = cur == g_epg_cur + 1 || (g_epg_cur == g_epg_n - 1 && cur == 0);
                epg_press((WORD)(down ? BTN_DOWN : BTN_UP));
            }
            if (g_ed_page == EDP_STAGE) cs_frame();               // v5.32 the highlighted stage behind the page
            epg_fill();
            return 0;
        }
        if (op == 54) {
            int key = (int)args[1], page = g_ed_page;
            static const WORD BITS[8] = {0, BTN_A, BTN_B, BTN_X, BTN_Y, BTN_LEFT, BTN_RIGHT, BTN_START};
            if (key < 1 || key > 7) return 0;
            if (key == 1 && page == EDP_STAGE) {                 // v5.32 CREATE A STAGE: build on the highlighted stage
                if (cs_back_row()) { InterlockedExchange(&g_ed_page, EDP_CHOOSER); InterlockedExchange(&g_epg_on, 0); ed_state_save(); return 2; }
                if (cs_build()) { InterlockedExchange(&g_epg_on, 0); ed_state_save(); return 3; }
                return 0;
            }
            int ai;
            // v5.188.11 hourly Create a Mode pass 2 (in game 2026-11-16 18:24: "3 VS 3 LOBBY" went into Single Play, whose exit
            // is the TITLE screen - the player had to press Enter and pick the profile before the lobby came): this page is
            // already in the main menu, so the lobby is asked for here and the page closes; the main menu's own frame (2.8(21))
            // then goes to MULTIPLAYER > Split Screen > the 3 vs 3 lobby. No Single Play, no title.
            if (key == 1 && epg_needs_match(page, &ai) && page == EDP_MODE && !strncmp(g_edc[page].actions[ai], "3 VS 3", 6)) {
                if (g_sixf2_state != 1) { set_status("3 vs 3 is not ready: six fighters is off or was refused (see the log)"); rumble(2); return 0; }
                if (!g_lob_loaded) lob_load();
                InterlockedExchange(&g_l3, 1);
                g_l3_t = GetTickCount64();
                InterlockedExchange(&g_ed_page, EDP_CHOOSER);
                InterlockedExchange(&g_epg_on, 0);
                ed_state_save();
                logf("3 VS 3 LOBBY: asked from the game's menu page - straight to the lobby (no Single Play)");
                set_status("3 VS 3: press B - the lobby opens from the main menu");   // the CREATE list shows first
                rumble(1);
                return 2;
            }
            if (key == 1 && epg_needs_match(page, &ai)) {       // Single Play: its page opens, then this action
                int k = epg_entry_of(page);
                InterlockedExchange(&g_epg_pend_page, page);
                InterlockedExchange(&g_epg_pend_ai, ai);
                InterlockedExchange(&g_ed_direct, k);
                InterlockedExchange(&g_epg_on, 0);
                ed_state_save();
                logf("Edgar's Dream page in the game's menu: '%s' needs a match - into Single Play", g_edc[page].actions[ai]);
                return 3;
            }
            epg_press(BITS[key]);
            if (g_ed_page == EDP_CHOOSER) { InterlockedExchange(&g_epg_on, 0); ed_state_save(); return 2; }
            epg_fill();
            return 0;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        logf("Edgar's Dream page in the game's menu: fault - back to the EDGAR'S DREAM window");
        InterlockedExchange(&g_epg_on, 0);
        return op == 54 ? 2 : 0;
    }
    return 0;
}

// Single Play, page open: the action asked for in the game's menu (ed_input, worker thread)
static bool epg_pending_step(int page) {
    if (g_epg_pend_ai < 0 || page != g_epg_pend_page || g_ed_kb || g_ed_help) return false;
    int ai = InterlockedExchange(&g_epg_pend_ai, -1);
    if (ai < 0 || page < EDP_SCHOOL || page >= EDP_N || ai >= g_edc[page].na) return false;
    InterlockedExchange(&g_ed_row, 2 + g_edc[page].nf + ai);
    logf("Edgar's Dream: '%s' (asked for in the game's menu)", g_edc[page].actions[ai]);
    ed_action(page, ai);
    return true;
}

static int epg_self_test() {                                    // the row format, the line reader, the windows on a stand-in
    char out[200];
    epg_format_row("> Hue                     < 110 >", out, sizeof(out));
    bool fmt = !strcmp(out, "\x07" "230wHue\x0c\x07" "1#\x07" "240c110\x0c\x07" "0#");
    epg_format_row("  Save", out, sizeof(out));
    fmt &= !strcmp(out, "Save");
    epg_format_row("> School                 < FROST >  1/1  IN USE", out, sizeof(out));
    fmt &= !strcmp(out, "\x07" "230wSchool\x0c\x07" "1#\x07" "240cFROST 1/1 IN USE\x0c\x07" "0#");
    epg_format_row("  School skill 1           none", out, sizeof(out));
    fmt &= !strcmp(out, "\x07" "230wSchool skill 1\x0c\x07" "1#\x07" "240cnone\x0c\x07" "0#");
    // v5.31 nothing over the arrows: the value shortened (the whole kept for the notes), an on/off setting beside its label
    epg_format_row("> Base skill          < Psycho Wave Psycho, cost 4, damage 1 >", out, sizeof(out), 0);
    fmt &= !strcmp(out, "\x07" "230wBase skill\x0c\x07" "1#\x07" "240cPsycho Wave Psycho\x0c\x07" "0#") &&
           !strcmp(g_epg_full[0], "Psycho Wave Psycho, cost 4, damage 1") && !strcmp(g_epg_label[0], "Base skill");
    epg_format_row("  CPU allies:         < 2 (their level: the 3 VS 3 LOBBY's CPU level) >", out, sizeof(out), 1);
    fmt &= !strcmp(out, "\x07" "230wCPU allies\x0c\x07" "1#\x07" "240c2\x0c\x07" "0#");
    epg_format_row("  Player 1 starts as a CPU off", out, sizeof(out), 2);
    fmt &= !strcmp(out, "\x07" "230wPlayer 1 starts as a CPU\x0c\x07" "240cOff\x0c");
    char line[100] = "Then: main menu > Single Play > any stage > your arsenal > a boss row:";
    epg_fit_line(line, 62);
    fmt &= strlen(line) <= 62 && line[strlen(line) - 1] != ':';
    if (!fmt) logf("self-test: Edgar's Dream page rows: '%s' / '%s'", out, line);
    // a page as the drawing gives it: title, three rows (the second chosen), a hint, a note
    g_epg_capn = 0;
    epg_put(0, 0, "CREATE A SCHOOL                 (B: back   Y: help)", 0xFFFFD040);
    epg_put(0, 20, "  School                 < Fire >", 0xFFFFFFFF);
    epg_put(0, 36, "> Hue                    110", 0xFFFFD040);
    epg_put(0, 52, "  Save", 0xFFA0E0FF);
    epg_put(0, 70, "The aura's color", 0xFFA0E0FF);
    epg_put(0, 90, "Color", 0xFFFFFFFF);
    epg_put(64, 106, "^", 0xFFFFFFFF);
    for (int i = 0; i < g_epg_capn; i++) epg_join(g_epg_cap[i]);
    LONG keep_kb = g_ed_kb, keep_help = g_ed_help, keep_page = g_ed_page;
    g_ed_kb = 0; g_ed_help = 0; g_ed_page = EDP_SCHOOL;
    int sel = -1;
    epg_build(&sel);
    bool parse = !strcmp(g_epg_title, "CREATE A SCHOOL") && g_epg_n == 3 && sel == 1 && !strcmp(g_epg_row[2], "Save") &&
                 !strcmp(g_epg_help, "The aura's color") && g_epg_nd == 1 && !strcmp(g_epg_desc[0], "Color") &&
                 !strncmp(g_epg_intro, "Make your own school", 20) && !strcmp(g_epg_note2, "Color");
    // the keyboard: segments joined by x, the chosen key's row
    g_epg_capn = 0;
    epg_put(0, 0, "NAME THIS SCHOOL:  FIRE_", 0xFFFFD040);
    epg_put(0, 20, "A", 0xFFFFFFFF); epg_put(24, 20, "B", 0xFFFFFFFF);
    epg_put(0, 36, "C", 0xFFFFFFFF); epg_put(24, 36, "D", 0xFFFFD040); epg_put(16, 36, ">", 0xFFFFD040);
    epg_put(0, 56, "A type  X delete", 0xFFA0E0FF);
    for (int i = 0; i < g_epg_capn; i++) epg_join(g_epg_cap[i]);
    g_ed_kb = 1;
    epg_build(&sel);
    bool kb = g_epg_n == 2 && sel == 1 && !strcmp(g_epg_row[0], "A  B") && !strcmp(g_epg_help, "A type  X delete");
    g_ed_kb = keep_kb; g_ed_help = keep_help; g_ed_page = keep_page;
    // the windows: rows, cursor, title, notes, help on a stand-in menu
    static unsigned char menu[0x40 + 16 * 0x103c];
    memset(menu, 0, sizeof(menu));
    g_epg_test_menu = menu;
    ULONG_PTR w = epg_win(EPG_ROWS_WIN), d = epg_win(EPG_DESC_WIN), h = epg_win(EPG_HELP_WIN);
    *(short *)(w + 8) = 1; *(short *)(d + 8) = 2; *(short *)(h + 8) = 3; *(short *)(w + 0x18) = 2;
    strcpy_s(g_epg_row[0], "one"); strcpy_s(g_epg_row[1], "two"); strcpy_s(g_epg_row[2], "three");
    g_epg_n = 3; g_epg_nd = 1;
    // epg_fill reads the page itself; the parts after the capture are checked here the same way
    *(char **)(w + 0x1024) = g_epg_title;
    for (int r = 0; r < g_epg_n; r++) *(char **)(w + 0x24 + 8 * (ULONG_PTR)r) = g_epg_row[r];
    *(short *)(w + 0x16) = (short)g_epg_n;
    bool win = *(char **)(w + 0x24 + 16) == g_epg_row[2] && *(short *)(w + 0x16) == 3 && epg_win(13) == (ULONG_PTR)menu + 0x40 + 13 * 0x103c;
    g_epg_test_menu = NULL;
    g_epg_capn = 0; g_epg_n = 0; g_epg_nd = 0;
    bool map = epg_entry_of(EDP_MUSIC) == 9 && epg_entry_of(EDP_SCHOOL) == 2 && EPG_PAGE_OF[0] == EDP_STAGE && EPG_PAGE_OF[1] < 0;
    logf("self-test: Edgar's Dream pages in the game's menu: rows %s, lines %s, keyboard %s, windows %s, entries %s",
         fmt ? "ok" : "FAILED", parse ? "ok" : "FAILED", kb ? "ok" : "FAILED", win ? "ok" : "FAILED", map ? "ok" : "FAILED");
    return fmt && parse && kb && win && map ? 1 : -73;
}
