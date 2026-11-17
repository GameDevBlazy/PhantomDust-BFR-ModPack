// ---- v5.154 FEEDBACK, YOUR FEEDBACK, TO-DO LIST and the owner's INBOX (owner 2026-11-12; "a to-do list so people can kind of track my progress"; "I should be able to
// see their gamer tag"; AGENTS.md "Feedback to the owner's Google Form").
// Main menu rows FEEDBACK and TO-DO LIST (under MOD LIST, tools/build_mainmenu_feedback.py) run native 2.8(119, page).
// The window (drawn like MOD LIST, the pad held while it shows):
//   FEEDBACK      Category (Bug / Suggestion / Feedback), Message (keyboard, or A = the pad's letter keyboard), Name (the
//                 signed-in gamertag, else typed once), Submit. A submitted entry goes to <data>\CwoFeedbackOut.txt (the
//                 launcher sends it to the owner's form when it starts - cwo_update.inl) and to CwoFeedbackMine.txt.
//   YOUR FEEDBACK every entry you sent, read only (no undo), "waiting to send" / "sent".
//   TO-DO LIST    <data>\CwoTodo.txt ("status|item|asked by"), shipped with each signed pack update.
//   INBOX         the owner's PC only: <data>\CwoInbox.txt (made from the form's sheet); X = add the entry to the To-Do List.
// What is sent (shown on the page): gamertag, category, message, pack version, the last stage and mode played.
// The gamertag: as the game itself asks for it (Ghidra 2026-11-12, FUN_1401017c0: the signed-in Windows.System.User,
// GetPropertyAsync(L"Gamertag"), vtable +0x48) - here through the same official Windows API on a worker thread.
#include <windows.system.h>
static const char *FB_CATS[4] = {"Bug", "Suggestion", "Feedback", "Skill to share"};   // v5.186.0 + Skill to share
static int g_fb_sk = -1;                                         // v5.186.0 the chosen skill you made (g_ns index)
static const int FB_MSG_MAX = 400, FB_NAME_MAX = 32;
static int g_fb_page, g_fb_cat = 1, g_fb_row, g_fb_scroll, g_fb_kb, g_fb_kx, g_fb_ky, g_fb_klower;
static char g_fb_msg[FB_MSG_MAX + 1], g_fb_name[FB_NAME_MAX + 1], g_fb_said[96];
static char g_fb_tag[FB_NAME_MAX + 1];                           // the signed-in gamertag ("" = not known)
static const char *fb_tag_now() { return g_fb_tag; }          // v5.188.2 cwo_records.inl (records per profile)
static volatile LONG g_fb_tag_state;                              // 0 not asked, 1 asking, 2 done
static char g_fb_last_stage[64] = "(none yet)", g_fb_last_mode[40] = "(none yet)";
static char g_fb_lines[200][72];
static unsigned char g_fb_head[200];
static int g_fb_nl;
static const char FB_KEYS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .,!?'-:/()";
static const int FB_KCOLS = 12;

static void fb_path(const char *name, wchar_t *out) { swprintf_s(out, MAX_PATH, L"%s%S", g_dir, name); }
static void fb_clean(char *s) { for (char *c = s; *c; c++) if (*c == '\t' || *c == '\r' || *c == '\n' || *c == '|') *c = ' '; }

