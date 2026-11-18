// Gunsmith3D - fitting a modelled character onto an NPC's bones (see rig.hpp).
#include "rig.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>

namespace rig {
namespace {
using Q = std::array<double, 4>;   // x, y, z, w
struct V { double x, y, z; };
V sub(V a, V b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V add(V a, V b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V mulv(V a, double k) { return {a.x * k, a.y * k, a.z * k}; }
double dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V cross(V a, V b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double len(V a) { return std::sqrt(dot(a, a)); }
V unit(V a) { double l = len(a); return l > 1e-12 ? mulv(a, 1 / l) : V{0, 1, 0}; }
V vp(const double* p) { return {p[0], p[1], p[2]}; }
Q qof(const double* q) { return {q[0], q[1], q[2], q[3]}; }
Q qmul(Q a, Q b) {
    return {a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1], a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
            a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3], a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]};
}
Q qinv(Q a) { return {-a[0], -a[1], -a[2], a[3]}; }   // (unit quaternions)
Q qnorm(Q a) { double l = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2] + a[3] * a[3]); if (l < 1e-12) return {0, 0, 0, 1}; return {a[0] / l, a[1] / l, a[2] / l, a[3] / l}; }
V qrot(Q q, V v) {
    V u{q[0], q[1], q[2]};
    V t = mulv(cross(u, v), 2);
    return add(add(v, mulv(t, q[3])), cross(u, t));
}
Q qbetween(V a, V b) {   // the shortest turn taking direction a onto b
    a = unit(a); b = unit(b);
    double d = dot(a, b);
    if (d < -0.999999) {   // (opposite: any axis across)
        V ax = cross({1, 0, 0}, a);
        if (len(ax) < 1e-6) ax = cross({0, 1, 0}, a);
        ax = unit(ax);
        return {ax.x, ax.y, ax.z, 0};
    }
    V c = cross(a, b);
    return qnorm({c.x, c.y, c.z, 1 + d});
}
Q qfromFrame(V x, V y, V z) {   // the turn whose columns are x, y, z
    double m00 = x.x, m01 = y.x, m02 = z.x, m10 = x.y, m11 = y.y, m12 = z.y, m20 = x.z, m21 = y.z, m22 = z.z;
    double tr = m00 + m11 + m22;
    Q q;
    if (tr > 0) { double s = std::sqrt(tr + 1) * 2; q = {(m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25 * s}; }
    else if (m00 > m11 && m00 > m22) { double s = std::sqrt(1 + m00 - m11 - m22) * 2; q = {0.25 * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s}; }
    else if (m11 > m22) { double s = std::sqrt(1 + m11 - m00 - m22) * 2; q = {(m01 + m10) / s, 0.25 * s, (m12 + m21) / s, (m02 - m20) / s}; }
    else { double s = std::sqrt(1 + m22 - m00 - m11) * 2; q = {(m02 + m20) / s, (m12 + m21) / s, 0.25 * s, (m10 - m01) / s}; }
    return qnorm(q);
}

std::string lower(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }
std::string alnum(const std::string& s) { std::string o; for (char c : s) if (std::isalnum((unsigned char)c)) o += c; return o; }
bool has(const std::string& s, const char* w) { return s.find(w) != std::string::npos; }

// which side a joint's name says it's on: -1 left, 1 right, 0 neither; and the name without its side marks
int sideOf(std::string s, std::string& rest) {
    s = lower(s);
    for (const char* p : {"mixamorig:", "mixamorig_", "mixamorig", "def-", "def_", "bip01 ", "bip01_", "bip001 ", "bip001_"})
        if (s.rfind(p, 0) == 0) s = s.substr(std::string(p).size());
    int side = 0;
    auto cut = [&](size_t at, size_t n) { s.erase(at, n); };
    if (size_t k = s.find("left"); k != std::string::npos) { side = -1; cut(k, 4); }
    else if (size_t k2 = s.find("right"); k2 != std::string::npos) { side = 1; cut(k2, 5); }
    else {
        auto sep = [](char c) { return c == '.' || c == '_' || c == ' ' || c == '-'; };
        if (s.size() >= 2 && sep(s[s.size() - 2]) && (s.back() == 'l' || s.back() == 'r')) { side = s.back() == 'l' ? -1 : 1; s.resize(s.size() - 2); }
        else if (s.size() >= 2 && sep(s[1]) && (s[0] == 'l' || s[0] == 'r')) { side = s[0] == 'l' ? -1 : 1; s = s.substr(2); }
    }
    rest = alnum(s);
    return side;
}

// what a human joint is, by its name (without its side): one of our bone names' first words, or "" (not followed)
std::string kindOf(const std::string& r) {
    for (const char* w : {"thumb", "index", "middle", "ring", "pinky", "little", "finger", "toe", "end", "top", "nub", "twist", "roll",
                          "shoulder", "clavicle", "collar", "eye", "jaw", "tongue", "teeth", "breast", "ik", "pole", "target"})
        if (has(r, w)) return "";
    if (has(r, "forearm") || has(r, "lowerarm") || has(r, "elbow")) return "Forearm";
    if (has(r, "upperarm") || r == "arm" || r == "uparm") return "Upper arm";
    if (has(r, "hand") || has(r, "wrist")) return "Hand";
    if (has(r, "upleg") || has(r, "thigh") || has(r, "upperleg")) return "Thigh";
    if (r == "leg" || has(r, "calf") || has(r, "shin") || has(r, "lowerleg") || has(r, "knee")) return "Shin";
    if (has(r, "foot") || has(r, "ankle")) return "Foot";
    if (has(r, "hip") || has(r, "pelvis")) return "Pelvis";
    if (has(r, "spine") || has(r, "chest") || has(r, "torso") || has(r, "abdomen") || has(r, "belly")) return "Chest";
    if (has(r, "neck")) return "Neck";
    if (has(r, "head")) return "Head";
    return "";
}
bool limbKind(const std::string& k) { return k == "Upper arm" || k == "Forearm" || k == "Hand" || k == "Thigh" || k == "Shin" || k == "Foot"; }

int boneIndex(const NpcDef& n, const std::string& nm) { for (int i = 0; i < (int)n.bones.size(); i++) if (n.bones[i].name == nm) return i; return -1; }

// the NPC standing straight: its body bones as a human's are (the reference the model's own pose stands for)
NpcDef standing(const NpcDef& n) {
    NpcDef r = n;
    r.rootBend = 0; r.rootTurn = 0;
    static const NpcDef human = npcPreset("human");
    for (auto& b : r.bones)
        for (auto& h : human.bones)
            if (h.name == b.name) { b.bend = h.bend; b.turn = h.turn; b.twist = h.twist; }
    return r;
}

std::vector<int> order(const std::vector<Joint>& J) {   // parents before children
    std::vector<int> o; std::vector<char> done(J.size(), 0);
    for (size_t pass = 0; pass <= J.size() && o.size() < J.size(); pass++)
        for (int i = 0; i < (int)J.size(); i++)
            if (!done[i] && (J[i].parent < 0 || J[i].parent >= (int)J.size() || done[J[i].parent])) { done[i] = 1; o.push_back(i); }
    for (int i = 0; i < (int)J.size(); i++) if (!done[i]) o.push_back(i);   // (a loop: whatever's left)
    return o;
}
}  // namespace

