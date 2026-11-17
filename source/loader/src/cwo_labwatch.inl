// ---- v5.47.2 write watch (research aid; official Windows documentation only: OpenThread, SuspendThread, GetThreadContext,
// SetThreadContext with CONTEXT_DEBUG_REGISTERS, ResumeThread, EXCEPTION_SINGLE_STEP). A hardware watchpoint (DR0, 4 bytes,
// write) on one address of the game thread; every write is logged with the writer's address and registers, then execution
// continues. Used to find what overwrote the Lab manager's page field (+0x4ee4) in 5.47.0.
static DWORD g_lw_tid;                                           // the game thread (the one running the frame hook)
static ULONG_PTR g_lw_addr, g_lw_armed;
static LONG g_lw_hits;

static DWORD WINAPI lw_arm_thread(LPVOID p) {
    ULONG_PTR addr = (ULONG_PTR)p;
    HANDLE t = OpenThread(THREAD_ALL_ACCESS, FALSE, g_lw_tid);
    if (!t) { logf("watch: cannot open thread %u", g_lw_tid); return 0; }
    if (SuspendThread(t) == (DWORD)-1) { CloseHandle(t); logf("watch: cannot suspend"); return 0; }
    CONTEXT c; memset(&c, 0, sizeof(c)); c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    bool ok = GetThreadContext(t, &c) != 0;
    if (ok) {
        c.Dr0 = addr;
        c.Dr7 = (c.Dr7 & ~0xfULL & ~(0xfULL << 16)) | 1 | (1ULL << 16) | (3ULL << 18);   // L0, write, 4 bytes
        if (!addr) c.Dr7 &= ~1ULL;
        ok = SetThreadContext(t, &c) != 0;
    }
    ResumeThread(t); CloseHandle(t);
    logf("watch: %s write watch on %p (thread %u)", ok ? (addr ? "armed" : "cleared") : "FAILED to set", (void *)addr, g_lw_tid);
    return 0;
}

static void lw_arm(ULONG_PTR addr) {                             // from the game thread: arm (or clear with 0) asynchronously
    if (!g_lw_tid) g_lw_tid = GetCurrentThreadId();
    g_lw_armed = addr;
    HANDLE h = CreateThread(NULL, 0, lw_arm_thread, (LPVOID)addr, 0, NULL);
    if (h) CloseHandle(h);
}

static LONG lw_on_exception(EXCEPTION_POINTERS *ep) {            // called first by the crash log's handler
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP || !ep->ContextRecord || !(ep->ContextRecord->Dr6 & 1)) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT &c = *ep->ContextRecord;
    c.Dr6 = 0;
    LONG n = InterlockedIncrement(&g_lw_hits);
    if (n <= 12) {
        ULONG_PTR rip = (ULONG_PTR)ep->ExceptionRecord->ExceptionAddress;
        logf("WATCH %d: %p written (now %08x) by %p (exe+%llx) rax %llx rcx %llx rdx %llx rsi %llx rdi %llx r8 %llx r9 %llx", n,
             (void *)g_lw_armed, *(unsigned *)g_lw_armed, (void *)rip, (unsigned long long)(rip - g_base), c.Rax, c.Rcx, c.Rdx, c.Rsi, c.Rdi, c.R8, c.R9);
        char b[200]; cl_bytes("code", (void *)(rip - 16), b, sizeof(b)); logf("WATCH %d: %s", n, b);
    }
    return EXCEPTION_CONTINUE_EXECUTION;
}

// every frame: the Lab manager's page field (+0x4ee4) is watched whenever a Lab manager exists
static void lw_step() {
    ULONG_PTR lab = *(ULONG_PTR *)(g_base + 0x4c0140);
    ULONG_PTR want = lab ? lab + 0x4ee4 : 0;
    if (want != g_lw_armed) lw_arm(want);
}
