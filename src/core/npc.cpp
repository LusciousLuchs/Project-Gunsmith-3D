// Gunsmith3D - NPC designs (see npc.hpp).
#include "npc.hpp"
#include "json.hpp"

#include <algorithm>
#include <cmath>

using json = nlohmann::json;
namespace {
constexpr double PI_N = 3.14159265358979323846, D2R_N = PI_N / 180;
struct M3 { double m[3][3]; };   // columns: the frame's x, y, z axes
M3 ident() { return {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}; }
M3 mul(const M3& A, const M3& B) {
    M3 C{};
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) { C.m[i][j] = 0; for (int k = 0; k < 3; k++) C.m[i][j] += A.m[i][k] * B.m[k][j]; }
    return C;
}
M3 rotX(double a) { double c = std::cos(a), s = std::sin(a); return {{{1, 0, 0}, {0, c, -s}, {0, s, c}}}; }
M3 rotY(double a) { double c = std::cos(a), s = std::sin(a); return {{{c, 0, s}, {0, 1, 0}, {-s, 0, c}}}; }
M3 rotZ(double a) { double c = std::cos(a), s = std::sin(a); return {{{c, -s, 0}, {s, c, 0}, {0, 0, 1}}}; }
// a bone's turn relative to its parent: bend forward about x (y toward +z), turn to its right about z (y toward -x
// seen from the front... its right), then twist about its own length
M3 local(double bend, double turn, double twist) { return mul(mul(rotZ(-turn * D2R_N), rotX(bend * D2R_N)), rotY(twist * D2R_N)); }
void col(const M3& R, int c, double out[3]) { for (int i = 0; i < 3; i++) out[i] = R.m[i][c]; }

NpcBone bone(const std::string& name, int parent, double at, double side, double len, double r0, double r1, double bend, double turn,
             const std::string& color, const std::string& shape = "capsule", bool vital = false) {
    NpcBone b; b.name = name; b.parent = parent; b.at = at; b.side = side; b.len = len; b.r0 = r0; b.r1 = r1;
    b.bend = bend; b.turn = turn; b.color = color; b.shape = shape; b.vital = vital;
    return b;
}
}  // namespace

std::vector<NpcBoneAt> npcPose(const NpcDef& n) {
    std::vector<NpcBoneAt> out(n.bones.size());
    std::vector<M3> R(n.bones.size());
    for (size_t i = 0; i < n.bones.size(); i++) {
        const NpcBone& b = n.bones[i];
        double a[3];
        M3 frame;
        if (i == 0) {   // the root: from the ground up, turned as the root is set
            frame = mul(local(n.rootBend, n.rootTurn, 0), local(b.bend, b.turn, b.twist));
            a[0] = 0; a[1] = n.rootHeight; a[2] = 0;
        } else {
            int pi = b.parent >= 0 && b.parent < (int)i ? b.parent : 0;   // (a bone listed before its parent hangs off the root)
            const NpcBoneAt& p = out[pi];
            frame = mul(R[pi], local(b.bend, b.turn, b.twist));
            double t = std::clamp(b.at, 0.0, 1.0);
            for (int k = 0; k < 3; k++) a[k] = p.a[k] + (p.b[k] - p.a[k]) * t + p.x[k] * b.side;
        }
        R[i] = frame;
        NpcBoneAt& o = out[i];
        col(frame, 0, o.x); col(frame, 1, o.y); col(frame, 2, o.z);
        for (int k = 0; k < 3; k++) { o.a[k] = a[k]; o.b[k] = a[k] + o.y[k] * b.len; }
    }
    return out;
}

void npcAimBone(NpcDef& n, int i, const double dir[3]) {
    if (i < 0 || i >= (int)n.bones.size()) return;
    double l = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (l < 1e-9) return;
    double d[3] = {dir[0] / l, dir[1] / l, dir[2] / l};
    // the parent's frame (for the root: the world)
    M3 P = ident();
    NpcBone& b = n.bones[i];
    if (i != 0 && b.parent >= 0 && b.parent < i) {
        auto pose = npcPose(n);
        const NpcBoneAt& p = pose[b.parent];
        for (int r = 0; r < 3; r++) { P.m[r][0] = p.x[r]; P.m[r][1] = p.y[r]; P.m[r][2] = p.z[r]; }
    } else if (i == 0) {
        P = local(n.rootBend, n.rootTurn, 0);
    }
    // into the parent's frame: then y = (sin turn cos bend, cos turn cos bend, sin bend) from local()
    double q[3];
    for (int c = 0; c < 3; c++) q[c] = P.m[0][c] * d[0] + P.m[1][c] * d[1] + P.m[2][c] * d[2];
    b.bend = std::asin(std::clamp(q[2], -1.0, 1.0)) / D2R_N;
    b.turn = std::atan2(q[0], q[1]) / D2R_N;
}

