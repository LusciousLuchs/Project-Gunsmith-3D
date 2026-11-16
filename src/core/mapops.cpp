// Gunsmith3D - what the level editor does to a map (see mapops.hpp).
#include "mapops.hpp"
#include "json.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <map>
#include <set>

namespace gmap {
namespace {
const double PI = 3.14159265358979323846;
double comp(V3 v, int a) { return a == 0 ? v.x : a == 1 ? v.y : v.z; }
void setComp(V3& v, int a, double x) { (a == 0 ? v.x : a == 1 ? v.y : v.z) = x; }
V3 mul(const Mat3& a, V3 v) { return {a.m[0] * v.x + a.m[1] * v.y + a.m[2] * v.z, a.m[3] * v.x + a.m[4] * v.y + a.m[5] * v.z, a.m[6] * v.x + a.m[7] * v.y + a.m[8] * v.z}; }
Face mkface(V3 n, double d, int mat) { Face f; f.n = n; f.d = d; f.mat = mat; return f; }
bool visibleBrush(const Brush& b) { return !b.hidden; }
}  // namespace

Mat3 rotationAbout(int axis, double r) {
    double c = std::cos(r), s = std::sin(r);
    Mat3 a;
    if (axis == 0) { double v[9] = {1, 0, 0, 0, c, -s, 0, s, c}; std::copy(v, v + 9, a.m); }
    else if (axis == 1) { double v[9] = {c, 0, s, 0, 1, 0, -s, 0, c}; std::copy(v, v + 9, a.m); }
    else { double v[9] = {c, -s, 0, s, c, 0, 0, 0, 1}; std::copy(v, v + 9, a.m); }
    return a;
}
Mat3 scalingBy(V3 s) { Mat3 a; a.m[0] = s.x; a.m[4] = s.y; a.m[8] = s.z; return a; }
V3 apply(const Mat3& a, V3 v) { return mul(a, v); }

bool transform(Brush& b, const Mat3& a, V3 pivot, V3 move) {
    const double* m = a.m;
    double det = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6]);
    if (std::fabs(det) < 1e-9) return false;
    // the inverse's transpose turns a plane's normal
    double inv[9] = {(m[4] * m[8] - m[5] * m[7]) / det, (m[2] * m[7] - m[1] * m[8]) / det, (m[1] * m[5] - m[2] * m[4]) / det,
                     (m[5] * m[6] - m[3] * m[8]) / det, (m[0] * m[8] - m[2] * m[6]) / det, (m[2] * m[3] - m[0] * m[5]) / det,
                     (m[3] * m[7] - m[4] * m[6]) / det, (m[1] * m[6] - m[0] * m[7]) / det, (m[0] * m[4] - m[1] * m[3]) / det};
    Mat3 invT;
    for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) invT.m[r * 3 + c] = inv[c * 3 + r];
    for (auto& f : b.faces) {
        V3 q0 = mul(a, f.n * f.d - pivot) + pivot + move;
        V3 n2 = norm(mul(invT, f.n));
        f.d = dot(n2, q0);
        f.n = n2;
    }
    return true;
}
bool scaleBrush(Brush& b, V3 f, V3 pivot) {
    if (f.x <= 1e-6 || f.y <= 1e-6 || f.z <= 1e-6) return false;
    return transform(b, scalingBy(f), pivot);
}
void rotateAbout(Brush& b, int axis, double r, V3 pivot) { transform(b, rotationAbout(axis, r), pivot); }
void mirrorAbout(Brush& b, int axis, V3 pivot) { V3 s{1, 1, 1}; setComp(s, axis, -1); transform(b, scalingBy(s), pivot); }

