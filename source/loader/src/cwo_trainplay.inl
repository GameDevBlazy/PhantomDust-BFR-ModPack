// ---- v5.188.52 TRAIN A CPU, part 2: REPLAY (v5.188.53: on the recording's own stage the CPU starts on the recorded start spot) (owner voice memo 2026-11-21, item 9; the Train a CPU idea of 2026-10-25: "later
// 'watch your CPU' replays you as the CPU"). Offline matches only.
// Game facts (Ghidra full pass 065, pd_fn.py + capstone on the clean exe 9ba7411b, 2026-11-21):
//   - FUN_14024ec80 (a fighter's step) at 0x14024ede0: `mov rcx, rbx; call 0x1401775b0` (48 8b cb e8 c8 87 f2 ff) runs the CPU
//     brain with rcx = rbx = fighter + 0x142d0; after it only rbx is used (imul rcx, [rbx+0x2620] ...), rax is not read;
//   - the brain writes the fighter's input block at fighter + 0x14248 (= brain - 0x88), the block a human's pad fills
//     (cwo_trainrec.inl). No other loader module patches 0x14024ede3.
// What this does: with CwoTrainPlay.txt in the loader's data folder ("latest" or a recording's file name from CwoTrain\, then
// optionally the fighter slot 0-3 to drive; default the first CPU fighter), in an offline match the call at 0x14024ede3 goes
// to a stub (in a page next to the game): it runs the brain as before, then the loader copies the recording's block for this
// tick (block ints 1-12; int 0 and 13-15 stay the game's) over what the brain wrote - for that one fighter only. Every second
// the fighter's position is compared with the recording; once it is more than 20 units (2 m) off (capsules / hits went
// differently) the replay stops and the brain plays on for the rest of the match. The stub stays (installed once, outside a
// match, after its bytes are checked); with no replay on it is the brain call and nothing else.
static const ULONG_PTR TP_SITE = 0x24ede3, TP_BRAIN = 0x1775b0;
static const unsigned char TP_OLD[5] = {0xe8, 0xc8, 0x87, 0xf2, 0xff};
static TrSample *g_tp_buf;
static int g_tp_n, g_tp_state = 0, g_tp_slot = -1, g_tp_t0 = -1, g_tp_last = -1, g_tp_switch = -1, g_tp_stopped;
static volatile ULONG_PTR g_tp_actor;
static ULONG_PTR g_tp_page;
static ULONGLONG g_tp_check_t;
static wchar_t g_tp_file[MAX_PATH];
static char g_tp_want[96];
static int g_tp_want_slot = -1;
static UINT32 g_tp_rec_stage; static char g_tp_rec_pack[16]; static int g_tp_place;   // v5.188.53 the recording's stage

