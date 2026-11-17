// ---- v3.7 Create a Character, live preview: the character file built inside the loader (owner 2026-10-24). A C++ port of our own Python tools
// (13_Independent_Research_Tools/alr_transplant.py parse/build, build_body_swap.py joint_map/build/rescale_positions,
// build_character.py per-slot animations); the self-test checks it byte for byte against files those tools wrote.
// Facts (from those tools' docs, CONFIRMED there): ALR = header {u32 0x11, u32 header span, u16 v0, u16 v1, u32 resource
// offset, u32 group count, u32 resource span, 8 zero} + i32 group offsets; the texture table chunk 0x15 follows the header;
// groups are packed in ascending offset order, each a chunk list {u32 id, u32 span} ending with {0, 8}; then zero padding up
// to the resource offset, the resource buffer, trailing bytes. A player file = 130 animation groups (chunk 5 tracks:
// +8 f32 frames, +12 u16 joint, +16 u32 position keys, +30 u8 key stride, keys at +32) + model groups; skeleton chunk 3:
// u16 count at +8, 64-byte joints from +12 (+4 3 x f32 bind offset, +46 i16 parent).
#include <vector>
#include <algorithm>
#include <cmath>
typedef std::vector<unsigned char> Bytes;

struct Alr {
    unsigned short v0, v1;
    unsigned span;
    std::vector<int> raw;                     // the group offsets as read (absent entries keep their raw value)
    std::vector<int> slot;                    // group index -> unique group, -1 absent
    std::vector<Bytes> groups;
    Bytes tex, resource, trailing;
    bool has_res;
};

static unsigned rd32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }
static unsigned short rd16(const unsigned char *p) { return (unsigned short)(p[0] | p[1] << 8); }
static void put32(Bytes &b, unsigned v) { b.push_back((unsigned char)v); b.push_back((unsigned char)(v >> 8)); b.push_back((unsigned char)(v >> 16)); b.push_back((unsigned char)(v >> 24)); }

struct Chunk { unsigned id, off, span; };
static bool walk(const Bytes &g, std::vector<Chunk> &out) {            // chunks of one group (alr_transplant.walk)
    out.clear();
    for (size_t i = 0;;) {
        if (i + 8 > g.size()) return false;
        unsigned id = rd32(&g[i]), sp = rd32(&g[i + 4]);
        if (id == 0) return true;
        if (sp < 8 || i + sp > g.size()) return false;
        out.push_back({id, (unsigned)i, sp});
        i += sp;
    }
}

static bool alr_parse(const Bytes &b, Alr &m) {
    if (b.size() < 0x20 || rd32(&b[0]) != 0x11) return false;
    unsigned hs = rd32(&b[4]), ro = rd32(&b[0x0C]), cnt = rd32(&b[0x10]), rspan = rd32(&b[0x14]);
    if (0x20 + 4ull * cnt > b.size() || hs >= b.size() || b[hs] != 0x15) return false;
    m.v0 = rd16(&b[8]); m.v1 = rd16(&b[10]); m.span = rspan;
    m.raw.resize(cnt);
    for (unsigned k = 0; k < cnt; k++) m.raw[k] = (int)rd32(&b[0x20 + 4 * k]);
    unsigned tspan = rd32(&b[hs + 4]);
    m.tex.assign(b.begin() + hs, b.begin() + hs + tspan);
    std::vector<int> present;
    for (int o : m.raw) if (o > 0) present.push_back(o);
    std::sort(present.begin(), present.end());
    present.erase(std::unique(present.begin(), present.end()), present.end());
    size_t limit = ro ? ro : b.size(), prev = hs + tspan;
    m.groups.clear();
    for (size_t k = 0; k < present.size(); k++) {
        size_t o = present[k];
        if (o != prev) return false;
        size_t nxt = k + 1 < present.size() ? (size_t)present[k + 1] : limit, i = o;
        for (;;) {                                                      // alr_chunks.walk_group
            if (i + 8 > nxt) return false;
            unsigned id = rd32(&b[i]), sp = rd32(&b[i + 4]);
            if (id == 0) { if (sp != 8) return false; i += 8; break; }
            if (sp < 8 || i + sp > nxt) return false;
            i += sp;
        }
        if (k + 1 < present.size() && i != nxt) return false;
        m.groups.push_back(Bytes(b.begin() + o, b.begin() + i));
        prev = i;
    }
    m.has_res = ro != 0;
    if (ro) {
        for (size_t i = prev; i < ro; i++) if (b[i]) return false;
        if ((size_t)ro + rspan > b.size()) return false;
        m.resource.assign(b.begin() + ro, b.begin() + ro + rspan);
        m.trailing.assign(b.begin() + ro + rspan, b.end());
    } else {
        m.resource.clear();
        m.trailing.assign(b.begin() + prev, b.end());
    }
    m.slot.resize(cnt);
    for (unsigned k = 0; k < cnt; k++) {
        int o = m.raw[k];
        m.slot[k] = o > 0 ? (int)(std::lower_bound(present.begin(), present.end(), o) - present.begin()) : -1;
    }
    return true;
}

