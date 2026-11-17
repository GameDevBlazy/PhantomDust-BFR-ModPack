// v5.0 the six-fighter table move itself (shared with tests/sixf_test.cpp)
#include <limits.h>
#include "cwo_sixf_sites.h"
static const unsigned SIXF_STRIDE = 0x80;                     // 8 entries (0x40) per table, room after each

// write every site for the tables placed at `block` (+ t * SIXF_STRIDE); `base` = the image; all-or-nothing
static bool sixf_apply(ULONG_PTR base, ULONG_PTR block, char *why, size_t nwhy) {
    const size_t n = sizeof(SIXF_SITES) / sizeof(*SIXF_SITES);
    for (size_t i = 0; i < n; i++) {                            // 1. every site exactly as in the clean exe
        const SixfSite &s = SIXF_SITES[i];
        if (memcmp((const void *)(base + s.rva), s.b, s.len)) { sprintf_s(why, nwhy, "site %zu at +0x%x differs", i, s.rva); return false; }
    }
    for (size_t i = 0; i < n; i++) {                            // 2. every new address fits its form
        const SixfSite &s = SIXF_SITES[i];
        long long tgt = (long long)(block + s.table * SIXF_STRIDE + s.ofs);
        long long d = s.mode == 0 ? tgt - (long long)(base + s.rva + s.len) : tgt - (long long)base;
        if (d < INT_MIN || d > INT_MAX || (s.mode == 1 && d < 0)) { sprintf_s(why, nwhy, "site %zu out of reach", i); return false; }
    }
    for (size_t i = 0; i < n; i++) {                            // 3. write
        const SixfSite &s = SIXF_SITES[i];
        long long tgt = (long long)(block + s.table * SIXF_STRIDE + s.ofs);
        int d = (int)(s.mode == 0 ? tgt - (long long)(base + s.rva + s.len) : tgt - (long long)base);
        memcpy((void *)(base + s.rva + s.dispoff), &d, 4);
    }
    return true;
}