std::vector<int> mapJoints(const std::vector<Joint>& J, const NpcDef& n) {
    std::vector<int> map(J.size(), -1);
    const auto rest = npcPose(standing(n));
    for (size_t j = 0; j < J.size(); j++) {
        // the same name as one of the NPC's bones (any NPC, not only people)
        std::string jn = alnum(lower(J[j].name));
        for (int i = 0; i < (int)n.bones.size(); i++) if (alnum(lower(n.bones[i].name)) == jn) { map[j] = i; break; }
        if (map[j] >= 0) continue;
        std::string r;
        int side = sideOf(J[j].name, r);
        std::string k = kindOf(r);
        if (k.empty()) continue;
        if (!limbKind(k)) { map[j] = boneIndex(n, k); continue; }
        int L = boneIndex(n, k + " (left)"), R = boneIndex(n, k + " (right)");
        if (L < 0 || R < 0) { map[j] = L >= 0 ? L : R; continue; }
        // the side: the NPC's bone on the same side of the body as the joint (the model may be the mirror of the NPC's naming)
        double jx = J[j].bind.p[0], lx = rest[L].a[0], rx = rest[R].a[0];
        if (std::fabs(jx) > 1e-4 && std::fabs(lx - rx) > 1e-6) map[j] = (jx > 0) == (lx > rx) ? L : R;
        else map[j] = side < 0 ? L : side > 0 ? R : -1;
    }
    return map;
}

