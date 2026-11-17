// ---- v5.47.1 crash log (official Windows documentation only: AddVectoredExceptionHandler, EXCEPTION_POINTERS, CONTEXT).
// When the game faults, the registers, the fault address and a few bytes around the objects the code was using are
// written to the loader log before Windows ends the process. Nothing is handled or hidden (EXCEPTION_CONTINUE_SEARCH).
static LONG g_cl_count;
static LONG lw_on_exception(EXCEPTION_POINTERS *ep);

static void cl_bytes(const char *what, const void *p, char *out, size_t n) {
    out[0] = 0;
    if (!p) { snprintf(out, n, "%s: null", what); return; }
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) { snprintf(out, n, "%s %p: unreadable", what, p); return; }
    size_t k = snprintf(out, n, "%s %p:", what, p);
    for (int i = 0; i < 32 && k + 3 < n; i++) k += snprintf(out + k, n - k, " %02x", ((const unsigned char *)p)[i]);
}

static ULONG_PTR cl_q(ULONG_PTR a) {                            // v5.148.12 a qword only if readable (log 18:46: the handler read 0x168)
    MEMORY_BASIC_INFORMATION mbi;
    if (a < 0x10000 || !VirtualQuery((void *)a, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return 0;
    return *(ULONG_PTR *)a;
}
// ---- v5.163.0 crash shield (owner 2026-11-13). Game facts (Ghidra full pass 065 via pd_fn +
// capstone on the clean exe 9ba7411b, MODLOG 2026-11-13): FUN_14001e4b0 sets a texture stage for a draw with the texture
// object from FUN_14001de70(entry); an entry whose +0 is -1 returns its object pointer unchecked, and once the game has
// evicted that effect (FUN_14018cf80) the object is freed - the read mov r8,[rax+0x20] at exe+0x1e4ef faults (crashes
// 11-08 Create an Outfit, 11-10 Mega Photon Burst, 11-10 an effect loaded mid-match). When FUN_14001de70 returns 0 the game
// itself goes on at exe+0x1e50b: FUN_14001d340(0) = its blank texture, set on the same stage (edi, same frame). The shield
// sends only that read fault there, so the particle draws untextured instead of the game ending. Offline only (AGENTS.md);
// both places' bytes and the faulting address (rax + 0x20) are checked first; anything else is left to Windows as before.
static bool ed_online();
static const ULONG_PTR SH_SITE = 0x1e4ef, SH_NOTEX = 0x1e50b;
static const unsigned char SH_SITE_B[] = {0x4c, 0x8b, 0x40, 0x20}, SH_NOTEX_B[] = {0x33, 0xc9, 0xe8};
static volatile LONG g_sh_hits;
static bool g_sh_test;                                          // self-test: the online check is not asked
// v5.179.5 second site (owner 2026-11-13; log 18:05:34
// exe+1e032 reading a freed object, after 241 shield hits in that match). Game fact (pd_fn full pass 065, clean exe):
// FUN_14001e010(entry) RELEASES a texture - entry +0 == -1 -> its object from FUN_14001de70; `cmp byte [rax+0x31], 0`
// (exe+0x1e032) - set = "not ours to release": `xor al, al` ... ret at exe+0x1e038 (returns 0, nothing freed); else the
// object's COM parts are released and it is freed. At the match's end the game releases every particle's texture entry;
// an entry that still points at an object freed by the mid-match eviction (FUN_14018cf80, see above) faults here. The
// shield sends only that read fault to the game's own "not released" return, so nothing is freed twice. Offline only.
static const ULONG_PTR SH2_SITE = 0x1e032, SH2_RET = 0x1e038;
static const unsigned char SH2_SITE_B[] = {0x80, 0x78, 0x31, 0x00, 0x74, 0x0d}, SH2_RET_B[] = {0x32, 0xc0};
static volatile LONG g_sh2_hits;
static bool shield_fault2(EXCEPTION_POINTERS *ep) {
    const EXCEPTION_RECORD *er = ep->ExceptionRecord;
    if (er->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || er->NumberParameters < 2 || er->ExceptionInformation[0] != 0) return false;
    ULONG_PTR rip = (ULONG_PTR)er->ExceptionAddress;
    if (rip != g_base + SH2_SITE || er->ExceptionInformation[1] != ep->ContextRecord->Rax + 0x31) return false;
    if (memcmp((const void *)rip, SH2_SITE_B, sizeof(SH2_SITE_B)) || memcmp((const void *)(g_base + SH2_RET), SH2_RET_B, sizeof(SH2_RET_B))) return false;
    if (!g_sh_test && ed_online()) return false;
    ep->ContextRecord->Rip = g_base + SH2_RET;
    LONG n = InterlockedIncrement(&g_sh2_hits);
    if (n <= 8 || n % 100 == 0)
        logf("crash shield: a texture freed earlier (object %p) released again at exe+1e032 - left alone instead of crashing (%ld so far)",
             (void *)ep->ContextRecord->Rax, n);
    return true;
}
// v5.186.5 third site (seen 2026-11-15 01:52, MODS, an effect pushed out mid-fight: CRASH exe+1d6c0 reading -1, rdi =
// ff130703ff190b06). Game fact (pd_fn full pass 065 + capstone, clean exe): FUN_14001d630(slot, view, sampler) binds a texture
// view - old = slot table *(0x1405b0080 + slot * 8); if new != old: old->Release, the slot := new (`mov [rsi], rdi` at
// exe+1d6b8), then new->AddRef (`mov rax, [rdi]` at exe+1d6c0) and device->PSSetShaderResources(slot, 1, &slot) at exe+1d6c9.
// FUN_14001e4b0 passes it the view of a texture object freed by the eviction (when the first site did not fault: the freed
// memory still read back, with junk inside). For that read fault only, the slot is set back to null and the game goes on at
// exe+1d6c9 - the same path as its own `test rdi, rdi; je` for no texture: the stage is bound empty. Offline only.
static const ULONG_PTR SH3_SITE = 0x1d6c0, SH3_NEXT = 0x1d6c9;
static const unsigned char SH3_B[] = {0x48, 0x8b, 0x07, 0x48, 0x8b, 0xcf, 0xff, 0x50, 0x08};
static volatile LONG g_sh3_hits;
static bool shield_fault3(EXCEPTION_POINTERS *ep) {
    const EXCEPTION_RECORD *er = ep->ExceptionRecord;
    if (er->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || er->NumberParameters < 2 || er->ExceptionInformation[0] != 0) return false;
    ULONG_PTR rip = (ULONG_PTR)er->ExceptionAddress;
    CONTEXT *c = ep->ContextRecord;
    if (rip != g_base + SH3_SITE || memcmp((const void *)rip, SH3_B, sizeof(SH3_B))) return false;
    ULONG_PTR at = (ULONG_PTR)er->ExceptionInformation[1];
    bool noncanon = (c->Rdi >> 47) != 0 && (c->Rdi >> 47) != 0x1FFFF;
    if (at != c->Rdi && !(at == ~(ULONG_PTR)0 && noncanon)) return false;
    if (!g_sh_test && ed_online()) return false;
    __try { if (*(ULONG_PTR *)c->Rsi != c->Rdi) return false; *(ULONG_PTR *)c->Rsi = 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    ULONG_PTR bad = c->Rdi;
    c->Rdi = 0;
    c->Rip = g_base + SH3_NEXT;
    LONG n = InterlockedIncrement(&g_sh3_hits);
    if (n <= 8 || n % 100 == 0)
        logf("crash shield: a freed texture view (%p) bound at exe+1d6c0 - the stage is bound empty instead of crashing (%ld so far)", (void *)bad, n);
    return true;
}
static bool shield_fault(EXCEPTION_POINTERS *ep) {
    if (shield_fault2(ep)) return true;
    if (shield_fault3(ep)) return true;
    const EXCEPTION_RECORD *er = ep->ExceptionRecord;
    if (er->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || er->NumberParameters < 2 || er->ExceptionInformation[0] != 0) return false;
    ULONG_PTR rip = (ULONG_PTR)er->ExceptionAddress;
    if (rip != g_base + SH_SITE || er->ExceptionInformation[1] != ep->ContextRecord->Rax + 0x20) return false;
    if (memcmp((const void *)rip, SH_SITE_B, sizeof(SH_SITE_B)) || memcmp((const void *)(g_base + SH_NOTEX), SH_NOTEX_B, g_sh_test ? 2 : sizeof(SH_NOTEX_B))) return false;
    if (!g_sh_test && ed_online()) return false;
    ep->ContextRecord->Rip = g_base + SH_NOTEX;
    LONG n = InterlockedIncrement(&g_sh_hits);
    if (n <= 8 || n % 1000 == 0)
        logf("crash shield: a freed effect texture (object %p) at exe+1e4ef - drawn with the game's blank texture instead of crashing (%ld so far)",
             (void *)ep->ContextRecord->Rax, n);
    return true;
}

static LONG CALLBACK cl_handler(EXCEPTION_POINTERS *ep) {
    if (!ep || !ep->ExceptionRecord || !ep->ContextRecord) return EXCEPTION_CONTINUE_SEARCH;
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code == EXCEPTION_SINGLE_STEP) return lw_on_exception(ep);
    if (shield_fault(ep)) return EXCEPTION_CONTINUE_EXECUTION;       // v5.163.0 the freed-texture crash
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION && code != EXCEPTION_PRIV_INSTRUCTION
        && code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_STACK_OVERFLOW) return EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedIncrement(&g_cl_count) > 4) return EXCEPTION_CONTINUE_SEARCH;
    const CONTEXT &c = *ep->ContextRecord;
    ULONG_PTR rip = (ULONG_PTR)ep->ExceptionRecord->ExceptionAddress;
    logf("CRASH: code %08x at %p (exe+%llx) %s %p", code, (void *)rip, (unsigned long long)(rip - g_base),
         ep->ExceptionRecord->NumberParameters >= 2 ? (ep->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading") : "",
         ep->ExceptionRecord->NumberParameters >= 2 ? (void *)ep->ExceptionRecord->ExceptionInformation[1] : NULL);
    {   // v5.148.9 the module the fault is in (GetModuleHandleExW FROM_ADDRESS + GetModuleFileNameW; "exe+" is only right for the exe)
        HMODULE hm = NULL; wchar_t mp[MAX_PATH] = L"?";
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)rip, &hm) && hm)
            GetModuleFileNameW(hm, mp, MAX_PATH);
        const wchar_t *bn = wcsrchr(mp, L'\\'); bn = bn ? bn + 1 : mp;
        logf("CRASH: in module %ls + %llx", bn, hm ? (unsigned long long)(rip - (ULONG_PTR)hm) : 0ULL);
    }
    logf("CRASH: rax %llx rbx %llx rcx %llx rdx %llx rsi %llx rdi %llx rbp %llx rsp %llx", c.Rax, c.Rbx, c.Rcx, c.Rdx, c.Rsi, c.Rdi, c.Rbp, c.Rsp);
    logf("CRASH: r8 %llx r9 %llx r10 %llx r11 %llx r12 %llx r13 %llx r14 %llx r15 %llx", c.R8, c.R9, c.R10, c.R11, c.R12, c.R13, c.R14, c.R15);
    char b[200];
    cl_bytes("code", (void *)rip, b, sizeof(b)); logf("CRASH: %s", b);
    const ULONG_PTR regs[] = {c.Rax, c.Rbx, c.Rcx, c.Rdx, c.Rsi, c.Rdi, c.R8, c.R9, c.R12, c.R14, c.R15};
    const char *names[] = {"[rax]", "[rbx]", "[rcx]", "[rdx]", "[rsi]", "[rdi]", "[r8]", "[r9]", "[r12]", "[r14]", "[r15]"};
    for (int i = 0; i < 11; i++) { cl_bytes(names[i], (void *)regs[i], b, sizeof(b)); logf("CRASH: %s", b); }
    for (int i = 0; i < 16; i++) { cl_bytes("[rsp+8n]", (void *)(c.Rsp + 8 * i), b, sizeof(b)); if (i < 4) logf("CRASH: %s", b); }
    ULONG_PTR lab = cl_q(g_base + 0x4c0140);
    logf("CRASH: Lab manager %llx, menu objects [rbx+58] %llx [rbx+60] %llx, big ids %d (maps %p / %p), big skills on %d", (unsigned long long)lab,
         (unsigned long long)cl_q(c.Rbx + 0x58), (unsigned long long)cl_q(c.Rbx + 0x60),
         g_bi_state, (void *)g_bi_mapA, (void *)g_bi_mapB, (int)g_bs_on);
    return EXCEPTION_CONTINUE_SEARCH;
}

