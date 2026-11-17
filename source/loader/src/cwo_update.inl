// ---- v5.153 the BFR pack updater (owner 2026-11-12; "each update is signed with a key only I hold, and the launcher refuses anything unsigned. That
// way, nobody can push code to my players through a hacked GitHub account"; AGENTS.md "Auto-updater").
// Independently authored from official Windows documentation only: WinHTTP (WinHttpOpen / Connect / OpenRequest /
// SendRequest / ReceiveResponse / QueryHeaders / ReadData), CNG (BCryptHash SHA-256, BCryptImportKeyPair +
// BCryptVerifySignature with the ECDSA P-256 public key of cwo_update_key.h), MoveFileExW / CopyFileW.
// When the BFR launcher starts (before the game):
//  1. the newest commit of GameDevBlazy/PhantomDust-BFR-ModPack main (api.github.com, one request);
//  2. MANIFEST.json + MANIFEST.sig of that commit (raw.githubusercontent.com/<owner>/<repo>/<commit>/...); the signature
//     must verify with the owner's public key, else nothing is used ("refused: not signed by the pack's author");
//  3. a higher version: every listed file whose SHA-256 differs is downloaded into <pack>\_update\ and checked against the
//     signed manifest; one bad file = no update;
//  4. applied: the old pack files go to <pack>\_backup_<old version>\, the new ones in; under data\ the player's copy in the
//     BFR data folder is replaced only when it is still the old pack's (the player did not change it) - else it is kept;
//     CwoNewSkills.txt is merged (the pack's skills + the player's own); data\patches\ always (the loader rebuilds from them);
//  5. offline, a refusal or any error: the game starts with the version that is there.
// Tests while the repository is private (owner's PC only, never shipped): <BFR data folder>\CwoUpdate.txt with
// "source <folder>" (a local copy of the repository) or "token <read-only access token>". No stored login is ever read.
#include <winhttp.h>
#include <bcrypt.h>
#include <string>
#include <vector>
#include <map>
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")
#include "cwo_update_key.h"

static const wchar_t *UP_OWNER = L"GameDevBlazy", *UP_REPO = L"PhantomDust-BFR-ModPack", *UP_BRANCH = L"main";
static std::wstring g_up_token, g_up_source;                     // test switches (CwoUpdate.txt)

