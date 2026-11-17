// ---- v4.5 Create a Character live preview, everything (owner 2026-10-25). C++ port of our own Python tools, checked byte for byte against them (tests/cb2_test.cpp):
//   build_body_swap.py  build_npc_body (+ relaxed_map, skinned_joints, bind_vectors) - a model-only NPC body;
//   build_part_swap.py  pieces / palette / bind_world / refit_vertices / swap - mesh pieces moved between skeletons;
//   build_section_swap.py subtree / piece_shares / section_of; build_character.py head_swap_any / section_swap_any.
// Facts (from those tools' docs, CONFIRMED there): skinned body pieces = mesh chunks 0x02 whose vertex descriptor (chunk
// 0x16, 28-byte entries, code at +0) is 0x11 (stride 24): +0x3C ordinal, +0x40 material, +0x42 holder, +0x44 descriptor,
// +0x48 first vertex, +0x4C vertex count, +0x50 index count, +0x68 u16 indices, then the piece's bone list (u32 count +
// bytes). Vertex bone codes 120 + 3 * slot (+20 / +22), weights +18 / +17. Materials: chunk 0x01, u16 count at +8,
// 0x4C-byte entries from +12 (+0x16 texture mask, +0x18 four u16 texture indices). Texture table chunk 0x15: i16 count at
// +8, 28-byte entries from +12 (+4 resource offset, +12 packed size word).
#include <map>
#include <set>
#include <array>
#include <string>
#include <climits>

typedef std::array<double, 3> V3;
static const char *const CB_NPCS[9] = {"npc01a", "npc04a", "npc05a", "npc08a", "npc09a", "npc10a", "npc12a", "npc13a",
                                        "npc02a"};       // v4.9 Spokesman (19)
// v5.10 owner HARD RULE (2026-10-26, replaces the one-way v4.9 rule): "female characters can only be created using female
// character body parts", male only male; the same for animations. Female: Freia 2, Tsubutaki 8, Mikan 16 (and Baroness 15,
// who is not offered). Mac and Baroness give no parts. Move sources 11-21 are the OG files of players 0-10.
// (build_character.part_allowed / moves_allowed)
static bool cb_female(int ch) { return ch == 2 || ch == 8 || ch == 15 || ch == 16; }
static bool cb_part_allowed(int body, int donor) {
    if (donor == 14 || donor == 15) return false;
    return cb_female(donor) == cb_female(body);
}
static bool cb_moves_allowed(int body, int src) {                      // src: a move source 0-21
    return src < 0 || cb_female(src <= 10 ? src : src - 11) == cb_female(body);
}

static int pyfloordiv(int a, int b) { int q = a / b; if ((a % b != 0) && ((a < 0) != (b < 0))) q--; return q; }
static float rdf(const unsigned char *p) { float f; memcpy(&f, p, 4); return f; }
static void put16at(Bytes &b, size_t o, unsigned v) { b[o] = (unsigned char)v; b[o + 1] = (unsigned char)(v >> 8); }
static void put32at(Bytes &b, size_t o, unsigned v) { b[o] = (unsigned char)v; b[o + 1] = (unsigned char)(v >> 8); b[o + 2] = (unsigned char)(v >> 16); b[o + 3] = (unsigned char)(v >> 24); }

// an ordered joint map (Python dict: insertion order kept; a key set again keeps its place)
struct JMap {
    std::vector<std::pair<int, int>> kv;
    int get(int k) const { for (auto &p : kv) if (p.first == k) return p.second; return INT_MIN; }
    bool has(int k) const { return get(k) != INT_MIN; }
    void set(int k, int v) { for (auto &p : kv) if (p.first == k) { p.second = v; return; } kv.push_back({k, v}); }
};
static JMap jmap_of(const std::vector<int> &jm, const std::vector<int> &order) {
    JMap m;
    for (int a : order) if (a >= 0 && a < (int)jm.size() && jm[a] >= 0) m.set(a, jm[a]);
    return m;
}

// first skeleton chunk (3) of the groups, from group index `from` on; parents and bind offsets
static bool skel_any(const Alr &m, int from, std::vector<int> *par, std::vector<V3> *bind) {
    std::vector<Chunk> ch;
    for (size_t gi = from; gi < m.slot.size(); gi++) {
        int s = m.slot[gi];
        if (s < 0 || !walk(m.groups[s], ch)) continue;
        for (const Chunk &c : ch) {
            if (c.id != 3) continue;
            const unsigned char *g = &m.groups[s][c.off];
            int n = rd16(g + 8);
            if (par) par->resize(n);
            if (bind) bind->resize(n);
            for (int k = 0; k < n; k++) {
                if (par) (*par)[k] = (short)rd16(g + 12 + 64 * k + 46);
                if (bind) for (int c3 = 0; c3 < 3; c3++) (*bind)[k][c3] = rdf(g + 12 + 64 * k + 4 + 4 * c3);
            }
            return true;
        }
    }
    return false;
}

static std::set<int> skinned_joints(const Alr &m) {
    std::set<int> out;
    std::vector<Chunk> ch;
    for (int s : m.slot) {
        if (s < 0) continue;
        const Bytes &g = m.groups[s];
        if (!walk(g, ch)) continue;
        std::vector<int> codes;
        for (const Chunk &c : ch) if (c.id == 0x16) {
            codes.clear();
            unsigned n = rd32(&g[c.off + 8]);
            for (unsigned k = 0; k < n; k++) codes.push_back(g[c.off + 12 + 28 * k]);
        }
        for (const Chunk &c : ch) {
            if (c.id != 2) continue;
            unsigned desc = rd16(&g[c.off + 0x44]), cnt = rd32(&g[c.off + 0x50]);
            if (desc >= codes.size() || codes[desc] != 0x11) continue;
            size_t t0 = c.off + 0x68 + 2 * (size_t)cnt, t1 = c.off + c.span;
            if (t0 > t1) t0 = t1;
            for (size_t p = 0; p < 8; p += 2) {
                if (t0 + p + 4 > t1) break;
                unsigned n = rd32(&g[t0 + p]);
                if (n > 0 && n < 64) {
                    for (size_t q = t0 + p + 4; q < t0 + p + 4 + n && q < t1; q++) out.insert(g[q]);
                    break;
                }
            }
        }
    }
    return out;
}

