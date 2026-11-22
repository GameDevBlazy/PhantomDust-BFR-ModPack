// ---- v5.80 MOD LIST on the main menu (owner 2026-11-02). Read-only.
// The main menu's MOD LIST row (id 170, tools/build_modlist_menu.py) runs native 2.8(95): the loader opens this window and
// holds the pad (Up / Down scroll, B or A at the end closes). Each mod: its name in the holiday's accent, what it does
// wrapped to the window (no line runs past it). The list: CwoModList.txt in the loader folder ("Name|what it does" lines),
// else the built-in list below.
struct MlMod { const char *name, *what; int ed; };   // v5.180.3: an old entry removed (its skills were taken out 2026-11-07)                   // v5.170 ed: 0 both editions, 1 MODS only, 2 BFR only
static const MlMod ML_BUILTIN[] = {
    {"CwoLoader (Phantom Dust MODS)", "Our own loader: runs every mod below inside the game, offline unless a mod says otherwise.", 1},   // v5.185.2 per edition
    {"CwoLoader (BFR mod pack)", "Our own loader: runs every mod below inside the game, offline unless a mod says otherwise.", 2},
    {"CREATE", "Main menu CREATE (was Edgar's Dream): Create a Stage, Skill, Character, School, Quest, Mode, Outfit, the Skill Fuser and more."},   // v5.180.1
    {"Single Play", "MULTIPLAYER > Single Play: you against CPU fighters with the lobby's rules and types (Boss Raid, Spectator Mode ...). Offline.", 1},   // v5.185.2 MODS wording
    {"Single Play", "MULTIPLAYER > Single Play: you and CPU fighters - 1 vs 1, Tag Team, 3 vs 3, Battle Royale, Boss Raid; the CPUs move, fight and revive their teammates. Offline.", 2},
    {"OPTIONS: Records, Visions, Stage Music", "Your wins, losses, streaks and favourite skills; graphics looks; the music of each stage."},
    {"Pause-menu Teammate Lock-on", "PAUSE MENU row: lock on to your teammates or not, during an offline match (online: the lobby's BFR Options)."},
    {"Body shape", "Freia and Gyne have a slightly fuller bust on every outfit (CwoBodyShape.txt; delete a line to undo)."},
    {"Create an Outfit", "Recolor an outfit live on the turning model, save it to a slot or the outfit stock (50 per character)."},
    {"Outfit effects", "A skill's flames, ice or sparks on the body; move it, size it, speed it up, slow it or pause it."},
    {"Outfit stock + themes", "Keep your outfits per character; 17 theme outfits, no two characters look the same."},
    {"15 outfits per fighter", "Every fighter has 15 outfit slots; slot 5 is the original outfit."},
    {"HUD faces", "Each fighter's face beside their name; it changes with damage, KO, hits and status effects."},
    {"HUD presets", "Move the battle HUD (lobby BFR Options or CwoHud.txt)."},
    {"Select portraits", "New faces for every player, monster portraits on X, the NPC page, and the face follows the highlighted outfit; COMING SOON is decoration only."},   // v5.185.2
    {"Intro poses", "Every fighter strikes their quest intro pose during the 3-second countdown (online too)."},   // v5.185.2 (online since BFR 1.43.1)
    {"Holiday themes", "Menus and the title logo take the holiday's colors on its dates, with a greeting and What's New."},
    {"Download Battle line-ups", "Scoreboard Download Battle: your arsenal, stage, character, and 1v1, 1v2, 1v3 or 2v2 with a CPU."},
    {"3 vs 3 and lobby options", "Six-player lobbies, Change Rule, Change Game Type, Edit Arsenal and teammate lock-on in the lobby."},
    {"Multiplayer rules", "Most KOs, First to 5 KOs, Survival revives, King of the Hill, Shrinking Arena, Aura Rush, Sudden Death and Realistic Collision (online too, unrated); the KO tally; Next Map rotation; the Tiebreaker vote after a 1 - 1."},   // v5.185.2
    {"Arsenal lists + Arsenal Data", "Three lists of 16 arsenals and win / loss counts per stage."},
    {"Battle for Reality skill pack", "Revised skills 1-374 and new skills 375-465, switched in the multiplayer Lab."},
    {"Edgar's Dream Summit", "The top-of-the-stairs arena as its own stage."},
    {"Added maps", "Grand Panorama, Grand Panorama (Sunset), Sky Ring Highway, Highway to the Alley, Jungle Cave, Five-Story Building, Ruins, Antarctic Outpost, RoofTop Day, Edgar's Dream: Summit and the Preview Stage, in both stage lists (online too); split screen: one row per stage family, Left / Right picks the version.", 2},   // v5.185.2
    {"New skills", "Mirror, Hologram, Spiritual Projection, Thunder Punch, Ice Punch, Fiery Tornado, Mega Photon Burst and Giant Meteor - every player of the pack has them, so they work online; the Mirror, Hologram and Spiritual Projection doubles fight offline only.", 2},   // v5.186.0 (5.185.2 said offline - wrong: pack skills are allowed online)
    {"Skills you make stay offline", "Skills from Create a Skill or the Skill Fuser are for story, split screen and Single Play; online they play as Aura Particles. Saved Create a Stage maps are not used online either."},   // v5.186.0
    {"Community skills", "FEEDBACK > Category: Skill to share sends a skill you made to the pack's author. Skills the author adds come to every player in a pack update and work online."},
    {"Downed Glow", "A glow on every downed fighter until revived (online too: the host switches it in the lobby's BFR Options)."},
    {"60 FPS display", "Fighters, effects and projectiles drawn at 60 FPS (CwoFps.txt: 60 or off)."},
    {"Spectator and CPU levels", "Watch CPU matches; CPU personalities and levels."},
    {"Skill Fuser", "Fuse two close-range skills into one with both effects."},
    {"The Illusion school", "A sixth school: 75 skills (753-827) with their own mark, smoke-silver effects and aura. Offline.", 1},
    {"Skill Vault", "Lab > View Skill Vault: a skill you don't own takes the copies of one you own that is in none of your arsenals."},   // v5.180.1 in BFR too
    {"FEEDBACK and To-Do List", "Main menu FEEDBACK: send a bug, suggestion or idea to the pack's author, and read what is planned."},   // v5.180.1 in BFR too
    {"Save backup", "Your own saved game (arsenals, ratings, story, wins / losses): X here backs it up before the next start, to desktop > Phantom Dust save backups (the newest 3 are kept). BACKUP MY SAVE.bat does it too."},   // v5.188.10
    {"Pack check-in", "Once per install and after each pack update: your gamertag and the pack version go to the pack's author, so the pack's author can see who plays the pack."},   // v5.180.1 in BFR too
    {"Crash shield", "An effect drawn after its texture was freed no longer closes the game (offline)."},   // v5.180.1 in BFR too
    {"Compact stage list", "One row per stage family in the split-screen STAGE LIST; D-pad left / right for its versions.", 1},   // v5.186.1 BFR: in its Added maps row
    {"Train a CPU (new, test)", "LB here: record your offline matches (CwoTrain folder). RB here: a CPU replays your newest recording, then fights on its own once the match goes differently.", 0},   // v5.188.54
    {"Own idle (new, test)", "Every fighter's long idle is our own: breathing, a weight shift, a look around. Stand still to see it.", 1},   // v5.188.54
    {"Day and night (new, test)", "PAUSE MENU row Day and Night (A = on / off, at once) or L3 here: in offline matches the stage's light runs from day to sunset, dusk and back (4 minutes a day); the sky picture stays as it is.", 1},   // v5.188.58
    {"Poses (new, test)", "In an offline match, hold RB and press the D-pad while you stand: Up your intro pose, Right your second intro, Left your third intro, Down your victory pose - the game's own poses of your fighter.", 0},   // v5.199.1 BFR too
    {"Ultrawide (new, test)", "On a screen wider than 16:9 (21:9, 32:9) the 3D view fills the whole screen in offline play; the HUD and menus stay in the middle. Put off in CwoUltrawide.txt to turn it off.", 0},   // v5.189.0
    {"Campaign rules (new, test)", "CwoCampaign.txt in the loader folder: after A B open Q, lock Q until A, open Q, phase P, chapter C - your own campaign order and jumps on your save. Offline; shows when an area loads.", 1},   // v5.190.0
    {"Visual themes (new, test)", "PAUSE MENU row Theme (A = next): HD Remaster, Anime / Cel, Neon Ruins, Overgrown, Frozen Earth or OFF - the stage on screen changes at once. R3 here picks it before a match. Edgar's outfit 1 from the next load. Textures only, offline.", 0},   // v5.191.0
    {"Effect Lab (new)", "Lab > Effect Lab (Split Screen, Xbox Live menu or the mission terminal): pick a stage and play any skill or any of the game's effects on a target - parts and frames, motions, wind, slow motion and frame step, camera mode. Offline.", 0},   // v5.199.0
    {"Texture Size Test (new, test)", "Stage list: three floors with the same picture at 512, 1024 and 2048 - do the bigger textures draw sharper?", 1},   // v5.188.54
};
static const int ML_ROWS = 11, ML_W = 470, ML_CHARS = 50;    // v5.170.4 11 rows: the legend sits above the game's own notice box ("Skill Update ... applied")
// g_ml_open: cwo_loader.cpp
static int g_ml_scroll;
static const int ML_MAXL = 220;                                   // v5.185.2 (was 160: the list had 122 lines)
static char g_ml_lines[ML_MAXL][ML_CHARS + 4];
static unsigned char g_ml_head[ML_MAXL];                               // 1 = a mod's name line
static int g_ml_n;
static char g_ml_file_names[64][48], g_ml_file_what[64][200], g_ml_file_by[64][120];

