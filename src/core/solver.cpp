#include "solver_internal.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <map>
#include <functional>

namespace sim {

static constexpr double DENS = 7.85e-6;  // kg per mm^3, steel

// ---------- geometry ----------
double polyArea(const std::vector<V2>& p) {
    double a = 0;
    for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) a += p[j].x * p[i].y - p[i].x * p[j].y;
    return a / 2;
}
V2 polyCentroid(const std::vector<V2>& p) {
    double a = 0, cx = 0, cy = 0;
    for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) {
        double f = p[j].x * p[i].y - p[i].x * p[j].y;
        a += f; cx += (p[j].x + p[i].x) * f; cy += (p[j].y + p[i].y) * f;
    }
    if (std::fabs(a) < 1e-12) {
        V2 s; for (auto& q : p) { s.x += q.x; s.y += q.y; }
        return {s.x / p.size(), s.y / p.size()};
    }
    a /= 2;
    return {cx / (6 * a), cy / (6 * a)};
}
bool inPoly(double x, double y, const std::vector<V2>& p) {
    bool c = false;
    for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) {
        double xi = p[i].x, yi = p[i].y, xj = p[j].x, yj = p[j].y;
        if (((yi > y) != (yj > y)) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) c = !c;
    }
    return c;
}
V2 closestOnSeg(V2 p, V2 a, V2 b, double* d2) {
    double dx = b.x - a.x, dy = b.y - a.y, l = dx * dx + dy * dy;
    if (l < 1e-18) l = 1e-18;
    double t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / l;
    t = std::clamp(t, 0.0, 1.0);
    V2 q{a.x + t * dx, a.y + t * dy};
    if (d2) *d2 = (p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y);
    return q;
}
void interpTable(const std::vector<V2>& tab0, double u, double& val, double& slope) {
    val = 0; slope = 0;
    if (tab0.empty()) return;
    std::vector<V2> t = tab0;
    std::sort(t.begin(), t.end(), [](const V2& a, const V2& b) { return a.x < b.x; });
    if (u <= t.front().x) { val = t.front().y; return; }
    if (u >= t.back().x) { val = t.back().y; return; }
    for (size_t i = 0; i + 1 < t.size(); i++) {
        if (u >= t[i].x && u <= t[i + 1].x) {
            double dx = t[i + 1].x - t[i].x;
            slope = dx > 1e-9 ? (t[i + 1].y - t[i].y) / dx : 0;
            val = t[i].y + slope * (u - t[i].x);
            return;
        }
    }
}

V2 wpt(const Body& b, V2 l) {
    double c = std::cos(b.a), s = std::sin(b.a);
    return {b.x + c * l.x - s * l.y, b.y + s * l.x + c * l.y};
}
V2 lpt(const Body& b, V2 w) {
    double c = std::cos(b.a), s = std::sin(b.a), dx = w.x - b.x, dy = w.y - b.y;
    return {c * dx + s * dy, -s * dx + c * dy};
}
double axial(const Body& A, const Body& B, V2 rel0) { return lpt(B, {A.x, A.y}).x - rel0.x; }

Body* Sim::find(const std::string& id) {
    if (id.empty() || id == "world") return &world;
    for (auto& b : bodies) if (b.id == id) return &b;
    return nullptr;
}

V2 designToWorld(const Sim& S, const std::string& id, V2 p) {
    if (id.empty() || id == "world") return p;
    for (auto& b : S.bodies) {
        if (b.id != id) continue;
        if (b.fixed) return p;
        V2 w = wpt(b, {p.x / 1000 - b.x0, p.y / 1000 - b.y0});
        return {w.x * 1000, w.y * 1000};
    }
    return p;
}

// ---------- XPBD primitives ----------
void placePieces(Body& r) {  // welded pieces ride on their root
    double c = std::cos(r.a), s = std::sin(r.a);
    for (Body* p : r.pieces) {
        p->x = r.x + c * p->off.x - s * p->off.y; p->y = r.y + s * p->off.x + c * p->off.y; p->a = r.a + p->aOff;
    }
}
void solve(Body& A0, Body& B0, V2 pA, V2 pB, double nx, double ny, double c) {
    Body& A = A0.root ? *A0.root : A0;  // a welded piece is pushed through its whole assembly
    Body& B = B0.root ? *B0.root : B0;
    if (&A == &B) return;
    double rax = pA.x - A.x, ray = pA.y - A.y, rbx = pB.x - B.x, rby = pB.y - B.y;
    double ca = rax * ny - ray * nx, cb = rbx * ny - rby * nx;
    double w = A.invM + A.invI * ca * ca + B.invM + B.invI * cb * cb;
    if (w <= 0) return;
    double l = c / w;
    A.x += nx * l * A.invM; A.y += ny * l * A.invM; A.a += A.invI * ca * l;
    B.x -= nx * l * B.invM; B.y -= ny * l * B.invM; B.a -= B.invI * cb * l;
    if (A.fixed) { A.rfx += nx * l / H; A.rfy += ny * l / H; }  // what a fixed part soaks up, for the felt recoil
    if (B.fixed) { B.rfx -= nx * l / H; B.rfy -= ny * l / H; }
    if (!A.pieces.empty()) placePieces(A);
    if (!B.pieces.empty()) placePieces(B);
}
// Contacts use a capped version: a tiny part struck by a heavy fast one (a sear hit by a recoiling slide) would
// otherwise be spun further in one step than its stops can catch. Nothing real turns more than ~0.04 rad per
// contact correction in 1/8000 s; anything left over is resolved over the next steps.
static void solveContact(Body& A0, Body& B0, V2 pA, V2 pB, double nx, double ny, double c) {
    Body& A = A0.root ? *A0.root : A0;
    Body& B = B0.root ? *B0.root : B0;
    if (&A == &B) return;
    double rax = pA.x - A.x, ray = pA.y - A.y, rbx = pB.x - B.x, rby = pB.y - B.y;
    double ca = rax * ny - ray * nx, cb = rbx * ny - rby * nx;
    double w = A.invM + A.invI * ca * ca + B.invM + B.invI * cb * cb;
    if (w <= 0) return;
    double l = c / w, rot = std::max(std::fabs(A.invI * ca * l), std::fabs(B.invI * cb * l));
    (void)rot;  // (a cap on the rotation here made stops too soft for fast light parts)
    solve(A0, B0, pA, pB, nx, ny, c);
}
void solveAng(Body& A0, Body& B0, double target) {
    Body& A = A0.root ? *A0.root : A0;
    Body& B = B0.root ? *B0.root : B0;
    if (&A == &B) return;
    double w = A.invI + B.invI;
    if (w <= 0) return;
    double C = A0.a - B0.a - target, l = -C / w;
    A.a += A.invI * l; B.a -= B.invI * l;
    if (!A.pieces.empty()) placePieces(A);
    if (!B.pieces.empty()) placePieces(B);
}
static void solveAxial(Body& A, Body& B, double c) {
    double ax = std::cos(B.a), ay = std::sin(B.a);
    solve(A, B, {A.x, A.y}, {A.x, A.y}, ax, ay, c);
}

