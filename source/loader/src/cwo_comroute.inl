// v5.188.0 CPU route data per stage (owner 2026-11-15).
//
// GAME FACTS (Ghidra full pass 065, pd_fn.py; AI_NAVIGATION_CAD_SPEC_BATCH019.md):
//  - FUN_140176420 (caller FUN_1401282f0) copies the stage number DAT_1404c01d8 to DAT_1403de3dc and builds the request
//    "com\st%02d" (stage % 10) + "comdata%d.cad" (variant DAT_1404c51f0), read through FUN_140197000 into the AI arena
//    (*0x140530058 + 0x180000, 674,312 bytes). Stage 30 (the Underground) and stage 10 (Map Slot 7, every mx pack) both
//    ask for com\st00 - since 2026-10-23 a copy of Highway's com\st02, so CPUs there follow Highway's cells and barely move.
// What this does (offline only): when the game opens ...\com\st00\comdataN.cad, stage 30 reads com\st30\comdataN.cad and
// stage 10 reads com\<the chosen pack's file, e.g. mx516>\comdataN.cad - when that file exists (game folder or the Store
// override folder); otherwise the request is left alone. The files are built from each stage's own collision
// (30_PD_UE_Stage_Bridge/tools/cad_build.py, same layout and size as the game's).
static bool ed_online();
static const ULONG_PTR RVA_COM_STAGE = 0x3de3dc;

static bool cr_path(LPCWSTR f, wchar_t *out, size_t cap) {
    if (!f || ed_online()) return false;
    const wchar_t *k = NULL;
    for (const wchar_t *q = f; *q; q++) {                // com\st00\comdata at the start or after a separator
        if ((q == f || q[-1] == L'\\' || q[-1] == L'/') && _wcsnicmp(q, L"com", 3) == 0 && (q[3] == L'\\' || q[3] == L'/') &&
            _wcsnicmp(q + 4, L"st00", 4) == 0 && (q[8] == L'\\' || q[8] == L'/') && _wcsnicmp(q + 9, L"comdata", 7) == 0) {
            k = q + 4;
            break;
        }
    }
    if (!k) return false;
    int stage = -1;
    __try { stage = *(int *)(g_base + RVA_COM_STAGE); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    wchar_t name[24];
    if (stage == 30) wcscpy_s(name, L"st30");
    else if (stage == LIST_STAGE) {
        LONG c = g_mx_cur;
        if (c < 0 || c >= g_mx_count || !g_mx[c].file[0]) return false;
        swprintf_s(name, L"%.15hs", g_mx[c].file);
    } else return false;
    size_t head = (size_t)(k - f);
    if (head + wcslen(name) + wcslen(k + 4) + 1 > cap) return false;
    wcsncpy_s(out, cap, f, head);
    wcscat_s(out, cap, name);
    wcscat_s(out, cap, k + 4);
    return true;
}