std::string mapReport(const std::vector<Joint>& J, const std::vector<int>& map, const NpcDef& n) {
    static const char* body[] = {"Pelvis", "Chest", "Neck", "Head", "Upper arm (left)", "Forearm (left)", "Hand (left)", "Upper arm (right)",
                                 "Forearm (right)", "Hand (right)", "Thigh (left)", "Shin (left)", "Foot (left)", "Thigh (right)", "Shin (right)", "Foot (right)"};
    int want = 0, got = 0;
    std::string missing;
    for (const char* b : body) {
        int i = boneIndex(n, b);
        if (i < 0) continue;
        want++;
        if (std::find(map.begin(), map.end(), i) != map.end()) got++;
        else missing += (missing.empty() ? "" : ", ") + std::string(b);
    }
    (void)J;
    if (want == 0) {   // (not a person: how many of its bones a joint follows)
        int f = 0; for (int m : map) if (m >= 0) f++;
        return std::to_string(f) + " joints follow the NPC's bones";
    }
    std::string s = std::to_string(got) + " of the " + std::to_string(want) + " body bones found";
    if (!missing.empty()) s += " (missing: " + missing + ")";
    return s;
}

std::vector<Xf> poseJoints(const std::vector<Joint>& J, const std::vector<int>& map, const NpcDef& n, const std::vector<NpcBoneAt>& P) {
    std::vector<Xf> out(J.size());
    for (size_t j = 0; j < J.size(); j++) out[j] = J[j].bind;
    if (J.empty() || P.size() < n.bones.size()) return out;
    const auto rest = npcPose(standing(n));
    std::vector<Q> carry(J.size(), Q{0, 0, 0, 1});   // each joint's turn from as modelled to as posed
    // the size: the model's hips height against the NPC's pelvis height
    int hips = -1, pelvis = boneIndex(n, "Pelvis");
    auto ord = order(J);
    for (int j : ord) if (map[j] >= 0 && (map[j] == pelvis || (pelvis < 0 && map[j] == 0))) { hips = j; break; }
    if (pelvis < 0) pelvis = 0;
    double s = 0.001;   // (metres per mm, if nothing better)
    if (hips >= 0 && rest[pelvis].a[1] > 1 && J[hips].bind.p[1] > 1e-3) s = J[hips].bind.p[1] / rest[pelvis].a[1];
    std::vector<std::vector<int>> kids(J.size());
    for (int j = 0; j < (int)J.size(); j++) if (J[j].parent >= 0 && J[j].parent < (int)J.size()) kids[J[j].parent].push_back(j);
    for (int j : ord) {
        const Joint& jt = J[j];
        int p = jt.parent >= 0 && jt.parent < (int)J.size() ? jt.parent : -1;
        Q up = p >= 0 ? carry[p] : Q{0, 0, 0, 1};
        Q bindQ = qof(jt.bind.q);
        Q c = up;
        int b = map[j];
        if (b >= 0 && b < (int)P.size()) {
            const NpcBoneAt& now = P[b];
            const NpcBoneAt& was = rest[b];
            bool limb = limbKind(kindOf(alnum(lower(n.bones[b].name.substr(0, n.bones[b].name.find(" ("))))));
            // the direction the joint points as modelled: toward the child that follows the next bone (else the farthest)
            int next = -1; double far = -1;
            for (int k : kids[j]) {
                double d = len(sub(vp(J[k].bind.p), vp(jt.bind.p)));
                bool other = map[k] >= 0 && map[k] != b;
                if ((other ? d + 1e6 : d) > far) { far = other ? d + 1e6 : d; next = k; }
            }
            if (limb && next >= 0 && len(sub(vp(J[next].bind.p), vp(jt.bind.p))) > 1e-5) {
                // an arm or leg: carried by the joint above, then swung so it points along its bone
                V dirModel = qrot(up, unit(sub(vp(J[next].bind.p), vp(jt.bind.p))));
                V dirNow = unit(sub(vp(now.b), vp(now.a)));
                c = qnorm(qmul(qbetween(dirModel, dirNow), up));
            } else {
                // the body, or a joint with nothing below it: turned as its bone has turned from standing straight
                Q qNow = qfromFrame(vp(now.x), vp(now.y), vp(now.z)), qWas = qfromFrame(vp(was.x), vp(was.y), vp(was.z));
                c = qnorm(qmul(qNow, qinv(qWas)));
            }
        }
        carry[j] = c;
        Q q = qnorm(qmul(c, bindQ));
        out[j].q[0] = q[0]; out[j].q[1] = q[1]; out[j].q[2] = q[2]; out[j].q[3] = q[3];
        V pos;
        if (j == hips) pos = add(vp(jt.bind.p), mulv(sub(vp(P[pelvis].a), vp(rest[pelvis].a)), s));
        else if (p >= 0) pos = add(vp(out[p].p), qrot(carry[p], sub(vp(jt.bind.p), vp(J[p].bind.p))));
        else pos = vp(jt.bind.p);
        out[j].p[0] = pos.x; out[j].p[1] = pos.y; out[j].p[2] = pos.z;
    }
    return out;
}

