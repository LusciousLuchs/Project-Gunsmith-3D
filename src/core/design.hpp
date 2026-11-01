// Gunsmith3D - design data (what the user draws). Units: millimetres, side elevation, y points down.
#pragma once
#include <string>
#include <vector>

struct V2 { double x = 0, y = 0; };

// A cartridge, described by the dimensions needed to draw it, feed it and work out its recoil.
struct Cartridge {
    std::string name = "9x19mm";
    double caseLen = 19.15, rimDia = 9.96, rimThick = 1.27, baseDia = 9.93;
    double shoulderDia = 9.65, shoulderAt = 19.15, neckDia = 9.65;   // shoulderAt = caseLen for straight cases
    double bulletDia = 9.01, oal = 29.69, noseLen = 7.5;
    double bulletMass = 7.45;   // g
    double velocity = 360;      // m/s, nominal
    bool rimmed = false;
    double headDia() const { return rimDia > baseDia ? rimDia : baseDia; }
    double impulse() const { return bulletMass / 1000.0 * velocity * 1.35; }  // N*s, bullet plus a share for the gas
    double energy() const { return 0.5 * bulletMass / 1000.0 * velocity * velocity; }  // J
};
const std::vector<Cartridge>& cartridgePresets();
std::vector<V2> cartridgeOutline(const Cartridge& c, bool caseOnly = false);  // side view, head at x = 0, axis along +x at y = 0 (mm)
double cartridgeMass(const Cartridge& c);                   // loaded round, grams (bullet + brass estimate)
std::vector<Cartridge> loadUserCartridges();                 // cartridges.json next to the program
bool saveUserCartridges(const std::vector<Cartridge>& list);

// Procedural magazine settings (the magazine maker).
struct MagGen {
    bool on = false;
    int stack = 2;              // 1 single stack, 2 double stack
    double curve = 0;           // degrees the magazine bends forward over its length (banana mags)
    double wall = 1.2;          // mm
    double angle = 0;           // degrees from straight down (follows the well)
    V2 top;                     // top centre (feed lips), design coords
    double H = 0, W = 0;        // generated height and width
};

struct BodyDef {
    std::string id, name;
    bool fixed = false;
    std::string layer = "none";          // "none", "A", "B", "C"
    double thick = 10;                   // depth in mm (also drives mass)
    double z = 0;                        // depth position of its centre, mm (+ = the near side in side view, 0 = the bore)
    double mass = 0;                     // grams; 0 = from area x thickness x steel
    double damp = 0;                     // friction-like damping, 1/s
    std::string color = "#8e9ab4";
    bool ghost = false;                  // drawn see-through in X-ray mode
    double smooth = 0;                   // 0..1: rounds the front and back edges (grips, dehorned slides). Looks only.
    bool rollOn = false;                 // rotates about the bore (rotating bolt)
    double rollMin = 0, rollMax = 90;    // degrees
    double handle = 0;                   // bolt handle length, mm
    bool magazine = false;               // detachable magazine (rides in a magwell joint)
    int magCap = 10;                     // rounds when full
    // feed lips and spring (physical feeding). The defaults are fitted to the cartridge; these nudge them.
    double lipHeight = 0;                // mm the top round sits above (+) or below (-) where the lips would normally hold it
    double lipRelease = 0.45;            // the lips let the round go once it has been pushed this far forward (fraction of its length;
                                         // not in the panel yet: the round is also let go as soon as its nose enters the chamber)
    double magSpring = 1;                // follower spring strength against a standard spring (1): how hard and fast rounds come up
    bool doubleFeed = false;             // double stack: feed alternately from each side (double position) instead of
                                         // tapering to one central round (single position)
    MagGen gen;
    // controls: parts you work with a key in Test mode (trigger, buttons, levers)
    std::string ctrl = "none";           // none, button, lever, trigger
    std::string key = "none";            // none, fire, magrelease, k1..k4
    V2 ctrlPt;                           // where the finger pushes (design coords)
    double ctrlDir = 180;                // push direction, degrees: 0 = forward (+x), 90 = down, 180 = rearward
    double ctrlForce = 25;               // N
    // shape: control polygon plus rounded corners and curved edges
    std::vector<V2> pts;
    std::vector<double> rad;             // corner radius at each vertex (mm)
    std::vector<double> bulge;           // edge i (pts[i] -> pts[i+1]) bows outward by this much (mm, negative = inward)
};

