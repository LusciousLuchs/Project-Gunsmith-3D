// Gunsmith3D - The fitting bench (see fitbench.hpp).
#include "app/fitbench.hpp"
#include "app/paths.hpp"
#include "app/view.hpp"
#include "app/fonts.hpp"
#include "app/units.hpp"
#include "imgui.h"
#include "raymath.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

std::vector<std::string> attachmentFiles() {
    std::vector<std::string> out;
    std::error_code ec;
    if (fs::exists(ATTACH_DIR, ec))
        for (auto& e : fs::directory_iterator(ATTACH_DIR, ec)) if (e.path().extension() == ".json") out.push_back(e.path().stem().string());
    std::sort(out.begin(), out.end());
    return out;
}
bool loadAttachment(const std::string& file, Design& out) {
    std::ifstream in(fs::path(ATTACH_DIR) / (file + ".json"));
    if (!in) return false;
    std::stringstream ss; ss << in.rdbuf();
    Design d;
    if (!designFromJson(ss.str(), d) || !d.attach.on) return false;
    autoAttach(d);
    if (d.name.empty()) d.name = file;
    out = d;
    return true;
}
double attachmentMass(const Design& att) {
    double g = 0;
    for (auto& b : att.bodies) {
        if (b.mass > 0) { g += b.mass; continue; }
        double a = 0;
        for (size_t i = 0; i < b.pts.size(); i++) { const V2 &p = b.pts[i], &q = b.pts[(i + 1) % b.pts.size()]; a += p.x * q.y - q.x * p.y; }
        double area = std::fabs(a) / 2;
        g += (b.round ? area * b.thick * 0.785 : area * b.thick) * 0.0027;   // aluminium unless a part has its own weight (a turned part: about pi/4 of its box)
    }
    return g;
}

void enterFitting() { A.fitting = true; A.posing = false; A.running = false; refitAll(); }
void leaveFitting() { A.fitting = false; A.fitPick.clear(); A.fitHover.on = false; A.fitSel = -1; }

// ---------------------------------------------------------------- where things are
static fit::Site* siteByKey(std::vector<fit::Site>& ss, const std::string& k) { for (auto& s : ss) if (s.key() == k) return &s; return nullptr; }
void refitAll() {   // each one again where it was, in order (adapters first, as they were fitted)
    std::vector<fit::Item> kept;
    for (auto& it : A.fitItems) {
        auto ss = fit::sites(A.design, kept);
        fit::Site* s = siteByKey(ss, it.site);
        if (!s) continue;   // (its mount is gone from the gun)
        fit::Item n = it;
        n.along = s->rail ? fit::snapAlong(*s, it.along) : 0;
        n.xf = fit::place(n.att, *s, n.along);
        kept.push_back(n);
    }
    A.fitItems = kept;
    if (A.fitSel >= (int)A.fitItems.size()) A.fitSel = -1;
}
std::string fitCarrier(const fit::Item& it) {
    // "g:rail:<part>" / "g:pt:<part>:i": that part; "aK:...": whatever carries item K
    if (it.site.rfind("g:", 0) == 0) {
        size_t a = it.site.find(':', 2), b = it.site.find(':', a + 1);
        return it.site.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1);
    }
    int k = std::atoi(it.site.c_str() + 1);
    if (k >= 0 && k < (int)A.fitItems.size() && &A.fitItems[k] != &it) return fitCarrier(A.fitItems[k]);
    return "";
}
std::string siteLabel(const std::string& key) {
    auto ss = fit::sites(A.design, A.fitItems);
    for (auto& s : ss) if (s.key() == key) {
        int bi = s.owner < 0 ? A.design.bodyIndex(s.part) : -1;
        std::string on = s.owner >= 0 && s.owner < (int)A.fitItems.size() ? "on the " + A.fitItems[s.owner].att.name
                                                                           : (bi >= 0 ? "on the " + A.design.bodies[bi].name : "");
        return (s.rail ? std::string("Picatinny rail ") : s.type + " ") + on;
    }
    return "?";
}
Matrix fitMatrix(const fit::Item& it) {
    // design mm (y down) -> world (y up): W = U S p with S = diag(1, -1, 1); on the gun: U S (O + R (p - spot))
    const double s[3] = {1, -1, 1};
    double Q[3][3];
    for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) Q[r][c] = s[r] * it.xf.R[r][c] * s[c];
    fit::P3 sp = it.spot();
    double ws[3] = {sp.x * U, -sp.y * U, sp.z * U}, wo[3] = {it.xf.O.x * U, -it.xf.O.y * U, it.xf.O.z * U}, t[3];
    for (int r = 0; r < 3; r++) t[r] = wo[r] - (Q[r][0] * ws[0] + Q[r][1] * ws[1] + Q[r][2] * ws[2]);
    Matrix M = {(float)Q[0][0], (float)Q[0][1], (float)Q[0][2], (float)t[0],
                (float)Q[1][0], (float)Q[1][1], (float)Q[1][2], (float)t[1],
                (float)Q[2][0], (float)Q[2][1], (float)Q[2][2], (float)t[2],
                0, 0, 0, 1};
    return M;
}