void npcMirrorBone(NpcDef& n, int i) {
    if (i <= 0 || i >= (int)n.bones.size()) return;
    // the bone and its children (they come after it), copied with sides and turns swapped
    std::vector<int> sub{i};
    for (int k = i + 1; k < (int)n.bones.size(); k++)
        if (std::find(sub.begin(), sub.end(), n.bones[k].parent) != sub.end()) sub.push_back(k);
    std::vector<int> newIdx(n.bones.size(), -1);
    for (int k : sub) {
        NpcBone c = n.bones[k];
        if (k == i) { c.side = -c.side; c.turn = -c.turn; c.twist = -c.twist; }
        else { c.parent = newIdx[c.parent]; c.side = -c.side; c.turn = -c.turn; c.twist = -c.twist; }
        auto swapLR = [](std::string s) {
            for (auto [a, b] : {std::pair<const char*, const char*>{"left", "right"}, {"Left", "Right"}, {"(L)", "(R)"}}) {
                size_t p;
                if ((p = s.find(a)) != std::string::npos) return s.replace(p, std::string(a).size(), b);
                if ((p = s.find(b)) != std::string::npos) return s.replace(p, std::string(b).size(), a);
            }
            return s + " (other side)";
        };
        c.name = swapLR(c.name);
        newIdx[k] = (int)n.bones.size();
        n.bones.push_back(c);
    }
}

void npcRemoveBone(NpcDef& n, int i) {
    if (i <= 0 || i >= (int)n.bones.size()) return;
    std::vector<bool> gone(n.bones.size(), false);
    gone[i] = true;
    for (int k = i + 1; k < (int)n.bones.size(); k++) if (n.bones[k].parent >= 0 && gone[n.bones[k].parent]) gone[k] = true;
    std::vector<int> idx(n.bones.size(), -1);
    std::vector<NpcBone> keep;
    for (int k = 0; k < (int)n.bones.size(); k++) if (!gone[k]) { idx[k] = (int)keep.size(); keep.push_back(n.bones[k]); }
    for (auto& b : keep) if (b.parent >= 0) b.parent = idx[b.parent];
    n.bones = keep;
}

double npcHeight(const NpcDef& n) {
    double top = 0;
    auto pose = npcPose(n);
    for (size_t i = 0; i < pose.size(); i++) top = std::max({top, pose[i].a[1] + n.bones[i].r0, pose[i].b[1] + n.bones[i].r1});
    return top;
}

double npcBoneRadius(const NpcBone& b, double t) {
    double r = b.r0 + (b.r1 - b.r0) * t;
    return std::max(0.0, r * (1 + b.bulge * std::sin(3.14159265358979 * std::clamp(t, 0.0, 1.0))));
}
double npcBoneReach(const NpcBone& b) {
    double r = 0;
    for (int k = 0; k <= 8; k++) r = std::max(r, npcBoneRadius(b, k / 8.0));
    return r * std::max(b.width, b.depth);
}

#include "builtin_npcs.inc"   // the owner's own NPCs (assets/npcs, embedded by tools/embed_npcs.py)

const std::vector<std::pair<std::string, std::string>>& npcPresets() {
    static std::vector<std::pair<std::string, std::string>> v;
    if (v.empty()) {
        for (auto& [file, text] : BUILTIN_NPCS) {   // the built-in designs first, by their own names
            NpcDef n;
            if (npcFromJson(text, n)) v.push_back({"builtin:" + file, n.name.empty() ? file : n.name});
        }
        v.insert(v.end(), {{"human", "Human"}, {"hound", "Hound (four legs)"}, {"crawler", "Crawler (six legs)"}, {"blank", "Blank (one bone)"}});
    }
    return v;
}