// ---------- build ----------
static constexpr int MAX_SPAWNED = 200;  // magazines and loose rounds added during a test
static void reshapeRolled(Body& b);
void resample(Body& b);
std::unique_ptr<Sim> finishBuild(std::unique_ptr<Sim>& S);

std::unique_ptr<Sim> build(const Design& design) {
    auto S = std::make_unique<Sim>();
    S->design = design;
    Sim& s = *S;
    s.world.id = "world";
    s.bodies.reserve(s.design.bodies.size() + MAX_SPAWNED);  // body pointers must stay valid as magazines and rounds spawn
    s.groundY = groundLevelMM(s.design) / 1000;
    for (auto& d : s.design.bodies) {
        Body b;
        if (!makeBody(d, b)) continue;
        s.bodies.push_back(b);
    }
    return finishBuild(S);
}

bool makeBody(const BodyDef& d, Body& b) {
    {
        std::vector<V2> shape = outlineOf(d);
        double A = std::fabs(polyArea(shape));
        if (A < 1 || shape.size() < 3) return false;
        V2 c = polyCentroid(shape);
        b.d = &d; b.id = d.id; b.fixed = d.fixed; b.damp = d.damp;
        for (auto& p : shape) b.lp.push_back({(p.x - c.x) / 1000, (p.y - c.y) / 1000});
        b.m = d.mass > 0 ? d.mass / 1000 : A * (d.thick > 0 ? d.thick : 10) * DENS;
        double sig = b.m / (A * 1e-6), num = 0;
        for (size_t i = 0; i < b.lp.size(); i++) {
            V2 p1 = b.lp[i], p2 = b.lp[(i + 1) % b.lp.size()];
            num += (p1.x * p2.y - p2.x * p1.y) * (p1.x * p1.x + p1.x * p2.x + p2.x * p2.x + p1.y * p1.y + p1.y * p2.y + p2.y * p2.y);
        }
        b.I = std::fabs(sig * num / 12);
        b.I = std::max(b.I, b.m * 0.004 * 0.004);  // tiny parts (sears, plungers) get at least a 4 mm radius of gyration
        double ymin = 1e9, ymax = -1e9;
        for (auto& p : b.lp) { ymin = std::min(ymin, p.y); ymax = std::max(ymax, p.y); }
        double r = std::max(0.002, (ymax - ymin) / 2);
        b.Ir = 0.5 * b.m * r * r;
        b.invM = b.fixed ? 0 : 1 / b.m;
        b.invI = b.fixed ? 0 : 1 / b.I;
        b.invIr = (d.rollOn && !b.fixed) ? 1 / b.Ir : 0;
        b.rmin = d.rollOn ? d.rollMin * D2R : 0;
        b.rmax = d.rollOn ? d.rollMax * D2R : 0;
        resample(b);
        b.x = b.x0 = c.x / 1000; b.y = b.y0 = c.y / 1000;
        b.z = d.z / 1000; b.zh = std::max(0.5, d.thick) / 2000;
        b.isMag = d.magazine && !d.fixed;
        b.rounds = b.isMag ? d.magCap : 0;
    }
    return true;
}