// ---- the gamertag (worker thread; never online-only, it only reads this PC's own signed-in user)
template <class T> static bool fb_wait(ABI::Windows::Foundation::IAsyncOperation<T> *op) {
    ABI::Windows::Foundation::IAsyncInfo *info = NULL;
    if (!op || FAILED(op->QueryInterface(__uuidof(ABI::Windows::Foundation::IAsyncInfo), (void **)&info)) || !info) return false;
    bool ok = false;
    for (int i = 0; i < 100; i++) {                               // 10 s at most
        AsyncStatus st = AsyncStatus::Started;
        if (FAILED(info->get_Status(&st))) break;
        if (st == AsyncStatus::Completed) { ok = true; break; }
        if (st != AsyncStatus::Started) break;
        Sleep(100);
    }
    info->Release();
    return ok;
}
static DWORD WINAPI fb_tag_thread(LPVOID) {
    HMODULE c = LoadLibraryW(L"combase.dll");
    typedef HRESULT(WINAPI *PFN_RF)(HSTRING, REFIID, void **);
    typedef HRESULT(WINAPI *PFN_SR)(PCWSTR, UINT32, HSTRING_HEADER *, HSTRING *);
    typedef HRESULT(WINAPI *PFN_INIT)(RO_INIT_TYPE);
    typedef PCWSTR(WINAPI *PFN_RAW)(HSTRING, UINT32 *);
    typedef HRESULT(WINAPI *PFN_DEL)(HSTRING);
    PFN_RF rf = c ? (PFN_RF)GetProcAddress(c, "RoGetActivationFactory") : NULL;
    PFN_SR sr = c ? (PFN_SR)GetProcAddress(c, "WindowsCreateStringReference") : NULL;
    PFN_INIT ri = c ? (PFN_INIT)GetProcAddress(c, "RoInitialize") : NULL;
    PFN_RAW raw = c ? (PFN_RAW)GetProcAddress(c, "WindowsGetStringRawBuffer") : NULL;
    PFN_DEL del = c ? (PFN_DEL)GetProcAddress(c, "WindowsDeleteString") : NULL;
    char got[FB_NAME_MAX + 1] = "";
    if (rf && sr && ri && raw && del) {
        ri(RO_INIT_MULTITHREADED);
        HSTRING_HEADER h1, h2; HSTRING cls = NULL, key = NULL;
        ABI::Windows::System::IUserStatics *us = NULL;
        if (SUCCEEDED(sr(RuntimeClass_Windows_System_User, (UINT32)wcslen(RuntimeClass_Windows_System_User), &h1, &cls)) &&
            SUCCEEDED(rf(cls, __uuidof(ABI::Windows::System::IUserStatics), (void **)&us)) && us) {
            ABI::Windows::Foundation::IAsyncOperation<ABI::Windows::Foundation::Collections::IVectorView<ABI::Windows::System::User *> *> *op = NULL;
            ABI::Windows::Foundation::Collections::IVectorView<ABI::Windows::System::User *> *users = NULL;
            if (SUCCEEDED(us->FindAllAsync(&op)) && fb_wait(op) && SUCCEEDED(op->GetResults(&users)) && users) {
                unsigned n = 0;
                users->get_Size(&n);
                for (unsigned i = 0; i < n && !got[0]; i++) {
                    ABI::Windows::System::IUser *u = NULL;
                    if (FAILED(users->GetAt(i, &u)) || !u) continue;
                    ABI::Windows::Foundation::IAsyncOperation<IInspectable *> *pop = NULL;
                    IInspectable *v = NULL;
                    if (SUCCEEDED(sr(L"Gamertag", 8, &h2, &key)) && SUCCEEDED(u->GetPropertyAsync(key, &pop)) && fb_wait(pop) &&
                        SUCCEEDED(pop->GetResults(&v)) && v) {
                        ABI::Windows::Foundation::IPropertyValue *pv = NULL;
                        HSTRING s = NULL;
                        if (SUCCEEDED(v->QueryInterface(__uuidof(ABI::Windows::Foundation::IPropertyValue), (void **)&pv)) && pv &&
                            SUCCEEDED(pv->GetString(&s)) && s) {
                            UINT32 len = 0; PCWSTR w = raw(s, &len);
                            int k = 0;
                            for (UINT32 j = 0; w && j < len && k < FB_NAME_MAX; j++) if (w[j] >= 32 && w[j] < 127) got[k++] = (char)w[j];
                            got[k] = 0;
                            del(s);
                        }
                        if (pv) pv->Release();
                    }
                    if (v) v->Release();
                    if (pop) pop->Release();
                    u->Release();
                }
            }
            if (users) users->Release();
            if (op) op->Release();
            us->Release();
        }
    }
    strcpy_s(g_fb_tag, got);
    logf("feedback: the signed-in gamertag %s", got[0] ? "was found" : "is not available (the page asks for a name)");
    InterlockedExchange(&g_fb_tag_state, 2);
    return 0;
}
// v5.169.0 (in game: the Windows lookup above never returned the gamertag in the Store package - "is not available"): the
// gamertag from the game's own sign-in line. Game facts (Ghidra full pass 065, MODLOG 2026-11-13): FUN_1402830b0 refreshes the
// signed-in user block 0x14054bd50 (0xa0 bytes) from the Xbox user object - the name, a C string at +0x0C = 0x14054bd5c, empty
// when no one is signed in (0x1402d4918 = ""); FUN_1401847d0 draws it right-aligned after the "Sign-in: " text slot 34 on every
// menu. Only that visible name is read (printable, 1-32 characters), nothing else of the block.
static const ULONG_PTR RVA_SIGNIN_NAME = 0x54bd5c;
static bool fb_tag_from_game() {
    char t[FB_NAME_MAX + 1] = "";
    int n = 0;
    __try {
        const char *s = (const char *)(g_base + RVA_SIGNIN_NAME);
        for (; n < FB_NAME_MAX && s[n]; n++) {
            unsigned char c = (unsigned char)s[n];
            if (c < 32 || c > 126) return false;
            t[n] = (char)c;
        }
        if (s[n]) return false;                                    // longer than a gamertag: not a name
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    t[n] = 0;
    if (n < 1) return false;
    strcpy_s(g_fb_tag, t);
    logf("feedback: the signed-in gamertag from the game's own sign-in line (%d characters)", n);
    return true;
}
static void fb_tag_ask() {
    if (InterlockedCompareExchange(&g_fb_tag_state, 1, 0) != 0) return;
    HANDLE t = CreateThread(NULL, 0, fb_tag_thread, NULL, 0, NULL);
    if (t) CloseHandle(t); else InterlockedExchange(&g_fb_tag_state, 2);
}

// ---- the version, the last stage and mode
static void fb_version(char *out, size_t n) {
    wchar_t p[MAX_PATH]; fb_path("pack_version.txt", p);           // written by the launcher at each start
    FILE *f = NULL; char v[40] = "";
    if (cwo_wfopen(&f, p, L"r") == 0 && f) { if (!fgets(v, sizeof(v), f)) v[0] = 0; fclose(f); }
    fb_clean(v);
    for (int i = (int)strlen(v) - 1; i >= 0 && v[i] == ' '; i--) v[i] = 0;
    if (v[0]) sprintf_s(out, n, "%s %s (loader %s)", g_bfr_edition ? "BFR" : "MODS", v, CWO_VERSION);
    else sprintf_s(out, n, "%s (loader %s)", g_bfr_edition ? "BFR" : "MODS", CWO_VERSION);
}
// ---- v5.161 PACK CHECK-IN (owner 2026-11-13, AGENTS.md): the gamertag of each player who installs the pack goes to the pack
// author's form: the FEEDBACK outbox gets category Feedback, message "THANK-YOU LIST: ...", the pack version, once per install
// and after each pack update (v5.179: automatically, see ty_step). <data>\CwoThankYou.txt = "sent" / "no" + the last version sent.
static void fb_open(int page);                                   // below
static int g_ty_answer = -2;                                      // -2 not read, -1 not asked yet, 0 no, 1 yes
static char g_ty_sent[80];
static void ty_key(char *out, size_t n) {                         // the pack: edition + pack version (MODS: once per install)
    wchar_t p[MAX_PATH]; fb_path("pack_version.txt", p);
    FILE *f = NULL; char v[40] = "";
    if (cwo_wfopen(&f, p, L"r") == 0 && f) { if (!fgets(v, sizeof(v), f)) v[0] = 0; fclose(f); }
    fb_clean(v);
    for (int i = (int)strlen(v) - 1; i >= 0 && v[i] == ' '; i--) v[i] = 0;
    sprintf_s(out, n, "%s %s", g_bfr_edition ? "BFR" : "MODS", v[0] ? v : "pack");
}
// v5.188.6 (owner 2026-11-16): the check-in's message prefix and its old state file name are not stored as plain text in the DLL; the state file
// is CwoCheckIn.txt (the old file is renamed once, so an explicit "no" stays respected); the log says "pack check-in".
static const unsigned char CI_PREFIX[] = {0x0e, 0x12, 0x1b, 0x14, 0x11, 0x77, 0x03, 0x15, 0x0f, 0x7a, 0x16, 0x13, 0x09, 0x0e, 0x60, 0x7a, 0x5a};
static const unsigned char CI_OLDFILE[] = {0x19, 0x2d, 0x35, 0x0e, 0x32, 0x3b, 0x34, 0x31, 0x03, 0x35, 0x2f, 0x74, 0x2e, 0x22, 0x2e, 0x5a};
static void ci_dec(const unsigned char *e, char *out, size_t n) { size_t i = 0; for (; i + 1 < n && (e[i] ^ 0x5A); i++) out[i] = (char)(e[i] ^ 0x5A); out[i] = 0; }
static void ci_migrate() {
    static bool done; if (done) return; done = true;
    char old[40]; ci_dec(CI_OLDFILE, old, sizeof(old));
    wchar_t po[MAX_PATH], pn[MAX_PATH]; fb_path(old, po); fb_path("CwoCheckIn.txt", pn);
    if (GetFileAttributesW(pn) == INVALID_FILE_ATTRIBUTES && GetFileAttributesW(po) != INVALID_FILE_ATTRIBUTES) MoveFileW(po, pn);
}
static void ty_read() {
    g_ty_answer = -1; g_ty_sent[0] = 0;
    ci_migrate();
    wchar_t p[MAX_PATH]; fb_path("CwoCheckIn.txt", p);
    FILE *f = NULL; char a[16] = "";
    if (cwo_wfopen(&f, p, L"r") != 0 || !f) return;
    if (fgets(a, sizeof(a), f)) g_ty_answer = !strncmp(a, "yes", 3) || !strncmp(a, "sent", 4) ? 1 : !strncmp(a, "no", 2) ? 0 : -1;
    if (!fgets(g_ty_sent, sizeof(g_ty_sent), f)) g_ty_sent[0] = 0;
    fclose(f);
    fb_clean(g_ty_sent);
    for (int i = (int)strlen(g_ty_sent) - 1; i >= 0 && g_ty_sent[i] == ' '; i--) g_ty_sent[i] = 0;
}
static void ty_write() {
    wchar_t p[MAX_PATH]; fb_path("CwoCheckIn.txt", p);
    FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"w") == 0 && f) { fprintf(f, "%s\n%s\n", g_ty_answer == 1 ? "sent" : "no", g_ty_sent); fclose(f); }
}
static void ty_load_name() {                                       // the name typed on FEEDBACK / here before
    if (g_fb_name[0]) return;
    wchar_t p[MAX_PATH]; fb_path("CwoFeedbackName.txt", p);
    FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"r") == 0 && f) { if (!fgets(g_fb_name, sizeof(g_fb_name), f)) g_fb_name[0] = 0; fclose(f); fb_clean(g_fb_name); }
    for (int i = (int)strlen(g_fb_name) - 1; i >= 0 && g_fb_name[i] == ' '; i--) g_fb_name[i] = 0;
}
static const char *fb_who();
static bool ty_queue() {                                          // one entry for the launcher to send (fb_send in CwoLaunch)
    if (!fb_who()[0]) return false;
    char ver[80], when[24], who[FB_NAME_MAX + 1], key[80], msg[200];
    fb_version(ver, sizeof(ver)); ty_key(key, sizeof(key));
    SYSTEMTIME t; GetLocalTime(&t);
    sprintf_s(when, "%04d-%02d-%02d %02d:%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute);
    strcpy_s(who, fb_who()); fb_clean(who); fb_clean(ver);
    size_t kl = strlen(key);                                       // v5.179.2 "MODS pack" (no pack_version.txt) said "pack pack"
    char pre[24]; ci_dec(CI_PREFIX, pre, sizeof(pre));
    sprintf_s(msg, "%s%s downloaded the %s%s", pre, who, key, kl > 5 && !strcmp(key + kl - 5, " pack") ? "" : " pack");
    wchar_t p[MAX_PATH]; fb_path("CwoFeedbackOut.txt", p);
    FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"a") != 0 || !f) return false;
    fprintf(f, "%s\t%s\t%s\t%s\t%s\t%s\t%s\n", when, who, "Feedback", msg, ver, "-", "-");
    fclose(f);
    strcpy_s(g_ty_sent, key);
    logf("pack check-in: %s queued (%s) - the launcher sends it at the next start", who, key);
    return true;
}
// v5.179.0 (owner 2026-11-13, AGENTS.md): no question page any more - once per install and after each pack update the
// gamertag (the game's own sign-in name, or the name typed on FEEDBACK), the edition and the pack version are queued for the
// pack author's form, automatically. MOD LIST ("Pack check-in") and the FEEDBACK page say plainly that the pack sends the
// gamertag and version once per install / update. An explicit "no" given to the old question (5.161-5.178) stays respected.
static void ty_step() {                                           // game thread
    if (g_ty_answer == -2) { ty_read(); ty_load_name(); fb_tag_ask(); }
    if (g_in_match || g_fb_open) return;
    ULONGLONG now = GetTickCount64();
    if (g_fb_tag_state == 2 && !g_fb_tag[0]) {                     // v5.169 the game's own sign-in name (about once a second)
        static ULONGLONG lastg;
        if (now - lastg > 1000) { lastg = now; fb_tag_from_game(); }
    }
    if (g_fb_tag_state == 2 && !g_fb_tag[0]) {                     // asked before the profile signed in: ask again (5 times)
        static ULONGLONG last; static int tries;
        if (tries < 5 && now - last > 15000) { last = now; tries++; InterlockedExchange(&g_fb_tag_state, 0); fb_tag_ask(); }
    }
    if (g_ty_answer == 0 || g_fb_tag_state == 1 || !fb_who()[0]) return;
    char key[80]; ty_key(key, sizeof(key));
    if (!strcmp(key, g_ty_sent)) return;
    static bool test_logged;
    wchar_t tp[MAX_PATH]; fb_path("CwoTestNoCheckIn.txt", tp);  // the agent's offline test runs: nothing queued
    if (GetFileAttributesW(tp) != INVALID_FILE_ATTRIBUTES) {
        if (!test_logged) { test_logged = true; logf("pack check-in: CwoTestNoCheckIn.txt - nothing queued in this test run"); }
        return;
    }
    if (ty_queue()) { g_ty_answer = 1; ty_write(); }
}
static void ty_answer(bool yes) {
    if (yes && !fb_who()[0]) { strcpy_s(g_fb_said, "Type your gamertag first (X: pad letters  Y: keyboard)."); rumble(2); return; }
    g_ty_answer = yes ? 1 : 0;
    if (yes && !g_fb_tag[0] && g_fb_name[0]) {                       // the typed name is kept for the next update
        wchar_t p[MAX_PATH]; fb_path("CwoFeedbackName.txt", p);
        FILE *f = NULL;
        if (cwo_wfopen(&f, p, L"w") == 0 && f) { fprintf(f, "%s\n", g_fb_name); fclose(f); }
    }
    if (yes) ty_queue();
    ty_write();
    logf("pack check-in: the player said %s", yes ? "yes" : "no");
    InterlockedExchange(&g_fb_open, 0);
    InterlockedExchange(&g_ed_game_hold, 1);
}

