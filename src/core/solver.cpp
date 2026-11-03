#include "solver_internal.hpp"
#include "carve.hpp"
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
void useChamber(Sim& S, int k) {
    if (k == S.cur || k < 0 || k >= (int)S.chambers.size()) return;
    auto swapIn = [&](Sim::Chamber& c, bool save) {
        if (save) {
            c.BR = S.BR; c.CH = S.CH; c.faceB = S.faceB; c.faceC = S.faceC; c.muzC = S.muzC; c.hasFace = S.hasFace; c.hasMuzzle = S.hasMuzzle;
            c.chambered = S.chambered; c.inTransit = S.inTransit; c.spent = S.spent; c.gripped = S.gripped; c.cycledOpen = S.cycledOpen;
        } else {
            S.BR = c.BR; S.CH = c.CH; S.faceB = c.faceB; S.faceC = c.faceC; S.muzC = c.muzC; S.hasFace = c.hasFace; S.hasMuzzle = c.hasMuzzle;
            S.chambered = c.chambered; S.inTransit = c.inTransit; S.spent = c.spent; S.gripped = c.gripped; S.cycledOpen = c.cycledOpen;
        }
    };
    swapIn(S.chambers[S.cur], true);
    swapIn(S.chambers[k], false);
    // each chamber's own cartridge (a barrel's): moved, not copied
    {
        auto& o = S.chambers[S.cur]; auto& n = S.chambers[k];
        Cartridge live = std::move(S.design.breech.cart);
        S.design.breech.cart = std::move(n.cart); o.cart = std::move(live);
        std::vector<double> r = std::move(S.chamberR);
        S.chamberR = std::move(n.chamberR); o.chamberR = std::move(r);
        o.impulse = S.design.breech.impulse; S.design.breech.impulse = n.impulse;
    }
    S.cur = k;
}
int chamberCount(const Sim& S) { return std::max(1, (int)S.chambers.size()); }
int chamberedCount(Sim& S) {
    int was = S.cur, n = 0;
    for (int k = 0; k < chamberCount(S); k++) { useChamber(S, k); if (S.chambered) n++; }
    useChamber(S, was);
    return n;
}
bool anyChambered(Sim& S) { return chamberedCount(S) > 0; }
static bool leavesCase(const Sim& S) { return S.design.breech.cart.kind != "caseless"; }  // caseless rounds burn away
static void fireChamber(Sim& S) {  // the chamber that's swapped in goes off
    S.fireT = S.t;
    if (!S.chambers.empty()) S.chambers[S.cur].shotT = S.t;
    S.chambered = false; S.spent = leavesCase(S); S.shots++; S.recoilPeak = 0; S.recoilImpulse = 0;
    S.events.push_back({Ev::Bang, S.t});
}
static constexpr int MAX_SPAWNED = 200;  // magazines and loose rounds added during a test
static void reshapeRolled(Body& b);
void resample(Body& b);
std::unique_ptr<Sim> finishBuild(std::unique_ptr<Sim>& S);