// ---------------------------------------------------------------- the mouse
static Vector2 scr(fit::P3 p) { return toScreen(W3({p.x, p.y}, (float)(p.z * U))); }
void fitInput(bool overUI) {
    A.fitHover.on = false;
    if (A.mode != Mode::Test) return;
    Vector2 m = GetMousePosition();
    if (!A.fitPick.empty()) {   // placing: the nearest slot or matching point under the mouse
        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) || IsKeyPressed(KEY_ESCAPE)) { A.fitPick.clear(); return; }
        if (overUI) return;
        auto ss = fit::sites(A.design, A.fitItems);
        int best = -1; double bestAlong = 0; float bd = 40;
        for (size_t i = 0; i < ss.size(); i++) {
            const fit::Site& s = ss[i];
            if (s.rail) {
                for (double v : s.slots) {
                    fit::P3 q{s.at.x + s.along.x * v, s.at.y + s.along.y * v, s.at.z + s.along.z * v};
                    float d = Vector2Distance(scr(q), m);
                    if (d < bd) { bd = d; best = (int)i; bestAlong = v; }
                }
            } else {
                float d = Vector2Distance(scr(s.at), m);
                if (d < bd) { bd = d; best = (int)i; bestAlong = 0; }
            }
        }
        if (best < 0) return;
        A.fitHover.on = true; A.fitHover.site = best; A.fitHover.along = bestAlong;
        A.fitHover.why = fit::check(A.design, A.fitItems, A.fitPickD, ss[best], bestAlong);
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (!A.fitHover.why.empty()) { toast("It can't go there: " + A.fitHover.why + "."); return; }
            fit::Item it;
            it.file = A.fitPick; it.att = A.fitPickD; it.site = ss[best].key(); it.along = bestAlong;
            it.xf = fit::place(it.att, ss[best], bestAlong);
            A.fitItems.push_back(it);
            A.fitSel = (int)A.fitItems.size() - 1;
            if (!(IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT))) A.fitPick.clear();   // (Shift: fit another)
        }
        return;
    }
    if (overUI || !IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return;
    int best = -1; float bd = 30;   // a fitted one, by where it's mounted
    for (size_t k = 0; k < A.fitItems.size(); k++) {
        float d = Vector2Distance(scr(A.fitItems[k].xf.O), m);
        if (d < bd) { bd = d; best = (int)k; }
    }
    A.fitSel = best;
}