// alr_transplant.build(m, keep_resource_offset=False) with resource_dirty: the resource offset = the group end rounded up to 0x1000
static Bytes alr_build(const Alr &m) {
    size_t n = m.slot.size(), hs = 0x20 + 4 * n, pos = hs + m.tex.size();
    std::vector<size_t> starts;
    for (const Bytes &g : m.groups) { starts.push_back(pos); pos += g.size(); }
    size_t ro = (m.has_res || !m.resource.empty()) ? (pos + 0xFFF) / 0x1000 * 0x1000 : 0;
    Bytes out;
    out.reserve(ro + m.resource.size() + m.trailing.size() + 16);
    put32(out, 0x11); put32(out, (unsigned)hs);
    out.push_back((unsigned char)m.v0); out.push_back((unsigned char)(m.v0 >> 8));
    out.push_back((unsigned char)m.v1); out.push_back((unsigned char)(m.v1 >> 8));
    put32(out, (unsigned)ro); put32(out, (unsigned)n); put32(out, (unsigned)m.resource.size());
    for (int i = 0; i < 8; i++) out.push_back(0);
    for (size_t k = 0; k < n; k++) put32(out, m.slot[k] >= 0 ? (unsigned)starts[m.slot[k]] : (unsigned)m.raw[k]);
    out.insert(out.end(), m.tex.begin(), m.tex.end());
    for (const Bytes &g : m.groups) out.insert(out.end(), g.begin(), g.end());
    if (ro) { out.resize(ro, 0); out.insert(out.end(), m.resource.begin(), m.resource.end()); }
    out.insert(out.end(), m.trailing.begin(), m.trailing.end());
    return out;
}

static int anim_count(const Alr &m) {                                   // leading groups made only of chunk-5 tracks
    std::vector<Chunk> ch;
    int n = 0;
    for (int s : m.slot) {
        if (s < 0 || !walk(m.groups[s], ch)) break;
        bool all5 = true;
        for (const Chunk &c : ch) all5 &= c.id == 5;
        if (!all5) break;
        n++;
    }
    return n;
}

static bool skeleton(const Alr &m, std::vector<int> &parents, std::vector<double> *lens) {
    int na = anim_count(m);
    std::vector<Chunk> ch;
    for (size_t gi = 0; gi < m.slot.size(); gi++) {
        int s = m.slot[gi];
        if (s < 0 || (int)gi < na || !walk(m.groups[s], ch)) continue;
        for (const Chunk &c : ch) {
            if (c.id != 3) continue;
            const unsigned char *g = &m.groups[s][c.off];
            int n = rd16(g + 8);
            parents.resize(n);
            if (lens) lens->resize(n);
            for (int k = 0; k < n; k++) {
                parents[k] = (short)rd16(g + 12 + 64 * k + 46);
                if (lens) {
                    double sum = 0;
                    for (int c3 = 0; c3 < 3; c3++) { float f; memcpy(&f, g + 12 + 64 * k + 4 + 4 * c3, 4); sum += (double)f * (double)f; }
                    (*lens)[k] = pow(sum, 0.5);                         // Python's ** 0.5
                }
            }
            return true;
        }
    }
    return false;
}