// ---------------------------------------------------------------- selections
void selectionBounds(const Map& m, const Selection& s, V3& mn, V3& mx) {
    mn = {1e30, 1e30, 1e30}; mx = {-1e30, -1e30, -1e30};
    auto add = [&](V3 a, V3 b) { mn = {std::min(mn.x, a.x), std::min(mn.y, a.y), std::min(mn.z, a.z)}; mx = {std::max(mx.x, b.x), std::max(mx.y, b.y), std::max(mx.z, b.z)}; };
    for (int i : s.brushes) if (i >= 0 && i < (int)m.brushes.size()) { V3 a, b; brushBounds(m.brushes[i], a, b); add(a, b); }
    for (int i : s.ents) if (i >= 0 && i < (int)m.ents.size()) add(m.ents[i].pos, m.ents[i].pos);
    if (mn.x > mx.x) mn = mx = {0, 0, 0};
}
V3 selectionCentre(const Map& m, const Selection& s) { V3 a, b; selectionBounds(m, s, a, b); return (a + b) * 0.5; }

void transformSelection(Map& m, const Selection& s, const Mat3& a, V3 pivot, V3 move, double yawChange) {
    for (int i : s.brushes) if (i >= 0 && i < (int)m.brushes.size()) transform(m.brushes[i], a, pivot, move);
    for (int i : s.ents) if (i >= 0 && i < (int)m.ents.size()) {
        Entity& e = m.ents[i];
        e.pos = mul(a, e.pos - pivot) + pivot + move;
        e.yaw = std::fmod(e.yaw + yawChange + 7200, 360);
    }
}
void rotateSelection(Map& m, const Selection& s, int axis, double r, V3 pivot) { transformSelection(m, s, rotationAbout(axis, r), pivot, {}, axis == 1 ? -r * 180 / PI : 0); }
bool scaleSelection(Map& m, const Selection& s, V3 f, V3 pivot) {
    if (f.x <= 1e-6 || f.y <= 1e-6 || f.z <= 1e-6) return false;
    transformSelection(m, s, scalingBy(f), pivot, {}, 0);
    return true;
}
void moveSelection(Map& m, const Selection& s, V3 d) { transformSelection(m, s, Mat3{}, {}, d, 0); }
void mirrorSelection(Map& m, const Selection& s, int axis, V3 pivot) {
    V3 f{1, 1, 1}; setComp(f, axis, -1);
    for (int i : s.brushes) if (i >= 0 && i < (int)m.brushes.size()) transform(m.brushes[i], scalingBy(f), pivot);
    for (int i : s.ents) if (i >= 0 && i < (int)m.ents.size()) {
        Entity& e = m.ents[i];
        e.pos = mul(scalingBy(f), e.pos - pivot) + pivot;
        if (axis == 0) e.yaw = std::fmod(540 - e.yaw + 720, 360);   // facing (cos, sin): X flips the cosine
        else if (axis == 2) e.yaw = std::fmod(360 - e.yaw, 360);
    }
}