static int max_width(const std::vector<int> &par) {
    std::map<int, int> c;
    int best = 0;
    for (int p : par) if (p != -1) best = (std::max)(best, ++c[p]);
    return best;
}

// relaxed_map: best fit with children paired in any order (see build_body_swap.py)
struct Relax {
    std::vector<std::vector<int>> kn, kb;
    std::vector<char> used;
    std::vector<V3> mt, bt;
    std::set<int> skinned;
    struct Res { double sc; JPairs pairs; };
    std::map<std::pair<int, int>, Res> best_memo, bridged_memo;
    double cosv(int a, int b) {
        const V3 &u = mt[a], &v = bt[b];
        double su = 0, sv = 0, d = 0;
        for (int i = 0; i < 3; i++) su += u[i] * u[i];
        for (int i = 0; i < 3; i++) sv += v[i] * v[i];
        double nu = pow(su, 0.5), nv = pow(sv, 0.5);
        if (nu < 1e-4 || nv < 1e-4) return 0.0;
        for (int i = 0; i < 3; i++) d += u[i] * v[i];
        return d / (nu * nv);
    }
    Res f(int a, int b, size_t i, unsigned mask, std::map<std::pair<size_t, unsigned>, Res> &memo) {
        auto key = std::make_pair(i, mask);
        auto it = memo.find(key);
        if (it != memo.end()) return it->second;
        const std::vector<int> &ca = kn[a + 1], &cb = kb[b + 1];
        Res r;
        if (i == ca.size()) { r.sc = 0.0; memo[key] = r; return r; }
        Res bestr = f(a, b, i + 1, mask, memo);                 // this move child left unplaced
        for (size_t j = 0; j < cb.size(); j++) {
            if (mask >> j & 1) continue;
            Res br = bridged(ca[i], cb[j]);
            Res rest = f(a, b, i + 1, mask | (1u << j), memo);
            double s = br.sc + rest.sc;
            if (s > bestr.sc) { bestr.sc = s; bestr.pairs = br.pairs; bestr.pairs.insert(bestr.pairs.end(), rest.pairs.begin(), rest.pairs.end()); }
        }
        memo[key] = bestr;
        return bestr;
    }
    Res best(int a, int b) {
        auto key = std::make_pair(a, b);
        auto it = best_memo.find(key);
        if (it != best_memo.end()) return it->second;
        std::map<std::pair<size_t, unsigned>, Res> memo;
        Res r = f(a, b, 0, 0, memo);
        r.sc = r.sc + ((a < (int)used.size() && used[a]) ? 1 : 0) + 0.01 * cosv(a, b) + (skinned.count(b) ? 0.02 : 0.0);
        best_memo[key] = r;
        return r;
    }
    Res bridged(int a, int b) {
        auto key = std::make_pair(a, b);
        auto it = bridged_memo.find(key);
        if (it != bridged_memo.end()) return it->second;
        Res bs_ = best(a, b);
        Res r;
        r.sc = bs_.sc;
        r.pairs.push_back({a, b});
        r.pairs.insert(r.pairs.end(), bs_.pairs.begin(), bs_.pairs.end());
        if (kn[a + 1].size() == 1) {
            Res o = bridged(kn[a + 1][0], b);
            if (o.sc > r.sc) r = o;
        }
        bridged_memo[key] = r;
        return r;
    }
};
static bool relaxed_map(const Alr &M, const std::vector<int> &bpar, const std::vector<V3> *bbind, const std::set<int> &skinned,
                        JMap &jm, int &placed, int &total, const std::set<int> *only = NULL) {
    std::vector<int> pm;
    Relax R;
    if (!skeleton(M, pm, NULL) || !animated(M, R.used)) return false;
    int na = anim_count(M);
    if (!skel_any(M, na, NULL, &R.mt)) return false;
    std::vector<char> keep(pm.size(), 0);
    for (size_t k = 0; k < R.used.size(); k++) {
        if (!R.used[k]) continue;
        int j = (int)k;
        while (j != -1 && j < (int)pm.size() && !keep[j]) { keep[j] = 1; j = pm[j]; }
    }
    R.kn.resize(pm.size() + 1); R.kb.resize(bpar.size() + 1);
    for (size_t k = 0; k < pm.size(); k++) if (keep[k]) R.kn[pm[k] + 1].push_back((int)k);
    for (size_t k = 0; k < bpar.size(); k++) if (!only || only->count((int)k)) R.kb[bpar[k] + 1].push_back((int)k);
    if (bbind) R.bt = *bbind; else R.bt.assign(bpar.size(), V3{0.0, 0.0, 0.0});
    R.skinned = skinned;
    if (R.kn[0].size() != 1) return false;
    int r = R.kn[0][0], bsel = -1;
    double bsc = 0;
    for (int b = 0; b < (int)bpar.size(); b++) {                  // max((score, b)): ties go to the larger b
        double s = R.best(r, b).sc;
        if (bsel < 0 || s > bsc || (s == bsc && b > bsel)) { bsc = s; bsel = b; }
    }
    if (bsel < 0) return false;
    Relax::Res res = R.best(r, bsel);
    jm = JMap();
    jm.set(r, bsel);
    for (auto &p : res.pairs) jm.set(p.first, p.second);
    total = 0; placed = 0;
    for (size_t j = 0; j < R.used.size(); j++) if (R.used[j]) { total++; if (jm.has((int)j)) placed++; }
    return true;
}

static void rescale_jm(Bytes &g, const JMap &jm, const std::vector<double> &lm, const std::vector<double> &lb) {
    double sy = 0, sx = 0;
    for (auto &p : jm.kv) if (lm[p.first] > 1e-4 && lb[p.second] > 1e-4) { sx += lm[p.first]; sy += lb[p.second]; }
    double overall = sy / sx;
    std::vector<Chunk> ch;
    walk(g, ch);
    for (const Chunk &c : ch) {
        int j = rd16(&g[c.off + 12]);
        unsigned n1 = rd32(&g[c.off + 16]), stride = g[c.off + 30];
        if (!n1) continue;
        int b = jm.get(j);
        double r = (lm[j] > 1e-4 && lb[b] > 1e-4) ? lb[b] / lm[j] : overall;
        for (unsigned k = 0; k < n1; k++)
            for (unsigned c4 = 1; c4 < stride / 4; c4++) {
                size_t o = c.off + 32 + (size_t)stride * k + 4 * c4;
                float v = (float)((double)rdf(&g[o]) * r);
                memcpy(&g[o], &v, 4);
            }
    }
}