// v5.161.7 the thank-you page by keyboard (overlay thread, called from dp_frame where the game's CoreWindow answers):
// Enter = yes, Esc = no, Y = type the gamertag
static void fb_page4_keys() {
    static bool pe, pq, py;
    bool e = dp_key(VK_RETURN), q = dp_key(VK_ESCAPE), y = dp_key('Y');
    if (g_fb_open && g_fb_page == 4 && !g_fb_typing && !g_fb_kb) {
        if (e && !pe) ty_answer(true);
        else if (q && !pq) ty_answer(false);
        else if (y && !py && !g_fb_tag[0]) InterlockedExchange(&g_fb_typing, 1);
    }
    pe = e; pq = q; py = y;
}
static void fb_step() {                                           // game thread, every frame: what was played last
    static LONG was;
    LONG now = g_in_match;
    if (now && !was) {
        int st = -1;
        __try { st = *(int *)(g_base + 0x3e0304); } __except (EXCEPTION_EXECUTE_HANDLER) { st = -1; }
        if (st == LIST_STAGE && g_mx_cur >= 0 && g_mx_cur < g_mx_count) sprintf_s(g_fb_last_stage, "%s (%s)", g_mx[g_mx_cur].name, g_mx[g_mx_cur].file);
        else sprintf_s(g_fb_last_stage, "stage %d", st);
    }
    if (now && g_rc_cur_mode[0]) strncpy_s(g_fb_last_mode, g_rc_cur_mode, _TRUNCATE);
    was = now;
    ty_step();                                                     // v5.161 the thank-you list
}