struct JointDef {
    std::string type;                    // pin weld track spring cam lug latch magwell
    std::string a, b = "world";
    V2 p, pa, pb;
    std::vector<V2> path;                // track / magwell path (on body b); magwell: path[0] = seated, last = exit
    bool lockRot = false;
    double k = 1, rest = 0, damp = 0;    // spring: N/mm, mm, N*s/m
    std::string kind = "coil";           // spring: coil or leaf
    std::string guide;                   // coil: guide rod part ("" = none, it can buckle)
    double coilDia = 8, wire = 1, turns = 12;  // coil: outer diameter, wire, active turns (sets solid height)
    double preload = 1;                  // leaf: mm of deflection at rest
    bool flip = false;                   // leaf: push to the other side
    std::vector<V2> table;               // cam: x = travel mm, y = rotation deg
    double unlock = 45;                  // lug: degrees
    bool blockOpen = false;
    double catchAt = -30;                // latch: mm
    std::string rel;                     // latch: release part; magwell: catch button part
    double releaseAt = -2;               // latch: mm; magwell: button travel that releases the catch (mm)
};

struct BreechDef {
    bool on = false;
    std::string breech, chamber = "world";
    bool hasFace = false, hasMuzzle = false;
    V2 face, muzzle;
    double impulse = 3, dur = 1, feed = 20;
    int rounds = 10;
    bool autoFire = false, loaded = true;
    double roundLen = 20, roundDia = 9;
    Cartridge cart;
    bool cartImpulse = true;             // take the recoil impulse from the cartridge
    std::string feedMode = "magazine";   // magazine, internal, single
    bool physicalFeed = false;           // rounds and cases are physical through the whole cycle (older blueprints: simplified)
    std::string ignition = "button";     // button (fires on the FIRE key) or striker (fires when a part hits the primer)
    std::string striker;                 // part that hits the primer
    V2 strikerTip;
    double igniteSpeed = 1.5;            // m/s needed at impact
    // The built-in hammer or striker (ignition "button"): nothing to draw, but it still has to be cocked by the action.
    // Feeding, the bolt and extraction are always real parts; the fire control can be these settings or drawn parts.
    std::string hammer = "striker";      // what to call it: "hammer" or "striker" (wording only)
    double cockAt = 8;                   // mm the breech must open to cock it again; 0 = always cocked (older blueprints load as 0)
    std::string fireMode = "semi";       // semi, auto, burst. Drawn parts (ignition "striker") decide this for themselves.
    int burst = 3;                       // shots per pull in burst
    bool doubleAction = false;           // pulling the trigger cocks and drops it, so it fires even when not cocked
    double lockTime = 0;                 // ms from the trigger breaking to the shot
    bool fullAuto() const { return fireMode == "auto" || (autoFire && fireMode != "burst"); }
};
void applyCartridge(BreechDef& b);      // copy round size and impulse from the cartridge

struct Design {
    std::string name = "Untitled action";
    double speed = 0.02;                 // slow-motion speed for shots
    double friction = 0.12;              // contact friction coefficient
    std::vector<BodyDef> bodies;
    std::vector<JointDef> joints;
    BreechDef breech;
    // where the blueprint came from (filled in when loading; saving always writes the current version)
    int format = 1;                      // blueprint format number (GUNSMITH_BLUEPRINT_FORMAT when saved)
    std::string savedWith;               // app version that saved it, "" for blueprints older than versioning

    int bodyIndex(const std::string& id) const {
        for (size_t i = 0; i < bodies.size(); i++) if (bodies[i].id == id) return (int)i;
        return -1;
    }
};

// Shape helpers: the outline actually used for physics and drawing.
void fixShape(BodyDef& b);                                     // keep rad/bulge sized to pts
std::vector<V2> outlineOf(const BodyDef& b);
V2 centroidOf(const BodyDef& b);
std::vector<V2> circlePts(V2 c, double r, std::vector<double>* bulge);

// Editing operations (Sprocket-style shaping).
std::vector<BodyDef> splitBody(const BodyDef& b, V2 a, V2 c);   // knife cut along segment a-c; returns the pieces (1 if no cut)
void mirrorBody(BodyDef& b, V2 a, V2 c);                         // reflect across the line through a and c
void flipBody(BodyDef& b, bool horizontal);                      // flip about its own centre
void subdivideBody(BodyDef& b);                                  // add a vertex in the middle of every edge
int splitEdge(BodyDef& b, int edge);                             // add a vertex in the middle of one edge (on its curve); returns its index

// Magazine maker.
void generateMagazine(Design& d, int bodyIndex);               // rebuild the magazine shape and its well
V2 magCentre(const MagGen& g, double s, double* phiDeg);       // point on the magazine centreline, s mm below the lips

// JSON in the same format as the browser prototype (extra fields are ignored there).
std::string designToJson(const Design& d, bool pretty = false);   // stamps the current version and format
// A note for the user if the blueprint came from a newer Gunsmith3D, "" otherwise.
std::string blueprintVersionNote(const Design& d);
bool designFromJson(const std::string& text, Design& out, std::string* err = nullptr);

// Height of the baseplate (mm, y down): below every part, with room for magazines to drop clear.
double groundLevelMM(const Design& d);

struct PresetInfo { const char* key; const char* label; };
const std::vector<PresetInfo>& presetList();
Design loadPreset(const std::string& key);