// build_npc_body(moves, body, scale=True, skin_aware=True): (file, joint map)
static bool npc_body(const Alr &M, const Alr &B, Alr &out, JMap &jm, char *why, size_t nwhy) {
    std::vector<int> bpar;
    if (!skel_any(B, 0, &bpar, NULL)) { sprintf_s(why, nwhy, "the NPC file has no skeleton"); return false; }
    bool exact = false;
    std::vector<int> sj, so;
    if (structural_map(M, bpar, sj, so)) {
        jm = jmap_of(sj, so);
        exact = true;
        if (max_width(bpar) <= 8) {
            std::vector<V3> bb;
            const std::vector<V3> *pb = NULL;
            if (anim_count(B) && skel_any(B, anim_count(B), NULL, &bb)) pb = &bb;
            else if (skel_any(B, 0, NULL, &bb)) pb = &bb;
            std::set<int> sk = skinned_joints(B);
            JMap rj; int placed = 0, total = 0;
            if (relaxed_map(M, bpar, pb, sk, rj, placed, total)) {
                std::vector<char> used;
                animated(M, used);
                auto unsk = [&](const JMap &m) { int n = 0; for (size_t j = 0; j < used.size(); j++) if (used[j] && m.has((int)j) && !sk.count(m.get((int)j))) n++; return n; };
                if (placed == total && unsk(rj) < unsk(jm)) { jm = rj; exact = false; }
            }
        }
    } else {
        std::set<int> only, *po = NULL;
        if (max_width(bpar) > 12) {                              // v4.9: helper leaves (no skin, no children) left out
            std::set<int> sk0 = skinned_joints(B), kids;
            for (int p : bpar) if (p != -1) kids.insert(p);
            for (int k = 0; k < (int)bpar.size(); k++) if (sk0.count(k) || kids.count(k) || bpar[k] == -1) only.insert(k);
            std::map<int, int> w;
            int mw = 0;
            for (int k : only) if (bpar[k] != -1) mw = (std::max)(mw, ++w[bpar[k]]);
            if (!mw || mw > 12) { sprintf_s(why, nwhy, "the NPC skeleton is too wide to fit"); return false; }
            po = &only;
        }
        std::vector<V3> bb;
        bool have = skel_any(B, 0, NULL, &bb);
        int placed = 0, total = 0;
        if (!relaxed_map(M, bpar, have ? &bb : NULL, skinned_joints(B), jm, placed, total, po)) { sprintf_s(why, nwhy, "these moves do not fit the NPC"); return false; }
        if (placed < 0.85 * total) { sprintf_s(why, nwhy, "the moves fit only %d of %d joints of the NPC", placed, total); return false; }
    }
    std::vector<int> pp;
    std::vector<double> lm, lb;
    skeleton(M, pp, &lm);
    skeleton(B, pp, &lb);
    int nm = anim_count(M);
    out = B;
    out.groups.clear();
    std::vector<Chunk> ch;
    for (int gi = 0; gi < nm; gi++) {
        Bytes g = M.groups[M.slot[gi]];
        walk(g, ch);
        if (!exact) {                                            // tracks of joints the body lacks are dropped
            Bytes kept;
            for (const Chunk &c : ch) if (jm.has(rd16(&g[c.off + 12]))) kept.insert(kept.end(), g.begin() + c.off, g.begin() + c.off + c.span);
            size_t end = ch.empty() ? 0 : ch.back().off + ch.back().span;
            kept.insert(kept.end(), g.begin() + end, g.end());
            g = kept;
        }
        rescale_jm(g, jm, lm, lb);
        walk(g, ch);
        for (const Chunk &c : ch) put16at(g, c.off + 12, (unsigned)jm.get(rd16(&g[c.off + 12])));
        out.groups.push_back(g);
    }
    for (const Bytes &g : B.groups) out.groups.push_back(g);
    out.slot.clear(); out.raw.clear();
    for (int gi = 0; gi < nm; gi++) { out.slot.push_back(gi); out.raw.push_back(0); }
    for (int s : B.slot) { out.slot.push_back(s < 0 ? -1 : nm + s); out.raw.push_back(0); }
    out.has_res = true;
    return true;
}

