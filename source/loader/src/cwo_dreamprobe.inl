// ---- v5.123 Dream input probe (owner 2026-11-09: Edgar's Dream CREATE editor, Unreal-style; "Go ahead and show me an
// image of it once you're ready"). The first build step of the editor plan (16_Reports/EDGARS_DREAM_CREATE_EDITOR_PREPLAN):
// research spikes R1 (mouse + keys from the loader), R2 (cursor), R4 (keep keys away from the game), R8 (L3+R3 switch).
// On only while CwoDreamProbe.txt exists in the loader folder, offline only.
// Game facts (PD Ghidra full pass 065 via pd_fn.py, MODLOG 2026-11-09):
//   the game's KeyDown handler FUN_140008490 turns a key into a game button (FUN_140008410) and sets bit n of DAT_14054ca48;
//   its pointer handlers read only buttons and the wheel (binding table 0x1403dcbb0); the cursor position is never used.
// Our own design (Windows SDK, Windows.UI.Core ABI): the loader asks the game's CoreWindow (GetForCurrentThread on the
// thread that runs CwoOverlayFrame) for the pointer position, window bounds and key / mouse-button states each frame, and
// draws its own cursor and a test strip. Keys + mouse go to the EDITOR by default (the game's key bits are cleared while
// they do); F10 hands them back to the fighter. L3 + R3 together switch what the pad drives (the game sees nothing from the
// pad while it drives the editor).
#include <windows.ui.core.h>
static volatile LONG g_dp_on, g_dp_pad_editor, g_dp_kbm_editor = 1, g_dp_pad_combo;
static ABI::Windows::UI::Core::ICoreWindow *g_dp_win;
static int g_dp_win_state;                                       // 0 not tried, 1 got it, -1 none on this thread
static unsigned g_dp_frames, g_dp_clicks;
static float g_dp_mx = -1, g_dp_my = -1;                         // the cursor in the debug font's 640 x 480 space
static float g_dp_raw[4];                                        // pointer x, y and window x, y (DIPs) for the log
static char g_dp_keys[64], g_dp_last[64];
static bool g_dp_lmb, g_dp_rmb, g_dp_f10, g_dp_cursor_logged;
static UINT32 g_dp_pad_buttons;
static int g_ui_state;                                            // cwo_ui.inl: 1 = the neat UI draws instead
static volatile float g_ui_pfx = -1, g_ui_pfy = -1;               // v5.124 pointer for the neat UI (cwo_ui.inl)
static volatile LONG g_ui_click_seq;
// v5.123.1 (in game 2026-11-09: the reported pointer never moved - the game hides its window's cursor (PointerCursor =
// none) and Windows then clips the system cursor to one pixel at the screen centre, a relative-mouse lock; the exe has no
// ClipCursor / user32 import). While keys + mouse belong to the editor the loader gives the window an arrow cursor
// (CoreCursor, Windows SDK) so the pointer moves freely; the game's own cursor (none) is put back when they go to the fighter.
static ABI::Windows::UI::Core::ICoreCursor *g_dp_arrow, *g_dp_game_cursor;
static int g_dp_cursor_mode = -1;                                // 1 = our arrow set, 0 = the game's cursor back
static const ULONG_PTR RVA_KEY_BITS = 0x54ca48;
// v5.123.2 R6 (Ghidra full pass 065): the debug font queues at most DAT_14054c978 = 1000 glyphs (FUN_14028c9d0 allocates
// 4000 vertices of 0x1C bytes); FUN_14028caa0 draws the queue and empties it (the battle HUD calls it after each panel). The
// editor draws in batches: when the queue nears its cap it is flushed and drawing goes on - no glyph limit per frame.
static const ULONG_PTR RVA_FONT_FLUSH = 0x28caa0;
typedef void(__fastcall *PFN_FLUSH)();
static unsigned g_dp_flushes;
static volatile LONG g_dp_flushing;                              // v5.123.6: the flush calls CwoOverlayFrame again - it must return at once
static char g_dp_name[24];                                       // R5: a typed name (polled keys)
static bool g_dp_prev_keys[256];
static double g_dp_fps;
static bool g_dp_stress;                                         // F9: a wall of ~3,500 glyphs (R6)