static bool animated(const Alr &m, std::vector<char> &used) {
    std::vector<Chunk> ch;
    int na = anim_count(m);
    for (int gi = 0; gi < na; gi++) {
        int s = m.slot[gi];
        if (s < 0) continue;
        if (!walk(m.groups[s], ch)) return false;
        for (const Chunk &c : ch) {
            int j = rd16(&m.groups[s][c.off + 12]);
            if (j >= (int)used.size()) used.resize(j + 1, 0);
            used[j] = 1;
        }
    }
    return true;
}

struct WalkNode { int joint, depth, kids; };
static void pruned_order(const std::vector<int> &parents, const std::vector<char> &used, std::vector<WalkNode> &out) {
    std::vector<char> keep(parents.size(), 0);
    for (size_t k = 0; k < used.size(); k++) {
        if (!used[k]) continue;
        int j = (int)k;
        while (j != -1 && j < (int)parents.size() && !keep[j]) { keep[j] = 1; j = parents[j]; }
    }
    std::vector<std::vector<int>> kids(parents.size() + 1);             // [0] = roots (parent -1)
    for (size_t k = 0; k < parents.size(); k++) if (keep[k]) kids[parents[k] + 1].push_back((int)k);
    out.clear();
    struct R { static void go(int k, int d, std::vector<std::vector<int>> &kids, std::vector<WalkNode> &out) {
        out.push_back({k, d, (int)kids[k + 1].size()});
        for (int c : kids[k + 1]) go(c, d + 1, kids, out);
    } };
    for (int r : kids[0]) R::go(r, 0, kids, out);
}

// joint_map + the order the Python dict keeps (insertion = walk order), for the rescale sums
static bool joint_map(const Alr &mv, const Alr &body, std::vector<int> &jm, std::vector<int> &order) {
    std::vector<int> pm, pb;
    std::vector<char> um, ub;
    if (!skeleton(mv, pm, NULL) || !skeleton(body, pb, NULL) || !animated(mv, um) || !animated(body, ub)) return false;
    std::vector<WalkNode> om, ob;
    pruned_order(pm, um, om);
    pruned_order(pb, ub, ob);
    if (om.size() != ob.size()) return false;
    for (size_t i = 0; i < om.size(); i++) if (om[i].depth != ob[i].depth || om[i].kids != ob[i].kids) return false;
    jm.assign(pm.size(), -1);
    order.clear();
    for (size_t i = 0; i < om.size(); i++) { jm[om[i].joint] = ob[i].joint; order.push_back(om[i].joint); }
    return true;
}

static void rescale(Bytes &g, const std::vector<int> &jm, const std::vector<int> &order, const std::vector<double> &lm,
                    const std::vector<double> &lb) {
    double sy = 0, sx = 0;                                              // Python: sum(y) / sum(x) in dict order
    for (int a : order) { int b = jm[a]; if (lm[a] > 1e-4 && lb[b] > 1e-4) { sx += lm[a]; sy += lb[b]; } }
    double overall = sy / sx;
    std::vector<Chunk> ch;
    walk(g, ch);
    for (const Chunk &c : ch) {
        int j = rd16(&g[c.off + 12]);
        unsigned n1 = rd32(&g[c.off + 16]), stride = g[c.off + 30];
        if (!n1) continue;
        int b = jm[j];
        double r = (lm[j] > 1e-4 && lb[b] > 1e-4) ? lb[b] / lm[j] : overall;
        for (unsigned k = 0; k < n1; k++)
            for (unsigned c4 = 1; c4 < stride / 4; c4++) {
                size_t o = c.off + 32 + (size_t)stride * k + 4 * c4;
                float f; memcpy(&f, &g[o], 4);
                float v = (float)((double)f * r);
                memcpy(&g[o], &v, 4);
            }
    }
}

