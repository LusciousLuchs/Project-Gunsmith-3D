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
            for (int k = 0; k < 3; k++) a[k] = p.a[k] + (p.b[k] - p.a[k]) * t + p.x[k] * b.side + p.z[k] * b.fwd;
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
        if (b.fwd != 0) B[i]["fwd"] = b.fwd;
        if (b.gen) B[i]["gen"] = true;
    }
    o["bones"] = B;
    o["fight"] = {{"health", n.health}, {"speed", n.speed}, {"attack", n.attack}, {"damage", n.damage}, {"reach", n.reach},
                  {"gun", n.gun}, {"sight", n.sight}, {"behaviour", n.behaviour}, {"team", n.team}};
    if (n.armorBar > 0) o["fight"]["armor"] = n.armorBar;
    if (n.look.on) {
        const CharLook& L = n.look;
        o["look"] = {{"body", L.body}, {"height", L.height}, {"build", L.build}, {"skin", L.skin}, {"eyes", L.eyes}, {"hair", L.hair},
                     {"hairColor", L.hairColor}, {"top", L.top}, {"topColor", L.topColor}, {"pants", L.pants}, {"pantsColor", L.pantsColor},
                     {"shoes", L.shoes}, {"belt", L.belt}, {"hat", L.hat}, {"headphones", L.headphones}, {"glasses", L.glasses},
                     {"beltColor", L.beltColor}, {"hatColor", L.hatColor}, {"headphonesColor", L.headphonesColor}, {"glassesColor", L.glassesColor}};
    }
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
        b.fwd = num(jb, "fwd", 0);
        b.gen = jb.contains("gen") && jb["gen"].is_boolean() && jb["gen"].get<bool>();
        n.bones.push_back(b);
    }
    if (n.bones.empty()) { if (err) *err = "it has no bones"; return false; }
    n.bones[0].parent = -1;
    for (size_t i = 1; i < n.bones.size(); i++) if (n.bones[i].parent < 0 || n.bones[i].parent >= (int)i) n.bones[i].parent = 0;
    if (o.contains("fight") && o["fight"].is_object()) {
        auto& f = o["fight"];
        n.health = num(f, "health", 100); n.speed = num(f, "speed", 1.4); n.attack = str(f, "attack", "melee"); n.damage = num(f, "damage", 20);
        n.reach = num(f, "reach", 1.5); n.gun = str(f, "gun", ""); n.sight = num(f, "sight", 30); n.behaviour = str(f, "behaviour", "chase"); n.team = (int)num(f, "team", 1);
        n.armorBar = std::max(0.0, num(f, "armor", 0));
    }
    if (o.contains("look") && o["look"].is_object()) {
        auto& j = o["look"];
        CharLook& L = n.look;
        const CharLook d;
        auto flag = [&](const char* k, bool def) { return j.contains(k) && j[k].is_boolean() ? j[k].get<bool>() : def; };
        L.on = true;
        L.body = str(j, "body", d.body); L.height = std::clamp(num(j, "height", 1), 0.85, 1.15); L.build = std::clamp(num(j, "build", 1), 0.8, 1.3);
        L.skin = str(j, "skin", d.skin); L.eyes = str(j, "eyes", d.eyes); L.hair = str(j, "hair", d.hair); L.hairColor = str(j, "hairColor", d.hairColor);
        L.top = str(j, "top", d.top); L.topColor = str(j, "topColor", d.topColor); L.pants = str(j, "pants", d.pants); L.pantsColor = str(j, "pantsColor", d.pantsColor);
        L.shoes = str(j, "shoes", d.shoes);
        L.belt = flag("belt", d.belt); L.hat = flag("hat", d.hat); L.headphones = flag("headphones", d.headphones); L.glasses = flag("glasses", d.glasses);
        L.beltColor = str(j, "beltColor", d.beltColor); L.hatColor = str(j, "hatColor", d.hatColor);
        L.headphonesColor = str(j, "headphonesColor", d.headphonesColor); L.glassesColor = str(j, "glassesColor", d.glassesColor);
    }
    out = n;
    return true;
}

bool npcCoreBone(const std::string& name) {
    static std::vector<std::string> core;
    if (core.empty()) for (auto& b : npcPreset("human").bones) core.push_back(b.name);
    return std::find(core.begin(), core.end(), name) != core.end();
}

