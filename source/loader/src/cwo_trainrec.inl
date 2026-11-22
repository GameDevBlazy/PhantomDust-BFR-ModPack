// ---- v5.188.51 TRAIN A CPU, part 1: RECORD (owner voice memo 2026-11-21, item 9 "smarter bots"; the Train a CPU idea of
// 2026-10-25: "play the match as that character; the whole match is recorded (inputs, where it goes on the map, skill pickups,
// timings) ... then the quest replays both recordings"). Offline matches only; READ ONLY on the game (nothing is written to it).
// Game facts (Ghidra full pass 065, pd_fn.py, recorded 2026-11-02 HOST_BOT_RESEARCH + checked again 2026-11-21):
//   - FUN_14024ec80 (a fighter's step): fighter +0x30 != 0 = a local human -> FUN_140256580(fighter + 0x14248) copies its
//     pad (slot = block int 0, table 0x14059b800, 0x34 bytes per pad) into the INPUT BLOCK at +0x14248: int 5 (+0x1425c) the
//     held buttons, int 6 the previous, int 7 the newly pressed (prev ^ now) & now, then the sticks (FUN_140256600 / 140256710);
//   - +0x30 == 0 with a brain at +0x142d0: the CPU brain FUN_1401775b0 writes THE SAME block (it clears +0x1425c, and
//     FUN_140178ad0 sets its button bits) - so a recorded human block is exactly what a CPU fighter's step reads (part 2).
//   - the game tick counter *0x14052dd6c (cwo_fps60.inl: changes once per game logic step, ~30 a second); position +0x12E40.
// What this does: with CwoTrainRecord.txt in the loader's data folder (a test switch until the Train a CPU page exists), every
// offline match records the first human fighter: one sample per game tick = tick, position (3 floats) and the 64-byte input
// block (+0x14248..+0x14287). At the match end it is saved as CwoTrain\rec_<date>_<time>_s<stage>.cwotrain:
//   header "CWOTRN01", u32 stage, u32 fighter slot, u32 samples, char[16] pack file ("" for stock stages), then the samples
//   (u32 tick, f32 x, y, z, u32 block[16]) = 80 bytes each. Up to 30 minutes per match (54,000 samples, 4.3 MB).
static const ULONG_PTR TR_TICKS = 0x52dd6c, TR_BLOCK = 0x14248, TR_POS = 0x12E40;
static const int TR_MAX = 54000;
#pragma pack(push, 1)
struct TrSample { UINT32 tick; float pos[3]; UINT32 block[16]; };
struct TrHeader { char magic[8]; UINT32 stage, slot, samples; char pack[16]; };
#pragma pack(pop)
static TrSample *g_tr_buf;
static int g_tr_n = -1, g_tr_slot = -1, g_tr_last_tick = -1, g_tr_stage = -1, g_tr_switch = -1;
static char g_tr_pack[16];
static ULONGLONG g_tr_check_t;
static unsigned g_tr_logs;

static void tr_save() {
    if (g_tr_n <= 0 || !g_tr_buf) { g_tr_n = -1; return; }
    wchar_t dir[MAX_PATH], path[MAX_PATH];
    swprintf_s(dir, MAX_PATH, L"%sCwoTrain", g_dir);
    CreateDirectoryW(dir, NULL);
    SYSTEMTIME st; GetLocalTime(&st);
    swprintf_s(path, MAX_PATH, L"%s\\rec_%04d%02d%02d_%02d%02d%02d_s%d.cwotrain", dir, st.wYear, st.wMonth, st.wDay, st.wHour,
               st.wMinute, st.wSecond, g_tr_stage);
    TrHeader h = {};
    memcpy(h.magic, "CWOTRN01", 8);
    h.stage = (UINT32)g_tr_stage; h.slot = (UINT32)g_tr_slot; h.samples = (UINT32)g_tr_n;
    memcpy(h.pack, g_tr_pack, sizeof(h.pack));
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD w1 = 0, w2 = 0;
    bool ok = f != INVALID_HANDLE_VALUE && WriteFile(f, &h, sizeof(h), &w1, NULL) &&
              WriteFile(f, g_tr_buf, (DWORD)(sizeof(TrSample) * g_tr_n), &w2, NULL) && w2 == sizeof(TrSample) * (DWORD)g_tr_n;
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    logf("train a CPU: %s %d ticks (%.1f s) of fighter %d on stage %d%s%s -> %ls", ok ? "recorded" : "could NOT save", g_tr_n,
         g_tr_n / 30.0, g_tr_slot, g_tr_stage, g_tr_pack[0] ? " " : "", g_tr_pack, path);
    g_tr_n = -1;
}

static void tr_step() {                                          // game thread, every drawn frame
    ULONGLONG now = GetTickCount64();
    if (now - g_tr_check_t > 3000 && g_tr_n < 0) {                // the switch file, looked at every 3 s outside a recording
        g_tr_check_t = now;
        wchar_t p[MAX_PATH];
        swprintf_s(p, MAX_PATH, L"%sCwoTrainRecord.txt", g_dir);
        int on = GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES;
        if (on != g_tr_switch) { logf("train a CPU: recording %s (CwoTrainRecord.txt)", on ? "ON for offline matches" : "off"); g_tr_switch = on; }
    }
    bool want = g_tr_switch == 1 && g_in_match && !ed_online();
    if (!want) { if (g_tr_n >= 0) tr_save(); return; }
    __try {
        int tick = *(int *)(g_base + TR_TICKS);
        ULONG_PTR *acts = cwo_actors();
        if (g_tr_n < 0) {                                        // a new match: the first local human fighter
            g_tr_slot = -1;
            for (int p = 0; p < 4 && g_tr_slot < 0; p++)
                if (acts[p] && *(int *)(acts[p] + 0x20) && *(int *)(acts[p] + 0x30)) g_tr_slot = p;
            if (g_tr_slot < 0) return;
            if (!g_tr_buf) g_tr_buf = (TrSample *)VirtualAlloc(NULL, sizeof(TrSample) * TR_MAX, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (!g_tr_buf) { if (g_tr_logs++ < 3) logf("train a CPU: no memory for the recording"); return; }
            g_tr_stage = *(int *)(g_base + RVA_STAGE);
            g_tr_pack[0] = 0;
            LONG k = g_mx_cur;
            if (g_tr_stage == LIST_STAGE && k >= 0 && k < g_mx_count) strncpy_s(g_tr_pack, g_mx[k].file, _TRUNCATE);
            g_tr_n = 0; g_tr_last_tick = tick - 1;
            logf("train a CPU: recording fighter %d on stage %d%s%s", g_tr_slot, g_tr_stage, g_tr_pack[0] ? " " : "", g_tr_pack);
        }
        if (tick == g_tr_last_tick || g_tr_n >= TR_MAX) return;     // one sample per game tick
        g_tr_last_tick = tick;
        ULONG_PTR a = acts[g_tr_slot];
        if (!a) return;
        TrSample &s = g_tr_buf[g_tr_n++];
        s.tick = (UINT32)tick;
        memcpy(s.pos, (void *)(a + TR_POS), sizeof(s.pos));
        memcpy(s.block, (void *)(a + TR_BLOCK), sizeof(s.block));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (g_tr_logs++ < 3) logf("train a CPU: fighter read failed - recording stopped");
        if (g_tr_n >= 0) tr_save();
    }
}