// ---------------------------------------------------------------- corners
std::vector<V3> brushVertices(const Brush& b) {
    std::vector<V3> out;
    for (size_t i = 0; i < b.faces.size(); i++)
        for (auto& p : facePolygon(b, (int)i)) {
            bool seen = false;
            for (auto& q : out) if (len(p - q) < 1e-5) { seen = true; break; }
            if (!seen) out.push_back(p);
        }
    return out;
}
std::vector<std::pair<int, int>> brushEdges(const Brush& b, const std::vector<V3>& verts) {
    std::set<std::pair<int, int>> es;
    auto idx = [&](V3 p) { for (size_t i = 0; i < verts.size(); i++) if (len(p - verts[i]) < 1e-5) return (int)i; return -1; };
    for (size_t i = 0; i < b.faces.size(); i++) {
        auto poly = facePolygon(b, (int)i);
        for (size_t k = 0; k < poly.size(); k++) {
            int a = idx(poly[k]), c = idx(poly[(k + 1) % poly.size()]);
            if (a >= 0 && c >= 0 && a != c) es.insert({std::min(a, c), std::max(a, c)});
        }
    }
    return {es.begin(), es.end()};
}
bool brushFromPoints(const std::vector<V3>& in, const Brush& like, Brush& out) {
    std::vector<V3> P;
    for (auto& p : in) { bool seen = false; for (auto& q : P) if (len(p - q) < 1e-6) { seen = true; break; } if (!seen) P.push_back(p); }
    if (P.size() < 4) return false;
    struct Pl { V3 n; double d; };
    std::vector<Pl> planes;
    const double eps = 1e-7;
    for (size_t i = 0; i < P.size(); i++) for (size_t j = i + 1; j < P.size(); j++) for (size_t k = j + 1; k < P.size(); k++) {
        V3 n = cross(P[j] - P[i], P[k] - P[i]);
        double l = len(n);
        if (l < 1e-9) continue;
        n = n * (1 / l);
        double d = dot(n, P[i]), lo = 1e30, hi = -1e30;
        for (auto& p : P) { double s = dot(n, p) - d; lo = std::min(lo, s); hi = std::max(hi, s); }
        if (hi > eps && lo < -eps) continue;                  // points on both sides: not a face
        if (hi > eps) { n = -n; d = -d; }                     // all on the far side: flip so they're behind it
        bool dup = false;
        for (auto& q : planes) if (dot(n, q.n) > 1 - 1e-9 && std::fabs(d - q.d) < 1e-7) { dup = true; break; }
        if (!dup) planes.push_back({n, d});
    }
    if (planes.size() < 4) return false;
    Brush b;
    for (auto& p : planes) {
        Face f;
        f.n = p.n; f.d = p.d;
        double best = -2;
        for (auto& lf : like.faces) { double s = dot(lf.n, p.n); if (s > best) { best = s; f.mat = lf.mat; f.scale = lf.scale; f.shiftU = lf.shiftU; f.shiftV = lf.shiftV; } }
        b.faces.push_back(f);
    }
    b.name = like.name; b.group = like.group; b.frozen = like.frozen; b.hidden = like.hidden; b.locked = like.locked;
    if (!brushValid(b)) return false;
    out = b;
    return true;
}
bool moveVertices(const Brush& b, const std::vector<int>& which, V3 delta, Brush& out) {
    auto v = brushVertices(b);
    bool any = false;
    for (int i : which) if (i >= 0 && i < (int)v.size()) { v[i] += delta; any = true; }
    return any && brushFromPoints(v, b, out);
}
bool extrudeFace(const Brush& b, int face, double amount, Brush& out) {
    if (face < 0 || face >= (int)b.faces.size() || amount <= 1e-6) return false;
    auto poly = facePolygon(b, face);
    if (poly.size() < 3) return false;
    std::vector<V3> pts = poly;
    for (auto& p : poly) pts.push_back(p + b.faces[face].n * amount);
    return brushFromPoints(pts, b, out);
}

// ---------------------------------------------------------------- snapping
namespace {
void snapCandidates(const Brush& b, V3 p, double radius, std::vector<V3>& out) {
    V3 mn, mx; brushBounds(b, mn, mx);
    if (p.x < mn.x - radius || p.x > mx.x + radius || p.y < mn.y - radius || p.y > mx.y + radius || p.z < mn.z - radius || p.z > mx.z + radius) return;
    auto v = brushVertices(b);
    for (auto& q : v) out.push_back(q);
    for (auto& e : brushEdges(b, v)) out.push_back((v[e.first] + v[e.second]) * 0.5);
    for (size_t i = 0; i < b.faces.size(); i++) {
        auto poly = facePolygon(b, (int)i);
        if (poly.empty()) continue;
        V3 c; for (auto& q : poly) c += q;
        out.push_back(c * (1.0 / poly.size()));
    }
}
bool inSel(const std::vector<int>& v, int i) { return std::find(v.begin(), v.end(), i) != v.end(); }
}  // namespace

