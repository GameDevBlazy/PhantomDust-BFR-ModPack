// ---- v5.188.8 BFR pack resync (owner 2026-11-16).
// Independently authored from official Windows documentation only (FindFirstFileW / MoveFileExW; CNG SHA-256 and the ECDSA
// check through cwo_update.inl). Runs in the BFR launcher at every start, before the game (also after an update):
//  1. the pack folder: MANIFEST.json must carry the pack author's signature (MANIFEST.sig) and every listed file its SHA-256.
//     Anything changed or missing is downloaded again from the pack's GitHub (signed manifest, every file checked; the old
//     files go to <pack>\_resync_<date>\). If that cannot be done (offline), the game is NOT started with changed pack files.
//     Files in <pack>\launcher\ that the pack does not list (a planted DLL ...) are moved to the same folder.
//  2. the data folder (AC\Temp\CwoLoaderBFR\) the loader reads:
//     - data\patches\: exactly the pack's patches (changed ones put back, extra ones moved out);
//     - the pack's own game files (data\game\...) and the LOCKED files (skill pack, community skills, stage effects / sounds,
//       effect sizes, low gravity, body shape, HUD faces, shout-outs, feedback form, boss raid, six fighters, markers): put
//       back when changed;
//     - game\ (every file there replaces a game file): only the pack's game files, the files its patches build (the loader
//       checks those against the patch's own hash and rebuilds them) and the files the loader itself makes (fighters' shaped
//       / outfit models under Assets\Data\Player\<fighter>\, maplight0.ssb, com\single2.csv, Sound\common.bin). Anything
//       else - a mod from another program, an old stage - is moved to CwoLoaderBFR_not_in_pack_<date>\ (kept, not deleted).
//     The player's own settings and creations (records, arsenals, outfits, modes, HUD, created skills / fusions, Forge maps,
//     logs) are never touched.
//  3. the messages say what happened; then the game starts with the pack's files.
static const char *RS_LOCKED[] = {"CwoBFR.txt", "CwoCommunitySkills.txt", "CwoRemovedSkills.txt",
                                  "CwoFxSize.txt", "CwoStageFx.txt",
                                  "CwoStageSound.txt", "CwoLowGravity.txt", "CwoBodyShape.txt", "CwoFaces.txt", "CwoShoutOut.txt",
                                  "CwoFeedbackForm.txt", "CwoBossRaid.txt", "CwoSixFighters.txt", "markers_st08.csv"};

static std::string rs_low(std::string s) { for (auto &c : s) if (c >= 'A' && c <= 'Z') c = (char)(c + 32); return s; }
static std::string rs_narrow(const std::wstring &w) { std::string s; for (wchar_t c : w) s += c < 128 ? (char)c : '?'; return s; }
static std::wstring rs_bs(std::wstring s) { for (auto &c : s) if (c == L'/') c = L'\\'; return s; }
static std::wstring rs_stamp() {
    SYSTEMTIME t; GetLocalTime(&t);
    wchar_t s[32]; swprintf_s(s, 32, L"%04d-%02d-%02d_%02d%02d%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    return s;
}
static bool rs_hash_is(const std::wstring &path, const std::string &want) {
    std::vector<unsigned char> b;
    return up_read(path, b) && up_sha_hex(b) == want;
}
// every file under dir (relative paths with '/', as the manifest writes them)
static void rs_walk(const std::wstring &dir, const std::wstring &rel, std::vector<std::wstring> &out, int depth = 0) {
    if (depth > 12) return;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + rel + L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        std::wstring r = rel + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) rs_walk(dir, r + L"\\", out, depth + 1);
        else { std::wstring q = r; for (auto &c : q) if (c == L'\\') c = L'/'; out.push_back(q); }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
// moves base+rel to aside+rel (folders made); kept, never deleted
static bool rs_aside(const std::wstring &base, const std::wstring &rel, const std::wstring &aside) {
    std::wstring from = base + rs_bs(rel), to = aside + rs_bs(rel);
    for (size_t i = 3; i < to.size(); i++) if (to[i] == L'\\') CreateDirectoryW(to.substr(0, i).c_str(), NULL);
    return MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) != 0;
}
// the files the loader itself makes in game\ (relative to game\, lower case)
static bool rs_loader_made(const std::string &g) {
    if (g == "assets/data/map/maplight0.ssb" || g == "assets/data/com/single2.csv" || g == "assets/data/sound/common.bin") return true;
    // assets/data/player/<pc|enm>NN<x>/<same><digits>.alr: shaped bodies (cwo_bodyshape), outfits (cwo_outfit / outfits15)
    const char *P = "assets/data/player/";
    if (g.compare(0, strlen(P), P)) return false;
    std::string r = g.substr(strlen(P));
    size_t sl = r.find('/');
    if (sl == std::string::npos || r.find('/', sl + 1) != std::string::npos) return false;
    std::string stem = r.substr(0, sl), f = r.substr(sl + 1);
    bool ok = (stem.size() == 5 && stem[0] == 'p' && stem[1] == 'c' && isdigit((unsigned char)stem[2]) && isdigit((unsigned char)stem[3])) ||
              (stem.size() == 6 && !stem.compare(0, 3, "enm") && isdigit((unsigned char)stem[3]) && isdigit((unsigned char)stem[4]));
    if (!ok || f.size() < stem.size() + 4 || f.compare(0, stem.size(), stem) || f.compare(f.size() - 4, 4, ".alr")) return false;
    for (size_t i = stem.size(); i + 4 < f.size(); i++) if (!isdigit((unsigned char)f[i])) return false;
    return true;
}