static std::string up_sha_hex(const std::vector<unsigned char> &d) {
    unsigned char h[32];
    if (BCryptHash(BCRYPT_SHA256_ALG_HANDLE, NULL, 0, (PUCHAR)d.data(), (ULONG)d.size(), h, 32)) return "";
    char t[65];
    for (int i = 0; i < 32; i++) sprintf_s(t + 2 * i, 3, "%02x", h[i]);
    return std::string(t, 64);
}
static bool up_read(const std::wstring &p, std::vector<unsigned char> &out) {
    out.clear();
    FILE *f = NULL;
    if (_wfopen_s(&f, p.c_str(), L"rb") || !f) return false;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n > 0) { out.resize(n); if (fread(out.data(), 1, n, f) != (size_t)n) { fclose(f); out.clear(); return false; } }
    fclose(f);
    return true;
}
static bool up_write(const std::wstring &p, const std::vector<unsigned char> &d) {
    for (size_t i = 3; i < p.size(); i++) if (p[i] == L'\\') CreateDirectoryW(p.substr(0, i).c_str(), NULL);
    FILE *f = NULL;
    if (_wfopen_s(&f, p.c_str(), L"wb") || !f) return false;
    bool ok = d.empty() || fwrite(d.data(), 1, d.size(), f) == d.size();
    fclose(f);
    return ok;
}
static std::wstring up_w(const std::string &s) { return std::wstring(s.begin(), s.end()); }   // ASCII paths of the manifest
static std::wstring up_url_path(const std::string &rel) {        // percent-encode everything but unreserved and '/'
    std::wstring o;
    for (unsigned char c : rel) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/') o += (wchar_t)c;
        else { wchar_t t[4]; swprintf_s(t, 4, L"%%%02X", c); o += t; }
    }
    return o;
}
// one HTTPS GET; 200 -> true and the body
static bool up_get(const wchar_t *host, const std::wstring &path, const wchar_t *accept, std::vector<unsigned char> &body) {
    body.clear();
    bool ok = false;
    HINTERNET s = WinHttpOpen(L"CwoLaunch-BFR-Updater/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) return false;
    WinHttpSetTimeouts(s, 5000, 5000, 15000, 30000);
    HINTERNET c = WinHttpConnect(s, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : NULL;
    if (r) {
        std::wstring hdr;
        if (accept) { hdr += L"Accept: "; hdr += accept; hdr += L"\r\n"; }
        if (!g_up_token.empty()) { hdr += L"Authorization: token "; hdr += g_up_token; hdr += L"\r\n"; }
        if (WinHttpSendRequest(r, hdr.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : hdr.c_str(), hdr.empty() ? 0 : (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(r, NULL)) {
            DWORD code = 0, len = sizeof(code);
            WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &len, WINHTTP_NO_HEADER_INDEX);
            if (code == 200) {
                ok = true;
                for (;;) {
                    DWORD avail = 0;
                    if (!WinHttpQueryDataAvailable(r, &avail)) { ok = false; break; }
                    if (!avail) break;
                    size_t at = body.size();
                    body.resize(at + avail);
                    DWORD got = 0;
                    if (!WinHttpReadData(r, body.data() + at, avail, &got)) { ok = false; break; }
                    body.resize(at + got);
                    if (body.size() > 200u * 1024 * 1024) { ok = false; break; }
                }
            }
        }
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return ok;
}
// a file of the newest version: the local test copy, else raw.githubusercontent.com at that commit
static bool up_fetch(const std::wstring &commit, const std::string &rel, std::vector<unsigned char> &body) {
    if (!g_up_source.empty()) {
        std::wstring p = g_up_source + L"\\" + up_w(rel);
        for (auto &ch : p) if (ch == L'/') ch = L'\\';
        return up_read(p, body);
    }
    std::wstring path = L"/" + std::wstring(UP_OWNER) + L"/" + UP_REPO + L"/" + commit + L"/" + up_url_path(rel);
    return up_get(L"raw.githubusercontent.com", path, NULL, body);
}
static bool up_verify(const std::vector<unsigned char> &data, const std::vector<unsigned char> &sighex) {
    if (sighex.size() < 128 || data.empty()) return false;
    unsigned char sig[64], h[32];
    for (int i = 0; i < 64; i++) {
        char t[3] = {(char)sighex[2 * i], (char)sighex[2 * i + 1], 0};
        if (!isxdigit((unsigned char)t[0]) || !isxdigit((unsigned char)t[1])) return false;
        sig[i] = (unsigned char)strtoul(t, NULL, 16);
    }
    if (BCryptHash(BCRYPT_SHA256_ALG_HANDLE, NULL, 0, (PUCHAR)data.data(), (ULONG)data.size(), h, 32)) return false;
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_KEY_HANDLE k = NULL;
    bool ok = false;
    if (!BCryptOpenAlgorithmProvider(&alg, BCRYPT_ECDSA_P256_ALGORITHM, NULL, 0) &&
        !BCryptImportKeyPair(alg, NULL, BCRYPT_ECCPUBLIC_BLOB, &k, (PUCHAR)CWO_UPDATE_PUBKEY, sizeof(CWO_UPDATE_PUBKEY), 0))
        ok = BCryptVerifySignature(k, NULL, h, 32, sig, 64, 0) == 0;
    if (k) BCryptDestroyKey(k);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}
// MANIFEST.json (build_bfr_pack.py, json.dump indent 1): "version": "a.b.c" and "files": {"path": "sha256", ...}
static std::string up_json_str(const std::string &j, const char *key) {
    std::string k = std::string("\"") + key + "\": \"";
    size_t i = j.find(k);
    if (i == std::string::npos) return "";
    i += k.size();
    size_t e = j.find('"', i);
    return e == std::string::npos ? "" : j.substr(i, e - i);
}
static bool up_json_files(const std::string &j, std::map<std::string, std::string> &out) {
    out.clear();
    size_t i = j.find("\"files\": {");
    if (i == std::string::npos) return false;
    i += 10;
    for (;;) {
        size_t a = j.find_first_of("\"}", i);
        if (a == std::string::npos) return false;
        if (j[a] == '}') return !out.empty();
        size_t b = j.find('"', a + 1), c = j.find('"', b + 1), d = c == std::string::npos ? c : j.find('"', c + 1);
        if (b == std::string::npos || c == std::string::npos || d == std::string::npos) return false;
        std::string key = j.substr(a + 1, b - a - 1), val = j.substr(c + 1, d - c - 1);
        if (key.find("..") != std::string::npos || key.find(':') != std::string::npos || key.empty() || key[0] == '/' || key[0] == '\\' ||
            val.size() != 64) return false;                        // a path that leaves the pack folder: the manifest is refused
        out[key] = val;
        i = d + 1;
    }
}
static int up_vcmp(const std::string &a, const std::string &b) {
    int x[3] = {0, 0, 0}, y[3] = {0, 0, 0};
    sscanf_s(a.c_str(), "%d.%d.%d", &x[0], &x[1], &x[2]);
    sscanf_s(b.c_str(), "%d.%d.%d", &y[0], &y[1], &y[2]);
    for (int i = 0; i < 3; i++) if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    return 0;
}
// CwoNewSkills.txt: the pack's lines + the player's own skills (ids the pack does not have)
static std::vector<unsigned char> up_merge_skills(const std::vector<unsigned char> &pack, const std::vector<unsigned char> &mine) {
    std::string p(pack.begin(), pack.end()), m(mine.begin(), mine.end()), out = p;
    std::map<int, bool> have;
    size_t i = 0;
    while (i < p.size()) {
        size_t e = p.find('\n', i); if (e == std::string::npos) e = p.size();
        if (p[i] != '#') { int id = atoi(p.c_str() + i); if (id > 0) have[id] = true; }
        i = e + 1;
    }
    if (!out.empty() && out.back() != '\n') out += '\n';
    i = 0;
    while (i < m.size()) {
        size_t e = m.find('\n', i); if (e == std::string::npos) e = m.size();
        std::string line = m.substr(i, e - i);
        if (!line.empty() && line[0] != '#') { int id = atoi(line.c_str()); if (id > 0 && !have[id]) { out += line; if (out.back() != '\n') out += '\n'; } }
        i = e + 1;
    }
    return std::vector<unsigned char>(out.begin(), out.end());
}

// returns 1 updated (the launcher itself may have changed), 0 nothing to do / offline / refused
static int bfr_update(const wchar_t *pack, const wchar_t *data, bool *launcher_changed) {
    *launcher_changed = false;
    std::wstring P(pack), D(data);
    {                                                              // test switches on the owner's PC
        std::vector<unsigned char> t;
        if (up_read(D + L"CwoUpdate.txt", t)) {
            std::string s(t.begin(), t.end());
            size_t i = 0;
            while (i < s.size()) {
                size_t e = s.find('\n', i); if (e == std::string::npos) e = s.size();
                std::string line = s.substr(i, e - i);
                while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
                if (!line.compare(0, 6, "token ")) g_up_token = up_w(line.substr(6));
                if (!line.compare(0, 7, "source ")) g_up_source = up_w(line.substr(7));
                i = e + 1;
            }
            say(L"updater: test switches from CwoUpdate.txt (%s%s)", g_up_source.empty() ? L"" : L"local source", g_up_token.empty() ? L"" : L"access token");
        }
    }
    std::vector<unsigned char> lm;
    up_read(P + L"MANIFEST.json", lm);
    std::string lj(lm.begin(), lm.end()), lver = up_json_str(lj, "version");
    std::map<std::string, std::string> lfiles;
    up_json_files(lj, lfiles);
    say(L"BFR mod pack %S - checking for updates ...", lver.empty() ? "(unknown version)" : lver.c_str());
    std::wstring commit = L"local";
    if (g_up_source.empty()) {
        std::vector<unsigned char> b;
        std::wstring path = L"/repos/" + std::wstring(UP_OWNER) + L"/" + UP_REPO + L"/commits/" + UP_BRANCH;
        if (!up_get(L"api.github.com", path, L"application/vnd.github.sha", b) || b.size() < 40) {
            say(L"updater: no answer from GitHub (offline, or the pack is not public yet) - playing %S", lver.c_str());
            return 0;
        }
        commit = up_w(std::string(b.begin(), b.begin() + 40));
    }
    std::vector<unsigned char> rm, rs;
    if (!up_fetch(commit, "MANIFEST.json", rm) || !up_fetch(commit, "MANIFEST.sig", rs)) { say(L"updater: the newest manifest could not be read - playing %S", lver.c_str()); return 0; }
    if (!up_verify(rm, rs)) { say(L"updater: REFUSED - the newest pack is not signed by the pack's author; playing %S", lver.c_str()); return 0; }
    std::string rj(rm.begin(), rm.end()), rver = up_json_str(rj, "version");
    std::map<std::string, std::string> rfiles;
    if (rver.empty() || !up_json_files(rj, rfiles)) { say(L"updater: the newest manifest is not readable - playing %S", lver.c_str()); return 0; }
    if (!lver.empty() && up_vcmp(rver, lver) <= 0) { say(L"BFR mod pack %S - up to date", lver.c_str()); return 0; }
    // 3. download what differs into _update\, every file checked against the signed manifest
    std::wstring stage = P + L"_update\\";
    std::vector<std::string> todo;
    for (auto &f : rfiles) {
        std::vector<unsigned char> cur;
        std::wstring pf = P + up_w(f.first);
        for (auto &ch : pf) if (ch == L'/') ch = L'\\';
        if (up_read(pf, cur) && up_sha_hex(cur) == f.second) continue;
        todo.push_back(f.first);
    }
    say(L"updater: BFR mod pack %S -> %S, %d file(s) to download", lver.c_str(), rver.c_str(), (int)todo.size());
    for (size_t k = 0; k < todo.size(); k++) {
        std::vector<unsigned char> b;
        if (!up_fetch(commit, todo[k], b) || up_sha_hex(b) != rfiles[todo[k]]) {
            say(L"updater: %S did not download correctly - no update this time (playing %S)", todo[k].c_str(), lver.c_str());
            return 0;
        }
        std::wstring sp = stage + up_w(todo[k]);
        for (auto &ch : sp) if (ch == L'/') ch = L'\\';
        if (!up_write(sp, b)) { say(L"updater: cannot write %s - no update", sp.c_str()); return 0; }
        if ((k + 1) % 20 == 0 || k + 1 == todo.size()) say(L"updater: %d / %d", (int)(k + 1), (int)todo.size());
    }
    // 4. apply: pack files (old ones kept in _backup_<version>\), then the data folder
    std::wstring bk = P + L"_backup_" + up_w(lver.empty() ? std::string("old") : lver) + L"\\";
    SYSTEMTIME t; GetLocalTime(&t);
    wchar_t stamp[32]; swprintf_s(stamp, 32, L"%04d-%02d-%02d_%02d%02d%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    std::wstring dbk = D.substr(0, D.size() - 1) + L"_backup_" + stamp + L"\\";
    int kept = 0, data_new = 0;
    for (auto &rel : todo) {
        std::wstring w = up_w(rel), pf = P + w, sp = stage + w, bp = bk + w;
        for (auto *s : {&pf, &sp, &bp}) for (auto &ch : *s) if (ch == L'/') ch = L'\\';
        if (GetFileAttributesW(pf.c_str()) != INVALID_FILE_ATTRIBUTES) {
            up_write(bp, {});                                       // makes the folders
            if (!MoveFileExW(pf.c_str(), bp.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
                say(L"updater: cannot move %s aside - the update stops here (the rest next start)", pf.c_str());
                return 0;
            }
            if (!_wcsicmp(w.c_str(), L"launcher/CwoLaunch.exe")) *launcher_changed = true;
        }
        for (size_t i = 3; i < pf.size(); i++) if (pf[i] == L'\\') CreateDirectoryW(pf.substr(0, i).c_str(), NULL);
        if (!MoveFileExW(sp.c_str(), pf.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) { say(L"updater: cannot place %s", pf.c_str()); return 0; }
        if (rel.compare(0, 5, "data/")) continue;
        std::wstring tp = D + up_w(rel.substr(5));
        for (auto &ch : tp) if (ch == L'/') ch = L'\\';
        std::vector<unsigned char> mine, neu;
        up_read(pf, neu);
        bool exists = up_read(tp, mine);
        if (exists) {
            std::wstring b2 = dbk + up_w(rel.substr(5));
            for (auto &ch : b2) if (ch == L'/') ch = L'\\';
            up_write(b2, mine);                                    // the player's copy is kept first
        }
        bool patches = !rel.compare(0, 13, "data/patches/");
        bool unchanged = exists && lfiles.count(rel) && up_sha_hex(mine) == lfiles[rel];
        if (rel == "data/CwoNewSkills.txt" && exists) { up_write(tp, up_merge_skills(neu, mine)); data_new++; }
        else if (!exists || unchanged || patches) { up_write(tp, neu); data_new++; }
        else { kept++; say(L"updater: kept your own %S (you changed it)", rel.substr(5).c_str()); }
    }
    up_write(P + L"MANIFEST.json", rm);
    up_write(P + L"MANIFEST.sig", rs);
    std::string vtxt = rver + "\n";
    up_write(P + L"VERSION", std::vector<unsigned char>(vtxt.begin(), vtxt.end()));
    say(L"BFR mod pack updated %S -> %S (%d pack files, %d into the data folder, %d of your own kept; old files in %s)",
        lver.c_str(), rver.c_str(), (int)todo.size(), data_new, kept, bk.c_str());
    return 1;
}

// ---- v5.154 FEEDBACK out (owner 2026-11-12, AGENTS.md "Feedback to the owner's Google Form"): every start, the entries a player
// submitted on the FEEDBACK page (<data>\CwoFeedbackOut.txt, tab separated: when, gamertag, category, message, version, stage,
// mode) go to the owner's Google Form (<data>\CwoFeedbackForm.txt: "form <id>" and "<field> <entry number>" lines, shipped with
// the pack) as a normal form answer (HTTPS POST .../formResponse). An entry leaves the queue only when Google says 200; the
// player's own list (CwoFeedbackMine.txt) then reads "sent". Nothing else is sent.
static bool up_post(const wchar_t *host, const std::wstring &path, const std::string &form) {
    bool ok = false;
    HINTERNET s = WinHttpOpen(L"CwoLaunch-Feedback/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) return false;
    WinHttpSetTimeouts(s, 5000, 5000, 10000, 15000);
    HINTERNET c = WinHttpConnect(s, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"POST", path.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : NULL;
    if (r) {
        const wchar_t *hdr = L"Content-Type: application/x-www-form-urlencoded\r\n";
        if (WinHttpSendRequest(r, hdr, (DWORD)-1L, (LPVOID)form.data(), (DWORD)form.size(), (DWORD)form.size(), 0) && WinHttpReceiveResponse(r, NULL)) {
            DWORD code = 0, len = sizeof(code);
            WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &len, WINHTTP_NO_HEADER_INDEX);
            ok = code == 200;
            if (!ok) say(L"feedback: the form answered %lu", code);
        }
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return ok;
}
static std::string up_urlenc(const std::string &v) {
    std::string o;
    for (unsigned char ch : v) {
        if (isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') o += (char)ch;
        else if (ch == ' ') o += '+';
        else { char t[4]; sprintf_s(t, 4, "%%%02X", ch); o += t; }
    }
    return o;
}
static std::vector<std::string> up_lines(const std::vector<unsigned char> &b) {
    std::vector<std::string> out;
    std::string s(b.begin(), b.end());
    size_t i = 0;
    while (i < s.size()) {
        size_t e = s.find('\n', i); if (e == std::string::npos) e = s.size();
        std::string line = s.substr(i, e - i);
        while (!line.empty() && line.back() == '\r') line.pop_back();
        out.push_back(line);
        i = e + 1;
    }
    return out;
}
static void fb_send(const wchar_t *data) {
    std::wstring D(data);
    std::vector<unsigned char> q, cfg;
    if (!up_read(D + L"CwoFeedbackOut.txt", q) || q.empty()) return;
    if (!up_read(D + L"CwoFeedbackForm.txt", cfg)) { say(L"feedback: no CwoFeedbackForm.txt - your feedback waits"); return; }
    std::map<std::string, std::string> F;
    for (auto &l : up_lines(cfg)) { if (l.empty() || l[0] == '#') continue; size_t sp = l.find(' '); if (sp != std::string::npos) F[l.substr(0, sp)] = l.substr(sp + 1); }
    if (F["form"].empty()) return;
    static const char *FIELDS[7] = {"when", "gamertag", "category", "message", "version", "stage", "mode"};
    std::wstring path = L"/forms/d/e/" + up_w(F["form"]) + L"/formResponse";
    std::vector<std::string> keep, sent;
    for (auto &l : up_lines(q)) {
        if (l.empty()) continue;
        std::vector<std::string> v;
        size_t i = 0;
        for (;;) { size_t t = l.find('\t', i); v.push_back(l.substr(i, t == std::string::npos ? std::string::npos : t - i)); if (t == std::string::npos) break; i = t + 1; }
        std::string body;
        for (int k = 1; k < 7 && k < (int)v.size(); k++) {
            const std::string &id = F[FIELDS[k]];
            if (id.empty()) continue;
            if (!body.empty()) body += '&';
            body += "entry." + id + "=" + up_urlenc(v[k]);
        }
        if (!body.empty() && up_post(L"docs.google.com", path, body)) sent.push_back(l); else keep.push_back(l);
    }
    std::string rest;
    for (auto &l : keep) rest += l + "\n";
    up_write(D + L"CwoFeedbackOut.txt", std::vector<unsigned char>(rest.begin(), rest.end()));
    if (!sent.empty()) {                                           // the player's own list: "sent"
        std::vector<unsigned char> mine;
        up_read(D + L"CwoFeedbackMine.txt", mine);
        std::vector<std::string> ml = up_lines(mine);
        for (auto &l : sent) {
            std::string when = l.substr(0, l.find('\t'));
            for (auto &m : ml)
                if (!m.compare(0, when.size(), when) && m.find("|waiting to send") != std::string::npos) { m = m.substr(0, m.rfind('|')) + "|sent"; break; }
        }
        std::string out;
        for (auto &m : ml) if (!m.empty()) out += m + "\n";
        up_write(D + L"CwoFeedbackMine.txt", std::vector<unsigned char>(out.begin(), out.end()));
    }
    say(L"feedback: %d sent, %d waiting (no answer from the form)", (int)sent.size(), (int)keep.size());
}