std::unique_ptr<Sim> finishBuild(std::unique_ptr<Sim>& S) {
    Sim& s = *S;
    auto anc = [](const Body& b, V2 p) { return V2{p.x / 1000 - b.x0, p.y / 1000 - b.y0}; };
    auto rel = [](const Body& B, const Body& A) { return V2{A.x0 - B.x0, A.y0 - B.y0}; };
    std::set<std::string> welded;
    for (size_t i = 0; i < s.design.joints.size(); i++) {
        const JointDef& j = s.design.joints[i];
        Body* A = s.find(j.a);
        Body* B = s.find(j.b);
        if (!A || !B || A == B) continue;
        Joint J; J.j = &j; J.defIndex = (int)i; J.A = A; J.B = B;
        if (j.type == "pin" || j.type == "weld") {
            J.type = j.type == "pin" ? JT::Pin : JT::Weld;
            J.la = anc(*A, j.p); J.lb = anc(*B, j.p);
            welded.insert(A->id + "|" + B->id); welded.insert(B->id + "|" + A->id);
        } else if (j.type == "track") {
            if (j.path.size() < 2) continue;
            J.type = JT::Track; J.la = anc(*A, j.p);
            for (auto& p : j.path) J.path.push_back(anc(*B, p));
        } else if (j.type == "spring") {
            J.type = JT::Spring; J.la = anc(*A, j.pa); J.lb = anc(*B, j.pb);
            if (j.kind == "leaf") {  // push direction: perpendicular to the leaf, in the root part's frame
                double dx = j.pb.x - j.pa.x, dy = j.pb.y - j.pa.y, l = std::hypot(dx, dy);
                if (l < 1e-9) l = 1;
                J.nrm = {-dy / l, dx / l};
                if (j.flip) J.nrm = {-J.nrm.x, -J.nrm.y};
                J.rel0 = anc(*A, j.pb);  // where the leaf tip sits, carried by the root part
            }
        } else if (j.type == "magwell") {
            if (j.path.size() < 2 || !A->isMag || s.mwIdx >= 0) continue;
            J.type = JT::Magwell; J.la = anc(*A, j.p);
            for (auto& p : j.path) J.path.push_back(anc(*B, p));
            J.attached = true; J.latched = true;
            s.mwIdx = (int)s.joints.size();
            s.magTmpl = A->d;
            J.R = j.rel.empty() ? nullptr : s.find(j.rel);
            if (J.R == &s.world) J.R = nullptr;
            welded.insert(A->id + "|" + B->id); welded.insert(B->id + "|" + A->id);
        } else if (j.type == "cam" || j.type == "lug" || j.type == "latch") {
            J.type = j.type == "cam" ? JT::Cam : j.type == "lug" ? JT::Lug : JT::Latch;
            J.rel0 = rel(*B, *A);
            if (J.type == JT::Latch) { J.R = s.find(j.rel); if (J.R == &s.world) J.R = nullptr; if (J.R) J.relR0 = rel(*B, *J.R); }
        } else continue;
        s.joints.push_back(J);
    }
    {   // welded parts become rigid assemblies: union the weld groups, pick a root, fold the pieces' mass into it
        std::vector<int> grp(s.bodies.size());
        for (size_t i = 0; i < grp.size(); i++) grp[i] = (int)i;
        std::function<int(int)> top = [&](int i) { return grp[i] == i ? i : grp[i] = top(grp[i]); };
        auto idx = [&](Body* b) { return b == &s.world ? -1 : (int)(b - s.bodies.data()); };
        for (auto& J : s.joints) {
            if (J.type != JT::Weld) continue;
            int a = idx(J.A), b = idx(J.B);
            if (a < 0 || b < 0) continue;
            grp[top(a)] = top(b);
            J.merged = true;
        }
        std::map<int, int> rootOf;
        for (size_t i = 0; i < s.bodies.size(); i++) {
            int g = top((int)i);
            auto it = rootOf.find(g);
            if (it == rootOf.end()) { rootOf[g] = (int)i; continue; }
            Body& cur = s.bodies[it->second]; Body& b = s.bodies[i];
            auto rank = [](const Body& x) { return (x.fixed ? 1e6 : 0) + (x.invIr > 0 ? 1e3 : 0) + x.m; };
            if (rank(b) > rank(cur)) it->second = (int)i;
        }
        for (size_t i = 0; i < s.bodies.size(); i++) {
            Body& r = s.bodies[rootOf[top((int)i)]];
            Body& p = s.bodies[i];
            if (&r == &p) continue;
            p.root = &r;
            p.off = {p.x0 - r.x0, p.y0 - r.y0};
            r.pieces.push_back(&p);
            if (r.fixed) { p.fixed = true; p.invM = p.invI = p.invIr = 0; continue; }
            double d2 = p.off.x * p.off.x + p.off.y * p.off.y;
            r.I += p.I + p.m * d2;
            if (r.invIr > 0) r.Ir += p.m * ((p.y0 - r.y0) * (p.y0 - r.y0) + (p.z - r.z) * (p.z - r.z));
            r.m += p.m;
            r.invM = 1 / r.m; r.invI = 1 / r.I;
            if (r.invIr > 0) r.invIr = 1 / r.Ir;
            p.fixed = false; p.invM = p.invI = p.invIr = 0;
        }
    }
    for (size_t i = 0; i < s.bodies.size(); i++)
        for (size_t k = i + 1; k < s.bodies.size(); k++) {
            Body& a = s.bodies[i]; Body& b = s.bodies[k];
            if (a.fixed && b.fixed) continue;
            if ((a.root ? a.root : &a) == (b.root ? b.root : &b)) continue;  // one rigid assembly
            const std::string& la = a.d->layer; const std::string& lb = b.d->layer;
            if (la == "none" || lb == "none") continue;
            bool share = false;  // a part can sit on several layers ("AB")
            for (char c : la) if (c >= 'A' && c <= 'C' && lb.find(c) != std::string::npos) share = true;
            if (!share) continue;
            if (!a.rollParent && !b.rollParent && std::fabs(a.z - b.z) >= a.zh + b.zh - 1e-5) continue;  // side by side in depth: they never meet
            if (welded.count(a.id + "|" + b.id)) continue;
            s.pairs.push_back({&a, &b});
        }
    for (auto& bb : s.bodies) {  // pieces of a rotating bolt turn with it
        Body* ch = &bb;
        Body* par = ch->root;
        if (!par || par->invIr <= 0 || ch->fixed) continue;
        ch->rollParent = par;
        ch->lp0 = ch->lp; ch->z0 = ch->z; ch->zh0 = ch->zh;
        ch->axisDy = ch->y0 - par->y0;  // drawn level: the bolt's axis runs through its centre
        ch->axisDz = ch->z - par->z;
        ch->rollShaped = 1e9;
        reshapeRolled(*ch);
    }
    const BreechDef& br = s.design.breech;
    if (br.on) {
        s.BR = s.find(br.breech);
        if (s.BR == &s.world) s.BR = nullptr;
        s.CH = s.find(br.chamber);
        if (!s.CH) s.CH = &s.world;
        if (s.BR && br.hasFace) { s.hasFace = true; s.faceB = anc(*s.BR, br.face); s.faceC = anc(*s.CH, br.face); }
        if (br.hasMuzzle) { s.hasMuzzle = true; s.muzC = anc(*s.CH, br.muzzle); }
        s.chambered = br.loaded;
        s.rounds = br.rounds;
        s.feedMode = br.feedMode;
        if (br.ignition == "striker" && !br.striker.empty()) {
            s.STR = s.find(br.striker);
            if (s.STR == &s.world) s.STR = nullptr;
            if (s.STR) { s.strikerIgnition = true; s.strTip = anc(*s.STR, br.strikerTip); }
        }
    }
    for (auto& b : s.bodies) {  // controls bound to keys
        const BodyDef& d = *b.d;
        if (d.ctrl == "none" || d.key == "none" || b.fixed) continue;
        Pusher p; p.b = &b; p.l = anc(b, d.ctrlPt);
        p.dx = std::cos(d.ctrlDir * D2R); p.dy = std::sin(d.ctrlDir * D2R);
        p.force = d.ctrlForce; p.key = d.key;
        s.pushers.push_back(p);
    }
    cartridgeSetup(s);
    if (s.BR) {  // does a spring close the breech? then an action stuck open is a jam, not a bolt left open
        Body* br = s.BR->root ? s.BR->root : s.BR;
        for (auto& J : s.joints)
            if (J.type == JT::Spring && ((J.A->root ? J.A->root : J.A) == br || (J.B->root ? J.B->root : J.B) == br)) s.breechSprung = true;
    }
    return std::move(S);
}

