// Gunsmith3D - design data (what the user draws). Units: millimetres, side elevation, y points down.
#pragma once
#include <map>
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
    // the bullet's shape. "auto" is the original generic ogive, kept so older blueprints behave as they always did.
    std::string nose = "auto";          // auto, spitzer, round, flat, hollow, wadcutter, semiwad, ball, custom
    double meplat = 0;                  // flat tip, as a fraction of the bullet's diameter (0 = sharp)
    double tailLen = 0, tailDia = 0.8;  // boat tail: length (mm) and base diameter as a fraction of the bullet's
    double seat = 0;                    // how deep the bullet sits in the case (mm); 0 = a usual depth for the case
    std::vector<V2> custom;             // nose "custom": the outline drawn by hand, (mm from the bullet's base, radius mm)
    bool massFromShape = false;         // work the weight out from the shape (jacketed lead) instead of typing it
    // What kind of round it is. Older blueprints are all "centerfire" with one bullet, as before.
    //   centerfire  primer in the middle of the head
    //   rimfire     the priming is in the rim: the firing pin has to hit the rim's edge, not the middle
    //   shotshell   a hull (plastic or paper) on a brass head, crimped shut, holding shot or a slug; no bullet shows
    //   caseless    the propellant is the round's body and burns away: nothing is left to extract
    std::string kind = "centerfire";
    int projectiles = 1;                // bullets, balls or pellets fired at once
    double shotDia = 0;                 // shotshells: pellet size, mm. 0 = a slug (bulletMass)
    double headLen = 8;                 // shotshells: the height of the brass head, mm
    unsigned hullColor = 0xB0302AFF;    // shotshells: the hull's colour (RGBA)
    bool shot() const { return kind == "shotshell" && shotDia > 0; }
    double pelletMass() const { double r = shotDia / 20.0; return 4.18879 * r * r * r * 11.34; }  // g, a lead ball
    double payloadMass() const { return (shot() ? pelletMass() : bulletMass) * (projectiles < 1 ? 1 : projectiles); }  // g, all of it
    double headDia() const { return rimDia > baseDia ? rimDia : baseDia; }
    double impulse() const { return payloadMass() / 1000.0 * velocity * 1.35; }  // N*s, payload plus a share for the gas
    double energy() const { return 0.5 * payloadMass() / 1000.0 * velocity * velocity; }  // J
};
const std::vector<Cartridge>& cartridgePresets();
std::vector<V2> cartridgeOutline(const Cartridge& c, bool caseOnly = false);  // side view, head at x = 0, axis along +x at y = 0 (mm)
// The bullet's half outline, (mm from the cartridge head, radius mm), from its base to its tip; it may turn back in
// at the tip (a hollow point). Empty for "auto", which keeps the original shape.
std::vector<V2> bulletShape(const Cartridge& c);
double bulletBase(const Cartridge& c);                     // where the bullet's base sits, mm from the head
double bulletShapeMass(const Cartridge& c);
double bulletLength(const Cartridge& c);                   // mm, base to tip
// How well a barrel's rifling spins this bullet to keep it point first (the Miller rule): about 1.4 and up is stable,
// 1 to 1.4 marginal, under 1 it tumbles. A smoothbore gives 0 (a bullet isn't spun at all).
double spinStability(const Cartridge& c, double twistMM, bool rifled);                // grams of jacketed lead in that shape
double ballisticCoefficient(const Cartridge& c);           // G1, estimated from the shape and weight
const char* noseStyles();                                  // "\0"-separated names for the shapes, in nose-id order
const char* noseId(int i);
int noseIndex(const std::string& id);
double cartridgeMass(const Cartridge& c);                   // loaded round, grams (bullet + brass estimate)
double spentCaseMass(const Cartridge& c);                   // what's left after the shot, grams (0 for caseless)
const char* cartKinds();                                    // "\0"-separated names, in kind-id order
const char* cartKindId(int i);
int cartKindIndex(const std::string& id);
std::vector<Cartridge> loadUserCartridges();                 // cartridges.json next to the program
bool saveUserCartridges(const std::vector<Cartridge>& list);