// ---------------------------------------------------------------- build_part_swap.py
struct Piece { size_t co, sp; int mat, holder, desc; unsigned base, used, cnt; std::vector<int> pal; size_t pal_off; bool has_pal; };
static int body_slot(const Alr &m) {
    int na = anim_count(m);
    std::vector<Chunk> ch;
    for (int gi = na; gi < (int)m.slot.size(); gi++) {
        int s = m.slot[gi];
        if (s < 0 || !walk(m.groups[s], ch)) continue;
        for (const Chunk &c : ch) if (c.id == 0x16) return gi;
    }
    return -1;
}
static void pieces(const Bytes &g, std::vector<Piece> &out, std::vector<int> &codes, std::vector<size_t> &descs) {
    std::vector<Chunk> ch;
    walk(g, ch);
    codes.clear(); descs.clear(); out.clear();
    for (const Chunk &c : ch) if (c.id == 0x16) {
        unsigned n = rd32(&g[c.off + 8]);
        for (unsigned k = 0; k < n; k++) { size_t d = c.off + 12 + 28 * k; codes.push_back(g[d]); descs.push_back(d); }
    }
    for (const Chunk &c : ch) {
        if (c.id != 2) continue;
        Piece p;
        p.co = c.off; p.sp = c.span;
        p.mat = rd16(&g[c.off + 0x40]); p.holder = rd16(&g[c.off + 0x42]); p.desc = rd16(&g[c.off + 0x44]);
        if (p.desc >= (int)codes.size() || codes[p.desc] != 0x11) continue;
        p.base = rd32(&g[c.off + 0x48]); p.used = rd32(&g[c.off + 0x4C]); p.cnt = rd32(&g[c.off + 0x50]);
        p.has_pal = false; p.pal_off = 0;
        size_t t0 = c.off + 0x68 + 2 * (size_t)p.cnt;
        for (size_t q = 0; q < 8; q += 2) {
            if (t0 + q + 4 > c.off + c.span) break;
            unsigned n = rd32(&g[t0 + q]);
            if (n > 0 && n < 64) {
                for (size_t k = 0; k < n && t0 + q + 4 + k < g.size(); k++) p.pal.push_back(g[t0 + q + 4 + k]);
                p.has_pal = true; p.pal_off = t0 + q;
                break;
            }
        }
        out.push_back(p);
    }
}
static bool bind_world(const Bytes &g, std::vector<V3> &out) {
    std::vector<Chunk> ch;
    walk(g, ch);
    for (const Chunk &c : ch) {
        if (c.id != 3) continue;
        int n = rd16(&g[c.off + 8]);
        std::vector<int> par(n);
        std::vector<V3> tr(n);
        for (int k = 0; k < n; k++) {
            par[k] = (short)rd16(&g[c.off + 12 + 64 * k + 46]);
            for (int i = 0; i < 3; i++) tr[k][i] = rdf(&g[c.off + 12 + 64 * k + 4 + 4 * i]);
        }
        out.assign(n, V3{0, 0, 0});
        for (int j = 0; j < n; j++) {
            V3 x{0.0, 0.0, 0.0};
            int k = j, guard = 0;
            while (k != -1 && k >= 0 && k < n && guard++ < 256) { for (int i = 0; i < 3; i++) x[i] = x[i] + tr[k][i]; k = par[k]; }
            out[j] = x;
        }
        return true;
    }
    return false;
}
static void refit_vertices(Bytes &vb, unsigned start, unsigned count, const std::vector<int> &pal, const std::map<int, V3> &delta) {
    for (unsigned v = start; v < start + count; v++) {
        size_t o = 24 * (size_t)v;
        double wa = vb[o + 18] / 255.0, wb = vb[o + 17] / 255.0;
        V3 d{0.0, 0.0, 0.0};
        const int codes[2] = {vb[o + 20], vb[o + 22]};
        const double ws[2] = {wa, wb};
        for (int t = 0; t < 2; t++) {
            int slot = pyfloordiv(codes[t] - 120, 3);
            if (ws[t] > 0 && slot >= 0 && slot < (int)pal.size()) {
                const V3 &dd = delta.at(pal[slot]);
                for (int i = 0; i < 3; i++) d[i] = d[i] + ws[t] * dd[i];
            }
        }
        for (int i = 0; i < 3; i++) { float f = (float)((double)rdf(&vb[o + 4 * i]) + d[i]); memcpy(&vb[o + 4 * i], &f, 4); }
    }
}
static size_t align_up(size_t n, size_t a) { return (n + a - 1) / a * a; }
static bool texture_bytes(unsigned packed, size_t &total) {
    unsigned w = 1u << ((packed >> 24) & 15), h = 1u << ((packed >> 20) & 15), mips = (packed >> 16) & 15, fmt = (packed >> 8) & 0xFF;
    total = 0;
    for (unsigned k = 0; k < (mips ? mips : 1); k++) {
        unsigned mw = (std::max)(w >> k, 1u), mh = (std::max)(h >> k, 1u);
        if (fmt == 0x0C) total += (size_t)(std::max)(mw / 4, 1u) * (std::max)(mh / 4, 1u) * 8;
        else if (fmt == 0x0E || fmt == 0x0F) total += (size_t)(std::max)(mw / 4, 1u) * (std::max)(mh / 4, 1u) * 16;
        else if (fmt == 0x86) total += (size_t)mw * mh * 4;
        else if (fmt == 0x02 || fmt == 0x82 || fmt == 0x85 || fmt == 0xA8) total += (size_t)mw * mh * 2;
        else return false;
    }
    total = align_up(total, 0x80);                                       // the whole mip chain rounded up to 0x80 (Batch 012)
    if (packed & 4) total *= 6;                                          // a cube map: six faces
    return true;
}