// ---------- parts that turn with a bolt ----------
static std::vector<V2> hull(std::vector<V2> p) {
    std::sort(p.begin(), p.end(), [](V2 a, V2 b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    if (p.size() < 3) return p;
    std::vector<V2> h(2 * p.size());
    size_t k = 0;
    auto cr = [](V2 o, V2 a, V2 b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); };
    for (size_t i = 0; i < p.size(); i++) { while (k >= 2 && cr(h[k - 2], h[k - 1], p[i]) <= 0) k--; h[k++] = p[i]; }
    for (size_t i = p.size() - 1, t = k + 1; i > 0; i--) { while (k >= t && cr(h[k - 2], h[k - 1], p[i - 1]) <= 0) k--; h[k++] = p[i - 1]; }
    h.resize(k - 1);
    return h;
}
// Contact points along the outline: about 40 round a small part (a sear nose has millimetre faces),
// never further apart than 2.5 mm on big ones.
void resample(Body& b) {
    double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
    for (auto& p : b.lp) { x0 = std::min(x0, p.x); x1 = std::max(x1, p.x); y0 = std::min(y0, p.y); y1 = std::max(y1, p.y); }
    double step = std::clamp(std::max(x1 - x0, y1 - y0) / 20, 0.0004, 0.0025);
    b.samples.clear();
    for (size_t i = 0; i < b.lp.size(); i++) {
        V2 p = b.lp[i], q = b.lp[(i + 1) % b.lp.size()];
        int n = std::max(1, (int)std::ceil(std::hypot(q.x - p.x, q.y - p.y) / step));
        for (int k = 0; k < n; k++) b.samples.push_back({p.x + (q.x - p.x) * k / n, p.y + (q.y - p.y) * k / n});
    }
}
// Seen from the side, a lug turning about the bore swings up and over and moves across in depth. Each point of the
// part, at its near and far faces, is turned about the bolt's axis; the side outline is their hull and the depth
// range their spread. (Parts that turn with a bolt are treated as convex.)
static void reshapeRolled(Body& b) {
    double th = b.rollParent->roll;
    if (std::fabs(th - b.rollShaped) < 0.003 && !b.lp.empty()) return;
    b.rollShaped = th;
    double c = std::cos(th), s = std::sin(th), zlo = b.axisDz - b.zh0, zhi = b.axisDz + b.zh0;
    std::vector<V2> pts;
    double zmin = 1e9, zmax = -1e9;
    for (auto& p : b.lp0)
        for (double dz : {zlo, zhi}) {
            double dy = b.axisDy + p.y;
            double y2 = dy * c - dz * s, z2 = dz * c + dy * s;
            pts.push_back({p.x, y2 - b.axisDy});
            zmin = std::min(zmin, z2); zmax = std::max(zmax, z2);
        }
    b.lp = hull(pts);
    b.z = b.rollParent->z + (zmin + zmax) / 2;
    b.zh = (zmax - zmin) / 2;
    resample(b);
}

// ---------- contacts ----------
void bbox(Body& b) {
    double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9, c = std::cos(b.a), s = std::sin(b.a);
    for (auto& l : b.lp) {
        double x = b.x + c * l.x - s * l.y, y = b.y + s * l.x + c * l.y;
        x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y);
    }
    b.bb[0] = x0; b.bb[1] = y0; b.bb[2] = x1; b.bb[3] = y1;
}
void contacts(Body& A, Body& B, double mu) {  // push B's boundary samples out of A, with friction
    // (the hot loop of the whole simulation: transforms are worked out once, and a sample is tested against A's
    // outline only if it falls inside A's own bounding box)
    double lx0 = 1e9, ly0 = 1e9, lx1 = -1e9, ly1 = -1e9;
    for (auto& q : A.lp) { lx0 = std::min(lx0, q.x); lx1 = std::max(lx1, q.x); ly0 = std::min(ly0, q.y); ly1 = std::max(ly1, q.y); }
    const size_t n = A.lp.size();
    double cB = std::cos(B.a), sB = std::sin(B.a), cA = std::cos(A.a), sA = std::sin(A.a);
    for (auto& s : B.samples) {
        V2 w{B.x + cB * s.x - sB * s.y, B.y + sB * s.x + cB * s.y};
        if (w.x < A.bb[0] || w.x > A.bb[2] || w.y < A.bb[1] || w.y > A.bb[3]) continue;
        double dx = w.x - A.x, dy = w.y - A.y;
        V2 l{cA * dx + sA * dy, -sA * dx + cA * dy};
        if (l.x < lx0 || l.x > lx1 || l.y < ly0 || l.y > ly1) continue;
        if (!inPoly(l.x, l.y, A.lp)) continue;
        double best = 1e18; V2 bq;
        for (size_t i = 0; i < n; i++) {
            double d2; V2 q = closestOnSeg(l, A.lp[i], A.lp[i + 1 < n ? i + 1 : 0], &d2);
            if (d2 < best) { best = d2; bq = q; }
        }
        double d = std::sqrt(best);
        if (d < 1e-7 || d > 0.005) continue;
        V2 qw{A.x + cA * bq.x - sA * bq.y, A.y + sA * bq.x + cA * bq.y};
        double nx = (qw.x - w.x) / d, ny = (qw.y - w.y) / d;
        solveContact(B, A, w, qw, nx, ny, d);
        if (mu > 0) {  // friction: oppose the slip of the two touching points over this substep
            auto at = [](double x, double y, double a, V2 p) { double c = std::cos(a), sn = std::sin(a); return V2{x + c * p.x - sn * p.y, y + sn * p.x + c * p.y}; };
            V2 wb = wpt(B, s), wb0 = at(B.px, B.py, B.pa, s), wa = wpt(A, bq);
            V2 wa0 = A.fixed ? wa : at(A.px, A.py, A.pa, bq);
            double sx = (wb.x - wb0.x) - (wa.x - wa0.x), sy = (wb.y - wb0.y) - (wa.y - wa0.y);
            double sn = sx * nx + sy * ny;
            sx -= sn * nx; sy -= sn * ny;
            double sl = std::hypot(sx, sy);
            if (sl > 1e-9) solveContact(B, A, wb, wb, -sx / sl, -sy / sl, std::min(sl, mu * d));
            cB = std::cos(B.a); sB = std::sin(B.a); cA = std::cos(A.a); sA = std::sin(A.a);
        } else {
            cB = std::cos(B.a); sB = std::sin(B.a); cA = std::cos(A.a); sA = std::sin(A.a);  // the push turned them a little
        }
    }
}

bool inBattery(const Sim& S) {
    if (!S.BR || !S.hasFace) return false;
    const double tol = 0.001;  // parts riding in drawn rails have a little play
    if (breechGap(S) > 1.0) return false;
    for (const Body* b : {S.BR, S.CH}) {
        if (b->fixed) continue;
        if (std::hypot(b->x - b->x0, b->y - b->y0) > tol || std::fabs(b->a) > 1.0 * D2R) return false;  // gravity can tip a part a little
        if (b->invIr > 0 && std::fabs(b->roll) > 5 * D2R) return false;
    }
    return true;
}
double breechGap(const Sim& S) {
    if (!S.BR || !S.hasFace) return 0;
    V2 a = wpt(*S.BR, S.faceB), b = wpt(*S.CH, S.faceC);
    return std::hypot(a.x - b.x, a.y - b.y) * 1000;
}

