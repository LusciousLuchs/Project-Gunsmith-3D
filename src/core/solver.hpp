// Gunsmith3D - mechanism solver.
// Planar rigid bodies (x, y, in-plane angle) plus an optional roll angle about each body's bore axis,
// solved with XPBD at 8 kHz. Units inside: metres, kg, seconds. Design data stays in mm.
#pragma once
#include "design.hpp"
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace sim {

constexpr double H = 1.0 / 8000.0;   // substep, seconds
constexpr double D2R = 3.14159265358979323846 / 180.0;

// Where a physical cartridge is: on the bench or in a hand, guided in by hand, under the magazine lips,
// loose in the action, or held on the breech face by the extractor.
enum class RS { None, Loose, Staged, Presented, Free, Clawed, Tube };  // Tube: being pushed out of a tube magazine

struct Body {
    double tilt = 0;                     // presented round tipped nose-up by a feed ramp (rad, <= 0)
    const BodyDef* d = nullptr;
    std::string id;
    bool fixed = true;
    double x = 0, y = 0, a = 0, x0 = 0, y0 = 0;
    double vx = 0, vy = 0, w = 0, roll = 0, wr = 0;
    double m = 0, I = 0, invM = 0, invI = 0, Ir = 0, invIr = 0, rmin = 0, rmax = 0;
    double damp = 0;
    std::vector<V2> lp, samples;          // local polygon and boundary samples (m)
    // parts with many corners: which edges can be near a point (a grid over the part) and which edges cross each
    // horizontal band (for inside tests), so a contact looks at a few edges instead of all of them. Built by resample.
    struct Accel {
        size_t n = 0;                     // lp.size() it was built for (0 = none)
        double x0 = 0, y0 = 0, cell = 1, band = 1, y0band = 0;
        double lb[4] = {0, 0, 0, 0};      // the outline's own box (local)
        int gw = 0, gh = 0, nb = 0;
        std::vector<std::vector<int>> near; // per grid cell: edges within the contact reach of some point in it
        std::vector<std::vector<int>> close; // per grid cell: edges within `tight` of some point in it (shallow contacts)
        double tight = 0;
        std::vector<std::vector<int>> bands;
    } acc;
    std::vector<std::vector<V2>> cav;     // carved parts: cuts through this slice (local, m, convex). Empty for most parts
    bool slice = false;                  // one of the extra depth slices of a carved part (the part itself is the main slice)
    double fx = 0, fy = 0, tq = 0, tr = 0, px = 0, py = 0, pa = 0, pr = 0;
    double bb[4] = {0, 0, 0, 0};
    double z = 0, zh = 0;                // depth: centre and half thickness (m)
    double rfx = 0, rfy = 0;             // fixed parts: constraint impulse taken this substep (N s), for felt recoil
    // welded parts form one rigid assembly: a piece rides on its root, and pushes on it
    Body* root = nullptr;                // set on pieces: the part this one is welded to
    V2 off;                              // piece centre in the root's frame (m)
    double aOff = 0;
    std::vector<Body*> pieces;           // set on roots
    // parts welded to a rotating bolt turn with it: their outline and depth follow the bolt's roll
    Body* rollParent = nullptr;
    std::vector<V2> lp0;                 // outline at roll 0 (local, m)
    double z0 = 0, zh0 = 0, axisDy = 0, axisDz = 0;  // depth at roll 0; this part's centre relative to the bolt's axis (m)
    double rollShaped = 0;               // roll the current outline was built for
    bool isMag = false;                  // removable magazine
    bool builtInMag = false;             // a built-in magazine (internal box, tube): feeds the gun, loaded in place
    bool loose = false;                  // free body under gravity (dropped magazine)
    bool grounded = false;
    int rounds = 0;                      // magazines: rounds inside
    std::string tmpl;                    // spawned copies: id of the design part they copy
    // loose cartridges
    bool isRound = false;                // a physical round you can pick up and load
    bool staged = false;                 // sitting in the open action on the bore line, ready to be pushed home
    bool gone = false;                   // went into the chamber (the body is parked; skip it)
    double headLx = 0;                   // local x of the cartridge head (m); the round points along local +x
    double stageS = 0;                   // staged: how far the head sits behind the chamber (m)
    RS rs = RS::None;
    bool isCase = false;                 // an empty case (fired)
    bool inChamber = false;              // its nose went into the chamber mouth straight: the chamber now guides it
    int slot = 0;                        // which chamber it belongs to (several chambers: see Sim::chambers)
    bool wasOut = false;                 // has been clear of the chamber (so going back in means seating it)
    double vz = 0;                       // sideways speed (m/s), for cases leaving through the ejection port
    Body* mag = nullptr;                 // presented: the magazine holding it
    V2 lipsL, feedL;                     // presented: where its head starts and the feed direction, in the magazine's frame
                                         // (leaving a tube: the tube's mouth and the way out)
    V2 clawB, clawR;                     // clawed: the extractor's grip, on the breech and on the cartridge (local, m)
    Body* top = nullptr;                 // magazines: the round held up under the lips (physical feeding)
    Body* pawl = nullptr;                // belts: the part whose stroke moves the belt on
    bool pawlArmed = false, indexReady = true;  // belts: the pawl has caught the next link; a round is ready to come into the tray
    double zFeed = 0;                    // presented by a double-position magazine: how far off the centre line it started (m);
                                         // it comes into the chamber at an angle sideways
    double zVis = 0;                     // ...and where it's drawn, easing to the centre. It collides as if centred: the example
                                         // actions are built only as wide as a single stack (a wider action would make this real)
    double idle = 0;                     // seconds (real time) lying untouched: spent cases and empty magazines are tidied away
};

enum class JT { Pin, Weld, Track, Spring, Cam, Lug, Latch, Magwell };
enum class LatchState { Armed, Hold, Released };

struct Joint {
    std::vector<double> pathAng;         // magwell: the magazine's tilt at each path point (rad)
    const JointDef* j = nullptr;
    int defIndex = -1;
    JT type = JT::Pin;
    Body* A = nullptr;
    Body* B = nullptr;
    Body* R = nullptr;
    V2 la, lb, rel0, relR0, relRA0;     // (relRA0: an action lock's release part, measured from the held part)
    std::vector<V2> path;
    LatchState state = LatchState::Armed;
    bool engaged = false;
    double force = 0;
    bool attached = false, latched = false;  // magwell
    bool merged = false;                     // weld inside a rigid assembly: nothing left to solve
    V2 nrm;                                  // leaf spring: push direction in A's frame
    bool buckled = false;                    // coil spring with no guide rod, bent sideways
};

// A control (trigger, button, lever) pushed by a key in Test mode.
struct Pusher {
    Body* b = nullptr;
    V2 l;                   // local point (m)
    double dx = -1, dy = 0; // world push direction
    double force = 25;      // N
    std::string key;        // fire, magrelease, k1..k4
    bool on = false;
    // the finger: it presses toward a point that moves at finger speed, with at most `force` newtons
    bool wasOn = false;
    V2 anchor;              // where the control point was when the press began (world, m)
    double ext = 0;         // how far the finger has pressed along the push direction (m)
};

enum class Ev { Bang, Eject, EjectLive, Click, Chambered, Malfunction, MagLoaded, ClipOut };  // MagLoaded: a round pushed into a magazine  // EjectLive: an unfired round thrown out

// What can go wrong when a gun cycles. Physical feeding lets these happen for real.
enum class Malf { None, FailureToFeed, DoubleFeed, Stovepipe, FailureToEject, FailureToReturn, LightStrike, FailureToCock, Count };
const char* malfName(Malf m);
const char* malfAdvice(Malf m);
struct Event { Ev kind; double t; };

struct Sim {
    Design design;                       // private copy; bodies point into it
    std::vector<Body> bodies;
    Body world;
    std::vector<Joint> joints;
    std::vector<std::pair<Body*, Body*>> pairs;
    double t = 0, fireT = -1e9;
    bool trigger = false, pressed = false;
    bool chambered = true, inTransit = false, spent = false;
    int rounds = 0, shots = 0;
    std::vector<Event> events;
    Body* BR = nullptr;
    Body* CH = nullptr;
    V2 faceB, faceC, muzC;
    bool hasFace = false, hasMuzzle = false;
    struct Hand { bool on = false; Body* b = nullptr; V2 l, t; } hand;
    struct { bool on = false; Body* b = nullptr; double t = 0, arm = 0; } rollHand;  // arm: lever (m) limiting the torque; 0 = no limit
    double handStrength = 150;           // N, the most force your hand applies
    bool magRelease = false;             // magazine release button held
    double groundY = 0.3;                // m, baseplate height for loose parts
    // The gun simulates in its own frame. frameAccel is gravity (and, in a game, the shooter's motion) as felt
    // in that frame; frameForce is what the frame feels back from the moving parts: the recoil in your hands.
    V2 frameAccel{0, 9.81};              // m/s^2, gun frame (y down)
    V2 frameForce;                       // N, on the fixed parts this substep
    double recoilPeak = 0;               // N, largest rearward frame force since the last shot (1 ms average)
    double recoilWin[8] = {0};           // last 1 ms of rearward frame force
    int recoilIdx = 0;
    double recoilImpulse = 0;            // N s, rearward impulse into the frame since the last shot
    int mwIdx = -1;                      // magwell joint index
    const BodyDef* magTmpl = nullptr;    // design magazine, copied for new magazines
    std::deque<BodyDef> extraDefs;       // definitions of spawned magazines
    int spawned = 0;
    std::vector<Pusher> pushers;
    std::string feedMode = "magazine";   // magazine, internal, single
    bool strikerIgnition = false;
    Body* STR = nullptr;                 // striker / firing pin
    V2 strTip;                           // its tip, local (m)
    bool primed = true;                  // striker must back off before it can fire again
    // the built-in hammer or striker (no drawn parts)
    bool cocked = true;                  // ready to fall
    int burstLeft = 0;                   // shots left in this burst
    double igniteAt = -1;                // when the falling hammer reaches the primer (s); -1 = not falling
    bool deadPull = false;               // the last pull found it down (single action): the trigger did nothing
    bool gripped = false;                // the extractor has the chambered round (the breech closed on it)
    // physical feeding: rounds come up under the magazine lips, are stripped, ride the feed ramp, enter the chamber
    // only if they arrive straight, and are pulled out by the extractor and kicked clear by an ejector you draw
    bool physicalFeed = false;
    std::vector<double> chamberR;        // chamber radius every 0.5 mm from the chamber face (m)
    double gunBox[4] = {0, 0, 0, 0};     // x0, y0, x1, y1 (m) of the gun as drawn
    double gunZmin = 0, gunZmax = 0;     // depth the gun's parts span (m): a case beyond it has left the gun
    Malf malf = Malf::None;              // the jam the gun is in right now
    int malfCount[(int)Malf::Count] = {0};
    int fed = 0;                         // rounds fed into the chamber by the action
    double stuckT = 0;                   // how long the action has sat open and still
    bool breechSprung = false;           // a spring closes the breech (self-loading): a stuck open action is a jam
    bool cycledOpen = false;             // the breech opened far enough to pick up the next round
    // shot-to-shot variation (the reliability test sets these; 1 and 0 for a plain shot)
    double powerScale = 1;               // this round's power against the cartridge's nominal impulse
    double limp = 0;                     // how loosely the gun is held, 0 firm .. 1 free: the frame recoils with the slide
    // Several chambers. Each has its own breech face, bore and state. The chamber being worked on is in the fields
    // above (BR, CH, faceB ... gripped); useChamber() swaps another one in. Chamber 0 is the gun's first, and the one
    // the magazine feeds. With one chamber nothing is ever swapped and everything behaves as it always has.
    struct Chamber {
        Body* BR = nullptr; Body* CH = nullptr;
        V2 faceB, faceC, muzC;
        bool hasFace = false, hasMuzzle = false;
        bool chambered = false, inTransit = false, spent = false, gripped = false, cycledOpen = false;
        double shotT = -1e9;             // when this chamber last fired (its pressure lasts the cartridge's burn time)
        Cartridge cart;                  // what it's cut for (moved in and out of design.breech.cart with the rest)
        std::vector<double> chamberR;
        double impulse = 0;
    };
    std::vector<Chamber> chambers;       // [cur] is stale while it's swapped in
    int cur = 0;                         // which chamber is in the fields above
    int nextChamber = 0;                 // fired in turn: the one to try first on the next pull

    Body* find(const std::string& id);
};
void useChamber(Sim& S, int k);          // swap chamber k's state into the working fields (no-op with one chamber)
int chamberCount(const Sim& S);
bool anyChambered(Sim& S);               // a live round in any chamber
int chamberedCount(Sim& S);

std::unique_ptr<Sim> build(const Design& d);
void substep(Sim& S);
int run(Sim& S, double dt, int maxSteps = 0);
bool inBattery(const Sim& S);
double breechGap(const Sim& S);                // mm
V2 wpt(const Body& b, V2 l);                   // local (m) -> world (m)
V2 lpt(const Body& b, V2 w);                   // world (m) -> local (m)
double axial(const Body& A, const Body& B, V2 rel0);
V2 designToWorld(const Sim& S, const std::string& bodyId, V2 pmm);  // design point (mm) carried by body -> current (mm)

// magazines
Joint* magwell(Sim& S);
Body* seatedMag(Sim& S);                                   // attached and latched, or null
Body* spawnMagazine(Sim& S, V2 centreMM, bool seat);      // full magazine; seat=true puts it straight in the well
void dropMagazine(Sim& S);                                 // unlatch and pull the seated magazine out
void quickReload(Sim& S);                                  // drop the old magazine, seat a full one
bool magLoadPoint(const Sim& S, const Body& M, V2& w);     // where to hold a round to push it into this magazine (m)
int fillMagazines(Sim& S);                                 // top up every magazine (seated and on the bench); rounds added
// Remove spent cases that have lain on the bench `caseLife` seconds, and dropped empty magazines nobody has touched
// for `magLife` seconds. Real time, so slow motion doesn't keep them about. Returns how many were removed.
int tidy(Sim& S, double dt, double caseLife = 10, double magLife = 30);
bool handLoad(Sim& S);                                     // drop a round straight into an open chamber (shortcut)
// loose rounds: physical cartridges. Hold one in the open action, in line with the bore and in front of the
// breech face, and it drops into place; closing the breech (or pushing it by hand) seats it in the chamber.
Body* spawnRound(Sim& S, V2 headMM, double vx = 0, double vy = 0);  // head position in mm, velocity m/s
Body* dropIntoChamber(Sim& S, int k);                             // a round pushed home into chamber k of an open action
int looseRounds(const Sim& S);                                       // rounds lying about (not chambered)
struct BoreLine { bool ok = false; V2 face, dir; double faceS = 0, faceLat = 0; };
int jamCount(const Sim& S);
BoreLine boreLine(const Sim& S);   // chamber mouth (m), bore direction, and where the breech face sits along it
void setKey(Sim& S, const std::string& key, bool down);    // press or release the controls bound to a key
bool hasControl(const Sim& S, const std::string& key);

// geometry helpers shared with the app
double polyArea(const std::vector<V2>& p);
V2 polyCentroid(const std::vector<V2>& p);
bool inPoly(double x, double y, const std::vector<V2>& p);
V2 closestOnSeg(V2 p, V2 a, V2 b, double* d2);
void interpTable(const std::vector<V2>& tab, double u, double& val, double& slope);

}  // namespace sim