// ---- the lists
static void fb_add(const char *t, bool head) { if (g_fb_nl < 200) { strncpy_s(g_fb_lines[g_fb_nl], t, _TRUNCATE); g_fb_head[g_fb_nl++] = head ? 1 : 0; } }
static void fb_wrap(const char *t) {
    char line[72] = "  "; int len = 2;
    const char *p = t;
    while (*p) {
        while (*p == ' ') p++;
        const char *e = p; while (*e && *e != ' ') e++;
        int wl = (int)(e - p);
        if (!wl) break;
        if (len + 1 + wl > 60 && len > 2) { fb_add(line, false); strcpy_s(line, "  "); len = 2; }
        if (len > 2) line[len++] = ' ';
        for (int i = 0; i < wl && len < 66; i++) line[len++] = p[i];
        line[len] = 0;
        p = e;
    }
    if (len > 2) fb_add(line, false);
}
static void fb_build() {                                          // the lines of YOUR FEEDBACK / TO-DO LIST / INBOX
    g_fb_nl = 0;
    const char *file = g_fb_page == 1 ? "CwoFeedbackMine.txt" : g_fb_page == 2 ? "CwoTodo.txt" : "CwoInbox.txt";
    wchar_t p[MAX_PATH]; fb_path(file, p);
    FILE *f = NULL;
    int n = 0;
    if (cwo_wfopen(&f, p, L"r") == 0 && f) {
        char line[700];
        while (fgets(line, sizeof(line), f)) {
            for (char *c = line; *c; c++) if (*c == '\r' || *c == '\n') *c = 0;
            if (!line[0] || line[0] == '#') continue;
            char *a[8] = {line}; int k = 1;
            for (char *c = line; *c && k < 8; c++) if (*c == '|') { *c = 0; a[k++] = c + 1; }
            char head[160];
            if (g_fb_page == 2) {                                   // status|item|asked by
                sprintf_s(head, "[%s] %s", k > 0 ? a[0] : "", k > 2 && a[2][0] ? a[2] : "");
                fb_add(head, true); if (k > 1) fb_wrap(a[1]);
            } else {                                                // when|gamertag|category|stage|mode|message|state
                sprintf_s(head, "%s  %s - %s", k > 0 ? a[0] : "", k > 2 ? a[2] : "", k > 1 ? a[1] : "");
                fb_add(head, true);
                if (k > 5) fb_wrap(a[5]);
                if (k > 3) { char at[160]; sprintf_s(at, "on %s, %s%s%s", a[3], k > 4 ? a[4] : "", k > 6 ? "  -  " : "", k > 6 ? a[6] : ""); fb_wrap(at); }
            }
            n++;
        }
        fclose(f);
    }
    if (!n) fb_wrap(g_fb_page == 1 ? "Nothing sent yet. Main menu > FEEDBACK." : g_fb_page == 2 ? "The to-do list comes with the next update." : "No feedback in the inbox yet.");
}
static bool fb_is_owner() {                                       // the INBOX: only on a PC that has the owner's inbox file
    wchar_t p[MAX_PATH]; fb_path("CwoInbox.txt", p);
    return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES;
}