std::unique_ptr<Sim> build(const Design& design) {
    auto S = std::make_unique<Sim>();
    S->design = design;
    syncBarrels(S->design);  // barrels drawn with the Barrel tool set the chambers and muzzles
    Sim& s = *S;
    s.world.id = "world";
    s.bodies.reserve(s.design.bodies.size() + MAX_SPAWNED);  // body pointers must stay valid as magazines and rounds spawn
    s.groundY = groundLevelMM(s.design) / 1000;
    for (auto& d : s.design.bodies) carve::syncRound(d);  // a round part's thickness is its diameter
    for (auto& d : s.design.bodies) {
        Body b;
        if (!makeBody(d, b)) continue;
        if ((d.carves.empty() && !d.round && !carve::tilted(d)) || d.rollOn || d.magazine) { s.bodies.push_back(b); continue; }
        // carved: one body per depth slice, welded into one part. The thickest-in-material slice keeps the part's id.
        std::vector<Body> sl;
        double vol = 0;
        std::vector<V2> plain = outlineOf(d);
        for (auto& c : carve::slices(d))
        for (auto& shape : c.outlines.empty() ? std::vector<std::vector<V2>>{plain} : c.outlines) {  // (a round part's slab can be in pieces)
            double area, J; V2 cen;
            if (!carve::material(shape, c.cav, area, cen, J) || area < 0.5) continue;
            // every slice keeps the part's own reference point (the outline's centre), so the part draws and joins
            // as before; like welded pieces, the small shift of the centre of mass is left out
            Body x = b;
            double t = c.z1 - c.z0, cx = b.x0 * 1000, cy = b.y0 * 1000;
            x.lp.clear();
            for (auto& p : shape) x.lp.push_back({(p.x - cx) / 1000, (p.y - cy) / 1000});
            x.cav.clear();
            for (auto& q : c.cav) {
                std::vector<V2> lq;
                for (auto& p : q) lq.push_back({(p.x - cx) / 1000, (p.y - cy) / 1000});
                x.cav.push_back(lq);
            }
            double off2 = (cen.x - cx) * (cen.x - cx) + (cen.y - cy) * (cen.y - cy);
            x.m = area * t * DENS;
            x.I = std::max((J + area * off2) * t * DENS * 1e-6, x.m * 0.004 * 0.004);
            x.invM = x.fixed ? 0 : 1 / x.m;
            x.invI = x.fixed ? 0 : 1 / x.I;
            x.z = (c.z0 + c.z1) / 2000; x.zh = t / 2000;
            resample(x);
            vol += area * t;
            sl.push_back(x);
        }
        if (sl.empty()) continue;  // carved away completely
        if (d.mass > 0) for (auto& x : sl) {  // the player set the weight: share it out by volume
            double f = (x.m / DENS) / vol;
            x.m = d.mass / 1000 * f; x.I = std::max(x.I * (d.mass / 1000) / (vol * DENS), x.m * 0.004 * 0.004);
            x.invM = x.fixed ? 0 : 1 / x.m; x.invI = x.fixed ? 0 : 1 / x.I;
        }
        size_t main = 0;
        for (size_t k = 1; k < sl.size(); k++) if (sl[k].m > sl[main].m) main = k;
        for (size_t k = 0, n = 0; k < sl.size(); k++) {
            if (k == main) continue;
            sl[k].id = d.id + "~" + std::to_string(++n);
            sl[k].slice = true;
        }
        std::swap(sl[0], sl[main]);  // the part itself first
        for (auto& x : sl) s.bodies.push_back(x);
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
        b.isMag = d.magazine && !d.fixed && !d.magFixed;
        b.builtInMag = d.magazine && d.magFixed;
        b.rounds = (b.isMag || b.builtInMag) ? d.magCap : 0;
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
            for (size_t k = 0; k < j.path.size(); k++) J.pathAng.push_back((k < j.pathAng.size() ? j.pathAng[k] : 0) * D2R);
            J.attached = true; J.latched = true;
            s.mwIdx = (int)s.joints.size();
            s.magTmpl = A->d;
            J.R = j.rel.empty() ? nullptr : s.find(j.rel);
            if (J.R == &s.world) J.R = nullptr;
            welded.insert(A->id + "|" + B->id); welded.insert(B->id + "|" + A->id);
        } else if (j.type == "cam" || j.type == "lug" || j.type == "latch") {
            J.type = j.type == "cam" ? JT::Cam : j.type == "lug" ? JT::Lug : JT::Latch;
            J.rel0 = rel(*B, *A);
            if (J.type == JT::Latch) { J.R = s.find(j.rel); if (J.R == &s.world) J.R = nullptr; if (J.R) { J.relR0 = rel(*B, *J.R); J.relRA0 = rel(*A, *J.R); } }
        } else continue;
        s.joints.push_back(J);
    }
    {   // welded parts become rigid assemblies: union the weld groups, pick a root, fold the pieces' mass into it
        std::vector<int> grp(s.bodies.size());
        for (size_t i = 0; i < grp.size(); i++) grp[i] = (int)i;
        std::function<int(int)> top = [&](int i) { return grp[i] == i ? i : grp[i] = top(grp[i]); };
        auto idx = [&](Body* b) { return b == &s.world ? -1 : (int)(b - s.bodies.data()); };
        {   // welded parts (the same weld tag) are one solid piece
            std::map<std::string, int> first;
            for (size_t i = 0; i < s.bodies.size(); i++) {
                const BodyDef* d = s.bodies[i].d;
                if (!d || d->weld.empty() || s.bodies[i].slice || !s.bodies[i].tmpl.empty()) continue;
                auto it = first.find(d->weld);
                if (it == first.end()) first[d->weld] = (int)i;
                else grp[top((int)i)] = top(it->second);
            }
        }
        for (size_t i = 0; i < s.bodies.size(); i++) {  // a carved part's slices are one solid
            if (!s.bodies[i].slice) continue;
            Body* m = s.find(s.bodies[i].d->id);
            if (m) grp[top((int)i)] = top(idx(m));
        }
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
        s.chambers.assign(1 + br.more.size(), Sim::Chamber());
        for (size_t k = 0; k < br.more.size(); k++) {  // the further chambers
            const ChamberDef& cd = br.more[k];
            Sim::Chamber& c = s.chambers[k + 1];
            c.BR = s.find(cd.breech.empty() ? br.breech : cd.breech);
            if (c.BR == &s.world) c.BR = nullptr;
            c.CH = s.find(cd.chamber);
            if (!c.CH) c.CH = &s.world;
            if (c.BR) { c.hasFace = true; c.faceB = anc(*c.BR, cd.face); c.faceC = anc(*c.CH, cd.face); }
            if (cd.hasMuzzle) { c.hasMuzzle = true; c.muzC = anc(*c.CH, cd.muzzle); }
            c.chambered = cd.loaded;
            c.cart = cd.ownCart ? cd.cart : br.cart;
            c.impulse = cd.ownCart && br.cartImpulse ? std::round(c.cart.impulse() * 100) / 100 : br.impulse;
        }
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
        if (d.ctrl == "none" || d.key == "none" || b.fixed || b.slice) continue;
        Pusher p; p.b = &b; p.l = anc(b, d.ctrlPt);
        p.dx = std::cos(d.ctrlDir * D2R); p.dy = std::sin(d.ctrlDir * D2R);
        p.force = d.ctrlForce; p.key = d.key;
        s.pushers.push_back(p);
    }
    cartridgeSetup(s);
    for (int k = 1; k < chamberCount(s); k++) { useChamber(s, k); cartridgeSetup(s); }  // each chamber cut for its own round
    useChamber(s, 0);
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
static constexpr double CONTACT_REACH = 0.005;   // deeper than this (m) a point isn't pushed out (see contacts)
static void buildAccel(Body& b) {
    b.acc = Body::Accel{};
    const size_t n = b.lp.size();
    if (n < 48) return;   // few edges: checking them all is quicker
    double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
    for (auto& p : b.lp) { x0 = std::min(x0, p.x); x1 = std::max(x1, p.x); y0 = std::min(y0, p.y); y1 = std::max(y1, p.y); }
    auto& A = b.acc;
    const double R = CONTACT_REACH;
    A.x0 = x0 - R; A.y0 = y0 - R;
    double w = x1 - x0 + 2 * R, h = y1 - y0 + 2 * R;
    A.cell = std::max({0.001, w / 96, h / 96});
    A.gw = std::max(1, (int)std::ceil(w / A.cell)); A.gh = std::max(1, (int)std::ceil(h / A.cell));
    A.near.assign((size_t)A.gw * A.gh, {});
    A.close.assign((size_t)A.gw * A.gh, {});
    A.tight = 0.0005;
    A.nb = std::clamp((int)n / 4, 8, 1024);
    A.band = std::max(1e-6, (y1 - y0) / A.nb);
    A.bands.assign(A.nb, {});
    for (size_t i = 0; i < n; i++) {
        V2 p = b.lp[i], q = b.lp[(i + 1) % n];
        int c0 = std::clamp((int)std::floor((std::min(p.x, q.x) - R - A.x0) / A.cell), 0, A.gw - 1);
        int c1 = std::clamp((int)std::floor((std::max(p.x, q.x) + R - A.x0) / A.cell), 0, A.gw - 1);
        int r0 = std::clamp((int)std::floor((std::min(p.y, q.y) - R - A.y0) / A.cell), 0, A.gh - 1);
        int r1 = std::clamp((int)std::floor((std::max(p.y, q.y) + R - A.y0) / A.cell), 0, A.gh - 1);
        for (int r = r0; r <= r1; r++)
            for (int c = c0; c <= c1; c++) A.near[(size_t)r * A.gw + c].push_back((int)i);
        const double T = A.tight;
        c0 = std::clamp((int)std::floor((std::min(p.x, q.x) - T - A.x0) / A.cell), 0, A.gw - 1);
        c1 = std::clamp((int)std::floor((std::max(p.x, q.x) + T - A.x0) / A.cell), 0, A.gw - 1);
        r0 = std::clamp((int)std::floor((std::min(p.y, q.y) - T - A.y0) / A.cell), 0, A.gh - 1);
        r1 = std::clamp((int)std::floor((std::max(p.y, q.y) + T - A.y0) / A.cell), 0, A.gh - 1);
        for (int r = r0; r <= r1; r++)
            for (int c = c0; c <= c1; c++) A.close[(size_t)r * A.gw + c].push_back((int)i);
        int b0 = std::clamp((int)std::floor((std::min(p.y, q.y) - y0) / A.band), 0, A.nb - 1);
        int b1 = std::clamp((int)std::floor((std::max(p.y, q.y) - y0) / A.band), 0, A.nb - 1);
        for (int k = b0; k <= b1; k++) A.bands[k].push_back((int)i);
    }
    A.y0band = y0;
    A.lb[0] = x0; A.lb[1] = y0; A.lb[2] = x1; A.lb[3] = y1;
    A.n = n;
}
void resample(Body& b) {
    buildAccel(b);
    double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
    for (auto& p : b.lp) { x0 = std::min(x0, p.x); x1 = std::max(x1, p.x); y0 = std::min(y0, p.y); y1 = std::max(y1, p.y); }
    double step = std::clamp(std::max(x1 - x0, y1 - y0) / 20, 0.0004, 0.0025);
    b.samples.clear();
    // parts drawn with more corners than the old limit (32): a corner that barely bends gets no point of its own
    // unless the last one is a step away, so a finely drawn curve costs no more than a coarse one. (Not rounds and
    // cases, made on the bench as "round#..." and "case#...": their outlines come from the cartridge and stay as they were.)
    const bool thin = b.d && b.d->pts.size() > 32 && b.d->id.find('#') == std::string::npos;
    const size_t N = b.lp.size();
    V2 last{1e9, 1e9};
    for (size_t i = 0; i < N; i++) {
        V2 p = b.lp[i], q = b.lp[(i + 1) % N];
        int n = std::max(1, (int)std::ceil(std::hypot(q.x - p.x, q.y - p.y) / step));
        for (int k = 0; k < n; k++) {
            V2 s{p.x + (q.x - p.x) * k / n, p.y + (q.y - p.y) * k / n};
            if (thin && k == 0) {
                V2 o = b.lp[(i + N - 1) % N];
                double ax = p.x - o.x, ay = p.y - o.y, bx = q.x - p.x, by = q.y - p.y, la = std::hypot(ax, ay), lb = std::hypot(bx, by);
                double turn = la > 0 && lb > 0 ? std::fabs(std::atan2(ax * by - ay * bx, ax * bx + ay * by)) : 1;
                if (turn < 0.1 && std::hypot(s.x - last.x, s.y - last.y) < step) continue;   // under about 6 degrees
            }
            b.samples.push_back(s); last = s;
        }
    }
    if (b.cav.empty()) return;
    // carved: the outline only where it wasn't cut away, plus the walls of the cuts where they are in the part
    std::vector<V2> keep;
    for (auto& p : b.samples) {
        bool cut = false;
        for (auto& c : b.cav) if (carve::inConvex(c, p)) { cut = true; break; }
        if (!cut) keep.push_back(p);
    }
    for (auto& c : b.cav)
        for (size_t i = 0; i < c.size(); i++) {
            V2 p = c[i], q = c[(i + 1) % c.size()];
            int n = std::max(1, (int)std::ceil(std::hypot(q.x - p.x, q.y - p.y) / step));
            for (int k = 0; k < n; k++) {
                V2 s{p.x + (q.x - p.x) * k / n, p.y + (q.y - p.y) * k / n};
                if (!inPoly(s.x, s.y, b.lp)) continue;
                bool other = false;
                for (auto& c2 : b.cav) if (&c2 != &c && carve::inConvex(c2, s)) { other = true; break; }
                if (!other) keep.push_back(s);
            }
        }
    b.samples.swap(keep);
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
    if (b.acc.n && b.acc.n == b.lp.size()) {   // many corners: the turned box round the outline (a little larger, which is fine)
        const double* L = b.acc.lb;
        for (V2 l : {V2{L[0], L[1]}, V2{L[2], L[1]}, V2{L[2], L[3]}, V2{L[0], L[3]}}) {
            double x = b.x + c * l.x - s * l.y, y = b.y + s * l.x + c * l.y;
            x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y);
        }
        b.bb[0] = x0; b.bb[1] = y0; b.bb[2] = x1; b.bb[3] = y1;
        return;
    }
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
    const bool fast = A.acc.n == n && n > 0;
    double cB = std::cos(B.a), sB = std::sin(B.a), cA = std::cos(A.a), sA = std::sin(A.a);
    for (auto& s : B.samples) {
        V2 w{B.x + cB * s.x - sB * s.y, B.y + sB * s.x + cB * s.y};
        if (w.x < A.bb[0] || w.x > A.bb[2] || w.y < A.bb[1] || w.y > A.bb[3]) continue;
        double dx = w.x - A.x, dy = w.y - A.y;
        V2 l{cA * dx + sA * dy, -sA * dx + cA * dy};
        if (l.x < lx0 || l.x > lx1 || l.y < ly0 || l.y > ly1) continue;
        const std::vector<int>* cand = nullptr; int cell = 0;   // many corners: only the edges that can be within reach
        if (fast) {
            int c = std::clamp((int)std::floor((l.x - A.acc.x0) / A.acc.cell), 0, A.acc.gw - 1);
            int r = std::clamp((int)std::floor((l.y - A.acc.y0) / A.acc.cell), 0, A.acc.gh - 1);
            cell = r * A.acc.gw + c;
            cand = &A.acc.near[(size_t)cell];
            if (cand->empty()) continue;   // nothing within reach: too deep inside to push, or outside
            // inside? the same crossing test as inPoly, over the edges that cross this point's band
            int k = std::clamp((int)std::floor((l.y - A.acc.y0band) / A.acc.band), 0, A.acc.nb - 1);
            bool in = false;
            for (int i : A.acc.bands[k]) {
                const V2 &pi = A.lp[i], &pj = A.lp[i + 1 < (int)n ? i + 1 : 0];
                if (((pi.y > l.y) != (pj.y > l.y)) && l.x < (pj.x - pi.x) * (l.y - pi.y) / (pj.y - pi.y) + pi.x) in = !in;
            }
            if (!in) continue;
        } else if (!inPoly(l.x, l.y, A.lp)) continue;
        double best = 1e18; V2 bq;
        auto eachEdge = [&](auto&& f) {
            if (cand) for (int i : *cand) f((size_t)i);
            else for (size_t i = 0; i < n; i++) f(i);
        };
        if (A.cav.empty()) {
            if (cand) {   // shallow first: an edge found within `tight` means the nearest one is among those within it
                for (int i : A.acc.close[(size_t)cell]) {
                    double d2; V2 q = closestOnSeg(l, A.lp[i], A.lp[i + 1 < (int)n ? i + 1 : 0], &d2);
                    if (d2 < best) { best = d2; bq = q; }
                }
                if (best > A.acc.tight * A.acc.tight) {
                    best = 1e18;
                    for (int i : *cand) {
                        double d2; V2 q = closestOnSeg(l, A.lp[i], A.lp[i + 1 < (int)n ? i + 1 : 0], &d2);
                        if (d2 < best) { best = d2; bq = q; }
                    }
                }
            } else
                for (size_t i = 0; i < n; i++) {
                    double d2; V2 q = closestOnSeg(l, A.lp[i], A.lp[i + 1 < n ? i + 1 : 0], &d2);
                    if (d2 < best) { best = d2; bq = q; }
                }
        } else {  // carved: inside the outline but in a cut is empty space; the cut walls are surfaces too
            bool hollow = false;
            for (auto& c : A.cav) if (carve::inConvex(c, l)) { hollow = true; break; }
            if (hollow) continue;
            auto inCut = [&](V2 q) { for (auto& c : A.cav) if (carve::inConvex(c, q)) return true; return false; };
            eachEdge([&](size_t i) {
                double d2; V2 q = closestOnSeg(l, A.lp[i], A.lp[i + 1 < n ? i + 1 : 0], &d2);
                if (d2 < best && !inCut(q)) { best = d2; bq = q; }
            });
            for (auto& c : A.cav)
                for (size_t i = 0; i < c.size(); i++) {
                    double d2; V2 q = closestOnSeg(l, c[i], c[(i + 1) % c.size()], &d2);
                    if (d2 < best && inPoly(q.x, q.y, A.lp)) { best = d2; bq = q; }
                }
        }
        double d = std::sqrt(best);
        if (d < 1e-7 || d > CONTACT_REACH) continue;
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
Body* seatedMag(Sim& S) {   // the magazine feeding the gun: a removable one latched in its well, or a built-in one
    Joint* J = magwell(S);
    if (J && J->A && J->attached && J->latched) return J->A;
    for (auto& b : S.bodies) if (b.builtInMag && !b.gone) return &b;
    return nullptr;
}
static void solveMagwell(Sim& S, Joint& J) {
    Body& B = *J.B;
    V2 seat = wpt(B, J.path.front()), exitp = wpt(B, J.path.back());
    if (!J.A) {  // try to catch a loose magazine pushed into the mouth of the well
        for (auto& b : S.bodies) {
            if (!b.isMag || !b.loose) continue;
            V2 pa = wpt(b, J.la);
            double da = std::remainder(b.a - B.a - J.pathAng.back(), 2 * 3.14159265358979323846);   // it goes in at the mouth's tilt
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
    double tilt = J.pathAng.empty() ? 0 : J.pathAng.front();   // seated: as drawn (or the first point's tilt)
    if (!J.latched) {
        double best = 1e18;
        for (size_t i = 0; i + 1 < J.path.size(); i++) {
            V2 a = wpt(B, J.path[i]), b = wpt(B, J.path[i + 1]);
            double d2; V2 c = closestOnSeg(pa, a, b, &d2);
            if (d2 < best) {
                best = d2; q = c;
                double l = std::hypot(b.x - a.x, b.y - a.y), t = l > 0 ? std::hypot(c.x - a.x, c.y - a.y) / l : 0;
                if (i + 1 < J.pathAng.size()) tilt = J.pathAng[i] + (J.pathAng[i + 1] - J.pathAng[i]) * t;   // rocks in and out
            }
        }
    }
    double L = std::hypot(q.x - pa.x, q.y - pa.y);
    if (L > 1e-9) solve(M, B, pa, q, (q.x - pa.x) / L, (q.y - pa.y) / L, L);
    solveAng(M, B, tilt);
    pa = wpt(M, J.la);
    double dseat = std::hypot(pa.x - seat.x, pa.y - seat.y);
    if (dseat > 0.002) J.engaged = false;  // release cooldown over once it has moved off the seat
    if (!J.latched && !release && !J.engaged && dseat < 0.0008) J.latched = true;
}
// ---------- spawned bodies: removed ones are reused, so a long session never runs out of room
bool spawnSlot(Sim& S, Body** slot, BodyDef** def) {
    for (auto& o : S.bodies)
        if (o.gone && !o.tmpl.empty() && o.d) { *slot = &o; *def = const_cast<BodyDef*>(o.d); return true; }  // spawned defs live in extraDefs
    if (S.bodies.size() >= S.bodies.capacity()) {
        // the bench is full: clear away the oldest thing lying on it, first an empty case, then a dropped magazine,
        // then a loose round (never anything in the gun or in your hand)
        Joint* J = magwell(S);
        auto lying = [&](const Body& o) { return !o.gone && !o.tmpl.empty() && o.d && !(S.hand.on && S.hand.b == &o); };
        Body* pick = nullptr;
        for (int pass = 0; pass < 3 && !pick; pass++)
            for (auto& o : S.bodies) {
                if (!lying(o)) continue;
                bool ok = pass == 0 ? (o.isCase && o.rs == RS::Loose)
                        : pass == 1 ? (o.isMag && o.loose && !(J && J->A == &o) && !o.top)
                                    : (o.isRound && !o.isCase && o.rs == RS::Loose);
                if (ok) { pick = &o; break; }
            }
        if (!pick) return false;
        retireBody(S, *pick);
        *slot = pick; *def = const_cast<BodyDef*>(pick->d);
        return true;
    }
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
        if (!(b.isMag || b.builtInMag) || b.gone || !b.d) continue;
        int cap = b.d->magCap, add = cap - b.rounds;
        if (b.d->clip == "enbloc" && b.rounds > 0) continue;  // an en-bloc clip can't be topped up: only a full one goes in
        if (b.d->clip == "stripper") add = std::min(add, b.d->clipSize);  // one clip's worth at a time
        if (add > 0) { added += add; b.rounds += add; b.idle = 0; if (!b.d->feedPawl.empty() && !b.top) b.indexReady = true; }  // (a new belt: its first round goes in the tray)
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
        bool held = (b.isMag && !b.loose) || (b.isRound && (b.rs == RS::Presented || b.rs == RS::Tube));
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
    for (int k = 0; k < chamberCount(S); k++) {  // each chamber that has fired pushes on its own breech face
        double t0 = S.chambers.empty() ? S.fireT : S.chambers[k].shotT;
        if (!(S.t >= t0 && S.t < t0 + dur)) continue;
        useChamber(S, k);
        if (!S.BR || !S.hasFace) continue;
        double F = br.impulse * S.powerScale / dur, cx = std::cos(S.CH->a), cy = std::sin(S.CH->a);
        V2 p = wpt(*S.BR, S.faceB);
        Body& b = *S.BR;
        double rx = p.x - b.x, ry = p.y - b.y;
        b.fx -= cx * F; b.fy -= cy * F; b.tq -= rx * cy * F - ry * cx * F;
    }
    useChamber(S, 0);
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
    // geometric joints. An action lock holds before anything else is solved, so a shot can't shove what it holds (and
    // through it, whatever is linked to it) before the lock has caught it.
    for (auto& J : S.joints) {
        if (J.type != JT::Latch || J.merged || !J.j->holdShut) continue;
        Body &A = *J.A, &B = *J.B;
        {
            const JointDef& j = *J.j;
            double u = axial(A, B, J.rel0) * 1000, c = j.catchAt;
            // released by the release part drawn back against the held part (a pump's bar in its lost motion): a shot
            // that throws both back together never releases it
            double r = J.R ? axial(*J.R, A, J.relRA0) * 1000 : 0;
            bool release = J.R && r <= -std::fabs(j.releaseAt);
            if (J.state == LatchState::Armed && u >= c && !release) J.state = LatchState::Hold;
            if (J.state == LatchState::Hold) {
                if (release) J.state = LatchState::Released;
                else if (u < c) solveAxial(A, B, (c - u) / 1000);
            }
            if (J.state == LatchState::Released && !release) J.state = LatchState::Armed;  // catches again when it comes home
        }
    }
    for (auto& J : S.joints) {
        if (J.type == JT::Magwell) { solveMagwell(S, J); continue; }
        if (J.merged) continue;
        Body &A = *J.A, &B = *J.B;
        if (J.type == JT::Latch) continue;  // (held shut: above; catches that hold a part open: with the rotation joints, below)
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
        } else if (J.type == JT::Latch && !j.holdShut) {
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
    for (int k = 1; k < chamberCount(S); k++) {  // the further chambers: their extractors take hold when the breech closes
        useChamber(S, k);
        if (!S.BR || !S.hasFace) continue;
        double gap = breechGap(S);
        if (gap < 1 && (S.chambered || S.spent)) S.gripped = true;
        if (gap < 1 && S.inTransit) { S.inTransit = false; S.chambered = true; }
    }
    useChamber(S, 0);
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
        } else if (gap > feed && S.feedMode == "magazine" && !S.chambered && !S.inTransit) {
            // a tube (rounds nose to tail) feeds the simple way for now, even with physical rounds: the round reaches the
            // chamber once the action has opened this far. (Feeding it physically needs a carrier you draw: to come.)
            Body* m = seatedMag(S);
            if (m && m->rounds > 0 && m->d && m->d->roundStyle == "line" && m->d->roundPath.size() >= 2 && m->d->tubeFeed != "physical") { m->rounds--; S.inTransit = true; }
        }
        if (gap < 1 && S.inTransit) { S.inTransit = false; S.chambered = true; }
        if (S.strikerIgnition) {  // fires when the striker's tip hits the primer hard enough
            // (with several chambers: whichever primer it comes down on)
            Body& st = *S.STR;
            V2 tip = wpt(st, S.strTip);
            double rx = tip.x - st.x, ry = tip.y - st.y;
            double d = 1e9; int hit = 0;
            for (int k = 0; k < chamberCount(S); k++) {
                useChamber(S, k);
                if (!S.BR || !S.hasFace) continue;
                V2 primer = wpt(*S.BR, S.faceB);
                double dk = std::hypot(tip.x - primer.x, tip.y - primer.y);
                if (br.cart.kind == "rimfire") {  // the priming is in the rim: only a blow on its edge (above or below the bore) fires it
                    double rr = std::max(0.5, br.cart.rimDia / 2 - 0.6) / 1000, ux = -std::sin(S.CH->a), uy = std::cos(S.CH->a);
                    dk = std::min(std::hypot(tip.x - primer.x - ux * rr, tip.y - primer.y - uy * rr), std::hypot(tip.x - primer.x + ux * rr, tip.y - primer.y + uy * rr));
                }
                if (dk < d) { d = dk; hit = k; }
            }
            useChamber(S, hit);
            double vx = st.vx - st.w * ry - S.BR->vx, vy = st.vy + st.w * rx - S.BR->vy;
            if (d > 0.004) S.primed = true;
            if (S.primed && d < 0.0012 && std::hypot(vx, vy) > br.igniteSpeed && S.t > S.fireT + dur * 2) {
                S.primed = false;
                if (S.chambered && inBattery(S)) fireChamber(S);
                else if (inBattery(S)) S.events.push_back({Ev::Click, S.t});
            }
            useChamber(S, 0);
        } else {
            // The built-in hammer or striker: no parts, but the action must open far enough to cock it again.
            if (br.cockAt <= 0 || gap >= br.cockAt) S.cocked = true;
            auto ignite = [&]() {  // the hammer reaches the primer
                S.igniteAt = -1;
                int n = chamberCount(S);
                if (n == 1) {
                    if (!inBattery(S)) return;
                    if (S.chambered) fireChamber(S);
                    else S.events.push_back({Ev::Click, S.t});
                    return;
                }
                // several chambers: the next loaded one in turn, or every loaded one at once (a volley)
                bool closed = false, fired = false;
                for (int i = 0; i < n && !(fired && br.fireOrder != "together"); i++) {
                    int k = br.fireOrder == "together" ? i : (S.nextChamber + i) % n;
                    useChamber(S, k);
                    if (!S.BR || !S.hasFace || !inBattery(S)) continue;
                    closed = true;
                    if (S.chambered) { fireChamber(S); fired = true; S.nextChamber = (k + 1) % n; }
                }
                useChamber(S, 0);
                if (closed && !fired) S.events.push_back({Ev::Click, S.t});
            };
            if (S.igniteAt >= 0 && S.t >= S.igniteAt) ignite();
            bool burst = br.fireMode == "burst", full = br.fullAuto();
            // a held trigger keeps firing in full-auto, and until the burst is spent; a fresh pull is needed otherwise
            bool want = S.pressed || (S.trigger && (chamberCount(S) == 1 ? S.chambered : anyChambered(S)) && (full || (burst && S.burstLeft > 0)));
            if (want && S.igniteAt < 0 && S.t > S.fireT + dur * 2) {
                bool ok = inBattery(S);
                for (int k = 1; k < chamberCount(S) && !ok; k++) { useChamber(S, k); ok = S.BR && S.hasFace && inBattery(S); }  // any one closed
                useChamber(S, 0);
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