double modelScale(const std::vector<Joint>& J, const std::vector<int>& map, const NpcDef& n) {
    const auto rest = npcPose(standing(n));
    int pelvis = boneIndex(n, "Pelvis");
    for (int j : order(J))
        if (map[j] >= 0 && (map[j] == pelvis || (pelvis < 0 && map[j] == 0))) {
            int b = map[j];
            if (rest[b].a[1] > 1 && J[j].bind.p[1] > 1e-3) return J[j].bind.p[1] / rest[b].a[1];
            break;
        }
    return 0.001;
}

void skinMatrix(const Xf& bind, const Xf& posed, float m[16]) {
    // posed x inverse(bind): a point as modelled, taken into the joint's own frame, then out by where it is now
    Q c = qnorm(qmul(qof(posed.q), qinv(qof(bind.q))));
    V t = sub(vp(posed.p), qrot(c, vp(bind.p)));
    V cx = qrot(c, {1, 0, 0}), cy = qrot(c, {0, 1, 0}), cz = qrot(c, {0, 0, 1});
    // column-major (as raylib's Matrix is laid out m0..m15 down the columns)
    m[0] = (float)cx.x; m[1] = (float)cx.y; m[2] = (float)cx.z; m[3] = 0;
    m[4] = (float)cy.x; m[5] = (float)cy.y; m[6] = (float)cy.z; m[7] = 0;
    m[8] = (float)cz.x; m[9] = (float)cz.y; m[10] = (float)cz.z; m[11] = 0;
    m[12] = (float)t.x; m[13] = (float)t.y; m[14] = (float)t.z; m[15] = 1;
}

}  // namespace rig