NpcDef npcPreset(const std::string& key) {
    NpcDef n;
    if (key.rfind("builtin:", 0) == 0)
        for (auto& [file, text] : BUILTIN_NPCS)
            if ("builtin:" + file == key && npcFromJson(text, n)) return n;
    const std::string skin = "#c8a088", cloth = "#4f5d52", dark = "#30353b";
    if (key == "hound") {   // the body lies forward; legs hang from its ends
        n.name = "Hound"; n.rootHeight = 560; n.rootBend = 90; n.health = 60; n.speed = 6; n.damage = 15; n.reach = 1.2;
        n.bones = {
            bone("Body", -1, 0, 0, 620, 120, 105, 0, 0, "#6b5a48"),
            bone("Neck", 0, 1, 0, 200, 70, 60, -45, 0, "#6b5a48"),
            bone("Head", 1, 1, 0, 230, 75, 45, 55, 0, "#5e4f40", "capsule", true),
            bone("Tail", 0, 0, 0, 300, 25, 10, 160, 0, "#5e4f40"),
            bone("Front leg (left)", 0, 0.9, -70, 300, 45, 35, 90, 0, "#6b5a48"),
            bone("Front shin (left)", 4, 1, 0, 260, 32, 25, -10, 0, "#5e4f40"),
            bone("Hind leg (left)", 0, 0.08, -70, 320, 55, 40, 90, 0, "#6b5a48"),
            bone("Hind shin (left)", 6, 1, 0, 250, 35, 25, 20, 0, "#5e4f40"),
        };
        npcMirrorBone(n, 4); npcMirrorBone(n, 6);
        return n;
    }
    if (key == "crawler") {   // something that isn't from here: a low body, six jointed legs, a long head
        n.name = "Crawler"; n.rootHeight = 420; n.rootBend = 90; n.health = 150; n.speed = 2.5; n.damage = 30; n.reach = 1.8; n.sight = 20;
        n.bones = {
            bone("Body", -1, 0, 0, 700, 150, 120, 0, 0, "#3c4a3a"),
            bone("Head", 0, 1, 0, 380, 90, 30, 15, 0, "#4f6a3f", "capsule", true),
            bone("Core", 0, 0.5, 0, 60, 70, 70, -90, 0, "#c0e060", "ball", true),
        };
        for (double at : {0.85, 0.5, 0.15}) {
            int l = (int)n.bones.size();
            n.bones.push_back(bone("Leg (left)", 0, at, -110, 420, 45, 35, 35, 0, "#3c4a3a"));
            n.bones.push_back(bone("Claw (left)", l, 1, 0, 200, 30, 8, 35, 0, "#2a3328"));
            npcMirrorBone(n, l);
        }
        n.bones[0].armor = 0.5;
        return n;
    }
    if (key == "blank") { n.name = "New NPC"; n.bones = {bone("Body", -1, 0, 0, 400, 80, 80, 0, 0, "#8a8f98")}; return n; }
    // a person standing: hips up through the chest to the head, arms down, legs down. Shaped: a flattened chest
    // wider at the shoulders, an egg of a head, muscles that swell, flat hands, boots that thin to the toe.
    auto look = [](NpcBone b, double width, double depth, double bulge) { b.width = width; b.depth = depth; b.bulge = bulge; return b; };
    const std::string trousers = "#3a3f36", boots = "#26231f", shirt = cloth;
    n.name = "Human";
    n.bones = {
        look(bone("Pelvis", -1, 0, 0, 150, 115, 125, 0, 0, trousers, "box"), 1.3, 0.78, 0),
        look(bone("Chest", 0, 1, 0, 330, 118, 150, 0, 0, shirt, "box"), 1.18, 0.66, 0.1),
        look(bone("Neck", 1, 1, 0, 95, 46, 42, 0, 0, skin, "cylinder"), 1, 1, 0),
        look(bone("Head", 2, 1, 0, 225, 92, 92, 4, 0, skin, "ball", true), 0.84, 1, 0.05),
        look(bone("Upper arm (left)", 1, 0.9, -205, 300, 50, 41, 0, 172, shirt), 1, 1, 0.16),
        look(bone("Forearm (left)", 4, 1, 0, 265, 41, 30, 12, 0, skin), 1, 0.9, 0.14),
        look(bone("Hand (left)", 5, 1, 0, 95, 31, 26, 0, 0, skin, "box"), 1.05, 0.45, 0.1),
        look(bone("Thigh (left)", 0, 0, -100, 450, 80, 54, 0, 178, trousers), 1, 1, 0.1),
        look(bone("Shin (left)", 7, 1, 0, 430, 53, 38, -4, 0, trousers), 1, 1, 0.2),
        look(bone("Foot (left)", 8, 1, 0, 235, 46, 40, 90, 0, boots, "wedge"), 0.85, 1.0, 0),
    };
    npcMirrorBone(n, 4); npcMirrorBone(n, 7);
    n.rootHeight = 920;
    return n;
}

