// Gunsmith3D - live mirrors: two parts kept as mirror images of each other (see BodyDef::mirror).
#include "carve.hpp"
#include "design.hpp"
#include <cmath>
#include <map>

namespace {
bool near(double a, double b) { return std::fabs(a - b) < 1e-6; }
bool sameV(V2 a, V2 b) { return near(a.x, b.x) && near(a.y, b.y); }
bool sameCarve(const CarveDef& a, const CarveDef& b) {
    return a.shape == b.shape && sameV(a.at, b.at) && near(a.atZ, b.atZ) && near(a.dx, b.dx) && near(a.dy, b.dy) && near(a.dz, b.dz) &&
           near(std::remainder(a.spin - b.spin, 360.0), 0) && near(a.w, b.w) && near(a.h, b.h) && near(a.depth, b.depth);
}
// what a mirror carries over: the shape and the look; how it moves, joins and feeds stays each part's own
void copyLook(const BodyDef& from, BodyDef& to) {
    to.pts = from.pts; to.rad = from.rad; to.bulge = from.bulge; to.carves = from.carves;
    to.depthOff = from.depthOff; to.depthZn = from.depthZn; to.depthZf = from.depthZf; to.depthRef = from.depthRef;
    to.z = from.z; to.thick = from.thick; to.tiltX = from.tiltX; to.tiltY = from.tiltY;
    to.color = from.color; to.texture = from.texture; to.texScale = from.texScale; to.smooth = from.smooth;
    to.round = from.round; to.roundSides = from.roundSides; to.roundTurn = from.roundTurn; to.roundSweep = from.roundSweep;
    to.roundAxis = from.roundAxis; to.mass = from.mass;
}
bool sameLook(const BodyDef& a, const BodyDef& b) {
    if (a.pts.size() != b.pts.size() || a.carves.size() != b.carves.size()) return false;
    for (size_t i = 0; i < a.pts.size(); i++) {
        if (!sameV(a.pts[i], b.pts[i])) return false;
        if (!near(i < a.rad.size() ? a.rad[i] : 0, i < b.rad.size() ? b.rad[i] : 0)) return false;
        if (!near(i < a.bulge.size() ? a.bulge[i] : 0, i < b.bulge.size() ? b.bulge[i] : 0)) return false;
    }
    for (size_t i = 0; i < a.carves.size(); i++) if (!sameCarve(a.carves[i], b.carves[i])) return false;
    if (a.depthOff.size() != b.depthOff.size()) return false;
    for (size_t i = 0; i < a.depthOff.size(); i++)
        if (!sameV(a.depthOff[i], b.depthOff[i]) || !near(a.depthZn[i], b.depthZn[i]) || !near(a.depthZf[i], b.depthZf[i])) return false;
    return near(a.z, b.z) && near(a.thick, b.thick) && near(a.tiltX, b.tiltX) && near(a.tiltY, b.tiltY) && a.color == b.color &&
           a.texture == b.texture && near(a.texScale, b.texScale) && near(a.smooth, b.smooth) && a.round == b.round &&
           a.roundSides == b.roundSides && near(a.roundTurn, b.roundTurn) && near(a.roundSweep, b.roundSweep) &&
           a.roundAxis == b.roundAxis && near(a.mass, b.mass);
}
}  // namespace

BodyDef mirrorImage(const BodyDef& src) {
    BodyDef c = src;
    fixShape(c);
    if (src.mirrorKind == "line") {
        mirrorBody(c, src.mirrorA, src.mirrorB);
    } else {   // across the gun: the same outline on the far side, its cuts and lean turned round
        c.z = -c.z;
        c.marks.clear();   // engravings are each side's own (text copied across would read backwards)
        carve::flipCarvesZ(c);
        c.tiltX = -c.tiltX; c.tiltY = -c.tiltY;
        if (depthShaped(c)) {   // shaped in depth: its near and far faces change places
            size_t n = c.pts.size();
            for (size_t i = 0; i < n; i++) {
                V2 far{c.pts[i].x + c.depthOff[i].x, c.pts[i].y + c.depthOff[i].y};
                double zn = c.depthZn[i], zf = c.depthZf[i];
                c.depthOff[i] = {c.pts[i].x - far.x, c.pts[i].y - far.y};
                c.pts[i] = far;
                c.depthZn[i] = -zf; c.depthZf[i] = -zn;
            }
            c.depthRef = c.pts;
        }
    }
    return c;
}

bool mirrorMatches(const BodyDef& src, const BodyDef& other) { return sameLook(mirrorImage(src), other); }

int mirrorPartner(const Design& d, int bi) {
    if (bi < 0 || bi >= (int)d.bodies.size() || d.bodies[bi].mirror.empty()) return -1;
    int found = -1, count = 0;
    for (int i = 0; i < (int)d.bodies.size(); i++)
        if (i != bi && d.bodies[i].mirror == d.bodies[bi].mirror) { found = i; count++; }
    return count == 1 ? found : -1;   // a pair, or nothing
}

int syncMirrors(Design& d, const Design* prev, int prefer) {
    std::map<std::string, std::vector<int>> pairs;
    for (int i = 0; i < (int)d.bodies.size(); i++) if (!d.bodies[i].mirror.empty()) pairs[d.bodies[i].mirror].push_back(i);
    int made = 0;
    for (auto& [tag, ix] : pairs) {
        if (ix.size() != 2) continue;
        int a = ix[0], b = ix[1], src = -1;
        bool inPair = prefer == a || prefer == b;
        if (prev) {   // the one that changed; if both did, the preferred one
            auto changed = [&](int i) {
                int pi = prev->bodyIndex(d.bodies[i].id);
                return pi < 0 || !sameLook(prev->bodies[pi], d.bodies[i]);
            };
            bool ca = changed(a), cb = changed(b);
            src = ca && !cb ? a : cb && !ca ? b : ca ? (inPair ? prefer : a) : -1;
        } else if (inPair) src = prefer;
        if (src < 0) continue;
        int dst = src == a ? b : a;
        if (mirrorMatches(d.bodies[src], d.bodies[dst])) continue;
        BodyDef img = mirrorImage(d.bodies[src]);
        BodyDef& t = d.bodies[dst];
        copyLook(img, t);
        if (img.ctrl != "none" && t.ctrl != "none") { t.ctrlPt = img.ctrlPt; t.ctrlDir = img.ctrlDir; }
        made++;
    }
    return made;
}