static bool cr_wanted();                                         // v5.127 cwo_create.inl: Edgar's Dream > CREATE is on
static void cr_keys();
static void dp_pad(ABI::Windows::Gaming::Input::GamepadReading *r) {      // pad thread, every reading
    if (!g_dp_on || !r) return;
    UINT32 b = (UINT32)r->Buttons;
    bool combo = (b & 0x3000) == 0x3000;                         // GamepadButtons_LeftThumbstick 0x1000 | RightThumbstick 0x2000
    if (combo && !g_dp_pad_combo) InterlockedExchange(&g_dp_pad_editor, !g_dp_pad_editor);
    InterlockedExchange(&g_dp_pad_combo, combo ? 1 : 0);
    g_dp_pad_buttons = b;
    if (g_dp_pad_editor || combo) {                              // the editor has the pad: the game sees none of it
        r->Buttons = (ABI::Windows::Gaming::Input::GamepadButtons)0;
        r->LeftThumbstickX = r->LeftThumbstickY = r->RightThumbstickX = r->RightThumbstickY = 0.0;
        r->LeftTrigger = r->RightTrigger = 0.0;
    }
}

static void fb_page4_keys();                                    // cwo_feedback.inl (included later)
static bool dp_key(int vk) {
    ABI::Windows::UI::Core::CoreVirtualKeyStates st = ABI::Windows::UI::Core::CoreVirtualKeyStates_None;
    if (!g_dp_win || FAILED(g_dp_win->GetAsyncKeyState((ABI::Windows::System::VirtualKey)vk, &st))) return false;
    return (st & ABI::Windows::UI::Core::CoreVirtualKeyStates_Down) != 0;
}

// v5.150.0 (owner 2026-11-11). Ghidra full pass 065 (clean exe
// 9ba7411b): the game's input = FUN_14028eac0 reads DAT_14054ca40 (mouse buttons) | DAT_14054ca48 (keys), then clears ca40.
// The mouse handler FUN_140007c70 rewrites ca40 on every main-loop poll while a button is held, through the binding table
// 0x1403dcbb0 (14 x 8 ints: +0 kind 0-6, +4..+6 the game button per control scheme, -1 none); KeyDown FUN_140008490 maps
// the key with FUN_140008410 through 0x1403dc890 (25 x 8 ints: +0 button, +4 / +5 the two virtual keys) and 0x1403dcd70
// (11 x 8 ints), sets bit n of ca48 and the held state DAT_1405ae178[button]. The 5.123 R4 mask cleared only ca48 once per
// drawn frame: every mouse click on a panel still reached the fighter (ca40 is never masked), and a held key could slip in
// between. Now, while the keyboard + mouse drive the editor, the three tables map nothing (the keys' +4 / +5 = -2, the mouse
// buttons' +4..+6 = -1); the game's own copies come back the moment they drive the fighter again, or the editor closes.
static const ULONG_PTR RVA_KEYTAB_A = 0x3dc890, RVA_MOUSETAB = 0x3dcbb0, RVA_KEYTAB_B = 0x3dcd70, RVA_MOUSE_BITS = 0x54ca40, RVA_KEY_HELD = 0x5ae178;
static int g_dp_tab_saved[(25 + 14 + 11) * 8];
static bool g_dp_tab_blocked;
static void dp_block_game_input(bool on) {
    if (on == g_dp_tab_blocked) return;
    int *t = (int *)(g_base + RVA_KEYTAB_A);                         // the three tables lie back to back (0x320 + 0x1C0 + 0x160 bytes)
    if ((ULONG_PTR)(t + 25 * 8) != g_base + RVA_MOUSETAB || (ULONG_PTR)(t + 39 * 8) != g_base + RVA_KEYTAB_B) return;
    DWORD old = 0;
    if (!VirtualProtect(t, sizeof(g_dp_tab_saved), PAGE_READWRITE, &old)) { logf("CREATE: input tables not writable"); return; }
    __try {
        if (on) {
            memcpy(g_dp_tab_saved, t, sizeof(g_dp_tab_saved));
            unsigned char *held = (unsigned char *)(g_base + RVA_KEY_HELD);
            for (int k = 0; k < 50; k++) {
                int *e = t + k * 8;
                if (k >= 25 && k < 39) { e[4] = e[5] = e[6] = -1; continue; }   // mouse: no game button
                if (e[0] >= 0 && e[0] < 0x1A) held[e[0]] = 0;                // as the game's own KeyUp for that button
                e[4] = e[5] = -2;                                            // keys: no virtual key matches
            }
        } else memcpy(t, g_dp_tab_saved, sizeof(g_dp_tab_saved));
        *(volatile DWORD *)(g_base + RVA_MOUSE_BITS) = 0;
        *(volatile DWORD *)(g_base + RVA_KEY_BITS) = 0;
        g_dp_tab_blocked = on;
        logf("CREATE: the game's key + mouse bindings %s", on ? "held back (keyboard + mouse drive the editor)" : "back (they drive the fighter)");
    } __except (EXCEPTION_EXECUTE_HANDLER) { logf("CREATE: input tables fault - left as they were"); }
    VirtualProtect(t, sizeof(g_dp_tab_saved), old, &old);
}