std::string npcToJson(const NpcDef& n) {
    json o;
    o["gunsmithNpc"] = 1;
    o["name"] = n.name;
    o["root"] = {{"height", n.rootHeight}, {"bend", n.rootBend}, {"turn", n.rootTurn}};
    json B = json::array();
    for (auto& b : n.bones)
        B.push_back({{"name", b.name}, {"parent", b.parent}, {"at", b.at}, {"side", b.side}, {"len", b.len}, {"r0", b.r0}, {"r1", b.r1},
                     {"bend", b.bend}, {"turn", b.turn}, {"twist", b.twist}, {"shape", b.shape}, {"color", b.color}, {"vital", b.vital}, {"armor", b.armor}});
    for (size_t i = 0; i < n.bones.size(); i++) {   // (the newer looks only when used, so older games read the file the same)
        const NpcBone& b = n.bones[i];
        if (b.width != 1) B[i]["width"] = b.width;
        if (b.depth != 1) B[i]["depth"] = b.depth;
        if (b.bulge != 0) B[i]["bulge"] = b.bulge;
        if (!b.texture.empty()) { B[i]["texture"] = b.texture; B[i]["texScale"] = b.texScale; }
    }
    o["bones"] = B;
    o["fight"] = {{"health", n.health}, {"speed", n.speed}, {"attack", n.attack}, {"damage", n.damage}, {"reach", n.reach},
                  {"gun", n.gun}, {"sight", n.sight}, {"behaviour", n.behaviour}, {"team", n.team}};
    return o.dump(1);
}

bool npcFromJson(const std::string& text, NpcDef& out, std::string* err) {
    json o = json::parse(text, nullptr, false);
    if (o.is_discarded() || !o.is_object() || !o.contains("bones") || !o["bones"].is_array()) { if (err) *err = "not an NPC file"; return false; }
    auto num = [](const json& j, const char* k, double d) { return j.contains(k) && j[k].is_number() ? j[k].get<double>() : d; };
    auto str = [](const json& j, const char* k, const std::string& d) { return j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : d; };
    NpcDef n;
    n.name = str(o, "name", "NPC");
    if (o.contains("root") && o["root"].is_object()) { auto& r = o["root"]; n.rootHeight = num(r, "height", 950); n.rootBend = num(r, "bend", 0); n.rootTurn = num(r, "turn", 0); }
    for (auto& jb : o["bones"]) {
        if (!jb.is_object()) continue;
        NpcBone b;
        b.name = str(jb, "name", "Bone"); b.parent = (int)num(jb, "parent", -1); b.at = num(jb, "at", 1); b.side = num(jb, "side", 0);
        b.len = std::max(1.0, num(jb, "len", 200)); b.r0 = std::max(0.5, num(jb, "r0", 40)); b.r1 = std::max(0.5, num(jb, "r1", 35));
        b.bend = num(jb, "bend", 0); b.turn = num(jb, "turn", 0); b.twist = num(jb, "twist", 0);
        b.shape = str(jb, "shape", "capsule"); b.color = str(jb, "color", "#8a8f98");
        b.vital = jb.contains("vital") && jb["vital"].is_boolean() && jb["vital"].get<bool>(); b.armor = std::clamp(num(jb, "armor", 0), 0.0, 1.0);
        b.width = std::clamp(num(jb, "width", 1), 0.05, 10.0); b.depth = std::clamp(num(jb, "depth", 1), 0.05, 10.0);
        b.bulge = std::clamp(num(jb, "bulge", 0), -0.9, 3.0);
        b.texture = str(jb, "texture", ""); b.texScale = std::clamp(num(jb, "texScale", 150), 1.0, 5000.0);
        n.bones.push_back(b);
    }
    if (n.bones.empty()) { if (err) *err = "it has no bones"; return false; }
    n.bones[0].parent = -1;
    for (size_t i = 1; i < n.bones.size(); i++) if (n.bones[i].parent < 0 || n.bones[i].parent >= (int)i) n.bones[i].parent = 0;
    if (o.contains("fight") && o["fight"].is_object()) {
        auto& f = o["fight"];
        n.health = num(f, "health", 100); n.speed = num(f, "speed", 1.4); n.attack = str(f, "attack", "melee"); n.damage = num(f, "damage", 20);
        n.reach = num(f, "reach", 1.5); n.gun = str(f, "gun", ""); n.sight = num(f, "sight", 30); n.behaviour = str(f, "behaviour", "chase"); n.team = (int)num(f, "team", 1);
    }
    out = n;
    return true;
}