SnapHit snapPoint(const Map& m, const Selection& skip, V3 p, double radius) {
    SnapHit h;
    double best = radius;
    std::vector<V3> c;
    for (size_t i = 0; i < m.brushes.size(); i++) {
        if (inSel(skip.brushes, (int)i) || !visibleBrush(m.brushes[i])) continue;
        c.clear();
        snapCandidates(m.brushes[i], p, radius, c);
        for (auto& q : c) { double d = len(q - p); if (d < best) { best = d; h.hit = true; h.to = q; } }
    }
    for (size_t i = 0; i < m.ents.size(); i++) {
        if (inSel(skip.ents, (int)i) || m.ents[i].hidden) continue;
        double d = len(m.ents[i].pos - p);
        if (d < best) { best = d; h.hit = true; h.to = m.ents[i].pos; }
    }
    h.from = p;
    h.delta = h.hit ? h.to - p : V3{};
    return h;
}
SnapHit snapMove(const Map& m, const Selection& s, V3 delta, double radius) {
    SnapHit best;
    best.delta = delta;
    double bd = radius;
    std::vector<V3> pts;
    V3 mn, mx; selectionBounds(m, s, mn, mx);
    for (int x = 0; x < 2; x++) for (int y = 0; y < 2; y++) for (int z = 0; z < 2; z++) pts.push_back({x ? mx.x : mn.x, y ? mx.y : mn.y, z ? mx.z : mn.z});
    pts.push_back((mn + mx) * 0.5);
    for (int i : s.brushes) if (i >= 0 && i < (int)m.brushes.size() && pts.size() < 300) for (auto& v : brushVertices(m.brushes[i])) pts.push_back(v);
    for (int i : s.ents) if (i >= 0 && i < (int)m.ents.size()) pts.push_back(m.ents[i].pos);
    for (auto& p : pts) {
        SnapHit h = snapPoint(m, s, p + delta, bd);
        if (!h.hit) continue;
        double d = len(h.to - (p + delta));
        if (d < bd) { bd = d; best.hit = true; best.from = p + delta; best.to = h.to; best.delta = delta + (h.to - (p + delta)); }
    }
    return best;
}

// ---------------------------------------------------------------- aligning and spacing
namespace {
struct Obj { bool brush; int i; double lo, hi; };
std::vector<Obj> objects(const Map& m, const Selection& s, int axis) {
    std::vector<Obj> v;
    for (int i : s.brushes) if (i >= 0 && i < (int)m.brushes.size()) { V3 a, b; brushBounds(m.brushes[i], a, b); v.push_back({true, i, comp(a, axis), comp(b, axis)}); }
    for (int i : s.ents) if (i >= 0 && i < (int)m.ents.size()) { double p = comp(m.ents[i].pos, axis); v.push_back({false, i, p, p}); }
    return v;
}
void shift(Map& m, const Obj& o, int axis, double d) {
    V3 mv; setComp(mv, axis, d);
    if (o.brush) translate(m.brushes[o.i], mv); else m.ents[o.i].pos += mv;
}
}  // namespace
void alignSelection(Map& m, const Selection& s, int axis, AlignTo to) {
    auto v = objects(m, s, axis);
    if (v.size() < 2) return;
    double lo = 1e30, hi = -1e30;
    for (auto& o : v) { lo = std::min(lo, o.lo); hi = std::max(hi, o.hi); }
    for (auto& o : v) {
        double d = to == AlignTo::Low ? lo - o.lo : to == AlignTo::High ? hi - o.hi : (lo + hi) / 2 - (o.lo + o.hi) / 2;
        shift(m, o, axis, d);
    }
}
void distributeSelection(Map& m, const Selection& s, int axis) {
    auto v = objects(m, s, axis);
    if (v.size() < 3) return;
    std::sort(v.begin(), v.end(), [](const Obj& a, const Obj& b) { return a.lo + a.hi < b.lo + b.hi; });
    double c0 = (v.front().lo + v.front().hi) / 2, c1 = (v.back().lo + v.back().hi) / 2;
    for (size_t k = 1; k + 1 < v.size(); k++) {
        double want = c0 + (c1 - c0) * (double)k / (double)(v.size() - 1);
        shift(m, v[k], axis, want - (v[k].lo + v[k].hi) / 2);
    }
}

