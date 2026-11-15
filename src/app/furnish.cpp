// Gunsmith3D - Furniture, props and guns placed on maps (see furnish.hpp).
#include "app/furnish.hpp"
#include "app/mapgfx.hpp"
#include "app/paths.hpp"
#include "app/render.hpp"
#include "app/app.hpp"
#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>

namespace fs = std::filesystem;
using gmap::V3;

static std::string readFile(const fs::path& p) { std::ifstream in(p); std::stringstream ss; ss << in.rdbuf(); return ss.str(); }

std::vector<FurnChoice> furnitureChoices(bool props) {
    std::vector<FurnChoice> out;
    for (auto& f : props ? gmap::builtinProps() : gmap::builtinFurniture()) out.push_back({"builtin:" + f.key, f.label});
    std::error_code ec;
    std::vector<FurnChoice> mine;
    for (auto& e : fs::directory_iterator(FURNITURE_DIR, ec)) {
        if (e.path().extension() != ".json") continue;
        gmap::Entity x;
        if (!gmap::entityFromJson(readFile(e.path()), x)) continue;
        if ((x.type == "prop") != props) continue;
        mine.push_back({"user:" + e.path().stem().string(), x.ref.empty() ? e.path().stem().string() : x.ref});
    }
    std::sort(mine.begin(), mine.end(), [](const FurnChoice& a, const FurnChoice& b) { return a.label < b.label; });
    out.insert(out.end(), mine.begin(), mine.end());
    if (props) {
        for (auto& p : presetList()) if (std::string(p.key) != "blank") out.push_back({std::string("gun:preset:") + p.key, std::string("Gun: ") + p.label});
        std::vector<FurnChoice> guns;
        for (auto& e : fs::directory_iterator(GUNS_DIR, ec))
            if (e.path().extension() == ".json") guns.push_back({"gun:" + e.path().stem().string(), "Gun: " + e.path().stem().string()});
        std::sort(guns.begin(), guns.end(), [](const FurnChoice& a, const FurnChoice& b) { return a.label < b.label; });
        out.insert(out.end(), guns.begin(), guns.end());
    }
    return out;
}
bool furnitureFromPick(const std::string& pick, gmap::Entity& e) {
    if (pick.rfind("builtin:", 0) == 0) {
        std::string k = pick.substr(8);
        for (auto* list : {&gmap::builtinFurniture(), &gmap::builtinProps()})
            for (auto& f : *list) if (f.key == k) { e = f.e; return true; }
        return false;
    }
    if (pick.rfind("user:", 0) == 0) {
        if (!gmap::entityFromJson(readFile(fs::path(FURNITURE_DIR) / (pick.substr(5) + ".json")), e)) return false;
        e.pos = {0, 0, 0}; e.yaw = 0;
        return true;
    }
    if (pick.rfind("gun:", 0) == 0) {
        e = gmap::Entity(); e.type = "gun_item"; e.ref = pick.substr(4);
        return gunItemDesign(e.ref) != nullptr;
    }
    return false;
}
bool saveFurniture(const gmap::Entity& e0, std::string& file) {
    gmap::Entity e = e0;
    e.pos = {0, 0, 0}; e.yaw = 0;   // (kept round its own floor, facing +X: placed again turned as you like)
    std::error_code ec;
    fs::create_directories(FURNITURE_DIR, ec);
    file = safeFileName(e.ref.empty() ? (e.type == "prop" ? "My prop" : "My furniture") : e.ref);
    std::ofstream out(fs::path(FURNITURE_DIR) / (file + ".json"));
    if (!out) return false;
    out << gmap::entityToJson(e);
    return (bool)out;
}

