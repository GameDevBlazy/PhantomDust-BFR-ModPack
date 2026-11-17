// ---- v5.3.3 / 5.3.4 crash watch (owner 2026-10-25 12:56 and 13:32: 3 VS 3 LOBBY start -> crash, both times
// exe+0x18dfce with the same value: FUN_14018dfb0 calls method +0x18 of each of the particle manager's 8 children and
// child +0x60's first 8 bytes - its vtable pointer - held 0x80000000bf34fc13, two floats (-0.707, -0.0)).
// 5.3.3 armed only on children whose vtable was still good and only in six-fighter matches: it never armed (the child
// was already bad, or the slot itself points at the wrong place). 5.3.4 watches the manager's slot +0x60 too, from
// game start, and re-aims at a new child on the writing thread at once.
// Game facts (Ghidra batch_065, capstone on the clean exe 9ba7411b):
//  - *0x1405bb210 = the particle manager (FUN_14018e190: MSEffectParticle, 0xb0 from the process heap *0x14052dfe8),
//    children at +0x48 .. +0x80 made on first use (FUN_14010ec50 +0x48, FUN_140114ab0 +0x50 YKParticleFunc00 0x440,
//    FUN_140115450 +0x58, FUN_140110120 +0x60 0x1040, FUN_14010d8f0 +0x68, FUN_1401116b0 +0x70), each starting with
//    its vtable pointer (exe read-only data); FUN_140285aa0 hands out heap blocks with a 0x10 header;
//  - FUN_1401cbd40 (the process manager's task walk) runs FUN_14018dfb0 every frame.
// What this does (official Windows documentation only: AddVectoredExceptionHandler, CreateToolhelp32Snapshot /
// Thread32First / Thread32Next, OpenThread, SuspendThread / GetThreadContext / SetThreadContext / ResumeThread with
// CONTEXT_DEBUG_REGISTERS, the x64 debug registers Dr0-Dr3 / Dr6 / Dr7 as Windows exposes them in CONTEXT):
//  - Dr0 = the manager's slot +0x60, Dr1 = child +0x60's first qword, Dr2 = child +0x50's, Dr3 = child +0x58's (writes,
//    8 bytes) on every game thread, set by a loader thread (every 2 s for new threads, at once for new objects);
//  - a write raises a single-step exception after the writing instruction. The handler keeps the event (writer, old /
//    new value, registers, exe return addresses; up to 24 events, logged by the loader thread). A slot write re-aims Dr1
//    on that thread at once. A child write that is not a vtable while the manager still holds that child is undone (the
//    vtable it had is put back); vtables are the game's own constructors / destructors and are kept.
// Diagnostic only: it names the instruction that breaks the object; the fix goes into the six-fighter manifest.
static const ULONG_PTR RVA_PARTICLES = 0x5bb210;
static const int W_CHILD[4] = {-1, 3, 1, 2};                     // Dr0 slot +0x60; Dr1..3 children +0x60, +0x50, +0x58
struct WatchEv { int dr; DWORD tid; ULONG_PTR rip, addr, oldv, newv, regs[16], ret[12]; };
static WatchEv g_we[24];
static volatile LONG g_we_n, g_we_logged;
static volatile ULONG_PTR g_w_addr[4], g_w_val[4];
static ULONG_PTR g_w_ro_lo, g_w_ro_hi;                           // the exe's .rdata .. end of .data, for "a vtable"
static volatile LONG g_w_test, g_w_stop, g_w_reaim;              // g_w_test: the self-test's watch (no manager check)

static bool watch_is_vtable(ULONG_PTR v) { return v >= g_w_ro_lo && v < g_w_ro_hi; }

static DWORD64 watch_dr7() {                                     // local enable (bit 2i), write (01 at 16 + 4i), 8 bytes (10)
    DWORD64 dr7 = 0;
    for (int i = 0; i < 4; i++)
        if (g_w_addr[i]) dr7 |= (1ull << (2 * i)) | (1ull << (16 + 4 * i)) | (2ull << (18 + 4 * i));
    return dr7;
}