static void cl_install() { if (AddVectoredExceptionHandler(1, cl_handler)) logf("crash log: on (+ crash shield for the freed-texture faults exe+1e4ef / 1e032 / 1d6c0)"); }

// offline test: a stand-in function with the same bytes at the same places faults on an unreadable object and comes back
// through the 'no texture' path (returns 1); the hit count and g_base are restored
static int shield_self_test() {
    unsigned char *img = (unsigned char *)VirtualAlloc(NULL, 0x1f000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    unsigned char *bad = (unsigned char *)VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_NOACCESS);
    if (!img || !bad) { if (img) VirtualFree(img, 0, MEM_RELEASE); if (bad) VirtualFree(bad, 0, MEM_RELEASE); return -98; }
    static const unsigned char A[] = {0x48, 0x89, 0xc8, 0x4c, 0x8b, 0x40, 0x20, 0x31, 0xc0, 0xc3};   // mov rax,rcx; mov r8,[rax+20h]; xor eax,eax; ret
    static const unsigned char B[] = {0x33, 0xc9, 0xb8, 0x01, 0x00, 0x00, 0x00, 0xc3};             // xor ecx,ecx; mov eax,1; ret
    memcpy(img + SH_SITE - 3, A, sizeof(A));
    memcpy(img + SH_NOTEX, B, sizeof(B));
    FlushInstructionCache(GetCurrentProcess(), img, 0x1f000);
    ULONG_PTR keep = g_base; LONG kh = g_sh_hits; bool kt = g_sh_test;
    g_base = (ULONG_PTR)img; g_sh_test = true;
    PVOID v = AddVectoredExceptionHandler(1, cl_handler);
    int r = -1;
    if (v) r = ((int (*)(void *))(img + SH_SITE - 3))(bad);
    if (v) RemoveVectoredExceptionHandler(v);
    bool ok = v && r == 1 && g_sh_hits == kh + 1;
    // v5.179.5 the release site: mov rax,rcx; cmp byte [rax+31h],0; je +0Dh | at +6: xor al,al; mov eax,2; ret -> 2 = shielded
    static const unsigned char A2[] = {0x48, 0x89, 0xc8, 0x80, 0x78, 0x31, 0x00, 0x74, 0x0d};
    static const unsigned char B2[] = {0x32, 0xc0, 0xb8, 0x02, 0x00, 0x00, 0x00, 0xc3};
    memcpy(img + SH2_SITE - 3, A2, sizeof(A2));
    memcpy(img + SH2_RET, B2, sizeof(B2));
    FlushInstructionCache(GetCurrentProcess(), img, 0x1f000);
    LONG kh2 = g_sh2_hits;
    v = AddVectoredExceptionHandler(1, cl_handler);
    int r2 = -1;
    if (v) r2 = ((int (*)(void *))(img + SH2_SITE - 3))(bad);
    if (v) RemoveVectoredExceptionHandler(v);
    bool ok2 = v && r2 == 2 && g_sh2_hits == kh2 + 1;
    g_base = keep; g_sh_hits = kh; g_sh2_hits = kh2; g_sh_test = kt;
    VirtualFree(img, 0, MEM_RELEASE); VirtualFree(bad, 0, MEM_RELEASE);
    logf("self-test: crash shield sends the freed-texture fault to the game's blank-texture path %s, the second release to its 'not released' return %s",
         ok ? "ok" : "FAILED", ok2 ? "ok" : "FAILED");
    return ok && ok2 ? 1 : -99;
}