void drawFitOverlay() {
    if (!A.fitting) return;
    auto ss = fit::sites(A.design, A.fitItems);
    bool placing = !A.fitPick.empty();
    for (auto& s : ss) {   // every place to mount things; when placing, the ones that take it stand out
        bool ok = !placing || (s.rail ? A.fitPickD.attach.mount == "Picatinny" : fit::kindsMatch(A.fitPickD.attach.mount, s.type));
        Color c = ok ? Color{120, 220, 255, 230} : Color{120, 130, 140, 110};
        if (s.rail) {
            fit::P3 e{s.at.x + s.along.x * s.length, s.at.y + s.along.y * s.length, s.at.z + s.along.z * s.length};
            DrawLineEx(scr(s.at), scr(e), ok ? 3.f : 1.5f, c);
            for (double v : s.slots) DrawCircleV(scr({s.at.x + s.along.x * v, s.at.y + s.along.y * v, s.at.z + s.along.z * v}), ok ? 2.5f : 1.5f, c);
        } else {
            Vector2 p = scr(s.at);
            Vector2 q[4] = {{p.x, p.y - 6}, {p.x + 6, p.y}, {p.x, p.y + 6}, {p.x - 6, p.y}};
            for (int j = 0; j < 4; j++) DrawLineEx(q[j], q[(j + 1) % 4], 2, c);
            viewText(s.type.c_str(), p.x + 9, p.y - 18, 12, c);
        }
    }
    for (size_t k = 0; k < A.fitItems.size(); k++) {   // the fitted ones: their names where they're mounted
        Vector2 p = scr(A.fitItems[k].xf.O);
        Color c = (int)k == A.fitSel ? Color{255, 190, 70, 255} : Color{230, 233, 238, 200};
        DrawCircleV(p, (int)k == A.fitSel ? 5.f : 3.5f, c);
        viewText(A.fitItems[k].att.name.c_str(), p.x + 8, p.y + 6, 12, c);
    }
    if (placing) {
        Vector2 m = GetMousePosition();
        if (A.fitHover.on) {
            const fit::Site& s = ss[A.fitHover.site];
            fit::P3 q = s.rail ? fit::P3{s.at.x + s.along.x * A.fitHover.along, s.at.y + s.along.y * A.fitHover.along, s.at.z + s.along.z * A.fitHover.along} : s.at;
            bool ok = A.fitHover.why.empty();
            Color c = ok ? Color{110, 230, 140, 255} : Color{255, 100, 80, 255};
            DrawCircleLinesV(scr(q), 9, c);
            viewText(ok ? ("Click to fit the " + A.fitPickD.name + " here").c_str() : A.fitHover.why.c_str(), m.x + 16, m.y + 10, 13, c);
        } else viewText(("Placing the " + A.fitPickD.name + ": point at a rail's slot or a matching point (right-click to stop)").c_str(), m.x + 16, m.y + 10, 13, Color{230, 233, 238, 220});
    }
}

// ---------------------------------------------------------------- loadouts
std::vector<std::string> loadoutFiles() {
    std::vector<std::string> out;
    std::error_code ec;
    if (fs::exists(LOADOUT_DIR, ec))
        for (auto& e : fs::directory_iterator(LOADOUT_DIR, ec)) if (e.path().extension() == ".json") out.push_back(e.path().stem().string());
    std::sort(out.begin(), out.end());
    return out;
}
bool saveLoadout(const std::string& name) {
    std::error_code ec;
    fs::create_directories(LOADOUT_DIR, ec);
    std::ofstream f(fs::path(LOADOUT_DIR) / (safeFileName(name) + ".json"));
    if (!f) return false;
    f << fit::loadoutToJson(A.design.name, A.fitItems);
    return true;
}
bool loadLoadout(const std::string& name) {
    std::ifstream in(fs::path(LOADOUT_DIR) / (name + ".json"));
    if (!in) return false;
    std::stringstream ss; ss << in.rdbuf();
    std::string gun; std::vector<fit::LoadoutEntry> es;
    if (!fit::loadoutFromJson(ss.str(), gun, es)) return false;
    A.fitItems.clear();
    int missing = 0, wontFit = 0;
    for (auto& e : es) {   // each in order (an adapter before what sits on it), checked again: the gun may have changed
        fit::Item it;
        if (!loadAttachment(e.file, it.att)) { missing++; continue; }
        auto sites = fit::sites(A.design, A.fitItems);
        fit::Site* s = siteByKey(sites, e.site);
        if (!s) { wontFit++; continue; }
        double along = s->rail ? fit::snapAlong(*s, e.along) : 0;
        if (!fit::check(A.design, A.fitItems, it.att, *s, along).empty()) { wontFit++; continue; }
        it.file = e.file; it.site = e.site; it.along = along; it.xf = fit::place(it.att, *s, along);
        A.fitItems.push_back(it);
    }
    std::string msg = "Loadout " + name + ": " + std::to_string(A.fitItems.size()) + " fitted";
    if (missing) msg += ", " + std::to_string(missing) + " not in your Attachments folder";
    if (wontFit) msg += ", " + std::to_string(wontFit) + " no longer fit this gun";
    if (gun != A.design.name && !gun.empty()) msg += " (it was saved for " + gun + ")";
    toast(msg + ".");
    return true;
}