// ---- open, submit
static void fb_open(int page) {                                   // native 2.8(119, page), game thread
    g_fb_page = page >= 0 && page <= 4 ? page : 0;
    if (g_fb_page == 3 && !fb_is_owner()) g_fb_page = 2;
    g_fb_row = 0; g_fb_scroll = 0; g_fb_kb = 0; g_fb_said[0] = 0;
    if (g_fb_page) fb_build();
    fb_tag_ask();
    if (!g_fb_name[0]) {                                          // the name typed last time
        wchar_t p[MAX_PATH]; fb_path("CwoFeedbackName.txt", p);
        FILE *f = NULL;
        if (cwo_wfopen(&f, p, L"r") == 0 && f) { if (!fgets(g_fb_name, sizeof(g_fb_name), f)) g_fb_name[0] = 0; fclose(f); fb_clean(g_fb_name); }
    }
    InterlockedExchange(&g_fb_open, 1);
    logf("feedback: page %d opened", g_fb_page);
}
static const char *fb_who() { return g_fb_tag[0] ? g_fb_tag : g_fb_name; }
// v5.186.0 D (owner 2026-11-14 "go ahead and work on A, D"; AGENTS.md "Community skills and created-content safety online"):
// Category "Skill to share" sends ONE skill you made (Create a Skill) to the pack's author: its line as CwoNewSkills.txt keeps
// it (number, base skill, name, description, options), with the usual gamertag / version / stage / mode. The form's own
// category stays "Feedback" (its answer list is fixed); the message starts "SKILL SUBMISSION v1:". If the author adds it, it
// comes to every player as a community skill (600-639) in a signed pack update and may be used online.
static bool fb_sk_ok(int k) { return k >= 0 && k < g_ns_n && !g_ns[k].unsaved && !g_ns[k].comm && !ns_is_pack(g_ns[k]); }
static void fb_sk_next(int dir) {
    int k = g_fb_sk;
    for (int i = 0; i <= g_ns_n; i++) {
        k += dir;
        if (k < 0) k = g_ns_n - 1;
        if (k >= g_ns_n) k = 0;
        if (fb_sk_ok(k)) { g_fb_sk = k; return; }
    }
    g_fb_sk = -1;
}
static void fb_submit_skill() {
    if (!fb_sk_ok(g_fb_sk)) { strcpy_s(g_fb_said, "Make a skill first (CREATE > Create a Skill), then choose it."); rumble(2); return; }
    if (!fb_who()[0]) { strcpy_s(g_fb_said, "Type your gamertag on the Name row first."); rumble(2); g_fb_row = 2; return; }
    const NewSkill &k = g_ns[g_fb_sk];
    char opts[700], msg[1300], ver[80], when[24], who[FB_NAME_MAX + 1], st[64], md[40];
    ns_opts(k, opts, sizeof(opts));
    sprintf_s(msg, "SKILL SUBMISSION v1: %d ^ %d ^ %s ^ %s ^ %s", k.id, k.src, k.name, k.desc[0] ? k.desc : "-", opts);
    fb_version(ver, sizeof(ver));
    SYSTEMTIME t; GetLocalTime(&t);
    sprintf_s(when, "%04d-%02d-%02d %02d:%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute);
    strcpy_s(who, fb_who()); strcpy_s(st, g_fb_last_stage); strcpy_s(md, g_fb_last_mode);
    fb_clean(who); fb_clean(msg); fb_clean(st); fb_clean(md); fb_clean(ver);
    wchar_t p[MAX_PATH];
    bool ok = true;
    FILE *f = NULL;
    fb_path("CwoFeedbackOut.txt", p);
    if (cwo_wfopen(&f, p, L"a") == 0 && f) { fprintf(f, "%s\t%s\t%s\t%s\t%s\t%s\t%s\n", when, who, "Feedback", msg, ver, st, md); fclose(f); } else ok = false;
    fb_path("CwoFeedbackMine.txt", p);
    if (cwo_wfopen(&f, p, L"a") == 0 && f) { fprintf(f, "%s|%s|%s|%s|%s|%s|waiting to send (next start)\n", when, who, "Skill to share", st, md, msg); fclose(f); } else ok = false;
    if (!ok) { strcpy_s(g_fb_said, "Could not save it - see the loader log."); return; }
    logf("feedback: skill '%s' (%d) shared by %s (%d characters) - the launcher sends it at the next start", k.name, k.id, who, (int)strlen(msg));
    strcpy_s(g_fb_said, "Thank you! Your skill goes to the pack's author at the next start.");
    rumble(1);
}
static void fb_submit() {
    if (g_fb_cat == 3) { fb_submit_skill(); return; }
    if (strlen(g_fb_msg) < 3) { strcpy_s(g_fb_said, "Type your message first."); rumble(2); return; }
    if (!fb_who()[0]) { strcpy_s(g_fb_said, "Type your gamertag on the Name row first."); rumble(2); g_fb_row = 2; return; }
    char ver[80], when[24], who[FB_NAME_MAX + 1], msg[FB_MSG_MAX + 1], st[64], md[40];
    fb_version(ver, sizeof(ver));
    SYSTEMTIME t; GetLocalTime(&t);
    sprintf_s(when, "%04d-%02d-%02d %02d:%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute);
    strcpy_s(who, fb_who()); strcpy_s(msg, g_fb_msg); strcpy_s(st, g_fb_last_stage); strcpy_s(md, g_fb_last_mode);
    fb_clean(who); fb_clean(msg); fb_clean(st); fb_clean(md); fb_clean(ver);
    wchar_t p[MAX_PATH];
    bool ok = true;
    FILE *f = NULL;
    fb_path("CwoFeedbackOut.txt", p);                              // the launcher sends these (tab separated)
    if (cwo_wfopen(&f, p, L"a") == 0 && f) { fprintf(f, "%s\t%s\t%s\t%s\t%s\t%s\t%s\n", when, who, FB_CATS[g_fb_cat], msg, ver, st, md); fclose(f); } else ok = false;
    fb_path("CwoFeedbackMine.txt", p);                             // your own read-only list
    if (cwo_wfopen(&f, p, L"a") == 0 && f) { fprintf(f, "%s|%s|%s|%s|%s|%s|waiting to send (next start)\n", when, who, FB_CATS[g_fb_cat], st, md, msg); fclose(f); } else ok = false;
    if (!g_fb_tag[0] && g_fb_name[0]) { fb_path("CwoFeedbackName.txt", p); if (cwo_wfopen(&f, p, L"w") == 0 && f) { fprintf(f, "%s\n", g_fb_name); fclose(f); } }
    if (!ok) { strcpy_s(g_fb_said, "Could not save it - see the loader log."); return; }
    logf("feedback: %s from %s saved (%d characters) - the launcher sends it at the next start", FB_CATS[g_fb_cat], who, (int)strlen(msg));
    g_fb_msg[0] = 0;
    strcpy_s(g_fb_said, "Thank you! It is in YOUR FEEDBACK and goes out the next time the game starts.");
    rumble(1);
}
static void fb_todo_add(int line_head) {                          // INBOX X: the owner adds an entry to the To-Do List
    if (line_head < 0 || line_head >= g_fb_nl) return;
    char item[300] = "";
    for (int i = line_head + 1; i < g_fb_nl && !g_fb_head[i]; i++) { if (item[0]) strcat_s(item, " "); strcat_s(item, g_fb_lines[i] + 2); }
    const char *h = g_fb_lines[line_head];
    const char *dash = strstr(h, " - ");
    char who[48] = ""; if (dash) strncpy_s(who, dash + 3, _TRUNCATE);
    wchar_t p[MAX_PATH]; fb_path("CwoTodo.txt", p);
    FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"a") == 0 && f) { fprintf(f, "Planned|%s|%s\n", item, who); fclose(f); }
    strcpy_s(g_fb_said, "Added to the TO-DO LIST (it ships with your next update).");
    rumble(1);
}