// ---------- magazines ----------
Joint* magwell(Sim& S) { return S.mwIdx >= 0 ? &S.joints[S.mwIdx] : nullptr; }
Body* seatedMag(Sim& S) {
    Joint* J = magwell(S);
    return (J && J->A && J->attached && J->latched) ? J->A : nullptr;
}
static void solveMagwell(Sim& S, Joint& J) {
    Body& B = *J.B;
    V2 seat = wpt(B, J.path.front()), exitp = wpt(B, J.path.back());
    if (!J.A) {  // try to catch a loose magazine pushed into the mouth of the well
        for (auto& b : S.bodies) {
            if (!b.isMag || !b.loose) continue;
            V2 pa = wpt(b, J.la);
            double da = std::remainder(b.a - B.a, 2 * 3.14159265358979323846);
            V2 a2 = wpt(B, J.path[J.path.size() - 2]);
            double ex = exitp.x - a2.x, ey = exitp.y - a2.y, el = std::hypot(ex, ey);
            double over = el > 0 ? ((pa.x - exitp.x) * ex + (pa.y - exitp.y) * ey) / el : 0;  // how far past the mouth
            if (std::hypot(pa.x - exitp.x, pa.y - exitp.y) < 0.008 && over < -0.0005 && std::fabs(da) < 15 * D2R) {
                J.A = &b; b.loose = false; J.attached = true; J.latched = false; J.engaged = false;
                break;
            }
        }
        if (!J.A) return;
    }
    Body& M = *J.A;
    bool release = S.magRelease;
    if (J.R && std::hypot(J.R->x - J.R->x0, J.R->y - J.R->y0) * 1000 > J.j->releaseAt) release = true;  // catch button pressed in
    if (release && J.latched) { J.latched = false; J.engaged = true; }
    V2 pa = wpt(M, J.la), q = seat;
    if (!J.latched) {  // moving out past the mouth of the well frees the magazine
        V2 a = wpt(B, J.path[J.path.size() - 2]);
        double dx = exitp.x - a.x, dy = exitp.y - a.y, l2 = dx * dx + dy * dy;
        double t = l2 > 0 ? ((pa.x - a.x) * dx + (pa.y - a.y) * dy) / l2 : 0;
        if (t > 1.0) { J.attached = false; J.A = nullptr; M.loose = true; return; }
    }
    if (!J.latched) {
        double best = 1e18;
        for (size_t i = 0; i + 1 < J.path.size(); i++) {
            double d2; V2 c = closestOnSeg(pa, wpt(B, J.path[i]), wpt(B, J.path[i + 1]), &d2);
            if (d2 < best) { best = d2; q = c; }
        }
    }
    double L = std::hypot(q.x - pa.x, q.y - pa.y);
    if (L > 1e-9) solve(M, B, pa, q, (q.x - pa.x) / L, (q.y - pa.y) / L, L);
    solveAng(M, B, 0);
    pa = wpt(M, J.la);
    double dseat = std::hypot(pa.x - seat.x, pa.y - seat.y);
    if (dseat > 0.002) J.engaged = false;  // release cooldown over once it has moved off the seat
    if (!J.latched && !release && !J.engaged && dseat < 0.0008) J.latched = true;
}
// ---------- spawned bodies: removed ones are reused, so a long session never runs out of room
bool spawnSlot(Sim& S, Body** slot, BodyDef** def) {
    for (auto& o : S.bodies)
        if (o.gone && !o.tmpl.empty() && o.d) { *slot = &o; *def = const_cast<BodyDef*>(o.d); return true; }  // spawned defs live in extraDefs
    if (S.bodies.size() >= S.bodies.capacity()) return false;
    S.extraDefs.emplace_back();
    *slot = nullptr; *def = &S.extraDefs.back();
    return true;
}
Body* placeSpawned(Sim& S, Body* slot, const Body& b) {
    if (slot) { *slot = b; return slot; }
    S.bodies.push_back(b);
    return &S.bodies.back();
}
void retireBody(Sim& S, Body& b) {
    if (S.hand.on && S.hand.b == &b) S.hand.on = false;
    if (b.mag && b.mag->top == &b) b.mag->top = nullptr;
    if (Joint* J = magwell(S); J && J->A == &b) { J->A = nullptr; J->attached = false; J->latched = false; }
    b.gone = true; b.fixed = true; b.loose = false; b.staged = false; b.rs = RS::None; b.mag = nullptr; b.top = nullptr;
    b.vx = b.vy = b.w = 0; b.idle = 0;
}
int fillMagazines(Sim& S) {
    int added = 0;
    for (auto& b : S.bodies) {
        if (!b.isMag || b.gone || !b.d) continue;
        int cap = b.d->magCap;
        if (b.rounds < cap) { added += cap - b.rounds; b.rounds = cap; b.idle = 0; }
    }
    return added;
}
int tidy(Sim& S, double dt, double caseLife, double magLife) {
    int n = 0;
    Joint* J = magwell(S);
    for (auto& b : S.bodies) {
        if (b.gone) continue;
        bool held = S.hand.on && S.hand.b == &b;
        if (b.isCase && b.rs == RS::Loose) {  // a fired case lying clear of the gun
            b.idle = held ? 0 : b.idle + dt;
            if (b.idle > caseLife) { retireBody(S, b); n++; }
        } else if (b.isMag && b.loose && !(J && J->A == &b)) {  // a magazine out of the gun: only once it's empty and left alone
            b.idle = (held || b.rounds > 0 || b.top) ? 0 : b.idle + dt;
            if (b.idle > magLife) { retireBody(S, b); n++; }
        } else b.idle = 0;
    }
    return n;
}
Body* spawnMagazine(Sim& S, V2 c, bool seat) {
    if (!S.magTmpl) return nullptr;
    Body* slot; BodyDef* dp;
    if (!spawnSlot(S, &slot, &dp)) return nullptr;
    *dp = *S.magTmpl;
    BodyDef& d = *dp;
    d.id = S.magTmpl->id + "#" + std::to_string(++S.spawned);
    Body b;
    if (!makeBody(d, b)) { if (!slot) S.extraDefs.pop_back(); return nullptr; }
    b.tmpl = S.magTmpl->id;
    b.loose = true;
    b.x = c.x / 1000; b.y = c.y / 1000; b.px = b.x; b.py = b.y; b.pa = b.a;
    Body* nb = placeSpawned(S, slot, b);
    Joint* J = magwell(S);
    if (seat && J && !J->A) {
        Body& B = *J->B;
        V2 sw = wpt(B, J->path.front());
        double ca = std::cos(B.a), sa = std::sin(B.a);
        nb->a = B.a;
        nb->x = sw.x - (ca * J->la.x - sa * J->la.y);
        nb->y = sw.y - (sa * J->la.x + ca * J->la.y);
        nb->loose = false;
        J->A = nb; J->attached = true; J->latched = true; J->engaged = false;
    }
    nb->px = nb->x; nb->py = nb->y; nb->pa = nb->a;  // placed, not moved: no velocity
    return nb;
}
void dropMagazine(Sim& S) {
    Joint* J = magwell(S);
    if (!J || !J->A) return;
    Body& M = *J->A;
    J->latched = false; J->engaged = true;
    V2 seat = wpt(*J->B, J->path.front()), exitp = wpt(*J->B, J->path.back());
    double dx = exitp.x - seat.x, dy = exitp.y - seat.y, dl = std::hypot(dx, dy);
    if (dl > 0) { M.vx = dx / dl * 0.9; M.vy = dy / dl * 0.9; }
}
void quickReload(Sim& S) {
    Joint* J = magwell(S);
    if (!J) return;
    if (J->A) {
        Body& M = *J->A;
        V2 seat = wpt(*J->B, J->path.front()), exitp = wpt(*J->B, J->path.back());
        M.x += exitp.x - seat.x + 0.004; M.y += exitp.y - seat.y + 0.004;
        M.px = M.x; M.py = M.y; M.pa = M.a;
        M.vx = 0.3; M.vy = 0.5; M.loose = true;
        J->A = nullptr; J->attached = false; J->latched = false;
    }
    spawnMagazine(S, {0, 0}, true);
}

bool handLoad(Sim& S) {
    if (!S.BR || !S.hasFace || S.chambered || S.inTransit) return false;
    double need = std::min(S.design.breech.feed, S.design.breech.roundLen * 0.6);
    if (breechGap(S) < need) return false;
    S.chambered = true;
    return true;
}
void setKey(Sim& S, const std::string& key, bool down) {
    for (auto& p : S.pushers) if (p.key == key) p.on = down;
}
bool hasControl(const Sim& S, const std::string& key) {
    for (auto& p : S.pushers) if (p.key == key) return true;
    return false;
}