// swap(target, donor, jmap, remove, add, holder, refit=True, refit_from, allow_keep)
template <class REM, class ADD>
static bool part_swap(const Alr &T0, const Alr &D, const JMap &jmap, REM remove, ADD add, int holder, const JMap *refit_from,
                      bool allow_keep, Alr &out, char *why, size_t nwhy) {
    Alr T = T0;
    int tsg = body_slot(T), dsg = body_slot(D);
    if (tsg < 0 || dsg < 0) { sprintf_s(why, nwhy, "no body group"); return false; }
    int ts = T.slot[tsg], ds = D.slot[dsg];
    const Bytes tg = T.groups[ts], &dg = D.groups[ds];
    std::vector<Piece> tp, dp;
    std::vector<int> tcodes, dcodes;
    std::vector<size_t> tdescs, ddescs;
    pieces(tg, tp, tcodes, tdescs);
    pieces(dg, dp, dcodes, ddescs);
    std::vector<const Piece *> rem, adds;
    for (const Piece &p : tp) if (remove(p)) rem.push_back(&p);
    for (const Piece &p : dp) if (add(p)) adds.push_back(&p);
    if ((rem.empty() && !allow_keep) || adds.empty()) { sprintf_s(why, nwhy, "nothing to remove (%zu) or add (%zu)", rem.size(), adds.size()); return false; }
    for (const Piece *p : adds) for (int j : p->pal) if (!jmap.has(j)) { sprintf_s(why, nwhy, "a donor piece uses an unmapped joint %d", j); return false; }
    int tdesc = -1, ddesc = -1;
    for (size_t i = 0; i < tcodes.size() && tdesc < 0; i++) if (tcodes[i] == 0x11) tdesc = (int)i;
    for (size_t i = 0; i < dcodes.size() && ddesc < 0; i++) if (dcodes[i] == 0x11) ddesc = (int)i;
    if (tdesc < 0 || ddesc < 0) { sprintf_s(why, nwhy, "no skinned vertex buffer"); return false; }
    size_t td = tdescs[tdesc];
    unsigned t_n = rd32(&tg[td + 8]), t_off = rd32(&tg[td + 0x14]), d_off = rd32(&dg[ddescs[ddesc] + 0x14]);
    if ((size_t)t_off + 24ull * t_n > T.resource.size()) { sprintf_s(why, nwhy, "vertex buffer outside the file"); return false; }
    // 1. the vertex buffer + the added pieces' vertices (refitted to the target's rest pose)
    Bytes vb(T.resource.begin() + t_off, T.resource.begin() + t_off + 24 * (size_t)t_n);
    std::map<size_t, unsigned> new_base;
    std::vector<V3> tw, dw;
    if (!bind_world(tg, tw) || !bind_world(dg, dw)) { sprintf_s(why, nwhy, "no skeleton in a body group"); return false; }
    for (const Piece *p : adds) {
        new_base[p->co] = (unsigned)(vb.size() / 24);
        size_t a = d_off + 24 * (size_t)p->base, b = d_off + 24 * (size_t)(p->base + p->used);
        if (b > D.resource.size()) { sprintf_s(why, nwhy, "donor vertices outside the file"); return false; }
        vb.insert(vb.end(), D.resource.begin() + a, D.resource.begin() + b);
        std::map<int, V3> delta;
        for (int j : p->pal) {
            int src = refit_from && refit_from->has(j) ? refit_from->get(j) : j;
            int tj = jmap.get(src);
            if (tj < 0 || tj >= (int)tw.size() || src < 0 || src >= (int)dw.size()) { sprintf_s(why, nwhy, "refit joint out of range"); return false; }
            V3 d;
            for (int i = 0; i < 3; i++) d[i] = tw[tj][i] - dw[src][i];
            delta[j] = d;
        }
        refit_vertices(vb, new_base[p->co], p->used, p->pal, delta);
    }
    size_t pos = align_up(T.resource.size(), 0x80);
    T.resource.resize(pos, 0);
    T.resource.insert(T.resource.end(), vb.begin(), vb.end());
    // 2. textures + materials of the added pieces
    auto tex_list = [](const Alr &m, std::vector<Bytes> &e) {
        int n = (short)rd16(&m.tex[8]);
        e.clear();
        for (int k = 0; k < n; k++) e.push_back(Bytes(m.tex.begin() + 12 + 28 * k, m.tex.begin() + 12 + 28 * (k + 1)));
    };
    std::vector<Bytes> rtex, dtex;
    tex_list(T, rtex); tex_list(D, dtex);
    std::map<int, int> tex_map, mat_map;
    std::vector<Chunk> tch, dch;
    walk(tg, tch); walk(dg, dch);
    size_t dmat_co = 0, tmat_co = 0;
    bool fd = false, ft = false;
    for (const Chunk &c : dch) if (c.id == 1 && !fd) { dmat_co = c.off; fd = true; }
    for (const Chunk &c : tch) if (c.id == 1 && !ft) { tmat_co = c.off; ft = true; }
    if (!fd || !ft) { sprintf_s(why, nwhy, "no material chunk"); return false; }
    int tmat_count = rd16(&tg[tmat_co + 8]);
    std::vector<Bytes> new_mats;
    for (const Piece *p : adds) {
        if (mat_map.count(p->mat)) continue;
        Bytes e(dg.begin() + dmat_co + 12 + 0x4C * p->mat, dg.begin() + dmat_co + 12 + 0x4C * (p->mat + 1));
        unsigned mask = rd16(&e[0x16]);
        for (int slot = 0; slot < 4; slot++) {
            if (!((mask >> (4 * slot)) & 15)) continue;
            int tix = rd16(&e[0x18 + 2 * slot]);
            if (!tex_map.count(tix)) {
                if (tix >= (int)dtex.size()) { sprintf_s(why, nwhy, "texture index out of range"); return false; }
                Bytes ent = dtex[tix];
                unsigned off = rd32(&ent[4]), packed = rd32(&ent[12]);
                size_t nb;
                if (!texture_bytes(packed, nb) || off + nb > D.resource.size()) { sprintf_s(why, nwhy, "a donor texture cannot be copied"); return false; }
                size_t tp_ = align_up(T.resource.size(), 0x100);
                T.resource.resize(tp_, 0);
                T.resource.insert(T.resource.end(), D.resource.begin() + off, D.resource.begin() + off + nb);
                put32at(ent, 4, (unsigned)tp_);
                tex_map[tix] = (int)rtex.size();
                rtex.push_back(ent);
            }
            put16at(e, 0x18 + 2 * slot, (unsigned)tex_map[tix]);
        }
        mat_map[p->mat] = tmat_count + (int)new_mats.size();
        new_mats.push_back(e);
    }
    {   // set_tex_entries
        Bytes t;
        put32(t, 0x15); put32(t, (unsigned)(12 + 28 * rtex.size()));
        t.push_back((unsigned char)rtex.size()); t.push_back((unsigned char)(rtex.size() >> 8));
        t.push_back(T.tex[10]); t.push_back(T.tex[11]);
        for (const Bytes &e : rtex) t.insert(t.end(), e.begin(), e.end());
        T.tex = t;
    }
    // 3. the target body group, chunk by chunk
    std::set<size_t> removed;
    for (const Piece *p : rem) removed.insert(p->co);
    long long last_mesh = -1;
    for (const Chunk &c : tch) if (c.id == 2 && !removed.count(c.off)) last_mesh = (std::max)(last_mesh, (long long)c.off);
    if (last_mesh < 0) { sprintf_s(why, nwhy, "no mesh would be left"); return false; }
    Bytes og;
    for (const Chunk &c : tch) {
        if (c.id == 1) {
            Bytes cc(tg.begin() + c.off, tg.begin() + c.off + c.span);
            for (const Bytes &m : new_mats) cc.insert(cc.end(), m.begin(), m.end());
            put32at(cc, 4, (unsigned)cc.size());
            put16at(cc, 8, (unsigned)(tmat_count + new_mats.size()));
            og.insert(og.end(), cc.begin(), cc.end());
            continue;
        }
        if (c.id == 0x16) {
            Bytes cc(tg.begin() + c.off, tg.begin() + c.off + c.span);
            size_t d = 12 + 28 * (size_t)tdesc;
            put32at(cc, d + 8, (unsigned)(vb.size() / 24));
            put32at(cc, d + 0x14, (unsigned)pos);
            og.insert(og.end(), cc.begin(), cc.end());
            continue;
        }
        if (removed.count(c.off)) continue;
        og.insert(og.end(), tg.begin() + c.off, tg.begin() + c.off + c.span);
        if ((long long)c.off == last_mesh) {
            for (const Piece *p : adds) {
                Bytes cc(dg.begin() + p->co, dg.begin() + p->co + p->sp);
                put16at(cc, 0x40, (unsigned)mat_map[p->mat]);
                put16at(cc, 0x42, (unsigned)holder);
                put16at(cc, 0x44, (unsigned)tdesc);
                unsigned nb = new_base[p->co];
                put32at(cc, 0x48, nb);
                for (unsigned i = 0; i < p->cnt; i++) {
                    size_t o = 0x68 + 2 * (size_t)i;
                    put16at(cc, o, (unsigned)(rd16(&cc[o]) - p->base + nb) & 0xFFFF);
                }
                size_t po = p->pal_off - p->co;
                for (size_t k = 0; k < p->pal.size(); k++) cc[po + 4 + k] = (unsigned char)jmap.get(p->pal[k]);
                og.insert(og.end(), cc.begin(), cc.end());
            }
        }
    }
    size_t end = tch.back().off + tch.back().span;
    og.insert(og.end(), tg.begin() + end, tg.end());
    {
        std::vector<Chunk> oc;
        walk(og, oc);
        unsigned k = 0;
        for (const Chunk &c : oc) if (c.id == 2) put32at(og, c.off + 0x3C, k++);
    }
    T.groups[ts] = og;
    T.has_res = true;
    out = T;
    return true;
}

