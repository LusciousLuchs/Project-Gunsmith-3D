// Gunsmith3D - action kits (see kits.hpp).
#include "kits.hpp"
#include "carve.hpp"
#include "mounts.hpp"
#include <cmath>
#include <map>
#include <set>

const std::vector<KitInfo>& actionKits() {
    static const std::vector<KitInfo> k = {
        {"blowback", "blowback", "Straight blowback (pistol)", "The slide's weight and its spring hold the breech shut. Simplest of all; for small pistol rounds."},
        {"tilting", "tilting", "Tilting barrel, Browning cam lock (pistol)", "Barrel and slide locked together, then a cam drops the barrel's back to unlock. Most service pistols."},
        {"gaspistol", "gaspistol", "Gas-operated, locked slide (pistol)", "A latch holds the slide shut; gas from the barrel pushes a rod that frees and drives it."},
        {"longrecoil", "longrecoil", "Long recoil", "Barrel and bolt recoil the whole way together; the barrel returns first, the bolt follows."},
        {"boltaction", "boltaction", "Turn bolt, magazine fed (rifle)", "A bolt you lift, pull, push and turn down; locking lugs at the front, cocks on closing."},
        {"single", "single", "Turn bolt, single shot (rifle)", "The same bolt with no magazine: load each round by hand."},
        {"pump", "pump", "Pump action with tube magazine", "Slide the fore-end back and forward: an action bar works the bolt, a lifter raises each round from the tube."},
        {"pumpshotgun", "pumpshotgun", "Pump action, shotgun", "The pump action sized for 12-gauge shells."},
        {"revolver", "revolver", "Double-action revolver", "A turning cylinder of chambers lines each one up with the barrel; the trigger turns it and drops the hammer."},
        {"derringer", "derringer", "Break-open, two barrels", "Barrels hinged at the front that tip up to load; fires one barrel per pull."},
        {"belted", "belted", "Belt-fed blowback", "The blowback slide also works a pawl that pulls a belt of rounds through."},
    };
    return k;
}

Design kitDesign(const std::string& key) {
    for (auto& k : actionKits()) if (key == k.key) return loadPreset(k.preset);
    return Design();
}

void translateDesign(Design& d, double dx, double dy) {
    auto mv = [&](V2& p) { p.x += dx; p.y += dy; };
    for (auto& b : d.bodies) {
        for (auto& p : b.pts) mv(p);
        for (auto& p : b.roundPath) mv(p);
        for (auto& p : b.depthRef) mv(p);
        for (auto& p : b.meshRef) mv(p);
        for (auto& p : b.markRef) mv(p);
        if (b.gen.on) { mv(b.gen.top); for (auto& p : b.gen.pts0) mv(p); }
        if (b.ctrl != "none") mv(b.ctrlPt);
        mv(b.mirrorA); mv(b.mirrorB);
        carve::moveCarves(b, dx, dy);
        mounts::moveMounts(b, dx, dy);
        for (auto& m : b.marks) for (auto& p : m.pts) { p.x += dx; p.y += dy; }
        for (auto& v : b.mesh.v) { v.x += dx; v.y += dy; }
    }
    for (auto& j : d.joints) { mv(j.p); mv(j.pa); mv(j.pb); for (auto& q : j.path) mv(q); }
    auto& br = d.breech;
    mv(br.face); mv(br.muzzle); mv(br.strikerTip);
    for (auto& c : br.more) { mv(c.face); mv(c.muzzle); }
    for (HandHold* h : {&d.pose.firing, &d.pose.support, &d.pose.butt}) mv(h->at);
    for (auto& [slot, a] : d.anims) for (auto& k : a.keys) { mv(k.firing.at); mv(k.support.at); }
}

int insertKit(Design& D, const Design& kitIn, V2 at) {
    if (kitIn.bodies.empty()) return -1;
    Design kit = kitIn;
    V2 from = kit.breech.on && kit.breech.hasFace ? kit.breech.face : V2{0, 0};
    translateDesign(kit, at.x - from.x, at.y - from.y);
    // new names: the kit's ids, made unique against the gun's (a second kit gets _2, _3...)
    std::set<std::string> ids, names;
    for (auto& b : D.bodies) { ids.insert(b.id); names.insert(b.name); }
    std::map<std::string, std::string> nid;
    for (auto& b : kit.bodies) {
        std::string id = b.id;
        for (int n = 2; ids.count(id); n++) id = b.id + "_" + std::to_string(n);
        ids.insert(id); nid[b.id] = id;
    }
    auto ren = [&](std::string& s) { auto it = nid.find(s); if (it != nid.end()) s = it->second; };
    std::map<std::string, std::string> tag;   // weld and mirror tags: the kit's own, kept apart from the gun's
    std::string pre = "kit" + std::to_string(D.bodies.size()) + "-";
    auto retag = [&](std::string& t) { if (t.empty()) return; if (!tag.count(t)) tag[t] = pre + t; t = tag[t]; };
    int first = (int)D.bodies.size();
    for (auto b : kit.bodies) {
        ren(b.id); ren(b.feedPawl); ren(b.rail.base);
        retag(b.weld); retag(b.mirror);
        std::string base = b.name;
        for (int n = 2; names.count(b.name); n++) b.name = base + " (" + std::to_string(n) + ")";
        names.insert(b.name);
        D.bodies.push_back(b);
    }
    for (auto j : kit.joints) { ren(j.a); ren(j.b); ren(j.guide); ren(j.rel); D.joints.push_back(j); }
    BreechDef br = kit.breech;
    ren(br.breech); ren(br.chamber); ren(br.striker);
    for (auto& c : br.more) { ren(c.breech); ren(c.chamber); }
    for (auto& g : br.gas) ren(g.piston);
    ren(br.cylinder.indexBy);
    D.breech = br;
    if (!D.pose.firing.on) {   // a gun with no hands yet: hold it the way the kit's example is held
        Pose P = kit.pose;
        for (HandHold* h : {&P.firing, &P.support, &P.butt}) ren(h->part);
        D.pose = P;
    }
    return first;
}

bool untouchedBlank(const Design& D) {
    Design blank = loadPreset("blank");
    if (D.bodies.size() != 1 || !D.joints.empty() || D.breech.on || blank.bodies.size() != 1) return false;
    const auto &a = D.bodies[0], &b = blank.bodies[0];
    if (a.id != b.id || a.pts.size() != b.pts.size()) return false;
    for (size_t i = 0; i < a.pts.size(); i++) if (std::fabs(a.pts[i].x - b.pts[i].x) > 1e-6 || std::fabs(a.pts[i].y - b.pts[i].y) > 1e-6) return false;
    return true;
}