// ---------------------------------------------------------------- pieces and arrays
Piece copyPiece(const Map& m, const Selection& s) {
    Piece p;
    for (int i : s.brushes) if (i >= 0 && i < (int)m.brushes.size()) p.brushes.push_back(m.brushes[i]);
    for (int i : s.ents) if (i >= 0 && i < (int)m.ents.size()) p.ents.push_back(m.ents[i]);
    return p;
}
Selection pastePiece(Map& m, const Piece& p, V3 move) {
    Selection s;
    int nextGroup = 1;
    for (auto& b : m.brushes) nextGroup = std::max(nextGroup, b.group + 1);
    for (auto& e : m.ents) nextGroup = std::max(nextGroup, e.group + 1);
    std::map<int, int> regroup;   // copies that were grouped stay grouped, apart from the originals
    auto gid = [&](int g) { if (!g) return 0; auto it = regroup.find(g); if (it == regroup.end()) it = regroup.emplace(g, nextGroup++).first; return it->second; };
    for (auto b : p.brushes) {
        translate(b, move);
        b.group = gid(b.group); b.locked = false;
        m.brushes.push_back(b);
        s.brushes.push_back((int)m.brushes.size() - 1);
    }
    for (auto e : p.ents) {
        if (e.type == "player_start") continue;   // there's only one place you start
        e.pos += move; e.group = gid(e.group);
        m.ents.push_back(e);
        s.ents.push_back((int)m.ents.size() - 1);
    }
    return s;
}
void transformPiece(Piece& p, const Mat3& a, V3 pivot, V3 move, double yawChange) {
    for (auto& b : p.brushes) transform(b, a, pivot, move);
    for (auto& e : p.ents) { e.pos = mul(a, e.pos - pivot) + pivot + move; e.yaw = std::fmod(e.yaw + yawChange + 7200, 360); }
}
std::string pieceToJson(const Piece& p) {
    Map t; t.name = "piece"; t.brushes = p.brushes; t.ents = p.ents;
    nlohmann::json j = nlohmann::json::parse(mapToJson(t));
    j["gunsmithMapPiece"] = 1;
    return j.dump();
}
bool pieceFromJson(const std::string& text, Piece& out) {
    nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    if (!j.is_object() || !j.contains("gunsmithMapPiece")) return false;
    Map t;
    if (!mapFromJson(text, t)) return false;
    out.brushes = t.brushes; out.ents = t.ents;
    return !out.empty();
}
Selection linearArray(Map& m, const Selection& s, int count, V3 step) {
    Selection all;
    Piece base = copyPiece(m, s);
    for (int k = 1; k <= std::clamp(count, 0, 200); k++) {
        Selection n = pastePiece(m, base, step * (double)k);
        all.brushes.insert(all.brushes.end(), n.brushes.begin(), n.brushes.end());
        all.ents.insert(all.ents.end(), n.ents.begin(), n.ents.end());
    }
    return all;
}
Selection radialArray(Map& m, const Selection& s, int count, V3 pivot, int axis, double total, bool turn) {
    Selection all;
    Piece base = copyPiece(m, s);
    V3 c = selectionCentre(m, s);
    count = std::clamp(count, 0, 200);
    double stepDeg = total >= 359.9 ? total / (count + 1) : (count > 0 ? total / count : 0);
    for (int k = 1; k <= count; k++) {
        double r = stepDeg * k * PI / 180;
        Piece p = base;
        Mat3 R = rotationAbout(axis, r);
        if (turn) transformPiece(p, R, pivot, {}, axis == 1 ? -r * 180 / PI : 0);
        else transformPiece(p, Mat3{}, {}, mul(R, c - pivot) + pivot - c, 0);
        Selection n = pastePiece(m, p, {});
        all.brushes.insert(all.brushes.end(), n.brushes.begin(), n.brushes.end());
        all.ents.insert(all.ents.end(), n.ents.begin(), n.ents.end());
    }
    return all;
}