// ---------------------------------------------------------------- build_section_swap.py / build_character.py
static std::set<int> subtree(const std::vector<int> &par, int root) {
    std::set<int> out;
    std::vector<int> todo{root};
    while (!todo.empty()) {
        int j = todo.back(); todo.pop_back();
        out.insert(j);
        for (int k = 0; k < (int)par.size(); k++) if (par[k] == j && k != j) todo.push_back(k);
    }
    return out;
}
enum { RG_NONE = 0, RG_UPPER, RG_MIDDLE, RG_LOWER, RG_ARM, RG_BODY };
typedef std::vector<std::pair<int, double>> Shares;              // region -> share, in the order first met
static double py_round3(double x) { char b[64]; sprintf_s(b, "%.3f", x); return atof(b); }
template <class RF>
static void piece_shares(const Alr &m, RF region_of, std::vector<std::pair<Piece, Shares>> &out) {
    out.clear();
    int bsg = body_slot(m);
    if (bsg < 0) return;
    const Bytes &g = m.groups[m.slot[bsg]];
    std::vector<Piece> pcs;
    std::vector<int> codes;
    std::vector<size_t> descs;
    pieces(g, pcs, codes, descs);
    size_t d = 0;
    for (size_t i = 0; i < codes.size(); i++) if (codes[i] == 0x11) { d = descs[i]; break; }
    unsigned off = rd32(&g[d + 0x14]);
    const Bytes &vb = m.resource;
    for (const Piece &p : pcs) {
        std::vector<std::pair<int, long long>> tot;
        for (unsigned v = p.base; v < p.base + p.used; v++) {
            size_t o = off + 24 * (size_t)v;
            if (o + 24 > vb.size()) break;
            const int cw[2][2] = {{vb[o + 20], vb[o + 18]}, {vb[o + 22], vb[o + 17]}};
            for (int t = 0; t < 2; t++) {
                int slot = pyfloordiv(cw[t][0] - 120, 3), w = cw[t][1];
                if (w && slot >= 0 && slot < (int)p.pal.size()) {
                    int r = region_of(p.pal[slot]);
                    if (!r) r = RG_MIDDLE;
                    bool f = false;
                    for (auto &q : tot) if (q.first == r) { q.second += w; f = true; }
                    if (!f) tot.push_back({r, w});
                }
            }
        }
        long long s = 0;
        for (auto &q : tot) s += q.second;
        if (!s) s = 1;
        Shares sh;
        for (auto &q : tot) sh.push_back({q.first, py_round3((double)q.second / (double)s)});
        out.push_back({p, sh});
    }
}
static int section_of(const Shares &sh) {
    if (sh.empty()) return RG_MIDDLE;
    int best = sh[0].first; double bv = sh[0].second;
    for (auto &q : sh) if (q.second > bv) { bv = q.second; best = q.first; }
    return best;
}
static double share_of(const Shares &sh, int r) { for (auto &q : sh) if (q.first == r) return q.second; return 0.0; }

// the Player's numbering (pc00a0) mapped onto a file: npc = a model-only NPC body file (the build_npc_body route)
static bool ref_map(const Alr &R, const Alr &other, bool npc, const Alr *npc_body_file, JMap &out) {
    if (npc) { Alr tmp; char why[96]; return npc_body(R, *npc_body_file, tmp, out, why, sizeof(why)); }
    std::vector<int> jm, order;
    if (joint_map(R, other, jm, order)) { out = jmap_of(jm, order); return true; }
    std::vector<int> par;
    if (!skeleton(other, par, NULL) || !structural_map(R, par, jm, order)) return false;
    out = jmap_of(jm, order);
    return true;
}