// Procedural magazine settings (the magazine maker).
struct MagGen {
    bool on = false;
    int stack = 2;              // 1 single stack, 2 double stack
    double curve = 0;           // degrees the magazine bends forward over its length (banana mags)
    double wall = 1.2;          // mm
    double angle = 0;           // degrees from straight down (follows the well); 180 = on top
    int side = 0;               // 0 in the side view (below, above, slanted: see angle); +1 sticks out the right side, -1 the
                                // left: the rounds stack across the gun and come in sideways (the side view sees only the mouth)
    V2 top;                     // top centre (feed lips), design coords
    double H = 0, W = 0;        // generated height and width
};

// A carve: a cutter of some shape pushed into a part from any direction, to some depth. Subtractive only.
struct CarveDef {
    std::string shape = "circle";        // circle, half (half circle), rect, tri
    V2 at;                               // where the cutter enters, side view (design mm)
    double atZ = 0;                      // ...and at what depth, relative to the part's centre (mm, + = near side)
    double dx = 0, dy = 0, dz = -1;      // the way the cutter goes in (x forward, y down, z toward the near side)
    double spin = 0;                     // degrees: turns the shape about that direction
    double w = 6, h = 6;                 // mm across; circles and half circles stretch to w x h
    double depth = 4;                    // mm deep; 0 = right through
};