// build_body_swap.build: the moves' 130 animation groups (joint numbers translated, positions rescaled when asked) in the body
static bool moves_on_body(const Alr &mv, const Alr &body_in, bool scale, Alr &out) {
    std::vector<int> jm, order;
    if (!joint_map(mv, body_in, jm, order)) return false;
    std::vector<int> pp;
    std::vector<double> lm, lb;
    if (scale) { skeleton(mv, pp, &lm); skeleton(body_in, pp, &lb); }
    out = body_in;
    std::vector<Chunk> ch;
    for (int gi = 0; gi < 130; gi++) {
        if ((mv.slot[gi] < 0) != (out.slot[gi] < 0)) return false;
        if (mv.slot[gi] < 0) continue;
        Bytes g = mv.groups[mv.slot[gi]];
        if (scale) rescale(g, jm, order, lm, lb);
        walk(g, ch);
        for (const Chunk &c : ch) {
            int j = rd16(&g[c.off + 12]);
            if (j >= (int)jm.size() || jm[j] < 0) return false;
            g[c.off + 12] = (unsigned char)jm[j]; g[c.off + 13] = (unsigned char)(jm[j] >> 8);
        }
        out.groups[out.slot[gi]] = g;
    }
    out.has_res = out.has_res || !out.resource.empty();
    return true;
}

// build_body_swap.structural_map: the moves' animated tree placed in the body skeleton by shape (children in order onto a
// subsequence of the body joint's children, backtracking); exactly one placement must exist. The pairs keep the order the
// Python dict gets them in (the rescale sums use it).
typedef std::vector<std::pair<int, int>> JPairs;
struct ShapeCtx { std::vector<std::vector<int>> kn, kb; };
static bool sm_match(const ShapeCtx &c, int a, int b, JPairs mp, JPairs &out);
static bool sm_rec(const ShapeCtx &c, const std::vector<int> &ca, const std::vector<int> &cb, size_t i, size_t j, const JPairs &cur, JPairs &out) {
    if (i == ca.size()) { out = cur; return true; }
    for (size_t jj = j; jj < cb.size(); jj++) {
        JPairs r;
        if (sm_match(c, ca[i], cb[jj], cur, r)) {
            JPairs r2;
            if (sm_rec(c, ca, cb, i + 1, jj + 1, r, r2)) { out = r2; return true; }
        }
    }
    return false;
}
static bool sm_match(const ShapeCtx &c, int a, int b, JPairs mp, JPairs &out) {
    mp.push_back({a, b});
    return sm_rec(c, c.kn[a + 1], c.kb[b + 1], 0, 0, mp, out);
}
static bool structural_map(const Alr &mv, const std::vector<int> &bpar, std::vector<int> &jm, std::vector<int> &order) {
    std::vector<int> pm;
    std::vector<char> used;
    if (!skeleton(mv, pm, NULL) || !animated(mv, used)) return false;
    std::vector<char> keep(pm.size(), 0);
    for (size_t k = 0; k < used.size(); k++) {
        if (!used[k]) continue;
        int j = (int)k;
        while (j != -1 && j < (int)pm.size() && !keep[j]) { keep[j] = 1; j = pm[j]; }
    }
    ShapeCtx c;
    c.kn.resize(pm.size() + 1); c.kb.resize(bpar.size() + 1);
    for (size_t k = 0; k < pm.size(); k++) if (keep[k]) c.kn[pm[k] + 1].push_back((int)k);
    for (size_t k = 0; k < bpar.size(); k++) c.kb[bpar[k] + 1].push_back((int)k);
    if (c.kn[0].size() != 1) return false;
    int sols = 0;
    JPairs sol;
    for (size_t b = 0; b < bpar.size(); b++) {
        JPairs r;
        if (sm_match(c, c.kn[0][0], (int)b, JPairs(), r)) { if (++sols == 1) sol = r; }
    }
    if (sols != 1) return false;
    jm.assign(pm.size(), -1);
    order.clear();
    for (auto &p : sol) { jm[p.first] = p.second; order.push_back(p.first); }
    return true;
}