static bool head_swap_any(const Alr &T, const Alr &D, const Alr &R, int body, int head, const Alr &body_file, Alr &out, char *why, size_t nwhy) {
    JMap jT, jD;
    if (!ref_map(R, T, body >= 11, &body_file, jT) || !ref_map(R, D, head >= 11, &D, jD) ||
        !jT.has(24) || !jT.has(70) || !jT.has(71) || !jD.has(24) || !jD.has(70) || !jD.has(71)) {
        sprintf_s(why, nwhy, "the head chain could not be matched on one of the two skeletons"); return false;
    }
    int ht = jT.get(71), nt = jT.get(70), ct = jT.get(24), hd = jD.get(71), nd = jD.get(70), cd = jD.get(24);
    JMap inv;
    for (auto &p : jD.kv) if (jT.has(p.first)) inv.set(p.second, jT.get(p.first));
    auto is_head = [](const Piece &p, int h, int n, int c) {
        if (p.pal.empty()) return false;
        bool hh = false;
        for (int j : p.pal) { if (j == h) hh = true; if (j != h && j != n && j != c) return false; }
        return hh;
    };
    int tsg = body_slot(T);
    if (tsg < 0) { sprintf_s(why, nwhy, "no body group"); return false; }
    std::vector<Piece> tp; std::vector<int> cs; std::vector<size_t> ds;
    pieces(T.groups[T.slot[tsg]], tp, cs, ds);
    const Piece *best = NULL;
    for (const Piece &p : tp) if (is_head(p, ht, nt, ct) && (!best || p.used > best->used)) best = &p;
    if (!best) { sprintf_s(why, nwhy, "this body's head is not a separate piece: it cannot be replaced"); return false; }
    int holder = best->holder;
    return part_swap(T, D, inv, [&](const Piece &p) { return is_head(p, ht, nt, ct); },
                     [&](const Piece &p) { return is_head(p, hd, nd, cd); }, holder, NULL, false, out, why, nwhy);
}

static bool section_swap_any(const Alr &T, const Alr &D, const Alr &R, int body, int donor_ch, const Alr &body_file, int section,
                             bool blend, bool no_arms, Alr &out, char *why, size_t nwhy, bool skip_main = false) {
    JMap jT, jD;
    if (!ref_map(R, T, body >= 11, &body_file, jT) || !ref_map(R, D, donor_ch >= 11, &D, jD)) {
        sprintf_s(why, nwhy, "the skeletons could not be matched"); return false;
    }
    std::vector<int> tpar, dpar;
    if (!skeleton(T, tpar, NULL) || !skeleton(D, dpar, NULL)) {   // parents_of (a model-only file: its first skeleton)
        if (tpar.empty()) skel_any(T, 0, &tpar, NULL);
        if (dpar.empty()) skel_any(D, 0, &dpar, NULL);
    }
    auto root = [&](int k) { return jT.has(k) ? jT.get(k) : -1; };
    std::set<int> up = root(70) >= 0 ? subtree(tpar, root(70)) : std::set<int>(), lo, mid;
    if (root(22) >= 0) lo.insert(root(22));
    if (root(82) >= 0) { auto s = subtree(tpar, root(82)); lo.insert(s.begin(), s.end()); }
    if (root(113) >= 0) { auto s = subtree(tpar, root(113)); lo.insert(s.begin(), s.end()); }
    if (root(23) >= 0) { auto s = subtree(tpar, root(23)); for (int j : s) if (!up.count(j)) mid.insert(j); }
    std::vector<int> treg(tpar.size(), RG_NONE);
    for (int j = 0; j < (int)tpar.size(); j++) treg[j] = up.count(j) ? RG_UPPER : lo.count(j) ? RG_LOWER : mid.count(j) ? RG_MIDDLE : RG_NONE;
    auto treg_of = [&](int j) { return j >= 0 && j < (int)treg.size() ? treg[j] : RG_NONE; };
    JMap inv;
    for (auto &p : jD.kv) if (jT.has(p.first)) inv.set(p.second, jT.get(p.first));
    auto inv_get = [&](int j) { return inv.has(j) ? inv.get(j) : INT_MIN; };
    std::vector<std::pair<Piece, Shares>> tsh, dsh;
    piece_shares(T, [&](int j) { return treg_of(j); }, tsh);
    piece_shares(D, [&](int j) { int t = inv_get(j); return t == INT_MIN ? RG_NONE : treg_of(t); }, dsh);
    std::set<size_t> rem, add;
    if (!blend) for (auto &q : tsh) if (section_of(q.second) == section) rem.insert(q.first.co);
    for (auto &q : dsh) if (section_of(q.second) == section) add.insert(q.first.co);
    if (no_arms) {
        std::set<int> arm;
        for (int j : {25, 47}) if (root(j) >= 0) { auto s = subtree(tpar, root(j)); arm.insert(s.begin(), s.end()); }
        std::vector<std::pair<Piece, Shares>> ash;
        piece_shares(D, [&](int j) { int t = inv_get(j); return t != INT_MIN && arm.count(t) ? RG_ARM : RG_BODY; }, ash);
        for (auto &q : ash) if (share_of(q.second, RG_ARM) >= 0.7) add.erase(q.first.co);
    }
    auto resolved = [&](int j) {
        int a = j, guard = 0;
        while (!inv.has(a) && a >= 0 && a < (int)dpar.size() && dpar[a] != a && guard++ < 256) a = dpar[a];
        return a;
    };
    {   // strict
        std::vector<std::pair<Piece, Shares>> rsh;
        piece_shares(D, [&](int j) { int t = inv_get(resolved(j)); return t == INT_MIN ? RG_NONE : treg_of(t); }, rsh);
        for (auto &q : rsh) if (share_of(q.second, section) < 0.5) add.erase(q.first.co);
    }
    static const char *SEC[4] = {"", "upper", "middle", "lower"};
    if (skip_main && !add.empty()) {                          // v4.9 head extras: the donor's main head piece stays behind
        const Piece *mp = NULL;
        for (auto &q : dsh) if (add.count(q.first.co) && (!mp || q.first.used > mp->used)) mp = &q.first;
        if (mp) add.erase(mp->co);
    }
    if (add.empty()) { sprintf_s(why, nwhy, "no %s pieces of that character fit this body", skip_main ? "extra head" : SEC[section]); return false; }
    int holder;
    if (body == 0) holder = section == RG_UPPER ? 18 : section == RG_MIDDLE ? 19 : 0;
    else {
        const Piece *best = NULL;
        for (auto &q : tsh) if (section_of(q.second) == section && (!best || q.first.used > best->used)) best = &q.first;
        if (!best) for (auto &q : tsh) if (!best || q.first.used > best->used) best = &q.first;
        if (!best) { sprintf_s(why, nwhy, "this body has no pieces"); return false; }
        holder = best->holder;
    }
    JMap jmap = inv, refit_from;
    int troot = -1;
    for (int k = 0; k < (int)tpar.size() && troot < 0; k++) if (tpar[k] == -1) troot = k;
    for (auto &q : dsh) {
        if (!add.count(q.first.co)) continue;
        for (int j : q.first.pal) {
            int a = resolved(j);
            if (!inv.has(a) && a < 0 && troot >= 0) { jmap.set(j, troot); refit_from.set(j, j); continue; }   // v4.9 the root
            if (!inv.has(a)) { sprintf_s(why, nwhy, "a %s piece has a bone with no match on this body", SEC[section]); return false; }
            if (a != j) { jmap.set(j, inv.get(a)); refit_from.set(j, a); }
        }
    }
    return part_swap(T, D, jmap, [&](const Piece &p) { return rem.count(p.co) > 0; }, [&](const Piece &p) { return add.count(p.co) > 0; },
                     holder, &refit_from, blend, out, why, nwhy);
}