// in-game proof (the agent's test runs only: CwoTestShield.txt in the loader folder, offline): once per launch, 20 s in, the
// game's own texture set-up FUN_14001e4b0 is given an entry the way FUN_14001de70 reads it (+0 = -1 'created', +4 = the
// object) whose object is an unreadable page - the exact fault of the crash logs. Render thread (CwoOverlayFrame), where the
// game draws; the 'no texture' path then sets the game's blank texture on stage 0, which the next draw sets again.
static void shield_live_test() {
    static int frames, done;
    if (done || (++frames % 120) || frames < 1200) return;
    wchar_t p[MAX_PATH]; swprintf_s(p, MAX_PATH, L"%sCwoTestShield.txt", g_dir);
    if (GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES) return;
    done = 1;
    if (ed_online()) { logf("crash shield test: online - not run"); return; }
    if (memcmp((const void *)(g_base + SH_SITE), SH_SITE_B, sizeof(SH_SITE_B)) || memcmp((const void *)(g_base + SH_NOTEX), SH_NOTEX_B, sizeof(SH_NOTEX_B))) {
        logf("crash shield test: the game's code is not the expected bytes - not run"); return;
    }
    unsigned char *bad = (unsigned char *)VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_NOACCESS);
    if (!bad) return;
    unsigned char e[16] = {0};
    *(int *)e = -1;
    void *obj = bad; memcpy(e + 4, &obj, 8);
    LONG before = g_sh_hits;
    logf("crash shield test (CwoTestShield.txt): calling the game's texture set-up FUN_14001e4b0 with a freed-looking texture object %p", (void *)bad);
    ((ULONG_PTR (*)(int, void *))(g_base + 0x1e4b0))(0, e);
    logf("crash shield test: the game's function returned and the game goes on - shield hits %ld -> %ld", before, (long)g_sh_hits);
    VirtualFree(bad, 0, MEM_RELEASE);
}