// the moves on the body, strict joint match first, else the shape match (build_character.moves_on / shape_build)
static bool moves_any(const Alr &mv, const Alr &body, bool scale, Alr &out) {
    if (moves_on_body(mv, body, scale, out)) return true;
    std::vector<int> jm, order, bpar;
    if (!skeleton(body, bpar, NULL) || !structural_map(mv, bpar, jm, order)) return false;
    std::vector<int> pp;
    std::vector<double> lm, lb;
    if (scale) { skeleton(mv, pp, &lm); skeleton(body, pp, &lb); }
    out = body;
    std::vector<Chunk> ch;
    int n = (std::min)(anim_count(mv), 130);
    for (int gi = 0; gi < n; gi++) {
        if (mv.slot[gi] < 0 || out.slot[gi] < 0) continue;
        Bytes g = mv.groups[mv.slot[gi]];
        walk(g, ch);
        for (const Chunk &c : ch) { int j = rd16(&g[c.off + 12]); if (j >= (int)jm.size() || jm[j] < 0) return false; }   // complete maps only
        if (scale) rescale(g, jm, order, lm, lb);
        for (const Chunk &c : ch) {
            int j = rd16(&g[c.off + 12]);
            g[c.off + 12] = (unsigned char)jm[j]; g[c.off + 13] = (unsigned char)(jm[j] >> 8);
        }
        out.groups[out.slot[gi]] = g;
    }
    out.has_res = out.has_res || !out.resource.empty();
    return true;
}

// build_character.build for player-family bodies (no head, no theme): move sources 0-10 = the players' current moves,
// 11-21 = their OG files (129 animations; the 130th stays the current one's)
struct CbSpec { int body, colour, moves, size_fit, from[130]; };        // from[slot] = move source or -1 (base)
static bool read_all(const wchar_t *path, Bytes &b) {
    FILE *f = NULL;
    if (cwo_wfopen(&f, path, L"rb") != 0 || !f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    b.resize(n > 0 ? n : 0);
    bool ok = n > 0 && fread(b.data(), 1, n, f) == (size_t)n;
    fclose(f);
    return ok;
}
static const wchar_t *g_cb_src;                                        // folder holding the clean pcNNa<k>.alr / pcNNa.alr copies
static bool cb_load(int ch, int colour, Alr &m) {
    wchar_t p[MAX_PATH];
    swprintf_s(p, L"%spc%02da%d.alr", g_cb_src, ch, colour);
    Bytes b;
    if (!read_all(p, b)) { swprintf_s(p, L"%spc%02da0.alr", g_cb_src, ch); if (!read_all(p, b)) return false; }
    return alr_parse(b, m);
}
static bool cb_load_moves(int d, Alr &m) {
    if (d <= 10) return cb_load(d, 0, m);
    wchar_t p[MAX_PATH];
    swprintf_s(p, L"%spc%02da.alr", g_cb_src, d - 11);
    Bytes b;
    return read_all(p, b) && alr_parse(b, m);
}
static bool cb_build(const CbSpec &sp, Bytes &out, char *why, size_t nwhy) {
    Alr body, base, res;
    int cur = sp.moves <= 10 ? sp.moves : sp.moves - 11;
    int want[130];
    for (int s = 0; s < 130; s++) want[s] = (sp.moves != cur && s < 129) ? sp.moves : -1;
    for (int s = 0; s < 130; s++) if (sp.from[s] >= 0) want[s] = sp.from[s];   // groups and single ones, already resolved
    for (int s = 0; s < 130; s++) if (want[s] > 10 && s >= 129) want[s] = -1;
    if (!cb_load(sp.body, sp.colour, body)) { sprintf_s(why, nwhy, "body file pc%02da missing", sp.body); return false; }
    if (!cb_load_moves(cur, base) || !moves_any(base, body, sp.size_fit != 0, res)) {
        sprintf_s(why, nwhy, "these moves do not fit this body"); return false;
    }
    for (int d = 0; d <= 21; d++) {                                     // each other source, in order (Python: sorted)
        bool any = false;
        for (int s = 0; s < 130; s++) any |= want[s] == d && d != cur;
        if (!any) continue;
        Alr donor, tmp;
        if (!cb_load_moves(d, donor) || !moves_any(donor, body, sp.size_fit != 0, tmp)) {
            sprintf_s(why, nwhy, "move source %d does not fit this body", d); return false;
        }
        for (int s = 0; s < 130; s++)
            if (want[s] == d && d != cur && res.slot[s] >= 0 && tmp.slot[s] >= 0) res.groups[res.slot[s]] = tmp.groups[tmp.slot[s]];
    }
    out = alr_build(res);
    return true;
}