// ---------------------------------------------------------------- the whole character (build_character.build, no theme)
struct CbSpec2 { CbSpec base; int head, mid, mstyle, low, lstyle, headx; };
static bool cb_load_any(int ch, int colour, Alr &m) {                // 0-10 players, 11-18 the NPC files
    if (ch <= 10) return cb_load(ch, colour, m);
    if (ch > 19) return false;
    wchar_t p[MAX_PATH];
    swprintf_s(p, L"%s%S.alr", g_cb_src, CB_NPCS[ch - 11]);
    Bytes b;
    return read_all(p, b) && alr_parse(b, m);
}
static bool cb_moves_on(const Alr &mv, const Alr &body, int body_ch, bool scale, Alr &out, char *why, size_t nwhy) {
    if (body_ch >= 11) { JMap jm; return npc_body(mv, body, out, jm, why, nwhy); }
    if (!moves_any(mv, body, scale, out)) { sprintf_s(why, nwhy, "these moves do not fit this body"); return false; }
    return true;
}
static bool cb_build2(const CbSpec2 &s2, Bytes &out, char *why, size_t nwhy) {
    const CbSpec &sp = s2.base;
    Alr body, base, res;
    int cur = sp.moves <= 10 ? sp.moves : sp.moves - 11;
    int want[130];
    for (int s = 0; s < 130; s++) want[s] = (sp.moves != cur && s < 129) ? sp.moves : -1;
    for (int s = 0; s < 130; s++) if (sp.from[s] >= 0) want[s] = sp.from[s];
    for (int s = 0; s < 130; s++) if (want[s] > 10 && s >= 129) want[s] = -1;
    if (!cb_load_any(sp.body, sp.body <= 10 ? sp.colour : 0, body)) { sprintf_s(why, nwhy, "body file %d missing", sp.body); return false; }
    if (!cb_load_moves(cur, base) || !cb_moves_on(base, body, sp.body, sp.size_fit != 0, res, why, nwhy)) return false;
    for (int d = 0; d <= 21; d++) {
        bool any = false;
        for (int s = 0; s < 130; s++) any |= want[s] == d && d != cur;
        if (!any) continue;
        Alr donor, tmp;
        if (!cb_load_moves(d, donor) || !cb_moves_on(donor, body, sp.body, sp.size_fit != 0, tmp, why, nwhy)) {
            sprintf_s(why, nwhy, "move source %d does not fit this body", d); return false;
        }
        for (int s = 0; s < 130; s++)
            if (want[s] == d && d != cur && res.slot[s] >= 0 && tmp.slot[s] >= 0) res.groups[res.slot[s]] = tmp.groups[tmp.slot[s]];
    }
    // the parts work on the file as written (Python: parse(build(...)))
    Bytes cur_b = alr_build(res);
    Alr ref;
    bool need_ref = (s2.head >= 0 && s2.head != sp.body) || (s2.mid >= 0 && s2.mid != sp.body) || (s2.low >= 0 && s2.low != sp.body) ||
                    (s2.headx >= 0 && s2.headx != sp.body);
    const int parts[4] = {s2.head, s2.headx, s2.mid, s2.low};
    for (int d : parts)
        if (d >= 0 && d != sp.body && !cb_part_allowed(sp.body, d)) { sprintf_s(why, nwhy, "that part is not allowed on this body (part rules)"); return false; }
    for (int s = 0; s < 130; s++)                                       // v5.10: the moves follow the same rule
        if (!cb_moves_allowed(sp.body, want[s]) || !cb_moves_allowed(sp.body, sp.moves)) {
            sprintf_s(why, nwhy, "those moves are not allowed on this body (male / female rule)"); return false;
        }
    if (need_ref && !cb_load(0, 0, ref)) { sprintf_s(why, nwhy, "pc00a0 missing"); return false; }
    if (s2.head >= 0 && s2.head != sp.body) {
        Alr T, D, o;
        if (!alr_parse(cur_b, T) || !cb_load_any(s2.head, 0, D)) { sprintf_s(why, nwhy, "head file missing"); return false; }
        // v4.9: the whole head area (section route), as build_character.py
        if (!section_swap_any(T, D, ref, sp.body, s2.head, body, RG_UPPER, false, false, o, why, nwhy)) return false;
        cur_b = alr_build(o);
    }
    if (s2.headx >= 0 && s2.headx != sp.body) {                          // v4.9 head extras (hair, hat, glasses ...)
        Alr T, D, o;
        if (!alr_parse(cur_b, T) || !cb_load_any(s2.headx, 0, D)) { sprintf_s(why, nwhy, "head extras file missing"); return false; }
        if (!section_swap_any(T, D, ref, sp.body, s2.headx, body, RG_UPPER, true, false, o, why, nwhy, true)) return false;
        cur_b = alr_build(o);
    }
    const int secs[2][3] = {{s2.mid, s2.mstyle, RG_MIDDLE}, {s2.low, s2.lstyle, RG_LOWER}};
    for (int k = 0; k < 2; k++) {
        int d = secs[k][0], style = secs[k][1];
        if (d < 0 || d == sp.body) continue;
        Alr T, D, o;
        if (!alr_parse(cur_b, T) || !cb_load_any(d, 0, D)) { sprintf_s(why, nwhy, "part file missing"); return false; }
        if (!section_swap_any(T, D, ref, sp.body, d, body, secs[k][2], style >= 1, style == 2, o, why, nwhy)) return false;
        cur_b = alr_build(o);
    }
    out = cur_b;
    return true;
}