// A barrel (the Barrel tool): a round part with a chamber cut at its breech end for one cartridge, and a bore through
// to the muzzle. Its ends come from its own shape; the firing system takes its chamber and muzzle from it.
struct BarrelDef {
    bool on = false;
    Cartridge cart;               // what its chamber is cut for
    bool rifled = true;           // false: smoothbore
    double twist = 254;           // rifling: mm of barrel per turn (254 = 1 in 10")
    int grooves = 6;
    int muzzleEnd = 1;            // which end is the muzzle, along the part's axis: +1 or -1
};
struct P3d { double x, y, z; };
// An engraving on a part's surface (the Engrave tool): a line drawn freehand, or a line of text. Looks only: it never
// changes how the part moves or collides. Points are in design mm like the outline (x, y down, z depth), lying on the
// surface; n is the surface's outward direction at each point. Text: pts[0] is its middle, `right` the way it reads.
struct MarkDef {
    std::vector<P3d> pts, n;
    std::string text;                    // "" = a drawn line
    double size = 4;                     // text: capital height (mm)
    P3d right{1, 0, 0};                  // text: the way it reads, along the surface
    double width = 0.4;                  // the cut's width (mm)
    std::string color = "#dcd8d0";      // the cut's colour (bright: bare metal showing through)
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
    std::string texture;                 // a picture from user/textures on its surface ("" = plain colour); the colour tints it
    double texScale = 40;                // mm before the picture repeats
    bool ghost = false;                  // drawn see-through in X-ray mode
    BarrelDef barrel;
    double tiltX = 0, tiltY = 0;         // degrees: leaned out of the side view about the part's middle (X: about the
                                         // gun's length, + top toward you; Y: about the upright, + front toward you)
    double smooth = 0;                   // 0..1: rounds the front and back edges (grips, dehorned slides). Looks only.
    bool rollOn = false;                 // rotates about the bore (rotating bolt)
    double rollMin = 0, rollMax = 90;    // degrees
    double handle = 0;                   // bolt handle length, mm
    bool magazine = false;               // a magazine: removable (rides in a magwell joint), or built in (magFixed)
    bool magFixed = false;               // built in: part of the gun and loaded in place (internal box, tube, anything)
    std::vector<V2> roundPath;           // how rounds sit inside (design coords, mm), from the feed end inward; empty = automatic
    std::string roundStyle = "side";     // side: side by side across the path (a box); line: nose to tail along it (a tube)
    std::string tubeFeed = "physical";   // a tube, with physical rounds: "physical" = the spring pushes each round out of the
                                         // mouth, where parts you draw (a lifter) take it; "simple" = it reaches the chamber
                                         // when the action opens (older blueprints)
    int magCap = 10;                     // rounds when full
    // feed lips and spring (physical feeding). The defaults are fitted to the cartridge; these nudge them.
    double lipHeight = 0;                // mm the top round sits above (+) or below (-) where the lips would normally hold it
    double lipRelease = 0.45;            // the lips let the round go once it has been pushed this far forward (fraction of its length;
                                         // not in the panel yet: the round is also let go as soon as its nose enters the chamber)
    double magSpring = 1;                // follower spring strength against a standard spring (1): how hard and fast rounds come up
    bool doubleFeed = false;             // double stack: feed alternately from each side (double position) instead of
                                         // tapering to one central round (single position)
    // a belt: no spring; a part you draw (the pawl) moves it on one round each time it travels pawlStroke from where it
    // rests. Rounds wait in the tray at the lips; the rest of the belt hangs out of the side of the gun.
    std::string feedPawl;                // "" = a spring brings rounds up (a box, a tube)
    double pawlStroke = 10;              // mm
    // clips: "stripper" (reloading adds one clip of clipSize rounds at a time), "enbloc" (the clip goes in with the rounds:
    // it can only be loaded empty, and the empty clip is thrown out after the last round)
    std::string clip = "none";
    int clipSize = 5;
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
    std::vector<CarveDef> carves;        // cut out of the shape in 3D, in order
    // Shaped in depth (empty: straight through, as drawn). Each corner has a point on the near face and one on the far
    // face: the near one is pts[i] at depth z + thick/2 + depthZn[i]; the far one is pts[i] + depthOff[i] at depth
    // z - thick/2 + depthZf[i]. The faces between are flat pieces between those points. depthRef: the outline these
    // were set for (so they can follow when corners are added or removed). Corners shaped this way are sharp.
    std::vector<V2> depthOff;
    std::vector<double> depthZn, depthZf;
    std::vector<V2> depthRef;
    std::vector<MarkDef> marks;          // engravings (looks only)
    std::vector<V2> markRef;             // (not saved) the outline, depth and thickness the marks were last placed for:
    double markRefZ = 0, markRefThick = 0;   // when the whole part moves, turns or flips, they go with it (followMarks)
    std::string weld;                    // welded: every part with the same tag is one solid piece ("" = on its own)
    // A live mirror: two parts with the same tag are kept as mirror images of each other; edit either and the other
    // follows (shape, cuts, depth, thickness, tilt, look). "depth": across the gun (the far side's matching part);
    // "line": across the side-view line a-b. "" = not mirrored.
    std::string mirror;
    std::string mirrorKind = "depth";
    V2 mirrorA, mirrorB;
    bool round = false;                  // turned: the outline spun about its long middle line (barrels, rods, bolts);
                                         // its thickness is then its width across
    int roundSides = 0;                  // 0 = a circle; 3+ = that many flats (4 = the square bar it was drawn as, 8 = octagon)
    double roundTurn = 0;                // degrees the cross-section is turned about the axis (0 = a flat on top)
    double roundSweep = 360;             // degrees it goes round: less makes a wedge (a lug), centred on the Turn direction
    int roundAxis = 0;                   // the line it turns about, through its middle: 0 level (front to back), 1 upright,
                                         // 2 along its own length (for parts drawn at a slant)
};

