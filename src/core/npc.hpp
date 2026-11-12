// Gunsmith3D - NPC designs (the NPC editor, under Map editing tools): an enemy or character built like the posing
// mannequin, but free: any number of bones, each a shaped piece hanging off another, posed where you want it, so it
// can be a soldier, a hound, a crawler with six legs, or something stranger. Plus what it's like in a fight (health,
// speed, how it attacks). Saved as JSON in user/npcs.
//
// Not alive yet: the editor builds and poses them; walking, fighting and spawning them on maps comes next (BACKLOG).
#pragma once
#include <string>
#include <vector>

struct NpcBone {
    std::string name = "Bone";
    int parent = -1;              // -1: the root (only bone 0)
    double at = 1;                // where it hangs off its parent: 0 the parent's start, 1 its end
    double side = 0;              // and how far across from there (mm, + to its right)
    double fwd = 0;               // and how far in front of it (mm, + the parent's front: an eye on a face, a pouch on a vest)
    double len = 200;             // mm
    double r0 = 40, r1 = 35;      // thickness (radius) at its start and its end, mm
    double bend = 0, turn = 0, twist = 0;   // degrees from its parent's direction: bend forward (+) or back, turn
                                            // to the side (+ its right), twist about its own length
    std::string shape = "capsule";          // capsule (rounded ends), cylinder, box, ball (an egg the bone's length),
                                            // wedge (a box thinning to an edge at its end)
    double width = 1, depth = 1;  // its cross-section stretched side to side and front to back (1 = round)
    double bulge = 0;             // swelling in the middle of its length (0.3: a muscle; negative: pinched)
    std::string color = "#8a8f98";
    std::string texture;          // a picture on it (user/textures or a built-in finish; "" = plain colour), tinted by
    double texScale = 150;        // the colour; mm one copy of it covers
    bool vital = false;           // a weak spot: hits here do triple damage (a head, a glowing core)
    double armor = 0;             // 0 .. 1: how much of a hit it stops
    bool gen = false;             // made by the character maker (rebuilt from its choices; your own gear isn't)
    bool body = false;            // drawn as part of the one-piece body (the character maker's torso, neck, arms and legs)
};

// The player character's choices (the character maker): the body is built from these, not shaped bone by bone
struct CharLook {
    bool on = false;              // false: an older character, shaped bone by bone
    std::string body = "male";    // male, female (a female body: narrower shoulders, wider hips, a chest)
    double height = 1, build = 1; // 0.85 .. 1.15 tall; 0.8 slim .. 1.3 heavy
    std::string skin = "#c8a088", eyes = "#4a6a8a";
    std::string hair = "short";   // none, buzz, short, long, ponytail, bun
    std::string hairColor = "#3b2a1e";
    std::string top = "tshirt";   // longsleeve, tshirt, tank
    std::string topColor = "#4f5d52";
    std::string pants = "long";   // long, shorts
    std::string pantsColor = "#3a3f36", shoes = "#26231f";
    bool belt = true, hat = false, headphones = true, glasses = false;
    std::string beltColor = "#2a2420", hatColor = "#4a5240", headphonesColor = "#3a4038", glassesColor = "#1c1e22";
};

struct NpcDef {
    std::string name = "New NPC";
    std::vector<NpcBone> bones;   // bones[0] is the root
    double rootHeight = 950;      // mm above the ground where the root starts
    double rootBend = 0, rootTurn = 0;   // the root's own direction: 0, 0 = straight up
    // in a fight (used once NPCs come alive on maps)
    double health = 100;
    double armorBar = 0;          // its armour bar (as plates give the player: 50 a plate), soaking most of each hit
    double speed = 1.4;           // m/s
    std::string attack = "melee"; // melee, ranged, explode
    double damage = 20;           // per hit (melee, explode)
    double reach = 1.5;           // m: how close it has to be to attack (ranged: how far it shoots)
    std::string gun;              // ranged: the blueprint it carries (user/guns)
    double sight = 30;            // m: how far it notices you
    std::string behaviour = "chase";   // stand, wander, chase, guard
    int team = 1;                 // who it fights (you are 0)
    CharLook look;                // the player character: built by the character maker
};
// The character maker: a person built from the choices (hands with fingers, a face, hair, clothes, extras). Gear of
// your own on `keep` (bones not made by the maker) is kept, on the bone of the same name.
NpcDef characterFromLook(const CharLook& look, const NpcDef* keep = nullptr);
void npcRelaxedStance(NpcDef& n, unsigned seed);   // standing at ease, a little different for each seed (character-maker people only)
bool npcCoreBone(const std::string& name);   // a human's own bones (the character can't lose them; gear can be added)

std::string npcToJson(const NpcDef& n);
bool npcFromJson(const std::string& text, NpcDef& n, std::string* err = nullptr);
const std::vector<std::pair<std::string, std::string>>& npcPresets();   // key, label
NpcDef npcPreset(const std::string& key);   // human, hound, crawler, blank

// Where every bone is: world millimetres, y up, the NPC standing at the origin facing +z. ax/ay/az: the bone's own
// axes (az... ay points along it).
struct NpcBoneAt { double a[3], b[3], x[3], y[3], z[3]; };
std::vector<NpcBoneAt> npcPose(const NpcDef& n);
// Aim bone i so it points along `dir` (world), keeping its twist: what dragging a bone's end does in the editor
void npcAimBone(NpcDef& n, int i, const double dir[3]);
// Turn bone i (and so everything hanging off it) by `deg` about `axis` (world, the right-hand way): what the rotation
// rings do. Its bend, turn and twist are worked out again from the turned frame.
void npcRotateBone(NpcDef& n, int i, const double axis[3], double deg);
void npcMirrorBone(NpcDef& n, int i);       // a copy of bone i and everything below it, on the other side
void npcRemoveBone(NpcDef& n, int i);       // and everything below it (not the root)
double npcHeight(const NpcDef& n);          // mm, ground to the highest point
// How thick a bone is at t (0 its start .. 1 its end), before width and depth: its taper and bulge (mm)
double npcBoneRadius(const NpcBone& b, double t);
double npcBoneReach(const NpcBone& b);      // the most it sticks out from its middle line anywhere (mm), for hits