static void tp_after_brain(ULONG_PTR brain) {                    // game thread, inside the fighter's step, after the brain
    __try {
        ULONG_PTR f = brain - 0x142d0;
        if (!g_tp_actor || f != g_tp_actor || g_tp_stopped || !g_tp_buf) return;
        int tick = *(int *)(g_base + TR_TICKS);
        if (g_tp_t0 < 0) {
            g_tp_t0 = tick;
            if (g_tp_place) {                                    // v5.188.53 same stage: start where the recording started
                float *p = (float *)(f + TR_POS);
                memcpy(p, g_tp_buf[0].pos, 12);                  // +0x12E40 position and +0x12E4C previous position: a clean move
                memcpy(p + 3, g_tp_buf[0].pos, 12);
                logf("train a CPU: fighter placed on the recording's start (%.0f, %.0f, %.0f)", p[0], p[1], p[2]);
            }
        }
        int i = tick - g_tp_t0;
        if (i < 0 || i >= g_tp_n) { if (i >= g_tp_n && !g_tp_stopped) { g_tp_stopped = 1; logf("train a CPU: replay finished (%d ticks) - the brain plays on", g_tp_n); } return; }
        UINT32 *blk = (UINT32 *)(f + TR_BLOCK);
        for (int k = 1; k <= 12; k++) blk[k] = g_tp_buf[i].block[k];
        g_tp_last = i;
        if (i % 30 == 0) {
            float *p = (float *)(f + TR_POS);
            float dx = p[0] - g_tp_buf[i].pos[0], dy = p[1] - g_tp_buf[i].pos[1], dz = p[2] - g_tp_buf[i].pos[2];
            float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 > 400.0f) {
                g_tp_stopped = 1;
                logf("train a CPU: off the recording by %.0f units at tick %d (%.1f s) - the brain plays on", sqrtf(d2), i, i / 30.0);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { g_tp_stopped = 1; }
}

static bool tp_install() {                                       // once, outside a match: the stub + the call
    if (g_tp_page) return true;
    unsigned char *site = (unsigned char *)(g_base + TP_SITE);
    if (memcmp(site, TP_OLD, 5)) { logf("train a CPU: the brain call is not the expected bytes - replay off"); return false; }
    ULONG_PTR page = sixf2_near_alloc(0x1000, PAGE_EXECUTE_READWRITE);
    if (!page) { logf("train a CPU: no stub page near the game - replay off"); return false; }
    unsigned char *s = (unsigned char *)page;
    size_t n = 0;
    const unsigned char a[] = {0x48, 0x83, 0xec, 0x28, 0x48, 0x89, 0x4c, 0x24, 0x20, 0x48, 0xb8};   // sub rsp,0x28; mov [rsp+0x20],rcx; mov rax,
    memcpy(s + n, a, sizeof(a)); n += sizeof(a);
    ULONG_PTR brain = g_base + TP_BRAIN; memcpy(s + n, &brain, 8); n += 8;                      //   the brain
    const unsigned char b[] = {0xff, 0xd0, 0x48, 0x8b, 0x4c, 0x24, 0x20, 0x48, 0xb8};           // call rax; mov rcx,[rsp+0x20]; mov rax,
    memcpy(s + n, b, sizeof(b)); n += sizeof(b);
    ULONG_PTR cb = (ULONG_PTR)&tp_after_brain; memcpy(s + n, &cb, 8); n += 8;                  //   tp_after_brain
    const unsigned char c[] = {0xff, 0xd0, 0x48, 0x83, 0xc4, 0x28, 0xc3};                      // call rax; add rsp,0x28; ret
    memcpy(s + n, c, sizeof(c)); n += sizeof(c);
    FlushInstructionCache(GetCurrentProcess(), s, n);
    LONGLONG rel = (LONGLONG)page - (LONGLONG)(g_base + TP_SITE + 5);
    if (rel > 0x7fffffffLL || rel < -0x80000000LL) { VirtualFree((void *)page, 0, MEM_RELEASE); logf("train a CPU: stub out of reach - replay off"); return false; }
    unsigned char call[5] = {0xe8};
    INT32 r32 = (INT32)rel; memcpy(call + 1, &r32, 4);
    DWORD old = 0;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old)) { VirtualFree((void *)page, 0, MEM_RELEASE); logf("train a CPU: code not writable - replay off"); return false; }
    memcpy(site, call, 5);
    VirtualProtect(site, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    g_tp_page = page;
    logf("train a CPU: replay stub %p (%zu bytes) on the brain call exe+0x%llx", (void *)page, n, (unsigned long long)TP_SITE);
    return true;
}

static bool tp_load() {                                          // the recording named in CwoTrainPlay.txt
    wchar_t dir[MAX_PATH], path[MAX_PATH];
    swprintf_s(dir, MAX_PATH, L"%sCwoTrain", g_dir);
    if (!_stricmp(g_tp_want, "latest") || !g_tp_want[0]) {
        WIN32_FIND_DATAW fd; wchar_t pat[MAX_PATH]; FILETIME best = {}; path[0] = 0;
        swprintf_s(pat, MAX_PATH, L"%s\\rec_*.cwotrain", dir);
        HANDLE h = FindFirstFileW(pat, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do { if (CompareFileTime(&fd.ftLastWriteTime, &best) > 0) { best = fd.ftLastWriteTime; swprintf_s(path, MAX_PATH, L"%s\\%s", dir, fd.cFileName); } } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        if (!path[0]) { logf("train a CPU: no recording in CwoTrain\\ - replay off"); return false; }
    } else {
        swprintf_s(path, MAX_PATH, L"%s\\%hs", dir, g_tp_want);
    }
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) { logf("train a CPU: %ls not found - replay off", path); return false; }
    TrHeader h = {}; DWORD got = 0;
    bool ok = ReadFile(f, &h, sizeof(h), &got, NULL) && got == sizeof(h) && !memcmp(h.magic, "CWOTRN01", 8) && h.samples > 0 && h.samples <= (UINT32)TR_MAX;
    if (ok) {
        if (!g_tp_buf) g_tp_buf = (TrSample *)VirtualAlloc(NULL, sizeof(TrSample) * TR_MAX, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        ok = g_tp_buf && ReadFile(f, g_tp_buf, sizeof(TrSample) * h.samples, &got, NULL) && got == sizeof(TrSample) * h.samples;
    }
    CloseHandle(f);
    if (!ok) { logf("train a CPU: %ls is not a whole recording - replay off", path); return false; }
    g_tp_n = (int)h.samples;
    g_tp_rec_stage = h.stage; memcpy(g_tp_rec_pack, h.pack, sizeof(g_tp_rec_pack));
    wcscpy_s(g_tp_file, path);
    logf("train a CPU: replay ready - %ls (%d ticks, %.1f s, recorded on stage %u%s%.16s)", path, g_tp_n, g_tp_n / 30.0, h.stage,
         h.pack[0] ? " " : "", h.pack);
    return true;
}

static void tp_step() {                                          // game thread, every drawn frame
    ULONGLONG now = GetTickCount64();
    if (!g_in_match && now - g_tp_check_t > 3000) {              // the switch, read between matches
        g_tp_check_t = now;
        wchar_t p[MAX_PATH];
        swprintf_s(p, MAX_PATH, L"%sCwoTrainPlay.txt", g_dir);
        FILE *fp = NULL;
        int on = 0;
        if (_wfopen_s(&fp, p, L"r") == 0 && fp) {
            char line[160] = "";
            if (!fgets(line, sizeof(line), fp)) line[0] = 0;
            fclose(fp);
            int slot = -1;
            g_tp_want[0] = 0;
            sscanf_s(line, "%95s %d", g_tp_want, (unsigned)sizeof(g_tp_want), &slot);
            g_tp_want_slot = (slot >= 0 && slot < 4) ? slot : -1;
            on = 1;
        }
        if (on != g_tp_switch) {
            logf("train a CPU: replay %s (CwoTrainPlay.txt)", on ? "ON for offline matches" : "off");
            g_tp_switch = on;
            g_tp_state = 0;
        }
        if (on && !g_tp_page && !tp_install()) g_tp_switch = 0;
    }
    if (!g_in_match || ed_online() || g_tp_switch != 1) {
        if (g_tp_actor) { logf("train a CPU: match over - replayed %d of %d ticks", g_tp_last + 1, g_tp_n); }
        g_tp_actor = 0; g_tp_state = 0;
        return;
    }
    if (g_tp_state) return;                                      // this match is set up (or refused)
    g_tp_state = 1;
    __try {
        if (!tp_load()) return;
        ULONG_PTR *acts = cwo_actors();
        int slot = g_tp_want_slot;
        if (slot < 0)
            for (int p = 0; p < 4 && slot < 0; p++)
                if (acts[p] && *(int *)(acts[p] + 0x20) && !*(int *)(acts[p] + 0x30) && *(int *)(acts[p] + 0x142d0)) slot = p;
        if (slot < 0 || !acts[slot] || *(int *)(acts[slot] + 0x30) || !*(int *)(acts[slot] + 0x142d0)) {
            logf("train a CPU: no CPU fighter to drive in this match - replay off"); return;
        }
        g_tp_t0 = -1; g_tp_last = -1; g_tp_stopped = 0; g_tp_slot = slot;
        {                                                        // v5.188.53 the same stage (and pack) as the recording?
            int stage = *(int *)(g_base + RVA_STAGE);
            char pack[16] = "";
            LONG k = g_mx_cur;
            if (stage == LIST_STAGE && k >= 0 && k < g_mx_count) strncpy_s(pack, g_mx[k].file, _TRUNCATE);
            g_tp_place = (UINT32)stage == g_tp_rec_stage && !strncmp(pack, g_tp_rec_pack, 16);
            logf("train a CPU: %s", g_tp_place ? "same stage as the recording - the CPU starts on its start spot"
                                               : "another stage than the recording - the CPU starts where it spawned");
        }
        g_tp_actor = acts[slot];
        logf("train a CPU: fighter %d plays the recording", slot);
    } __except (EXCEPTION_EXECUTE_HANDLER) { g_tp_actor = 0; }
}