// ---------------------------------------------------------------- sharing
namespace {
const char* B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
std::string b64enc(const std::string& s) {
    std::string o;
    size_t i = 0;
    for (; i + 2 < s.size(); i += 3) {
        unsigned v = ((unsigned char)s[i] << 16) | ((unsigned char)s[i + 1] << 8) | (unsigned char)s[i + 2];
        o += B64[v >> 18]; o += B64[(v >> 12) & 63]; o += B64[(v >> 6) & 63]; o += B64[v & 63];
    }
    if (i + 1 == s.size()) { unsigned v = (unsigned char)s[i] << 16; o += B64[v >> 18]; o += B64[(v >> 12) & 63]; o += "=="; }
    else if (i + 2 == s.size()) { unsigned v = ((unsigned char)s[i] << 16) | ((unsigned char)s[i + 1] << 8); o += B64[v >> 18]; o += B64[(v >> 12) & 63]; o += B64[(v >> 6) & 63]; o += '='; }
    return o;
}
bool b64dec(const std::string& s, std::string& out) {
    out.clear();
    unsigned acc = 0; int bits = 0;
    for (char ch : s) {
        if (ch == '=' || ch == '\n' || ch == '\r' || ch == ' ') continue;
        const char* p = std::strchr(B64, ch);
        if (!p || !ch) return false;
        acc = (acc << 6) | (unsigned)(p - B64); bits += 6;
        if (bits >= 8) { bits -= 8; out += (char)((acc >> bits) & 0xFF); }
    }
    return true;
}
}  // namespace
std::string mapShareCode(const Map& m) {
    nlohmann::json j = nlohmann::json::parse(mapToJson(m));
    return "GS3D-MAP1:" + b64enc(j.dump());
}
bool mapFromShareText(const std::string& text, Map& out) {
    size_t at = text.find("GS3D-MAP1:");
    if (at != std::string::npos) {
        std::string code, json;   // the code runs to the first space or line end; whatever was pasted round it is ignored
        for (size_t i = at + 10; i < text.size() && !std::isspace((unsigned char)text[i]); i++) code += text[i];
        if (!b64dec(code, json)) return false;
        return mapFromJson(json, out);
    }
    return mapFromJson(text, out);
}