// 1b. the pack files that differ from the newest signed manifest, downloaded again; false = could not
static bool rs_repair_pack(const std::wstring &P, bool *launcher_changed) {
    std::wstring commit = L"local";
    if (g_up_source.empty()) {
        std::vector<unsigned char> b;
        std::wstring path = L"/repos/" + std::wstring(UP_OWNER) + L"/" + UP_REPO + L"/commits/" + UP_BRANCH;
        if (!up_get(L"api.github.com", path, L"application/vnd.github.sha", b) || b.size() < 40) return false;
        commit = up_w(std::string(b.begin(), b.begin() + 40));
    }
    std::vector<unsigned char> rm, rsg;
    if (!up_fetch(commit, "MANIFEST.json", rm) || !up_fetch(commit, "MANIFEST.sig", rsg) || !up_verify(rm, rsg)) return false;
    std::map<std::string, std::string> rf;
    std::string rj(rm.begin(), rm.end());
    if (!up_json_files(rj, rf)) return false;
    std::wstring bk = P + L"_resync_" + rs_stamp() + L"\\";
    int n = 0;
    for (auto &f : rf) {
        std::wstring pf = P + rs_bs(up_w(f.first));
        if (rs_hash_is(pf, f.second)) continue;
        std::vector<unsigned char> b;
        if (!up_fetch(commit, f.first, b) || up_sha_hex(b) != f.second) { say(L"resync: %S did not download correctly", f.first.c_str()); return false; }
        if (GetFileAttributesW(pf.c_str()) != INVALID_FILE_ATTRIBUTES && !rs_aside(P, up_w(f.first), bk)) { say(L"resync: cannot move %s aside", pf.c_str()); return false; }
        if (!up_write(pf, b)) { say(L"resync: cannot write %s", pf.c_str()); return false; }
        if (!_stricmp(f.first.c_str(), "launcher/CwoLaunch.exe")) *launcher_changed = true;
        n++;
    }
    up_write(P + L"MANIFEST.json", rm);
    up_write(P + L"MANIFEST.sig", rsg);
    std::string ver = up_json_str(rj, "version") + "\n";
    up_write(P + L"VERSION", std::vector<unsigned char>(ver.begin(), ver.end()));
    say(L"resync: %d pack file(s) downloaded again from the pack's page (old ones in %s)", n, bk.c_str());
    return true;
}