// offline test: on a stand-in image the three tables map nothing while held back, the held states of the mapped buttons are
// cleared, the masks are 0, and every byte of the tables comes back
static int dp_block_self_test() {
    const size_t SZ = RVA_KEY_HELD + 0x40;
    unsigned char *img = (unsigned char *)VirtualAlloc(NULL, SZ, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!img) return -96;
    ULONG_PTR keep = g_base; bool kb = g_dp_tab_blocked;
    g_base = (ULONG_PTR)img; g_dp_tab_blocked = false;
    int *t = (int *)(img + RVA_KEYTAB_A);
    for (int k = 0; k < 50 * 8; k++) t[k] = k * 7 + 3;
    for (int k = 0; k < 50; k++) t[k * 8] = k % 20;                  // buttons 0-19
    unsigned char *held = img + RVA_KEY_HELD;
    memset(held, 1, 0x20);
    *(DWORD *)(img + RVA_MOUSE_BITS) = 0xFFFF; *(DWORD *)(img + RVA_KEY_BITS) = 0xFFFF;
    int ref[50 * 8]; memcpy(ref, t, sizeof(ref));
    dp_block_game_input(true);
    bool ok = g_dp_tab_blocked && !*(DWORD *)(img + RVA_MOUSE_BITS) && !*(DWORD *)(img + RVA_KEY_BITS);
    for (int k = 0; k < 50 && ok; k++) {
        int *e = t + k * 8;
        if (k >= 25 && k < 39) ok = e[4] == -1 && e[5] == -1 && e[6] == -1 && e[0] == ref[k * 8] && e[7] == ref[k * 8 + 7];
        else ok = e[4] == -2 && e[5] == -2 && e[6] == ref[k * 8 + 6] && held[e[0]] == 0;
    }
    ok = ok && held[0x1A] == 1;                                      // nothing past the held states touched
    dp_block_game_input(false);
    ok = ok && !g_dp_tab_blocked && !memcmp(ref, t, sizeof(ref));
    g_base = keep; g_dp_tab_blocked = kb;
    VirtualFree(img, 0, MEM_RELEASE);
    logf("self-test: CREATE holds back the game's key + mouse bindings and gives them back %s", ok ? "ok" : "FAILED");
    return ok ? 1 : -97;
}