// ---------------------------------------------------------------- shapes
Brush makeCone(V3 mn, V3 mx, int sides, int mat) {
    sides = std::clamp(sides, 3, 64);
    double cx = (mn.x + mx.x) / 2, cz = (mn.z + mx.z) / 2, rx = (mx.x - mn.x) / 2, rz = (mx.z - mn.z) / 2;
    std::vector<V3> p;
    if (sides == 4) { p = {{mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z}, {mx.x, mn.y, mx.z}, {mn.x, mn.y, mx.z}}; }
    else for (int k = 0; k < sides; k++) { double t = 2 * PI * k / sides; p.push_back({cx + rx * std::cos(t), mn.y, cz + rz * std::sin(t)}); }
    p.push_back({cx, mx.y, cz});
    Brush like; like.faces = {mkface({0, 1, 0}, 0, mat)};
    Brush out;
    if (!brushFromPoints(p, like, out)) return makeBox(mn, mx, mat);
    return out;
}
Brush makeSphere(V3 mn, V3 mx, int rings, int sides, int mat) {
    rings = std::clamp(rings, 2, 16); sides = std::clamp(sides, 4, 32);
    double cx = (mn.x + mx.x) / 2, cy = (mn.y + mx.y) / 2, cz = (mn.z + mx.z) / 2, rx = (mx.x - mn.x) / 2, ry = (mx.y - mn.y) / 2, rz = (mx.z - mn.z) / 2;
    std::vector<V3> p;
    p.push_back({cx, mx.y, cz}); p.push_back({cx, mn.y, cz});
    for (int i = 1; i < rings; i++) {
        double th = PI * i / rings, y = std::cos(th), r = std::sin(th);
        for (int k = 0; k < sides; k++) { double t = 2 * PI * k / sides; p.push_back({cx + rx * r * std::cos(t), cy + ry * y, cz + rz * r * std::sin(t)}); }
    }
    Brush like; like.faces = {mkface({0, 1, 0}, 0, mat)};
    Brush out;
    if (!brushFromPoints(p, like, out)) return makeBox(mn, mx, mat);
    return out;
}
Brush makeQuarterRound(V3 mn, V3 mx, int steps, int dir, int mat) {
    steps = std::clamp(steps, 2, 24);
    double rx = mx.x - mn.x, rz = mx.z - mn.z;
    int d = ((dir % 4) + 4) % 4;
    double px = (d == 0 || d == 3) ? mn.x : mx.x, pz = (d == 0 || d == 1) ? mn.z : mx.z;   // the sharp corner
    double sx = (d == 0 || d == 3) ? 1 : -1, sz = (d == 0 || d == 1) ? 1 : -1;               // pointing into the box
    std::vector<V3> p;
    for (double y : {mn.y, mx.y}) {
        p.push_back({px, y, pz});
        for (int k = 0; k <= steps; k++) { double t = (PI / 2) * k / steps; p.push_back({px + sx * rx * std::cos(t), y, pz + sz * rz * std::sin(t)}); }
    }
    Brush like; like.faces = {mkface({0, 1, 0}, 0, mat)};
    Brush out;
    if (!brushFromPoints(p, like, out)) return makeBox(mn, mx, mat);
    return out;
}
std::vector<Brush> makeArch(V3 mn, V3 mx, int segments, double wall, int dir, int mat) {
    segments = std::clamp(segments, 3, 32);
    bool alongX = (dir % 2) == 0;
    double width = alongX ? mx.x - mn.x : mx.z - mn.z, depth = alongX ? mx.z - mn.z : mx.x - mn.x;
    double c = alongX ? (mn.x + mx.x) / 2 : (mn.z + mx.z) / 2, d0 = alongX ? mn.z : mn.x;
    double H = mx.y - mn.y, R = width / 2;
    wall = std::clamp(wall, 0.02, R * 0.9);
    double spring = mn.y + std::max(H - R, 0.0);
    auto world = [&](double u, double y, double w) { return alongX ? V3{c + u, y, d0 + w} : V3{d0 + w, y, c + u}; };
    Brush like; like.faces = {mkface({0, 1, 0}, 0, mat)};
    std::vector<Brush> out;
    auto addHull = [&](const std::vector<V3>& pts) { Brush b; if (brushFromPoints(pts, like, b)) out.push_back(b); };
    if (spring > mn.y + 1e-6)
        for (int side = -1; side <= 1; side += 2) {   // the two legs
            double u0 = side * (R - wall), u1 = side * R;
            addHull({world(std::min(u0, u1), mn.y, 0), world(std::max(u0, u1), mn.y, 0), world(std::min(u0, u1), spring, 0), world(std::max(u0, u1), spring, 0),
                     world(std::min(u0, u1), mn.y, depth), world(std::max(u0, u1), mn.y, depth), world(std::min(u0, u1), spring, depth), world(std::max(u0, u1), spring, depth)});
        }
    for (int k = 0; k < segments; k++) {   // the curve, a block at a time
        double a0 = PI * k / segments, a1 = PI * (k + 1) / segments;
        std::vector<V3> pts;
        for (double w : {0.0, depth}) for (double a : {a0, a1}) {
            pts.push_back(world(R * std::cos(a), spring + R * std::sin(a), w));
            pts.push_back(world((R - wall) * std::cos(a), spring + (R - wall) * std::sin(a), w));
        }
        addHull(pts);
    }
    return out;
}
std::vector<Brush> makePipe(V3 mn, V3 mx, int sides, double wall, int mat) {
    sides = std::clamp(sides, 6, 48);
    double cx = (mn.x + mx.x) / 2, cz = (mn.z + mx.z) / 2, rx = (mx.x - mn.x) / 2, rz = (mx.z - mn.z) / 2;
    wall = std::clamp(wall, 0.02, std::min(rx, rz) * 0.9);
    Brush like; like.faces = {mkface({0, 1, 0}, 0, mat)};
    std::vector<Brush> out;
    for (int k = 0; k < sides; k++) {
        double a0 = 2 * PI * k / sides, a1 = 2 * PI * (k + 1) / sides;
        std::vector<V3> pts;
        for (double y : {mn.y, mx.y}) for (double a : {a0, a1}) {
            pts.push_back({cx + rx * std::cos(a), y, cz + rz * std::sin(a)});
            pts.push_back({cx + (rx - wall) * std::cos(a), y, cz + (rz - wall) * std::sin(a)});
        }
        Brush b;
        if (brushFromPoints(pts, like, b)) out.push_back(b);
    }
    return out;
}

