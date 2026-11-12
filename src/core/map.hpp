// Gunsmith3D - Maps: static brush geometry in the style of early-2000s idTech / Radiant maps.
//
// A map is a list of convex brushes plus a few entities (the player start). Each brush is the space inside a set of
// planes; each plane is one face and carries its own material and texture scale. Faces are turned into polygons by
// clipping a large square on each plane against all the others, the way Quake-era map compilers did.
// Units are metres, Y is up. Maps are saved as plain JSON so players can share and edit them freely.
// No graphics here: rendering and the editor live in app/.
#pragma once
#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace gmap {

struct V3 {
    double x = 0, y = 0, z = 0;
    V3() = default;
    V3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}
    V3 operator+(const V3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    V3 operator-(const V3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    V3 operator*(double s) const { return {x * s, y * s, z * s}; }
    V3 operator-() const { return {-x, -y, -z}; }
    V3& operator+=(const V3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    V3& operator-=(const V3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
};
inline double dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline double len(V3 a) { return std::sqrt(dot(a, a)); }
inline V3 norm(V3 a) { double l = len(a); return l > 1e-12 ? a * (1 / l) : V3{0, 1, 0}; }

// Materials are referred to by name in map files, so maps stay readable and new materials can be added later.
const std::vector<std::string>& materialNames();
int materialIndex(const std::string& name);       // unknown names fall back to 0 (the dev grid); "user:<file>" names
                                                   // (pictures in user/textures) are added to the list
int builtInMaterials();
bool isUserMaterial(int m);
double materialTile(int m);                        // metres per texture repeat at scale 1

struct Face {
    V3 n;                 // outward unit normal
    double d = 0;         // the brush is where dot(n, p) <= d for every face
    int mat = 0;
    double scale = 1;     // texture scale (1 = the material's natural size)
    double shiftU = 0, shiftV = 0;  // texture offset in metres
};
struct Brush {
    std::vector<Face> faces;
    // (brushBounds remembers its answer until the faces' planes change: collision asks it constantly)
    mutable double boundsCache[6] = {0, 0, 0, 0, 0, 0};
    mutable unsigned long long boundsKey = 0;
    bool locked = false;   // part of a building that comes with a career property (its walls): can't be changed
};
struct Entity {
    std::string type = "player_start";   // player_start, target_paper, npc_spawn (an NPC stands here), gun_spawner
                                         // (a stand the player uses to take guns), armor_plate (plates to pick up), attachment_crate (fit attachments)
    V3 pos;
    double yaw = 0;       // degrees; 0 looks along +X
    std::string ref;      // npc_spawn: which NPC (its name in the map's own copies, or in user/npcs)
    int count = 1;        // npc_spawn: how many; armor_plate: how many plates lie here
    double respawn = 0;   // armor_plate: seconds until taken plates come back (0 = never)
    // light: a lamp (type "light"). Lights everything within its reach, brightest close up; a spot only in its cone
    double color[3] = {1.0, 0.86, 0.66};
    double power = 3;     // how bright (about how many times a dim room's light at 1 m)
    double range = 10;    // metres: nothing past this
    double cone = 0;      // degrees edge to edge: 0 shines all round; a spot (a floodlight, a ceiling spot) otherwise
    double pitch = -90;   // a spot's aim: up (+) or down from level, toward `yaw`
};
struct Look {
    double sunYaw = 35, sunPitch = 55;      // degrees: where the sunlight comes from
    double sun[3] = {1.0, 0.96, 0.88};
    double ambient[3] = {0.48, 0.52, 0.60};
    double skyTop[3] = {0.36, 0.55, 0.80};
    double skyHorizon[3] = {0.78, 0.84, 0.90};
    double fogStart = 60, fogEnd = 450;    // metres
    double daylight = 1;                    // 1 broad daylight .. 0 night (lit only by the map's lights); older maps 1
};
// What a map's light entity shines like, ready for drawing (positions in metres)
struct LampAt { V3 pos, dir; double color[3]; double power, range, cosCone; };   // cosCone -2: all round
LampAt lampOf(const Entity& e);

// An area on the ground (x, z): a career property's floor you may build on, and its storefront
struct Zone {
    double x0 = 0, z0 = 0, x1 = 0, z1 = 0;
    bool on() const { return x1 > x0 && z1 > z0; }
    bool has(V3 p, double pad = 0) const { return !on() || (p.x >= x0 - pad && p.x <= x1 + pad && p.z >= z0 - pad && p.z <= z1 + pad); }
};
struct Map {
    std::string name = "Untitled";
    Zone build, store;   // career properties: where you may build, and the storefront (off: anywhere)
    double roofY = 0;    // career properties: the ceiling's height, drawn over the build area on foot (0: worked out from the walls)
    std::vector<Brush> brushes;
    std::vector<Entity> ents;
    Look look;
    // the NPCs the map uses, carried in the map file (name, the NPC's own JSON), so a shared map brings its enemies
    std::vector<std::pair<std::string, std::string>> npcs;
    std::vector<std::pair<std::string, std::string>> npcGuns;   // and the guns made for NPCs they carry (name, blueprint JSON)
};

// How lit a point is (red, green, blue, about 0..1+) facing normal n: the map's daylight and ambient plus its lights.
// (For things drawn without the lighting shader; matches it closely.)
void lightAt(const Map& m, V3 p, V3 n, double out[3]);

// ---- brush geometry
std::vector<V3> facePolygon(const Brush& b, int face);   // counter-clockwise seen from outside; empty if the face is gone
bool brushValid(const Brush& b);                        // a closed solid with some volume
void brushClean(Brush& b);                              // drop faces that no longer touch the solid
void brushBounds(const Brush& b, V3& mn, V3& mx);
V3 brushCentre(const Brush& b);

Brush makeBox(V3 mn, V3 mx, int mat);
Brush makeWedge(V3 mn, V3 mx, int dir, int mat);       // a ramp rising towards +X, +Z, -X, -Z for dir 0..3
Brush makeCylinder(V3 mn, V3 mx, int sides, int mat);  // an upright prism inscribed in the footprint
std::vector<Brush> makeStairs(V3 mn, V3 mx, int steps, int dir, int mat);

void translate(Brush& b, V3 d);
void rotateY(Brush& b, double radians, V3 pivot);
void mirror(Brush& b, int axis, V3 pivot);             // axis 0 = X, 2 = Z
bool pushFace(Brush& b, int face, double amount);      // move one face along its normal; false (unchanged) if it would break the brush
// Cut with a plane: keeps the part where dot(n, p) <= d. Returns false if nothing would be left.
bool clipBrush(Brush& b, V3 n, double d, int mat);
std::vector<Brush> hollow(const Brush& b, double wall); // walls of the given thickness around the brush's inside

// ---- collision and picking
struct Hit {
    double t = 1;        // fraction of the move (sweep) or distance (ray)
    V3 n;                // surface normal
    int brush = -1, face = -1;
    bool startSolid = false;
};
// Ray against one brush or the whole map; t is a distance along the (unit) direction. Misses give brush = -1.
Hit rayBrush(const Brush& b, V3 o, V3 dir, double maxT);
Hit rayMap(const Map& m, V3 o, V3 dir, double maxT);
// Sweep an axis-aligned box (half extents `half`, centred on the path) from a to b. t is the fraction travelled.
Hit sweepBox(const Map& m, V3 a, V3 b, V3 half);
bool boxFree(const Map& m, V3 centre, V3 half);

// ---- files
std::string mapToJson(const Map& m);
bool mapFromJson(const std::string& s, Map& m);
Map builtinRange();      // the starter practice range
Map defaultWorkshop();   // career mode: the room you start your business in (a bench, a counter)
// Career mode: the office computer and the front door (added where missing, in older workshops too)
void addShopBasics(Map& m);
// Career mode: what a shop needs to work that this one hasn't got, in plain words ("" for each found): where you start,
// your own workbench, the counter, the computer, the front door, and a light if it's dark inside
struct ShopNeed { std::string what, tool; };   // tool: where to place one in the editor
std::vector<ShopNeed> shopMissing(const Map& m);
// puts in whatever shopMissing finds, on the floor near where you start (else the middle of the room)
int shopRepair(Map& m);
// Career properties: the fixed building for a kind (garage, shack, retail, warehouse): locked walls, a floor, the build
// area and the storefront, and a starter bench, counter, computer, door, start and lights
Map propertyShell(const std::string& kind);
// How good the storefront looks, 0..5 stars: displays (with a gun on them most), decoration, light, a counter. `tips`
// gets what would help most.
double storeRating(const Map& m, std::vector<std::string>* tips = nullptr);
bool isDisplay(const std::string& type);   // display_case, display_rack, display_stand
bool isDecor(const std::string& type);     // decor_*
// career mode: the workstations placed in a workshop map (entities station_bench/lathe/mill/qc, shop_counter -> counter)
std::map<std::string, int> workshopStations(const Map& m);

}  // namespace gmap