// v5.188.36 (hourly Create a Mode pass 7, in game 07:58-08:09: with no controller connected SINGLE PLAY showed the game's
// debug list - the loader's worker skipped its whole step without a pad, so its pages never opened): the loader's menu pages
// (SINGLE PLAY lobby, CREATE / Edgar's Dream pages - g_ed_open over the menus, not the in-match editor) take the keyboard
// too: arrows = D-pad, Enter / A = A, Esc / Backspace / B = B, Space / X = X, Tab / Y = Y, Q / E = LB / RB; while they are
// open the game's key and mouse bindings are held back (dp_block_game_input, as the CREATE editor). Read here on the window's own thread, used by the worker (g_kb_bits).
static volatile LONG g_kb_bits;
static bool ed_kb_pages();                                        // cwo_edgar.inl: a loader menu page is open over the menus
static void kb_pages_frame() {
    WORD b = 0;
    if (g_dp_win && !ed_online() && ed_kb_pages()) {
        static const struct { int vk; WORD bit; } K[] = {{VK_UP, BTN_UP}, {VK_DOWN, BTN_DOWN}, {VK_LEFT, BTN_LEFT}, {VK_RIGHT, BTN_RIGHT},
            {VK_RETURN, BTN_A}, {VK_ESCAPE, BTN_B}, {VK_BACK, BTN_B}, {VK_SPACE, BTN_X}, {VK_TAB, BTN_Y}, {'Q', BTN_LB}, {'E', BTN_RB},
            {'A', BTN_A}, {'B', BTN_B}, {'X', BTN_X}, {'Y', BTN_Y}};       // the letters as the footers say them (the game's B key too)
        for (const auto &k : K) if (dp_key(k.vk)) b |= k.bit;
    }
    InterlockedExchange(&g_kb_bits, b);
}
static void dp_frame() {                                          // render thread (CwoOverlayFrame), every frame
    if ((g_dp_frames++ % 60) == 0 || (g_fb_typing && !g_dp_on)) {   // v5.154 typing FEEDBACK turns it on at once
        wchar_t p[MAX_PATH];
        swprintf_s(p, MAX_PATH, L"%sCwoDreamProbe.txt", g_dir);
        LONG on = (GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES || cr_wanted() || (g_fb_typing && g_fb_open)) && !ed_online();   // v5.127 CREATE: no probe file
        if (on) {                                                   // v5.123.5 the file's word "stress" turns the glyph wall on (R6)
            FILE *f = NULL; char w[32] = "";
            if (cwo_wfopen(&f, p, L"r") == 0 && f) { if (!fgets(w, sizeof(w), f)) w[0] = 0; fclose(f); }
            g_dp_stress = strstr(w, "stress") != NULL;
        }
        if (on != g_dp_on) logf("dream probe: %s", on ? (g_cr_entry ? "on (Edgar's Dream > CREATE)" : "on (CwoDreamProbe.txt)") : "off");
        InterlockedExchange(&g_dp_on, on);
    }
    {   // v5.124.1 while the CREATE screen shows, the game's battle HUD is hidden (the free camera's flag); put back after
        static bool shown, saved;
        bool now = g_dp_on && g_ui_state == 1;
        if (now && !shown) { saved = g_hide_hud; }                   // v5.125 the HUD stays: the whole game shows in the viewport
        if (!now && shown) g_hide_hud = saved;
        shown = now;
    }
    // v5.161.4 the game's CoreWindow is fetched once on this thread even while the probe is off (the thank-you page's
    // keyboard check, cwo_feedback.inl, reads keys through dp_key)
    if (!g_dp_win_state) {
        g_dp_win_state = -1;
        HSTRING_HEADER hh; HSTRING hs = NULL;
        ABI::Windows::UI::Core::ICoreWindowStatic *st = NULL;
        HMODULE c = LoadLibraryW(L"combase.dll");                // as the gamepad hook: combase at run time (KERNEL32-only import)
        typedef HRESULT(WINAPI *PFN_RF)(HSTRING, REFIID, void **);
        typedef HRESULT(WINAPI *PFN_SR)(PCWSTR, UINT32, HSTRING_HEADER *, HSTRING *);
        PFN_RF rf = c ? (PFN_RF)GetProcAddress(c, "RoGetActivationFactory") : NULL;
        PFN_SR sr = c ? (PFN_SR)GetProcAddress(c, "WindowsCreateStringReference") : NULL;
        if (rf && sr && SUCCEEDED(sr(RuntimeClass_Windows_UI_Core_CoreWindow, (UINT32)wcslen(RuntimeClass_Windows_UI_Core_CoreWindow), &hh, &hs)) &&
            SUCCEEDED(rf(hs, __uuidof(ABI::Windows::UI::Core::ICoreWindowStatic), (void **)&st)) && st) {
            if (SUCCEEDED(st->GetForCurrentThread(&g_dp_win)) && g_dp_win) g_dp_win_state = 1;
            st->Release();
        }
        logf("dream probe: CoreWindow on the overlay thread (tid %lu): %s", GetCurrentThreadId(), g_dp_win_state == 1 ? "YES" : "no");
    }
    // v5.161.7 (in game: keyboard keys moved the main menu under the FEEDBACK / thank-you page, which holds only the pad):
    // the game's key + mouse bindings are held back while that page is open too
    // v5.188.41 (hourly pass 10, in game: 3,345 'held back' / 'back' lines in 15 s on the SINGLE PLAY lobby): ONE decision per
    // frame - 5.188.36 released the bindings here and held them again below every frame (tables rewritten, held keys cleared)
    bool kb_pages = g_dp_win && ed_kb_pages();
    if (!g_dp_on || ed_online()) dp_block_game_input(!g_dp_on && !ed_online() && (g_fb_open || kb_pages));   // v5.150.0 / v5.161.7 / v5.188.36
    if (g_fb_open) fb_page4_keys();                               // v5.161.7 the thank-you page by keyboard (cwo_feedback.inl)
    {   // v5.161.8 keyboard activity for the thank-you page's 'left alone' check (the game's key bits miss menu keys)
        static const int KEYS[] = {VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, VK_RETURN, VK_ESCAPE, VK_SPACE, VK_BACK};
        for (int k : KEYS) if (dp_key(k)) { g_pad_last_ms = GetTickCount64(); break; }
    }
    if (!g_dp_on) {                                               // v5.127: off again (CREATE left) - the game's own cursor back
        if (g_dp_cursor_mode == 1 && g_dp_win) {
            HRESULT hr = g_dp_win->put_PointerCursor(g_dp_game_cursor);
            logf("dream probe: off - the game's cursor back (%08lX)", (unsigned long)hr);
        }
        g_dp_cursor_mode = -1;
        return;
    }
    if (g_dp_win_state != 1) return;
    ABI::Windows::Foundation::Point pt = {0, 0};
    ABI::Windows::Foundation::Rect bd = {0, 0, 0, 0};
    if (FAILED(g_dp_win->get_PointerPosition(&pt)) || FAILED(g_dp_win->get_Bounds(&bd)) || bd.Width <= 0 || bd.Height <= 0) return;
    g_dp_raw[0] = pt.X; g_dp_raw[1] = pt.Y; g_dp_raw[2] = bd.X; g_dp_raw[3] = bd.Y;
    float fx = (pt.X - bd.X) / bd.Width, fy = (pt.Y - bd.Y) / bd.Height;   // PointerPosition is in screen DIPs
    g_dp_mx = fx * 640.0f; g_dp_my = fy * 480.0f;
    g_ui_pfx = fx; g_ui_pfy = fy;                                // v5.124 the neat UI's pointer
    if (!g_dp_cursor_logged) {
        g_dp_cursor_logged = true;
        ABI::Windows::UI::Core::ICoreCursor *cur = NULL;
        HRESULT hr = g_dp_win->get_PointerCursor(&cur);
        logf("dream probe: pointer (%.0f, %.0f) window (%.0f, %.0f, %.0f x %.0f) -> (%.0f, %.0f) of 640 x 480; system cursor %s",
             pt.X, pt.Y, bd.X, bd.Y, bd.Width, bd.Height, g_dp_mx, g_dp_my, SUCCEEDED(hr) && cur ? "shown" : "hidden (the game hides it)");
        if (cur) cur->Release();
    }
    bool lmb = dp_key(1), rmb = dp_key(2), f10 = dp_key(121);
    if (f10 && !g_dp_f10) InterlockedExchange(&g_dp_kbm_editor, !g_dp_kbm_editor);
    g_dp_f10 = f10;
    if (lmb && !g_dp_lmb) {
        g_dp_clicks++;
        InterlockedIncrement(&g_ui_click_seq);
        bool on_button = g_dp_mx >= 470 && g_dp_mx <= 560 && g_dp_my >= 4 && g_dp_my <= 20;
        sprintf_s(g_dp_last, "click %u at (%.0f, %.0f)%s", g_dp_clicks, g_dp_mx, g_dp_my, on_button ? " - Test button!" : "");
        logf("dream probe: %s", g_dp_last);
    }
    g_dp_lmb = lmb; g_dp_rmb = rmb;
    static const struct { int vk; const char *n; } K[] = {{87, "W"}, {65, "A"}, {83, "S"}, {68, "D"}, {81, "Q"}, {69, "E"}, {70, "F"},
        {82, "R"}, {17, "Ctrl"}, {16, "Shift"}, {18, "Alt"}, {32, "Space"}, {13, "Enter"}, {27, "Esc"}};
    g_dp_keys[0] = 0;
    for (const auto &k : K) if (dp_key(k.vk)) { strcat_s(g_dp_keys, k.n); strcat_s(g_dp_keys, " "); }
    if (g_fb_typing) fb_keys();                                   // v5.154 FEEDBACK: the message / name
    else {   // R5: typing by key edges (letters, digits, space, backspace; Shift = capitals)
        bool shift = dp_key(16);
        for (int vk = 8; vk <= 120; vk++) {                       // v5.123.4 up to F9 (120)
            bool alpha = vk >= 0x41 && vk <= 0x5A, digit = vk >= 0x30 && vk <= 0x39;
            if (!(alpha || digit || vk == 8 || vk == 32 || vk == 120)) continue;
            bool d = dp_key(vk);
            if (d && !g_dp_prev_keys[vk] && g_dp_kbm_editor) {
                size_t n = strlen(g_dp_name);
                if (vk == 8) { if (n) g_dp_name[n - 1] = 0; }
                else if (vk == 120) g_dp_stress = !g_dp_stress;                       // F9
                else if (n + 1 < sizeof(g_dp_name)) { g_dp_name[n] = (char)(alpha && !shift ? vk + 32 : vk); g_dp_name[n + 1] = 0; }
            }
            g_dp_prev_keys[vk] = d;
        }
        static LARGE_INTEGER f0, fq;
        LARGE_INTEGER now; QueryPerformanceCounter(&now);
        if (!fq.QuadPart) QueryPerformanceFrequency(&fq);
        // v5.123.4: the overlay hook runs several times per game frame - only calls more than 4 ms apart are frames
        double dt = f0.QuadPart ? (double)(now.QuadPart - f0.QuadPart) / fq.QuadPart : 0;
        if (!f0.QuadPart || dt > 0.004) { if (f0.QuadPart && dt < 1.0) g_dp_fps = g_dp_fps ? g_dp_fps * 0.9 + (1.0 / dt) * 0.1 : 1.0 / dt; f0 = now; }
    }
    cr_keys();                                                    // v5.127 CREATE's shortcuts
    if (g_dp_kbm_editor && !ed_online()) *(volatile DWORD *)(g_base + RVA_KEY_BITS) = 0;   // R4: the game gets no key
    dp_block_game_input(((g_dp_kbm_editor && g_dp_on) || (g_dp_win && ed_kb_pages())) && !ed_online());   // v5.150.0 mouse buttons too, at the source; v5.188.36 + the loader's menu pages (in game: Space went to the game's list under SINGLE PLAY)
    int want = g_dp_kbm_editor ? 1 : 0;
    if (want != g_dp_cursor_mode) {
        if (want && !g_dp_arrow) {
            HSTRING_HEADER hh; HSTRING hs = NULL;
            ABI::Windows::UI::Core::ICoreCursorFactory *cf = NULL;
            HMODULE c = LoadLibraryW(L"combase.dll");
            typedef HRESULT(WINAPI *PFN_RF)(HSTRING, REFIID, void **);
            typedef HRESULT(WINAPI *PFN_SR)(PCWSTR, UINT32, HSTRING_HEADER *, HSTRING *);
            PFN_RF rf = c ? (PFN_RF)GetProcAddress(c, "RoGetActivationFactory") : NULL;
            PFN_SR sr = c ? (PFN_SR)GetProcAddress(c, "WindowsCreateStringReference") : NULL;
            if (rf && sr && SUCCEEDED(sr(RuntimeClass_Windows_UI_Core_CoreCursor, (UINT32)wcslen(RuntimeClass_Windows_UI_Core_CoreCursor), &hh, &hs)) &&
                SUCCEEDED(rf(hs, __uuidof(ABI::Windows::UI::Core::ICoreCursorFactory), (void **)&cf)) && cf) {
                cf->CreateCursor(ABI::Windows::UI::Core::CoreCursorType_Arrow, 0, &g_dp_arrow);
                cf->Release();
            }
            logf("dream probe: arrow cursor %s", g_dp_arrow ? "made" : "NOT made");
        }
        if (want && g_dp_arrow) {
            if (g_dp_game_cursor) { g_dp_game_cursor->Release(); g_dp_game_cursor = NULL; }
            g_dp_win->get_PointerCursor(&g_dp_game_cursor);          // the game's (none while it plays)
            HRESULT hr = g_dp_win->put_PointerCursor(g_dp_arrow);
            logf("dream probe: editor cursor on (%08lX)", (unsigned long)hr);
        } else if (!want) {
            HRESULT hr = g_dp_win->put_PointerCursor(g_dp_game_cursor);
            logf("dream probe: the game's cursor back (%08lX)", (unsigned long)hr);
        }
        g_dp_cursor_mode = want;
    }
}

