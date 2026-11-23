// CWO-LOADER-01 v0.1 - our own in-game loader for Phantom Dust (PDUWP.exe). Independently authored 2026-10-23.
// Provenance: written from the original game's own facts (Ghidra batches recorded in MODLOG) and official Windows
// documentation (DllMain, CreateThread, XInput, GetTempPath). No community loader code, structure or design used.
//
// v0.1 features (owner: get the loader running, then the stage creator):
//   - host guard: runs only inside the exact PDUWP.exe build it was made for (code bytes checked), else stays idle;
//   - log file + marker file in the game's writable temp folder (<TEMP>\CwoLoader\);
//   - stage-creator marker tool (controller): hold BACK/View and press
//         A = spawn marker at player 1's position      B = kill-zone point
//         X = delete the last marker                    Y = write the current position to the log only
//         RB = capsule spot (v0.2)                     LB = pin / unpin the on-screen panel (v0.3)
//     each action answers with a short rumble (two pulses for delete).
// v0.3: on-screen panel in the game's own debug font (Ghidra batch_067b-e): the patched exe calls CwoOverlayFrame
//   right before every debug-text flush FUN_14028caa0 (tools/add_overlay_hook.py); lines are queued with the game's
//   FUN_14028d240(x, y, text). Shows the controls, live coordinates, marker counts and the last action.
// Game facts used (CONFIRMED in Ghidra, MODLOG / Batch 026-029, STAGE_SKILL_CAPSULE_SPEC):
//   actor table 0x1404bc9d8 (index 0 = player 1), active when the entry is non-null and actor+0x20 != 0,
//   position f32[3] at actor+0x12E40 (written by player init 0x14024e190), current stage number 0x1403e0304,
//   stage file requester entry 0x1401d85f0 (used as the build check).
// v2.0 FORGE MODE (src/cwo_forge.inl): a FORGE MODE item on the main menu (native 2.8 + Single Play), and in its
//   matches a Forge-style editor: object menu, grab / place / delete / copy / lock, turn with snap, saved maps.
// v0.2: the game reads its controllers through Windows.Gaming.Input, not XInput (Ghidra batch_067a: FUN_1400deb80
//   activates "Windows.Gaming.Input.Gamepad" with IGamepadStatics; no XInput import), so XInput rumble from v0.1 was
//   accepted but never reached the pad. Rumble now goes through Windows.Gaming.Input (official WinRT ABI headers).
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <math.h>
#include <map>
#include <string>
#include <roapi.h>
#include <winstring.h>
#include <windows.gaming.input.h>

using ABI::Windows::Gaming::Input::IGamepad;
using ABI::Windows::Gaming::Input::IGamepadStatics;
using ABI::Windows::Gaming::Input::GamepadVibration;

#define CWO_VERSION "5.204.2"

static const ULONG_PTR RVA_ACTORS = 0x4bc9d8;
static const ULONG_PTR RVA_STAGE = 0x3e0304;
static const ULONG_PTR RVA_CHECK = 0x1d85f0;
static const unsigned char CHECK_BYTES[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57};
static const ULONG_PTR ACTOR_ACTIVE = 0x20;
static const ULONG_PTR ACTOR_POS = 0x12E40;
// debug-font text (Ghidra batch_067b-e): FUN_14028d240(x, y, text) queues characters; font state globals below
static const ULONG_PTR RVA_TEXT = 0x28d240;
static const ULONG_PTR RVA_GLYPHS = 0x54c964, RVA_GLYPH_CAP = 0x54c978;          // queued count, capacity (1000)
static const ULONG_PTR RVA_GLYPH_PTR = 0x54c980, RVA_GLYPH_BUF = 0x54c988;       // v5.36 the queue's write pointer and its start (FUN_14028c9d0)
static const ULONG_PTR RVA_OFS_X = 0x54c97c, RVA_OFS_Y = 0x54c990;              // flush screen offset
static const ULONG_PTR RVA_FONT_SPACE = 0x54c994, RVA_FONT_GAP = 0x54c9a4, RVA_FONT_H = 0x54c9a8, RVA_FONT_W = 0x54c9ac;
static const ULONG_PTR RVA_FONT_RGBA = 0x54c9b0, RVA_FONT_X0 = 0x54c9c0;       // 4 corner colours, line start x
// v5.36 the game's own see-through box (Ghidra batch_065: the menu window draw FUN_140249420 calls FUN_14028e4e0(0x5401),
// then FUN_14029bb60(x, y, w, h, 0x80000000) for its backing, then its text and the flush; the rectangle is drawn at once
// with the colour's alpha - the debug font's glyphs ignore it)
static const ULONG_PTR RVA_RECT = 0x29bb60, RVA_BLEND = 0x28e4e0;
typedef void (*PFN_BLEND)(unsigned);
typedef void (*PFN_RECT)(int, int, int, int, DWORD);
static const DWORD BOX_GREY = 0xA0383838;                                      // dark grey, about 63 % (owner)
// stage object instances (Ghidra batch_068a-c): spawner FUN_140203dc0 fills DAT_1405322c8[DAT_1405322bc]; each entry
// is the object's base part; method +0x158 (FUN_1401da000 for pdStageObjRep01) stores the position at +0x1E0
static const ULONG_PTR RVA_INSTANCES = 0x5322c8, RVA_INSTANCE_COUNT = 0x5322bc;
static const ULONG_PTR OBJ_POS = 0x1E0, VT_SET_POSITION = 0x158;
// the spawner walks 2 placement tables (row pointers DAT_1405322e8[2], row counts u16 DAT_1405322d4[2]); instance n is
// row n in that order (0x24-byte rows, +0 = object index); object names come from the stage file (NM00, header +0x5C,
// count +0x60; build_st32_objects / STAGE_OBJECTS_AND_PLACEMENT_BATCH031)
static const ULONG_PTR RVA_ROW_TABLES = 0x5322e8, RVA_ROW_COUNTS = 0x5322d4;

typedef struct { WORD wButtons; BYTE bLeftTrigger, bRightTrigger; SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY; } XPAD;
typedef struct { DWORD dwPacketNumber; XPAD Gamepad; } XSTATE;
typedef struct { WORD wLeftMotorSpeed, wRightMotorSpeed; } XVIB;
typedef DWORD(WINAPI *PFN_GETSTATE)(DWORD, XSTATE *);
typedef DWORD(WINAPI *PFN_SETSTATE)(DWORD, XVIB *);
enum { BTN_UP = 0x0001, BTN_DOWN = 0x0002, BTN_LEFT = 0x0004, BTN_RIGHT = 0x0008, BTN_START = 0x0010, BTN_BACK = 0x0020, BTN_LTHUMB = 0x0040, BTN_RTHUMB = 0x0080, BTN_LB = 0x0100, BTN_RB = 0x0200, BTN_A = 0x1000, BTN_B = 0x2000, BTN_X = 0x4000, BTN_Y = 0x8000 };