// returns 1 = start the game, 0 = do not start it, 2 = the launcher itself was replaced (start the new one)
static int bfr_resync(const wchar_t *pack, const wchar_t *data) {
    std::wstring P(pack), D(data);
    // 1. the pack folder
    std::vector<unsigned char> lm, ls;
    up_read(P + L"MANIFEST.json", lm);
    up_read(P + L"MANIFEST.sig", ls);
    std::map<std::string, std::string> lf;
    bool signed_ok = !lm.empty() && up_verify(lm, ls) && up_json_files(std::string(lm.begin(), lm.end()), lf);
    int bad = 0;
    if (signed_ok) for (auto &f : lf) if (!rs_hash_is(P + rs_bs(up_w(f.first)), f.second)) bad++;
    std::wstring lq = P + L"_resync_" + rs_stamp() + L"\\";
    {   // files in launcher\ the pack does not list
        std::vector<std::wstring> lfs;
        rs_walk(P, L"launcher\\", lfs);
        int moved = 0;
        for (auto &w : lfs) {
            std::string k = rs_narrow(w), kl = rs_low(k);
            if (kl == "launcher/cwolaunch.log" || kl == "launcher/edition.txt") continue;
            bool listed = false;
            for (auto &f : lf) if (rs_low(f.first) == kl) { listed = true; break; }
            if (!listed && rs_aside(P, w, lq)) moved++;
        }
        if (moved) say(L"resync: %d file(s) in the launcher folder are not part of the pack - moved to %s", moved, lq.c_str());
    }
    if (!signed_ok || bad) {
        say(L"");
        say(L"Your BFR pack files don't match the BFR mod pack (%s). Resynchronizing ...", signed_ok ? L"changed files" : L"the pack's list is not the author's");
        bool lc = false;
        if (!rs_repair_pack(P, &lc)) {
            say(L"Resync could not download the pack's files (offline?). Phantom Dust is NOT started with changed pack files.");
            say(L"Connect to the internet and start again, or download the pack again and run INSTALL BFR.bat.");
            Sleep(8000);
            return 0;
        }
        if (lc) return 2;
        lm.clear(); ls.clear(); lf.clear();
        up_read(P + L"MANIFEST.json", lm);
        up_json_files(std::string(lm.begin(), lm.end()), lf);
    }
    // 2. the data folder
    std::wstring aside = D.substr(0, D.size() - 1) + L"_not_in_pack_" + rs_stamp() + L"\\";
    int put_back = 0, moved = 0;
    auto restore = [&](const std::string &mrel, const std::string &want) {      // pack file data/<x> -> D<x>
        std::wstring rel = up_w(mrel.substr(5)), dst = D + rs_bs(rel);
        if (rs_hash_is(dst, want)) return;
        std::vector<unsigned char> b;
        if (!up_read(P + rs_bs(up_w(mrel)), b)) return;
        if (GetFileAttributesW(dst.c_str()) != INVALID_FILE_ATTRIBUTES) rs_aside(D, rel, aside);
        if (up_write(dst, b)) { put_back++; say(L"resync: put back %s", rel.c_str()); }
    };
    std::map<std::string, std::string> keep_game;                     // lower-case paths under game\ that may stay
    std::map<std::string, bool> keep_patch;
    for (auto &f : lf) {
        if (f.first.compare(0, 5, "data/")) continue;
        std::string r = f.first.substr(5), rl = rs_low(r);
        if (!rl.compare(0, 8, "patches/")) {
            keep_patch[rl] = true;
            restore(f.first, f.second);
            if (rl.size() > 17 && !rl.compare(rl.size() - 9, 9, ".cwopatch") && rl.compare(8, 6, "_data/")) keep_game[rl.substr(8, rl.size() - 17)] = "patch";
        } else if (!rl.compare(0, 5, "game/")) {
            keep_game[rl.substr(5)] = "pack";
            restore(f.first, f.second);
        } else {
            for (const char *L : RS_LOCKED) if (rl == rs_low(L)) { restore(f.first, f.second); break; }
        }
    }
    std::vector<std::wstring> found;
    rs_walk(D, L"patches\\", found);
    for (auto &w : found) if (!keep_patch.count(rs_low(rs_narrow(w))) && rs_aside(D, w, aside)) { moved++; say(L"resync: not in the pack, moved out: %s", w.c_str()); }
    found.clear();
    rs_walk(D, L"game\\", found);
    for (auto &w : found) {
        std::string g = rs_low(rs_narrow(w)).substr(5);
        if (keep_game.count(g) || rs_loader_made(g)) continue;
        if (rs_aside(D, w, aside)) { moved++; say(L"resync: not in the pack, moved out: %s", w.c_str()); }
    }
    if (put_back || moved) {
        say(L"");
        say(L"Your files didn't match the BFR mod pack: %d put back, %d that are not part of the pack moved to", put_back, moved);
        say(L"  %s (kept, no longer loaded).", aside.c_str());
        say(L"Resynchronized - starting Phantom Dust with the pack's files.");
        Sleep(3000);
    } else say(L"BFR files checked: they match the pack");
    return 1;
}

// offline test (no network, no game): a stand-in pack + data folder; rules only
static bool rs_selftest() {
    bool a = rs_loader_made("assets/data/player/pc02a/pc02a3.alr") && rs_loader_made("assets/data/player/enm05a/enm05a.alr") &&
             rs_loader_made("assets/data/map/maplight0.ssb");
    bool b = !rs_loader_made("assets/data/player/pc02a/other.alr") && !rs_loader_made("assets/data/map/mx305.alr") &&
             !rs_loader_made("assets/data/player/pc02a/pc03a1.alr") && !rs_loader_made("assets/data/player/pc02a/x/pc02a1.alr");
    return a && b;
}