static void dp_draw() {                                           // render thread, after dp_frame
    if (!g_dp_on || g_dp_win_state != 1) return;
    if (g_ui_state == 1) return;                                 // v5.124 the neat Dream UI draws this frame
    int glyphs = *(int *)(g_base + RVA_GLYPHS), cap = *(int *)(g_base + RVA_GLYPH_CAP);
    (void)glyphs; (void)cap;                                      // v5.123.2: batches flush, no early stop
    if (*(int *)(g_base + RVA_OFS_X) != 0 || *(int *)(g_base + RVA_OFS_Y) != 0) return;
    int *space = (int *)(g_base + RVA_FONT_SPACE), *gap = (int *)(g_base + RVA_FONT_GAP);
    int *fh = (int *)(g_base + RVA_FONT_H), *fw = (int *)(g_base + RVA_FONT_W);
    DWORD *rgba = (DWORD *)(g_base + RVA_FONT_RGBA);
    int s_space = *space, s_gap = *gap, s_h = *fh, s_w = *fw;
    DWORD s_rgba[4];
    memcpy(s_rgba, rgba, sizeof(s_rgba));
    PFN_TEXT text0 = (PFN_TEXT)(g_base + RVA_TEXT);
    PFN_FLUSH flush = (PFN_FLUSH)(g_base + RVA_FONT_FLUSH);
    auto text = [&](int x, int y, const char *t) {              // R6: flush before the queue overflows
        int q = *(int *)(g_base + RVA_GLYPHS), c = *(int *)(g_base + RVA_GLYPH_CAP);
        if (q + (int)strlen(t) > c - 8) { InterlockedExchange(&g_dp_flushing, 1); flush(); InterlockedExchange(&g_dp_flushing, 0); g_dp_flushes++; }
        text0(x, y, t);
    };
    *space = 0; *gap = 2; *fh = 12; *fw = 6;
    // the Dream Bar strip and the test button
    game_box(0, 0, 640, 24, 0xE0141619);
    game_box(0, 23, 640, 1, 0xFFB08530);
    set_rgba(rgba, 0xFFF0BF55); text(8, 6, "CREATE");
    set_rgba(rgba, 0xFFCFC6B2); text(60, 6, "Dream input test  -  mouse, keys and pad routing");
    bool hover = g_dp_mx >= 470 && g_dp_mx <= 560 && g_dp_my >= 4 && g_dp_my <= 20;
    game_box(470, 4, 90, 16, hover ? 0xFF6A5220 : 0xFF2A2D35);
    set_rgba(rgba, hover ? 0xFFFFE2A0 : 0xFFDDD5C4); text(478, 6, "Test button");
    // the readout panel
    char line[96];
    game_box(8, 32, 236, 92, 0xD0111318);
    game_box(8, 32, 236, 14, 0xFF1D2028);
    set_rgba(rgba, 0xFFF0BF55); text(14, 33, "ATTRIBUTES  (probe)");
    set_rgba(rgba, 0xFFE9E3D6);
    sprintf_s(line, "Cursor  %.0f, %.0f   %s%s", g_dp_mx, g_dp_my, g_dp_lmb ? "[L] " : "", g_dp_rmb ? "[R]" : ""); text(14, 50, line);
    sprintf_s(line, "Keys    %.30s", g_dp_keys[0] ? g_dp_keys : "-"); text(14, 64, line);
    sprintf_s(line, "Clicks  %.40s", g_dp_last[0] ? g_dp_last : "none yet"); text(14, 78, line);
    sprintf_s(line, "Pad     %04X%s", g_dp_pad_buttons, g_dp_pad_combo ? "  L3+R3" : ""); text(14, 92, line);
    set_rgba(rgba, 0xFF9A9384); text(14, 106, "F10 keys>fighter  L3+R3 pad  F9 stress");
    game_box(8, 128, 236, 46, 0xD0111318);
    set_rgba(rgba, 0xFFF0BF55); text(14, 131, "Name");
    game_box(50, 129, 186, 14, 0xFF0D0F13);
    set_rgba(rgba, 0xFFF2EAD8); sprintf_s(line, "%s%s", g_dp_name, ((GetTickCount64() / 400) & 1) ? "_" : ""); text(54, 131, line);
    set_rgba(rgba, 0xFF9A9384); sprintf_s(line, "%.0f fps   flushes %u", g_dp_fps, g_dp_flushes); text(14, 150, line);
    if (g_dp_stress) {                                            // R6: ~3,500 glyphs in the right half
        set_rgba(rgba, 0xFFBDB5A3);
        for (int r = 0; r < 40; r++) text(256, 30 + r * 10, "Sein Ring  Island Rock  Rock Bridge  Perch Lift  Plate A ");
    }
    // the routing badge
    game_box(392, 456, 240, 18, 0xD00A0B0E);
    set_rgba(rgba, 0xFFFFD77A);
    sprintf_s(line, "PAD > %s   KEYS+MOUSE > %s", g_dp_pad_editor ? "EDITOR" : "FIGHTER", g_dp_kbm_editor ? "EDITOR" : "FIGHTER");
    text(398, 459, line);
    // the cursor: a gold arrow-like mark
    if (g_dp_mx >= 0 && g_dp_my >= 0 && g_dp_mx < 640 && g_dp_my < 480) {
        int x = (int)g_dp_mx, y = (int)g_dp_my;
        game_box(x, y, 2, 12, 0xFFFFD77A);
        game_box(x, y, 9, 2, 0xFFFFD77A);
        game_box(x + 2, y + 2, 5, 5, 0xC0FFD77A);
    }
    *space = s_space; *gap = s_gap; *fh = s_h; *fw = s_w;
    memcpy(rgba, s_rgba, sizeof(s_rgba));
}