// ---------------------------------------------------------------- guns on maps
const Design* gunItemDesign(const std::string& ref) {
    static std::map<std::string, std::unique_ptr<Design>> cache;
    auto it = cache.find(ref);
    if (it != cache.end()) return it->second.get();
    std::unique_ptr<Design> d;
    if (ref.rfind("preset:", 0) == 0) {
        std::string k = ref.substr(7);
        for (auto& p : presetList()) if (k == p.key) { d = std::make_unique<Design>(loadPreset(k)); break; }
    } else {
        std::string text = readFile(fs::path(GUNS_DIR) / (ref + ".json"));
        Design x;
        if (!text.empty() && designFromJson(text, x) && !x.npcGun.on) d = std::make_unique<Design>(std::move(x));
    }
    if (d) syncBarrels(*d);
    const Design* r = d.get();
    cache[ref] = std::move(d);
    return r;
}
std::string gunItemLabel(const std::string& ref) {
    if (ref.rfind("preset:", 0) == 0) { for (auto& p : presetList()) if (ref.substr(7) == p.key) return p.label; }
    return ref;
}
static void designBox(const Design& d, double& x0, double& x1, double& y0, double& y1, double& z0, double& z1) {
    x0 = y0 = z0 = 1e9; x1 = y1 = z1 = -1e9;
    for (auto& b : d.bodies) {
        for (auto& q : b.pts) { x0 = std::min(x0, q.x); x1 = std::max(x1, q.x); y0 = std::min(y0, q.y); y1 = std::max(y1, q.y); }
        z0 = std::min(z0, b.z - b.thick / 2); z1 = std::max(z1, b.z + b.thick / 2);
    }
    if (x0 > x1) { x0 = y0 = z0 = 0; x1 = y1 = z1 = 1; }
}
double gunItemLength(const Design& d) { double x0, x1, y0, y1, z0, z1; designBox(d, x0, x1, y0, y1, z0, z1); return (x1 - x0) * 0.001; }
Matrix entityMatrix(V3 pos, double yawDeg) {
    return MatrixMultiply(MatrixRotateY((float)(-yawDeg * DEG2RAD)), MatrixTranslate((float)pos.x, (float)pos.y, (float)pos.z));
}
Matrix gunItemMatrix(const Design& d, V3 pos, double yawDeg) {
    double x0, x1, y0, y1, z0, z1;
    designBox(d, x0, x1, y0, y1, z0, z1);
    float k = (float)(0.001 / U);   // (drawing units to metres)
    Matrix M = MatrixMultiply(MatrixTranslate(-(float)((x0 + x1) / 2) * U, (float)((y0 + y1) / 2) * U, -(float)((z0 + z1) / 2) * U), MatrixScale(k, k, k));
    M = MatrixMultiply(M, MatrixRotateX(-PI / 2));   // (on its side: its near face up)
    M = MatrixMultiply(M, MatrixTranslate(0, (float)((z1 - z0) / 2 * 0.001), 0));
    return MatrixMultiply(M, entityMatrix(pos, yawDeg));
}

// ---------------------------------------------------------------- drawing
static void edges(const gmap::Brush& b, Color c) {
    for (size_t i = 0; i < b.faces.size(); i++) {
        auto p = gmap::facePolygon(b, (int)i);
        for (size_t k = 0; k < p.size(); k++) {
            V3 a = p[k], q = p[(k + 1) % p.size()];
            DrawLine3D({(float)a.x, (float)a.y, (float)a.z}, {(float)q.x, (float)q.y, (float)q.z}, c);
        }
    }
}
static void drawThing(const gmap::Entity& e, bool sel, int idx) {
    const Color hi{255, 205, 60, 255};
    if (e.type == "prop") {
        mapDrawBrushes(e.parts, entityMatrix(e.pos, e.yaw));
        if (sel) for (auto& p : e.parts) edges(gmap::placedBrush(e, p), hi);
    } else if (e.type == "gun_item") {
        const Design* d = gunItemDesign(e.ref);
        if (!d) { DrawCubeWires({(float)e.pos.x, (float)e.pos.y + 0.03f, (float)e.pos.z}, 0.3f, 0.06f, 0.3f, RED); return; }
        drawDesignModel("mapgun:" + std::to_string(idx) + ":" + e.ref, *d, gunItemMatrix(*d, e.pos, e.yaw));
        if (sel) {
            double L = gunItemLength(*d);
            V3 f{std::cos(e.yaw * DEG2RAD), 0, std::sin(e.yaw * DEG2RAD)};
            V3 a = e.pos - f * (L / 2), b = e.pos + f * (L / 2);
            DrawLine3D({(float)a.x, (float)a.y + 0.002f, (float)a.z}, {(float)b.x, (float)b.y + 0.002f, (float)b.z}, hi);
            DrawSphere({(float)b.x, (float)b.y + 0.01f, (float)b.z}, 0.012f, hi);
        }
    }
}
void drawMapThings(const gmap::Map& m, int selEnt) {
    int idx = 0;
    for (size_t i = 0; i < m.ents.size(); i++) {
        const auto& e = m.ents[i];
        bool sel = (int)i == selEnt;
        if (e.type == "prop" || e.type == "gun_item") drawThing(e, sel, idx++);
        else if (e.type == "furniture") {
            for (auto& k : gmap::placedItems(e)) drawThing(k, false, idx++);
            if (sel) for (auto& b : m.brushes) if (b.furn == (int)i + 1) edges(b, Color{255, 205, 60, 255});
        }
    }
}