struct JointDef {
    std::string type;                    // pin weld track spring cam lug latch magwell
    std::string a, b = "world";
    V2 p, pa, pb;
    std::vector<V2> path;                // track / magwell path (on body b); magwell: path[0] = seated, last = exit
    std::vector<double> pathAng;         // magwell: the magazine's tilt at each path point (degrees): rock-in magazines
    bool lockRot = false;
    double pinDia = 3.2;                 // pin: how thick it is drawn (mm). The pivot itself is a point; the pin is not solid
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
    bool holdShut = false;               // latch: an action lock. Holds the part from going back past "catchAt" until the
                                         // release part has moved back "releaseAt" mm; it catches again when the part comes home
};

// A further chamber, with its own bore, breech face and muzzle (doubles, volley guns, a cylinder's chambers). The gun's
// first chamber is the BreechDef itself; these all take the same cartridge.
struct ChamberDef {
    std::string breech, chamber = "world";   // the part whose face closes it, and the part it's cut in
    V2 face, muzzle;
    bool hasMuzzle = false;
    bool loaded = true;
    bool ownCart = false;                    // cut for its own cartridge (a barrel's); otherwise the breech's
    Cartridge cart;
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
    std::string extractor = "auto";      // which edge of the rim the extractor claw grips: "top", "bottom", or "auto" (the side
                                         // away from the magazine: the top, unless the magazine is on top). The ejector has to
                                         // hit the case on the other side to flick it off the claw
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
    std::vector<ChamberDef> more;        // further chambers (none in older blueprints)
    std::string fireOrder = "turn";      // with several chambers: "turn" fires the next loaded one per pull, "together" all at once
    bool fullAuto() const { return fireMode == "auto" || (autoFire && fireMode != "burst"); }
};
void applyCartridge(BreechDef& b);      // copy round size and impulse from the cartridge