// ---- typing (render thread, from dp_frame while the Message or Name row is typed)
static volatile LONG g_fb_key_prev[256];
static void fb_keys() {
    if (!g_fb_open || !g_fb_typing) return;
    char *buf = g_fb_row == 2 ? g_fb_name : g_fb_msg;
    size_t cap = g_fb_row == 2 ? FB_NAME_MAX : FB_MSG_MAX;
    bool shift = dp_key(16);
    for (int vk = 8; vk < 223; vk++) {
        bool d = dp_key(vk);
        bool edge = d && !g_fb_key_prev[vk];
        g_fb_key_prev[vk] = d;
        if (!edge) continue;
        size_t n = strlen(buf);
        char ch = 0;
        if (vk == 8) { if (n) buf[n - 1] = 0; continue; }
        if (vk == 13) { InterlockedExchange(&g_fb_typing, 0); if (g_fb_row == 1) g_fb_row = 3; continue; }   // Enter: done, onto Submit
        if (vk == 27) { InterlockedExchange(&g_fb_typing, 0); continue; }
        if (vk >= 0x41 && vk <= 0x5A) ch = (char)(shift ? vk : vk + 32);
        else if (vk >= 0x30 && vk <= 0x39) ch = shift ? ")!@#$%^&*("[vk - 0x30] : (char)vk;
        else if (vk == 32) ch = ' ';
        else if (vk == 0xBE) ch = shift ? '>' : '.';
        else if (vk == 0xBC) ch = shift ? '<' : ',';
        else if (vk == 0xBF) ch = shift ? '?' : '/';
        else if (vk == 0xDE) ch = shift ? '"' : '\'';
        else if (vk == 0xBA) ch = shift ? ':' : ';';
        else if (vk == 0xBD) ch = shift ? '_' : '-';
        else if (vk == 0xBB) ch = shift ? '+' : '=';
        if (ch && n < cap) { buf[n] = ch; buf[n + 1] = 0; }
    }
}

// ---- the pad (pad thread)
static void fb_input(WORD pressed) {
    if (g_fb_kb) {                                                 // the pad's letter keyboard
        int nk = (int)strlen(FB_KEYS), rows = (nk + FB_KCOLS - 1) / FB_KCOLS;
        char *buf = g_fb_row == 2 ? g_fb_name : g_fb_msg;
        size_t cap = g_fb_row == 2 ? FB_NAME_MAX : FB_MSG_MAX;
        if (pressed & BTN_RIGHT) g_fb_kx = (g_fb_kx + 1) % FB_KCOLS;
        if (pressed & BTN_LEFT) g_fb_kx = (g_fb_kx + FB_KCOLS - 1) % FB_KCOLS;
        if (pressed & BTN_DOWN) g_fb_ky = (g_fb_ky + 1) % rows;
        if (pressed & BTN_UP) g_fb_ky = (g_fb_ky + rows - 1) % rows;
        if (pressed & BTN_Y) g_fb_klower = !g_fb_klower;
        size_t n = strlen(buf);
        if ((pressed & BTN_X) && n) buf[n - 1] = 0;
        if (pressed & BTN_A) {
            int k = g_fb_ky * FB_KCOLS + g_fb_kx;
            if (k < nk && n < cap) { char ch = FB_KEYS[k]; if (g_fb_klower && ch >= 'A' && ch <= 'Z') ch = (char)(ch + 32); buf[n] = ch; buf[n + 1] = 0; }
        }
        if (pressed & (BTN_B | BTN_START)) g_fb_kb = 0;
        return;
    }
    if (g_fb_page == 4) {                                          // v5.161 THANK-YOU LIST: A yes, B no; X / Y type the gamertag
        g_fb_row = 2;
        if (!g_fb_tag[0] && (pressed & BTN_X)) { g_fb_kb = 1; g_fb_kx = g_fb_ky = 0; return; }
        if (!g_fb_tag[0] && (pressed & BTN_Y)) { InterlockedExchange(&g_fb_typing, 1); return; }
        if (pressed & BTN_A) ty_answer(true);
        else if (pressed & BTN_B) ty_answer(false);
        return;
    }
    if (g_fb_page) {                                               // the read-only lists
        int maxs = g_fb_nl - 12; if (maxs < 0) maxs = 0;
        if (pressed & BTN_UP) g_fb_scroll = g_fb_scroll > 0 ? g_fb_scroll - 1 : maxs;
        if (pressed & BTN_DOWN) g_fb_scroll = g_fb_scroll < maxs ? g_fb_scroll + 1 : 0;
        if (pressed & BTN_RB) { g_fb_page = g_fb_page == 1 ? 2 : g_fb_page == 2 && fb_is_owner() ? 3 : 1; g_fb_scroll = 0; fb_build(); }
        if (pressed & BTN_LB) { g_fb_page = 0; g_fb_row = 0; }
        if ((pressed & BTN_X) && g_fb_page == 3) {                 // INBOX: the first entry on the screen goes to the To-Do List
            for (int i = g_fb_scroll; i < g_fb_nl; i++) if (g_fb_head[i]) { fb_todo_add(i); break; }
        }
        if (pressed & BTN_B) { InterlockedExchange(&g_fb_open, 0); InterlockedExchange(&g_ed_game_hold, 1); }
        return;
    }
    if (pressed & BTN_DOWN) g_fb_row = (g_fb_row + 1) % 4;        // 0 Category, 1 Message, 2 Name, 3 Submit
    if (pressed & BTN_UP) g_fb_row = (g_fb_row + 3) % 4;
    if (g_fb_row == 0 && (pressed & (BTN_LEFT | BTN_RIGHT | BTN_A))) {
        g_fb_cat = (g_fb_cat + ((pressed & BTN_LEFT) ? 3 : 1)) % 4;
        if (g_fb_cat == 3 && !fb_sk_ok(g_fb_sk)) fb_sk_next(1);   // v5.186.0 the first skill you made
    }
    if (g_fb_cat == 3 && g_fb_row == 1 && (pressed & (BTN_LEFT | BTN_RIGHT | BTN_A))) fb_sk_next((pressed & BTN_LEFT) ? -1 : 1);
    if (((g_fb_row == 1 && g_fb_cat != 3) || (g_fb_row == 2 && !g_fb_tag[0])) && (pressed & BTN_A)) { g_fb_kb = 1; g_fb_kx = g_fb_ky = 0; }
    if (((g_fb_row == 1 && g_fb_cat != 3) || (g_fb_row == 2 && !g_fb_tag[0])) && (pressed & BTN_Y)) InterlockedExchange(&g_fb_typing, 1);   // the keyboard
    if (g_fb_row == 3 && (pressed & BTN_A)) fb_submit();
    if (pressed & BTN_RB) { g_fb_page = 1; g_fb_scroll = 0; fb_build(); }
    if (pressed & BTN_B) { InterlockedExchange(&g_fb_typing, 0); InterlockedExchange(&g_fb_open, 0); InterlockedExchange(&g_ed_game_hold, 1); }
}