// ---------- step ----------
void substep(Sim& S) {
    const BreechDef& br = S.design.breech;
    for (auto& b : S.bodies) { b.fx = b.fy = b.tq = b.tr = 0; b.rfx = b.rfy = 0; }
    Joint* MW = magwell(S);
    V2 acc = S.frameAccel;
    if (S.limp > 0 && S.t > S.fireT && S.CH) {  // a loose hold: the frame is driven back by the shot, so in its frame
        // everything that moves is thrown forward for a few milliseconds, robbing the slide of travel
        double tau = 0.004, mGun = 0;
        for (auto& b : S.bodies) if (!b.isRound && !b.isMag && b.m > 0 && !b.root) mGun += b.m;
        mGun = std::max(mGun, 0.3) + 0.5;  // plus the hand and wrist that go with it
        double a = S.limp * S.design.breech.impulse * S.powerScale / mGun / tau * std::exp(-(S.t - S.fireT) / tau);
        acc.x += std::cos(S.CH->a) * a; acc.y += std::sin(S.CH->a) * a;
    }
    for (auto& b : S.bodies) {  // gravity (and the shooter's motion) acts on every moving part
        if (b.fixed || b.staged) continue;
        // a seated magazine and the round under its lips are held by the frame: they get gravity only
        bool held = (b.isMag && !b.loose) || (b.isRound && b.rs == RS::Presented);
        V2 a = held ? S.frameAccel : acc;
        b.fx += b.m * a.x; b.fy += b.m * a.y;
    }
    // springs (explicit force)
    for (auto& J : S.joints) {
        if (J.type != JT::Spring) continue;
        Body &A = *J.A, &B = *J.B;
        if (J.j->kind == "leaf") {  // pushes B's contact point along the leaf normal, never pulls
            V2 tip = wpt(A, J.rel0), pb = wpt(B, J.lb);
            double c = std::cos(A.a), sn = std::sin(A.a);
            double nx = c * J.nrm.x - sn * J.nrm.y, ny = sn * J.nrm.x + c * J.nrm.y;
            double sep = (pb.x - tip.x) * nx + (pb.y - tip.y) * ny;  // how far B has moved off along the push direction
            double defl = J.j->preload / 1000 - sep;
            double F = defl > 0 ? J.j->k * 1000 * defl : 0;
            J.force = F;
            double rbx = pb.x - B.x, rby = pb.y - B.y, rax = pb.x - A.x, ray = pb.y - A.y;
            B.fx += nx * F; B.fy += ny * F; B.tq += rbx * ny * F - rby * nx * F;
            A.fx -= nx * F; A.fy -= ny * F; A.tq -= rax * ny * F - ray * nx * F;
            continue;
        }
        V2 pa = wpt(A, J.la), pb = wpt(B, J.lb);
        double dx = pb.x - pa.x, dy = pb.y - pa.y, L = std::hypot(dx, dy);
        if (L < 1e-9) L = 1e-9;
        dx /= L; dy /= L;
        double rax = pa.x - A.x, ray = pa.y - A.y, rbx = pb.x - B.x, rby = pb.y - B.y;
        double vax = A.vx - A.w * ray, vay = A.vy + A.w * rax, vbx = B.vx - B.w * rby, vby = B.vy + B.w * rbx;
        double vrel = (vbx - vax) * dx + (vby - vay) * dy;
        double F = J.j->k * 1000 * (J.j->rest / 1000 - L) - J.j->damp * vrel;
        // a long coil spring with nothing inside it bows out sideways and loses most of its push
        J.buckled = J.j->guide.empty() && J.j->rest > 4 * J.j->coilDia && (J.j->rest / 1000 - L) > 0.25 * J.j->rest / 1000;
        if (J.buckled && F > 0) F *= 0.35;
        J.force = F;
        B.fx += dx * F; B.fy += dy * F; B.tq += rbx * dy * F - rby * dx * F;
        A.fx -= dx * F; A.fy -= dy * F; A.tq -= rax * dy * F - ray * dx * F;
    }
    // chamber pressure on the breech face
    double dur = std::max(0.05, br.dur) / 1000;
    if (S.t >= S.fireT && S.t < S.fireT + dur && S.BR && S.hasFace) {
        double F = br.impulse * S.powerScale / dur, cx = std::cos(S.CH->a), cy = std::sin(S.CH->a);
        V2 p = wpt(*S.BR, S.faceB);
        Body& b = *S.BR;
        double rx = p.x - b.x, ry = p.y - b.y;
        b.fx -= cx * F; b.fy -= cy * F; b.tq -= rx * cy * F - ry * cx * F;
    }
    if (S.rollHand.on && S.rollHand.b && S.rollHand.b->invIr > 0) {
        Body& b = *S.rollHand.b;
        double om = 2 * 3.14159265358979323846 * 5;
        double t = b.Ir * om * om * (S.rollHand.t - b.roll) - 2 * b.Ir * om * b.wr;
        if (S.rollHand.arm > 0) t = std::clamp(t, -S.handStrength * S.rollHand.arm, S.handStrength * S.rollHand.arm);
        b.tr += t;
    }
    cartridgeForces(S);
    // integrate
    for (auto& b : S.bodies) {  // forces on welded pieces act on their assembly
        if (!b.root || b.fixed) continue;
        Body& r = *b.root;
        r.fx += b.fx; r.fy += b.fy; r.tq += b.tq + (b.x - r.x) * b.fy - (b.y - r.y) * b.fx; r.tr += b.tr;
    }
    for (auto& b : S.bodies) {
        if (b.fixed || b.staged || b.root) continue;
        double dmp = std::exp(-b.damp * H);
        b.vx = (b.vx + b.fx * b.invM * H) * dmp;
        b.vy = (b.vy + b.fy * b.invM * H) * dmp;
        b.w = (b.w + b.tq * b.invI * H) * dmp;
        b.wr = (b.wr + b.tr * b.invIr * H) * dmp;
        b.px = b.x; b.py = b.y; b.pa = b.a; b.pr = b.roll;
        b.x += b.vx * H; b.y += b.vy * H; b.a += b.w * H; b.roll += b.wr * H;
    }
    for (auto& b : S.bodies) {  // pieces ride along (and remember where they were, for friction)
        if (b.pieces.empty() || b.fixed) continue;
        double c = std::cos(b.pa), s = std::sin(b.pa);
        for (Body* p : b.pieces) { p->px = b.px + c * p->off.x - s * p->off.y; p->py = b.py + s * p->off.x + c * p->off.y; p->pa = b.pa + p->aOff; }
        placePieces(b);
    }
    // geometric joints
    for (auto& J : S.joints) {
        if (J.type == JT::Magwell) { solveMagwell(S, J); continue; }
        if (J.merged) continue;
        Body &A = *J.A, &B = *J.B;
        if (J.type == JT::Spring) {  // a coil spring compressed to solid height becomes a hard stop
            if (J.j->kind != "coil") continue;
            double solid = (J.j->turns + 2) * J.j->wire / 1000;
            V2 pa = wpt(A, J.la), pb = wpt(B, J.lb);
            double dx = pa.x - pb.x, dy = pa.y - pb.y, L = std::hypot(dx, dy);
            if (L > 1e-9 && L < solid) solve(A, B, pa, pb, dx / L, dy / L, solid - L);
            continue;
        }
        if (J.type == JT::Pin || J.type == JT::Weld) {
            V2 pa = wpt(A, J.la), pb = wpt(B, J.lb);
            double dx = pb.x - pa.x, dy = pb.y - pa.y, L = std::hypot(dx, dy);
            if (L > 1e-9) solve(A, B, pa, pb, dx / L, dy / L, L);
            if (J.type == JT::Weld) solveAng(A, B, 0);
        } else if (J.type == JT::Track) {
            V2 pa = wpt(A, J.la), bq; double best = 1e18;
            for (size_t i = 0; i + 1 < J.path.size(); i++) {
                double d2; V2 q = closestOnSeg(pa, wpt(B, J.path[i]), wpt(B, J.path[i + 1]), &d2);
                if (d2 < best) { best = d2; bq = q; }
            }
            double L = std::sqrt(best);
            if (L > 1e-9) solve(A, B, pa, bq, (bq.x - pa.x) / L, (bq.y - pa.y) / L, L);
            if (J.j->lockRot) solveAng(A, B, 0);
        }
    }
    // fingers on the controls: each presses toward a point moving at finger speed, with at most its rated force,
    // so a trigger that suddenly breaks free follows through at finger speed instead of flying
    for (auto& p : S.pushers) {
        Body& b = *p.b;
        V2 w = wpt(b, p.l);
        if (!p.on) { p.wasOn = false; continue; }
        if (!p.wasOn) { p.wasOn = true; p.anchor = w; p.ext = 0; }
        double done = (w.x - p.anchor.x) * p.dx + (w.y - p.anchor.y) * p.dy;
        p.ext = std::min(p.ext + 0.25 * H, std::max(done, 0.0) + 0.0015);  // a blocked finger stops just past where it is
        double d = p.ext - done;
        if (d <= 0) continue;  // a finger only pushes
        Body& rb = b.root ? *b.root : b;
        double rx = w.x - rb.x, ry = w.y - rb.y, cr = rx * p.dy - ry * p.dx;
        double wgt = rb.invM + rb.invI * cr * cr, alpha = 1.0 / 20000.0;  // fingertip stiffness 20 kN/m
        if (wgt <= 0) continue;
        double lam = std::min(d / (wgt + alpha / (H * H)), p.force * H * H);
        solve(b, S.world, w, {w.x + p.dx * d, w.y + p.dy * d}, p.dx, p.dy, lam * wgt);
    }
    // hand: a stiff grip that can push or pull with at most handStrength newtons
    if (S.hand.on && S.hand.b && !S.hand.b->fixed) {
        Body& b = *S.hand.b;
        V2 p = wpt(b, S.hand.l);
        double dx = S.hand.t.x - p.x, dy = S.hand.t.y - p.y, L = std::hypot(dx, dy);
        if (L > 1e-9) {
            Body& rb = b.root ? *b.root : b;
            double nx = dx / L, ny = dy / L, rx = p.x - rb.x, ry = p.y - rb.y, cr = rx * ny - ry * nx;
            double w = rb.invM + rb.invI * cr * cr, alpha = 1.0 / 30000.0;  // grip stiffness 30 kN/m
            double lam = std::min(L / (w + alpha / (H * H)), S.handStrength * H * H);
            solve(b, S.world, p, S.hand.t, nx, ny, lam * w);
        }
    }
    for (auto& b : S.bodies) if (b.rollParent) reshapeRolled(b);
    for (auto& b : S.bodies) bbox(b);
    for (int it = 0; it < 3; it++) {  // several passes so stacked contacts (rails, lugs) stay stiff under big impulses
        if (it) for (auto& b : S.bodies) if (!b.fixed) bbox(b);  // (once per pass; fixed parts never move)
        for (auto& pr : S.pairs) {
            Body &a = *pr.first, &b = *pr.second;
            if (a.bb[0] > b.bb[2] || b.bb[0] > a.bb[2] || a.bb[1] > b.bb[3] || b.bb[1] > a.bb[3]) continue;
            if (std::fabs(a.z - b.z) >= a.zh + b.zh - 1e-5) continue;  // passing each other in depth
            contacts(a, b, it == 0 ? S.design.friction : 0); contacts(b, a, it == 0 ? S.design.friction : 0);
        }
    }
    // loose parts rest on the baseplate
    for (auto& b : S.bodies) {
        b.grounded = false;
        if (b.fixed || !(b.loose || (b.isMag && !(MW && MW->A == &b)))) continue;
        for (auto& l : b.lp) {
            V2 w = wpt(b, l);
            if (w.y > S.groundY) { solve(b, S.world, w, {w.x, S.groundY}, 0, -1, w.y - S.groundY); b.grounded = true; }
        }
    }
    // rotation-coupled joints
    for (auto& J : S.joints) {
        if (J.type == JT::Magwell) continue;
        Body &A = *J.A, &B = *J.B;
        const JointDef& j = *J.j;
        if (J.type == JT::Cam) {
            if (A.invIr <= 0) continue;
            double u = axial(A, B, J.rel0) * 1000, f, s;
            interpTable(j.table, u, f, s);
            double sl = s * D2R * 1000;  // rad per metre
            double C = A.roll - f * D2R, w = A.invIr + sl * sl * (A.invM + B.invM);
            if (w <= 0) continue;
            double l = -C / w;
            A.roll += A.invIr * l;
            double ax = std::cos(B.a), ay = std::sin(B.a), du = -sl * l;
            A.x += ax * du * A.invM; A.y += ay * du * A.invM;
            B.x -= ax * du * B.invM; B.y -= ay * du * B.invM;
        } else if (J.type == JT::Lug) {
            double u = axial(A, B, J.rel0), un = j.unlock * D2R;
            if (u > 0) solveAxial(A, B, -u);
            J.engaged = std::fabs(A.roll) < un && u > -0.0006;
            if (J.engaged) solveAxial(A, B, -axial(A, B, J.rel0));
            if (j.blockOpen && u < -0.002 && A.invIr > 0 && A.roll < un) A.roll = un;
        } else if (J.type == JT::Latch) {
            double u = axial(A, B, J.rel0) * 1000, c = j.catchAt;
            if (J.state == LatchState::Armed && u <= c) J.state = LatchState::Hold;
            if (J.state == LatchState::Hold) {
                double r = J.R ? axial(*J.R, B, J.relR0) * 1000 : 0;
                if (J.R && r >= j.releaseAt) J.state = LatchState::Released;
                else if (u > c) solveAxial(A, B, (c - u) / 1000);
            }
            if (J.state == LatchState::Released && u > c + 5) J.state = LatchState::Armed;
        }
    }
    for (auto& b : S.bodies)
        if (b.invIr > 0) b.roll = std::clamp(b.roll, b.rmin, b.rmax);
    cartridgeStep(S);
    for (auto& b : S.bodies) if (!b.pieces.empty() && !b.fixed) placePieces(b);
    for (auto& b : S.bodies) {
        if (b.fixed) continue;
        if (b.root) {  // a piece moves with its assembly
            Body& r = *b.root;
            b.vx = r.vx - r.w * (b.y - r.y); b.vy = r.vy + r.w * (b.x - r.x); b.w = r.w;
            continue;
        }
        b.vx = (b.x - b.px) / H; b.vy = (b.y - b.py) / H; b.w = (b.a - b.pa) / H; b.wr = (b.roll - b.pr) / H;
        // nothing in a gun moves this fast; a bad step must not snowball into an explosion
        b.w = std::clamp(b.w, -2000.0, 2000.0); b.wr = std::clamp(b.wr, -2000.0, 2000.0);
        double sp = std::hypot(b.vx, b.vy), vmax = 60;
        if (b.isRound && b.rs != RS::Loose) {  // a cartridge knocked about inside the action: a few metres a second, not a ricochet
            b.w = std::clamp(b.w, -800.0, 800.0); vmax = 12;
        }
        if (sp > vmax) { b.vx *= vmax / sp; b.vy *= vmax / sp; }
        if (b.grounded) {  // resting on the baseplate: no bounce, and friction against sliding and rolling
            if (b.vy < -0.02) b.vy = -0.02;
            b.vx *= 0.995; b.w *= 0.98;
        }
        if (S.hand.on && S.hand.b == &b) { b.vx *= 0.9985; b.vy *= 0.9985; b.w *= 0.995; }
    }
    // the frame's side of every spring, contact and joint: the recoil you would feel
    S.frameForce = {0, 0};
    for (auto& b : S.bodies) {
        if (!b.fixed || b.gone) continue;
        S.frameForce.x += b.fx + b.rfx / H; S.frameForce.y += b.fy + b.rfy / H;
    }
    if (S.t >= S.fireT && S.t < S.fireT + 0.25) {
        double back = -S.frameForce.x * std::cos(S.CH ? S.CH->a : 0) - S.frameForce.y * std::sin(S.CH ? S.CH->a : 0);
        S.recoilWin[S.recoilIdx++ % 8] = back;  // rigid impacts spike for one substep; a hand feels the average
        double avg = 0;
        for (double v : S.recoilWin) avg += v / 8;
        S.recoilPeak = std::max(S.recoilPeak, avg);
        S.recoilImpulse += back * H;
    }
    S.t += H;
    // feeding and firing
    if (S.BR && S.hasFace) {
        double gap = breechGap(S), feed = br.feed;
        if (gap < 1 && (S.chambered || S.spent)) S.gripped = true;  // the extractor takes hold when the breech closes on it
        if (gap > feed && !S.physicalFeed) {  // simplified feeding: rounds jump from magazine to chamber
            if (S.spent) { S.spent = false; S.events.push_back({Ev::Eject, S.t}); }
            if (S.chambered && S.gripped) {  // opening a loaded action pulls the live round out and throws it clear
                S.chambered = false; S.gripped = false;
                V2 f = wpt(*S.BR, S.faceB);
                BoreLine L = boreLine(S);
                double ux = L.ok ? L.dir.x : 1, uy = L.ok ? L.dir.y : 0;
                spawnRound(S, {(f.x + ux * 0.002) * 1000, (f.y + uy * 0.002 - 0.004) * 1000}, S.BR->vx * 0.3 + 0.25, -1.3);
                S.events.push_back({Ev::EjectLive, S.t});
            }
            if (!S.chambered && !S.inTransit) {
                if (S.feedMode == "magazine") { Body* m = seatedMag(S); if (m && m->rounds > 0) { m->rounds--; S.inTransit = true; } }
                else if (S.feedMode == "internal" && S.rounds > 0) { S.rounds--; S.inTransit = true; }
            }
        }
        if (gap < 1 && S.inTransit) { S.inTransit = false; S.chambered = true; }
        if (S.strikerIgnition) {  // fires when the striker's tip hits the primer hard enough
            Body& st = *S.STR;
            V2 tip = wpt(st, S.strTip), primer = wpt(*S.BR, S.faceB);
            double rx = tip.x - st.x, ry = tip.y - st.y;
            double vx = st.vx - st.w * ry - S.BR->vx, vy = st.vy + st.w * rx - S.BR->vy;
            double d = std::hypot(tip.x - primer.x, tip.y - primer.y);
            if (d > 0.004) S.primed = true;
            if (S.primed && d < 0.0012 && std::hypot(vx, vy) > br.igniteSpeed && S.t > S.fireT + dur * 2) {
                S.primed = false;
                if (S.chambered && inBattery(S)) { S.fireT = S.t; S.chambered = false; S.spent = true; S.shots++; S.recoilPeak = 0; S.recoilImpulse = 0; S.events.push_back({Ev::Bang, S.t}); }
                else if (inBattery(S)) S.events.push_back({Ev::Click, S.t});
            }
        } else {
            // The built-in hammer or striker: no parts, but the action must open far enough to cock it again.
            if (br.cockAt <= 0 || gap >= br.cockAt) S.cocked = true;
            auto ignite = [&]() {  // the hammer reaches the primer
                S.igniteAt = -1;
                if (!inBattery(S)) return;
                if (S.chambered) {
                    S.fireT = S.t; S.chambered = false; S.spent = true; S.shots++; S.recoilPeak = 0; S.recoilImpulse = 0;
                    S.events.push_back({Ev::Bang, S.t});
                } else S.events.push_back({Ev::Click, S.t});
            };
            if (S.igniteAt >= 0 && S.t >= S.igniteAt) ignite();
            bool burst = br.fireMode == "burst", full = br.fullAuto();
            // a held trigger keeps firing in full-auto, and until the burst is spent; a fresh pull is needed otherwise
            bool want = S.pressed || (S.trigger && S.chambered && (full || (burst && S.burstLeft > 0)));
            if (want && S.igniteAt < 0 && S.t > S.fireT + dur * 2) {
                bool ok = inBattery(S);
                for (auto& p : S.pushers) {  // with a trigger part bound to FIRE, the shot waits until it has been pulled 2 mm
                    if (p.key != "fire") continue;
                    V2 w = wpt(*p.b, p.l), w0{p.b->x0 + p.l.x, p.b->y0 + p.l.y};
                    if ((w.x - w0.x) * p.dx + (w.y - w0.y) * p.dy < 0.002) ok = false;
                    break;
                }
                if (ok) {
                    bool fresh = S.pressed;
                    S.pressed = false;
                    if (fresh && burst) S.burstLeft = std::max(1, br.burst);
                    if (S.cocked || br.doubleAction) {
                        S.cocked = br.cockAt <= 0;  // it falls; older designs are cocked again at once
                        S.deadPull = false;
                        if (burst) S.burstLeft--;
                        S.igniteAt = S.t + br.lockTime / 1000;
                        if (br.lockTime <= 0) ignite();
                    } else if (fresh) {
                        S.deadPull = true;  // single action with the hammer down: the trigger moves and nothing happens
                        S.burstLeft = 0;
                    }
                }
            }
        }
    }
}

int run(Sim& S, double dt, int maxSteps) {
    int n = (int)std::lround(dt / H);
    if (maxSteps > 0) n = std::min(n, maxSteps);
    for (int i = 0; i < n; i++) substep(S);
    return n;
}

}  // namespace sim