static wchar_t g_dir[MAX_PATH];
static bool g_bfr_edition;                                      // v5.58 started as the BFR mod pack (CwoLaunch --edition BFR)
static ULONG_PTR g_base;
// v5.0 six fighters: the per-fighter tables may have moved to 8-entry copies (cwo_sixf.inl); the loader reads them here
static ULONG_PTR g_six_tab[3];
static void game_box(int x, int y, int w, int h, DWORD col) {                  // v5.36 the game's see-through rectangle
    ((PFN_BLEND)(g_base + RVA_BLEND))(0x5401);
    ((PFN_RECT)(g_base + RVA_RECT))(x, y, w, h, col);
}
static ULONG_PTR *cwo_actors() { return (ULONG_PTR *)(g_six_tab[0] ? g_six_tab[0] : g_base + 0x4bc9d8); }
static ULONG_PTR *cwo_bdata() { return (ULONG_PTR *)(g_six_tab[1] ? g_six_tab[1] : g_base + 0x4bc608); }
static int cwo_nfighters() { return g_six_tab[0] ? 8 : 4; }  // entries in the fighter tables
static int g_sixf2_state;                                   // v5.1 step 2: 0 off, 1 applied, -1 refused (cwo_sixf2.inl)
static ULONGLONG g_sh_drawn;                                // v5.7 last time the game's panels showed fighters 5 / 6 (cwo_sixhud.inl)
static volatile LONG g_card_id; static volatile LONG g_card_x, g_card_y;   // v5.8 the Skill Creator's card: the game's own (cwo_sixhud.inl)
static volatile ULONGLONG g_card_want, g_card_drawn;
static ULONG_PTR cwo_gaddr(ULONG_PTR rva);                  // v5.1 a game store wherever it is now (cwo_sixf2.inl)
static char *cwo_caps(ULONG_PTR off);                       // v5.1 a capsule-table field by its old offset
// v5.0 (owner): how the match was started, *0x1404bca04 (Ghidra batch_065:
// 1 Single Play / offline Multiplayer (FUN_140192a50, FUN_1401d0900), 2-3 online (FUN_140187710), 5 a story mission from
// the quest data (FUN_140214770), 6 a story-started battle (FUN_140212720), 4 FUN_1401d0750). Story matches: no quick panel.
static bool cwo_story_match() {
    __try { int o = *(int *)(g_base + 0x4bca04); return o == 5 || o == 6; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static PFN_GETSTATE g_get;
static PFN_SETSTATE g_set;
static PFN_SETSTATE g_set2;                     // second controller library, tried when the first cannot rumble
static int g_rumble_logged;
static IGamepadStatics *g_pads;                 // Windows.Gaming.Input, the controller system the game itself uses
static CRITICAL_SECTION g_log_lock;             // log writes come from several threads
// on-screen panel state: written by the worker thread, read by the game's render thread in CwoOverlayFrame
static volatile LONG g_ready, g_pinned;
#ifdef CWO_EXPIRES
static volatile LONG g_expired;                                // v5.37 partner build past its date (cwo_expiry.inl)
static INT64 ex_native(int op, INT64 *args);
#endif
static volatile LONG g_counts[3];               // spawns, capsules, kill-zone points in the current stage's marker file
static CRITICAL_SECTION g_ui_lock;
static char g_status[96];
static ULONGLONG g_status_until;
static volatile LONG g_pull_request;             // D-pad Up (panel on): bring the selected (or nearest) object to player 1
static volatile LONG g_return_request;           // D-pad Down (panel on): send the selected (or last moved) object back
static float g_orig[1024][3];                    // where each moved instance stood when the stage loaded
static bool g_has_orig[1024];
static int g_last_moved = -1;
static volatile LONG g_sel = -1;                 // selected instance (-1 = nearest); BACK+D-pad left/right browses
static char g_names[256][32];                    // object names of the current stage file (NM00)
static int g_name_count = -1;

static void file_monitor_install();
static void file_requests_flush();
static int g_hooked;                             // v0.8 file monitor: imports watched
static bool g_forge_native_ok;                   // v2.0: native 2.8 installed

static void logf(const char *fmt, ...) {
    EnterCriticalSection(&g_log_lock);
    wchar_t path[MAX_PATH];
    swprintf_s(path, MAX_PATH, L"%scwo_loader.log", g_dir);
    FILE *f = NULL;
    for (int i = 0; i < 20 && (_wfopen_s(&f, path, L"a") != 0 || !f); i++) Sleep(5);
    if (!f) { LeaveCriticalSection(&g_log_lock); return; }
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(f, "%04d-%02d-%02d %02d:%02d:%02d.%03d ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list a;
    va_start(a, fmt);
    vfprintf(f, fmt, a);
    va_end(a);
    fputc('\n', f);
    fclose(f);
    LeaveCriticalSection(&g_log_lock);
}

#include "cwo_storemode.inl"                             // v5.38 Store mode: memory patch + file overrides

static bool host_ok() {
    __try {
        return memcmp((const void *)(g_base + RVA_CHECK), CHECK_BYTES, sizeof(CHECK_BYTES)) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool read_player(int idx, float pos[3], int *stage) {
    __try {
        ULONG_PTR actor = cwo_actors()[idx];
        if (!actor || *(int *)(actor + ACTOR_ACTIVE) == 0) return false;
        const float *p = (const float *)(actor + ACTOR_POS);
        pos[0] = p[0]; pos[1] = p[1]; pos[2] = p[2];
        *stage = *(int *)(g_base + RVA_STAGE);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

typedef HRESULT(WINAPI *PFN_ROINIT)(RO_INIT_TYPE);
typedef HRESULT(WINAPI *PFN_ROFACTORY)(HSTRING, REFIID, void **);
typedef HRESULT(WINAPI *PFN_STRREF)(PCWSTR, UINT32, HSTRING_HEADER *, HSTRING *);

// same activation the game performs (FUN_1400deb80), loaded at run time so the DLL still imports only KERNEL32
static void pads_init() {
    HMODULE c = LoadLibraryW(L"combase.dll");
    PFN_ROINIT ro_init = c ? (PFN_ROINIT)GetProcAddress(c, "RoInitialize") : NULL;
    PFN_ROFACTORY ro_factory = c ? (PFN_ROFACTORY)GetProcAddress(c, "RoGetActivationFactory") : NULL;
    PFN_STRREF str_ref = c ? (PFN_STRREF)GetProcAddress(c, "WindowsCreateStringReference") : NULL;
    if (!ro_init || !ro_factory || !str_ref) { logf("Windows.Gaming.Input: runtime functions missing"); return; }
    ro_init(RO_INIT_MULTITHREADED);                 // this worker thread only; "already initialised" results are fine
    static const wchar_t name[] = L"Windows.Gaming.Input.Gamepad";
    HSTRING_HEADER hh;
    HSTRING hs = NULL;
    HRESULT hr = str_ref(name, (UINT32)(sizeof(name) / sizeof(name[0]) - 1), &hh, &hs);
    if (SUCCEEDED(hr)) hr = ro_factory(hs, __uuidof(IGamepadStatics), (void **)&g_pads);
    logf("Windows.Gaming.Input gamepads %s (0x%08lx)", g_pads ? "ready" : "NOT available", (unsigned long)hr);
}

// vibrate the first gamepad Windows.Gaming.Input lists; returns the HRESULT, S_FALSE when no pad is listed
static HRESULT pad_vibrate(double strength, unsigned *count) {
    *count = 0;
    if (!g_pads) return E_POINTER;
    ABI::Windows::Foundation::Collections::IVectorView<ABI::Windows::Gaming::Input::Gamepad *> *list = NULL;
    HRESULT hr = g_pads->get_Gamepads(&list);
    if (FAILED(hr) || !list) return hr;
    list->get_Size(count);
    if (*count == 0) { list->Release(); return S_FALSE; }
    IGamepad *pad = NULL;
    hr = list->GetAt(0, &pad);
    if (SUCCEEDED(hr) && pad) {
        GamepadVibration v = {strength, strength, 0.0, 0.0};
        hr = pad->put_Vibration(v);
        pad->Release();
    }
    list->Release();
    return hr;
}

// v0.4 input filter (owner: with the panel on, hide the creator's buttons from the game so they work alone).
// The game reads its pad with IGamepad::GetCurrentReading (method 8, +0x40, the call pattern in Ghidra batch_069b),
// on the same Gamepad objects Windows.Gaming.Input lists for us. Each listed pad gets its method-table pointer swapped
// for a copy whose GetCurrentReading entry runs the original and then clears the hidden buttons. No game code changes;
// the loader's own input comes from XInput and is not affected.
// GamepadButtons (Windows SDK): Menu 0x1, View 0x2, A 0x4, B 0x8, X 0x10, Y 0x20, D-pad 0x40/0x80/0x100/0x200,
// LeftShoulder 0x400 (jump stays), RightShoulder 0x800, sticks 0x1000/0x2000.
static const UINT32 GB_MENU = 0x1, GB_VIEW = 0x2, GB_CREATOR = 0x1 | 0x4 | 0x8 | 0x10 | 0x20 | 0x40 | 0x80 | 0x100 | 0x200 | 0x800 | 0x2000;
static volatile LONG g_in_match;
static volatile ULONGLONG g_pad_last_ms;                    // v5.161.4 the pad's last held button (cwo_feedback.inl)
static volatile LONG g_page;                     // panel page: 0 objects, 1 stage packs, 2 fly (right-stick click cycles)
static volatile float g_stick_mag, g_trig_up, g_trig_down;   // raw left stick strength and triggers (loader's own read)
static volatile bool g_flying;                     // fly page active (see fly_step)
static volatile bool g_fly_landed;                 // D-pad Down on the fly page: stop flying until pressed again
static volatile bool g_hide_hud;                   // fly page D-pad Up: hide the game's battle HUD (v1.1)
static const unsigned FX_NONE = 0xFFFFFFFF;          // v1.2 aura FX (see fx_step)
static volatile bool g_fx_on, g_fx_always;         // skills page: D-pad Down on / off, B "always" vs "with the aura"
static volatile LONG g_fx_track, g_fx_change;      // A: next part (track); worker -> game thread: re-start
static volatile LONG g_fx_skill = -1;
static unsigned g_fx_handle = FX_NONE;
static volatile LONGLONG g_view_since;           // GetTickCount64 when View went down (0 = up), from XInput
static volatile LONG g_forge_session, g_forge_active, g_fprecise, g_fmenu, g_fmenu_sel;
static ULONGLONG g_sel_live_seen;                                 // v5.87 the Xbox Live select grid asked op 78 (cwo_widemenu.inl)
static ULONGLONG g_so_seen;
static volatile LONG g_cb_cx = -1;                                // v5.88.4 the open COLORS window's x (menu units), -1 none                                       // v5.86.4 the last COLORS row draw (op 79, cwo_selout.inl)
static volatile LONG g_tbv_show;                                 // v5.86 the tiebreaker vote window (cwo_bfrnet.inl)
static volatile LONG g_ed_wait_release, g_ed_game_hold;   // v3.5.1: buttons held when the chooser opens / closes
static volatile LONG g_cc_prev, g_cc_dirty, g_cc_redirect;
// v5.10.1 (owner 2026-10-26): while 1, a View tap belongs to the game (filtered_read lets it through)
static volatile LONG g_game_view;
// v5.11 Download Battle choices (cwo_dbtl.inl): 0 idle, 1 the choice page is up (it owns the pad), 2 / 3 start, 4 starting
static volatile LONG g_db_state;
// v3.9 Try it now (owner):
static volatile LONG g_ed_try_page, g_ed_auto, g_ed_live, g_fexit_to_ed;
static volatile LONG g_slot_on;                            // v4.1 the select screen's cursor is on the created-character cell
static volatile LONG g_slot_drop;                          // v4.3 the list of created characters is open
static volatile LONG g_fexit_giveup;                       // v4.6 the exit asks the game thread for the game's Give Up
static volatile LONGLONG g_slot_pass_until;                // v4.3 A handed to the game until then  // v3.7 Create a Character live preview (cwo_ccprev.inl)
static volatile LONG g_six_auto;
static volatile LONG g_fc_on;                               // v5.9 free camera on (cwo_freecam.inl): the pad flies the camera
static volatile LONG g_ph_pause_req, g_ph_speed_req;        // v5.20 photo mode: LB pause, D-pad speed steps (cwo_freecam.inl)
static bool ars_list_focused();                             // v5.9 an arsenal list window is focused (cwo_arsenal.inl)
static INT64 ars_native(int op, INT64 *args);
static INT64 ls_native(int op, INT64 *args);                // v5.16 SESSION INFO's "This Session" text (cwo_livesess.inl)
static INT64 gt_native(int op, INT64 *args);                // v5.18 the split-screen lobby's game type (cwo_gametype.inl)
static INT64 on_native(INT64 *args);                        // v5.19 the COLORS list's outfit names (cwo_outfitnames.inl)
static void rc_on_skill(ULONG_PTR action, int id);          // v5.21 Records: a skill move started (cwo_records.inl)
static volatile LONG g_co_prev;                             // v5.61 Create an Outfit preview state (cwo_outfit.inl)
static void co_input(WORD b, WORD pressed);
static int g_th_popup;                                      // v5.74 cwo_holiday.inl
static volatile LONG g_ml_open;
static volatile LONG g_fb_open, g_fb_typing;                 // v5.154 cwo_feedback.inl
static void fb_keys();                             // v5.80 cwo_modlist.inl
static void ml_input(WORD pressed);
static void ml_open();
static void th_input(WORD pressed);
static volatile LONG g_co_stick;                            // v5.69 Create an Outfit preview: the right stick turns the model
static volatile float g_co_rx;
static volatile float g_fc_axis[6];                         // its sticks and triggers: lx, ly, rx, ry, lt, rt (read by the game)
static volatile LONG g_scp_loop;                             // v5.32 the Skill Creator's R3 loop: the sticks turn its camera
static volatile float g_scp_axis[6];                         // its sticks and triggers (as g_fc_axis)
static volatile UINT32 g_fc_btn;                            // and its buttons (Windows.Gaming.Input bits)
static volatile LONG g_six_match;                        // v5.1 this match has fighters 5-6 (cwo_sixf2.inl)
static volatile LONG g_l3;                               // v5.3 the 3 VS 3 LOBBY in the game's lobby: 0 off .. 3 open (cwo_lobby.inl)
static INT64 lob3_native(int op, INT64 *args);
static volatile LONG g_lob_go;                           // v5.2 a 3 VS 3 LOBBY match is on its way / running (cwo_lobby.inl)
static volatile LONG g_lob_go_n = 6;                        // v5.152 SINGLE PLAY: the lobby match's fighters (6, 4 or 2)
static volatile LONG g_sp_boss_row = -1;                     // v5.152 SINGLE PLAY Boss Raid: the battle-list row to take (-1 none)
static int g_lob_stage = -1, g_lob_var;                  // v5.2 the lobby's stage list row and variant                         // v5.1.2 a 3 vs 3 from Create a Mode: 1 your stage + arsenal, 2 the battle list passed
static volatile LONG g_vt_shown, g_vt_req;                  // v5.136 g_vt_req: the Lab's View Skill Vault row (native 2.8 op 115)
static volatile LONG g_ed_open, g_ed_mode;                // v3.3 Edgar's Dream chooser (open), what was chosen (0 stage, 1 skill)   // v2.0 Forge mode (cwo_forge.inl)
static const UINT32 GB_FORGE = 0x3FFE;             // Forge match: every button but Menu belongs to the editor
// v2.2 (owner: the game's pause menu could not be used in Stage Creator): the game's own menus run while
// *0x140533744 != 3 (FUN_140246020 sets 3 on leaving the menus; battle logic tests == 3, e.g. 0x140129ac0, 0x140150640);
// then the editor takes no buttons and hides none. Start opens the Stage Creator pause menu instead of the game's.
static volatile LONG g_game_menu, g_ftesting;
static volatile LONG g_ap_lx, g_ap_ly;              // v5.122 test helper: CwoAutoPad.txt LX= / LY= (x1000): the LEFT stick (walk / steer checks)
static volatile LONG g_ap_lt, g_ap_rt;                       // v5.188.20 test helper: CwoAutoPad.txt LT= (x1000) with MS= - fly down in the editor
static volatile LONG g_ap_rx, g_ap_ry;              // v5.118 test helper: CwoAutoPad.txt RX= / RY= (x1000), held until
static volatile LONGLONG g_ap_stick_until;          //   g_ap_stick_until (offline only, as the rest of the auto pad)
static volatile LONG g_inject;                      // buttons the loader holds down for the owner (exit: forfeit, B taps)
static volatile LONGLONG g_inject_until;
typedef HRESULT(STDMETHODCALLTYPE *PFN_READ)(void *, ABI::Windows::Gaming::Input::GamepadReading *);
static PFN_READ g_orig_read;
static void *g_vtbl_copy[16];
static int g_filter_logged;

static void el_off(const char *why);
static bool el_game_menu() {                                // v5.194.0 a game menu (pause, Give Up box) is up: its buttons are the game's
    __try {                                                 // (the battle's menu machine, as forge_poll: FUN_140167580 modes 9..14)
        if (*(int *)(g_base + 0x533744) != 3) return true;
        ULONG_PTR bm = *(ULONG_PTR *)(g_base + 0x4c0130);
        return bm && *(int *)(bm + 0x50) >= 9;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static volatile LONG g_el_lt, g_el_rt;                       // v5.198.0 Effect Lab: the triggers (x1000), read from the pad
static volatile LONG g_el_white, g_el_restart;               // v5.196.0 Effect Lab: white background / reload the match (cwo_effectlab.inl)
static volatile LONG g_el, g_el_panel;                      // v5.194.0 the Effect Lab is on / its panel has the buttons (cwo_effectlab.inl)
static void dp_pad(ABI::Windows::Gaming::Input::GamepadReading *r);   // v5.123 cwo_dreamprobe.inl
static HRESULT STDMETHODCALLTYPE filtered_read(void *self, ABI::Windows::Gaming::Input::GamepadReading *r) {
    HRESULT hr = g_orig_read(self, r);
    if (SUCCEEDED(hr) && r && GetTickCount64() < (ULONGLONG)g_ap_stick_until) {   // v5.118 test helper: the auto pad holds the
        r->RightThumbstickX = g_ap_rx / 1000.0;                                   // right stick (camera turn checks)
        r->RightThumbstickY = g_ap_ry / 1000.0;
        if (g_ap_lx || g_ap_ly) { r->LeftThumbstickX = g_ap_lx / 1000.0; r->LeftThumbstickY = g_ap_ly / 1000.0; }   // v5.122
        if (g_ap_lt || g_ap_rt) { r->LeftTrigger = g_ap_lt / 1000.0; r->RightTrigger = g_ap_rt / 1000.0; }          // v5.198.0 test pad triggers
    }
    static UINT32 ed_latch;                                     // buttons held when the chooser closed: kept back until released
    if (SUCCEEDED(hr) && r && ((g_ed_open && g_forge_session && !g_forge_active) || (g_vt_shown && g_ed_open) || g_cc_prev == 2 || g_co_prev == 2 || g_db_state == 1 || g_th_popup == 2 || g_ml_open || g_fb_open || (g_tbv_show && !g_in_match))) {   // v3.7: + the preview, v5.11 + Download Battle
        // v3.3.1 (owner): the Edgar's Dream chooser sits on the menus,
        // where g_in_match is 0, so it holds back the game's buttons and stick here
        ed_latch = (UINT32)r->Buttons & (GB_FORGE | 0x1);
        r->Buttons = (ABI::Windows::Gaming::Input::GamepadButtons)((UINT32)r->Buttons & ~(GB_FORGE | 0x1));
        r->LeftThumbstickX = r->LeftThumbstickY = 0.0;
        r->RightThumbstickX = r->RightThumbstickY = 0.0;
    } else if (SUCCEEDED(hr) && r && ed_latch) {                // the A / B that closed it must not also press the list
        ed_latch &= (UINT32)r->Buttons;
        r->Buttons = (ABI::Windows::Gaming::Input::GamepadButtons)((UINT32)r->Buttons & ~ed_latch);
    }
    if (SUCCEEDED(hr) && r && g_ed_game_hold && !g_ed_open) {
        // v3.5.1 (owner): the chooser closed on the
        // loader's thread between two game reads, so its A could reach the list; the game gets no button until all are up
        if ((UINT32)r->Buttons & (GB_FORGE | 0x1)) r->Buttons = (ABI::Windows::Gaming::Input::GamepadButtons)((UINT32)r->Buttons & ~(GB_FORGE | 0x1));
        else InterlockedExchange(&g_ed_game_hold, 0);
    }
    if (SUCCEEDED(hr) && r && g_in_match && g_scp_loop) {        // v5.32 the R3 loop: the fighter stays, the sticks turn the camera
        g_scp_axis[0] = (float)r->LeftThumbstickX; g_scp_axis[1] = (float)r->LeftThumbstickY;
        g_scp_axis[2] = (float)r->RightThumbstickX; g_scp_axis[3] = (float)r->RightThumbstickY;
        g_scp_axis[4] = (float)r->LeftTrigger; g_scp_axis[5] = (float)r->RightTrigger;
        r->LeftThumbstickX = r->LeftThumbstickY = r->RightThumbstickX = r->RightThumbstickY = 0.0;
        r->LeftTrigger = r->RightTrigger = 0.0;
    }
    if (SUCCEEDED(hr) && r && g_co_stick) g_co_rx = (float)r->RightThumbstickX;   // v5.69
    if (SUCCEEDED(hr) && r && g_el && g_in_match && !g_forge_session) {   // v5.198.0 Effect Lab: LT / RT = the pace (pdpEffect's RTRIGGER)
        InterlockedExchange(&g_el_lt, (LONG)(r->LeftTrigger * 1000.0)); InterlockedExchange(&g_el_rt, (LONG)(r->RightTrigger * 1000.0));
        if (g_el_panel) { r->LeftTrigger = 0.0; r->RightTrigger = 0.0; }
    }
    if (SUCCEEDED(hr) && r && g_in_match && g_fc_on) {           // v5.9 free camera: the pad is the camera's, Start the game's
        g_fc_axis[0] = (float)r->LeftThumbstickX; g_fc_axis[1] = (float)r->LeftThumbstickY;
        g_fc_axis[2] = (float)r->RightThumbstickX; g_fc_axis[3] = (float)r->RightThumbstickY;
        g_fc_axis[4] = (float)r->LeftTrigger; g_fc_axis[5] = (float)r->RightTrigger;
        g_fc_btn = (UINT32)r->Buttons;
        r->LeftThumbstickX = r->LeftThumbstickY = r->RightThumbstickX = r->RightThumbstickY = 0.0;
        r->LeftTrigger = r->RightTrigger = 0.0;
        r->Buttons = (ABI::Windows::Gaming::Input::GamepadButtons)((UINT32)r->Buttons & 0x1u);
        return hr;
    }
    if (SUCCEEDED(hr) && r && g_in_match) {
        UINT32 bt = (UINT32)r->Buttons, hide = 0;
        if (g_pinned && !g_forge_session && !cwo_story_match()) hide |= GB_CREATOR | (g_game_view ? 0 : GB_VIEW);   // panel on: every creator button is the loader's
        if (g_forge_active && !g_game_menu) {
            if (g_ftesting) hide |= 0x1 | (g_ed_mode == 1 ? 0x1000u : 0u)   // testing: buttons play, Start = menu (v5.33 L3 = HUD in Create a Skill)
                                    | (g_ed_mode == 1 && !g_fmenu ? 0x2038u : 0u);   // v5.188.49 B play, X undo, Y redo, R3 loop are the test's
            else {                                              // building: every button is the editor's
                hide |= GB_FORGE | 0x1;
                r->LeftTrigger = 0.0;
                r->RightTrigger = 0.0;
                if (g_fmenu) { r->LeftThumbstickX = r->LeftThumbstickY = 0.0; }   // menus open: the monitor stays put
            }
        }
        if (g_flying) {                                         // flying (panel shown or hidden): triggers climb / sink
            r->LeftTrigger = 0.0;
            r->RightTrigger = 0.0;
        }
        LONGLONG since = g_view_since;
        // taps (v5.10.1: not in the mulligan window - the fighter exists during 3-2-1, so the loader counted it as the match
        // and hid every View tap from the game; 5.5.0 had only kept the panel from opening)
        if ((bt & GB_VIEW) && !g_game_view && (since == 0 || GetTickCount64() - (ULONGLONG)since < 700)) hide |= GB_VIEW;
        if ((bt & GB_VIEW) && (bt & 0x400)) hide |= 0x400;      // v3.5 View + LB: the CPU switch, not the game's LB
        if ((bt & GB_VIEW) && (bt & 0x800)) hide |= 0x800;      // v5.9 View + RB: the free camera, not the game's lock-on
        if (g_el && !g_forge_session && !el_game_menu()) hide |= g_el_panel ? 0x3FFEu : 0x2002u;   // v5.194.0 Effect Lab: the list has the buttons (Start and the sticks stay the game's)
        r->Buttons = (ABI::Windows::Gaming::Input::GamepadButtons)(bt & ~hide);
    }
    if (SUCCEEDED(hr) && r && !g_in_match && ((UINT32)r->Buttons & 0x300u) && ars_list_focused())   // v5.9 D-pad Left / Right:
        r->Buttons = (ABI::Windows::Gaming::Input::GamepadButtons)((UINT32)r->Buttons & ~0x300u);    // the arsenal list's, not the game's
    if (SUCCEEDED(hr) && r && (g_slot_on || g_slot_drop)) {  // v4.1: LB / RB switch the created characters, not the game's
        UINT32 m = 0xC00u;
        if (g_slot_drop) m |= 0x3CCu;                        // v4.3 the list open: A, B and the D-pad are the list's
        else if (GetTickCount64() > (ULONGLONG)g_slot_pass_until) m |= 0x4u;   // A on the cell opens the list
        r->Buttons = (ABI::Windows::Gaming::Input::GamepadButtons)((UINT32)r->Buttons & ~m);
    }
    if (SUCCEEDED(hr) && r && g_inject && GetTickCount64() < (ULONGLONG)g_inject_until)
        r->Buttons = (ABI::Windows::Gaming::Input::GamepadButtons)((UINT32)r->Buttons | (UINT32)g_inject);
    if (SUCCEEDED(hr)) dp_pad(r);                                // v5.123.2 L3+R3 pad routing, after the loader's own presses
    return hr;
}

// swap one object's method table for the filtered copy (the copy is made from the first object seen)
static bool attach_filter(void ***obj) {
    if (*obj == g_vtbl_copy) return false;
    if (!g_orig_read) {
        memcpy(g_vtbl_copy, *obj, sizeof(g_vtbl_copy));
        g_orig_read = (PFN_READ)g_vtbl_copy[8];
        g_vtbl_copy[8] = (void *)filtered_read;
    }
    if ((*obj)[8] != (void *)g_orig_read) return false;       // a different class: leave it alone
    *obj = g_vtbl_copy;
    return true;
}

// v0.4.1: inside the UWP app XInput is served from the same Gamepad reading, so it is filtered too (owner test:
// the BACK tap never reached the loader, so no panel). The loader reads the first pad through the ORIGINAL method
// instead, and maps Windows.Gaming.Input bits to the XInput-style bits used below.
static IGamepad *g_pad0;

static volatile LONG g_self_bits;                   // v5.30.1 test helper: CwoAutoPad.txt "LOADER ..." presses the loader's menus
static volatile LONGLONG g_self_until;
static WORD self_xi() {                                         // v5.188.36 the test pad's loader presses in XInput bits (no pad)
    static const struct { UINT32 wgi; WORD xi; } map[] = {
        {0x1, BTN_START}, {0x2, BTN_BACK}, {0x4, BTN_A}, {0x8, BTN_B}, {0x10, BTN_X}, {0x20, BTN_Y},
        {0x40, BTN_UP}, {0x80, BTN_DOWN}, {0x100, BTN_LEFT}, {0x200, BTN_RIGHT}, {0x400, BTN_LB}, {0x800, BTN_RB}};
    WORD b = 0;
    for (const auto &m : map) if ((UINT32)g_self_bits & m.wgi) b |= m.xi;
    return b;
}
static bool read_raw_buttons(WORD *out) {
    if (!g_pad0 || !g_orig_read) return false;
    ABI::Windows::Gaming::Input::GamepadReading r;
    if (FAILED(g_orig_read(g_pad0, &r))) return false;
    static const struct { UINT32 wgi; WORD xi; } map[] = {
        {0x1, BTN_START}, {0x2, BTN_BACK}, {0x4, BTN_A}, {0x8, BTN_B}, {0x10, BTN_X}, {0x20, BTN_Y},
        {0x40, BTN_UP}, {0x80, BTN_DOWN}, {0x100, BTN_LEFT}, {0x200, BTN_RIGHT}, {0x400, BTN_LB}, {0x800, BTN_RB},
        {0x1000, BTN_LTHUMB}, {0x2000, BTN_RTHUMB}};
    WORD b = 0;
    UINT32 self = g_self_bits && GetTickCount64() < (ULONGLONG)g_self_until ? (UINT32)g_self_bits : 0;
    for (const auto &m : map)
        if (((UINT32)r.Buttons | self) & m.wgi) b |= m.xi;
    *out = b;
    g_stick_mag = (float)sqrt(r.LeftThumbstickX * r.LeftThumbstickX + r.LeftThumbstickY * r.LeftThumbstickY);
    g_trig_up = (float)r.RightTrigger;
    g_trig_down = (float)r.LeftTrigger;
    return true;
}

static void filter_install() {
    if (!g_pads) return;
    ABI::Windows::Foundation::Collections::IVectorView<ABI::Windows::Gaming::Input::Gamepad *> *list = NULL;
    if (FAILED(g_pads->get_Gamepads(&list)) || !list) return;
    unsigned n = 0;
    list->get_Size(&n);
    int swapped = 0;
    for (unsigned i = 0; i < n; i++) {
        IGamepad *pad = NULL;
        if (FAILED(list->GetAt(i, &pad)) || !pad) continue;
        if (attach_filter((void ***)pad)) swapped++;
        if (i == 0 && pad != g_pad0) {                          // keep the first pad for the loader's own reading
            pad->AddRef();
            IGamepad *old = (IGamepad *)InterlockedExchangePointer((void **)&g_pad0, pad);
            if (old) old->Release();
        }
        pad->Release();
    }
    list->Release();
    if (swapped && g_filter_logged < 5) {
        logf("input filter on %d gamepad(s) (%u listed): creator buttons hidden from the game while the panel is on", swapped, n);
        g_filter_logged++;
    }
}

static void xinput_vibrate(WORD speed) {
    PFN_SETSTATE set = g_set ? g_set : g_set2;
    if (!set) return;
    XVIB v = {speed, speed};
    set(0, &v);
}

static void rumble(int pulses) {
    for (int i = 0; i < pulses; i++) {
        unsigned n = 0;
        HRESULT hr = S_OK;
        // re-sent every 10 ms for 150 ms so a vibration value the game writes in between cannot cancel the pulse
        for (int t = 0; t < 15; t++) {
            hr = pad_vibrate(1.0, &n);
            xinput_vibrate(50000);
            Sleep(10);
        }
        pad_vibrate(0.0, &n);
        xinput_vibrate(0);
        if (i == 0 && g_rumble_logged < 3) {
            logf("rumble: Windows.Gaming.Input result 0x%08lx, %u gamepad(s) listed", (unsigned long)hr, n);
            g_rumble_logged++;
        }
        if (i + 1 < pulses) Sleep(120);
    }
}

// markers_stNN.csv keeps every marker of a stage; delete rewrites the file without its last line
static const char *names_dat(int stage);
// v2.9: Map Slot 7 (stage 10) holds many stage files (templates, stage packs): their markers are kept per file
static void marker_path(int stage, wchar_t *out) {
    const char *dat = stage == 10 ? names_dat(stage) : "";
    if (dat[0] && _strnicmp(dat, "st10", 4) != 0) {
        char stem[40];
        strncpy_s(stem, dat, _TRUNCATE);
        char *dot = strrchr(stem, '.');
        if (dot) *dot = 0;
        swprintf_s(out, MAX_PATH, L"%smarkers_%S.csv", g_dir, stem);
        return;
    }
    swprintf_s(out, MAX_PATH, L"%smarkers_st%02d.csv", g_dir, stage);
}

static void add_marker(const char *type, int stage, const float p[3]) {
    wchar_t path[MAX_PATH];
    marker_path(stage, path);
    FILE *f = NULL;
    bool fresh = GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES;
    if (cwo_wfopen(&f, path, L"a") != 0 || !f) { logf("cannot write markers for stage %d", stage); return; }
    if (fresh) fprintf(f, "type,stage,x,y,z\n");
    fprintf(f, "%s,%d,%.3f,%.3f,%.3f\n", type, stage, p[0], p[1], p[2]);
    fclose(f);
    logf("marker %s stage %d (%.3f, %.3f, %.3f)", type, stage, p[0], p[1], p[2]);
}

static bool delete_last(int stage) {
    wchar_t path[MAX_PATH];
    marker_path(stage, path);
    FILE *f = NULL;
    if (cwo_wfopen(&f, path, L"rb") != 0 || !f) return false;
    static char buf[1 << 20];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = 0;
    if (n == 0) return false;
    size_t end = n;
    if (end && buf[end - 1] == '\n') end--;
    size_t cut = end;
    while (cut && buf[cut - 1] != '\n') cut--;
    if (cut == 0) return false;                        // only the header is left
    char removed[256] = {0};
    strncpy_s(removed, sizeof(removed), buf + cut, end - cut);
    if (cwo_wfopen(&f, path, L"wb") != 0 || !f) return false;
    fwrite(buf, 1, cut, f);
    fclose(f);
    logf("deleted last marker: %s", removed);
    return true;
}

static void set_status(const char *msg) {
    EnterCriticalSection(&g_ui_lock);
    strncpy_s(g_status, sizeof(g_status), msg, _TRUNCATE);
    g_status_until = GetTickCount64() + 4000;
    LeaveCriticalSection(&g_ui_lock);
}
#include "cwo_patchfiles.inl"                           // v5.58 the BFR pack: game files from patches
#include "cwo_lightnet.inl"                             // v5.126 stage light safety net (no more green fighters)

// count the SPAWN / CAPSULE / KILLZONE lines of a stage's marker file (shown on the panel)
static void recount(int stage) {
    LONG c[3] = {0, 0, 0};
    wchar_t path[MAX_PATH];
    marker_path(stage, path);
    FILE *f = NULL;
    if (cwo_wfopen(&f, path, L"r") == 0 && f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if (!strncmp(line, "SPAWN,", 6)) c[0]++;
            else if (!strncmp(line, "CAPSULE,", 8)) c[1]++;
            else if (!strncmp(line, "KILLZONE,", 9)) c[2]++;
        }
        fclose(f);
    }
    for (int i = 0; i < 3; i++) InterlockedExchange(&g_counts[i], c[i]);
}

typedef void(__fastcall *PFN_TEXT)(int, int, const char *);
static const char *instance_name(int n);

static void set_rgba(DWORD *rgba, DWORD c) { for (int i = 0; i < 4; i++) rgba[i] = c; }

// ---- v0.5 the stage pack list (Map\mx_list.txt) ----
// The stage file table (0x1403F2B60, .data, 24-byte entries {const char *name; 0; 0}; entry 2N = stage N's .alr name,
// 2N+1 its .dat, relative to Map\; MAP_SLOTS_2026-10-23) is read when a stage loads. Map Slot 7 (stage 10) becomes the
// list slot: the loader points its two names at the chosen pack files (Map\mxNNN.alr/.dat) - two pointer writes,
// logged, no code change. The pack list is Map\mx_list.txt (file<TAB>name<TAB>floors<TAB>spawns); the choice is kept
// in <TEMP>\CwoLoader\mx_choice.txt and applied again at start-up.
static const ULONG_PTR RVA_STAGE_FILES = 0x3F2B60;
static const int LIST_STAGE = 10;
struct MxStage { char file[16]; char name[48]; int floors, spawns; float kill; int has_kill; };   // v5.108 kill = optional 5th column
static MxStage g_mx[512];
static int g_mx_count;
static volatile LONG g_mx_sel, g_mx_cur = -1;

// ---- v0.7 skill showcase (owner, like the stage creator) ----
// Panel page 4 lists every skill (CwoSkills.txt next to the exe: id<TAB>name<TAB>school<TAB>cost, from our catalogue);
// D-pad Up drops a capsule with the chosen skill in front of player 1, the way the in-game-tested Skywalk patch
// (IGT-058e, build_igt058_stage_capsule.py) does: capsule table 0x140584f80, player p's spots at
// +0xD10 + (p*3+k)*0x48 {pos f32[3]; +0xC state (0 waiting, 2 present); +0x10 respawn countdown; +0x18 skill id};
// a WAITING spot gets the position, the skill and countdown 1, and the game spawns the capsule itself; once it is
// picked up, the spot's own position and deck card are put back (deck count +0xC80 adjusted as the patch does).
static const ULONG_PTR RVA_CAPSULES = 0x584f80;
struct SkillRow { int id; char name[32]; char school[10]; char cost[4]; };
static const int SK_MAX = 1300;                         // v5.188.46 the live list (stock + BFR + Illusion + new + fusions)
static SkillRow g_sk[SK_MAX];
static int g_sk_count;
static volatile LONG g_sk_sel, g_drop_request;
static struct { int k; float pos[3]; int card, skill, phase; } g_drop = {-1};
static char g_mx_names[2][32];
static const char *g_slot_orig[2];
static bool g_slot_saved;
static char *g_table_test;                         // self-test only: a stand-in for the game's table

static const char **mx_entry(int i) {
    return (const char **)((g_table_test ? (ULONG_PTR)g_table_test : g_base + RVA_STAGE_FILES) + 24 * i);
}

static void mx_load_list() {
    wchar_t exe[MAX_PATH], path[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    wchar_t *slash = wcsrchr(exe, L'\\');
    if (slash) slash[1] = 0;
    swprintf_s(path, MAX_PATH, L"%sAssets\\Data\\Map\\mx_list.txt", exe);
    FILE *f = NULL;
    g_mx_count = 0;
    if (cwo_wfopen(&f, path, L"r") != 0 || !f) { logf("stage list: no Map\\mx_list.txt"); return; }
    char line[256];
    while (g_mx_count < 512 && fgets(line, sizeof(line), f)) {
        MxStage &m = g_mx[g_mx_count];
        m.has_kill = 0; m.kill = 0.0f;
        int got = sscanf_s(line, "%15[^\t]\t%47[^\t]\t%d\t%d\t%f", m.file, (unsigned)sizeof(m.file), m.name, (unsigned)sizeof(m.name),
                           &m.floors, &m.spawns, &m.kill);
        if (got >= 4) {
            m.has_kill = got == 5 && m.kill != 0.0f;              // v5.108: the pack's kill height (Unreal KILL ZONE marker)
            g_mx_count++;
        }
    }
    fclose(f);
    logf("stage list: %d stage packs", g_mx_count);
}

static void mx_save_choice(int k) {
    wchar_t path[MAX_PATH];
    swprintf_s(path, MAX_PATH, L"%smx_choice.txt", g_dir);
    FILE *f = NULL;
    if (cwo_wfopen(&f, path, L"w") == 0 && f) { fprintf(f, "%s\n", k >= 0 ? g_mx[k].file : "none"); fclose(f); }
}

static bool bigstage_fits(int stage, const char *file, char *why, size_t n);   // v5.39 cwo_bigstage.inl

// v5.148.1 (owner 2026-11-10). Ghidra
// (clean 9ba7411b): the stage load FUN_1401d85f0 -> FUN_14021a6a0 loads the map sound bank of record (0x45 + stage % 10) of
// the stage file table (the records after st00.alr: 0x46 MapSEIsekiIn (st01) ... 0x4E MapSEUnderground) and the reverb
// DSP table 0x1402dc970[stage % 10]; Map Slot 7 is stage 10 -> record 0x45, which is EMPTY: no map bank. A pack that keeps a
// stock stage's .dat (Palace Ascended = st03's, with its looping ambient sounds) then plays them without their bank. The
// pack's own bank (CwoStageSound.txt: "mxNNN n", n = 1-9 the stock map bank) goes into record 0x45 while that pack is on
// Map Slot 7; the last-loaded cache *0x1403e05f4 is set to -1 so the next stage load reads it.
static const ULONG_PTR RVA_MAPSE_CACHE = 0x3e05f4;
static char g_ss_pack[16][16]; static int g_ss_bank[16], g_ss_n = -1;
static const char *g_ss_orig; static bool g_ss_saved;
static void ss_load() {
    g_ss_n = 0;
    wchar_t p[MAX_PATH]; swprintf_s(p, MAX_PATH, L"%sCwoStageSound.txt", g_dir);
    FILE *f = NULL; if (cwo_wfopen(&f, p, L"r") != 0 || !f) return;
    char line[256];
    while (g_ss_n < 16 && fgets(line, sizeof(line), f)) {
        int n; if (_strnicmp(line, "mx", 2) || sscanf_s(line, "%15s %d", g_ss_pack[g_ss_n], (unsigned)sizeof(g_ss_pack[0]), &n) != 2 || n < 1 || n > 9) continue;
        g_ss_bank[g_ss_n++] = n;
    }
    fclose(f);
    logf("stage sound: %d packs with a map sound bank (CwoStageSound.txt)", g_ss_n);
}
static void ss_apply(int k) {
    __try {
        if (g_ss_n < 0) ss_load();
        const char **r = mx_entry(0x45);
        if (!g_ss_saved) { g_ss_orig = *r; g_ss_saved = true; }
        // v5.155.1 / 5.151.2 (owner 2026-11-12): record 0x45 is also
        // stage 34's .dat entry (2 x 34 + 1); at start-up it holds "st34.dat", and a pack WITHOUT a bank of its own put it back,
        // so the stage load (FUN_14021a6a0: loads record 0x45 + stage % 10 when it is not NULL) read a stage file as the map
        // sound bank (log: "map sound bank -> st34.dat"). A pack now gets NULL there = no map bank (the file table's own value);
        // the start-up value only comes back when Map Slot 7 takes its own files again (k < 0).
        const char *want = k >= 0 ? NULL : g_ss_orig;
        int bank = 0;
        if (k >= 0 && k < g_mx_count)
            for (int i = 0; i < g_ss_n; i++) if (!_stricmp(g_ss_pack[i], g_mx[k].file)) { bank = g_ss_bank[i]; want = *mx_entry(0x45 + bank); }
        if (*r == want) return;
        InterlockedExchangePointer((void **)r, (void *)want);
        *(int *)(g_base + RVA_MAPSE_CACHE) = -1;
        logf("stage sound: Map Slot 7 map sound bank -> %s", want ? want : "(none, as stock)");
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// k >= 0: Map Slot 7 loads stage pack k; k < 0: Map Slot 7 loads its own files again
static void mx_apply(int k) {
    const char **e = mx_entry(2 * LIST_STAGE), **d = mx_entry(2 * LIST_STAGE + 1);
    char why[128];
    if (k >= 0 && k < g_mx_count && !g_table_test && !bigstage_fits(LIST_STAGE, g_mx[k].file, why, sizeof(why))) {
        logf("stage switch: Map Slot 7 not switched to '%s' (%s): %s", g_mx[k].file, g_mx[k].name, why);   // v5.39
        return;
    }
    if (!g_slot_saved) { g_slot_orig[0] = *e; g_slot_orig[1] = *d; g_slot_saved = true; }
    const char *old_a = *e, *old_d = *d;
    char was_a[24] = {}, was_d[24] = {};                       // v5.155 the names before (g_mx_names is rewritten in place)
    if (old_a) strncpy_s(was_a, old_a, _TRUNCATE);
    if (old_d) strncpy_s(was_d, old_d, _TRUNCATE);
    if (k >= 0 && k < g_mx_count) {
        sprintf_s(g_mx_names[0], "%s.alr", g_mx[k].file);
        sprintf_s(g_mx_names[1], "%s.dat", g_mx[k].file);
        InterlockedExchangePointer((void **)e, (void *)g_mx_names[0]);
        InterlockedExchangePointer((void **)d, (void *)g_mx_names[1]);
        InterlockedExchange(&g_mx_cur, k);
    } else {
        InterlockedExchangePointer((void **)e, (void *)g_slot_orig[0]);
        InterlockedExchangePointer((void **)d, (void *)g_slot_orig[1]);
        InterlockedExchange(&g_mx_cur, -1);
    }
    // v5.155 (owner 2026-11-12: Highway Pieces Test and Grand Panorama crashed in the level list's stage view). Ghidra: the
    // game's read queue FUN_14021ab70 takes a table entry {name, int +8, ...}; +8 == 0 sends the file through the first-time
    // lookup (FUN_14021ad70 / FUN_14021a810 store the looked-up flags into +8), +8 != 0 skips it. Both crashes walked the
    // PREVIOUS pack's collision (exe+1d317b = FUN_1401d3040, vertex index 25 = mx327's first triangle, 1679 = the Palace
    // test's) with its vertex block gone: a new pack in Map Slot 7 kept the old pack's +8. A changed name now clears +8 of
    // both entries, as the game's own first load of that entry.
    {
        bool ca = !*e != !old_a || (*e && _stricmp(*e, was_a)), cd = !*d != !old_d || (*d && _stricmp(*d, was_d));
        if (ca) InterlockedExchange((volatile LONG *)((char *)e + 8), 0);
        if (cd) InterlockedExchange((volatile LONG *)((char *)d + 8), 0);
    }
    logf("stage switch: Map Slot 7 (stage %d) table entries %p/%p: '%s'/'%s' -> '%s'/'%s'", LIST_STAGE, (void *)e,
         (void *)d, was_a[0] ? was_a : "(null)", was_d[0] ? was_d : "(null)", *e ? *e : "(null)", *d ? *d : "(null)");
    mx_save_choice(g_mx_cur);
    if (!g_table_test) ss_apply(g_mx_cur);                  // v5.148.1 the pack's map sound bank
}

// v5.64.4 the lobby's "Level" line shows the chosen pack's own name (owner 2026-11-02). Ghidra: native 2.151(51, row) = FUN_14024c500 returns the name pointer at +0 of row `row`
// of the game's stage list 0x140509ba0 (0x28-byte rows; row count *0x14050a5d0); the lobby asks for row = the stage number
// (memory read 2026-11-02: row 10 = "10 Dummy" in the clean list, its +0x18 field holds 8, not 10), so row 10 (Map Slot 7)
// gets a pointer to the chosen pack's name, and its own pointer back when no pack is chosen.
static const ULONG_PTR RVA_STAGE_ROWS = 0x509ba0, RVA_STAGE_ROWS_N = 0x50a5d0;
static char g_mx_rowname[64];
static const char *g_mx_row_orig[64];
static int g_mx_row_stage; static bool g_mx_row_stage_saved;   // v5.186.11 row 10's own stage field
static void rt_suffix(const char *pack, char *out, size_t n);   // v5.148.9 cwo_rating.inl
static void rt_press(WORD pressed);                              // v5.148.10 cwo_rating.inl
static void bn_pause_press(WORD pressed);                        // v5.148.21 cwo_bfrnet.inl
static void mx_name_step() {
    static ULONGLONG next;
    ULONGLONG now = GetTickCount64();
    if (now < next) return;
    next = now + 500;
    __try {
        int n = *(int *)(g_base + RVA_STAGE_ROWS_N);
        if (n <= 0 || n > 64) return;
        LONG cur = g_mx_cur;
        if (cur >= 0 && cur < g_mx_count) {                       // v5.148.9 the pack's name + its map rating (offline)
            char want[64], sfx[40];
            rt_suffix(g_mx[cur].file, sfx, sizeof(sfx));
            sprintf_s(want, "%.*s%s", (int)(sizeof(want) - 1 - strlen(sfx)), g_mx[cur].name, sfx);
            if (strcmp(g_mx_rowname, want)) strcpy_s(g_mx_rowname, want);
        }
        for (int r = LIST_STAGE; r == LIST_STAGE && r < n; r++) {
            ULONG_PTR row = g_base + RVA_STAGE_ROWS + 0x28 * (ULONG_PTR)r;
            const char **name = (const char **)row;
            if (cur >= 0 && cur < g_mx_count) {
                if (*name != g_mx_rowname) {
                    if (!g_mx_row_orig[r]) g_mx_row_orig[r] = *name;
                    *name = g_mx_rowname;
                    logf("stage list: row %d (Map Slot 7) named '%s'", r, g_mx_rowname);
                }
                // v5.186.11 (seen 2026-11-15 on Xbox Live: a pack chosen in the LEVEL LIST gave the session stage 8, Sein): the
                // session's stage is the catalog row's stage field, *(row + 0x18) (Ghidra full pass 065: FUN_140187370 /
                // FUN_140213c00 `DAT_140527bc8 = (&DAT_140509bb8)[row * 10]`); stock row 10 holds 8. While a pack is chosen it
                // holds 10 (Map Slot 7, where the pack's files are switched in); the old value comes back when none is.
                int *stg = (int *)(row + 0x18);
                if (*stg != LIST_STAGE) {
                    if (!g_mx_row_stage_saved) { g_mx_row_stage = *stg; g_mx_row_stage_saved = true; }
                    *stg = LIST_STAGE;
                    logf("stage list: row %d's stage field %d -> %d (the session plays Map Slot 7)", r, g_mx_row_stage, LIST_STAGE);
                }
            } else if (g_mx_row_orig[r] && *name == g_mx_rowname) {
                *name = g_mx_row_orig[r];
                if (g_mx_row_stage_saved) { *(int *)(row + 0x18) = g_mx_row_stage; g_mx_row_stage_saved = false; }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static void mx_restore_choice() {
    wchar_t path[MAX_PATH];
    swprintf_s(path, MAX_PATH, L"%smx_choice.txt", g_dir);
    FILE *f = NULL;
    char want[32] = {0};
    if (cwo_wfopen(&f, path, L"r") != 0 || !f) return;
    if (!fgets(want, sizeof(want), f)) want[0] = 0;
    fclose(f);
    want[strcspn(want, "\r\n")] = 0;
    for (int k = 0; k < g_mx_count; k++)
        if (!strcmp(g_mx[k].file, want)) { InterlockedExchange(&g_mx_sel, k); mx_apply(k); return; }
}

// queue the panel with the game's debug font; the game's font state is saved and restored around it
static void draw_panel() {
    if (g_forge_session) return;                                // Forge draws its own screens (forge_draw)
    float p[3];
    int stage = -1;
    bool in_match = read_player(0, p, &stage);
    char st[96] = {0};
    bool status = false;
    if (TryEnterCriticalSection(&g_ui_lock)) {                 // never block the render thread
        if (GetTickCount64() < g_status_until) { strcpy_s(st, sizeof(st), g_status); status = true; }
        LeaveCriticalSection(&g_ui_lock);
    }
    bool full = in_match && g_pinned;                           // v0.3.1: tap BACK toggles (holding BACK ~3 s = forfeit)
    if (!full && !status) return;
    int glyphs = *(int *)(g_base + RVA_GLYPHS), cap = *(int *)(g_base + RVA_GLYPH_CAP);
    if (glyphs > cap - 450) return;                            // panel: ~70 box glyphs + ~320 characters
    if (*(int *)(g_base + RVA_OFS_X) != 0 || *(int *)(g_base + RVA_OFS_Y) != 0) return;   // unshifted passes only
    int *space = (int *)(g_base + RVA_FONT_SPACE), *gap = (int *)(g_base + RVA_FONT_GAP);
    int *fh = (int *)(g_base + RVA_FONT_H), *fw = (int *)(g_base + RVA_FONT_W), *x0 = (int *)(g_base + RVA_FONT_X0);
    DWORD *rgba = (DWORD *)(g_base + RVA_FONT_RGBA);
    int s_space = *space, s_gap = *gap, s_h = *fh, s_w = *fw, s_x0 = *x0;
    DWORD s_rgba[4];
    memcpy(s_rgba, rgba, sizeof(s_rgba));
    PFN_TEXT text = (PFN_TEXT)(g_base + RVA_TEXT);
    char line[128];
    // v0.3.2 (owner): a compact panel at the
    // top centre, between the two players' HUDs, on a see-through dark box made of large Shift-JIS 'Ã¢â€“Â ' (0x81A1)
    // glyphs from the same debug font, overlapped so no gaps show
    const int BOX_X = 124, BOX_W = 392, LINE = 16, PAD = 6;
    int lines = full ? 7 : 1;
    int box_h = lines * LINE + 2 * PAD;
    game_box(BOX_X, 2, BOX_W, box_h, BOX_GREY);                 // v5.36 the game's see-through rectangle (was solid glyphs)
    *space = 0; *gap = 2; *fh = 14; *fw = 7;
    int x = BOX_X + 8, y = 2 + PAD;
    if (full && g_page == 1) {
        set_rgba(rgba, 0xFFFFD040);
        text(x, y, "EDGAR'S DREAM - stage packs   (R-stick: next)");
        y += LINE;
        LONG k = g_mx_sel, cur = g_mx_cur;
        set_rgba(rgba, 0xFFFFFFFF);
        if (g_mx_count <= 0) sprintf_s(line, "No stage packs installed (Map\\mx_list.txt)");
        else sprintf_s(line, "Stage %ld/%d: %.36s", k + 1, g_mx_count, g_mx[k].name);
        text(x, y, line);
        y += LINE;
        if (g_mx_count > 0) sprintf_s(line, "Floors %d   Spawns %d   File %s", g_mx[k].floors, g_mx[k].spawns, g_mx[k].file);
        else line[0] = 0;
        text(x, y, line);
        y += LINE;
        set_rgba(rgba, 0xFFFFD040);
        sprintf_s(line, "Map Slot 7 now: %.34s", cur >= 0 && cur < g_mx_count ? g_mx[cur].name : "its own stage");
        text(x, y, line);
        y += LINE;
        set_rgba(rgba, 0xFFA0E0FF);
        text(x, y, "D-pad  L/R choose  Up put in Map Slot 7  Down reset");
        y += LINE;
        text(x, y, "Then leave the match and pick Map Slot 7");
        y += LINE;
        y += LINE;
    } else if (full && g_page == 3) {
        set_rgba(rgba, 0xFFFFD040);
        text(x, y, "EDGAR'S DREAM - skill showcase   (R-stick: next)");
        y += LINE;
        LONG k = g_sk_sel;
        set_rgba(rgba, 0xFFFFFFFF);
        if (g_sk_count <= 0) sprintf_s(line, "No skill list (CwoSkills.txt)");
        else sprintf_s(line, "Skill %ld/%d: %.32s", k + 1, g_sk_count, g_sk[k].name);
        text(x, y, line);
        y += LINE;
        if (g_sk_count > 0) sprintf_s(line, "School %s   Aura %s   Record %d", g_sk[k].school, g_sk[k].cost, g_sk[k].id);
        else line[0] = 0;
        text(x, y, line);
        y += LINE;
        set_rgba(rgba, 0xFFA0E0FF);
        text(x, y, "D-pad L/R choose  X/Y 10  Up drop as a capsule");
        y += LINE;
        sprintf_s(line, "D-pad Down aura effect  A part %ld  B %s", g_fx_track, g_fx_always ? "always" : "with aura");
        text(x, y, line);
        y += LINE;
        set_rgba(rgba, 0xFFFFD040);
        if (g_fx_on) {
            const char *nm = "";
            for (int i = 0; i < g_sk_count; i++)
                if (g_sk[i].id == g_fx_skill) { nm = g_sk[i].name; break; }
            sprintf_s(line, "Aura effect: %.26s part %ld%s", nm, g_fx_track, g_fx_handle != FX_NONE ? "" : " (waiting)");
            text(x, y, line);
        } else
            text(x, y, g_drop.k >= 0 ? "A dropped capsule is waiting for you" : "Pick it up, then use it");
        y += LINE;
        y += LINE;
    } else if (full && g_page == 2) {
        set_rgba(rgba, 0xFFFFD040);
        text(x, y, "EDGAR'S DREAM - fly mode, walls off   (R-stick: next)");
        y += LINE;
        set_rgba(rgba, 0xFFFFFFFF);
        sprintf_s(line, "Stage %02d   X %7.1f   Y %7.1f   Z %7.1f", stage, p[0], p[1], p[2]);
        text(x, y, line);
        y += LINE;
        sprintf_s(line, "Spawns %ld   Capsules %ld   Kill zones %ld", g_counts[0], g_counts[1], g_counts[2]);
        text(x, y, line);
        y += LINE;
        set_rgba(rgba, 0xFFA0E0FF);
        text(x, y, "Left stick fly  RT up  LT down  D-pad Down land");
        y += LINE;
        text(x, y, g_hide_hud ? "D-pad Up: show HUD   BACK hides panel, keeps flying"
                              : "D-pad Up: hide HUD   BACK hides panel, keeps flying");
        y += LINE;
        text(x, y, "A spawn  RB capsule  B kill zone  X delete  Y log");
        y += LINE;
        set_rgba(rgba, 0xFFFFD040);
        text(x, y, "Marks drop to the floor below you");
        y += LINE;
        y += LINE;
    } else if (full) {
        set_rgba(rgba, 0xFFFFD040);
        text(x, y, "EDGAR'S DREAM - quick panel   (R-stick: next)");
        y += LINE;
        set_rgba(rgba, 0xFFFFFFFF);
        sprintf_s(line, "Stage %02d   X %7.1f   Y %7.1f   Z %7.1f", stage, p[0], p[1], p[2]);
        text(x, y, line);
        y += LINE;
        sprintf_s(line, "Spawns %ld   Capsules %ld   Kill zones %ld", g_counts[0], g_counts[1], g_counts[2]);
        text(x, y, line);
        y += LINE;
        int n = *(unsigned short *)(g_base + RVA_INSTANCE_COUNT);
        LONG sel = g_sel;
        set_rgba(rgba, 0xFFFFD040);
        if (n <= 0 || !*(void **)(g_base + RVA_INSTANCES)) sprintf_s(line, "Objects: none on this stage");
        else if (sel < 0 || sel >= n) sprintf_s(line, "Object: nearest of %d", n);
        else sprintf_s(line, "Object %ld/%d: %.32s", sel + 1, n, instance_name(sel));
        text(x, y, line);
        y += LINE;
        set_rgba(rgba, 0xFFA0E0FF);
        text(x, y, "A spawn  RB capsule  B kill zone  X delete  Y log");
        y += LINE;
        text(x, y, "D-pad  L/R choose  Up bring here  Down send back");
        y += LINE;
    }
    if (status) {
        set_rgba(rgba, 0xFF80FF80);
        text(x, y, st);
    }
    memcpy(rgba, s_rgba, sizeof(s_rgba));
    *space = s_space; *gap = s_gap; *fh = s_h; *fw = s_w; *x0 = s_x0;
}

// object names of a stage file, read from the game's own folder (read-only). Stock files point +0x5C at the "NM00" tag,
// files written by build_st32_objects point just after it; both are accepted.
static char g_names_dat[48];                        // v2.8: the stage file the names were read from
static const char *names_dat(int stage) {
    const char *dat = NULL;
    __try { if (stage >= 0 && stage < 40) dat = *mx_entry(2 * stage + 1); } __except (EXCEPTION_EXECUTE_HANDLER) { dat = NULL; }
    return dat && dat[0] && strlen(dat) < 40 ? dat : "";
}
static bool names_current(int stage) { return !strcmp(g_names_dat, names_dat(stage)); }

static void load_names(int stage) {
    g_name_count = 0;
    strcpy_s(g_names_dat, names_dat(stage));
    wchar_t exe[MAX_PATH], path[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    wchar_t *slash = wcsrchr(exe, L'\\');
    if (slash) slash[1] = 0;
    swprintf_s(path, MAX_PATH, L"%sAssets\\Data\\Map\\st%02d.dat", exe, stage);
    // v2.0: the name the stage file table holds for this stage (Map Slot 7 may point at a stage pack file)
    const char *dat = NULL;
    __try { if (stage >= 0 && stage < 40) dat = *mx_entry(2 * stage + 1); } __except (EXCEPTION_EXECUTE_HANDLER) { dat = NULL; }
    if (dat && dat[0] && strlen(dat) < 40 && !strchr(dat, '\\')) swprintf_s(path, MAX_PATH, L"%sAssets\\Data\\Map\\%S", exe, dat);
    FILE *f = NULL;
    if (cwo_wfopen(&f, path, L"rb") != 0 || !f) { logf("names: cannot read %ls", path); return; }
    static unsigned char buf[4 << 20];
    size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    if (n < 0x140 || memcmp(buf, "ST00", 4) != 0) return;
    int off = *(int *)(buf + 0x5C), count = *(int *)(buf + 0x60) & 0xFFFF;
    if (off <= 0 || (size_t)off >= n || count <= 0) { logf("names: stage %d has no objects", stage); return; }
    if (off + 4 <= (int)n && memcmp(buf + off, "NM00", 4) == 0) off += 4;
    int k = 0;
    for (size_t i = off; k < count && k < 256 && i < n; k++) {
        size_t e = i;
        while (e < n && buf[e]) e++;
        strncpy_s(g_names[k], sizeof(g_names[k]), (const char *)buf + i, _TRUNCATE);
        i = e + 1;
    }
    g_name_count = k;
    logf("names: stage %d, %d object names", stage, k);
}

// name of instance n: row n of the spawner's placement tables -> object index -> name
static const char *instance_name(int n) {
    int base_row = 0;
    for (int t = 0; t < 2; t++) {
        const unsigned char *rows = *(const unsigned char **)(g_base + RVA_ROW_TABLES + 8 * t);
        int cnt = *(unsigned short *)(g_base + RVA_ROW_COUNTS + 2 * t);
        if (rows && n < base_row + cnt) {
            int obj = *(unsigned short *)(rows + 0x24 * (n - base_row));
            return obj < g_name_count ? g_names[obj] : "?";
        }
        base_row += cnt;
    }
    return "?";
}

typedef void(__fastcall *PFN_SETPOS)(void *, const float *);

// v0.3 experiment (live editor groundwork): move the nearest stage object next to player 1 through the object's own
// position method. Answers in game whether objects are redrawn (and collide) at a position changed after loading.
static void pull_nearest_object() {
    float p[3];
    int stage = -1;
    if (!read_player(0, p, &stage)) return;
    void **inst = *(void ***)(g_base + RVA_INSTANCES);
    int n = *(unsigned short *)(g_base + RVA_INSTANCE_COUNT);
    if (!inst || n <= 0) { set_status("No stage objects on this stage"); logf("pull: no instance array (count %d)", n); return; }
    int best = -1;
    float bd = 1e30f;
    for (int i = 0; i < n; i++) {
        if (!inst[i]) continue;
        const float *q = (const float *)((char *)inst[i] + OBJ_POS);
        float dx = q[0] - p[0], dy = q[1] - p[1], dz = q[2] - p[2], d = dx * dx + dy * dy + dz * dz;
        if (d < bd) { bd = d; best = i; }
    }
    LONG sel = g_sel;
    if (sel >= 0 && sel < n && inst[sel]) {                // the object chosen with BACK+D-pad
        best = sel;
        const float *q = (const float *)((char *)inst[sel] + OBJ_POS);
        bd = (q[0] - p[0]) * (q[0] - p[0]) + (q[1] - p[1]) * (q[1] - p[1]) + (q[2] - p[2]) * (q[2] - p[2]);
    }
    if (best < 0) { set_status("No stage objects on this stage"); return; }
    char *obj = (char *)inst[best];
    float *q = (float *)(obj + OBJ_POS);
    float old[3] = {q[0], q[1], q[2]};
    float to[3] = {p[0] + 25.0f, p[1], p[2]};                // 2.5 m beside the player, at the player's height
    PFN_SETPOS setpos = *(PFN_SETPOS *)(*(char **)obj + VT_SET_POSITION);
    if (best < 1024 && !g_has_orig[best]) {
        memcpy(g_orig[best], old, sizeof(old));
        g_has_orig[best] = true;
    }
    setpos(obj, to);
    g_last_moved = best;
    logf("pull: stage %d instance %d (%s) at %p (+0x1E0) old (%.2f, %.2f, %.2f) new (%.2f, %.2f, %.2f) via method %p",
         stage, best, instance_name(best), obj + OBJ_POS, old[0], old[1], old[2], q[0], q[1], q[2], (void *)setpos);
    char msg[96];
    sprintf_s(msg, "%.28s moved next to you (was %.0f m away)", instance_name(best), sqrtf(bd) / 10.0f);
    set_status(msg);
}

// D-pad Down: put the selected object (or the last one moved) back where the stage file placed it
static void return_object() {
    void **inst = *(void ***)(g_base + RVA_INSTANCES);
    int n = *(unsigned short *)(g_base + RVA_INSTANCE_COUNT);
    LONG sel = g_sel;
    int k = (sel >= 0 && sel < n && sel < 1024 && g_has_orig[sel]) ? sel : g_last_moved;
    if (!inst || k < 0 || k >= n || k >= 1024 || !g_has_orig[k] || !inst[k]) { set_status("Nothing to send back"); return; }
    char *obj = (char *)inst[k];
    float *q = (float *)(obj + OBJ_POS);
    float old[3] = {q[0], q[1], q[2]};
    PFN_SETPOS setpos = *(PFN_SETPOS *)(*(char **)obj + VT_SET_POSITION);
    setpos(obj, g_orig[k]);
    logf("return: instance %d (%s) at %p old (%.2f, %.2f, %.2f) new (%.2f, %.2f, %.2f)", k, instance_name(k),
         obj + OBJ_POS, old[0], old[1], old[2], q[0], q[1], q[2]);
    g_has_orig[k] = false;
    if (g_last_moved == k) g_last_moved = -1;
    char msg[96];
    sprintf_s(msg, "%.28s sent back", instance_name(k));
    set_status(msg);
}

// v1.1 hide-HUD (owner). Ghidra batch_073a-e:
// the in-match HUD is class CBattle2DMenuClass (RTTI); its method table sits in .data at 0x14047b8d0 and method 4
// (slot 0x14047b8f0) is the per-frame draw FUN_140150c30. That draw only draws the HUD when the object's +0x54
// "ready" flag is set (+0x50 = active; FUN_1401505a0 clears both on shutdown); with +0x54 = 0 it takes the game's
// own nothing-to-draw path (only the cinema bars when +0x2a0 is set). The loader points that one slot at a wrapper
// that clears +0x54 for the duration of the call and restores it at once: no game code changes, no lasting state.
static const ULONG_PTR RVA_HUD_DRAW_SLOT = 0x47b8f0, RVA_HUD_DRAW = 0x150c30, HUD_READY = 0x54;
typedef void(__fastcall *PFN_HUD_DRAW)(void *);
static PFN_HUD_DRAW o_hud_draw;
// v5.9 (owner 21:34 screenshots + "no overlapping menus when selecting any and all menus from Edgar's dream"): while an
// Edgar's Dream page or a creator menu is drawn over a match, the game's HUD steps aside (forge_draw keeps this time
// fresh every frame it draws one; the HUD is back 250 ms after the menu closes)
static volatile ULONGLONG g_menu_hud_until;
static void cwo_card_draw();                                // cwo_sixhud.inl: the game's skill card keeps showing
static void __fastcall hud_draw(void *self) {
    if ((g_hide_hud || GetTickCount64() < g_menu_hud_until) && g_in_match && self) {
        int *ready = (int *)((char *)self + HUD_READY);
        int keep = *ready;
        *ready = 0;
        o_hud_draw(self);
        *ready = keep;
        __try { cwo_card_draw(); } __except (EXCEPTION_EXECUTE_HANDLER) {}   // (normally drawn from inside the HUD draw)
        return;
    }
    o_hud_draw(self);
}

// explicit check: the slot must hold exactly the stock draw routine, otherwise the switch is refused
static bool hud_install(ULONG_PTR slot_addr, ULONG_PTR expect) {
    void **slot = (void **)slot_addr;
    if ((ULONG_PTR)*slot != expect) {
        logf("hide-HUD refused: draw slot holds %p, expected %p", *slot, (void *)expect);
        return false;
    }
    DWORD old;
    if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old)) return false;
    o_hud_draw = (PFN_HUD_DRAW)*slot;
    *slot = (void *)hud_draw;
    VirtualProtect(slot, sizeof(void *), old, &old);
    return true;
}

// v0.6 fly page (owner: no-clip to reach spawn spots inside buildings). While the panel is on the fly page, the loader
// holds player 1's position itself (actor +0x12E40, and the previous-position copy +0x12E4C written by player setup
// FUN_14024e190) so gravity and walls cannot move it; the left stick moves along the facing the game gives the
// character, the triggers raise / lower.
// v1.0 (owner: every stick direction pushed the character the same way): +0x12E58 is only written by player setup
// (the spawn heading). The live body facing is +0x12E68: the movement code turns it toward the stick direction
// relative to the camera, whose heading +0x12E5C the camera code sets with atan2(dx, dz) * 5215.19 (x = sin,
// z = cos; FUN_140254040, FUN_14025b270, Ghidra batch_072d). Units: radians x 5215.19 = DAT_1402f824c.
// Fly also stays on when BACK hides the panel (owner: see the map without the panel); D-pad Down on the fly page
// lands / takes off again, and leaving the fly page lands.
static const ULONG_PTR ACTOR_PREV_POS = 0x12E4C, ACTOR_FACING = 0x12E68;
static const float ANGLE_UNITS = 5215.18896f, FLY_SPEED = 80.0f, FLY_CLIMB = 60.0f;   // PD units per second
static float g_ghost[3];
static LARGE_INTEGER g_fly_last, g_qpf;
static ULONG_PTR g_actor_test;                      // self-test only: a stand-in actor

static void fly_step() {
    bool want = g_forge_active ? !g_fly_landed                  // v2.0: the Forge editor always flies (menu: Fly)
                               : g_in_match && g_page == 2 && !g_fly_landed && (g_pinned || g_flying);
    ULONG_PTR actor = g_actor_test ? g_actor_test : cwo_actors()[0];
    if (!want || !actor || *(int *)(actor + ACTOR_ACTIVE) == 0) {
        if (g_flying) { g_flying = false; logf("fly off at (%.1f, %.1f, %.1f)", g_ghost[0], g_ghost[1], g_ghost[2]); }
        return;
    }
    float *pos = (float *)(actor + ACTOR_POS), *prev = (float *)(actor + ACTOR_PREV_POS);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (!g_qpf.QuadPart) QueryPerformanceFrequency(&g_qpf);
    if (!g_flying) {
        memcpy(g_ghost, pos, sizeof(g_ghost));
        g_flying = true;
        g_fly_last = now;
        if (!g_qpf.QuadPart) QueryPerformanceFrequency(&g_qpf);
        logf("fly on at (%.1f, %.1f, %.1f): writing actor %p +0x12E40/+0x12E4C each frame while the fly page is open",
             g_ghost[0], g_ghost[1], g_ghost[2], (void *)actor);
    }
    float dt = (float)(now.QuadPart - g_fly_last.QuadPart) / (float)g_qpf.QuadPart;
    if (dt > 0.004f) {                                          // several flushes per frame: integrate once per frame
        if (dt > 0.1f) dt = 0.1f;
        g_fly_last = now;
        float mag = g_stick_mag > 1.0f ? 1.0f : g_stick_mag;
        float sp = g_forge_active && g_fprecise ? 0.25f : 1.0f;   // Forge precision moves
        if (g_forge_active && (g_game_menu || g_fmenu)) sp = 0.0f;   // menus open: the monitor stays put
        mag = mag < 0.2f ? 0.0f : (mag - 0.2f) / 0.8f;          // dead zone
        float hd = (float)*(int *)(actor + ACTOR_FACING) / ANGLE_UNITS;
        g_ghost[0] += sinf(hd) * FLY_SPEED * sp * mag * dt;
        g_ghost[2] += cosf(hd) * FLY_SPEED * sp * mag * dt;
        float tu = g_trig_up, td = g_trig_down;
        if ((g_ap_lt || g_ap_rt) && GetTickCount64() < (ULONGLONG)g_ap_stick_until) { td = (float)g_ap_lt / 1000.0f; tu = (float)g_ap_rt / 1000.0f; }   // v5.188.20 test pad (no live pad needed)
        float ny = g_ghost[1] + (tu - td) * FLY_CLIMB * sp * dt;
        // v5.188.20 (BFR player feedback 2026-11-16: "selected Midnight City ... kept falling through the map"): flying down
        // (LT) had no floor - the fighter went under the stage and fell once flying stopped. Now a descent stops on the
        // first floor below, found with the game's own floor probe FUN_140164330(scratch, out, from, to): 1 = a floor
        // triangle (normal y > 0.643) on the segment, out = the one nearest 'from' (Ghidra + capstone 2026-11-16)
        if (ny < g_ghost[1] && !g_actor_test) {
            typedef int (*probe_fn)(void *, float *, const float *, const float *);
            float from[3] = {g_ghost[0], g_ghost[1] + 10.0f, g_ghost[2]}, to[3] = {g_ghost[0], ny - 1.0f, g_ghost[2]}, hit[3] = {0, 0, 0};
            __try {
                if (((probe_fn)(g_base + 0x164330))(NULL, hit, from, to) && hit[1] > ny && hit[1] <= from[1]) ny = hit[1];
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        g_ghost[1] = ny;
    }
    memcpy(pos, g_ghost, sizeof(g_ghost));
    memcpy(prev, g_ghost, sizeof(g_ghost));
}

static void sk_load_list() {
    wchar_t exe[MAX_PATH], path[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    wchar_t *slash = wcsrchr(exe, L'\\');
    if (slash) slash[1] = 0;
    swprintf_s(path, MAX_PATH, L"%sCwoSkills.txt", exe);
    FILE *f = NULL;
    g_sk_count = 0;
    if (cwo_wfopen(&f, path, L"r") != 0 || !f) { logf("skill list: no CwoSkills.txt"); return; }
    char line[160];
    while (g_sk_count < SK_MAX && fgets(line, sizeof(line), f)) {
        SkillRow &s = g_sk[g_sk_count];
        if (sscanf_s(line, "%d\t%31[^\t]\t%9[^\t]\t%3[^\t\r\n]", &s.id, s.name, (unsigned)sizeof(s.name), s.school,
                     (unsigned)sizeof(s.school), s.cost, (unsigned)sizeof(s.cost)) == 4)
            g_sk_count++;
    }
    fclose(f);
    logf("skill list: %d skills", g_sk_count);
}

static char *spot_ptr(int p, int k) { return cwo_caps(0xD10 + (ULONG_PTR)(p * 3 + k) * 0x48); }

// game thread: put the chosen skill in a waiting capsule spot of player 1, 2.5 m in front of them
static void drop_skill() {
    float p[3];
    int stage;
    if (!read_player(0, p, &stage) || g_sk_count <= 0) return;
    if (g_drop.k >= 0) { set_status("Pick up the last skill capsule first"); return; }
    int k = -1;
    for (int i = 0; i < 3 && k < 0; i++)
        if (*(int *)(spot_ptr(0, i) + 0xC) == 0) k = i;
    if (k < 0) {                                                 // v3.0 (owner)
        for (int i = 0; i < 3 && k < 0; i++)
            if (*(int *)(spot_ptr(0, i) + 0xC) == 2) k = i;              // a capsule lying there: it now holds this skill
        if (k < 0) { set_status("No capsule spot to use right now"); return; }
        char *s2 = spot_ptr(0, k);
        int old = *(int *)(s2 + 0x18);
        *(int *)(s2 + 0x18) = g_sk[g_sk_sel].id;
        logf("skill drop: spot %d occupied - its capsule now holds %d (was %d)", k, g_sk[g_sk_sel].id, old);
        char m2[96];
        sprintf_s(m2, "Capsule spot %d's capsule now holds %.40s", k + 1, g_sk[g_sk_sel].name);
        set_status(m2);
        return;
    }
    char *s = spot_ptr(0, k);
    ULONG_PTR actor = cwo_actors()[0];
    float hd = (float)*(int *)(actor + 0x12E58) / 5215.18896f;
    const SkillRow &sk = g_sk[g_sk_sel];
    g_drop.k = k;
    memcpy(g_drop.pos, s, 12);
    g_drop.card = *(int *)(s + 0x18);
    g_drop.skill = sk.id;
    g_drop.phase = 1;
    float to[3] = {p[0] + sinf(hd) * 25.0f, p[1] + 10.0f, p[2] + cosf(hd) * 25.0f};
    memcpy(s, to, 12);
    *(int *)(s + 0x18) = sk.id;
    *(int *)(s + 0x10) = 1;                                      // countdown 1: the game spawns it next tick
    logf("skill drop: spot %d at %p: pos (%.1f, %.1f, %.1f) -> (%.1f, %.1f, %.1f), card %d -> %d (%s), countdown -> 1", k,
         (void *)s, g_drop.pos[0], g_drop.pos[1], g_drop.pos[2], to[0], to[1], to[2], g_drop.card, sk.id, sk.name);
    char msg[96];
    sprintf_s(msg, "%.40s capsule dropped in front of you", sk.name);
    set_status(msg);
}

// game thread, every frame: once the dropped capsule has been taken, give the spot back its own place and card
static void drop_watch() {
    if (g_drop.k < 0) return;
    if (!g_in_match) { g_drop.k = -1; return; }                   // match over: the game resets its spots
    char *s = spot_ptr(0, g_drop.k);
    int state = *(int *)(s + 0xC);
    if (g_drop.phase == 1 && state == 2) g_drop.phase = 2;          // the capsule appeared
    if (g_drop.phase == 2 && state != 2) {                          // taken: restore, as the Skywalk patch does
        int card = *(int *)(s + 0x18);
        memcpy(s, g_drop.pos, 12);
        if (card == -1) (*(int *)cwo_caps(0xC80))--;
        *(int *)(s + 0x18) = g_drop.card;
        logf("skill drop: taken; spot %d restored to (%.1f, %.1f, %.1f), card %d -> %d", g_drop.k, g_drop.pos[0],
             g_drop.pos[1], g_drop.pos[2], card, g_drop.card);
        g_drop.k = -1;
    }
}

// ---- v1.2 aura FX (owner) ----
// Game facts (Ghidra full pass batch_065, reviewed 2026-10-24, 07_Combat_and_Skills/AURA_FX_2026-10-24.md):
//   effect manager *0x1405bb2e0: 256 instance slots of 0xF0 bytes at +0x60; a handle is serial<<16 | track<<12 | slot
//   and slot +0 holds it while the instance lives. FUN_14018a040(mgr, owner, request, track, matrix, 0) starts track k
//   of effect `request` (EFT tail +0xF4 + k*0x1C, bit 0 = startable) and returns the handle, or -1; while the effect's
//   pack span is not loaded yet it queues the load (FUN_140189e80) and returns -1, so we try again next frame.
//   FUN_14018d410(h) sets slot +0x14, the loop flag: the per-frame update FUN_1401899b0 puts a looping instance back
//   to time 0 when it reaches its end and keeps it alive. FUN_14018d440(h, m) replaces its matrix, FUN_14018d3d0(h)
//   stops it (slot +8). The game itself loops track 1 (the flying projectile) and tracks with bit 2.
//   owner = player record +0x14; the effect's nodes look up that player's joints by name hash (FUN_14018aa50 on the
//   table FUN_14018a7d0 fills at player setup), so a sword's hand flames sit on the hand as in the skill.
//   The skills' own matrix (FUN_140259000): identity 0x1404102e0, position +0x12E40, then FUN_1402906e0 turns it by
//   facing +0x12E68 + 0x4000. The logic ticks and this draw pass run one after the other on one thread (FUN_1401c85c0).
//   Skill -> effect request: FUN_1401272b0(profile = i16 at +0x14 of skill record *0x1404bdc50 + 0x90 * skill, skill).
//   Aura shown: aura manager *0x14052ad00, player slot +0xC0 + 0x120 * player, bit 0x10 (set by FUN_14019b520).
static const ULONG_PTR RVA_FX_MGR = 0x5bb2e0, RVA_FX_START = 0x18a040, RVA_FX_LOOP = 0x18d410, RVA_FX_MOVE = 0x18d440,
                       RVA_FX_STOP = 0x18d3d0, RVA_SKILL_EFFECT = 0x1272b0, RVA_TURN_Y = 0x2906e0, RVA_SKILLS = 0x4bdc50,
                       RVA_AURA_MGR = 0x52ad00, RVA_IDENTITY = 0x4102e0;
typedef unsigned(__fastcall *PFN_FX_START)(ULONG_PTR, int, int, int, const float *, int);
typedef void(__fastcall *PFN_FX_HANDLE)(unsigned);
typedef void(__fastcall *PFN_FX_MOVE)(unsigned, const float *);
typedef short(__fastcall *PFN_SKILL_EFFECT)(int, int);
typedef void(__fastcall *PFN_TURN_Y)(float *, unsigned);
struct FxGame {
    ULONG_PTR mgr, skills, aura, actor;
    const float *identity;
    PFN_FX_START start;
    PFN_FX_HANDLE loop, stop;
    PFN_FX_MOVE move;
    PFN_SKILL_EFFECT effect_of;
    PFN_TURN_Y turn;
};
static FxGame *g_fx_test;                          // self-test only: stand-in game
static int g_fx_req, g_fx_fail;
static __declspec(align(16)) float g_fx_mat[16];

static bool fx_game(FxGame *g) {
    if (g_fx_test) { *g = *g_fx_test; return g->mgr != 0; }
    g->mgr = *(ULONG_PTR *)(g_base + RVA_FX_MGR);
    g->skills = *(ULONG_PTR *)(g_base + RVA_SKILLS);
    g->aura = *(ULONG_PTR *)(g_base + RVA_AURA_MGR);
    g->actor = cwo_actors()[0];
    g->identity = (const float *)(g_base + RVA_IDENTITY);
    g->start = (PFN_FX_START)(g_base + RVA_FX_START);
    g->loop = (PFN_FX_HANDLE)(g_base + RVA_FX_LOOP);
    g->stop = (PFN_FX_HANDLE)(g_base + RVA_FX_STOP);
    g->move = (PFN_FX_MOVE)(g_base + RVA_FX_MOVE);
    g->effect_of = (PFN_SKILL_EFFECT)(g_base + RVA_SKILL_EFFECT);
    g->turn = (PFN_TURN_Y)(g_base + RVA_TURN_Y);
    return g->mgr != 0 && g->skills != 0;
}

static bool fx_alive(const FxGame &g, unsigned h) {
    return h != FX_NONE && *(unsigned *)(g.mgr + 0x60 + (ULONG_PTR)(h & 0xFFF) * 0xF0) == h;
}

static void fx_off(const FxGame &g, const char *why) {
    if (fx_alive(g, g_fx_handle)) {
        g.stop(g_fx_handle);
        logf("aura FX: stopped %08X (%s)", g_fx_handle, why);
    }
    g_fx_handle = FX_NONE;
}

// game thread, every flush: keep the chosen effect running on player 1 and moving with them
static void fx_step() {
    if (!g_in_match) { g_fx_handle = FX_NONE; return; }       // the match's effects are gone: forget, touch nothing
    FxGame g;
    if (!fx_game(&g)) return;
    if (InterlockedExchange(&g_fx_change, 0)) {
        fx_off(g, "changed");
        g_fx_req = 0;
        g_fx_fail = 0;
        LONG id = g_fx_skill;
        if (g_fx_on && id >= 0 && id < 753) {
            short prof = *(short *)(g.skills + 0x14 + (ULONG_PTR)id * 0x90);
            g_fx_req = g.effect_of(prof, id);
            logf("aura FX: skill %ld profile %d -> effect %d, part %ld, %s", id, prof, g_fx_req, g_fx_track,
                 g_fx_always ? "always" : "while the aura shows");
            if (g_fx_req <= 0) set_status("This skill has no effect to show");
        }
    }
    ULONG_PTR actor = g.actor;
    bool want = g_fx_on && g_fx_req > 0 && actor && *(int *)(actor + ACTOR_ACTIVE) != 0;
    if (want && !g_fx_always) want = g.aura && (*(unsigned *)(g.aura + 0xC0) & 0x10) != 0;   // player 1's aura slot
    if (!want) { if (g_fx_handle != FX_NONE) fx_off(g, "aura hidden or off"); return; }
    memcpy(g_fx_mat, g.identity, sizeof(g_fx_mat));
    memcpy(g_fx_mat + 12, (const float *)(actor + ACTOR_POS), 12);
    g.turn(g_fx_mat, (unsigned)(*(int *)(actor + ACTOR_FACING) + 0x4000));
    if (fx_alive(g, g_fx_handle)) { g.move(g_fx_handle, g_fx_mat); return; }
    if (g_fx_fail > 120) return;                              // ~2 s of refusals: this part is empty, wait for A
    unsigned h = g.start(g.mgr, *(int *)(actor + 0x14), g_fx_req, g_fx_track, g_fx_mat, 0);
    if (h == FX_NONE) {
        if (++g_fx_fail > 120) {
            logf("aura FX: effect %d part %ld refused for 120 tries (part empty?)", g_fx_req, g_fx_track);
            set_status("This part of the effect is empty: press A for the next");
        }
        return;
    }
    g.loop(h);
    g_fx_handle = h;
    g_fx_fail = 0;
    logf("aura FX: effect %d part %ld started, handle %08X, looping", g_fx_req, g_fx_track, h);
}

#include "cwo_fxcolour.inl"                              // v5.32 every skill's colour (recoloured as its effect loads)
#include "cwo_newskills.inl"
#include "cwo_hitbox.inl"                                // v5.49 per-skill hit radius (table filled by cwo_bigskills.inl)
#include "cwo_sndbank.inl"                               // v5.50.1 the override common.bin copied over the loaded bank
#include "cwo_bigids.inl"                                // v5.47 skill ids 0-1199
#include "cwo_labfix.inl"                               // v5.52.2 a Lab manager of the stock size is moved to a 0x5500 block
#include "cwo_bigskills.inl"                             // v5.45 the bigger skill table + the skill lists
#include "cwo_bfr.inl"                                   // v5.56 the Battle for Reality skill pack (on / off)
#include "cwo_strplus.inl"                              // v5.188.22 Charged Ray: STR = charge + value
static volatile ULONG_PTR g_ho_actor, g_ho_me;                 // v5.131 the Hologram (cwo_hologram.inl): out, and its caster
#include "cwo_stocklist.inl"                             // v5.46 the stock list for ids above 500
#include "cwo_hud.inl"                                   // v5.65 HUD presets
#include "cwo_bigarena.inl"                              // v5.48 a 64 MB stage collision arena
#include "cwo_crashlog.inl"                              // v5.47.1 crash log
#include "cwo_labwatch.inl"                              // v5.47.2 Lab page-field write watch (research)
#include "cwo_charbuild.inl"                             // v3.7 the character file built in the loader
#include "cwo_charbuild2.inl"                            // v4.5 NPC bodies and body parts in the preview
#include "cwo_theme.inl"                                 // v3.8 outfit themes (transparency kept)
static void cpu_level_apply(char *c, int lv);              // cwo_lobby.inl (v5.38.1: the spectator CPU uses it too)
static int cpu_level_pick();
#include "cwo_forge.inl"
#include "cwo_edset.inl"                                  // v5.165 Create a Skill / TEST lists your own arsenals
#include "cwo_slots.inl"                                 // v4.1 the select screen's created-character cell
#include "cwo_lobby.inl"                                 // v5.2 the 3 VS 3 LOBBY
#include "cwo_gametype.inl"                              // v5.18 Change Game Type in the split-screen lobby
#include "cwo_outfitnames.inl"                           // v5.19 outfit names in the COLORS list
#include "cwo_bossraid.inl"                              // v5.26 Boss Raid
#include "cwo_mirror.inl"                                // v5.34 the Mirror skill
#include "cwo_hologram.inl"                              // v5.131 the Hologram skill
#include "cwo_bigslot.inl"                              // v5.4 bigger fighter model slots
#include "cwo_sixf.inl"                                 // v5.0 six fighters, route A step 1
#include "cwo_npcpage.inl"                               // v5.99 the NPC page in the match
#include "cwo_sixf2.inl"                                // v5.1 six fighters, route A step 2
#include "cwo_watch.inl"                                // v5.3.3 crash watch in six-fighter matches
#include "cwo_dbtl.inl"                                 // v5.11 Download Battle: stage, characters, arsenal
#include "cwo_holiday.inl"                               // v5.74 holiday themes
#include "cwo_livesess.inl"                             // v5.11 the Xbox Live lobby's session win / loss
#include "cwo_bigstage.inl"                             // v5.15 / v5.39 own model buffers for big stages (Map Slot 7 too)
#include "cwo_splitobj.inl"                             // v5.119 Map Slot 7 packs keep their objects in split screen
#include "cwo_sndguard.inl"                             // v5.186.2 a sound with no loaded bank is skipped (new rows crashed)
#include "cwo_portrait.inl"                             // v5.40 stage portraits: Map Slot 7 shows its pack's picture
#include "cwo_school6.inl"                               // v5.159 the Illusion school = school 6 (marks, aura)
#include "cwo_schoolcheck.inl"                           // v5.164 Edit Arsenal's school limit counts the Illusion school
#include "cwo_hudface.inl"                               // v5.67 HUD faces
#include "cwo_selout.inl"                                // v5.77 select portraits follow the highlighted outfit
#include "cwo_lobstrip.inl"                              // v5.91 lobby face strips per outfit
#include "cwo_splitnames.inl"                            // v5.92 split screen: the other fighters' names
#include "cwo_post.inl"                                  // v5.78 post-process pass (holiday themes)
#include "cwo_colorbar.inl"                              // v5.88 COLORS moved into the left black bar
#include "cwo_visions.inl"                               // v5.86 Edgar's Dream > Visions
#include "cwo_vault.inl"                                 // v5.133 the Skill Vault (View Stock: Back + Y)
#include "cwo_discord.inl"                               // v5.188.49 Join our Discord (MOD LIST: Y)
#include "cwo_modlist.inl"                               // v5.80 MOD LIST on the main menu
#include "cwo_realistic.inl"                             // v5.81 Realistic Mode
#include "cwo_cpurevive.inl"                             // v5.86 CPUs revive downed teammates
#include "cwo_kskills.inl"                               // v5.129 Revive Mend + Body Pull
#include "cwo_titlevol.inl"                              // v5.132 quieter title screen
#include "cwo_outfitfx.inl"                              // v5.75 outfit effects
#include "cwo_downglow.inl"                              // v5.103 a glow on downed fighters
#include "cwo_spawnview.inl"                             // v5.187.0 spawn view: starts + capsule spots shown
#include "cwo_stagefx.inl"                               // v5.139 skill effects placed in a stage (CwoStageFx.txt)
#include "cwo_redmoon.inl"                               // v5.188.23 the Red Moon skill (BFR 434)
#include "cwo_fxsize.inl"                                // v5.148.1 effect size per skill (CwoFxSize.txt)
#include "cwo_lowgrav.inl"                               // v5.121 low gravity stages (Driftstone Sanctum)
#include "cwo_trainrec.inl"                              // v5.188.51 Train a CPU part 1: record a match (CwoTrainRecord.txt)
#include "cwo_trainplay.inl"                             // v5.188.52 Train a CPU part 2: replay as a CPU (CwoTrainPlay.txt)
#include "cwo_dreamprobe.inl"                            // v5.123 Edgar's Dream CREATE editor: input probe
#include "cwo_ui.inl"                                    // v5.124 the CREATE editor's own UI renderer (Present hook)
#include "cwo_fps60.inl"                                 // v5.138 60 FPS display (30 Hz logic, blended extra frame)
#include "cwo_intro.inl"                                 // v5.71 intro poses at the countdown
#include "cwo_emote.inl"                                 // v5.188.55 emotes: View + D-pad in offline matches (MODS)
#include "cwo_daynight.inl"                              // v5.188.58 day and night during a match (CwoDayNight.txt, MODS)
#include "cwo_ultrawide.inl"                             // v5.189.0 ultrawide: the 3D view fills screens wider than 16:9 (MODS, offline)
#include "cwo_campaign.inl"                              // v5.190.0 campaign rules: CwoCampaign.txt (MODS, offline)
#include "cwo_stagetheme.inl"                            // v5.191.0 visual themes: CwoStageTheme.txt (MODS, offline)
#include "cwo_rating.inl"                                // v5.148.9 map rating (CwoMapRatings.txt)
#include "cwo_fusion.inl"                               // v5.17 the Skill Fuser
#include "cwo_effectlab.inl"                            // v5.193.0 the Lab's Effect Lab (the game's Character viewer, skill page)
#include "cwo_records.inl"                              // v5.21 Records
#include "cwo_feedback.inl"                             // v5.154 FEEDBACK, YOUR FEEDBACK, TO-DO LIST, INBOX
#include "cwo_music.inl"                                // v5.23 Stage Music
#include "cwo_draft.inl"                                // v5.25 Draft Arsenal
#include "cwo_stageview.inl"                             // v5.28 the stage behind the level list
#include "cwo_catalog.inl"                               // v5.184.2 the stage catalog after a late start
#include "cwo_cputype1.inl"                              // v5.185.0 CPUs think in Single Play (type-1 matches)
#include "cwo_madeguard.inl"                             // v5.186.0 skills you made: Aura Particles online
#include "cwo_stagelist.inl"                             // v5.43 the Map Slot 7 packs as stage-list rows
#include "cwo_widemenu.inl"                              // v5.87 (off) the character selects use the black bars
#include "cwo_selgrid.inl"                               // v5.87 the select grid at 82 %, COLORS on the far left
#include "cwo_arsguard.inl"                              // v5.28 arsenal guard (numbers with no skill)
#include "cwo_edmenu.inl"                                // v5.29 the Edgar's Dream pages in the game's menu
#include "cwo_createstage.inl"                           // v5.32 CREATE A STAGE in the game's windows
#include "cwo_create.inl"                                // v5.127 the CREATE editor's panels (every create mode)
#include "cwo_livetype.inl"                              // v5.29 the Xbox Live lobby's game type
#include "cwo_auratab.inl"                              // v5.31 the aura draw's per-fighter table moved
#include "cwo_expiry.inl"                               // v5.37 the partner test build's end date (only with CWO_EXPIRES)

// called by the patched exe right before every debug-text flush, on the game's render thread: quick, never throws
extern "C" __declspec(dllexport) void CwoOverlayFrame() {
    if (g_dp_flushing) return;                                   // v5.123.6 (in game: stack overflow c00000fd) - called from inside our own font flush
    if (!g_ready) return;
    __try {
#ifdef CWO_EXPIRES
        if (g_expired) { ex_draw(); return; }
#endif
        fly_step();
        fx_step();
        drop_watch();
        if (InterlockedExchange(&g_drop_request, 0)) drop_skill();
        if (InterlockedExchange(&g_return_request, 0)) return_object();
        if (InterlockedExchange(&g_pull_request, 0)) pull_nearest_object();
        ns_step();
        ns_trail_step(g_in_match != 0);                          // v3.1 projectile extras
        tc_step(g_in_match != 0);                                // v5.167 test casts (CwoTestCast.txt, the agent's tests)
        mr_step(g_in_match != 0);                                // v5.34 the Mirror's double
        ho_step(g_in_match != 0);                                // v5.131 the Hologram
        rmn_step(g_in_match != 0);                               // v5.188.23 Red Moon
        fu_step(g_in_match != 0);                                // v5.17 fused skills: records, the added effect
        rc_step(g_in_match != 0);                                // v5.21 Records: each offline match's result
        mu_step(g_in_match != 0);                                // v5.23 Stage Music: previews, the menu's music back
        lv_frame();                                              // v5.28 the stage behind the level list: list closed?
        { static unsigned fr; fr++; if ((fr % 30) == 15) chara_catalog_check(); if ((fr % 30) == 20) epa_check(); }   // v5.186.8 game thread
        forge_frame();
        ed_step();                                               // v3.4 Edgar's Dream: mode (lives), school, spectator
        ed_set_step();                                           // v5.165 Create a Skill / TEST: your own arsenal set
        sr_step();                                               // v5.166 Edit Arsenal's picker: six school icons
        cc_prev_step();                                          // v3.7 Create a Character live preview
        el_step();                                               // v5.193.0 the Effect Lab
        co_prev_step();                                          // v5.61 Create an Outfit preview
        cu_step();                                               // v5.35 the character in use, built for the select cell
        slot_step();                                             // v4.1 the select screen's created-character cell
        dbtl_step();                                             // v5.11 Download Battle: the start with the choices
        qplayer_step();                                          // v5.14.1 Create a Quest: play as
        bigstage_step();                                         // v5.15 the Underground: the hub's fall-out line
        portrait_step();                                         // v5.40 Map Slot 7's picture = the chosen pack's
        s6_tex_step();                                           // v5.159.3 the Illusion mark in WusMn's texture 2
        so_step();                                               // v5.77 the COLORS outfit's face in the select cell
        lobstrip_step();                                         // v5.91 the lobby rows' strips
        pp_hook();                                               // v5.78 the post pass's Present hook (once)
        hf_upload_step();                                        // v5.67 HUD faces (render thread)
        bi_step();                                               // v5.47 the stock pointers mirrored
        sb_step();                                               // v5.50.1 the override common.bin over the loaded bank
        bn_step();                                               // v5.57 the BFR lobby bytes in the player-info packet
        of_counts_step();                                        // v5.63 15 outfits for every fighter
        lf_step();                                               // v5.52.2 Lab manager size check
        // lw_step();  v5.47.2 Lab page-field write watch: found the 5.47.0 cause; off since 5.48 (frees DR0)
        bs_step();                                               // v5.45 the bigger skill table + the skill lists
        s6_step();                                               // v5.159 the Illusion mark in the loaded Lab image
        bfr_step();                                              // v5.56 the Battle for Reality skill pack
        ls_step();                                               // v5.11 the Xbox Live session's win / loss
        fb_step();                                               // v5.154 the last stage / mode for FEEDBACK
        cb_step();                                               // v5.88 COLORS into the left bar (cwo_colorbar.inl)
        ad_step();                                               // v5.64 Arsenal Data per stage
        mx_name_step();                                          // v5.64.4 the lobby's Level line: the pack's name
        hud_step();                                              // v5.65 HUD presets
        hf_cache_step();                                         // v5.67 HUD faces: the character rows' models
        in_step();                                               // v5.71 intro poses at the countdown
        em_step();                                               // v5.188.55 emotes
        dn_step();                                               // v5.188.58 day and night
        uw_step();                                               // v5.189.0 ultrawide
        cp_step();                                               // v5.190.0 campaign rules
        st_step();                                               // v5.191.0 visual themes
        th_step();                                               // v5.74 holiday themes
        ofx_live();                                              // v5.79 the effect editor's rows
        rm_step();                                               // v5.81 Realistic Mode
        lg_step();                                               // v5.121 low gravity stages
        tr_step();                                               // v5.188.51 Train a CPU: record (offline, read only)
        tp_step();                                               // v5.188.52 Train a CPU: replay (offline)
        dp_frame();                                              // v5.123 Dream input probe
        kb_pages_frame();                                        // v5.188.36 keyboard on the loader's menu pages
        shield_live_test();                                      // v5.163.0 crash shield in-game proof (test file only)
        cr_step();                                               // v5.86 CPUs revive downed teammates
        ks_step(g_in_match != 0);                                // v5.129 Revive Mend + Body Pull
        tv_step();                                               // v5.132 quieter title screen
        fp_cfg();                                                // v5.138 CwoFps.txt
        pp_vis_cfg();                                            // v5.83 Visions (CwoVisions.txt)
        ofx_step();                                              // v5.75 outfit effects
        dg_step();                                               // v5.103 a glow on downed fighters
        sv_step();                                               // v5.187.0 spawn view (CwoSpawnView.txt)
        spx_step();                                              // v5.139 stage effects
        fz_step();                                               // v5.148.1 effect size per skill
        rt_step();                                               // v5.148.9 map rating window
        gt_maptest_step();                                       // v5.145 Map Test
        ofx_aura_step();                                         // v5.93 an aura colour per outfit
        dp_draw();                                               // v5.123.3 the Dream probe draws in every session (CREATE is its own full-screen session)
        if (g_forge_session && (!(g_dp_on && g_ui_state == 1) || g_fmenu)) forge_draw();   // v5.127 an open pad menu shows in the viewport   // v5.124.1 the CREATE screen replaces the old creator panel
        else if (g_cc_prev == 2 || g_co_prev == 2) cc_prev_draw();
        else { draw_panel(); ed_match_draw(); sn_draw(); spx_pop_draw(); ed_six_banner(); slot_draw(); dbtl_draw(); if (!ti_active()) { th_banner(); th_draw(); } sel_draw(); ml_draw(); fb_draw(); tb_draw(); hp_draw(); ti_draw(); el_draw(); }   // v5.188.25 title intro on top
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        InterlockedExchange(&g_ready, 0);                      // never touch the game's text again this session
    }
}

// v5.28.2 test helper (owner 2026-10-27; the keyboard is player 1 after the title, so split screen's player 2 needs the connected pad):
// a file CwoAutoPad.txt in the loader's data folder with button names (A B X Y START BACK UP DOWN LEFT RIGHT LB RB) is
// read once and deleted, and the pad presses those buttons together for 150 ms (filtered_read adds them, as f_press).
// Offline only; nothing happens without the file.
static void autopad_poll() {
    wchar_t p[MAX_PATH];
    swprintf_s(p, MAX_PATH, L"%sCwoAutoPad.txt", g_dir);
    if (GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES) return;
    char line[128] = "";
    FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"r") == 0 && f) { if (!fgets(line, sizeof(line), f)) line[0] = 0; fclose(f); }
    DeleteFileW(p);
    if (ed_online()) { logf("auto pad: refused online"); return; }
    static const struct { const char *name; LONG bit; } B[] = {{"START", 0x1}, {"BACK", 0x2}, {"A", 0x4}, {"B", 0x8}, {"X", 0x10},
        {"Y", 0x20}, {"UP", 0x40}, {"DOWN", 0x80}, {"LEFT", 0x100}, {"RIGHT", 0x200}, {"LB", 0x400}, {"RB", 0x800}, {"LS", 0x1000}, {"RS", 0x2000}};   // v5.123.2 + stick clicks (L3 / R3)
    LONG bits = 0;
    bool self = false;                                         // v5.30.1 "LOADER ...": the loader's own menus, not the game
    char *ctx = NULL;
    LONG rx = 0, ry = 0, ms = 0, lx = 0, ly = 0, lt = 0, rt = 0;
    for (char *t = strtok_s(line, " \t\r\n", &ctx); t; t = strtok_s(NULL, " \t\r\n", &ctx)) {
        if (!_strnicmp(t, "RX=", 3)) rx = (LONG)(atof(t + 3) * 1000.0);     // v5.118 right stick, -1..1
        if (!_strnicmp(t, "RY=", 3)) ry = (LONG)(atof(t + 3) * 1000.0);
        if (!_strnicmp(t, "MS=", 3)) ms = atol(t + 3);
        if (!_strnicmp(t, "LX=", 3)) lx = (LONG)(atof(t + 3) * 1000.0);     // v5.122 left stick, -1..1
        if (!_strnicmp(t, "LY=", 3)) ly = (LONG)(atof(t + 3) * 1000.0);
        if (!_strnicmp(t, "LT=", 3)) lt = (LONG)(atof(t + 3) * 1000.0);     // v5.188.20 left / right trigger, 0..1
        if (!_strnicmp(t, "RT=", 3)) rt = (LONG)(atof(t + 3) * 1000.0);
        if (!_stricmp(t, "LOADER")) self = true;
        if (!_strnicmp(t, "KEY=", 4)) {                         // v5.193.0 the agent's test: the game's key table (no focus needed)
            static const struct { const char *n; LONG b; } KB[] = {{"ENTER", 0x1}, {"UP", 0x40}, {"DOWN", 0x80}, {"LEFT", 0x100}, {"RIGHT", 0x200}, {"A", 0x4}, {"B", 0x8}, {"X", 0x10}, {"Y", 0x20}};
            for (const auto &k : KB) if (!_stricmp(t + 4, k.n)) { InterlockedExchange(&g_key_test, k.b); InterlockedExchange64(&g_key_test_until, (LONGLONG)(GetTickCount64() + 150)); logf("auto pad: key %s on the game's key table (test)", k.n); }
        }
        if (!_strnicmp(t, "EMOTE=", 6)) { InterlockedExchange(&g_em_req, atol(t + 6)); logf("auto pad: emote %ld asked (test)", atol(t + 6)); }   // v5.192.3 the agent's emote test
        for (const auto &b : B) if (!_stricmp(t, b.name)) bits |= b.bit;
    }
    if ((rx || ry || lx || ly || lt || rt) && ms > 0) {
        InterlockedExchange(&g_ap_lt, lt < 0 ? 0 : (lt > 1000 ? 1000 : lt));
        InterlockedExchange(&g_ap_rt, rt < 0 ? 0 : (rt > 1000 ? 1000 : rt));                                 // v5.118 (owner 2026-11-08)
        rx = rx > 1000 ? 1000 : (rx < -1000 ? -1000 : rx);
        ry = ry > 1000 ? 1000 : (ry < -1000 ? -1000 : ry);
        InterlockedExchange(&g_ap_rx, rx);
        InterlockedExchange(&g_ap_ry, ry);
        InterlockedExchange(&g_ap_lx, lx > 1000 ? 1000 : (lx < -1000 ? -1000 : lx));
        InterlockedExchange(&g_ap_ly, ly > 1000 ? 1000 : (ly < -1000 ? -1000 : ly));
        InterlockedExchange64(&g_ap_stick_until, (LONGLONG)(GetTickCount64() + (ULONGLONG)(ms > 20000 ? 20000 : ms)));
        logf("auto pad: right stick %ld,%ld left stick %ld,%ld /1000 for %ld ms", rx, ry, lx, ly, ms);
    }
    if (!bits) return;
    if (self) {
        InterlockedExchange64(&g_self_until, (LONGLONG)(GetTickCount64() + 150));
        InterlockedExchange(&g_self_bits, bits);
        logf("auto pad: loader menus pressed %03lX for 150 ms", bits);
        return;
    }
    f_press(bits, 150);
    rt_press((WORD)(((bits & 0x40) ? BTN_UP : 0) | ((bits & 0x200) ? BTN_RIGHT : 0) | ((bits & 0x80) ? BTN_DOWN : 0)));   // v5.148.10 the test pad votes too
    bn_pause_press((WORD)((bits & 0x20) ? BTN_Y : 0));        // v5.148.21 and toggles the pause-menu lock-on
    logf("auto pad: pressed %03lX for 150 ms", bits);
}

static DWORD WINAPI worker(LPVOID) {
    wchar_t exe[MAX_PATH] = {0};
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    logf("CWO-LOADER-01 v%s loaded into %ls, base %p, data folder %ls", CWO_VERSION, exe, (void *)g_base, g_dir);
#ifdef CWO_EXPIRES
    logf("partner test build: ends %d; date in force %d -> %s", CWO_EXPIRES, g_ex_date, g_expired ? "ENDED (loader off, notice shown)" : "running");
    if (g_expired) { if (host_ok()) InterlockedExchange(&g_ready, 1); return 0; }
#endif
    if (!host_ok()) { logf("host check failed (not the expected PDUWP.exe build); staying idle"); return 0; }
    if (g_store_mode) logf("store mode: game folder %ls (read only); overrides from %ls; memory patch %s", g_install, g_over,
                           g_mp_state == 1 ? "applied (38 ranges)" : g_mp_state == -1 ? "REFUSED (exe not clean) - loader idle" : "FAILED - loader idle");
    if (g_store_mode && g_mp_state == 1) logf("store mode: new code + tables first, then the code changes with %d game threads paused", g_mp_paused);
    else logf("loose-copy mode: the exe on disk carries the loader patches");
    if (g_store_mode && g_mp_state != 1) return 0;
    if (g_bfr_edition) logf("BFR mod pack edition (data folder %ls)", g_dir);
    pf_build_all();                                              // v5.58 the pack's game files, built from patches on this machine
    __try { bsh_startup(); } __except (EXCEPTION_EXECUTE_HANDLER) { logf("body shape: stopped by an exception"); }   // v5.180
    ln_install();                                                // v5.126 Map\maplight0.ssb for stages with no light
    HMODULE x = LoadLibraryW(L"xinput1_4.dll");
    HMODULE xu = LoadLibraryW(L"xinputuap.dll");
    if (!x) x = xu;
    if (x) {
        g_get = (PFN_GETSTATE)GetProcAddress(x, "XInputGetState");
        g_set = (PFN_SETSTATE)GetProcAddress(x, "XInputSetState");
    }
    if (xu && xu != x) g_set2 = (PFN_SETSTATE)GetProcAddress(xu, "XInputSetState");
    logf("controller libraries: xinput1_4 %s, xinputuap %s", x && x != xu ? "loaded" : "missing", xu ? "loaded" : "missing");
    pads_init();
    logf("controller API %s", g_get ? "ready" : "NOT available (markers disabled)");
    if (!g_get) return 0;
    logf("file monitor: %d of 10 file imports watched -> file_requests.csv + read_ranges.csv", g_hooked);
    logf("FORGE: main-menu native 2.8 %s", g_forge_native_ok ? "installed (STAGE CREATOR ready)" : "NOT installed (STAGE CREATOR must not be chosen)");
    mx_load_list();
    portrait_load();                                           // v5.40 stage portraits (Map\mx_portraits.txt)
    so_load();                                                 // v5.77 select portraits per outfit
    ls_load();                                                 // v5.91 lobby face strips
    sl_build_list();                                           // v5.43 the packs listed in the stage lists
    sl_fam_load();                                             // v5.157.5 the compact list's remembered variants
    sl_cap_load();                                             // v5.158 the list cap's test file
    sk_load_list();
    ns_load();
    bs_load();                                                 // v5.45 the skill lists
    bfr_load();                                                // v5.56 CwoBFR.txt + CwoBFR_state.txt
    ns_off_pack();                                             // v5.109 saved new skills off the pack's records
    if (host_ok()) stl_lock_install();                         // v5.60 the locked boss skills in the stock list
    if (host_ok()) s6_install();                               // v5.159 school 6: the Lab's school marks
    if (host_ok()) hud_install();                              // v5.65 HUD presets (the bars hook)
    if (host_ok()) sp_install();                               // v5.188.22 STR formula 21 for Charged Ray
    // if (host_ok()) wm_install();   // v5.87 tried 2026-11-03 09:09, reverted: the scissor is not what cuts the menus at x 0 (MODLOG)
    if (host_ok()) { hf_load(); hf_install(); }                // v5.67 HUD faces
    if (host_ok()) rm_install();                               // v5.81 Realistic Mode: the projectile updates wrapped
    // v5.149.2 (owner 2026-11-11): the BFR edition makes none of these skills; MODS keeps them
    // v5.151.0 (owner 2026-11-12): the BFR edition makes Mirror,
    // Spiritual Projection and Hologram too; Revive Mend and Body Pull stay MODS-only (not in the owner's list)
    mr_ensure_skill();                                         // v5.34 the Mirror skill (+ v5.148.19 Spiritual Projection)
    ho_ensure_skill();                                         // v5.131 the Hologram skill
    if (!g_bfr_edition) ks_ensure_skills();                    // v5.129 Revive Mend + Body Pull
    else logf("BFR edition: Revive Mend and Body Pull are MODS-only (not made)");
    mx_restore_choice();
    if (hud_install(g_base + RVA_HUD_DRAW_SLOT, g_base + RVA_HUD_DRAW))
        logf("hide-HUD ready: battle HUD draw slot %p -> wrapper (fly page, D-pad Up)", (void *)(g_base + RVA_HUD_DRAW_SLOT));
    if (player_draw_install(g_base + RVA_PLAYER_DRAW_SLOT, g_base + RVA_PLAYER_DRAW))      // v2.1 Forge: hide my character
        logf("hide-player ready: player draw slot %p -> wrapper (Forge menu)", (void *)(g_base + RVA_PLAYER_DRAW_SLOT));
    InterlockedExchange(&g_ready, 1);
    WORD prev = 0;
    int last_stage = -2;
    ULONGLONG back_down = 0;
    bool back_chord = false;
    unsigned tick = 0;
    for (;;) {
        Sleep(16);
        if ((tick++ % 60) == 0) filter_install();               // about once a second: covers pads plugged in later
        if ((tick % 180) == 0) file_requests_flush();          // every ~3 s while new requests come in
        if ((tick % 12) == 0) autopad_poll();                   // v5.28.2 test helper: CwoAutoPad.txt
        if ((tick % 30) == 0) catalog_check();                  // v5.184.2 the stage catalog the game read (once)
        // v5.186.8 (2026-11-15: two start-up crashes at exe+1cbdc0, 5 s after these re-parses ran on THIS pad thread while the
        // game thread ran its title tasks): the character / CPU table refills run on the game thread now (frame hook below)
        if ((tick % 6) == 3) cputype1_step();                   // v5.185.0 CPU programs in offline type-1 matches
        mg_step();                                              // v5.186.0 online: skills you made -> Aura Particles
        stl_rows_step();                                        // v5.186.12 View Stock rows = the skills listed
        if ((tick % 6) == 0) env_watch();                       // v5.148.1 log every environment change (cwo_stagefx.inl)
        WORD b = 0;
        if (!read_raw_buttons(&b)) {                            // before the filter is on: XInput (unfiltered then)
            XSTATE s;
            // v5.188.36 no controller at all: the step still runs (pages, sessions, the way back to the main menu) on the
            // keyboard's buttons and the test pad's - it used to stop here, so SINGLE PLAY showed the game's debug list
            if (g_get(0, &s) != 0) b = GetTickCount64() < (ULONGLONG)g_self_until ? self_xi() : 0;
            else b = s.Gamepad.wButtons;
        }
        b |= (WORD)g_kb_bits;                                   // v5.188.36 the keyboard on the loader's menu pages
        WORD pressed = b & ~prev, released = prev & ~b;
        rt_press(pressed);                                      // v5.148.9 map rating votes (d-pad, only while the window is open)
        bn_pause_press(pressed);                                // v5.148.21 pause menu: Y = teammate lock-on on / off (offline)
        prev = b;
        if (pressed & BTN_BACK) InterlockedExchange64(&g_view_since, (LONGLONG)GetTickCount64());
        if (!(b & BTN_BACK)) InterlockedExchange64(&g_view_since, 0);
        float p[3];
        int stage = -1;
        bool in_match = read_player(0, p, &stage);
        InterlockedExchange(&g_in_match, in_match ? 1 : 0);
        // v5.5 / v5.10.1: the first 15 s of every match (the 3-2-1 and the mulligan) and story matches leave View / Back to the
        // game; worked out here, before a Forge session or a page can 'continue', so the input filter always sees it
        static ULONGLONG s_match_t0;
        static bool s_was_in;
        if (in_match && !s_was_in) s_match_t0 = GetTickCount64();
        s_was_in = in_match;
        bool game_back = in_match && (cwo_story_match() || GetTickCount64() - s_match_t0 < 15000);
        InterlockedExchange(&g_game_view, game_back ? 1 : 0);
        // v2.8 (owner: template pieces listed as '?'): Map Slot 7 keeps stage id 10 while its file changes
        if (in_match && (stage != last_stage || !names_current(stage))) {
            last_stage = stage;
            recount(stage);
            load_names(stage);
            InterlockedExchange(&g_sel, -1);
            memset(g_has_orig, 0, sizeof(g_has_orig));
            g_last_moved = -1;
        }
        if (g_cc_prev == 2) { cc_input(b, pressed); continue; } // v3.7: the preview owns the controller
        if (g_el_restart == 2 && !in_match) { InterlockedExchange(&g_el_restart, 0); lob_start(); continue; }   // v5.196.0 Effect Lab reload: the lobby is back
        if (g_el && in_match && !g_fc_on && !g_forge_session && !g_ed_open && !el_game_menu()) { if (el_input(b, pressed)) continue; }   // v5.194.0 the Effect Lab match
        if (g_co_prev == 2) { co_input(b, pressed); continue; } // v5.69 the outfit preview too
        if (g_epg_on && g_ed_page == EDP_OUTFIT && !in_match) {   // v5.101.1 the game-style page gets one press per press:
            static ULONGLONG rep_t;                                    // holding L / R on Effect size / speed repeats here
            WORD lr = b & (BTN_LEFT | BTN_RIGHT);
            int fi = ed_fld(EDP_OUTFIT, g_ed_row);
            int kind = fi >= 0 && fi < g_edc[EDP_OUTFIT].nf ? g_edc[EDP_OUTFIT].f[fi].kind : -1;
            ULONGLONG now = GetTickCount64();
            if (pressed & lr) rep_t = now + 400;
            else if (lr && (kind == FK_OFXPCT || kind == FK_OFXSPD) && now >= rep_t) { rep_t = now + 50; ed_change(EDP_OUTFIT, g_ed_row, (lr & BTN_RIGHT) ? 1 : -1); }
        }
        if (b) g_pad_last_ms = GetTickCount64();                // v5.161.4 the last time a button was held (the thank-you page waits)
        if (g_th_popup == 2) { th_input(pressed); continue; }  // v5.74.4 What's New holds the pad until OK
        if (g_ml_open) { ml_input(pressed); continue; }          // v5.80 the Mod List too
        if (g_fb_open) { fb_input(pressed); continue; }          // v5.154 FEEDBACK / TO-DO LIST
        if (g_tbv_show && !g_in_match) { tb_input(pressed); continue; }
        // (v5.86.2: OPTIONS > HUD PRESET is the game's own menu now - no loader input)
        if (g_db_state == 1) { dbtl_input(pressed); continue; } // v5.11 the Download Battle choices own it too
        if (g_slot_drop) {                                       // v4.3 the list of created characters
            if (g_slot_n > 0 && (pressed & BTN_UP)) InterlockedExchange(&g_slot_drop_sel, (g_slot_drop_sel + g_slot_n - 1) % g_slot_n);
            if (g_slot_n > 0 && (pressed & BTN_DOWN)) InterlockedExchange(&g_slot_drop_sel, (g_slot_drop_sel + 1) % g_slot_n);
            if (pressed & BTN_A) { InterlockedExchange(&g_slot_pick, g_slot_drop_sel + 1); InterlockedExchange(&g_slot_drop, 0); rumble(1); }
            if (pressed & BTN_B) InterlockedExchange(&g_slot_drop, 0);
            continue;
        }
        if (g_slot_hold[0] && (pressed & BTN_B)) g_slot_hold[0] = 0;   // v4.3: B out of the COLORS window: the cell again
        if (g_slot_on && (pressed & BTN_A) && g_slot_n > 0) {     // v4.3: A on the cell: the list
            InterlockedExchange(&g_slot_drop_sel, g_slot_idx);
            InterlockedExchange(&g_slot_drop, 1);
            rumble(1);
            continue;
        }
        if (g_slot_on && (pressed & (BTN_LB | BTN_RB))) {          // v4.1: LB / RB on the created-character cell
            InterlockedExchange(&g_slot_req, (pressed & BTN_RB) ? 1 : -1);
            rumble(1);
            continue;
        }
        if (cr_input(b, pressed, in_match)) { forge_input(0, 0, in_match); continue; }   // v5.127 CREATE: commands; the pad on the panels
        // v5.133.3 the Skill Vault owns the controller while it is up (outside an Edgar's Dream session forge_input skips pages)
        if (InterlockedExchange(&g_vt_req, 0) && !in_match && !g_ed_open) { vt_open(); rumble(1); continue; }   // v5.136
        if (g_vt_shown) {
            if (!g_ed_open || g_ed_page != EDP_VAULT || in_match) InterlockedExchange(&g_vt_shown, 0);
            else { if (pressed) vt_input(pressed); continue; }
        }
        if (forge_input(b, pressed, in_match)) continue;       // v2.0: a Forge session owns the controller
        // v0.3.1 (owner: holding BACK ~3 s is the game's forfeit): a quick BACK tap on its own toggles the panel
        // v5.5 (owner 18:30: "at the very start, when I pressed the back button / view, it opened up the panel instead of
        // activating the mulligan"): game_back (worked out above) - the first 15 s of every match and story matches
        if (in_match && (pressed & BTN_RB)) InterlockedExchange(&g_lk_press, 1);   // v5.6 lock-on tracer (the game still gets RB)
        // v5.146 the owner's marker (owner 2026-11-10; "press the left bumper 3 times for it to start"): LB or RB 3 times within 1.5 s in an offline match
        // = a mark: stage (pack file on Map Slot 7, else the stage number), button, fighter 1's position -> CwoMarks.txt
        // (the loader data folder) and the log; the game still gets the presses
        if (in_match && !ed_online() && (pressed & (BTN_LB | BTN_RB)) && !(b & BTN_BACK)) {
            static ULONGLONG mk_t[2][3]; static int mk_n[2]; static int mk_count;
            int w = (pressed & BTN_LB) ? 0 : 1;
            ULONGLONG now = GetTickCount64();
            mk_t[w][mk_n[w] % 3] = now; mk_n[w]++;
            if (mk_n[w] >= 3 && now - mk_t[w][(mk_n[w] - 3) % 3] <= 1500) {
                mk_n[w] = 0;
                float pos[3] = {0, 0, 0}; int stage = -1; char pack[24] = "";
                __try {
                    ULONG_PTR a0 = cwo_actors()[0];
                    if (a0) memcpy(pos, (const void *)(a0 + ACTOR_POS), 12);
                    stage = *(int *)(g_base + RVA_CUR_STAGE);
                    LONG k = g_mx_cur;
                    if (stage == LIST_STAGE && k >= 0 && k < g_mx_count) strcpy_s(pack, g_mx[k].file);
                } __except (EXCEPTION_EXECUTE_HANDLER) {}
                mk_count++;
                wchar_t mp[MAX_PATH]; swprintf_s(mp, MAX_PATH, L"%sCwoMarks.txt", g_dir);
                FILE *mf = NULL;
                if (cwo_wfopen(&mf, mp, L"a") == 0 && mf) {
                    SYSTEMTIME st; GetLocalTime(&st);
                    fprintf(mf, "%04d-%02d-%02d %02d:%02d:%02d %s stage %d %s %.1f %.1f %.1f\n", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
                            st.wSecond, w ? "RB" : "LB", stage, pack[0] ? pack : "-", pos[0], pos[1], pos[2]);
                    fclose(mf);
                }
                logf("owner mark %d: %s x3 at (%.1f, %.1f, %.1f), stage %d %s", mk_count, w ? "RB" : "LB", pos[0], pos[1], pos[2], stage, pack);
                char msg[64]; sprintf_s(msg, "MARK %d saved (%s)", mk_count, w ? "RB" : "LB"); set_status(msg);
            }
        }
        // v5.9 (owner): the list's own D-pad Left / Right
        // v5.133 the Skill Vault: Back + Y on View Stock (owner)
        if (!in_match && (b & BTN_BACK) && (pressed & BTN_Y) && !g_ed_open && vt_stock_view()) { vt_open(); rumble(1); continue; }
        if (!in_match && (pressed & (BTN_LEFT | BTN_RIGHT)) && !(b & BTN_BACK) && ars_list_focused()) {
            InterlockedExchange(&g_ars_req, (pressed & BTN_RIGHT) ? 1 : -1);
            rumble(1);
            continue;
        }
        if (game_back) {                                        // v5.0: a story match - View / Back is the game's (mulligan)
            if (g_pinned) InterlockedExchange(&g_pinned, 0);    // the input filter would hide View from the game
            back_chord = true;
        }
        if (pressed & BTN_BACK) { back_down = GetTickCount64(); back_chord = game_back; }
        if ((b & BTN_BACK) && (pressed & ~BTN_BACK)) back_chord = true;
        // v5.106 (owner 2026-11-04): the BFR edition has no quick panel
        if (g_bfr_edition && g_pinned) InterlockedExchange(&g_pinned, 0);
        // v5.142 (owner 2026-11-10): no quick panel while the
        // fighter setup has two human players (records 0 and 1 at 0x1404bc420, 0x48 each: +0 used, +0x10 human - the
        // split-screen test of cwo_gametype.inl gt_fill)
        bool split2 = false;
        __try { const char *rec = (const char *)(g_base + 0x4bc420);
                split2 = *(const int *)rec && *(const int *)(rec + 0x48) && *(const int *)(rec + 0x10) && *(const int *)(rec + 0x48 + 0x10); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        if (split2 && g_pinned) InterlockedExchange(&g_pinned, 0);
        if ((released & BTN_BACK) && !back_chord && GetTickCount64() - back_down < 600 && !g_bfr_edition && !split2) {
            LONG pin = !g_pinned;
            InterlockedExchange(&g_pinned, pin);
            set_status(pin ? "Quick panel ON (tap BACK to hide)" : "Quick panel OFF");
            continue;
        }
        // v3.5 (owner): View + LB switches player 1
        // between you and the game's CPU (done on the game thread, ed_spectator_step)
        if (in_match && (b & BTN_BACK) && (pressed & BTN_LB)) { InterlockedExchange(&g_ed_spec_req, 1); rumble(1); continue; }
        // v5.9 (owner): View + RB = the free camera; while it
        // is on, B ends it, Y goes to the next fighter, X shows / hides the HUD (cwo_freecam.inl, on the game thread)
        if (in_match && (b & BTN_BACK) && (pressed & BTN_RB)) { InterlockedExchange(&g_fc_req, 1); rumble(1); continue; }
        // v5.188.55 emotes (owner voice memo 2026-11-21): View + D-pad while standing; offline, MODS only (cwo_emote.inl).
        // v5.188.64: Up our own Look around (slot 1), Right our own Wave, Left our own Bow, Down the game's poses in turn
        // (victory 132, intro 130, second intro 131). (5.188.62-63 had Wave / Bow on View + X / Y - those are the marker
        // tool's delete / log buttons in a match, so they went back to it.)
        // v5.192.4 (owner 2026-11-22; "why dont we switch it to rb instead?"): hold RB
        // and press the D-pad while standing - Up intro pose (EXMOT0, slot 130), Right second intro (EXMOT1, 131), Down victory
        // (EXMOT2, 132), Left third intro (EXMOT4, 134). RB is the game's lock-on switch (FUN_1402531d0, MODLOG 2026-10-2x): no
        // move, no jump. View is the quick panel only again.
        if (in_match && !g_fc_on && !ed_online() && (b & BTN_RB) && !(b & BTN_BACK) && (pressed & (BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT))) {
            LONG req = (pressed & BTN_UP) ? 130 : (pressed & BTN_RIGHT) ? 131 : (pressed & BTN_DOWN) ? 132 : 134;
            InterlockedExchange(&g_em_req, req);
            rumble(1); continue;
        }
        if (in_match && g_fc_on) {
            if (pressed & BTN_B) InterlockedExchange(&g_fc_req, 1);
            if (pressed & BTN_Y) InterlockedExchange(&g_fc_next, 1);
            if (pressed & BTN_X) InterlockedExchange(&g_fc_hud, 1);
            if (pressed & BTN_LB) InterlockedExchange(&g_ph_pause_req, 1);       // v5.20 photo mode
            if (pressed & BTN_UP) InterlockedIncrement(&g_ph_speed_req);
            if (pressed & BTN_DOWN) InterlockedDecrement(&g_ph_speed_req);
            continue;
        }
        // v0.5: right-stick click switches the panel between the objects page and the stage packs page
        if (g_pinned && in_match && (pressed & BTN_RTHUMB)) {
            InterlockedExchange(&g_page, (g_page + 1) % 4);     // objects -> stage packs -> fly -> skills -> objects
            g_fly_landed = false;                               // the fly page always starts flying
            continue;
        }
        if (g_pinned && in_match && g_page == 1 && (pressed & (BTN_LEFT | BTN_RIGHT | BTN_UP | BTN_DOWN)) && !(b & BTN_BACK)) {
            if (g_mx_count > 0) {
                if (pressed & (BTN_LEFT | BTN_RIGHT))
                    InterlockedExchange(&g_mx_sel, (g_mx_sel + ((pressed & BTN_RIGHT) ? 1 : g_mx_count - 1)) % g_mx_count);
                char msg[96];
                if (pressed & BTN_UP) {
                    mx_apply(g_mx_sel);
                    sprintf_s(msg, "Map Slot 7 = %.40s", g_mx[g_mx_sel].name);
                    set_status(msg);
                    rumble(1);
                }
                if (pressed & BTN_DOWN) { mx_apply(-1); set_status("Map Slot 7 = its own stage again"); rumble(1); }
            }
            continue;
        }
        // panel on: the D-pad edits objects without BACK (objects page only; v1.2: before, this branch also caught the
        // fly and skills pages' D-pad, so their own branches below never ran)
        if (g_pinned && in_match && g_page == 0 && (pressed & (BTN_LEFT | BTN_RIGHT | BTN_UP | BTN_DOWN)) && !(b & BTN_BACK)) {
            int n = *(unsigned short *)(g_base + RVA_INSTANCE_COUNT);
            if ((pressed & (BTN_LEFT | BTN_RIGHT)) && n > 0) {
                LONG next = g_sel + ((pressed & BTN_RIGHT) ? 1 : -1);
                if (next < -1) next = n - 1;
                if (next >= n) next = -1;                   // -1 = "nearest"
                InterlockedExchange(&g_sel, next);
            }
            if (pressed & BTN_UP) { InterlockedExchange(&g_pull_request, 1); rumble(1); }      // on the game thread
            if (pressed & BTN_DOWN) { InterlockedExchange(&g_return_request, 1); rumble(1); }
            continue;
        }
        // v0.4: with the panel on, A / RB / B / X / Y work alone (the input filter hides them from the game);
        // BACK + button still works with the panel off
        if (g_pinned && in_match && g_page == 2 && (pressed & (BTN_LEFT | BTN_RIGHT | BTN_UP | BTN_DOWN))) {
            if (pressed & BTN_UP) {
                g_hide_hud = !g_hide_hud;
                set_status(g_hide_hud ? "Game HUD hidden (D-pad Up to show)" : "Game HUD shown");
                rumble(1);
            }
            if (pressed & BTN_DOWN) {
                g_fly_landed = !g_fly_landed;
                set_status(g_fly_landed ? "Landed: walls and gravity back (D-pad Down to fly)" : "Flying again");
                rumble(1);
            }
            continue;
        }
        // skills page: D-pad L/R one skill, X/Y ten back/forward, Up drops the capsule (on the game thread)
        if (g_pinned && in_match && g_page == 3 && !(b & BTN_BACK) &&
            (pressed & (BTN_LEFT | BTN_RIGHT | BTN_UP | BTN_DOWN | BTN_A | BTN_B | BTN_X | BTN_Y | BTN_RB))) {
            if (g_sk_count > 0) {
                int step = (pressed & BTN_RIGHT) ? 1 : (pressed & BTN_LEFT) ? -1 : (pressed & BTN_Y) ? 10 : (pressed & BTN_X) ? -10 : 0;
                if (step) InterlockedExchange(&g_sk_sel, ((g_sk_sel + step) % g_sk_count + g_sk_count) % g_sk_count);
                if (pressed & BTN_UP) { InterlockedExchange(&g_drop_request, 1); rumble(1); }
                // v1.2 aura FX: Down puts the chosen skill's effect on your aura (again on the same skill: off),
                // A cycles the effect's part, B switches "only while the aura shows" / "always"
                if (pressed & BTN_DOWN) {
                    LONG id = g_sk[g_sk_sel].id;
                    bool on = !(g_fx_on && g_fx_skill == id);
                    InterlockedExchange(&g_fx_skill, id);
                    g_fx_on = on;
                    char msg[96];
                    if (on) sprintf_s(msg, "%.40s on your aura", g_sk[g_sk_sel].name);
                    else sprintf_s(msg, "Aura effect off");
                    set_status(msg);
                    InterlockedExchange(&g_fx_change, 1);
                    rumble(1);
                }
                if (pressed & BTN_A) {
                    InterlockedExchange(&g_fx_track, (g_fx_track + 1) % 6);
                    char msg[64];
                    sprintf_s(msg, "Aura effect part %ld", g_fx_track);
                    set_status(msg);
                    InterlockedExchange(&g_fx_change, 1);
                    rumble(1);
                }
                if (pressed & BTN_B) {
                    g_fx_always = !g_fx_always;
                    set_status(g_fx_always ? "Aura effect: always shown" : "Aura effect: only while your aura shows");
                    InterlockedExchange(&g_fx_change, 1);
                    rumble(1);
                }
            }
            continue;
        }
        // markers work on the objects and fly pages; the stage packs page uses no marker buttons (owner)
        if (g_pinned && g_page == 1 && (pressed & (BTN_A | BTN_B | BTN_X | BTN_Y | BTN_RB))) continue;
        bool single = g_pinned && in_match && !(b & BTN_BACK);
        if (!(single || (b & BTN_BACK)) || !(pressed & (BTN_A | BTN_B | BTN_X | BTN_Y | BTN_RB))) continue;
        if (!in_match) { logf("button pressed but player 1 is not in a match"); continue; }
        char msg[96];
        if (pressed & BTN_A) { add_marker("SPAWN", stage, p); recount(stage); sprintf_s(msg, "Spawn %ld placed", g_counts[0]); set_status(msg); rumble(1); }
        else if (pressed & BTN_B) { add_marker("KILLZONE", stage, p); recount(stage); sprintf_s(msg, "Kill-zone point %ld placed", g_counts[2]); set_status(msg); rumble(1); }
        else if (pressed & BTN_RB) { add_marker("CAPSULE", stage, p); recount(stage); sprintf_s(msg, "Capsule spot %ld placed", g_counts[1]); set_status(msg); rumble(1); }
        else if (pressed & BTN_X) { bool ok = delete_last(stage); recount(stage); set_status(ok ? "Last marker deleted" : "Nothing to delete"); rumble(ok ? 2 : 3); }
        else if (pressed & BTN_Y) {
            logf("position stage %d (%.3f, %.3f, %.3f)", stage, p[0], p[1], p[2]);
            sprintf_s(msg, "Position logged (%.1f, %.1f, %.1f)", p[0], p[1], p[2]);
            set_status(msg);
            rumble(1);
        }
    }
}

// ---- v0.8 file monitor (owner) ----
// The game opens files through 5 imports (dumpbin, clean exe): CreateFile2 / GetFileAttributesW / FindFirstFileW /
// CreateDirectoryW (api-ms-win-core-file-l1-2-1) and fopen (api-ms-win-crt-stdio-l1-1-0). Their import-table slots
// in memory point at wrappers that note the path and whether it was found, then return the real call's result
// unchanged (last error kept). No game code changes. The unique paths go to <TEMP>\CwoLoader\file_requests.csv.
struct FileReq { unsigned count; DWORD result; ULONGLONG first_ms; int first_stage; char api[8]; char callers[72];
                 unsigned reads; ULONGLONG read_bytes; };
static std::map<std::wstring, FileReq> *g_files;
static CRITICAL_SECTION g_file_lock;
static volatile LONG g_files_dirty;
static ULONGLONG g_start_ms;

static int current_stage() {
    int stage = -1;
    if (g_ready) __try { stage = *(int *)(g_base + RVA_STAGE); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return stage;
}

// v0.9 "who asked": up to 4 frames of the call stack that lie inside PDUWP.exe, as exe addresses (0x14xxxxxxx) for
// Ghidra; the loader's own frames and Windows' frames are skipped
static void capture_callers(char *out, size_t n) {
    out[0] = 0;
    void *fr[24];
    USHORT c = RtlCaptureStackBackTrace(1, 24, fr, NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(g_base + ((IMAGE_DOS_HEADER *)g_base)->e_lfanew);
    ULONG_PTR end = g_base + nt->OptionalHeader.SizeOfImage;
    int kept = 0;
    for (USHORT i = 0; i < c && kept < 4; i++) {
        ULONG_PTR a = (ULONG_PTR)fr[i];
        if (a < g_base || a >= end) continue;
        char one[24];
        sprintf_s(one, "%s0x%llx", kept ? ">" : "", (unsigned long long)(0x140000000ULL + (a - g_base)));
        strcat_s(out, n, one);
        kept++;
    }
}

static void note_file(const wchar_t *path, const char *api, DWORD result) {
    if (!path || !g_files) return;
    int stage = current_stage();
    char callers[72];
    capture_callers(callers, sizeof(callers));
    EnterCriticalSection(&g_file_lock);
    FileReq &e = (*g_files)[path];
    if (e.count++ == 0) {
        e.first_ms = GetTickCount64() - g_start_ms;
        e.first_stage = stage;
        strcpy_s(e.api, api);
        strcpy_s(e.callers, callers);
    }
    e.result = result;
    LeaveCriticalSection(&g_file_lock);
    InterlockedExchange(&g_files_dirty, 1);
}

// v0.9 "inside the packs": which parts of each file are read. Open handles / C streams are mapped to their path;
// every read adds (offset, size) to that path's ranges (offset from the OVERLAPPED block, else the file pointer, or
// ftell for C streams). build_file_map.py turns ranges in the effect packs into effect / skill names.
static std::map<HANDLE, std::wstring> *g_handles;
static std::map<void *, std::wstring> *g_streams;
static std::map<std::wstring, std::map<ULONGLONG, ULONGLONG>> *g_ranges;
static const size_t MAX_RANGES_PER_FILE = 50000;

static void note_read(const std::wstring &path, ULONGLONG off, ULONGLONG len) {
    EnterCriticalSection(&g_file_lock);
    auto &r = (*g_ranges)[path];
    auto it = r.find(off);
    if (it != r.end()) { if (len > it->second) it->second = len; }
    else if (r.size() < MAX_RANGES_PER_FILE) r[off] = len;
    auto f = g_files->find(path);
    if (f != g_files->end()) { f->second.reads++; f->second.read_bytes += len; }
    LeaveCriticalSection(&g_file_lock);
    InterlockedExchange(&g_files_dirty, 1);
}

static bool handle_path(HANDLE h, std::wstring &out) {
    EnterCriticalSection(&g_file_lock);
    auto it = g_handles->find(h);
    bool ok = it != g_handles->end();
    if (ok) out = it->second;
    LeaveCriticalSection(&g_file_lock);
    return ok;
}

typedef HANDLE(WINAPI *PFN_CF2)(LPCWSTR, DWORD, DWORD, DWORD, LPCREATEFILE2_EXTENDED_PARAMETERS);
typedef DWORD(WINAPI *PFN_GFA)(LPCWSTR);
typedef HANDLE(WINAPI *PFN_FFF)(LPCWSTR, LPWIN32_FIND_DATAW);
typedef BOOL(WINAPI *PFN_CDW)(LPCWSTR, LPSECURITY_ATTRIBUTES);
typedef FILE *(__cdecl *PFN_FOPEN)(const char *, const char *);
static PFN_CF2 o_CreateFile2;
static PFN_GFA o_GetFileAttributesW;
static PFN_FFF o_FindFirstFileW;
static PFN_CDW o_CreateDirectoryW;
static PFN_FOPEN o_fopen;

#include "cwo_comroute.inl"                             // v5.188 CPU route data per stage (com\st00 -> st30 / mxNNN)

static HANDLE WINAPI h_CreateFile2(LPCWSTR f, DWORD a, DWORD s, DWORD c, LPCREATEFILE2_EXTENDED_PARAMETERS p) {
    if (c == OPEN_EXISTING) {                               // v5.188 the stage's own CPU routes, when it has them
        wchar_t rp[MAX_PATH], ro[MAX_PATH];
        bool ok = false;
        __try { ok = cr_path(f, rp, MAX_PATH); } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
        if (ok) {
            HANDLE hr = sm_redirect(rp, ro, MAX_PATH, false) ? o_CreateFile2(ro, a, s, c, p) : o_CreateFile2(rp, a, s, c, p);
            if (hr != INVALID_HANDLE_VALUE) {
                logf("CPU routes: %ls -> %ls", f, rp);
                if (g_handles) { EnterCriticalSection(&g_file_lock); (*g_handles)[hr] = f; LeaveCriticalSection(&g_file_lock); }
                return hr;
            }
        }
    }
    // v3.7: while the Create a Character preview runs, the created character's file is read from the preview build
    if (g_cc_redirect && c == OPEN_EXISTING && cc_is_slot_file(f) && g_cc_file[0]) {
        HANDLE hp = o_CreateFile2(g_cc_file, a, s, c, p);
        if (hp != INVALID_HANDLE_VALUE) { logf("preview: %ls -> preview file", f); return hp; }
    }
    // v5.61 the Create an Outfit preview: the previewed character's outfit files are read from the preview build
    if (g_co_redirect && c == OPEN_EXISTING && co_is_target(f) && g_co_prev_file[0]) {
        HANDLE hp = o_CreateFile2(g_co_prev_file, a, s, c, p);
        if (hp != INVALID_HANDLE_VALUE) { logf("outfit preview: %ls -> preview file", f); return hp; }
    }
    // v5.35 the character in use (Create a Character's catalog / 'Use this character now'), built in the loader (cu_step)
    // v5.98 the NPC page (Menu cwo_npc_page.txt) owns the cell: its colour 1 is Spokesman; v5.99 the character in use is its
    // colour 10 (npc00a9.alr) there
    if (!g_cc_redirect && g_cu_ready && c == OPEN_EXISTING && (g_npc_page ? npc_is_created_file(f) : cc_is_slot_file(f)) && g_cu_file[0]) {
        HANDLE hu = o_CreateFile2(g_cu_file, a, s, c, p);
        if (hu != INVALID_HANDLE_VALUE) { logf("in use: %ls -> the character in use", f); return hu; }
    }
    if (c == OPEN_EXISTING) {                               // v5.74.1 the holiday's menu background
        wchar_t hp[MAX_PATH] = L"";
        bool ok = false;
        __try { ok = th_menu_path(f, hp); } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
        if (ok) { HANDLE hh = o_CreateFile2(hp, a, s, c, p); if (hh != INVALID_HANDLE_VALUE) return hh; }
    }
    { char st[8]; int ok_k;                                 // v5.63 an outfit 5-15 the game has no file for: built first
      if (g_store_mode && c == OPEN_EXISTING && of_parse(f, st, &ok_k)) { __try { of_ensure(f, st, ok_k); } __except (EXCEPTION_EXECUTE_HANDLER) {} } }
    if (g_store_mode && c == OPEN_EXISTING && g_bsh_ready) { __try { bsh_on_open(f); } __except (EXCEPTION_EXECUTE_HANDLER) {} }   // v5.180 body shape
    if (c == OPEN_EXISTING && g_st_theme > 0) {            // v5.191.0 a visual theme's copy of the file wins (cwo_stagetheme.inl)
        wchar_t tp[MAX_PATH];
        bool ok = false;
        __try { ok = st_theme_path(f, tp); } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
        if (ok) {
            HANDLE ht = o_CreateFile2(tp, a, s, c, p);
            if (ht != INVALID_HANDLE_VALUE) {
                note_file(f, "theme", 0);
                __try { st_note_open(f, g_st_theme); } __except (EXCEPTION_EXECUTE_HANDLER) {}   // v5.192.0 live switch
                if (g_handles) { EnterCriticalSection(&g_file_lock); (*g_handles)[ht] = f; LeaveCriticalSection(&g_file_lock); }
                return ht;
            }
        }
    }
    if (c == OPEN_EXISTING) { __try { st_note_open(f, 0); } __except (EXCEPTION_EXECUTE_HANDLER) {} }   // v5.192.0 stock look
    wchar_t ov[MAX_PATH];                                   // v5.38 Store mode: a file in <TEMP>\CwoLoader\game\ wins
    if (sm_redirect(f, ov, MAX_PATH, false)) {
        HANDLE ho = o_CreateFile2(ov, a, s, c, p);
        if (ho != INVALID_HANDLE_VALUE) {
            note_file(f, "override", 0);
            if (g_handles) { EnterCriticalSection(&g_file_lock); (*g_handles)[ho] = f; LeaveCriticalSection(&g_file_lock); }
            return ho;
        }
    }
    HANDLE h = o_CreateFile2(f, a, s, c, p);
    DWORD e = GetLastError();
    note_file(f, c == OPEN_EXISTING ? "open" : "create", h == INVALID_HANDLE_VALUE ? (e ? e : 1) : 0);
    if (h != INVALID_HANDLE_VALUE && f && g_handles) {
        EnterCriticalSection(&g_file_lock);
        (*g_handles)[h] = f;
        LeaveCriticalSection(&g_file_lock);
    }
    SetLastError(e);
    return h;
}

typedef BOOL(WINAPI *PFN_RF)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
typedef BOOL(WINAPI *PFN_RFE)(HANDLE, LPVOID, DWORD, LPOVERLAPPED, LPOVERLAPPED_COMPLETION_ROUTINE);
typedef BOOL(WINAPI *PFN_CH)(HANDLE);
typedef size_t(__cdecl *PFN_FREAD)(void *, size_t, size_t, FILE *);
typedef int(__cdecl *PFN_FCLOSE)(FILE *);
typedef long(__cdecl *PFN_FTELL)(FILE *);
static PFN_RF o_ReadFile;
static PFN_RFE o_ReadFileEx;
static PFN_CH o_CloseHandle;
static PFN_FREAD o_fread;
static PFN_FCLOSE o_fclose;
static PFN_FTELL o_ftell;

static ULONGLONG read_offset(HANDLE h, LPOVERLAPPED ov) {
    if (ov) return ((ULONGLONG)ov->OffsetHigh << 32) | ov->Offset;
    LARGE_INTEGER zero = {}, cur = {};
    SetFilePointerEx(h, zero, &cur, FILE_CURRENT);              // the loader's own call: not through the game's slot
    return (ULONGLONG)cur.QuadPart;
}
static BOOL WINAPI h_ReadFile(HANDLE h, LPVOID buf, DWORD n, LPDWORD got, LPOVERLAPPED ov) {
    std::wstring path;
    bool known = handle_path(h, path);
    ULONGLONG off = known ? read_offset(h, ov) : 0;
    BOOL r = o_ReadFile(h, buf, n, got, ov);
    DWORD e = GetLastError();
    if (known) note_read(path, off, n);
    SetLastError(e);
    return r;
}
static BOOL WINAPI h_ReadFileEx(HANDLE h, LPVOID buf, DWORD n, LPOVERLAPPED ov, LPOVERLAPPED_COMPLETION_ROUTINE cb) {
    std::wstring path;
    bool known = handle_path(h, path);
    BOOL r = o_ReadFileEx(h, buf, n, ov, cb);
    DWORD e = GetLastError();
    if (known && ov) note_read(path, ((ULONGLONG)ov->OffsetHigh << 32) | ov->Offset, n);
    SetLastError(e);
    return r;
}
static BOOL WINAPI h_CloseHandle(HANDLE h) {
    if (g_handles) {
        EnterCriticalSection(&g_file_lock);
        g_handles->erase(h);
        LeaveCriticalSection(&g_file_lock);
    }
    return o_CloseHandle(h);
}
static size_t __cdecl h_fread(void *buf, size_t size, size_t count, FILE *fp) {
    std::wstring path;
    bool known = false;
    if (g_streams) {
        EnterCriticalSection(&g_file_lock);
        auto it = g_streams->find(fp);
        if (it != g_streams->end()) { path = it->second; known = true; }
        LeaveCriticalSection(&g_file_lock);
    }
    long off = known && o_ftell ? o_ftell(fp) : 0;
    size_t r = o_fread(buf, size, count, fp);
    if (known && off >= 0) note_read(path, (ULONGLONG)off, (ULONGLONG)size * count);
    return r;
}
static int __cdecl h_fclose(FILE *fp) {
    if (g_streams) {
        EnterCriticalSection(&g_file_lock);
        g_streams->erase(fp);
        LeaveCriticalSection(&g_file_lock);
    }
    return o_fclose(fp);
}
static DWORD WINAPI h_GetFileAttributesW(LPCWSTR f) {
    wchar_t ov[MAX_PATH];
    if (sm_redirect(f, ov, MAX_PATH, false)) { note_file(f, "override?", 0); return o_GetFileAttributesW(ov); }
    DWORD r = o_GetFileAttributesW(f);
    DWORD e = GetLastError();
    note_file(f, "exists?", r == INVALID_FILE_ATTRIBUTES ? (e ? e : 1) : 0);
    SetLastError(e);
    return r;
}
static HANDLE WINAPI h_FindFirstFileW(LPCWSTR f, LPWIN32_FIND_DATAW d) {
    wchar_t ov[MAX_PATH];
    if (sm_redirect(f, ov, MAX_PATH, false)) { note_file(f, "override find", 0); return o_FindFirstFileW(ov, d); }
    HANDLE h = o_FindFirstFileW(f, d);
    DWORD e = GetLastError();
    note_file(f, "find", h == INVALID_HANDLE_VALUE ? (e ? e : 1) : 0);
    SetLastError(e);
    return h;
}
static BOOL WINAPI h_CreateDirectoryW(LPCWSTR f, LPSECURITY_ATTRIBUTES sa) {
    BOOL r = o_CreateDirectoryW(f, sa);
    DWORD e = GetLastError();
    note_file(f, "mkdir", r ? 0 : (e ? e : 1));
    SetLastError(e);
    return r;
}
static FILE *__cdecl h_fopen(const char *name, const char *mode) {
    if (g_store_mode && name && mode && !strpbrk(mode, "wa+")) {   // v5.38 Store mode override (read opens)
        wchar_t w[MAX_PATH], ov[MAX_PATH];
        char nov[MAX_PATH];
        BOOL lossy = FALSE;
        if (MultiByteToWideChar(CP_ACP, 0, name, -1, w, MAX_PATH) && sm_redirect(w, ov, MAX_PATH, false) &&
            WideCharToMultiByte(CP_ACP, 0, ov, -1, nov, MAX_PATH, NULL, &lossy) && !lossy) {
            FILE *ro = o_fopen(nov, mode);
            if (ro) {
                note_file(w, "override fopen", 0);
                if (g_streams) { EnterCriticalSection(&g_file_lock); (*g_streams)[ro] = w; LeaveCriticalSection(&g_file_lock); }
                return ro;
            }
        }
    }
    FILE *r = o_fopen(name, mode);
    DWORD e = GetLastError();
    if (name) {
        wchar_t w[MAX_PATH];
        if (MultiByteToWideChar(CP_ACP, 0, name, -1, w, MAX_PATH)) {
            note_file(w, "fopen", r ? 0 : 2);
            if (r && g_streams) {
                EnterCriticalSection(&g_file_lock);
                (*g_streams)[r] = w;
                LeaveCriticalSection(&g_file_lock);
            }
        }
    }
    SetLastError(e);
    return r;
}

// point one import-table slot of the game's exe at our wrapper (the slot's page is made writable for the write only)
static bool patch_import(const char *dll_prefix, const char *func, void *hook, void **orig) {
    BYTE *base = (BYTE *)g_base;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir.VirtualAddress); d->Name; d++) {
        if (_strnicmp((const char *)base + d->Name, dll_prefix, strlen(dll_prefix)) || !d->OriginalFirstThunk) continue;
        IMAGE_THUNK_DATA *names = (IMAGE_THUNK_DATA *)(base + d->OriginalFirstThunk);
        IMAGE_THUNK_DATA *iat = (IMAGE_THUNK_DATA *)(base + d->FirstThunk);
        for (int i = 0; names[i].u1.AddressOfData; i++) {
            if (IMAGE_SNAP_BY_ORDINAL(names[i].u1.Ordinal)) continue;
            IMAGE_IMPORT_BY_NAME *ibn = (IMAGE_IMPORT_BY_NAME *)(base + names[i].u1.AddressOfData);
            if (strcmp((const char *)ibn->Name, func)) continue;
            ULONGLONG *slot = &iat[i].u1.Function;
            if (!hook) { *orig = (void *)*slot; return true; }   // read only: the real function, slot unchanged
            DWORD old;
            if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old)) return false;
            *orig = (void *)*slot;
            *slot = (ULONGLONG)hook;
            VirtualProtect(slot, sizeof(*slot), old, &old);
            return true;
        }
    }
    return false;
}

static void file_monitor_install() {
    g_files = new std::map<std::wstring, FileReq>();
    g_handles = new std::map<HANDLE, std::wstring>();
    g_streams = new std::map<void *, std::wstring>();
    g_ranges = new std::map<std::wstring, std::map<ULONGLONG, ULONGLONG>>();
    patch_import("api-ms-win-crt-stdio-", "ftell", NULL, (void **)&o_ftell);   // read only: the game's own ftell
    g_hooked = patch_import("api-ms-win-core-file-", "ReadFile", (void *)h_ReadFile, (void **)&o_ReadFile) +
               patch_import("api-ms-win-core-file-", "ReadFileEx", (void *)h_ReadFileEx, (void **)&o_ReadFileEx) +
               patch_import("api-ms-win-core-handle-", "CloseHandle", (void *)h_CloseHandle, (void **)&o_CloseHandle) +
               patch_import("api-ms-win-crt-stdio-", "fread", (void *)h_fread, (void **)&o_fread) +
               patch_import("api-ms-win-crt-stdio-", "fclose", (void *)h_fclose, (void **)&o_fclose) +
               patch_import("api-ms-win-core-file-", "CreateFile2", (void *)h_CreateFile2, (void **)&o_CreateFile2) +
               patch_import("api-ms-win-core-file-", "GetFileAttributesW", (void *)h_GetFileAttributesW, (void **)&o_GetFileAttributesW) +
               patch_import("api-ms-win-core-file-", "FindFirstFileW", (void *)h_FindFirstFileW, (void **)&o_FindFirstFileW) +
               patch_import("api-ms-win-core-file-", "CreateDirectoryW", (void *)h_CreateDirectoryW, (void **)&o_CreateDirectoryW) +
               patch_import("api-ms-win-crt-stdio-", "fopen", (void *)h_fopen, (void **)&o_fopen);
}

// write the unique paths (worker thread, every few seconds while something changed)
static void file_requests_flush() {
    if (!g_files || !InterlockedExchange(&g_files_dirty, 0)) return;
    wchar_t path[MAX_PATH], tmp[MAX_PATH];
    swprintf_s(path, MAX_PATH, L"%sfile_requests.csv", g_dir);
    swprintf_s(tmp, MAX_PATH, L"%sfile_requests.tmp", g_dir);
    // copy under the lock, write after: the game's file calls never wait for the disk write
    EnterCriticalSection(&g_file_lock);
    std::map<std::wstring, FileReq> files = *g_files;
    std::map<std::wstring, std::map<ULONGLONG, ULONGLONG>> ranges;
    if (g_ranges) ranges = *g_ranges;
    LeaveCriticalSection(&g_file_lock);
    FILE *f = NULL;
    if (cwo_wfopen(&f, tmp, L"w, ccs=UTF-8") != 0 || !f) return;
    fwprintf(f, L"path,how,result,count,first_seconds,first_stage,callers,reads,read_bytes\n");
    for (const auto &kv : files) {
        const FileReq &e = kv.second;
        wchar_t res[24];
        if (e.result == 0) wcscpy_s(res, L"found");
        else swprintf_s(res, L"missing(%lu)", e.result);
        fwprintf(f, L"\"%s\",%S,%s,%u,%.1f,%d,%S,%u,%llu\n", kv.first.c_str(), e.api, res, e.count, e.first_ms / 1000.0,
                 e.first_stage, e.callers, e.reads, e.read_bytes);
    }
    fclose(f);
    MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING);
    // read ranges, touching / overlapping ones merged
    swprintf_s(path, MAX_PATH, L"%sread_ranges.csv", g_dir);
    swprintf_s(tmp, MAX_PATH, L"%sread_ranges.tmp", g_dir);
    if (cwo_wfopen(&f, tmp, L"w, ccs=UTF-8") != 0 || !f) return;
    fwprintf(f, L"path,offset,size\n");
    for (const auto &kv : ranges) {
        ULONGLONG s0 = 0, e0 = 0;
        bool open = false;
        for (const auto &r : kv.second) {
            if (open && r.first <= e0) { if (r.first + r.second > e0) e0 = r.first + r.second; continue; }
            if (open) fwprintf(f, L"\"%s\",%llu,%llu\n", kv.first.c_str(), s0, e0 - s0);
            s0 = r.first; e0 = r.first + r.second; open = true;
        }
        if (open) fwprintf(f, L"\"%s\",%llu,%llu\n", kv.first.c_str(), s0, e0 - s0);
    }
    fclose(f);
    MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING);
}

// offline test only (test_host.exe): switch the TEST program's own GetFileAttributesW import slot to the wrapper,
// exactly as file_monitor_install does for the game's slots, and ask what was recorded for a path
extern "C" __declspec(dllexport) int CwoTestHookImports() {
    if (!g_files) g_files = new std::map<std::wstring, FileReq>();
    return patch_import("KERNEL32", "GetFileAttributesW", (void *)h_GetFileAttributesW, (void **)&o_GetFileAttributesW) ? 1 : 0;
}
extern "C" __declspec(dllexport) int CwoTestSeen(const wchar_t *path) {
    if (!g_files) return -1;
    EnterCriticalSection(&g_file_lock);
    auto it = g_files->find(path);
    int r = it == g_files->end() ? -1 : (int)it->second.result;
    LeaveCriticalSection(&g_file_lock);
    return r;                                                   // -1 not seen, 0 found, else the error code
}

extern "C" __declspec(dllexport) int CwoLoaderVersion() { return 1; }

// offline self-test of the marker file code (stage 99, never a real stage): 3 adds, 1 delete -> 2 markers left
extern "C" __declspec(dllexport) int CwoSelfTest() {
#ifdef CWO_EXPIRES
    if (!ex_selftest()) return -90;                          // v5.37 the end-date decision
#endif
    float p[3] = {1.5f, 200.0f, -3.25f};
    wchar_t path[MAX_PATH];
    marker_path(99, path);
    DeleteFileW(path);
    add_marker("SPAWN", 99, p); p[0] += 10; add_marker("SPAWN", 99, p); p[0] += 10; add_marker("KILLZONE", 99, p);
    if (!delete_last(99)) return -1;
    FILE *f = NULL;
    if (cwo_wfopen(&f, path, L"r") != 0 || !f) return -2;
    int lines = 0; char line[256];
    while (fgets(line, sizeof(line), f)) lines++;
    fclose(f);
    DeleteFileW(path);
    if (lines != 3) return -3;                    // header + 2 markers
    // v0.2: the Windows.Gaming.Input path must activate and answer without crashing (strength 0 = no vibration)
    pads_init();
    unsigned n = 0;
    HRESULT hr = pad_vibrate(0.0, &n);
    logf("self-test: Windows.Gaming.Input result 0x%08lx, %u gamepad(s) listed", (unsigned long)hr, n);
    if (!g_pads) return -4;
    // v0.4: the input filter on a stand-in pad whose reading reports every button down (0x3FFF)
    struct Fake {
        static HRESULT STDMETHODCALLTYPE read(void *, ABI::Windows::Gaming::Input::GamepadReading *r) {
            memset(r, 0, sizeof(*r));
            r->Buttons = (ABI::Windows::Gaming::Input::GamepadButtons)0x3FFF;
            return S_OK;
        }
    };
    static void *fake_vtbl[16];
    fake_vtbl[8] = (void *)Fake::read;
    void *fake_obj[1] = {fake_vtbl};
    if (!attach_filter((void ***)fake_obj) || fake_obj[0] != (void *)g_vtbl_copy) return -5;
    typedef HRESULT(STDMETHODCALLTYPE * PFN)(void *, ABI::Windows::Gaming::Input::GamepadReading *);
    PFN call = (PFN)((void **)fake_obj[0])[8];
    ABI::Windows::Gaming::Input::GamepadReading r;
    LONG s_match = g_in_match, s_pin = g_pinned;
    UINT32 got[3];
    g_in_match = 0; g_pinned = 0; call(fake_obj, &r); got[0] = (UINT32)r.Buttons;     // menus: nothing hidden
    g_in_match = 1; call(fake_obj, &r); got[1] = (UINT32)r.Buttons;                     // match, panel off: View tap
    g_pinned = 1; call(fake_obj, &r); got[2] = (UINT32)r.Buttons;                       // panel on: creator buttons
    LONG s_gv = g_game_view;                                                            // v5.10.1 the mulligan window:
    g_pinned = 0; g_game_view = 1; call(fake_obj, &r); UINT32 got_mull = (UINT32)r.Buttons;   // a View tap is the game's
    g_game_view = s_gv;
    // v0.4.1: the loader's own reading must bypass the filter (panel on, match: every button still seen)
    IGamepad *s_pad = g_pad0;
    g_pad0 = (IGamepad *)fake_obj;
    WORD raw = 0;
    bool raw_ok = read_raw_buttons(&raw);
    g_pad0 = s_pad;
    g_in_match = s_match; g_pinned = s_pin;
    logf("self-test: input filter 0x%04x / 0x%04x / 0x%04x (expect 0x3fff / 0x33fd / 0x1000, v3.5: LB with View is the CPU switch, v5.9: RB with View the free camera); loader's own read 0x%04x (expect 0xf3ff, v2.0 reads L3 too)",
         got[0], got[1], got[2], raw);
    logf("self-test: mulligan window 0x%04x (expect 0x33ff: View reaches the game, LB / RB with View still the loader's)", got_mull);
    if (got[0] != 0x3FFF || got[1] != 0x33FD || got[2] != 0x1000 || got_mull != 0x33FF) return -6;
    if (!raw_ok || raw != 0xF3FF) return -7;
    // v0.5: Map Slot 7 switch on a stand-in table: choose -> pack names, reset -> the original names
    static char table[24 * 70];
    static const char *own_a = "st10.alr", *own_d = "st10.dat";
    memset(table, 0, sizeof(table));
    *(const char **)(table + 24 * 20) = own_a;
    *(const char **)(table + 24 * 21) = own_d;
    g_table_test = table;
    g_mx_count = 1;
    strcpy_s(g_mx[0].file, "mx001");
    strcpy_s(g_mx[0].name, "Test Stage");
    *(int *)(table + 24 * 20 + 8) = 1; *(int *)(table + 24 * 21 + 8) = 1;     // v5.155 looked-up flags of st10
    mx_apply(0);
    bool chose = !strcmp(*(const char **)(table + 24 * 20), "mx001.alr") && !strcmp(*(const char **)(table + 24 * 21), "mx001.dat");
    bool fresh = *(int *)(table + 24 * 20 + 8) == 0 && *(int *)(table + 24 * 21 + 8) == 0;   // new pack: looked up again
    *(int *)(table + 24 * 20 + 8) = 1; *(int *)(table + 24 * 21 + 8) = 1;
    mx_apply(0);
    bool kept8 = *(int *)(table + 24 * 20 + 8) == 1 && *(int *)(table + 24 * 21 + 8) == 1;   // same pack: kept
    mx_apply(-1);
    bool reset = *(const char **)(table + 24 * 20) == own_a && *(const char **)(table + 24 * 21) == own_d &&
                 *(int *)(table + 24 * 20 + 8) == 0;
    g_table_test = NULL; g_mx_count = 0; g_slot_saved = false; g_mx_cur = -1;
    logf("self-test: stage switch choose %s, reset %s, v5.155 looked-up flags cleared on a new pack %s / kept on the same %s",
         chose ? "ok" : "FAILED", reset ? "ok" : "FAILED", fresh ? "ok" : "FAILED", kept8 ? "ok" : "FAILED");
    if (!chose || !reset || !fresh || !kept8) return -8;
    // v0.6 fly: stand-in actor facing +z (0), stick full, right trigger full: after ~50 ms it must move +z and up only
    static unsigned char actor[0x18000];                // v2.1: reaches +0x17174 (captured by)
    memset(actor, 0, sizeof(actor));
    *(int *)(actor + ACTOR_ACTIVE) = 1;
    float *fp = (float *)(actor + ACTOR_POS);
    fp[0] = 0.0f; fp[1] = 200.0f; fp[2] = 0.0f;
    g_actor_test = (ULONG_PTR)actor;
    LONG s_pin2 = g_pinned, s_match2 = g_in_match, s_page = g_page;
    g_pinned = 1; g_in_match = 1; g_page = 2; g_stick_mag = 1.0f; g_trig_up = 1.0f; g_trig_down = 0.0f;
    fly_step();
    Sleep(50);
    fly_step();
    float fx = fp[0], fy = fp[1], fz = fp[2], px = ((float *)(actor + ACTOR_PREV_POS))[2];
    // v1.0: live facing a quarter turn (8192 = pi/2 x 5215.19) moves along +x; hiding the panel keeps flying;
    // D-pad Down's landed flag stops it
    *(int *)(actor + ACTOR_FACING) = 8192;
    float x0 = fp[0];
    g_pinned = 0;
    Sleep(30);
    fly_step();
    bool kept = g_flying && fp[0] > x0 + 0.5f;
    g_fly_landed = true;
    fly_step();
    bool landed = !g_flying;
    g_fly_landed = false;
    logf("self-test: fly along +0x12E68 quarter turn x %.2f -> %.2f, kept with panel hidden %s, D-pad Down lands %s",
         x0, fp[0], kept ? "ok" : "FAILED", landed ? "ok" : "FAILED");
    if (!kept || !landed) return -13;
    {   // v1.1 hide-HUD: a stand-in slot holding a stand-in draw; the wrapper must hide +0x54 only during the call
        static int seen_ready;
        struct T { static void __fastcall draw(void *o) { seen_ready = *(int *)((char *)o + HUD_READY); } };
        static void *fake_slot[1];
        fake_slot[0] = (void *)&T::draw;
        bool refused = !hud_install((ULONG_PTR)fake_slot, (ULONG_PTR)&T::draw + 1);   // wrong expectation: refuse
        bool ok_inst = hud_install((ULONG_PTR)fake_slot, (ULONG_PTR)&T::draw);
        static unsigned char hudobj[0x300];
        *(int *)(hudobj + HUD_READY) = 1;
        LONG s_match3 = g_in_match;
        g_in_match = 1;
        g_hide_hud = false;
        ((PFN_HUD_DRAW)fake_slot[0])(hudobj);
        int shown = seen_ready;
        g_hide_hud = true;
        ((PFN_HUD_DRAW)fake_slot[0])(hudobj);
        int hidden = seen_ready, after = *(int *)(hudobj + HUD_READY);
        g_hide_hud = false;
        g_in_match = s_match3;
        o_hud_draw = NULL;
        logf("self-test: hide-HUD refuse-unexpected %s, installed %s, shown sees ready %d, hidden sees %d, restored %d",
             refused ? "ok" : "FAILED", ok_inst ? "ok" : "FAILED", shown, hidden, after);
        if (!refused || !ok_inst || shown != 1 || hidden != 0 || after != 1) return -14;
    }
    {   // v1.2 aura FX on a stand-in game: the first start is refused (span loading), the second starts and loops; it
        // follows the player (position row, facing + 0x4000), stops when the aura hides, "always" ignores the aura,
        // and leaving the match forgets the handle without calling the game
        static unsigned char fxmgr[0x60 + 256 * 0xF0], skills[753 * 0x90], aura[0x200];
        static int starts, loops, stops, moves, last_track, last_owner, last_req;
        static unsigned last_h, turned;
        static float moved_pos[3];
        struct S {
            static unsigned __fastcall start(ULONG_PTR m, int owner, int req, int track, const float *, int) {
                last_owner = owner; last_req = req; last_track = track;
                if (++starts == 1) return FX_NONE;
                unsigned h = (unsigned)(starts << 16) | (unsigned)(track << 12) | 5u;
                *(unsigned *)(m + 0x60 + 5 * 0xF0) = h;
                return h;
            }
            static void __fastcall loop(unsigned h) { loops++; last_h = h; }
            static void __fastcall stop(unsigned h) { stops++; if (h == last_h) *(unsigned *)(fxmgr + 0x60 + 5 * 0xF0) = 0; }
            static void __fastcall move(unsigned, const float *m) { moves++; memcpy(moved_pos, m + 12, 12); }
            static short __fastcall effect_of(int prof, int skill) { return (short)(prof == 7 && skill == 10 ? 10 : 0); }
            static void __fastcall turn(float *, unsigned a) { turned = a; }
        };
        static const float ident[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        *(short *)(skills + 0x14 + 10 * 0x90) = 7;
        *(int *)(actor + ACTOR_ACTIVE) = 1;
        *(int *)(actor + 0x14) = 0;
        *(int *)(actor + ACTOR_FACING) = 1000;
        fp[0] = 11.0f; fp[1] = 22.0f; fp[2] = 33.0f;
        *(unsigned *)(aura + 0xC0) = 0x30;
        FxGame fake = {(ULONG_PTR)fxmgr, (ULONG_PTR)skills, (ULONG_PTR)aura, (ULONG_PTR)actor, ident,
                       &S::start, &S::loop, &S::stop, &S::move, &S::effect_of, &S::turn};
        g_fx_test = &fake;
        LONG s_match4 = g_in_match;
        g_in_match = 1; g_fx_on = true; g_fx_always = false; g_fx_track = 0; g_fx_skill = 10; g_fx_change = 1;
        fx_step();                                               // refused: still loading
        bool waited = g_fx_handle == FX_NONE && starts == 1 && last_req == 10;
        fx_step();                                               // starts and loops
        bool started = g_fx_handle != FX_NONE && loops == 1 && last_h == g_fx_handle && last_owner == 0 && last_track == 0;
        fx_step();                                               // follows
        bool follows = moves == 1 && moved_pos[0] == 11.0f && moved_pos[2] == 33.0f && turned == 1000 + 0x4000;
        *(unsigned *)(aura + 0xC0) = 0x20;                       // aura hidden
        fx_step();
        bool hid = stops == 1 && g_fx_handle == FX_NONE;
        g_fx_always = true;
        fx_step();
        bool always = g_fx_handle != FX_NONE && starts == 3;
        g_in_match = 0;
        fx_step();
        bool left = g_fx_handle == FX_NONE && stops == 1;
        g_fx_test = NULL; g_fx_on = false; g_fx_always = false; g_fx_skill = -1; g_fx_req = 0; g_in_match = s_match4;
        logf("self-test: aura FX waits for loading %s, starts+loops %s, follows %s, stops when aura hides %s, always %s, "
             "match end forgets %s", waited ? "ok" : "FAILED", started ? "ok" : "FAILED", follows ? "ok" : "FAILED",
             hid ? "ok" : "FAILED", always ? "ok" : "FAILED", left ? "ok" : "FAILED");
        if (!waited || !started || !follows || !hid || !always || !left) return -15;
    }
    {   // v2.0 Forge mode on stand-ins (native slot, session, editing, saved maps)
        int fr = forge_self_test(actor);
        if (fr != 1) return fr;
    }
    if (!ns_self_test()) return -26;                             // v2.6 new skills on a stand-in skill image
    { int er = ed_self_test(); if (er != 1) return er; }         // v3.4 Edgar's Dream creators, mode, lives on stand-ins
    { int er = ed_quest_self_test(); if (er != 1) return er; }   // v5.13 Create a Quest v2
    { int er = ed_reader_self_test(); if (er != 1) return er; }  // v4.4 the creator file reader
    { int er = bigslot_self_test(); if (er != 1) return er; }    // v5.4 bigger fighter model slots
    { int er = sixf_self_test(); if (er != 1) return er; }       // v5.0 the six-fighter table move
    { int er = aura_self_test(); if (er != 1) return er; }       // v5.31 the aura table move
    { int er = sixf2_self_test(); if (er != 1) return er; }      // v5.1 six fighters step 2
    { int er = sixhud_self_test(); if (er != 1) return er; }     // v5.7 fighters 5 / 6 on the game's own HUD
    { int er = lob_self_test(); if (er != 1) return er; }        // v5.2 the 3 VS 3 LOBBY's fighters
    { int er = lob_rate_self_test(); if (er != 1) return er; }   // v5.2.1 its ratings
    { int er = lob3_self_test(); if (er != 1) return er; }       // v5.3 the 3 VS 3 LOBBY in the game's lobby
    { int er = watch_self_test(); if (er != 1) return er; }      // v5.3.3 the crash watch
    { int er = cc_self_test(); if (er != 1) return er; }         // v3.7 Create a Character preview mapping
    { int er = sfx_self_test(); if (er != 1) return er; }        // v3.9 school effect placement
    { int er = fc_self_test(); if (er != 1) return er; }         // v5.9 free camera
    { int er = ars_self_test(); if (er != 1) return er; }        // v5.9 arsenal lists, the Xbox Live rule list
    { int er = dbtl_self_test(); if (er != 1) return er; }       // v5.11 Download Battle choices
    { int er = qplayer_self_test(); if (er != 1) return er; }    // v5.14.1 Create a Quest: play as
    { int er = bigstage_self_test(); if (er != 1) return er; }   // v5.15 / v5.39 the big-stage stubs
    { int er = portrait_self_test(); if (er != 1) return er; }   // v5.40 stage portraits
    { int er = ml_self_test(); if (er != 1) return er; }         // v5.80 the Mod List lines fit
    { int er = fb_self_test(); if (er != 1) return er; }         // v5.154 FEEDBACK
    { int er = fu_self_test(); if (er != 1) return er; }         // v5.17 the Skill Fuser
    { int er = gt_self_test(); if (er != 1) return er; }         // v5.18 the split-screen game types
    { int er = on_self_test(); if (er != 1) return er; }         // v5.19 the outfit names
    { int er = ph_self_test(); if (er != 1) return er; }         // v5.20 photo mode
    { int er = rc_self_test(); if (er != 1) return er; }         // v5.21 Records
    { int er = sv_self_test(); if (er != 1) return er; }         // v5.22 Survival
    { int er = mu_self_test(); if (er != 1) return er; }         // v5.23 Stage Music
    { int er = ar_self_test(); if (er != 1) return er; }         // v5.24 Shrinking arena / King of the Hill
    { int er = dr_self_test(); if (er != 1) return er; }         // v5.25 Draft Arsenal
    { int er = br_self_test(); if (er != 1) return er; }         // v5.26 Boss Raid
    { int er = edm_self_test(); if (er != 1) return er; }        // v5.27 the Edgar's Dream menu
    { int er = ars_made_self_test(); if (er != 1) return er; }   // v5.152 skills you made stay offline
    { int er = lv_self_test(); if (er != 1) return er; }         // v5.28 the stage behind the level list
    { int er = sl_self_test(); if (er != 1) return er; }         // v5.43 the stage-list rows
    { int er = bs_self_test(); if (er != 1) return er; }         // v5.45 the bigger skill table + the skill lists
    { int er = bfr_self_test(); if (er != 1) return er; }        // v5.56 the Battle for Reality skill pack
    { int er = bfr_free_id_self_test(); if (er != 1) return er; }   // v5.109 new skill numbers off the pack
    { int er = bs_removed_self_test(); if (er != 1) return er; }   // v5.110 removed skill numbers
    { int er = stl_self_test(); if (er != 1) return er; }        // v5.46 the stock list patch
    { int er = bi_self_test(); if (er != 1) return er; }         // v5.47 the big-id patch table
    { int er = ba_self_test(); if (er != 1) return er; }         // v5.48 the arena patch table
    { int er = hb_self_test(); if (er != 1) return er; }         // v5.49 the hit-radius stub
    { int er = arg_self_test(); if (er != 1) return er; }        // v5.28 arsenal guard
    { int er = fx_self_test(); if (er != 1) return er; }         // v5.32 skill colours: recolour, index numbers
    { int er = ru_self_test(); if (er != 1) return er; }         // v5.53 multiplayer rules
    { int er = nm_self_test(); if (er != 1) return er; }         // v5.54 next map
    { int er = bn_self_test(); if (er != 1) return er; }         // v5.57 BFR lobby hooks + vote count
    { int er = pf_self_test(); if (er != 1) return er; }         // v5.58 game files from patches + SHA-256
    { int er = dp_block_self_test(); if (er != 1) return er; }   // v5.150.0 CREATE: the game's bindings held back
    { int er = shield_self_test(); if (er != 1) return er; }     // v5.163.0 crash shield (freed texture)
    { int er = slim_self_test(); if (er != 1) return er; }       // v5.164.0 school limit + Illusion
    { int er = lf_revert_self_test(); if (er != 1) return er; }  // v5.167.5 a small Lab manager: the Lab sites back
    { int er = sl_fit_self_test(); if (er != 1) return er; }     // v5.171.2 stage list rows by whole words
    { int er = stl_lock_self_test(); if (er != 1) return er; }   // v5.60 the locked boss skills
    { int er = s6_self_test(); if (er != 1) return er; }         // v5.159 school 6 marks
    { int er = co_self_test(); if (er != 1) return er; }         // v5.61 Create an Outfit
    { int er = of_self_test(); if (er != 1) return er; }         // v5.63 15 outfits
    { int er = bsh_self_test(); if (er != 1) return er; }        // v5.180 body shape
    { int er = ad_self_test(); if (er != 1) return er; }         // v5.64 Arsenal Data per stage
    { int er = hud_self_test(); if (er != 1) return er; }        // v5.65 HUD presets
    // { int er = wm_self_test(); if (er != 1) return er; }   // v5.87 wide selects (off)
    { int er = sg_self_test(); if (er != 1) return er; }         // v5.87 the select grid
    { int er = hf_self_test(); if (er != 1) return er; }         // v5.67 HUD faces
    { int er = th_self_test(); if (er != 1) return er; }         // v5.74 holiday themes
    { int er = fp_self_test(); if (er != 1) return er; }         // v5.138 60 FPS detour
    { int er = epg_self_test(); if (er != 1) return er; }        // v5.29 the Edgar's Dream pages in the game's menu
    { int er = cs_self_test(); if (er != 1) return er; }         // v5.32 CREATE A STAGE
    { int er = mr_self_test(); if (er != 1) return er; }         // v5.34 the Mirror
    { int er = ho_self_test(); if (er != 1) return er; }         // v5.131 the Hologram
    { int er = ks_self_test(); if (er != 1) return er; }         // v5.129 support skills
    { int er = lt_self_test(); if (er != 1) return er; }         // v5.29 the Xbox Live lobby's game type
    { int er = cpu_level_self_test(); if (er != 1) return er; }   // v5.12 CPU Level on every offline CPU, CPU life
    { int er = ls_self_test(); if (er != 1) return er; }         // v5.11 the Xbox Live session's win / loss
    { int er = slot_self_test(); if (er != 1) return er; }       // v4.1 the select screen's character cell
    {   // v3.0.1: a text longer than its buffer no longer ends the process
        char small4[4];
        int rr = sprintf_s(small4, "%s", "longer than four");
        logf("self-test: too-long text %s (result %d)", rr == -1 && small4[0] == 0 ? "ok, survived" : "FAILED", rr);
        if (rr != -1 || small4[0]) return -28;
    }
    g_pinned = 1;
    g_page = 0;
    fly_step();                                                  // leaves fly mode
    g_pinned = s_pin2; g_in_match = s_match2; g_page = s_page; g_stick_mag = g_trig_up = 0.0f; g_actor_test = 0;
    logf("self-test: fly moved to (%.2f, %.2f, %.2f), prev z %.2f (expect x 0, y > 200, z > 0, prev = pos)", fx, fy, fz, px);
    if (fabsf(fx) > 0.01f || fy <= 200.5f || fz <= 1.0f || fz > 20.0f || px != fz || g_flying) return -9;
    // v0.7: a CwoSkills.txt line reads back exactly (same format string as sk_load_list)
    SkillRow t = {};
    int got_n = sscanf_s("3\tPsycho Knife\tPsycho\t2\n", "%d\t%31[^\t]\t%9[^\t]\t%3[^\t\r\n]", &t.id, t.name,
                         (unsigned)sizeof(t.name), t.school, (unsigned)sizeof(t.school), t.cost, (unsigned)sizeof(t.cost));
    logf("self-test: skill line -> %d fields: %d / %s / %s / %s", got_n, t.id, t.name, t.school, t.cost);
    if (got_n != 4 || t.id != 3 || strcmp(t.name, "Psycho Knife") || strcmp(t.school, "Psycho") || strcmp(t.cost, "2")) return -10;
    // v0.8 file monitor: the attributes wrapper records an existing file as found and a missing one as missing(2),
    // returns the real result, and the list is written to file_requests.csv
    if (!g_files) g_files = new std::map<std::wstring, FileReq>();
    o_GetFileAttributesW = GetFileAttributesW;
    wchar_t self[MAX_PATH], missing[MAX_PATH];
    GetModuleFileNameW(NULL, self, MAX_PATH);
    swprintf_s(missing, MAX_PATH, L"%sno_such_folder\\no_such_file.dat", g_dir);
    DWORD ra = h_GetFileAttributesW(self), rb = h_GetFileAttributesW(missing);
    DWORD eb = GetLastError();
    bool ok_found = (*g_files)[self].result == 0 && ra != INVALID_FILE_ATTRIBUTES;
    bool ok_missing = (*g_files)[missing].result != 0 && rb == INVALID_FILE_ATTRIBUTES && eb != 0;
    file_requests_flush();
    wchar_t csv[MAX_PATH];
    swprintf_s(csv, MAX_PATH, L"%sfile_requests.csv", g_dir);
    bool ok_csv = GetFileAttributesW(csv) != INVALID_FILE_ATTRIBUTES;
    logf("self-test: file monitor found %s, missing %s (last error %lu kept), csv %s", ok_found ? "ok" : "FAILED",
         ok_missing ? "ok" : "FAILED", eb, ok_csv ? "ok" : "FAILED");
    if (!ok_found || !ok_missing || !ok_csv) return -11;
    // v0.9: reads through the wrappers are recorded with their offsets (16 bytes at 0, 8 at 100), the handle is
    // forgotten on close, and the first request of a path carries a caller chain inside the host exe
    if (!g_handles) g_handles = new std::map<HANDLE, std::wstring>();
    if (!g_streams) g_streams = new std::map<void *, std::wstring>();
    if (!g_ranges) g_ranges = new std::map<std::wstring, std::map<ULONGLONG, ULONGLONG>>();
    o_CreateFile2 = CreateFile2; o_ReadFile = ReadFile; o_CloseHandle = CloseHandle;
    HANDLE fh = h_CreateFile2(self, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, NULL);
    char rbuf[16];
    DWORD nread = 0;
    h_ReadFile(fh, rbuf, 16, &nread, NULL);
    LARGE_INTEGER to;
    to.QuadPart = 100;
    SetFilePointerEx(fh, to, NULL, FILE_BEGIN);
    h_ReadFile(fh, rbuf, 8, &nread, NULL);
    h_CloseHandle(fh);
    bool ok_ranges, ok_closed, ok_callers;
    unsigned reads;
    EnterCriticalSection(&g_file_lock);
    auto &rr = (*g_ranges)[self];
    ok_ranges = rr.size() == 2 && rr.count(0) && rr[0] == 16 && rr.count(100) && rr[100] == 8;
    ok_closed = g_handles->find(fh) == g_handles->end();
    reads = (*g_files)[self].reads;
    ok_callers = (*g_files)[self].callers[0] == '0';
    LeaveCriticalSection(&g_file_lock);
    logf("self-test: reads %s (%u reads), handle forgotten on close %s, caller chain %s (%s)", ok_ranges ? "ok" : "FAILED",
         reads, ok_closed ? "ok" : "FAILED", ok_callers ? "ok" : "FAILED", (*g_files)[self].callers);
    if (!ok_ranges || !ok_closed || !ok_callers || reads != 2) return -12;
    return 1;
}

// v3.0.1 (owner crash 16:12, CwoLoader.dll c0000409): a too-long text for a sprintf_s buffer must never end the game.
// This DLL has its own static C runtime (/MT), so this handler covers only the loader's own calls: the text is
// left empty and logged once instead of the runtime's fast-fail.
static volatile LONG g_crt_said;
static void __cdecl cwo_invalid_parameter(const wchar_t *, const wchar_t *, const wchar_t *, unsigned, uintptr_t) {
    if (!InterlockedExchange(&g_crt_said, 1)) logf("loader: a text did not fit its buffer (left empty, game kept running)");
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
#ifdef CWO_PROBE
    return TRUE;                                               // Store-mode probe build: the DLL loads and does nothing
#endif
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        _set_invalid_parameter_handler(cwo_invalid_parameter);
        InitializeCriticalSection(&g_log_lock);
        InitializeCriticalSection(&g_ui_lock);
        InitializeCriticalSection(&g_file_lock);
        g_base = (ULONG_PTR)GetModuleHandleW(NULL);
        g_start_ms = GetTickCount64();
        DWORD n = GetTempPathW(MAX_PATH, g_dir);
        if (n == 0 || n + 12 >= MAX_PATH) g_dir[0] = 0;
        wcscat_s(g_dir, MAX_PATH, L"CwoLoader\\");
        {                                                      // v5.57.1 the edition (CwoLaunch --edition NAME writes edition.txt next to the DLL):
            wchar_t mod[MAX_PATH], ed[MAX_PATH];               // "BFR" = the BFR mod pack, its own data folder CwoLoaderBFR
            DWORD ml = GetModuleFileNameW(h, mod, MAX_PATH);
            wchar_t *sl = ml ? wcsrchr(mod, L'\\') : NULL;
            if (sl) {
                sl[1] = 0;
                swprintf_s(ed, MAX_PATH, L"%sedition.txt", mod);
                FILE *ef = NULL;
                if (_wfopen_s(&ef, ed, L"r") == 0 && ef) {
                    char e[16] = "";
                    if (fgets(e, sizeof(e), ef) && !_strnicmp(e, "BFR", 3)) {
                        size_t l = wcslen(g_dir);
                        if (l > 1 && l + 4 < MAX_PATH) { g_dir[l - 1] = 0; wcscat_s(g_dir, MAX_PATH, L"BFR\\"); g_bfr_edition = true; }
                    }
                    fclose(ef);
                }
            }
        }
        CreateDirectoryW(g_dir, NULL);
        sm_init(h);                                            // v5.38 Store mode? (the running exe has no .cwo section)
#ifdef CWO_EXPIRES
        if (ex_check()) {                                      // v5.37 past the partner build's date: only native 2.8
            InterlockedExchange(&g_expired, 1);
            if (host_ok()) g_forge_native_ok = forge_native_install(g_base + RVA_NATIVES_G2 + 8 * FORGE_NATIVE);
            HANDLE t = CreateThread(NULL, 0, worker, NULL, 0, NULL);
            if (t) CloseHandle(t);
            return TRUE;
        }
#endif
        if (host_ok() && g_store_mode) g_mp_state = mp_apply();   // v5.38 the exe changes, written to memory first
        if (host_ok()) { cl_install(); bi_apply(); stl_apply(); ba_apply(); hb_install(); if (g_store_mode) sb_load(); bn_install(); fp_install();
        }  // v5.47.1 crash log; v5.47 ids; v5.46 stock list; v5.48 arena; v5.49 hit radius; v5.50.1 sound bank
        if (host_ok() && (!g_store_mode || g_mp_state == 1)) {
            file_monitor_install();                            // before any game code runs: every request is seen
            g_forge_native_ok = forge_native_install(g_base + RVA_NATIVES_G2 + 8 * FORGE_NATIVE);   // v2.0 FORGE MODE
            dbtl_install();                                    // v5.11 Download Battle choices (native 4.661)
            slim_install();                                    // v5.164 school limit counts the Illusion school (native 5.31)
            qplayer_install();                                 // v5.14.1 Create a Quest: play as (one read of FUN_14024ea70)
            bigslot_install();                                 // v5.4 bigger fighter model slots (unless turned off)
            bigstage_install();                                // v5.15 / v5.39 every stage model over 12 MB: its own buffer
            splitobj_install();                                // v5.119 pack stages keep crumble props in split screen
            sndguard_install();                                // v5.186.2 no crash on a fighter without a sound bank
            fu_install();                                      // v5.17 Skill Fuser: the skill move start calls the loader
            mu_install();                                      // v5.23 Stage Music: natives 1.16 / 1.18
            arg_install();                                     // v5.28 arsenal guard: the arsenal check calls the loader first
            dk_install();                                      // v5.112 downed fighters keep their own aura
            sixf_install();                                    // v5.0 six fighters step 1: the tables move (when turned on)
            sixf2_install();                                   // v5.1 step 2: room for 6 fighters (after step 1)
            create_lite_install();                             // v5.90.3 without step 2: the match-start call alone
            rate_install();                                    // v5.102 the BFR rules leave the rating alone
            ofx_aura_install();                                // v5.93 native 2.46 (the aura colour) -> the loader
            npc_page_install();                                // v5.99 native 2.150: the NPC cell online without the created character
            aura_install();                                    // v5.31 the aura draw's per-fighter table (split-screen 3 vs 3 crash)
            fx_install();                                      // v5.32 skill colours: the effect load calls the loader first
            if (g_sixf2_state == 1) watch_install();           // v5.3.3 crash watch (arms in six-fighter matches)
        }
        HANDLE t = CreateThread(NULL, 0, worker, NULL, 0, NULL);
        if (t) CloseHandle(t);
    }
    return TRUE;
}