// How the shooter holds the gun, posed on the bench (Test bench > Pose). The hands and the stock's butt are points on
// parts (they move with them); each hold (aim, hip, lowered) places the body against the gun. Older blueprints with grip
// markers load them as the hands and butt.
enum { DIGIT_THUMB, DIGIT_INDEX, DIGIT_MIDDLE, DIGIT_RING, DIGIT_PINKY, DIGIT_COUNT };
struct DigitPose {
    double bend[3] = {0, 0, 0};   // degrees at each joint from the base: + curls toward the palm
    double spread = 0;            // degrees sideways at the base (+ toward the little finger; the thumb: away from the palm)
    double oppose = 0;            // the thumb only: degrees it swings round in front of the palm to meet the fingers
};
struct HandHold {
    bool digitsSet = false;        // false: a preset that suits the hand (core/hand.cpp)
    DigitPose digits[DIGIT_COUNT];
    bool on = false;
    std::string part;
    V2 at;                 // the middle of the hand, or of the butt (design mm)
    double angle = 105;    // the line the fingers wrap round (or the butt pad's line), degrees: 0 toward the muzzle, 90 down
};
struct BodyHold {
    bool set = false;      // false: worked out from the gun (holdOf)
    V2 eye;                // the aiming eye, in the gun's coordinates (mm)
    double eyeZ = 0;       // across the gun, mm: + is the gun's right side
    double yaw = 0;        // the body turned against the gun, degrees (+ = to its right)
    double tilt = 0;       // the muzzle lowered against the body, degrees
};
enum { HOLD_AIM, HOLD_HIP, HOLD_LOWERED, HOLD_COUNT };
struct Pose { HandHold firing, support, butt; BodyHold hold[HOLD_COUNT]; };
// A recorded hand animation (inspecting, reloading, working a control): keys on a timeline, each where both hands
// are then. The hands start and end in the aiming hold and move smoothly through the keys. From a key that grips,
// that hand holds the part it's on and pulls it along (through the physics) until a later key lets go. A key can
// also drop the magazine or put a fresh one in. A slot with no keys isn't animated: the plain action happens instead.
struct AnimKey {
    double t = 0;                        // seconds from the start
    HandHold firing, support;
    bool gripF = false, gripS = false;   // from here, the hand holds its part and moves it
    std::string event;                   // "" | "dropmag" (the magazine is let go) | "freshmag" (a new one goes in)
    double turn = -1;                    // a gripped part that turns (a bolt): how far it's turned from closed, degrees
                                         // (-1: the hand doesn't turn it)
};
struct Animation { std::vector<AnimKey> keys; };   // kept in time order
struct AnimHands { HandHold firing, support; bool gripF = false, gripS = false; double turn = -1; };
const char* holdName(int h);
const char* holdKey(int h);
double defaultHandAngle(const std::string& which, const std::string& style = "");   // firing, support, butt
std::string heldWith(const struct Design& d);  // "one hand", "two hands", or "" with no firing hand placed
BodyHold holdOf(const struct Design& d, int h); // the hold as posed, or one worked out from the gun
// animations: the slots a gun has (the fixed ones, then one per control other than the trigger), their names, how
// long one runs (its last key, then back to the hold), and where the hands are at a moment of it
std::vector<std::string> animSlots(const struct Design& d);
std::string animSlotName(const struct Design& d, const std::string& slot);
double animLength(const Animation& a);
AnimHands animAt(const struct Design& d, const Animation& a, double t);
struct Design {
    std::string name = "Untitled action";
    double speed = 0.02;                 // slow-motion speed for shots
    double friction = 0.12;              // contact friction coefficient
    std::vector<BodyDef> bodies;
    std::vector<JointDef> joints;
    BreechDef breech;
    Pose pose;                           // how it's held (nothing placed in older blueprints)
    std::map<std::string, Animation> anims;   // by slot: "work_action", "inspect", "reload", "reload_empty", "clear_jam", "control:<part id>"
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
void mirrorBody(BodyDef& b, V2 a, V2 c);
BodyDef mirrorImage(const BodyDef& src);                            // src reflected by its mirror link (shape and look)
bool mirrorMatches(const BodyDef& src, const BodyDef& other);       // other already is src's mirror image
int mirrorPartner(const Design& d, int bodyIndex);                  // the other part of its live mirror, or -1
// Keeps live mirrors in step. With `prev`: the part of each pair that changed since then is copied to the other (if
// both changed, `prefer` when it's in the pair). Without: `prefer` is copied to its partner. Returns parts updated.
int syncMirrors(Design& d, const Design* prev = nullptr, int prefer = -1);                         // reflect across the line through a and c
void flipBody(BodyDef& b, bool horizontal);                      // flip about its own centre
constexpr int MAX_CORNERS = 1024;                                 // corners a part can be given (older blueprints may have more)
int subdivideBody(BodyDef& b);                                   // add a vertex in the middle of every edge, longest first, up to
                                                                 // MAX_CORNERS; returns how many were added
void simplifyBody(BodyDef& b, int maxCorners = MAX_CORNERS);
// Two parts' side outlines combined: op 0 merge (union), 1 keep the overlap (intersect), 2 cut b away from a. False (with
// why) when there's nothing to combine. The result fits the corner limit, within a grid step or so of the true shape.
// How far (mm) a coil spring with nothing inside it can be squeezed before it buckles sideways: the spring
// standard's formula (EN 13906-1) for ends seated flat, from its free length and mean coil diameter. A spring
// short for its width never buckles (returns a huge number).
double bucklingSqueeze(double freeLen, double coilDia, double wire);
// Engravings (see MarkDef): keep them on their part when it's moved, turned, flipped or mirrored as a whole, or made
// thicker or moved in depth (call after edits; cheap when there are none). Reshaping the outline leaves them where they are.
void followMarks(BodyDef& b);
// The way a part's track lets it slide from where it sits (degrees, as BodyDef::ctrlDir: 0 forward, 90 down), if it has
// one: a button's natural push direction.
bool trackDirection(const Design& d, const std::string& partId, double& deg);
// A mark as lines to draw (design mm), each point with its surface direction: text is spelt out in a single-stroke
// lettering (capitals, figures and common signs; small letters are written as capitals).
struct MarkPt { P3d p, n; };
std::vector<std::vector<MarkPt>> markLines(const MarkDef& m);
std::vector<std::vector<V2>> strokeText(const std::string& text, double height, double* width = nullptr);   // flat, x right, y up, centred
P3d textRightFor(P3d n, double turnDeg);   // the way text reads on a surface facing n (design axes), turned by turnDeg
// Shaped in depth (see BodyDef::depthOff): whether a part is, keeping it in step after its outline changed (call after
// edits), its corners on the near and far faces (x, y, z in mm), how deep it reaches, and its outline where the plane
// at depth z cuts it (one or more pieces)
bool depthShaped(const BodyDef& b);
void normalizeDepth(BodyDef& b);
void depthRings(const BodyDef& b, std::vector<P3d>& nearR, std::vector<P3d>& farR);
void depthSpan(const BodyDef& b, double& z0, double& z1);
std::vector<std::vector<V2>> depthSection(const BodyDef& b, double z);
std::vector<int> triangulateOutline(const std::vector<V2>& p);   // ear clipping, any winding: index triples
// the solid as triangles (outward, x y z mm): near face, far face, then two per side wall (tag: -1 near, -2 far, else the edge)
void depthTriangles(const BodyDef& b, std::vector<P3d>& v, std::vector<int>& tri, std::vector<int>* tag = nullptr);
bool combineOutlines(const BodyDef& a, const BodyDef& b, int op, std::vector<V2>& out, std::string* why = nullptr,
                     std::vector<double>* rad = nullptr);   // given: rounded corners that survive keep their rounding
// Cuts corners off flat (a chamfer, or bevel in side view), each `size` mm back along both edges; `which` empty = all
// corners. Stops at the corner limit. Returns how many were cut.
int chamferCorners(BodyDef& b, double size, const std::vector<int>& which = {});    // drop the corners that change the shape least
int splitEdge(BodyDef& b, int edge);                             // add a vertex in the middle of one edge (on its curve); returns its index,
                                                                 // or -1 when the part already has MAX_CORNERS

// Magazine maker.
// Welding: parts that share a weld tag move as one piece. weldParts joins two parts (and whatever each was already
// welded to); unweld takes one part out of its piece. weldJointsToTags turns the weld joints older blueprints used
// into tags (a part welded to the ground becomes fixed).
void weldParts(Design& d, const std::string& a, const std::string& b);
void unweld(Design& d, const std::string& id);
std::vector<int> weldedWith(const Design& d, int i);   // the parts in i's piece (including i)
void weldJointsToTags(Design& d);
void generateMagazine(Design& d, int bodyIndex);               // rebuild the magazine shape and its well
V2 magCentre(const MagGen& g, double s, double* phiDeg);       // point on the magazine centreline, s mm below the lips

// JSON in the same format as the browser prototype (extra fields are ignored there).
std::string designToJson(const Design& d, bool pretty = false);   // stamps the current version and format
// A note for the user if the blueprint came from a newer Gunsmith3D, "" otherwise.
std::string blueprintVersionNote(const Design& d);
bool designFromJson(const std::string& text, Design& out, std::string* err = nullptr);

// Height of the baseplate (mm, y down): below every part, with room for magazines to drop clear.
double groundLevelMM(const Design& d);

// Barrels: where a barrel part's chamber mouth and muzzle are (design mm), and the firing system kept in step with the
// barrels drawn: the first is the breech's chamber, the rest its further chambers, each with its own cartridge.
// Does nothing in a design without barrel parts (older blueprints keep their placed breech face and muzzle).
bool barrelEnds(const BodyDef& b, V2& rear, V2& muzzle);
void syncBarrels(Design& d);
int barrelCount(const Design& d);
struct PresetInfo { const char* key; const char* label; };
const std::vector<PresetInfo>& presetList();
Design loadPreset(const std::string& key);
