// Gunsmith3D - Furniture and props on maps: blocks kept inside an entity, so a shelf or a table moves and turns as one
// piece, and a prop (a crate, a can) can be picked up. No graphics here.
#include "map.hpp"

#include <algorithm>
#include <cmath>

namespace gmap {

V3 entityToWorld(const Entity& e, V3 p) {
    double y = e.yaw * 3.14159265358979 / 180, c = std::cos(y), s = std::sin(y);
    return {e.pos.x + p.x * c - p.z * s, e.pos.y + p.y, e.pos.z + p.x * s + p.z * c};
}
Brush placedBrush(const Entity& e, const Brush& local) {
    Brush b = local;
    rotateY(b, -e.yaw * 3.14159265358979 / 180, {0, 0, 0});
    translate(b, e.pos);
    b.furn = 0;
    return b;
}
std::vector<Entity> placedItems(const Entity& f) {
    std::vector<Entity> out;
    for (auto k : f.items) {
        k.pos = entityToWorld(f, k.pos);
        k.yaw = std::fmod(k.yaw + f.yaw + 720, 360);
        out.push_back(k);
    }
    return out;
}
void bakeFurniture(Map& m) {
    m.brushes.erase(std::remove_if(m.brushes.begin(), m.brushes.end(), [](const Brush& b) { return b.furn != 0; }), m.brushes.end());
    for (size_t i = 0; i < m.ents.size(); i++) {
        if (m.ents[i].type != "furniture") continue;
        for (auto& p : m.ents[i].parts) {
            Brush b = placedBrush(m.ents[i], p);
            b.furn = (int)i + 1;
            if (brushValid(b)) m.brushes.push_back(b);
        }
    }
}
Entity makeFurniture(const std::vector<Brush>& world, V3 at, const std::string& type, const std::string& name) {
    Entity e;
    e.type = type; e.ref = name; e.pos = at; e.yaw = 0;
    for (auto b : world) { translate(b, -at); b.furn = 0; b.locked = false; e.parts.push_back(b); }
    return e;
}
void entityBounds(const Entity& e, V3& mn, V3& mx) {
    mn = {1e30, 1e30, 1e30}; mx = {-1e30, -1e30, -1e30};
    for (auto& p : e.parts) {
        V3 a, c; brushBounds(placedBrush(e, p), a, c);
        mn = {std::min(mn.x, a.x), std::min(mn.y, a.y), std::min(mn.z, a.z)};
        mx = {std::max(mx.x, c.x), std::max(mx.y, c.y), std::max(mx.z, c.z)};
    }
    if (mn.x > mx.x) { mn = e.pos - V3{0.1, 0, 0.1}; mx = e.pos + V3{0.1, 0.2, 0.1}; }
}
double propMass(const Entity& e) {
    if (e.mass > 0) return e.mass;
    double v = 0;
    for (auto& p : e.parts) { V3 a, c; brushBounds(p, a, c); v += (c.x - a.x) * (c.y - a.y) * (c.z - a.z); }
    return std::clamp(v * 300, 0.05, 200.0);   // (mostly air inside: boxes, cans)
}

// ---------------------------------------------------------------- the built-in pieces
// Each stands on the floor round its middle, its front toward +X (the way you face it once placed facing you).
namespace {
struct Builder {
    Entity e;
    Builder(const std::string& type, const std::string& name) { e.type = type; e.ref = name; }
    void box(V3 a, V3 b, const char* mat) { e.parts.push_back(makeBox(a, b, materialIndex(mat))); }
    void cyl(V3 a, V3 b, int sides, const char* mat) { e.parts.push_back(makeCylinder(a, b, sides, materialIndex(mat))); }
};
Entity shelfUnit() {   // a tall shelf: two sides, a back, a top and four shelves (1 m wide, 0.4 deep, 1.8 tall)
    Builder b("furniture", "Shelf unit");
    const double w = 0.5, d = 0.2, t = 0.02;
    b.box({-d, 0, -w}, {d, 1.8, -w + t}, "plywood");
    b.box({-d, 0, w - t}, {d, 1.8, w}, "plywood");
    b.box({-d, 0, -w + t}, {-d + t, 1.8, w - t}, "plywood");
    for (double y : {0.08, 0.5, 0.92, 1.34, 1.78}) b.box({-d + t, y - t, -w + t}, {d, y, w - t}, "wood_planks");
    return b.e;
}
Entity wallShelf() {   // one plank on two brackets, its back on a wall, 1.5 m up
    Builder b("furniture", "Wall shelf");
    b.box({-0.13, 1.48, -0.6}, {0.13, 1.5, 0.6}, "wood_planks");
    for (double z : {-0.45, 0.45}) b.box({-0.13, 1.3, z - 0.015}, {-0.1, 1.48, z + 0.015}, "steel"), b.box({-0.13, 1.45, z - 0.015}, {0.1, 1.48, z + 0.015}, "steel");
    return b.e;
}
Entity displayTable() {   // a table to lay guns out on: 1.6 x 0.8, 0.9 high
    Builder b("furniture", "Display table");
    b.box({-0.4, 0.86, -0.8}, {0.4, 0.9, 0.8}, "wood_planks");
    for (double x : {-0.35, 0.31}) for (double z : {-0.75, 0.71}) b.box({x, 0, z}, {x + 0.04, 0.86, z + 0.04}, "steel");
    return b.e;
}
Entity workbench() {   // a heavy bench with a shelf under it: 1.8 x 0.7, 0.95 high
    Builder b("furniture", "Workbench");
    b.box({-0.35, 0.89, -0.9}, {0.35, 0.95, 0.9}, "wood_planks");
    b.box({-0.32, 0.2, -0.85}, {0.32, 0.23, 0.85}, "plywood");
    for (double x : {-0.33, 0.27}) for (double z : {-0.88, 0.82}) b.box({x, 0, z}, {x + 0.06, 0.89, z + 0.06}, "wood_planks");
    return b.e;
}
Entity counter() {   // a shop counter: a solid base and a steel top (2 m long, 0.6 deep, 1 m high)
    Builder b("furniture", "Counter");
    b.box({-0.3, 0, -1}, {0.25, 0.96, 1}, "paint_white");
    b.box({-0.32, 0.96, -1.02}, {0.32, 1.0, 1.02}, "steel");
    return b.e;
}
Entity gunRack() {   // stands against a wall: a back board with three pairs of pegs to lay long guns across
    Builder b("furniture", "Gun rack");
    b.box({-0.15, 0, -0.65}, {-0.12, 1.6, 0.65}, "plywood");
    b.box({-0.15, 0, -0.65}, {0.15, 0.04, 0.65}, "plywood");
    for (double y : {0.45, 0.9, 1.35})
        for (double z : {-0.45, 0.45}) b.box({-0.12, y, z - 0.02}, {0.08, y + 0.03, z + 0.02}, "steel");
    return b.e;
}
Entity glassCase() {   // a low cabinet (no real glass on maps yet: a steel frame round an open top)
    Builder b("furniture", "Display cabinet");
    b.box({-0.3, 0, -0.8}, {0.3, 0.8, 0.8}, "wood_planks");
    for (double z : {-0.8, 0.77}) b.box({-0.3, 0.8, z}, {0.3, 1.0, z + 0.03}, "steel");
    for (double x : {-0.3, 0.27}) b.box({x, 0.8, -0.8}, {x + 0.03, 1.0, 0.8}, "steel");
    return b.e;
}

Entity prop(const std::string& name, double mass) { Entity e; e.type = "prop"; e.ref = name; e.mass = mass; return e; }
}  // namespace

const std::vector<Furnishing>& builtinFurniture() {
    static const std::vector<Furnishing> list = {
        {"shelf", "Shelf unit", shelfUnit()}, {"wallshelf", "Wall shelf", wallShelf()}, {"table", "Display table", displayTable()},
        {"workbench", "Workbench", workbench()}, {"counter", "Counter", counter()}, {"gunrack", "Gun rack", gunRack()},
        {"cabinet", "Display cabinet", glassCase()}};
    return list;
}
const std::vector<Furnishing>& builtinProps() {
    static const std::vector<Furnishing> list = [] {
        std::vector<Furnishing> v;
        auto add = [&](const char* key, const char* label, double mass, auto build) { Entity e = prop(label, mass); build(e); v.push_back({key, label, e}); };
        auto box = [](Entity& e, V3 a, V3 b, const char* m) { e.parts.push_back(makeBox(a, b, materialIndex(m))); };
        auto cyl = [](Entity& e, V3 a, V3 b, int n, const char* m) { e.parts.push_back(makeCylinder(a, b, n, materialIndex(m))); };
        add("crate", "Wooden crate", 9, [&](Entity& e) { box(e, {-0.25, 0, -0.25}, {0.25, 0.45, 0.25}, "wood_planks"); });
        add("box", "Cardboard box", 1.2, [&](Entity& e) { box(e, {-0.2, 0, -0.15}, {0.2, 0.3, 0.15}, "plywood"); });
        add("ammocan", "Ammo can", 6, [&](Entity& e) { box(e, {-0.14, 0, -0.06}, {0.14, 0.18, 0.06}, "metal_panels"); box(e, {-0.06, 0.18, -0.012}, {0.06, 0.2, 0.012}, "steel"); });
        add("can", "Tin can", 0.4, [&](Entity& e) { cyl(e, {-0.035, 0, -0.035}, {0.035, 0.12, 0.035}, 10, "steel"); });
        add("bucket", "Paint bucket", 4, [&](Entity& e) { cyl(e, {-0.13, 0, -0.13}, {0.13, 0.3, 0.13}, 12, "paint_white"); });
        add("brick", "Brick", 2.3, [&](Entity& e) { box(e, {-0.107, 0, -0.05}, {0.107, 0.065, 0.05}, "brick"); });
        add("bottle", "Bottle", 0.6, [&](Entity& e) { cyl(e, {-0.035, 0, -0.035}, {0.035, 0.2, 0.035}, 10, "paint_orange"); cyl(e, {-0.013, 0.2, -0.013}, {0.013, 0.28, 0.013}, 8, "paint_orange"); });
        return v;
    }();
    return list;
}

}  // namespace gmap