static void watch_keep(int dr, CONTEXT *c, ULONG_PTR a, ULONG_PTR oldv, ULONG_PTR newv) {
    LONG k = InterlockedIncrement(&g_we_n) - 1;
    if (k >= 24) return;
    WatchEv &e = g_we[k];
    e.dr = dr; e.tid = GetCurrentThreadId(); e.rip = c->Rip; e.addr = a; e.oldv = oldv; e.newv = newv;
    const DWORD64 *r = &c->Rax;                                  // Rax .. R15 in CONTEXT order
    for (int j = 0; j < 16; j++) e.regs[j] = (ULONG_PTR)r[j];
    int n = 0;
    __try {
        ULONG_PTR *sp = (ULONG_PTR *)c->Rsp;
        ULONG_PTR top = (ULONG_PTR)((NT_TIB *)NtCurrentTeb())->StackBase;   // v5.148.12 never past this thread's stack
        for (int j = 0; j < 128 && n < 12 && (ULONG_PTR)(sp + j + 1) <= top; j++)
            if (sp[j] >= g_base + 0x1000 && sp[j] < g_base + 0x2d3000) e.ret[n++] = sp[j];
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// v5.3.7 (the 16:33 dump held no stack memory, so its caller was lost): an access violation in the game's code is logged
// at once - where, what was read or written, the registers and the exe return addresses on its stack - then left to the
// game as before (first 4 per run). Logging only: the fault goes on to Windows unchanged.
static volatile LONG g_w_faults;
static void watch_fault(EXCEPTION_POINTERS *ep) {
    ULONG_PTR at = (ULONG_PTR)ep->ExceptionRecord->ExceptionAddress;
    if (at < g_base + 0x1000 || at >= g_base + 0x2d3000) return;
    if (InterlockedIncrement(&g_w_faults) > 4) return;
    CONTEXT *c = ep->ContextRecord;
    char ret[200] = {0};
    int n = 0, len = 0;
    __try {
        ULONG_PTR *sp = (ULONG_PTR *)c->Rsp;
        ULONG_PTR top = (ULONG_PTR)((NT_TIB *)NtCurrentTeb())->StackBase;   // v5.148.12 (log 18:46: it read past the stack's end)
        for (int j = 0; j < 256 && n < 12 && (ULONG_PTR)(sp + j + 1) <= top; j++)
            if (sp[j] >= g_base + 0x1000 && sp[j] < g_base + 0x2d3000) {
                len += sprintf_s(ret + len, sizeof(ret) - len, " %llx", (ULONGLONG)(sp[j] - g_base + 0x140000000ULL));
                n++;
            }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    ULONG_PTR kind = ep->ExceptionRecord->NumberParameters >= 2 ? ep->ExceptionRecord->ExceptionInformation[0] : 9;
    ULONG_PTR addr = ep->ExceptionRecord->NumberParameters >= 2 ? ep->ExceptionRecord->ExceptionInformation[1] : 0;
    logf("FAULT: access violation at exe %llx (%s %llx) on thread %lu", (ULONGLONG)(at - g_base + 0x140000000ULL),
         kind == 0 ? "reading" : kind == 1 ? "writing" : "executing", (ULONGLONG)addr, GetCurrentThreadId());
    logf("FAULT: rax %llx rbx %llx rcx %llx rdx %llx rsi %llx rdi %llx rbp %llx rsp %llx", c->Rax, c->Rbx, c->Rcx, c->Rdx,
         c->Rsi, c->Rdi, c->Rbp, c->Rsp);
    logf("FAULT: r8 %llx r9 %llx r10 %llx r11 %llx r12 %llx r13 %llx r14 %llx r15 %llx", c->R8, c->R9, c->R10, c->R11, c->R12,
         c->R13, c->R14, c->R15);
    logf("FAULT: exe return addresses on its stack:%s", ret);
}

static LONG CALLBACK watch_veh(EXCEPTION_POINTERS *ep) {
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) { watch_fault(ep); return EXCEPTION_CONTINUE_SEARCH; }
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT *c = ep->ContextRecord;
    if (!(c->Dr6 & 0xF)) return EXCEPTION_CONTINUE_SEARCH;
    bool ours = false;
    for (int i = 0; i < 4; i++) {
        ULONG_PTR a = g_w_addr[i];
        if (!(c->Dr6 & (1ull << i)) || !a) continue;
        ours = true;
        __try {
            ULONG_PTR now = *(ULONG_PTR *)a, was = g_w_val[i];
            if (now == was) continue;
            if (i == 0 && !g_w_test) {                           // the slot: a new child (or none) - re-aim Dr1 here
                watch_keep(0, c, a, was, now);
                g_w_val[0] = now;
                g_w_addr[1] = now && !(now & 7) ? now : 0;
                g_w_val[1] = g_w_addr[1] ? *(ULONG_PTR *)g_w_addr[1] : 0;
                c->Dr1 = g_w_addr[1];
                c->Dr7 = watch_dr7();
                InterlockedExchange(&g_w_reaim, 1);
                continue;
            }
            if (watch_is_vtable(now)) { g_w_val[i] = now; continue; }            // the game's own (re)construction
            watch_keep(i, c, a, was, now);
            // v5.35 (owner 2026-10-28 crash 16:31:25 after leaving the Character viewer): the game's heap (free
            // FUN_140285970 / alloc FUN_140285aa0, batch_065) wrote its free-list link into a child being freed while the
            // manager still pointed at it; putting the vtable back broke the free list and FUN_140285aa0 read -1 later.
            // Writes by the heap's own code are the block leaving: never undone, and the watch lets go of that address.
            if (c->Rip >= g_base + 0x285970 && c->Rip < g_base + 0x285c60) { g_w_val[i] = now; continue; }
            bool held = g_w_test != 0;
            if (!held) {
                ULONG_PTR P = *(ULONG_PTR *)(g_base + RVA_PARTICLES);
                held = P && *(ULONG_PTR *)(P + 0x48 + 8 * W_CHILD[i]) == a;
            }
            if (held && watch_is_vtable(was)) *(ULONG_PTR *)a = was;            // the object keeps its vtable
            else g_w_val[i] = now;
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    if (!ours) return EXCEPTION_CONTINUE_SEARCH;
    c->Dr6 = 0;
    return EXCEPTION_CONTINUE_EXECUTION;
}

static int watch_set_threads(DWORD only_tid) {
    DWORD64 dr7 = watch_dr7();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    THREADENTRY32 te = {sizeof(te)};
    DWORD pid = GetCurrentProcessId(), self = GetCurrentThreadId();
    int n = 0;
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid || te.th32ThreadID == self || (only_tid && te.th32ThreadID != only_tid)) continue;
        HANDLE t = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
        if (!t) continue;
        if (SuspendThread(t) != (DWORD)-1) {
            CONTEXT c = {};
            c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            if (GetThreadContext(t, &c)) {
                c.Dr0 = g_w_addr[0]; c.Dr1 = g_w_addr[1]; c.Dr2 = g_w_addr[2]; c.Dr3 = g_w_addr[3];
                c.Dr7 = dr7; c.Dr6 = 0;
                if (SetThreadContext(t, &c)) n++;
            }
            ResumeThread(t);
        }
        CloseHandle(t);
    }
    CloseHandle(snap);
    return n;
}

static const char *watch_name(int dr) {
    static const char *N[4] = {"manager slot +0x60", "child +0x60", "child +0x50", "child +0x58"};
    return dr >= 0 && dr < 4 ? N[dr] : "?";
}

static void watch_log_events() {
    LONG n = g_we_n < 24 ? g_we_n : 24;
    for (; g_we_logged < n; g_we_logged++) {
        WatchEv &e = g_we[g_we_logged];
        char st[220] = "";
        for (int k = 0; k < 12 && e.ret[k]; k++)
            sprintf_s(st + strlen(st), sizeof(st) - strlen(st), " %llx", (unsigned long long)(e.ret[k] - g_base + 0x140000000));
        logf("CRASH WATCH %ld: %s (%p) written by thread %lu at exe %llx: %016llx -> %016llx%s", g_we_logged + 1,
             watch_name(e.dr), (void *)e.addr, e.tid, (unsigned long long)(e.rip - g_base + 0x140000000),
             (unsigned long long)e.oldv, (unsigned long long)e.newv,
             e.dr > 0 ? (e.rip >= g_base + 0x285970 && e.rip < g_base + 0x285c60 ? " (the game's heap freeing it: left alone)"
                         : watch_is_vtable(e.oldv) ? " (not a vtable: put back)" : " (not a vtable)") : "");
        logf("CRASH WATCH %ld: rax %llx rcx %llx rdx %llx rbx %llx rsp %llx rbp %llx rsi %llx rdi %llx", g_we_logged + 1,
             e.regs[0], e.regs[1], e.regs[2], e.regs[3], e.regs[4], e.regs[5], e.regs[6], e.regs[7]);
        logf("CRASH WATCH %ld: r8 %llx r9 %llx r10 %llx r11 %llx r12 %llx r13 %llx r14 %llx r15 %llx", g_we_logged + 1,
             e.regs[8], e.regs[9], e.regs[10], e.regs[11], e.regs[12], e.regs[13], e.regs[14], e.regs[15]);
        logf("CRASH WATCH %ld: exe return addresses on its stack:%s", g_we_logged + 1, st);
    }
}

static DWORD WINAPI watch_thread(LPVOID) {
    ULONGLONG last_set = 0;
    ULONG_PTR seenP = 0;
    while (!g_w_stop) {
        Sleep(50);
        ULONG_PTR want[4] = {}, P = 0;
        __try {
            P = *(ULONG_PTR *)(g_base + RVA_PARTICLES);
            if (P) {
                want[0] = P + 0x48 + 8 * 3;
                for (int i = 1; i < 4; i++) {
                    ULONG_PTR ch = *(ULONG_PTR *)(P + 0x48 + 8 * W_CHILD[i]);
                    want[i] = ch && !(ch & 7) ? ch : 0;
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        bool changed = false;
        for (int i = 0; i < 4; i++) changed |= want[i] != g_w_addr[i];
        ULONGLONG now = GetTickCount64();
        if (changed || g_w_reaim || (P && now - last_set > 2000)) {
            if (changed) {
                for (int i = 0; i < 4; i++) {
                    ULONG_PTR v = 0;
                    __try { v = want[i] ? *(ULONG_PTR *)want[i] : 0; } __except (EXCEPTION_EXECUTE_HANDLER) { want[i] = 0; }
                    g_w_addr[i] = 0; g_w_val[i] = v; g_w_addr[i] = want[i];
                }
            }
            InterlockedExchange(&g_w_reaim, 0);
            int n = watch_set_threads(0);
            last_set = now;
            if (changed) {
                logf("CRASH WATCH: particle manager %p: slot +0x60 holds %p; child +0x60 %p first qword %016llx%s, +0x50 %p, "
                     "+0x58 %p - on %d threads", (void *)P, (void *)g_w_val[0], (void *)want[1], (unsigned long long)g_w_val[1],
                     want[1] ? (watch_is_vtable(g_w_val[1]) ? " (a vtable)" : " (NOT a vtable)") : "",
                     (void *)want[2], (void *)want[3], n);
                if (P != seenP) seenP = P;
                char kids[400] = "";
                __try {
                    for (int k = 0; k < 8 && P; k++) {              // v5.148.9: P is 0 at a match end (it read 0x48: a caught first-chance fault each match)
                        ULONG_PTR ch = *(ULONG_PTR *)(P + 0x48 + 8 * k), v = ch && !(ch & 7) ? *(ULONG_PTR *)ch : 0;
                        sprintf_s(kids + strlen(kids), sizeof(kids) - strlen(kids), " +0x%x=%p[%llx%s]", 0x48 + 8 * k,
                                  (void *)ch, (unsigned long long)v, ch && !watch_is_vtable(v) ? " BAD" : "");
                    }
                } __except (EXCEPTION_EXECUTE_HANDLER) {}
                logf("CRASH WATCH: children:%s", kids);
            }
        }
        if (g_we_n > g_we_logged) watch_log_events();
    }
    return 0;
}

static void watch_find_rdata() {
    BYTE *b = (BYTE *)g_base;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(b + ((IMAGE_DOS_HEADER *)b)->e_lfanew);
    IMAGE_SECTION_HEADER *s = IMAGE_FIRST_SECTION(nt);
    // 5.3.5: the game's vtables are in .data (pdEftAura01 0x14047b5b8), so ".rdata .. end of .data"
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        if (!memcmp(s[i].Name, ".rdata", 6)) g_w_ro_lo = g_base + s[i].VirtualAddress;
        if (!memcmp(s[i].Name, ".data", 6)) g_w_ro_hi = g_base + s[i].VirtualAddress + s[i].Misc.VirtualSize;
    }
    if (g_w_ro_hi <= g_w_ro_lo) g_w_ro_lo = g_w_ro_hi = 0;
}

static void watch_install() {                                    // DllMain path, after step 2 (in the game only)
    watch_find_rdata();
    if (!g_w_ro_lo) { logf("CRASH WATCH: no .rdata section - off"); return; }
    AddVectoredExceptionHandler(1, watch_veh);
    HANDLE t = CreateThread(NULL, 0, watch_thread, NULL, 0, NULL);
    if (t) CloseHandle(t);
    logf("CRASH WATCH: ready (particle manager slot +0x60 and children; read-only data %p-%p)", (void *)g_w_ro_lo, (void *)g_w_ro_hi);
}

// self-test: a stand-in object whose first qword is a "vtable" (inside the host's own .rdata); a second thread writes two
// floats over it; the watch (Dr1) must catch the write, keep the writer and put the vtable back
static volatile ULONG_PTR g_wt_obj[4];
static DWORD WINAPI watch_test_writer(LPVOID) {
    Sleep(50);
    g_wt_obj[0] = 0x80000000bf34fc13ull;                         // the value seen in the 12:56 and 13:32 dumps
    return 0;
}
static int watch_self_test() {
    ULONG_PTR base_keep = g_base, lo = g_w_ro_lo, hi = g_w_ro_hi;
    g_base = (ULONG_PTR)GetModuleHandleW(NULL);
    watch_find_rdata();
    bool ok = g_w_ro_lo != 0;
    ULONG_PTR vt = g_w_ro_lo + 0x100;
    g_wt_obj[0] = vt;
    PVOID veh = ok ? AddVectoredExceptionHandler(1, watch_veh) : NULL;
    InterlockedExchange(&g_w_test, 1);
    g_w_addr[1] = (ULONG_PTR)&g_wt_obj[0]; g_w_val[1] = vt;
    HANDLE t = CreateThread(NULL, 0, watch_test_writer, NULL, CREATE_SUSPENDED, NULL);
    bool set = false;
    if (t) {
        set = watch_set_threads(GetThreadId(t)) == 1;
        ResumeThread(t);
        WaitForSingleObject(t, 2000);
        CloseHandle(t);
    }
    bool caught = g_we_n == 1 && g_we[0].dr == 1 && g_we[0].newv == 0x80000000bf34fc13ull && g_we[0].oldv == vt;
    bool back = g_wt_obj[0] == vt;
    g_w_addr[1] = 0; g_w_val[1] = 0;
    memset((void *)g_we, 0, sizeof(g_we));
    InterlockedExchange(&g_we_n, 0);
    InterlockedExchange(&g_we_logged, 0);
    InterlockedExchange(&g_w_test, 0);
    if (veh) RemoveVectoredExceptionHandler(veh);
    g_base = base_keep; g_w_ro_lo = lo; g_w_ro_hi = hi;
    // v5.3.7 fault log: a made-up access violation inside the host's code range is logged once and goes on unchanged
    EXCEPTION_RECORD er = {};
    CONTEXT fc = {};
    RtlCaptureContext(&fc);
    er.ExceptionCode = EXCEPTION_ACCESS_VIOLATION;
    er.ExceptionAddress = (PVOID)(g_base + 0x1010);
    er.NumberParameters = 2;
    er.ExceptionInformation[1] = 0x10;
    EXCEPTION_POINTERS fp = {&er, &fc};
    LONG f0 = g_w_faults;
    bool flog = watch_veh(&fp) == EXCEPTION_CONTINUE_SEARCH && g_w_faults == f0 + 1;
    InterlockedExchange(&g_w_faults, f0);
    logf("self-test: crash watch armed %s, write caught %s, vtable put back %s, fault logged %s", set ? "ok" : "FAILED",
         caught ? "ok" : "FAILED", back ? "ok" : "FAILED", flog ? "ok" : "FAILED");
    return ok && set && caught && back && flog ? 1 : -64;
}
