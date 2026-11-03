// Gunsmith3D - engravings on parts (see MarkDef): following their part, and text spelt out in single strokes, the way
// an engraving pen writes.
#include "design.hpp"
#include <cmath>
#include <cstring>

namespace {
P3d M(P3d v) { return {v.x, -v.y, v.z}; }   // design axes (y down) <-> a right-handed frame (y up): the same flip both ways
P3d add(P3d a, P3d b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
P3d mul(P3d a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double dot(P3d a, P3d b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
P3d cross(P3d a, P3d b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
P3d unit(P3d a) { double l = std::sqrt(dot(a, a)); return l > 1e-12 ? mul(a, 1 / l) : P3d{1, 0, 0}; }

// The lettering: each character on a grid 4 wide and 6 tall (baseline 0), as strokes "x,y x,y ...|x,y ...".
struct Glyph { char c; const char* s; };
const Glyph GLYPHS[] = {
    {'A', "0,0 2,6 4,0|0.8,2.4 3.2,2.4"},
    {'B', "0,0 0,6 3,6 4,5 4,4 3,3 0,3|3,3 4,2 4,1 3,0 0,0"},
    {'C', "4,5 3,6 1,6 0,5 0,1 1,0 3,0 4,1"},
    {'D', "0,0 0,6 2.5,6 4,4.5 4,1.5 2.5,0 0,0"},
    {'E', "4,6 0,6 0,0 4,0|0,3 3,3"},
    {'F', "4,6 0,6 0,0|0,3 3,3"},
    {'G', "4,5 3,6 1,6 0,5 0,1 1,0 3,0 4,1 4,3 2,3"},
    {'H', "0,0 0,6|4,0 4,6|0,3 4,3"},
    {'I', "1,6 3,6|2,6 2,0|1,0 3,0"},
    {'J', "4,6 4,1 3,0 1,0 0,1"},
    {'K', "0,0 0,6|4,6 0,2|1.3,3.3 4,0"},
    {'L', "0,6 0,0 4,0"},
    {'M', "0,0 0,6 2,3 4,6 4,0"},
    {'N', "0,0 0,6 4,0 4,6"},
    {'O', "1,0 0,1 0,5 1,6 3,6 4,5 4,1 3,0 1,0"},
    {'P', "0,0 0,6 3,6 4,5 4,4 3,3 0,3"},
    {'Q', "1,0 0,1 0,5 1,6 3,6 4,5 4,1 3,0 1,0|2.5,1.5 4,0"},
    {'R', "0,0 0,6 3,6 4,5 4,4 3,3 0,3|2,3 4,0"},
    {'S', "4,5 3,6 1,6 0,5 0,4 1,3 3,3 4,2 4,1 3,0 1,0 0,1"},
    {'T', "0,6 4,6|2,6 2,0"},
    {'U', "0,6 0,1 1,0 3,0 4,1 4,6"},
    {'V', "0,6 2,0 4,6"},
    {'W', "0,6 1,0 2,4 3,0 4,6"},
    {'X', "0,0 4,6|0,6 4,0"},
    {'Y', "0,6 2,3 4,6|2,3 2,0"},
    {'Z', "0,6 4,6 0,0 4,0"},
    {'0', "1,0 0,1 0,5 1,6 3,6 4,5 4,1 3,0 1,0"},
    {'1', "1,5 2,6 2,0|1,0 3,0"},
    {'2', "0,5 1,6 3,6 4,5 4,4 0,0 4,0"},
    {'3', "0,5 1,6 3,6 4,5 4,4 3,3 1.5,3|3,3 4,2 4,1 3,0 1,0 0,1"},
    {'4', "3,0 3,6 0,2 4,2"},
    {'5', "4,6 0,6 0,3.5 3,3.5 4,2.5 4,1 3,0 1,0 0,1"},
    {'6', "4,5 3,6 1,6 0,5 0,1 1,0 3,0 4,1 4,2 3,3 0,3"},
    {'7', "0,6 4,6 1.5,0"},
    {'8', "1,3 0,4 0,5 1,6 3,6 4,5 4,4 3,3 1,3 0,2 0,1 1,0 3,0 4,1 4,2 3,3"},
    {'9', "0,1 1,0 3,0 4,1 4,5 3,6 1,6 0,5 0,4 1,3 4,3"},
    {'.', "2,0 2,0.35"},
    {',', "2,0.4 2,0 1.4,-1"},
    {'-', "1,3 3,3"},
    {'/', "0,0 4,6"},
    {':', "2,1 2,1.35|2,4 2,4.35"},
    {'(', "3,6 2,5 1.5,3 2,1 3,0"},
    {')', "1,6 2,5 2.5,3 2,1 1,0"},
    {'#', "1,0 1.6,6|2.4,0 3,6|0,2 4,2|0,4 4,4"},
    {'\'', "2,6 2,4.5"},
    {'"', "1.3,6 1.3,4.5|2.7,6 2.7,4.5"},
    {'!', "2,6 2,1.6|2,0 2,0.35"},
    {'?', "0,5 1,6 3,6 4,5 4,4 2,2.6 2,1.6|2,0 2,0.35"},
    {'&', "4,0 1,4.5 1,5.5 1.6,6 2.6,6 3,5.4 2.6,4.4 0,2 0,1 1,0 2.5,0 4,2"},
    {'+', "2,1 2,5|0,3 4,3"},
    {'=', "0,2 4,2|0,4 4,4"},
    {'%', "0,0 4,6|0.5,5.5 1,5.5 1,5 0.5,5 0.5,5.5|3,1 3.5,1 3.5,0.5 3,0.5 3,1"},
    {'*', "2,1.5 2,4.5|0.7,2.2 3.3,3.8|0.7,3.8 3.3,2.2"},
    {'_', "0,-0.6 4,-0.6"},
    {'<', "4,5 0,3 4,1"},
    {'>', "0,5 4,3 0,1"},
    {'@', "3,2 2,1.5 1.5,2.5 2,3.5 3,3.5 3,2 3.8,2 4,3 3.6,5 2.4,5.8 1,5.4 0,4 0,2 1,0.4 2.6,0 3.6,0.4"},
};
constexpr double ADVANCE = 5.5;   // each character's room (grid units): monospaced, like stamped serial numbers

const char* glyphOf(char c) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');   // small letters are written as capitals
    for (auto& g : GLYPHS) if (g.c == c) return g.s;
    return nullptr;
}
}  // namespace

std::vector<std::vector<V2>> strokeText(const std::string& text, double height, double* width) {
    std::vector<std::vector<V2>> out;
    double s = height / 6, n = (double)text.size(), w = n > 0 ? (n * ADVANCE - (ADVANCE - 4)) * s : 0;
    if (width) *width = w;
    for (size_t i = 0; i < text.size(); i++) {
        const char* g = glyphOf(text[i]);
        if (!g) continue;   // a space, or a sign the lettering doesn't have
        double ox = i * ADVANCE * s - w / 2, oy = -height / 2;
        std::vector<V2> line;
        const char* p = g;
        while (*p) {
            if (*p == '|') { if (line.size() > 1) out.push_back(line); line.clear(); p++; continue; }
            if (*p == ' ') { p++; continue; }
            char* e = nullptr;
            double x = std::strtod(p, &e);
            if (e == p || *e != ',') break;
            double y = std::strtod(e + 1, &e);
            line.push_back({ox + x * s, oy + y * s});
            p = e;
        }
        if (line.size() > 1) out.push_back(line);
    }
    return out;
}

P3d textRightFor(P3d n, double turnDeg) {
    P3d nm = unit(M(n)), up{0, 1, 0};
    if (std::fabs(dot(nm, up)) > 0.9) up = {0, 0, -1};   // a top or bottom face: read it along the gun
    P3d r = unit(cross(up, nm));
    double a = turnDeg * 3.14159265358979323846 / 180;   // turned about the surface's direction
    r = add(add(mul(r, std::cos(a)), mul(cross(nm, r), std::sin(a))), mul(nm, dot(nm, r) * (1 - std::cos(a))));
    return M(r);
}

std::vector<std::vector<MarkPt>> markLines(const MarkDef& m) {
    std::vector<std::vector<MarkPt>> out;
    if (m.pts.empty()) return out;
    if (m.text.empty()) {
        std::vector<MarkPt> line;
        for (size_t k = 0; k < m.pts.size(); k++) line.push_back({m.pts[k], k < m.n.size() ? m.n[k] : P3d{0, 0, 1}});
        out.push_back(line);
        return out;
    }
    P3d n = m.n.empty() ? P3d{0, 0, 1} : m.n[0];
    P3d nm = unit(M(n)), r = unit(M(m.right));
    r = unit(add(r, mul(nm, -dot(r, nm))));   // (kept flat on the surface)
    P3d up = cross(nm, r), o = M(m.pts[0]);
    for (auto& l : strokeText(m.text, m.size)) {
        std::vector<MarkPt> line;
        for (auto& q : l) line.push_back({M(add(o, add(mul(r, q.x), mul(up, q.y)))), n});
        out.push_back(line);
    }
    return out;
}

void followMarks(BodyDef& b) {
    if (b.marks.empty()) { b.markRef = b.pts; b.markRefZ = b.z; b.markRefThick = b.thick; return; }
    const auto& A = b.markRef;
    const auto& B = b.pts;
    bool movedFlat = A.size() != B.size();
    if (!movedFlat) for (size_t i = 0; i < A.size() && !movedFlat; i++) movedFlat = A[i].x != B[i].x || A[i].y != B[i].y;
    if (movedFlat && A.size() == B.size() && A.size() >= 2) {
        // moved, turned, flipped or mirrored as a whole? The best fitting turn (with or without a flip) and shift
        V2 ca{0, 0}, cb{0, 0};
        for (size_t i = 0; i < A.size(); i++) { ca.x += A[i].x; ca.y += A[i].y; cb.x += B[i].x; cb.y += B[i].y; }
        ca.x /= A.size(); ca.y /= A.size(); cb.x /= B.size(); cb.y /= B.size();
        double best = 1e30, bc = 1, bs = 0; int bflip = 1;
        for (int flip : {1, -1}) {   // flip: mirror x before turning
            double sxx = 0, sxy = 0;
            for (size_t i = 0; i < A.size(); i++) {
                double ax = (A[i].x - ca.x) * flip, ay = A[i].y - ca.y, bx = B[i].x - cb.x, by = B[i].y - cb.y;
                sxx += ax * bx + ay * by; sxy += ax * by - ay * bx;
            }
            double th = std::atan2(sxy, sxx), c = std::cos(th), s = std::sin(th), worst = 0;
            for (size_t i = 0; i < A.size(); i++) {
                double ax = (A[i].x - ca.x) * flip, ay = A[i].y - ca.y;
                worst = std::fmax(worst, std::hypot(c * ax - s * ay + cb.x - B[i].x, s * ax + c * ay + cb.y - B[i].y));
            }
            if (worst < best) { best = worst; bc = c; bs = s; bflip = flip; }
        }
        if (best < 0.01) {   // a whole-part move: the marks go with it (reshaping leaves them where they are)
            auto pt = [&](P3d& p) { double ax = (p.x - ca.x) * bflip, ay = p.y - ca.y; p.x = bc * ax - bs * ay + cb.x; p.y = bs * ax + bc * ay + cb.y; };
            auto dir = [&](P3d& v) { double ax = v.x * bflip, ay = v.y; v.x = bc * ax - bs * ay; v.y = bs * ax + bc * ay; };
            for (auto& m : b.marks) {
                for (auto& p : m.pts) pt(p);
                for (auto& n : m.n) dir(n);
                dir(m.right);
            }
        }
    }
    if (b.z != b.markRefZ || b.thick != b.markRefThick) {   // moved in depth, or made thicker: marks on a face stay on it
        double nearOld = b.markRefZ + b.markRefThick / 2, farOld = b.markRefZ - b.markRefThick / 2;
        double nearNew = b.z + b.thick / 2, farNew = b.z - b.thick / 2;
        for (auto& m : b.marks)
            for (auto& p : m.pts) {
                if (std::fabs(p.z - nearOld) < 0.05) p.z += nearNew - nearOld;
                else if (std::fabs(p.z - farOld) < 0.05) p.z += farNew - farOld;
                else p.z += b.z - b.markRefZ;
            }
    }
    b.markRef = b.pts; b.markRefZ = b.z; b.markRefThick = b.thick;
}

bool trackDirection(const Design& d, const std::string& partId, double& deg) {
    for (auto& j : d.joints) {
        if (j.type != "track" || j.a != partId || j.path.size() < 2) continue;
        // the end of the slot farther from where the part's point sits is the way it can go
        const V2& a = j.path.front(); const V2& b = j.path.back();
        double da = std::hypot(a.x - j.p.x, a.y - j.p.y), db = std::hypot(b.x - j.p.x, b.y - j.p.y);
        V2 to = db >= da ? b : a;
        double dx = to.x - j.p.x, dy = to.y - j.p.y;
        if (std::hypot(dx, dy) < 1e-6) { dx = b.x - a.x; dy = b.y - a.y; }
        if (std::hypot(dx, dy) < 1e-6) continue;
        deg = std::atan2(dy, dx) * 180 / 3.14159265358979323846;
        if (deg < 0) deg += 360;
        deg = std::round(deg);
        return true;
    }
    return false;
}