// ---- the window (render thread)
static void fb_draw() {
    if (!g_fb_open || g_in_match) { if (g_in_match) { g_fb_open = 0; g_fb_typing = 0; } return; }
    int glyphs = *(int *)(g_base + RVA_GLYPHS), cap = *(int *)(g_base + RVA_GLYPH_CAP);
    if (glyphs > cap - 800) return;
    if (*(int *)(g_base + RVA_OFS_X) != 0 || *(int *)(g_base + RVA_OFS_Y) != 0) return;
    int *space = (int *)(g_base + RVA_FONT_SPACE), *gap = (int *)(g_base + RVA_FONT_GAP);
    int *fh = (int *)(g_base + RVA_FONT_H), *fw = (int *)(g_base + RVA_FONT_W), *x0 = (int *)(g_base + RVA_FONT_X0);
    DWORD *rgba = (DWORD *)(g_base + RVA_FONT_RGBA);
    int s_space = *space, s_gap = *gap, s_h = *fh, s_w = *fw, s_x0 = *x0;
    DWORD s_rgba[4]; memcpy(s_rgba, rgba, sizeof(s_rgba));
    PFN_TEXT text = (PFN_TEXT)(g_base + RVA_TEXT);
    DWORD acc = g_th_on >= 0 ? th_accent() : 0xFF80E0D0, WHITE = 0xFFFFFFFF, GREY = 0xFFC8C8C8, YEL = 0xFFFFE040;
    auto box = [&](int px, int py, DWORD col, int w, int h) { *space = 0; *gap = 0; *fh = h; *fw = w; set_rgba(rgba, col); text(px, py, "\x81\xa1"); };
    auto put = [&](int px, int py, const char *s, DWORD col) { *space = 0; *gap = 2; *fh = 14; *fw = 7; set_rgba(rgba, 0xFF000000); text(px + 1, py + 1, s); set_rgba(rgba, col); text(px, py, s); };
    const int W = 500, X = (640 - W) / 2, Y = 56, LH = 19;
    // v5.170.1 the panel over the whole 4:3 picture down to the button legend (the main menu showed beside it)
    for (int by = Y - 10; by < 400; by += 48) for (int bx = 0; bx < 640; bx += 48) box(bx, by, 0xFF061614, 48, by + 64 > 400 ? 400 - by : 64);
    char line[128];
    static const char *TITLES[5] = {"FEEDBACK - BUGS, SUGGESTIONS, IDEAS", "YOUR FEEDBACK", "TO-DO LIST", "INBOX (pack author only)",
                                    "THE PACK"};
    put(X, Y, TITLES[g_fb_page], acc);
    if (g_fb_page == 4) {                                          // v5.161 the thank-you list question
        static const char *T[] = {"Once per install and after each pack update, this pack sends", "your gamertag and the pack version to the pack author.",
                                  "", "Nothing else is sent."};
        int y = Y + 30;
        for (const char *t : T) { put(X, y, t, t[0] == 'A' ? YEL : WHITE); y += 18; }
        const char *who = fb_who();
        sprintf_s(line, "Your gamertag:  %s%s", who[0] ? who : "(type it: X pad letters  Y keyboard)",
                  g_fb_tag[0] ? "  (signed in)" : g_fb_typing && ((GetTickCount64() / 400) & 1) ? "_" : "");
        put(X, y + 10, line, acc);
        if (g_fb_kb) {                                             // the pad's letter keyboard for the name
            int nk = (int)strlen(FB_KEYS), ky = y + 34;
            for (int k = 0; k < nk; k++) {
                int cx = X + 10 + (k % FB_KCOLS) * 38, cy = ky + (k / FB_KCOLS) * 22;
                bool on = k == g_fb_ky * FB_KCOLS + g_fb_kx;
                if (on) box(cx - 4, cy - 2, 0xFF8A2E3C, 17, 18);   // v5.170.5 ~27 wide drawn (x1.6) round the letter at cx + 6 (30 covered the next letter)
                char ch[4] = {FB_KEYS[k] == ' ' ? '_' : (g_fb_klower && FB_KEYS[k] >= 'A' && FB_KEYS[k] <= 'Z' ? (char)(FB_KEYS[k] + 32) : FB_KEYS[k]), 0};
                put(cx + 6, cy, ch, on ? YEL : WHITE);
            }
        }
        int help_y = g_fb_kb ? y + 34 + 4 * 22 + 6 : Y + 30 + LH * 15;     // under the letter grid while it shows
        if (g_fb_said[0] && !g_fb_kb) put(X, Y + 30 + LH * 14, g_fb_said, YEL);
        put(X, help_y, g_fb_kb ? "A type  X delete  Y a/A  B done" : g_fb_typing ? "Typing: Enter done   Esc stop"
                                       : "A / Enter: Yes, add me      B / Esc: No thanks", GREY);
    } else if (g_fb_page) {
        for (int r = 0; r < 14 && g_fb_scroll + r < g_fb_nl; r++)
            put(X, Y + 26 + LH * r, g_fb_lines[g_fb_scroll + r], g_fb_head[g_fb_scroll + r] ? acc : WHITE);
        if (g_fb_said[0]) put(X, Y + 26 + LH * 14, g_fb_said, YEL);
        put(X, Y + 30 + LH * 15, g_fb_page == 3 ? "Up/Down scroll  X: top entry -> To-Do  RB next  LB write  B close"
                                                 : "Up/Down scroll   RB next list   LB write feedback   B close", GREY);
    } else {
        int y = Y + 26;
        if (g_fb_cat == 3) {                                       // v5.186.0 what Skill to share sends
            put(X, y, "Skill to share: sends the skill's settings (base skill, name,", GREY); y += 16;
            put(X, y, "text, values), your gamertag and the pack version. If the", GREY); y += 16;
            put(X, y, "pack's author adds it, everyone gets it in an update.", GREY); y += 22;
        } else {
            put(X, y, "Read before every update. Sent with it: your gamertag, the pack", GREY); y += 16;
            put(X, y, "version and the last stage and mode you played. Once per", GREY); y += 16;
            put(X, y, "install / update the pack also sends your gamertag + version.", GREY); y += 22;
        }
        const char *who = fb_who();
        for (int r = 0; r < 4; r++) {
            bool on = r == g_fb_row;
            // v5.170.6 (in game: one wide box left a thin dash past its end - the stretched glyph bleeds) tiles of 24 (~38 drawn)
            if (on) { int x1 = X - 6 + W - 4 - 38; for (int bx = X - 6; bx < x1; bx += 24) box(bx, y - 2, 0xFF8A2E3C, 24, 17); box(x1, y - 2, 0xFF8A2E3C, 24, 17); }
            if (r == 0) sprintf_s(line, "Category    < %s >", FB_CATS[g_fb_cat]);
            if (r == 1) sprintf_s(line, "Message     %s", g_fb_msg[0] ? "" : (g_fb_typing && on ? "" : "(Y: type with the keyboard   A: pad letters)"));
            if (r == 1 && g_fb_cat == 3) {                         // v5.186.0 the skill you made, Left / Right to choose
                if (fb_sk_ok(g_fb_sk)) sprintf_s(line, "Skill       < %.32s >", g_ns[g_fb_sk].name);
                else strcpy_s(line, "Skill       (make one first: CREATE > Create a Skill)");
            }
            if (r == 2) sprintf_s(line, "Name        %s%s", who[0] ? who : "(your gamertag - Y keyboard / A pad letters)", g_fb_tag[0] ? "  (signed in)" : "");
            if (r == 3) strcpy_s(line, "[ SUBMIT ]");
            put(X, y, line, on ? YEL : WHITE);
            y += LH;
            if (r == 1 && g_fb_msg[0] && g_fb_cat != 3) {          // the message, wrapped, with the cursor while typed
                char tmp[FB_MSG_MAX + 4]; sprintf_s(tmp, "%s%s", g_fb_msg, g_fb_typing && on && ((GetTickCount64() / 400) & 1) ? "_" : "");
                int nl = 0; size_t len = strlen(tmp), at = 0;
                while (at < len && nl < 6) {
                    char seg[64]; size_t take = len - at > 58 ? 58 : len - at;
                    if (at + take < len) { size_t sp = take; while (sp > 20 && tmp[at + sp] != ' ') sp--; if (sp > 20) take = sp; }
                    memcpy(seg, tmp + at, take); seg[take] = 0;
                    put(X + 20, y, seg, 0xFFF2EAD8); y += 16; nl++; at += take; while (at < len && tmp[at] == ' ') at++;
                }
                sprintf_s(line, "%d / %d", (int)strlen(g_fb_msg), FB_MSG_MAX); put(X + W - 90, y - 16, line, GREY);
            }
        }
        if (g_fb_kb) {                                             // the pad's letter keyboard
            int nk = (int)strlen(FB_KEYS), ky = y + 6;
            for (int k = 0; k < nk; k++) {
                int cx = X + 10 + (k % FB_KCOLS) * 38, cy = ky + (k / FB_KCOLS) * 22;
                bool on = k == g_fb_ky * FB_KCOLS + g_fb_kx;
                if (on) box(cx - 4, cy - 2, 0xFF8A2E3C, 17, 18);   // v5.170.5 ~27 wide drawn (x1.6) round the letter at cx + 6 (30 covered the next letter)
                char ch[4] = {FB_KEYS[k] == ' ' ? '_' : (g_fb_klower && FB_KEYS[k] >= 'A' && FB_KEYS[k] <= 'Z' ? (char)(FB_KEYS[k] + 32) : FB_KEYS[k]), 0};
                put(cx + 6, cy, ch, on ? YEL : WHITE);
            }
            put(X, ky + 4 * 22 + 4, "A type  X delete  Y a/A  B done", GREY);
        } else {
            if (g_fb_said[0]) put(X, y + 8, g_fb_said, YEL);
            put(X, Y + 30 + LH * 15, g_fb_typing ? "Typing: Enter done   Esc stop" : "Up/Down row   A choose   RB your feedback / to-do   B close", GREY);
        }
    }
    memcpy(rgba, s_rgba, sizeof(s_rgba));
    *space = s_space; *gap = s_gap; *fh = s_h; *fw = s_w; *x0 = s_x0;
}
static INT64 fb_native(INT64 *args) {                             // op 119 (page)
    INT64 page = 0;
    __try { page = args[1]; } __except (EXCEPTION_EXECUTE_HANDLER) { page = 0; }
    fb_open((int)page);
    return 0;
}
static int fb_self_test() {                                       // the To-Do / Mine lines wrap inside the window
    fb_wrap("A long message that has to wrap inside the window so that no line runs past the right side of the panel at all, ever.");
    for (int i = 0; i < g_fb_nl; i++) if ((int)strlen(g_fb_lines[i]) > 66) return -83;
    char s[] = "a|b\tc\nd"; fb_clean(s);
    bool clean = !strcmp(s, "a b c d");
    g_fb_nl = 0;
    logf("self-test: feedback pages %s", clean ? "ok" : "FAILED");
    return clean ? 1 : -84;
}