// ---------------------------------------------------------------- props
const std::vector<std::string>& propKinds() {
    static const std::vector<std::string> k = {"crate", "barrel", "table", "chair", "shelf", "pillar", "bench", "counter", "sandbags", "locker"};
    return k;
}
std::vector<Brush> makeProp(const std::string& kind, V3 base, double yawDeg) {
    std::vector<Brush> out;
    int wood = materialIndex("wood_planks"), ply = materialIndex("plywood"), steel = materialIndex("metal_plate"), conc = materialIndex("concrete"), block = materialIndex("cinderblock"), paint = materialIndex("paint_white");
    auto box = [&](double x, double y0, double z, double w, double h, double d, int mat) { out.push_back(makeBox({x - w / 2, y0, z - d / 2}, {x + w / 2, y0 + h, z + d / 2}, mat)); };
    if (kind == "crate") box(0, 0, 0, 0.9, 0.9, 0.9, ply);
    else if (kind == "barrel") out.push_back(makeCylinder({-0.3, 0, -0.3}, {0.3, 0.95, 0.3}, 12, steel));
    else if (kind == "table") {
        box(0, 0.72, 0, 1.6, 0.06, 0.8, wood);
        for (double x : {-0.72, 0.72}) for (double z : {-0.32, 0.32}) box(x, 0, z, 0.07, 0.72, 0.07, wood);
    } else if (kind == "chair") {
        box(0, 0.45, 0, 0.45, 0.05, 0.45, wood);
        for (double x : {-0.2, 0.2}) for (double z : {-0.2, 0.2}) box(x, 0, z, 0.05, 0.45, 0.05, wood);
        box(0, 0.5, -0.2, 0.45, 0.45, 0.05, wood);
    } else if (kind == "shelf") {
        for (double x : {-0.6, 0.6}) box(x, 0, 0, 0.05, 2, 0.4, steel);
        for (double y : {0.0, 0.5, 1.0, 1.5, 1.95}) box(0, y, 0, 1.2, 0.05, 0.4, steel);
    } else if (kind == "pillar") out.push_back(makeCylinder({-0.25, 0, -0.25}, {0.25, 3, 0.25}, 12, conc));
    else if (kind == "bench") {
        box(0, 0.45, 0, 1.8, 0.06, 0.4, wood);
        for (double x : {-0.8, 0.8}) box(x, 0, 0, 0.06, 0.45, 0.36, wood);
    } else if (kind == "counter") { box(0, 0, 0, 2.4, 1.05, 0.7, ply); box(0, 1.05, 0, 2.5, 0.05, 0.8, paint); }
    else if (kind == "sandbags") {
        for (int row = 0; row < 3; row++) for (int k = 0; k < 4 - row; k++) box(-0.45 * (3 - row) / 2.0 + 0.45 * k, row * 0.22, 0, 0.42, 0.22, 0.7, block);
    } else if (kind == "locker") { box(0, 0, 0, 0.5, 1.8, 0.5, steel); }
    for (auto& b : out) { rotateAbout(b, 1, yawDeg * PI / 180, {0, 0, 0}); translate(b, base); }
    return out;
}

Measure measure(V3 a, V3 b) {
    Measure m;
    V3 d = b - a;
    m.dx = d.x; m.dy = d.y; m.dz = d.z;
    m.distance = len(d);
    m.flat = std::sqrt(d.x * d.x + d.z * d.z);
    m.slopeDegrees = m.flat > 1e-9 ? std::atan2(d.y, m.flat) * 180 / PI : (d.y > 0 ? 90 : d.y < 0 ? -90 : 0);
    return m;
}

}  // namespace gmap