static void ml_add(const char *t, bool head) {
    if (g_ml_n >= ML_MAXL) return;
    strncpy_s(g_ml_lines[g_ml_n], t, _TRUNCATE);
    g_ml_head[g_ml_n++] = head ? 1 : 0;
}
static void ml_wrap(const char *t) {                               // words onto lines of at most ML_CHARS - 2 (indented 2)
    char line[ML_CHARS + 4] = "  ";
    int len = 2;
    const char *p = t;
    while (*p) {
        while (*p == ' ') p++;
        const char *e = p;
        while (*e && *e != ' ') e++;
        int wl = (int)(e - p);
        if (!wl) break;
        if (len + (len > 2 ? 1 : 0) + wl > ML_CHARS - 2 && len > 2) { ml_add(line, false); strcpy_s(line, "  "); len = 2; }
        if (len > 2) line[len++] = ' ';
        for (int i = 0; i < wl && len < ML_CHARS; i++) line[len++] = p[i];
        line[len] = 0;
        p = e;
    }
    if (len > 2) ml_add(line, false);
}
static void ml_build() {
    g_ml_n = 0;
    int nf = 0;
    wchar_t path[MAX_PATH];
    swprintf_s(path, MAX_PATH, L"%sCwoModList.txt", g_dir);
    FILE *f = NULL;
    if (cwo_wfopen(&f, path, L"r") == 0 && f) {
        char line[300];
        while (fgets(line, sizeof(line), f) && nf < 64) {
            char *bar = strchr(line, '|');
            if (line[0] == '#' || !bar) continue;
            *bar = 0;
            for (char *c = bar + 1; *c; c++) if (*c == '\r' || *c == '\n') *c = 0;
            char *bar2 = strchr(bar + 1, '|');                  // v5.120 optional third field: who asked for the mod
            if (bar2) *bar2 = 0;
            strncpy_s(g_ml_file_names[nf], line, _TRUNCATE);
            strncpy_s(g_ml_file_what[nf], bar + 1, _TRUNCATE);
            strncpy_s(g_ml_file_by[nf], bar2 ? bar2 + 1 : "", _TRUNCATE);
            nf++;
        }
        fclose(f);
    }
    // v5.86 What's New first (it used to pop up on the main menu)
    for (int i = 0; i < th_news_n(); i++) { const char *t = th_news()[i]; if (t[0] == '-' || t[0] == ' ') ml_wrap(t[0] == '-' ? t + 2 : t + 2); else ml_add(t, true); }
    // v5.120 COMMUNITY SHOUT-OUT (owner 2026-11-09: a shout-out to the Facebook LFG Messenger group's active players and to
    // the players who requested the mods): CwoShoutOut.txt in the loader folder, "Name|why" lines (# = comment); without
    // it the built-in thanks below (the owner fills in the names later)
    ml_add("COMMUNITY SHOUT-OUT", true);
    {
        wchar_t sp[MAX_PATH];
        swprintf_s(sp, MAX_PATH, L"%sCwoShoutOut.txt", g_dir);
        FILE *sf = NULL;
        int ns = 0;
        if (cwo_wfopen(&sf, sp, L"r") == 0 && sf) {
            char sl[300];
            while (fgets(sl, sizeof(sl), sf)) {
                for (char *c = sl; *c; c++) if (*c == '\r' || *c == '\n') *c = 0;
                if (sl[0] == '#' || !sl[0]) continue;
                char *sb = strchr(sl, '|');
                if (sb) *sb = 0;
                ml_add(sl, true);
                if (sb && sb[1]) ml_wrap(sb + 1);
                ns++;
            }
            fclose(sf);
        }
        if (!ns) {
            ml_add("Phantom Dust Facebook community", true);
            ml_wrap("Thank you to the Phantom Dust LFG Messenger group and every active player who keeps the game alive online.");
            ml_add("Mod requests", true);
            ml_wrap("Thank you to the players who asked for the mods in this pack. Each mod below names who asked for it.");
        }
    }
    ml_add("EVERY MOD", true);
    if (nf) for (int i = 0; i < nf; i++) {
        ml_add(g_ml_file_names[i], true); ml_wrap(g_ml_file_what[i]);
        if (g_ml_file_by[i][0]) { char by[160]; sprintf_s(by, "Requested by: %s", g_ml_file_by[i]); ml_wrap(by); }
    }
    else for (const MlMod &m : ML_BUILTIN) { if (m.ed == (g_bfr_edition ? 1 : 2)) continue; ml_add(m.name, true); ml_wrap(m.what); }   // v5.170 per edition
}
// v5.188.10 (owner 2026-11-16): X asks the launcher to back up
// the player's own save before the next start (the game cannot write to the desktop); the last result is shown on opening
static char g_ml_said[120];
static ULONGLONG g_ml_said_t;
static void ml_said(const char *t) { strncpy_s(g_ml_said, t, _TRUNCATE); g_ml_said_t = GetTickCount64(); }
static void ml_open() {                                            // native 2.8(95), game thread
    ml_build();
    {
        wchar_t p[MAX_PATH]; FILE *f = NULL; char l[96] = "";
        swprintf_s(p, MAX_PATH, L"%sCwoSaveBackupResult.txt", g_dir);
        if (cwo_wfopen(&f, p, L"r") == 0 && f) { if (!fgets(l, sizeof(l), f)) l[0] = 0; fclose(f); }
        for (int i = (int)strlen(l) - 1; i >= 0 && (l[i] == '\n' || l[i] == '\r' || l[i] == ' '); i--) l[i] = 0;
        swprintf_s(p, MAX_PATH, L"%sCwoSaveBackupRequest.txt", g_dir);
        if (GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES) ml_said("Backup asked for: made at your next start.");
        else if (l[0]) { char m[120]; sprintf_s(m, "Last backup: %.37s", l); ml_said(m); }
        else g_ml_said[0] = 0;
    }
    g_ml_scroll = 0;
    InterlockedExchange(&g_ml_open, 1);
    logf("mod list: opened (%d lines)", g_ml_n);
}
static void ml_input(WORD pressed) {                              // pad thread while the list shows
    int maxs = g_ml_n - ML_ROWS; if (maxs < 0) maxs = 0;
    if (pressed & BTN_UP) g_ml_scroll = g_ml_scroll > 0 ? g_ml_scroll - 1 : maxs;      // v5.120 the list loops
    if (pressed & BTN_DOWN) g_ml_scroll = g_ml_scroll < maxs ? g_ml_scroll + 1 : 0;
    if (pressed & BTN_X) {
        wchar_t p[MAX_PATH]; FILE *f = NULL;
        swprintf_s(p, MAX_PATH, L"%sCwoSaveBackupRequest.txt", g_dir);
        if (cwo_wfopen(&f, p, L"w") == 0 && f) {
            fputs("back up the player's save before the next start\n", f); fclose(f);
            ml_said("Your save is backed up at your next start."); logf("mod list: save backup asked for"); rumble(1);
        } else ml_said("The backup could not be asked for.");
    }
    if (pressed & BTN_Y) { dc_open_invite(); ml_said("Discord invite: discord.gg/hSAER5cNMu"); rumble(1); }   // v5.188.49 Join our Discord
    if (!g_bfr_edition && (pressed & BTN_LTHUMB)) {               // v5.188.58 day and night switch (v5.200.0 also the pause menu row) (cwo_daynight.inl)
        wchar_t p[MAX_PATH];
        swprintf_s(p, MAX_PATH, L"%sCwoDayNight.txt", g_dir);
        if (GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES) { DeleteFileW(p); ml_said("Day and night: OFF."); }
        else { FILE *f = NULL; if (cwo_wfopen(&f, p, L"w") == 0 && f) { fputs("cycle 240\n", f); fclose(f); } ml_said("Day and night: ON for offline matches."); }
        rumble(1);
    }
    if ((pressed & BTN_RTHUMB)) {                                // v5.192.0 visual themes (v5.199.0 BFR too): R3 = the next theme (cwo_stagetheme.inl)
        char m[80]; sprintf_s(m, "Theme: %s (from the next stage load)", st_name(st_next()));
        ml_said(m); rumble(1);
    }
    if ((pressed & (BTN_LB | BTN_RB))) {                             // v5.188.54 Train a CPU switches (v5.199.0 BFR too): the files the
        bool rec = (pressed & BTN_LB) != 0;                           // recorder / replay read (cwo_trainrec / cwo_trainplay)
        wchar_t p[MAX_PATH];
        swprintf_s(p, MAX_PATH, rec ? L"%sCwoTrainRecord.txt" : L"%sCwoTrainPlay.txt", g_dir);
        if (GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES) {
            DeleteFileW(p);
            ml_said(rec ? "Train a CPU: recording OFF." : "Train a CPU: replay OFF - CPUs fight on their own.");
        } else {
            FILE *f = NULL;
            if (cwo_wfopen(&f, p, L"w") == 0 && f) { fputs(rec ? "on\n" : "latest\n", f); fclose(f); }
            ml_said(rec ? "Train a CPU: recording ON for offline matches." : "Train a CPU: replay ON - a CPU plays your newest recording.");
        }
        logf("mod list: Train a CPU %s switched", rec ? "recording" : "replay");
        rumble(1);
    }
    if (pressed & (BTN_A | BTN_B)) { InterlockedExchange(&g_ml_open, 0); InterlockedExchange(&g_ed_game_hold, 1); rumble(1); }   // v5.86 its A / B stay here
}
static void ml_draw() {
    if (!g_ml_open || g_in_match) { if (g_in_match) g_ml_open = 0; return; }
    int glyphs = *(int *)(g_base + RVA_GLYPHS), cap = *(int *)(g_base + RVA_GLYPH_CAP);
    if (glyphs > cap - 700) return;
    if (*(int *)(g_base + RVA_OFS_X) != 0 || *(int *)(g_base + RVA_OFS_Y) != 0) return;
    int *space = (int *)(g_base + RVA_FONT_SPACE), *gap = (int *)(g_base + RVA_FONT_GAP);
    int *fh = (int *)(g_base + RVA_FONT_H), *fw = (int *)(g_base + RVA_FONT_W), *x0 = (int *)(g_base + RVA_FONT_X0);
    DWORD *rgba = (DWORD *)(g_base + RVA_FONT_RGBA);
    int s_space = *space, s_gap = *gap, s_h = *fh, s_w = *fw, s_x0 = *x0;
    DWORD s_rgba[4];
    memcpy(s_rgba, rgba, sizeof(s_rgba));
    PFN_TEXT text = (PFN_TEXT)(g_base + RVA_TEXT);
    DWORD accent = g_th_on >= 0 ? th_accent() : 0xFF80E0D0;
    auto box = [&](int px, int py, DWORD col, int w, int h) { *space = 0; *gap = 0; *fh = h; *fw = w; set_rgba(rgba, col); text(px, py, "\x81\xa1"); };
    auto put = [&](int px, int py, const char *s, DWORD col) { *space = 0; *gap = 2; *fh = 14; *fw = 7; set_rgba(rgba, 0xFF000000); text(px + 1, py + 1, s); set_rgba(rgba, col); text(px, py, s); };
    const int LH = 19, X = (640 - ML_W) / 2, Y = 70, H = 34 + LH * ML_ROWS + 30;
    // v5.170.1 (owner 2026-11-13; in game the main menu's buttons and
    // help line showed beside the window): the panel covers the whole 4:3 picture from its top to just above the button
    // legend. The box glyph draws twice its width setting, so 48-wide steps tile it
    // v5.170.3 down to y 364 only: the game draws its menu help line (y ~370-384) after this panel, so it shows below it, clear
    const int BOT = 364;
    for (int by = Y - 10; by < BOT; by += 48)
        for (int bx = 0; bx < 640; bx += 48) box(bx, by, 0xFF061614, 48, by + 64 > BOT ? BOT - by : 64);
    int maxs = g_ml_n - ML_ROWS; if (maxs < 0) maxs = 0;
    put(X, Y, "MOD LIST - BATTLE FOR REALITY / PHANTOM DUST MODS", accent);
    for (int r = 0; r < ML_ROWS && g_ml_scroll + r < g_ml_n; r++)
        put(X, Y + 28 + LH * r, g_ml_lines[g_ml_scroll + r], g_ml_head[g_ml_scroll + r] ? accent : 0xFFFFFFFF);
    int bar_h = LH * ML_ROWS, knob = maxs ? bar_h * ML_ROWS / g_ml_n : bar_h, ky = maxs ? (bar_h - knob) * g_ml_scroll / maxs : 0;
    box(X + ML_W - 14, Y + 28, 0xFF2A3A3A, 6, bar_h);
    box(X + ML_W - 14, Y + 28 + ky, accent, 6, knob);
    if (g_ml_said[0] && GetTickCount64() - g_ml_said_t < 9000) put(X, Y + 34 + LH * ML_ROWS, g_ml_said, 0xFFFFE070);
    else put(X, Y + 34 + LH * ML_ROWS, "Up/Down  X backup  Y Discord  LB/RB Train  A/B close", 0xFFC8C8C8);
    memcpy(rgba, s_rgba, sizeof(s_rgba));
    *space = s_space; *gap = s_gap; *fh = s_h; *fw = s_w; *x0 = s_x0;
}
static int ml_self_test() {                                       // every line fits the window
    ml_build();
    for (int i = 0; i < g_ml_n; i++) if ((int)strlen(g_ml_lines[i]) > ML_CHARS) return -81;
    logf("self-test: mod list %d lines ok", g_ml_n);
    return g_ml_n > 20 ? 1 : -82;
}
