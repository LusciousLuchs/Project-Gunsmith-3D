// Gunsmith3D - Fitting attachments to a gun: where each one can go (the gun's rails and attachment points, and those
// on rail adapters already fitted), how it sits there, and whether it may: the right kind of mount, on the rail, not
// in another attachment or a solid part, and not in the way of a moving part's travel. No graphics here.
#pragma once
#include "design.hpp"
#include <string>
#include <vector>

namespace fit {

struct P3 { double x = 0, y = 0, z = 0; };
// An attachment's place on the gun: a point p of the attachment (its design mm) is at O + R (p - spot) on the gun
// (design mm: x forward, y down, z toward the right side), where spot is where the attachment meets its mount.
struct Xf {
    P3 O;
    double R[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    P3 apply(P3 p, P3 spot) const;
    P3 unapply(P3 g, P3 spot) const;   // back into the attachment's own mm
    P3 dir(P3 v) const;                // a direction
};

struct Site {                    // somewhere an attachment can go
    bool rail = false;           // a Picatinny rail, or an attachment point
    std::string type;            // "Picatinny", or the point's kind
    std::string part;            // the part carrying it (on the gun, or on an adapter)
    int owner = -1;              // -1: the gun; k: fitted item k (a rail adapter)
    int index = -1;              // a point: which of the part's points
    P3 at, along, out;           // rail: the start of its mounting face, its direction, out of the face; point: where, -, out
    double length = 0;           // a rail's length
    std::vector<double> slots;   // a rail's slot middles, from its start (mm)
    std::string key() const;     // names it in a loadout: "g:rail:<part>", "a2:pt:<part>:0"
};

struct Item {                    // an attachment fitted to the gun
    std::string file;            // its name in the Attachments folder
    Design att;
    std::string site;            // Site::key()
    double along = 0;            // on a rail: where its clamp's middle sits, mm from the rail's start (a slot's middle)
    Xf xf;
    P3 spot() const { return {att.attach.at.x, att.attach.at.y, att.attach.atZ}; }
};

// Every place to mount things: the gun's, then each fitted adapter's
std::vector<Site> sites(const Design& gun, const std::vector<Item>& items);
// How the attachment sits on the site (rails: with its clamp's middle `along` the rail)
Xf place(const Design& att, const Site& s, double along);
// "" if the attachment may go there; else why not, in game words. `skipItem`: an item being moved (left out of the checks)
std::string check(const Design& gun, const std::vector<Item>& items, const Design& att, const Site& s, double along, int skipItem = -1);
// the nearest slot middle on a rail to `along`
double snapAlong(const Site& s, double along);
// Picatinny matches Picatinny; point kinds match ignoring case and spaces at the ends
bool kindsMatch(const std::string& a, const std::string& b);

// Loadouts: which attachments go where on which gun, saved apart from the gun (its blueprint is never changed)
std::string loadoutToJson(const std::string& gun, const std::vector<Item>& items);
struct LoadoutEntry { std::string file, site; double along = 0; };
bool loadoutFromJson(const std::string& text, std::string& gun, std::vector<LoadoutEntry>& out);
}  // namespace fit
