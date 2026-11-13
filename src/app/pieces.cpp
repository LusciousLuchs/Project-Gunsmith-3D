// Gunsmith3D - pieces (see pieces.hpp).
#include "app/pieces.hpp"
#include "kits.hpp"
#include "app/app.hpp"
#include "app/view.hpp"
#include "app/paths.hpp"
#include "carve.hpp"
#include "raylib.h"
#include "json.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

std::string pieceText(int bi) {
    const Design& D = A.design;
    if (bi < 0 || bi >= (int)D.bodies.size()) return "";
    Design sub;
    std::set<std::string> ids;
    for (int k : weldedWith(D, bi)) { sub.bodies.push_back(D.bodies[k]); ids.insert(D.bodies[k].id); }
    for (auto& j : D.joints) {  // the joints inside the piece (and pins or slots to the world)
        bool a = ids.count(j.a), b = ids.count(j.b) || j.b == "world";
        if (a && b) { JointDef c = j; if (!c.rel.empty() && !ids.count(c.rel)) c.rel.clear(); sub.joints.push_back(c); }
    }
    sub.name = D.bodies[bi].name;
    json o = json::parse(designToJson(sub));
    o["gunsmithPiece"] = 1;
    return o.dump(1);
}

bool insertPiece(const std::string& text, double dx, double dy) {
    json o;
    try { o = json::parse(text); } catch (...) { return false; }
    if (!o.is_object() || !o.contains("gunsmithPiece")) return false;
    Design sub; std::string err;
    if (!designFromJson(o.dump(), sub, &err)) return false;
    if (sub.bodies.empty()) return false;
    Design& D = A.design;
    std::map<std::string, std::string> nid, ntag;
    std::set<std::string> names;
    for (auto& b : D.bodies) names.insert(b.name);
    std::map<std::string, int> mirrorCount;
    for (auto& b : sub.bodies) if (!b.mirror.empty()) mirrorCount[b.mirror]++;
    for (auto& b : sub.bodies) {
        if (!b.mirror.empty() && mirrorCount[b.mirror] != 2) b.mirror.clear();   // half a pair is just a part
        if (!b.mirror.empty() && !ntag.count("mirror:" + b.mirror)) ntag["mirror:" + b.mirror] = "m-" + uid();
        nid[b.id] = uid();
        if (!b.weld.empty() && !ntag.count(b.weld)) ntag[b.weld] = "w-" + uid();
    }
    int first = (int)D.bodies.size();
    for (auto b : sub.bodies) {
        b.id = nid[b.id];
        if (!b.weld.empty()) b.weld = ntag[b.weld];
        if (!b.mirror.empty()) b.mirror = ntag["mirror:" + b.mirror];
        for (auto& p : b.pts) { p.x += dx; p.y += dy; }
        for (auto& p : b.roundPath) { p.x += dx; p.y += dy; }
        carve::moveCarves(b, dx, dy);
        if (b.ctrl != "none") { b.ctrlPt.x += dx; b.ctrlPt.y += dy; }
        if (b.magazine && b.gen.on) { b.gen.top.x += dx; b.gen.top.y += dy; }
        if (names.count(b.name)) b.name += " (2)";
        D.bodies.push_back(b);
    }
    auto mv = [&](V2& p) { p.x += dx; p.y += dy; };
    for (auto j : sub.joints) {
        j.a = nid.count(j.a) ? nid[j.a] : j.a;
        j.b = nid.count(j.b) ? nid[j.b] : j.b;
        if (!j.rel.empty()) j.rel = nid.count(j.rel) ? nid[j.rel] : "";
        mv(j.p); mv(j.pa); mv(j.pb);
        for (auto& q : j.path) mv(q);
        D.joints.push_back(j);
    }
    A.sel = {1, first, -1}; A.selVs.clear(); A.selCarve = -1;
    commit();
    return true;
}

void copySelection() {
    if (A.sel.kind != 1 || A.sel.idx >= (int)A.design.bodies.size()) { toast("Select a part to copy."); return; }
    std::string t = pieceText(A.sel.idx);
    SetClipboardText(t.c_str());
    size_t n = weldedWith(A.design, A.sel.idx).size();
    toast(n > 1 ? "Copied " + A.design.bodies[A.sel.idx].name + " and the " + std::to_string(n - 1) + " parts welded to it. Ctrl+V pastes, here or in another gun."
                : "Copied " + A.design.bodies[A.sel.idx].name + ". Ctrl+V pastes, here or in another gun.");
}
void pasteClipboard() {
    const char* t = GetClipboardText();
    if (!t || !insertPiece(t, 10, 10)) { toast("Nothing to paste: copy a part first (Ctrl+C)."); return; }
    toast("Pasted. It's selected: drag it where you want it.");
}

bool savePieceToLibrary(int bi, const std::string& name) {
    std::string t = pieceText(bi);
    if (t.empty()) return false;
    std::error_code ec;
    fs::create_directories(PARTS_DIR, ec);
    std::ofstream f(fs::path(PARTS_DIR) / (safeFileName(name) + ".json"));
    if (!f) return false;
    f << t;
    return true;
}
std::vector<std::string> libraryParts() {
    std::vector<std::string> out;
    std::error_code ec;
    if (fs::is_directory(PARTS_DIR, ec))
        for (auto& e : fs::directory_iterator(PARTS_DIR, ec)) if (e.path().extension() == ".json") out.push_back(e.path().stem().string());
    std::sort(out.begin(), out.end());
    return out;
}
bool insertLibraryPart(const std::string& name) {
    std::ifstream in(fs::path(PARTS_DIR) / (name + ".json"));
    if (!in) return false;
    std::stringstream ss; ss << in.rdbuf();
    return insertPiece(ss.str(), 0, 0);   // where it was saved: drag it into place
}

// An action kit into the gun: at its breech face if it has one (else where the kit's example has it). The gun's own
// barrel parts stop being barrels (the kit brings its own chamber and bore); everything of the player's stays.
bool insertActionKit(const std::string& key) {
    Design kit = kitDesign(key);
    if (kit.bodies.empty()) return false;
    Design& D = A.design;
    V2 at = D.breech.on && D.breech.hasFace ? D.breech.face : kit.breech.face;
    if (untouchedBlank(D)) D.bodies.clear();   // a new gun that's still just the starting bar: the kit takes its place
    int unbarrelled = 0;
    for (auto& b : D.bodies) if (b.barrel.on) { b.barrel.on = false; unbarrelled++; }
    int first = insertKit(D, kit, at);
    if (first < 0) return false;
    A.sel = {1, first, -1}; A.selVs.clear(); A.selCarve = -1;
    commit();
    fitView();   // (the whole gun in view, kit and all)
    std::string label;
    for (auto& k : actionKits()) if (key == k.key) label = k.label;
    toast("Added the " + label + " action: " + std::to_string(kit.bodies.size()) + " parts, and the gun's breech is now the kit's."
          + (unbarrelled ? " Your barrel part is an ordinary part now (the kit has its own)." : "")
          + " Move or reshape anything of yours in its way. Undo takes it out.");
    return true;
}