// ---------------------------------------------------------------- the character maker
static const double HAND_TWIST = -90, CURL = 1, THUMB_TURN = 12;   // the palm toward the thigh, the fingers a little curled
NpcDef characterFromLook(const CharLook& L, const NpcDef* keep) {
    NpcDef n = npcPreset("human");
    n.look = L; n.look.on = true;
    const bool fem = L.body == "female";
    const double H = std::clamp(L.height, 0.85, 1.15), B = std::clamp(L.build, 0.8, 1.3);
    auto find = [&](const char* nm) { for (int i = 0; i < (int)n.bones.size(); i++) if (n.bones[i].name == nm) return i; return -1; };
    // the body: the human's bones, sized and coloured
    for (auto& b : n.bones) {
        b.len *= H; b.side *= H;
        double rk = B * (fem ? 0.9 : 1.0);
        b.r0 *= rk; b.r1 *= rk;
        b.vital = false; b.armor = 0;
    }
    n.rootHeight *= H;
    auto I = [&](const char* nm) -> NpcBone& { return n.bones[find(nm)]; };
    I("Head").vital = true;   // (where a hit hurts most, as for any NPC)
    I("Neck").color = I("Head").color = L.skin;
    I("Chest").color = L.topColor;
    I("Pelvis").color = L.pantsColor;
    if (fem) {
        I("Chest").r0 *= 0.95; I("Chest").r1 *= 0.86; I("Chest").width = 1.12;
        I("Pelvis").r0 *= 1.06; I("Pelvis").r1 *= 1.1; I("Pelvis").width = 1.38;
        I("Head").r0 *= 0.95; I("Head").r1 *= 0.95; I("Head").len *= 0.96;
        I("Neck").r0 *= 0.85; I("Neck").r1 *= 0.85;
    }
    for (const char* sd : {"left", "right"}) {
        std::string s = std::string("(") + sd + ")";
        auto& ua = I(("Upper arm " + s).c_str()); auto& fa = I(("Forearm " + s).c_str()); auto& hd = I(("Hand " + s).c_str());
        auto& th = I(("Thigh " + s).c_str()); auto& sh = I(("Shin " + s).c_str()); auto& ft = I(("Foot " + s).c_str());
        if (fem) { ua.side *= 0.9; th.side *= 1.05; }
        ua.color = L.top == "longsleeve" ? L.topColor : L.skin;
        fa.color = L.top == "longsleeve" ? L.topColor : L.skin;
        hd.color = L.skin;
        th.color = L.pantsColor;
        sh.color = L.pants == "shorts" ? L.skin : L.pantsColor;
        ft.color = L.shoes;
        hd.len = 88 * H;   // the palm: the fingers carry on from its end
        hd.twist = (std::string(sd) == "left" ? 1 : -1) * HAND_TWIST;   // the palm toward the thigh
    }
    for (auto& b : n.bones) b.gen = true;
    // more pieces: hang one off a bone by name
    auto add = [&](const char* parent, NpcBone b) { b.parent = find(parent); b.gen = true; n.bones.push_back(b); return (int)n.bones.size() - 1; };
    auto piece = [&](const char* name, double at, double side, double fwd, double len, double r0, double r1, double bend, double turn,
                     const std::string& color, const char* shape, double width = 1, double depth = 1) {
        NpcBone b; b.name = name; b.at = at; b.side = side * H; b.fwd = fwd * H; b.len = len * H; b.r0 = r0 * H; b.r1 = r1 * H;
        b.bend = bend; b.turn = turn; b.color = color; b.shape = shape; b.width = width; b.depth = depth;
        return b;
    };
    const NpcBone& head = I("Head");
    const double hr = (head.r0 + head.r1) / 2 / H;   // the head's radius (before height), front to back
    const double hw = hr * head.width;               // ...and side to side
    // the face: eyes (white, then the coloured middle), a nose (narrow at the bridge, wide at the bottom), a mouth;
    // each on the head's surface: how far its front is from its middle line at t along it (before height)
    auto front = [&](double t, double across) {
        double ph = 3.14159265358979 * t, R = hr * std::sin(ph) * (1 + head.bulge * std::sin(ph));
        double u = std::clamp(across / std::max(1.0, R * head.width), -0.95, 0.95);
        return R * head.depth * std::sqrt(1 - u * u);
    };
    for (int s : {-1, 1}) {
        double x = s * 0.36 * hw;
        int e = add("Head", piece(s < 0 ? "Eye (left)" : "Eye (right)", 0.56, x, front(0.56, x) - 13, 22, 12, 12, 90, 0, "#eeeae4", "ball"));
        NpcBone iris = piece(s < 0 ? "Iris (left)" : "Iris (right)", 0.62, 0, 0, 10, 6.5, 6.5, 0, 0, L.eyes, "ball");
        iris.parent = e; iris.gen = true; n.bones.push_back(iris);
    }
    add("Head", piece("Nose", 0.53, 0, front(0.45, 0) - 4, 46, 7, 13, 180, 0, L.skin, "box", 1.0, 1.6));
    add("Head", piece("Mouth", 0.3, 0, front(0.3, 0) - 3, 6, 15, 15, 90, 0, "#9a5a52", "cylinder", 1.7, 0.3));
    // the chest
    if (fem)
        for (int s : {-1, 1}) add("Chest", piece(s < 0 ? "Bust (left)" : "Bust (right)", 0.58, s * 46, 32, 92, 52, 52, 90, s * 8, L.topColor, "ball", 1, 1));
    // sleeves: a t-shirt's cover the top of the arm
    if (L.top == "tshirt")
        for (const char* sd : {"left", "right"}) {
            std::string nm = std::string("Upper arm (") + sd + ")";
            const NpcBone& ua = I(nm.c_str());
            add(nm.c_str(), piece((std::string("Sleeve (") + sd + ")").c_str(), 0, 0, 0, ua.len / H * 0.45, ua.r0 / H + 6, ua.r0 / H + 3, 0, 0, L.topColor, "cylinder"));
        }
    // fingers: three pieces each, from the end of the palm, a little curled; the thumb from the side of the palm
    for (const char* sd : {"left", "right"}) {
        std::string hn = std::string("Hand (") + sd + ")";
        const double sg = std::string(sd) == "left" ? 1 : -1;   // (which way across the palm is the thumb's side)
        const char* names[4] = {"Index", "Middle", "Ring", "Little"};
        const double lens[4][3] = {{40, 24, 20}, {44, 28, 21}, {41, 27, 20}, {32, 20, 18}};
        const double across[4] = {21, 7, -7, -20};
        for (int f = 0; f < 4; f++) {
            int prev = find(hn.c_str());
            double r = f == 3 ? 7.5 : 8.5;
            for (int k = 0; k < 3; k++) {
                NpcBone b = piece((std::string(names[f]) + " " + std::to_string(k + 1) + " (" + sd + ")").c_str(), k == 0 ? 1 : 1, k == 0 ? sg * across[f] : 0, 0,
                                  lens[f][k], r - k * 0.9, r - (k + 1) * 0.9, CURL * (k == 0 ? 12 : 18), 0, L.skin, "cylinder");
                b.parent = prev; b.gen = true; n.bones.push_back(b);
                prev = (int)n.bones.size() - 1;
            }
        }
        int prev = find(hn.c_str());
        const double tl[3] = {36, 30, 24};
        for (int k = 0; k < 3; k++) {
            NpcBone b = piece((std::string("Thumb ") + std::to_string(k + 1) + " (" + sd + ")").c_str(), k == 0 ? 0.25 : 1, k == 0 ? sg * 27 : 0, k == 0 ? 6 : 0,
                              tl[k], 10 - k * 1.1, 9 - k * 1.1, CURL * (k == 0 ? 20 : 10), k == 0 ? sg * THUMB_TURN : 0, L.skin, "cylinder");
            b.parent = prev; b.gen = true; n.bones.push_back(b);
            prev = (int)n.bones.size() - 1;
        }
    }
    // hair
    const std::string& hc = L.hairColor;
    if (L.hair != "none") {
        double grow = L.hair == "buzz" ? 3 : 8;
        add("Head", piece("Hair", 0.14, 0, -hr * 0.17, head.len / H * 0.9 + grow, hr + grow, hr + grow, 0, 0, hc, "ball", head.width * 1.04, 1.0));
        if (L.hair == "long") add("Head", piece("Long hair", 0.78, 0, -hr * 0.62, 250, hr * 0.72, hr * 0.55, 180, 0, hc, "box", 1.25, 0.38));
        if (L.hair == "ponytail") add("Head", piece("Ponytail", 0.68, 0, -hr * 0.95, 190, 22, 12, 150, 0, hc, "capsule"));
        if (L.hair == "bun") add("Head", piece("Bun", 0.86, 0, -hr * 0.8, 70, 34, 34, -50, 0, hc, "ball"));
    }
    // extras
    if (L.belt) {
        const NpcBone& pv = I("Pelvis");
        add("Pelvis", piece("Belt", 0.72, 0, 0, 34, pv.r1 / H * 1.06 + 3, pv.r1 / H * 1.06 + 3, 0, 0, L.beltColor, "box", pv.width * 1.0, pv.depth * 1.08));
        add("Pelvis", piece("Buckle", 0.77, 0, pv.r1 / H * pv.depth + 2, 6, 15, 15, 90, 0, "#b8a46a", "box", 1.3, 1));
    }
    if (L.hat) {
        add("Head", piece("Cap", 0.3, 0, -hr * 0.12, head.len / H * 0.78, hr + 14, hr + 14, 0, 0, L.hatColor, "ball", head.width * 1.04, 1.0));
        add("Head", piece("Cap brim", 0.62, 0, hr * 0.78, 70, 62, 52, 98, 0, L.hatColor, "box", 1.45, 0.1));
    }
    if (L.headphones) {
        // two cups over the ears, a band from each up over the top of the head
        const double Lh = head.len / H, y0 = 0.48 * Lh + 30, x0 = hw + 18, x1 = hw * 0.45, y1 = Lh * 0.985 + 20;
        const double bl = std::hypot(x0 - x1, y1 - y0), lean = std::atan2(x0 - x1, y1 - y0) * 57.2957795;
        for (int s : {-1, 1}) {
            add("Head", piece(s < 0 ? "Ear cup (left)" : "Ear cup (right)", 0.48, s * (hw - 4), -4, 28, 40, 40, 0, s * 90, L.headphonesColor, "cylinder"));
            add("Head", piece(s < 0 ? "Headband (left)" : "Headband (right)", y0 / Lh, s * x0, -4, bl, 8, 8, 0, -s * lean, L.headphonesColor, "cylinder", 1, 2.2));
        }
        add("Head", piece("Headband", y1 / Lh, -x1, -4, 2 * x1, 8, 8, 0, 90, L.headphonesColor, "cylinder", 1, 2.2));
    }
    if (L.glasses) {
        for (int s : {-1, 1}) {
            double x = s * 0.36 * hw;
            add("Head", piece(s < 0 ? "Lens (left)" : "Lens (right)", 0.56, x, front(0.56, x) + 6, 4, 21, 21, 90, 0, L.glassesColor, "cylinder"));
            add("Head", piece(s < 0 ? "Glasses arm (left)" : "Glasses arm (right)", 0.57, s * (hw * 0.97), front(0.56, x) + 4, hr * 1.0, 3, 3, -90, 0, L.glassesColor, "cylinder"));
        }
        add("Head", piece("Glasses bridge", 0.57, -0.16 * hw, front(0.56, 0.36 * hw) + 8, 0.32 * hw, 4, 4, 0, 90, L.glassesColor, "cylinder"));
    }
    // your own gear, kept: on the bone of the same name (else the chest)
    if (keep)
        for (size_t i = 0; i < keep->bones.size(); i++) {
            const NpcBone& g = keep->bones[i];
            if (g.gen || npcCoreBone(g.name) || i == 0) continue;
            NpcBone b = g;
            const std::string pn = g.parent >= 0 && g.parent < (int)keep->bones.size() ? keep->bones[g.parent].name : "Chest";
            int p = find(pn.c_str());
            b.parent = p >= 0 ? p : find("Chest");
            n.bones.push_back(b);
        }
    n.name = keep ? keep->name : "My character";
    return n;
}
