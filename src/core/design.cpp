#include "carve.hpp"
#include "design.hpp"
#include "version.hpp"
#include "json.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

using json = nlohmann::json;
static const double PI_ = 3.14159265358979323846;

static json v2j(const V2& v) { return json::array({v.x, v.y}); }
static V2 j2v(const json& j) {
    V2 v;
    if (j.is_array() && j.size() >= 2) { v.x = j[0].get<double>(); v.y = j[1].get<double>(); }
    return v;
}
template <class T> static T opt(const json& j, const char* k, T def) {
    auto it = j.find(k);
    if (it == j.end() || it->is_null()) return def;
    try { return it->get<T>(); } catch (...) { return def; }
}

// ---------------------------------------------------------------- cartridges
static json cartToJson(const Cartridge& c) {
    return {{"name", c.name}, {"caseLen", c.caseLen}, {"rimDia", c.rimDia}, {"rimThick", c.rimThick}, {"baseDia", c.baseDia},
            {"shoulderDia", c.shoulderDia}, {"shoulderAt", c.shoulderAt}, {"neckDia", c.neckDia}, {"bulletDia", c.bulletDia},
            {"oal", c.oal}, {"noseLen", c.noseLen}, {"bulletMass", c.bulletMass}, {"velocity", c.velocity}, {"rimmed", c.rimmed},
            {"nose", c.nose}, {"meplat", c.meplat}, {"tailLen", c.tailLen}, {"tailDia", c.tailDia}, {"seat", c.seat},
            {"custom", [&] { json a = json::array(); for (auto& q : c.custom) a.push_back({q.x, q.y}); return a; }()},
            {"massFromShape", c.massFromShape}, {"kind", c.kind}, {"projectiles", c.projectiles}, {"shotDia", c.shotDia},
            {"headLen", c.headLen}, {"hullColor", c.hullColor}};
}
static Cartridge cartFromJson(const json& j) {
    Cartridge c;
    c.name = opt<std::string>(j, "name", c.name);
    c.caseLen = opt<double>(j, "caseLen", c.caseLen); c.rimDia = opt<double>(j, "rimDia", c.rimDia);
    c.rimThick = opt<double>(j, "rimThick", c.rimThick); c.baseDia = opt<double>(j, "baseDia", c.baseDia);
    c.shoulderDia = opt<double>(j, "shoulderDia", c.shoulderDia); c.shoulderAt = opt<double>(j, "shoulderAt", c.shoulderAt);
    c.neckDia = opt<double>(j, "neckDia", c.neckDia); c.bulletDia = opt<double>(j, "bulletDia", c.bulletDia);
    c.oal = opt<double>(j, "oal", c.oal); c.noseLen = opt<double>(j, "noseLen", c.noseLen);
    c.bulletMass = opt<double>(j, "bulletMass", c.bulletMass); c.velocity = opt<double>(j, "velocity", c.velocity);
    c.rimmed = opt<bool>(j, "rimmed", c.rimmed);
    c.nose = opt<std::string>(j, "nose", "auto");   // older cartridges keep the original shape
    if (noseIndex(c.nose) == 0) c.nose = "auto";
    c.meplat = std::clamp(opt<double>(j, "meplat", 0.0), 0.0, 0.95);
    c.tailLen = std::max(0.0, opt<double>(j, "tailLen", 0.0)); c.tailDia = std::clamp(opt<double>(j, "tailDia", 0.8), 0.3, 1.0);
    c.seat = std::max(0.0, opt<double>(j, "seat", 0.0));
    if (j.contains("custom") && j["custom"].is_array())
        for (auto& q : j["custom"]) if (q.is_array() && q.size() >= 2) c.custom.push_back({q[0].get<double>(), q[1].get<double>()});
    c.massFromShape = opt<bool>(j, "massFromShape", false);
    c.kind = cartKindId(cartKindIndex(opt<std::string>(j, "kind", "centerfire")));
    c.projectiles = std::clamp(opt<int>(j, "projectiles", 1), 1, 2000);
    c.shotDia = std::clamp(opt<double>(j, "shotDia", 0.0), 0.0, 30.0);
    c.headLen = std::clamp(opt<double>(j, "headLen", 8.0), 0.0, 60.0);
    c.hullColor = opt<unsigned>(j, "hullColor", 0xB0302AFF);
    if (c.massFromShape) c.bulletMass = bulletShapeMass(c);
    return c;
}
static Cartridge mk(const char* n, double caseLen, double rim, double rimT, double base, double shDia, double shAt, double neck,
                    double bullet, double oal, double nose, double mass, double vel, bool rimmed) {
    Cartridge c;
    c.name = n; c.caseLen = caseLen; c.rimDia = rim; c.rimThick = rimT; c.baseDia = base; c.shoulderDia = shDia; c.shoulderAt = shAt;
    c.neckDia = neck; c.bulletDia = bullet; c.oal = oal; c.noseLen = nose; c.bulletMass = mass; c.velocity = vel; c.rimmed = rimmed;
    return c;
}
// Nominal published dimensions; bullet weights and velocities are typical loads, not a reloading reference.
// ---------------------------------------------------------------- bullet shapes
static const char* NOSE_IDS[] = {"auto", "spitzer", "round", "flat", "hollow", "wadcutter", "semiwad", "ball", "custom"};
const char* noseStyles() { return "Classic\0Spitzer\0Round nose\0Flat nose\0Hollow point\0Wadcutter\0Semi-wadcutter\0Round ball\0Custom (drawn)\0"; }
const char* noseId(int i) { return NOSE_IDS[std::clamp(i, 0, 8)]; }
int noseIndex(const std::string& id) { for (int i = 0; i < 9; i++) if (id == NOSE_IDS[i]) return i; return 0; }
double bulletBase(const Cartridge& c) {
    if (c.nose == "ball") return std::max(0.0, c.oal - c.bulletDia);
    double seat = c.seat > 0 ? c.seat : c.bulletDia * (c.shoulderAt < c.caseLen - 0.5 ? 1.1 : 0.55);  // bottleneck cases seat deeper
    return std::clamp(c.caseLen - seat, 0.5, c.oal - 1.0);
}
std::vector<V2> bulletShape(const Cartridge& c) {
    std::vector<V2> p;
    if (c.nose == "auto") return p;
    // the nose starts no further back than the case mouth: inside the case the bullet is full width
    double R = c.bulletDia / 2, xb = bulletBase(c), tip = c.oal, L = std::clamp(c.noseLen, 0.3, std::max(0.3, tip - std::max(xb, c.caseLen) - 0.2));
    double m = std::clamp(c.meplat, 0.0, 0.95) * R;
    if (c.nose == "custom" && c.custom.size() >= 2) {
        for (auto& q : c.custom) p.push_back({xb + q.x, std::max(0.0, q.y)});
        return p;
    }
    if (c.nose == "ball") {  // a sphere: the whole bullet is the "nose"
        for (int k = 0; k <= 16; k++) { double a = PI_ * k / 16; p.push_back({xb + R - R * std::cos(a), R * std::sin(a)}); }
        return p;
    }
    // base, with an optional boat tail
    double tl = std::clamp(c.tailLen, 0.0, (tip - xb) * 0.4);
    if (tl > 0.05) { p.push_back({xb, R * std::clamp(c.tailDia, 0.3, 1.0)}); p.push_back({xb + tl, R}); }
    else p.push_back({xb, R});
    double xn = tip - L;   // where the nose begins
    const int N = 10;
    if (c.nose == "spitzer") {  // a tangent ogive, cut off at the flat tip
        double rho = (L * L + R * R) / (2 * R);
        p.push_back({xn, R});
        for (int k = 1; k <= N; k++) {
            double x = L * k / N, r = std::sqrt(std::max(0.0, rho * rho - x * x)) + R - rho;  // x from the nose's start: R there, 0 at the tip
            if (r <= m) { p.push_back({xn + x, m}); break; }
            p.push_back({xn + x, r});
        }
        if (p.back().y > 0.01) p.push_back({p.back().x, 0});
    } else if (c.nose == "round" || c.nose == "hollow") {  // an elliptical round nose (a hollow point is blunter, with a cavity)
        double mm = c.nose == "hollow" ? std::max(m, 0.45 * R) : m;
        p.push_back({xn, R});
        for (int k = 1; k <= N; k++) {
            double x = L * k / N, r = R * std::sqrt(std::max(0.0, 1 - (x / L) * (x / L)));
            if (r <= mm) { p.push_back({xn + x, mm}); break; }
            p.push_back({xn + x, r});
        }
        if (c.nose == "hollow") {  // the cavity
            double depth = std::min(L * 0.6, mm * 1.6), xt = p.back().x;
            p.push_back({xt, mm * 0.7}); p.push_back({xt - depth, mm * 0.35}); p.push_back({xt - depth, 0});
        } else if (p.back().y > 0.01) p.push_back({p.back().x, 0});
    } else if (c.nose == "flat") {  // a truncated cone
        double mm = std::max(m, 0.55 * R);
        p.push_back({xn, R}); p.push_back({tip, mm}); p.push_back({tip, 0});
    } else if (c.nose == "wadcutter") {  // full diameter to a sharp-edged flat front
        p.push_back({tip - 0.3, R}); p.push_back({tip, R * 0.92}); p.push_back({tip, 0});
    } else if (c.nose == "semiwad") {  // a full-width shoulder, then a cone to a wide flat
        double sh = std::min(L * 0.25, 1.5), mm = std::max(m, 0.6 * R);
        p.push_back({xn, R}); p.push_back({xn + sh * 0.3, R * 0.86}); p.push_back({tip, mm}); p.push_back({tip, 0});
    } else {
        p.push_back({tip, R}); p.push_back({tip, 0});
    }
    return p;
}
double bulletLength(const Cartridge& c) { return std::max(c.bulletDia * 0.6, c.oal - bulletBase(c)); }
double spinStability(const Cartridge& c, double twistMM, bool rifled) {
    if (!rifled || twistMM <= 0) return 0;
    // the Miller rule, in its own units: grains, inches, calibers, with the speed correction
    double m = c.bulletMass * 15.4324, d = c.bulletDia / 25.4;
    if (d <= 0 || m <= 0) return 0;
    double t = (twistMM / 25.4) / d, l = (bulletLength(c) / 25.4) / d;
    double sg = 30.0 * m / (t * t * d * d * d * l * (1 + l * l));
    return sg * std::cbrt(std::max(100.0, c.velocity * 3.28084) / 2800.0);
}
double bulletShapeMass(const Cartridge& c) {
    auto p = bulletShape(c);
    if (p.size() < 2) return c.bulletMass;
    double v = 0;   // volume of revolution (mm^3); a cavity runs backwards along the axis and subtracts itself
    for (size_t i = 0; i + 1 < p.size(); i++) {
        double h = p[i + 1].x - p[i].x, r1 = p[i].y, r2 = p[i + 1].y;
        v += PI_ * h / 3 * (r1 * r1 + r1 * r2 + r2 * r2);
    }
    return std::max(0.05, v * 0.0109);   // jacketed lead, about 10.9 g/cm^3
}
double ballisticCoefficient(const Cartridge& c) {
    double d = c.bulletDia / 25.4, lb = c.bulletMass / 453.592;
    if (d <= 0) return 0;
    double sd = lb / (d * d);   // sectional density
    double i;                   // form factor against the G1 standard bullet: lower is sleeker
    double cal = c.noseLen / std::max(0.1, c.bulletDia);   // nose length in calibres
    const std::string& n = c.nose;
    if (n == "spitzer") i = std::clamp(1.15 - 0.22 * cal, 0.45, 1.0) * (1 + 0.6 * c.meplat);
    else if (n == "round" || n == "auto") i = 1.0;
    else if (n == "hollow") i = 1.1;
    else if (n == "flat") i = 1.2 + 0.4 * std::max(c.meplat, 0.55);
    else if (n == "wadcutter") i = 2.0;
    else if (n == "semiwad") i = 1.45;
    else if (n == "ball") i = 1.5;
    else {  // drawn: from how long and how pointed its front is
        auto p = bulletShape(c);
        double R = c.bulletDia / 2, start = c.oal, tipR = 0;
        for (auto& q : p) if (q.y < R * 0.98 && q.x > bulletBase(c) + 0.3) { start = std::min(start, q.x); }
        if (!p.empty()) tipR = p.back().y > 0 ? p.back().y : (p.size() >= 2 ? p[p.size() - 2].y : 0);
        double fin = (c.oal - start) / std::max(0.1, c.bulletDia);
        i = std::clamp(1.2 - 0.22 * fin + 0.8 * tipR / std::max(R, 0.01), 0.45, 2.2);
    }
    if (c.tailLen > 0.05 && n != "ball" && n != "custom") i *= 0.92;   // a boat tail
    return sd / i;
}

std::vector<V2> cartridgeOutline(const Cartridge& c, bool caseOnly) {
    // (x along the axis, radius) from the head to the bullet tip, then mirrored for the underside
    double rim = c.rimDia / 2, base = c.baseDia / 2, sh = c.shoulderDia / 2, neck = c.neckDia / 2, bul = c.bulletDia / 2;
    double sAt = std::min(c.shoulderAt, c.caseLen), ogive = std::max(1.0, std::min(c.noseLen, c.oal - c.caseLen));
    std::vector<V2> top = {{0, rim}, {c.rimThick, rim}};
    if (c.kind == "shotshell") {  // straight hull, crimped shut (a fired one has opened up)
        top.push_back({c.rimThick, base});
        double cr = std::min(3.0, c.caseLen * 0.08);
        top.push_back({c.caseLen - cr, base});
        if (caseOnly) { top.push_back({c.caseLen, base}); top.push_back({c.caseLen, base * 0.9}); }
        else top.push_back({c.caseLen, base * 0.62});
    } else {
    if (c.kind == "caseless") top.push_back({c.rimThick, base});  // no rim, no groove: just the block
    else if (!c.rimmed) top.push_back({c.rimThick + 0.6, base * 0.86}), top.push_back({c.rimThick + 1.4, base});  // extractor groove
    else top.push_back({c.rimThick, base});
    top.push_back({sAt, sh});
    if (sAt < c.caseLen - 0.5) top.push_back({std::min(c.caseLen - 0.3, sAt + (sh - neck) * 1.6 + 0.3), neck});
    top.push_back({c.caseLen, caseOnly ? neck : std::max(neck, bul)});
    if (caseOnly) top.push_back({c.caseLen, neck * 0.82});  // the empty mouth
    auto shape = caseOnly ? std::vector<V2>{} : bulletShape(c);
    if (!shape.empty()) {  // the drawn shape, from where it leaves the case mouth (a ball can stand proud of a short case)
        double mouth = c.caseLen;
        bool started = false;
        for (size_t i = 0; i < shape.size(); i++) {
            if (shape[i].x <= mouth + 1e-6 && !started) continue;
            if (!started && i > 0) {  // where the outline crosses the mouth
                V2 a = shape[i - 1], b = shape[i];
                double t = b.x > a.x ? (mouth - a.x) / (b.x - a.x) : 0;
                top.push_back({mouth, a.y + (b.y - a.y) * std::clamp(t, 0.0, 1.0)});
            }
            started = true;
            top.push_back(shape[i]);
        }
        if (!started) top.push_back({c.oal, 0});
        // Real tips are never needle sharp, and a needle point would slide under a feed ramp instead of riding up it:
        // the outline that collides stops where the nose is down to about a quarter of the bullet's width.
        double minTip = bul * 0.28;
        for (size_t i = 1; i < top.size(); i++)
            if (top[i].x >= mouth && top[i].y < minTip && top[i - 1].y >= minTip) {
                V2 a = top[i - 1], b = top[i];
                double t = (a.y - minTip) / std::max(1e-9, a.y - b.y);
                double xc = a.x + (b.x - a.x) * t;
                bool cavity = false;   // a hollow point's mouth is a real flat front: keep it
                for (size_t k = i; k < top.size(); k++) if (top[k].x < top[k - 1].x - 1e-6) cavity = true;
                if (!cavity) { top.resize(i); top.push_back({xc, minTip}); top.push_back({std::max(xc, c.oal - 0.05), minTip * 0.6}); }
                break;
            }
    } else {
        double bodyEnd = std::max(c.caseLen, c.oal - ogive);
        if (!caseOnly) top.push_back({bodyEnd, bul});
        for (int k = 1; k <= 4 && !caseOnly; k++) {  // ogive
            double t = k / 4.0;
            top.push_back({bodyEnd + ogive * t, bul * std::sqrt(std::max(0.0, 1 - t * t * 0.92))});
        }
    }
    }  // (not a shotshell)
    std::vector<V2> out;
    for (auto& p : top) out.push_back({p.x, -p.y});
    for (int k = (int)top.size() - 1; k >= 0; k--)
        if (top[k].y > 1e-6) out.push_back({top[k].x, top[k].y});   // a point on the axis (a sharp tip) only once
    // and no zero-length edges anywhere
    std::vector<V2> clean;
    for (auto& p : out) if (clean.empty() || std::hypot(p.x - clean.back().x, p.y - clean.back().y) > 1e-4) clean.push_back(p);
    return clean;
}
// ---------------------------------------------------------------- how it's held
const char* holdName(int h) { return h == HOLD_HIP ? "Hip" : h == HOLD_LOWERED ? "Lowered" : "Aim"; }
const char* holdKey(int h) { return h == HOLD_HIP ? "hip" : h == HOLD_LOWERED ? "lowered" : "aim"; }
double defaultHandAngle(const std::string& which, const std::string& style) {
    if (which == "support") return style == "vertical" ? 95 : 0;    // fore-end: along the barrel, the hand under it
    if (which == "butt") return 90;                                 // the butt pad, upright
    return style == "wrist" ? 145 : 110;                            // pistol grip, raked back; a rifle's wrist
}
std::string heldWith(const Design& d) {
    return !d.pose.firing.on ? "" : d.pose.support.on ? "two hands" : "one hand";
}
BodyHold holdOf(const Design& d, int h) {
    if (d.pose.hold[h].set) return d.pose.hold[h];
    // the top of the gun (about where sights would be) and its rear
    double top = 1e9, x0 = 1e9, x1 = -1e9;
    for (auto& b : d.bodies) {
        if (b.magazine) continue;
        for (auto& p : b.pts) { top = std::min(top, p.y); x0 = std::min(x0, p.x); x1 = std::max(x1, p.x); }
    }
    if (top > 1e8) { top = 0; x0 = 0; x1 = 200; }
    const Pose& P = d.pose;
    BodyHold o;
    double sight = top - 15;   // the eye just over the top line
    if (h == HOLD_HIP) {
        double gx = P.firing.on ? P.firing.at.x : P.butt.on ? P.butt.at.x + 200 : x0 + 100;
        // the gun at chest height on the right, the eye above and to its left; a stocked gun tucked in close
        o.eye = {gx - (P.butt.on ? 110 : 330), sight - 330}; o.eyeZ = -110;
        if (P.butt.on) o.yaw = 35;
    } else {
        o.eye = {P.butt.on ? P.butt.at.x + 90 : P.firing.on ? P.firing.at.x - 440 : x0 - 450, sight};
        if (P.butt.on) o.yaw = 20;   // a shouldered gun: the body stands bladed, the support shoulder forward
        if (h == HOLD_LOWERED) o.tilt = 35;
    }
    return o;
}
static const char* KIND_IDS[] = {"centerfire", "rimfire", "shotshell", "caseless"};
const char* cartKinds() { return "Centrefire\0Rimfire\0Shotshell\0Caseless\0"; }
const char* cartKindId(int i) { return KIND_IDS[std::clamp(i, 0, 3)]; }
int cartKindIndex(const std::string& id) { for (int i = 0; i < 4; i++) if (id == KIND_IDS[i]) return i; return 0; }
double spentCaseMass(const Cartridge& c) {
    const double brass = 0.0085, plastic = 0.00095;  // g per mm^3
    if (c.kind == "caseless") return 0;
    if (c.kind == "shotshell") {  // a thin brass head with a thick base wad, and a plastic hull
        double hl = std::clamp(c.headLen, 0.0, c.caseLen);
        double head = 3.14159 * c.baseDia * hl * 0.3 * brass + 3.14159 * c.headDia() * c.headDia() / 4 * 1.5 * brass;
        return head + 3.14159 * c.baseDia * c.caseLen * 0.6 * plastic + 1.0;  // + the base wad
    }
    double wall = 3.14159 * c.baseDia * c.caseLen * 0.35 * brass, head = 3.14159 * c.headDia() * c.headDia() / 4 * 3 * brass;
    if (c.kind == "rimfire") head *= 0.4;  // thin, folded head
    return wall + head;
}
double cartridgeMass(const Cartridge& c) {
    if (c.kind == "caseless") {  // the block of propellant
        double r = c.baseDia / 2;
        return c.payloadMass() + 3.14159 * r * r * c.caseLen * 0.0012;
    }
    if (c.kind == "shotshell") return c.payloadMass() + spentCaseMass(c) + 2.0 /* wad and powder */;
    return c.payloadMass() + spentCaseMass(c);
}

const std::vector<Cartridge>& cartridgePresets() {
    static std::vector<Cartridge> v = [] {
        auto shaped = [](Cartridge c, const char* nose, double tail = 0) { c.nose = nose; c.tailLen = tail; return c; };
        auto kinded = [](Cartridge c, const char* kind) { c.kind = kind; return c; };
        // 12 gauge, 2 3/4 inch: about 63.5 mm long once crimped, bore 18.5 mm
        auto shell = [](const char* n, unsigned col, double shotDia, int count, double vel, double slug = 0) {
            Cartridge c = mk(n, 63.5, 22.80, 1.50, 20.60, 20.60, 63.5, 20.60, 18.50, 63.5, 0, slug, vel, true);
            c.kind = "shotshell"; c.shotDia = shotDia; c.projectiles = count; c.hullColor = col; c.headLen = shotDia > 0 && shotDia < 4 ? 8 : 15;
            return c;
        };
        return std::vector<Cartridge>{
            shaped(mk("9x19mm", 19.15, 9.96, 1.27, 9.93, 9.65, 19.15, 9.65, 9.01, 29.69, 7.5, 7.45, 360, false), "round"),
            shaped(mk(".380 ACP", 17.30, 9.50, 1.10, 9.50, 9.50, 17.30, 9.50, 9.02, 25.00, 6.0, 6.15, 290, false), "round"),
            shaped(mk(".45 ACP", 22.80, 12.19, 1.24, 12.09, 12.01, 22.80, 12.01, 11.48, 32.40, 8.5, 14.90, 255, false), "round"),
            shaped(mk("5.56x45mm", 44.70, 9.60, 1.14, 9.58, 9.00, 36.50, 6.43, 5.70, 57.40, 14.0, 4.00, 940, false), "spitzer"),
            shaped(mk("7.62x39mm", 38.70, 11.35, 1.50, 11.35, 10.07, 30.50, 8.60, 7.92, 56.00, 14.0, 7.90, 730, false), "spitzer", 3.0),
            shaped(mk("7.62x54mmR", 53.72, 14.48, 1.60, 12.37, 11.61, 45.00, 8.53, 7.92, 77.16, 18.0, 9.60, 830, true), "spitzer", 3.0),
            kinded(shaped(mk(".41 Short", 11.70, 11.20, 1.00, 10.40, 10.40, 11.70, 10.40, 10.40, 23.40, 6.0, 8.40, 130, true), "round"), "rimfire"),
            kinded(shaped(mk(".22 LR", 15.57, 6.98, 1.04, 5.74, 5.74, 15.57, 5.74, 5.70, 25.40, 6.0, 2.59, 370, true), "round"), "rimfire"),
            shell("12 gauge 00 buck", 0xB0302AFF, 8.38, 9, 400),
            shell("12 gauge slug", 0x2F7A3AFF, 0, 1, 480, 28.35),
            shell("12 gauge birdshot", 0xC9A227FF, 2.41, 380, 370),
            kinded(shaped(mk("6mm caseless", 26.0, 9.50, 0.0, 9.50, 9.50, 26.0, 9.50, 6.00, 36.0, 10.0, 5.0, 880, false), "spitzer"), "caseless"),
        };
    }();
    return v;
}
std::vector<Cartridge> loadUserCartridges() {
    std::vector<Cartridge> out;
    std::ifstream in("cartridges.json");
    if (!in) return out;
    std::stringstream ss; ss << in.rdbuf();
    try {
        json j = json::parse(ss.str());
        if (j.is_array()) for (auto& c : j) out.push_back(cartFromJson(c));
    } catch (...) {}
    return out;
}
bool saveUserCartridges(const std::vector<Cartridge>& list) {
    json j = json::array();
    for (auto& c : list) j.push_back(cartToJson(c));
    std::ofstream out("cartridges.json");
    if (!out) return false;
    out << j.dump(2);
    return true;
}
// ---------------------------------------------------------------- barrels
bool barrelEnds(const BodyDef& b, V2& rear, V2& muzzle) {
    if (!b.barrel.on || b.pts.size() < 3) return false;
    carve::RoundAxis ax = carve::roundAxis(b);
    double u0 = 1e9, u1 = -1e9;
    for (auto& p : outlineOf(b)) { double u = (p.x - ax.o.x) * ax.dir.x + (p.y - ax.o.y) * ax.dir.y; u0 = std::min(u0, u); u1 = std::max(u1, u); }
    V2 a{ax.o.x + ax.dir.x * u0, ax.o.y + ax.dir.y * u0}, c{ax.o.x + ax.dir.x * u1, ax.o.y + ax.dir.y * u1};
    if (b.barrel.muzzleEnd > 0) { rear = a; muzzle = c; } else { rear = c; muzzle = a; }
    return true;
}
int barrelCount(const Design& d) { int n = 0; for (auto& b : d.bodies) if (b.barrel.on) n++; return n; }
void syncBarrels(Design& d) {
    std::vector<const BodyDef*> bs;
    for (auto& b : d.bodies) if (b.barrel.on) bs.push_back(&b);
    if (bs.empty()) return;
    BreechDef& br = d.breech;
    if (!br.on) { br = BreechDef(); br.on = true; br.physicalFeed = true; br.feedMode = "single"; }
    V2 rear, muz;
    barrelEnds(*bs[0], rear, muz);
    br.chamber = bs[0]->id; br.face = rear; br.hasFace = true; br.muzzle = muz; br.hasMuzzle = true;
    br.cart = bs[0]->barrel.cart; br.cartImpulse = true; applyCartridge(br);
    std::vector<ChamberDef> more;
    for (size_t k = 1; k < bs.size(); k++) {
        ChamberDef c;
        for (auto& o : br.more) if (o.chamber == bs[k]->id) c = o;   // keep what was set for it (its breech part, loaded)
        if (c.breech.empty()) c.breech = br.breech;
        barrelEnds(*bs[k], rear, muz);
        c.chamber = bs[k]->id; c.face = rear; c.muzzle = muz; c.hasMuzzle = true;
        c.ownCart = true; c.cart = bs[k]->barrel.cart;
        more.push_back(c);
    }
    br.more = more;
}
void applyCartridge(BreechDef& b) {
    b.roundLen = b.cart.oal;
    b.roundDia = b.cart.headDia();
    if (b.cartImpulse) b.impulse = std::round(b.cart.impulse() * 100) / 100;
}

// ---------------------------------------------------------------- shapes
static bool inside(double x, double y, const std::vector<V2>& p) {
    bool c = false;
    for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) {
        double xi = p[i].x, yi = p[i].y, xj = p[j].x, yj = p[j].y;
        if (((yi > y) != (yj > y)) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) c = !c;
    }
    return c;
}
void fixShape(BodyDef& b) {
    b.rad.resize(b.pts.size(), 0.0);
    b.bulge.resize(b.pts.size(), 0.0);
}
std::vector<V2> outlineOf(const BodyDef& b) {
    const auto& P = b.pts;
    size_t n = P.size();
    if (n < 3) return P;
    bool any = false;
    for (size_t i = 0; i < n; i++) {
        if (i < b.rad.size() && b.rad[i] > 0.01) any = true;
        if (i < b.bulge.size() && std::fabs(b.bulge[i]) > 0.01) any = true;
    }
    if (!any) return P;
    struct Corner { V2 t1, t2, c; double r = 0; bool arc = false; };
    std::vector<Corner> C(n);
    for (size_t i = 0; i < n; i++) {
        V2 p = P[i], a = P[(i + n - 1) % n], q = P[(i + 1) % n];
        double r = i < b.rad.size() ? b.rad[i] : 0;
        C[i].t1 = C[i].t2 = p;
        if (r <= 0.01) continue;
        double la = std::hypot(a.x - p.x, a.y - p.y), lb = std::hypot(q.x - p.x, q.y - p.y);
        if (la < 1e-6 || lb < 1e-6) continue;
        V2 u1{(a.x - p.x) / la, (a.y - p.y) / la}, u2{(q.x - p.x) / lb, (q.y - p.y) / lb};
        double ang = std::acos(std::clamp(u1.x * u2.x + u1.y * u2.y, -1.0, 1.0)), half = ang / 2;
        if (half < 1e-3 || half > PI_ / 2 - 1e-3) continue;
        double t = r / std::tan(half), maxT = 0.48 * std::min(la, lb);
        if (t > maxT) { t = maxT; r = t * std::tan(half); }
        V2 bis{u1.x + u2.x, u1.y + u2.y};
        double bl = std::hypot(bis.x, bis.y);
        bis = {bis.x / bl, bis.y / bl};
        double d = r / std::sin(half);
        C[i] = {{p.x + u1.x * t, p.y + u1.y * t}, {p.x + u2.x * t, p.y + u2.y * t}, {p.x + bis.x * d, p.y + bis.y * d}, r, true};
    }
    std::vector<V2> out;
    for (size_t i = 0; i < n; i++) {
        const Corner& c = C[i];
        out.push_back(c.t1);
        if (c.arc) {
            double a0 = std::atan2(c.t1.y - c.c.y, c.t1.x - c.c.x), a1 = std::atan2(c.t2.y - c.c.y, c.t2.x - c.c.x);
            double da = std::remainder(a1 - a0, 2 * PI_);
            int m = std::max(2, (int)std::ceil(std::fabs(da) / (10 * PI_ / 180)));
            for (int k = 1; k <= m; k++) { double aa = a0 + da * k / m; out.push_back({c.c.x + c.r * std::cos(aa), c.c.y + c.r * std::sin(aa)}); }
        }
        // edge to the next corner, optionally bowed
        double s = i < b.bulge.size() ? b.bulge[i] : 0;
        if (std::fabs(s) > 0.01) {
            V2 st = c.t2, en = C[(i + 1) % n].t1;
            double ex = en.x - st.x, ey = en.y - st.y, len = std::hypot(ex, ey);
            if (len > 1e-6) {
                V2 nn{ey / len, -ex / len};
                V2 mid{(st.x + en.x) / 2, (st.y + en.y) / 2};
                if (inside(mid.x + nn.x * 0.01 * len, mid.y + nn.y * 0.01 * len, P)) nn = {-nn.x, -nn.y};  // make nn point outward
                double as = std::min(std::fabs(s), len * 0.5);
                double R = (len * len / 4 + as * as) / (2 * as), sg = s > 0 ? 1 : -1;
                double sweep = 2 * std::asin(std::min(1.0, len / (2 * R)));
                int m = std::max(4, (int)std::ceil(sweep / (8 * PI_ / 180)));
                for (int k = 1; k < m; k++) {
                    double tt = (double)k / m, x = (tt - 0.5) * len, y = std::sqrt(std::max(0.0, R * R - x * x)) - (R - as);
                    out.push_back({st.x + ex * tt + nn.x * y * sg, st.y + ey * tt + nn.y * y * sg});
                }
            }
        }
    }
    // drop near-duplicate points
    std::vector<V2> clean;
    for (auto& p : out)
        if (clean.empty() || std::hypot(p.x - clean.back().x, p.y - clean.back().y) > 0.05) clean.push_back(p);
    if (clean.size() > 3 && std::hypot(clean.front().x - clean.back().x, clean.front().y - clean.back().y) < 0.05) clean.pop_back();
    return clean.size() >= 3 ? clean : P;
}
V2 centroidOf(const BodyDef& b) {
    auto p = outlineOf(b);
    double a = 0, cx = 0, cy = 0;
    for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) {
        double f = p[j].x * p[i].y - p[i].x * p[j].y;
        a += f; cx += (p[j].x + p[i].x) * f; cy += (p[j].y + p[i].y) * f;
    }
    if (std::fabs(a) < 1e-12) { V2 s; for (auto& q : p) { s.x += q.x; s.y += q.y; } return {s.x / p.size(), s.y / p.size()}; }
    a /= 2;
    return {cx / (6 * a), cy / (6 * a)};
}
std::vector<V2> circlePts(V2 c, double r, std::vector<double>* bulge) {
    if (bulge) bulge->assign(4, r * (1 - std::cos(PI_ / 4)));
    return {{c.x + r, c.y}, {c.x, c.y + r}, {c.x - r, c.y}, {c.x, c.y - r}};
}

// ---------------------------------------------------------------- knife, mirror, subdivide
namespace {
struct VR { V2 p; double rad = 0, bulge = 0; };  // a vertex with its corner radius and the curve of the edge after it
double distToBoundary(V2 q, const std::vector<VR>& P) {
    double best = 1e18;
    for (size_t i = 0; i < P.size(); i++) {
        V2 a = P[i].p, b = P[(i + 1) % P.size()].p;
        double dx = b.x - a.x, dy = b.y - a.y, l = dx * dx + dy * dy;
        double t = l > 0 ? std::clamp(((q.x - a.x) * dx + (q.y - a.y) * dy) / l, 0.0, 1.0) : 0;
        best = std::min(best, std::hypot(q.x - a.x - t * dx, q.y - a.y - t * dy));
    }
    return best;
}
void splitRec(const std::vector<VR>& P, V2 a, V2 c, int depth, std::vector<std::vector<VR>>& out) {
    size_t n = P.size();
    struct Hit { double t; size_t e; V2 p; };
    std::vector<Hit> hits;
    double sx = c.x - a.x, sy = c.y - a.y;
    for (size_t i = 0; i < n && depth < 10; i++) {
        V2 p = P[i].p, q = P[(i + 1) % n].p;
        double ex = q.x - p.x, ey = q.y - p.y, den = sx * ey - sy * ex;
        if (std::fabs(den) < 1e-12) continue;
        double t = ((p.x - a.x) * ey - (p.y - a.y) * ex) / den, u = ((p.x - a.x) * sy - (p.y - a.y) * sx) / den;
        if (t < -1e-9 || t > 1 + 1e-9 || u < -1e-9 || u >= 1 - 1e-9) continue;
        hits.push_back({t, i, {a.x + sx * t, a.y + sy * t}});
    }
    std::sort(hits.begin(), hits.end(), [](const Hit& x, const Hit& y) { return x.t < y.t; });
    std::vector<V2> poly;
    for (auto& v : P) poly.push_back(v.p);
    for (size_t k = 0; k + 1 < hits.size(); k++) {
        const Hit &h1 = hits[k], &h2 = hits[k + 1];
        if (h1.e == h2.e || std::hypot(h1.p.x - h2.p.x, h1.p.y - h2.p.y) < 0.05) continue;
        V2 mid{(h1.p.x + h2.p.x) / 2, (h1.p.y + h2.p.y) / 2};
        if (!inside(mid.x, mid.y, poly) || distToBoundary(mid, P) < 0.01) continue;
        // cut along the chord h1-h2: walk the outline both ways round
        auto piece = [&](const Hit& from, const Hit& to) {
            std::vector<VR> r;
            r.push_back({from.p, 0, 0});
            size_t i = (from.e + 1) % n;
            for (size_t guard = 0; guard < n; guard++) {
                VR v = P[i];
                if (i == to.e) v.bulge = 0;  // this edge is cut short
                r.push_back(v);
                if (i == to.e) break;
                i = (i + 1) % n;
            }
            r.push_back({to.p, 0, 0});
            std::vector<VR> clean;
            for (auto& v : r)
                if (clean.empty() || std::hypot(v.p.x - clean.back().p.x, v.p.y - clean.back().p.y) > 1e-3) clean.push_back(v);
            if (clean.size() > 2 && std::hypot(clean.front().p.x - clean.back().p.x, clean.front().p.y - clean.back().p.y) < 1e-3) clean.pop_back();
            return clean;
        };
        auto A1 = piece(h1, h2), A2 = piece(h2, h1);
        if (A1.size() >= 3 && A2.size() >= 3) {
            splitRec(A1, a, c, depth + 1, out);
            splitRec(A2, a, c, depth + 1, out);
            return;
        }
    }
    out.push_back(P);
}
}  // namespace

std::vector<BodyDef> splitBody(const BodyDef& b0, V2 a, V2 c) {
    BodyDef b = b0;
    fixShape(b);
    bool curved = false;
    for (double q : b.bulge) if (std::fabs(q) > 0.01) curved = true;
    if (curved) {  // bake curved edges into vertices first so the cut keeps their shape
        BodyDef flat = b;
        flat.pts = outlineOf(b);  // fillets and curves both become real vertices
        flat.rad.assign(flat.pts.size(), 0.0); flat.bulge.assign(flat.pts.size(), 0.0);
        b = flat;
    }
    std::vector<VR> P;
    for (size_t i = 0; i < b.pts.size(); i++) P.push_back({b.pts[i], b.rad[i], b.bulge[i]});
    std::vector<std::vector<VR>> pieces;
    splitRec(P, a, c, 0, pieces);
    std::vector<BodyDef> out;
    for (auto& pc : pieces) {
        BodyDef nb = b;
        nb.pts.clear(); nb.rad.clear(); nb.bulge.clear();
        for (auto& v : pc) {
            nb.pts.push_back({std::round(v.p.x * 100) / 100, std::round(v.p.y * 100) / 100});
            nb.rad.push_back(v.rad); nb.bulge.push_back(v.bulge);
        }
        nb.gen.on = false;
        out.push_back(nb);
    }
    return out;
}
void mirrorBody(BodyDef& b, V2 a, V2 c) {
    fixShape(b);
    double dx = c.x - a.x, dy = c.y - a.y, l2 = dx * dx + dy * dy;
    if (l2 < 1e-12) return;
    carve::mirrorCarves(b, a, c);
    auto refl = [&](V2 p) {
        double t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / l2;
        V2 f{a.x + dx * t, a.y + dy * t};
        return V2{2 * f.x - p.x, 2 * f.y - p.y};
    };
    size_t n = b.pts.size();
    std::vector<V2> pts(n);
    std::vector<double> rad(n), bulge(n);
    for (size_t j = 0; j < n; j++) {  // reflect and reverse so the outline keeps its winding
        pts[j] = refl(b.pts[n - 1 - j]);
        rad[j] = b.rad[n - 1 - j];
        bulge[j] = b.bulge[(2 * n - 2 - j) % n];
    }
    if (depthShaped(b)) {   // the far face, and the depths, go with their corners
        V2 o0 = refl({0, 0});
        std::vector<V2> off(n); std::vector<double> zn(n), zf(n);
        for (size_t j = 0; j < n; j++) {
            V2 e = refl(b.depthOff[n - 1 - j]);
            off[j] = {e.x - o0.x, e.y - o0.y};
            zn[j] = b.depthZn[n - 1 - j]; zf[j] = b.depthZf[n - 1 - j];
        }
        b.depthOff = off; b.depthZn = zn; b.depthZf = zf;
    }
    for (auto& p : pts) { p.x = std::round(p.x * 100) / 100; p.y = std::round(p.y * 100) / 100; }
    b.pts = pts; b.rad = rad; b.bulge = bulge;
    if (!b.depthRef.empty()) b.depthRef = b.pts;
    if (b.ctrl != "none") {
        b.ctrlPt = refl(b.ctrlPt);
        V2 d0{std::cos(b.ctrlDir * PI_ / 180), std::sin(b.ctrlDir * PI_ / 180)}, o = refl({0, 0}), e = refl(d0);
        b.ctrlDir = std::atan2(e.y - o.y, e.x - o.x) * 180 / PI_;
    }
    b.gen.on = false;
}
void flipBody(BodyDef& b, bool horizontal) {
    V2 c = centroidOf(b);
    if (horizontal) mirrorBody(b, c, {c.x, c.y + 1});
    else mirrorBody(b, c, {c.x + 1, c.y});
}
int splitEdge(BodyDef& b, int e) {
    fixShape(b);
    int n = (int)b.pts.size();
    if (n < 2 || e < 0 || e >= n || n >= MAX_CORNERS) return -1;
    V2 p = b.pts[e], q = b.pts[(e + 1) % n];
    double ex = q.x - p.x, ey = q.y - p.y, len = std::hypot(ex, ey);
    V2 mid{(p.x + q.x) / 2, (p.y + q.y) / 2};
    double s = b.bulge[e], s2 = 0;
    if (std::fabs(s) > 0.01 && len > 1e-6) {  // a bowed edge: the new corner goes on the curve, and each half keeps the same arc
        V2 nn{ey / len, -ex / len};
        if (inside(mid.x + nn.x * 0.01 * len, mid.y + nn.y * 0.01 * len, b.pts)) nn = {-nn.x, -nn.y};  // outward, as outlineOf does
        double as = std::min(std::fabs(s), len * 0.5), c = len / 2;
        double R = (c * c + as * as) / (2 * as), c2 = std::sqrt(c * c + as * as) / 2;
        s2 = (R - std::sqrt(std::max(0.0, R * R - c2 * c2))) * (s > 0 ? 1 : -1);
        mid = {mid.x + nn.x * as * (s > 0 ? 1 : -1), mid.y + nn.y * as * (s > 0 ? 1 : -1)};
    }
    mid = {std::round(mid.x * 100) / 100, std::round(mid.y * 100) / 100};
    b.pts.insert(b.pts.begin() + e + 1, mid);
    b.rad.insert(b.rad.begin() + e + 1, 0.0);
    b.bulge[e] = s2;
    b.bulge.insert(b.bulge.begin() + e + 1, s2);
    return e + 1;
}
void simplifyBody(BodyDef& b, int maxCorners) {
    fixShape(b);
    while ((int)b.pts.size() > std::max(3, maxCorners)) {
        int n = (int)b.pts.size(), best = 0;
        double ba = 1e300;
        for (int i = 0; i < n; i++) {  // the corner whose triangle with its neighbours is smallest
            V2 a = b.pts[(i + n - 1) % n], p = b.pts[i], c = b.pts[(i + 1) % n];
            double ar = std::fabs((p.x - a.x) * (c.y - a.y) - (p.y - a.y) * (c.x - a.x)) + b.rad[i] * b.rad[i];
            if (ar < ba) { ba = ar; best = i; }
        }
        int prev = (best + n - 1) % n;
        b.bulge[prev] = (b.bulge[prev] + b.bulge[best]) / 2;
        b.pts.erase(b.pts.begin() + best); b.rad.erase(b.rad.begin() + best); b.bulge.erase(b.bulge.begin() + best);
    }
}
int subdivideBody(BodyDef& b) {
    fixShape(b);
    int n = (int)b.pts.size(), room = std::max(0, MAX_CORNERS - n);
    std::vector<int> order(n);
    for (int i = 0; i < n; i++) order[i] = i;
    auto len = [&](int i) { V2 p = b.pts[i], q = b.pts[(i + 1) % n]; return std::hypot(q.x - p.x, q.y - p.y); };
    std::stable_sort(order.begin(), order.end(), [&](int x, int y) { return len(x) > len(y); });
    order.resize(std::min(n, room));                 // the longest edges, if they can't all be split
    std::sort(order.rbegin(), order.rend());         // from the end, so earlier indices stay put
    int made = 0;
    for (int e : order) if (splitEdge(b, e) >= 0) made++;
    return made;
}

// ---------------------------------------------------------------- magazine maker
V2 magCentre(const MagGen& g, double s, double* phiDeg) {
    V2 p = g.top;
    int N = 40;
    double H = g.H > 1 ? g.H : 1, du = s / N;
    double phi = g.angle;
    for (int k = 0; k < N; k++) {
        double u = (k + 0.5) * du;
        phi = g.angle + g.curve * (u / H);
        p.x += std::sin(phi * PI_ / 180) * du;
        p.y += std::cos(phi * PI_ / 180) * du;
    }
    if (phiDeg) *phiDeg = g.angle + g.curve * (s / H);
    return p;
}
void generateMagazine(Design& D, int bi) {
    if (bi < 0 || bi >= (int)D.bodies.size()) return;
    BodyDef& b = D.bodies[bi];
    MagGen& g = b.gen;
    const Cartridge& c = D.breech.cart;
    int wi = -1;
    for (size_t i = 0; i < D.joints.size(); i++) if (D.joints[i].type == "magwell" && D.joints[i].a == b.id) wi = (int)i;
    const bool wasOn = g.on;
    if (!g.on) {  // first time: keep the top of the existing part where it is
        auto o = outlineOf(b);
        double x0 = 1e9, x1 = -1e9, y0 = 1e9;
        for (auto& p : o) { x0 = std::min(x0, p.x); x1 = std::max(x1, p.x); y0 = std::min(y0, p.y); }
        g.top = {(x0 + x1) / 2, y0};
        g.on = true;
    }
    if (wi >= 0 && D.joints[wi].path.size() >= 2) {  // it follows the well's tilt, but never turns itself over: which way
        auto& j = D.joints[wi];                        // up it goes is set by the Well angle (a top magazine: 180)
        double pa = std::atan2(j.path.back().x - j.path.front().x, j.path.back().y - j.path.front().y) * 180 / PI_;
        if (!wasOn || std::fabs(std::remainder(pa - g.angle, 360.0)) <= 90) g.angle = pa;
    }
    b.magCap = std::clamp(b.magCap, 1, 250);
    double dia = c.headDia() * 1.02;
    double pitch = g.stack == 2 ? dia * 0.56 : dia * 0.98;
    double stackH = g.stack == 2 ? b.magCap * pitch + dia * 0.5 : b.magCap * pitch;
    g.H = 3 + stackH + 12 + 5;  // lips, rounds, follower and spring, floorplate
    g.W = c.oal + 2 * g.wall + 3;
    if (g.side != 0) {   // across the gun: the side view sees only its mouth, a box round the top round
        double yc = g.top.y + 3 + dia / 2, half = dia * (g.stack == 2 ? 0.9 : 0.55) + g.wall;
        double x0 = g.top.x - g.W / 2, x1 = g.top.x + g.W / 2;
        b.pts = {{x0, yc - half}, {x1, yc - half}, {x1, yc + half}, {x0, yc + half}};
        for (auto& q : b.pts) { q.x = std::round(q.x * 100) / 100; q.y = std::round(q.y * 100) / 100; }
        b.rad.assign(4, 1.0);
        b.bulge.assign(4, 0.0);
        b.mass = std::round(45 + 0.35 * g.H);
        b.thick = std::round(g.H);
        b.z = (g.side > 0 ? 1 : -1) * (dia / 2 + 0.5 + b.thick / 2);   // its mouth right beside the top round
        b.magazine = true;
        b.ghost = true;
        if (b.magFixed) {
            if (wi >= 0) D.joints.erase(D.joints.begin() + wi);
            return;
        }
        // the well holds it in the side view; it slides in and out across the gun (the simulation does that part)
        V2 p{g.top.x, g.top.y + 10};
        if (wi < 0) {
            JointDef j; j.type = "magwell"; j.a = b.id; j.b = "world";
            for (int i = (int)D.bodies.size() - 1; i >= 0; i--)
                if (D.bodies[i].fixed && inside(g.top.x, g.top.y + 2, outlineOf(D.bodies[i]))) { j.b = D.bodies[i].id; break; }
            if (j.b == "world") for (auto& o : D.bodies) if (o.fixed) { j.b = o.id; break; }
            D.joints.push_back(j);
            wi = (int)D.joints.size() - 1;
        }
        auto& j = D.joints[wi];
        j.p = p;
        j.path = {p, {p.x, p.y + 1}};
        j.pathAng = {0.0, 0.0};
        return;
    }
    int N = std::max(4, (int)std::ceil(std::fabs(g.curve) / 6) + 4);
    std::vector<V2> rear, front;
    for (int k = 0; k <= N; k++) {
        double s = g.H * k / N, phi;
        V2 m = magCentre(g, s, &phi);
        V2 nrm{std::cos(phi * PI_ / 180), -std::sin(phi * PI_ / 180)};  // points forward
        rear.push_back({m.x - nrm.x * g.W / 2, m.y - nrm.y * g.W / 2});
        front.push_back({m.x + nrm.x * g.W / 2, m.y + nrm.y * g.W / 2});
    }
    b.pts.clear();
    for (auto& p : rear) b.pts.push_back(p);
    for (int k = N; k >= 0; k--) b.pts.push_back(front[k]);
    for (auto& p : b.pts) { p.x = std::round(p.x * 100) / 100; p.y = std::round(p.y * 100) / 100; }
    b.rad.assign(b.pts.size(), 0.0);
    b.bulge.assign(b.pts.size(), 0.0);
    b.rad[N] = 2.5; b.rad[N + 1] = 2.5;  // rounded floorplate corners
    b.mass = std::round(45 + 0.35 * g.H);
    b.thick = std::round(std::max(12.0, c.headDia() * (g.stack == 2 ? 1.75 : 1.15) + 2));
    b.magazine = true;
    b.ghost = true;
    if (b.magFixed) {   // built in: no well to slide along
        if (wi >= 0) D.joints.erase(D.joints.begin() + wi);
        return;
    }
    // the well: straight, along the top of the magazine, long enough for it to drop clear
    double a = g.angle * PI_ / 180;
    V2 d0{std::sin(a), std::cos(a)};
    V2 p{g.top.x + d0.x * 10, g.top.y + d0.y * 10};
    double travel = g.H + 6;
    if (wi < 0) {
        JointDef j; j.type = "magwell"; j.a = b.id; j.b = "world";
        for (int i = (int)D.bodies.size() - 1; i >= 0; i--)
            if (D.bodies[i].fixed && inside(g.top.x, g.top.y + 2, outlineOf(D.bodies[i]))) { j.b = D.bodies[i].id; break; }
        if (j.b == "world") for (auto& o : D.bodies) if (o.fixed) { j.b = o.id; break; }
        D.joints.push_back(j);
        wi = (int)D.joints.size() - 1;
    }
    auto& j = D.joints[wi];
    if (j.path.size() > 2) {   // a shaped insertion path (bends, rock-in): keep its shape, moved to the new seat
        double dx = p.x - j.path.front().x, dy = p.y - j.path.front().y;
        for (auto& q : j.path) { q.x += dx; q.y += dy; }
        j.p = p;
        return;
    }
    j.p = p;
    j.path = {p, {p.x + d0.x * travel, p.y + d0.y * travel}};
    if (j.pathAng.size() != 2) j.pathAng.resize(2, 0.0);   // (a tilt at the mouth is kept)
}


// ---------------------------------------------------------------- welding: parts that are one piece
std::vector<int> weldedWith(const Design& d, int i) {
    std::vector<int> out;
    if (i < 0 || i >= (int)d.bodies.size()) return out;
    if (d.bodies[i].weld.empty()) return {i};
    for (int k = 0; k < (int)d.bodies.size(); k++) if (d.bodies[k].weld == d.bodies[i].weld) out.push_back(k);
    return out;
}
void weldParts(Design& d, const std::string& a, const std::string& b) {
    int ia = d.bodyIndex(a), ib = d.bodyIndex(b);
    if (ia < 0 || ib < 0 || ia == ib) return;
    std::string tag = !d.bodies[ia].weld.empty() ? d.bodies[ia].weld : !d.bodies[ib].weld.empty() ? d.bodies[ib].weld : "w-" + d.bodies[ia].id;
    std::string other = d.bodies[ib].weld;
    for (auto& x : d.bodies) if (!other.empty() && x.weld == other) x.weld = tag;  // b's whole piece joins
    d.bodies[ia].weld = tag; d.bodies[ib].weld = tag;
}
void unweld(Design& d, const std::string& id) {
    int i = d.bodyIndex(id);
    if (i < 0 || d.bodies[i].weld.empty()) return;
    std::string tag = d.bodies[i].weld;
    d.bodies[i].weld.clear();
    std::vector<int> rest;
    for (int k = 0; k < (int)d.bodies.size(); k++) if (d.bodies[k].weld == tag) rest.push_back(k);
    if (rest.size() == 1) d.bodies[rest[0]].weld.clear();  // one part left on its own: not welded to anything
}
void weldJointsToTags(Design& d) {
    std::vector<JointDef> keep;
    for (auto& j : d.joints) {
        if (j.type != "weld") { keep.push_back(j); continue; }
        int ia = d.bodyIndex(j.a), ib = d.bodyIndex(j.b);
        if (ia >= 0 && (j.b == "world" || j.b.empty())) d.bodies[ia].fixed = true;  // welded to the ground: it doesn't move
        else if (ib >= 0 && j.a == "world") d.bodies[ib].fixed = true;
        else if (ia >= 0 && ib >= 0) weldParts(d, j.a, j.b);
    }
    d.joints = keep;
}

// ---------------------------------------------------------------- JSON
std::string designToJson(const Design& d, bool pretty) {
    json o;
    o["format"] = GUNSMITH_BLUEPRINT_FORMAT;
    o["savedWith"] = std::string("Gunsmith3D ") + version::text();
    o["name"] = d.name;
    o["speed"] = d.speed;
    o["friction"] = d.friction;
    o["bodies"] = json::array();
    for (auto& b : d.bodies) {
        json jb = {{"id", b.id}, {"name", b.name}, {"fixed", b.fixed}, {"layer", b.layer},
                   {"thick", b.thick}, {"z", b.z}, {"color", b.color}, {"damp", b.damp}, {"ghost", b.ghost}};
        if (b.smooth > 0) jb["smooth"] = b.smooth;
        if (!b.weld.empty()) jb["weld"] = b.weld;
        if (depthShaped(b)) {
            json a = json::array();
            for (size_t i = 0; i < b.pts.size(); i++) a.push_back({b.depthOff[i].x, b.depthOff[i].y, b.depthZn[i], b.depthZf[i]});
            jb["depth"] = a;
        }
        if (!b.marks.empty()) {
            json ms = json::array();
            for (auto& m : b.marks) {
                json pts = json::array(), ns = json::array();
                for (size_t k = 0; k < m.pts.size(); k++) {
                    pts.push_back({m.pts[k].x, m.pts[k].y, m.pts[k].z});
                    P3d n = k < m.n.size() ? m.n[k] : P3d{0, 0, 1};
                    ns.push_back({n.x, n.y, n.z});
                }
                json jm = {{"pts", pts}, {"n", ns}, {"width", m.width}, {"color", m.color}};
                if (!m.text.empty()) { jm["text"] = m.text; jm["size"] = m.size; jm["right"] = {m.right.x, m.right.y, m.right.z}; }
                ms.push_back(jm);
            }
            jb["marks"] = ms;
        }
        if (!b.mirror.empty()) {
            jb["mirror"] = b.mirror; jb["mirrorKind"] = b.mirrorKind;
            if (b.mirrorKind == "line") { jb["mirrorA"] = v2j(b.mirrorA); jb["mirrorB"] = v2j(b.mirrorB); }
        }
        if (!b.texture.empty()) { jb["texture"] = b.texture; jb["texScale"] = b.texScale; }
        if (b.round) {
            jb["round"] = true;
            if (b.roundSides) jb["roundSides"] = b.roundSides;
            if (b.roundTurn != 0) jb["roundTurn"] = b.roundTurn;
            if (b.roundSweep < 360) jb["roundSweep"] = b.roundSweep;
            if (b.roundAxis) jb["roundAxis"] = b.roundAxis;
        }
        if (!b.carves.empty()) {
            json cs = json::array();
            for (auto& c : b.carves)
                cs.push_back({{"shape", c.shape}, {"at", {c.at.x, c.at.y, c.atZ}}, {"dir", {c.dx, c.dy, c.dz}},
                              {"spin", c.spin}, {"w", c.w}, {"h", c.h}, {"depth", c.depth}});
            jb["carves"] = cs;
        }
        if (b.mass > 0) jb["mass"] = b.mass;
        if (b.magazine) {
            jb["magazine"] = {{"cap", b.magCap}};
            if (b.lipHeight != 0) jb["magazine"]["lipHeight"] = b.lipHeight;
            if (b.lipRelease != 0.45) jb["magazine"]["lipRelease"] = b.lipRelease;
            if (b.magSpring != 1) jb["magazine"]["spring"] = b.magSpring;
            if (b.doubleFeed) jb["magazine"]["feed"] = "double";
            if (b.magFixed) jb["magazine"]["builtIn"] = true;
            if (b.roundStyle != "side") jb["magazine"]["style"] = b.roundStyle;
            if (b.roundStyle == "line") jb["magazine"]["tubeFeed"] = b.tubeFeed;
            if (!b.feedPawl.empty()) jb["magazine"]["pawl"] = {{"part", b.feedPawl}, {"stroke", b.pawlStroke}};
            if (b.clip != "none") jb["magazine"]["clip"] = {{"kind", b.clip}, {"size", b.clipSize}};
            if (!b.roundPath.empty()) { json a = json::array(); for (auto& q : b.roundPath) a.push_back(v2j(q)); jb["magazine"]["roundPath"] = a; }
            if (b.gen.on) jb["magazine"]["gen"] = {{"stack", b.gen.stack}, {"curve", b.gen.curve}, {"wall", b.gen.wall}, {"angle", b.gen.angle}, {"side", b.gen.side}, {"top", v2j(b.gen.top)}, {"H", b.gen.H}, {"W", b.gen.W}};
        }
        if (b.tiltX != 0 || b.tiltY != 0) jb["tilt"] = {b.tiltX, b.tiltY};
        if (b.barrel.on) jb["barrel"] = {{"cartridge", cartToJson(b.barrel.cart)}, {"rifled", b.barrel.rifled}, {"twist", b.barrel.twist}, {"grooves", b.barrel.grooves}, {"muzzleEnd", b.barrel.muzzleEnd}};
        if (b.rollOn) jb["roll"] = {{"on", true}, {"min", b.rollMin}, {"max", b.rollMax}, {"handle", b.handle}};
        if (b.ctrl != "none") jb["control"] = {{"kind", b.ctrl}, {"key", b.key}, {"at", v2j(b.ctrlPt)}, {"dir", b.ctrlDir}, {"force", b.ctrlForce}};
        json pts = json::array();
        for (auto& p : b.pts) pts.push_back(v2j(p));
        jb["pts"] = pts;
        bool anyR = false, anyB = false;
        for (double r : b.rad) if (r > 0) anyR = true;
        for (double s : b.bulge) if (s != 0) anyB = true;
        if (anyR) jb["rad"] = b.rad;
        if (anyB) jb["bulge"] = b.bulge;
        o["bodies"].push_back(jb);
    }
    o["joints"] = json::array();
    for (auto& j : d.joints) {
        json jj = {{"type", j.type}, {"a", j.a}, {"b", j.b}};
        if (j.type == "pin" || j.type == "weld") jj["p"] = v2j(j.p);
        if (j.type == "pin" && j.pinDia != 3.2) jj["dia"] = j.pinDia;
        if (j.type == "track" || j.type == "magwell") {
            jj["p"] = v2j(j.p);
            json p = json::array();
            for (auto& q : j.path) p.push_back(v2j(q));
            jj["path"] = p;
            jj["lockRot"] = j.lockRot;
            if (j.type == "magwell" && !j.rel.empty()) { jj["rel"] = j.rel; jj["releaseAt"] = j.releaseAt; }
        }
        if (j.type == "spring") {
            jj["pa"] = v2j(j.pa); jj["pb"] = v2j(j.pb); jj["k"] = j.k; jj["rest"] = j.rest; jj["damp"] = j.damp;
            jj["kind"] = j.kind;
            if (j.kind == "coil") { jj["guide"] = j.guide; jj["coilDia"] = j.coilDia; jj["wire"] = j.wire; jj["turns"] = j.turns; }
            else { jj["preload"] = j.preload; jj["flip"] = j.flip; }
        }
        if (j.type == "cam") { json t = json::array(); for (auto& q : j.table) t.push_back(v2j(q)); jj["table"] = t; }
        if (j.type == "lug") { jj["unlock"] = j.unlock; jj["blockOpen"] = j.blockOpen; }
        if (j.type == "magwell") { bool any = false; for (double a : j.pathAng) if (a != 0) any = true; if (any) jj["pathAng"] = j.pathAng; }
        if (j.type == "latch") { jj["catchAt"] = j.catchAt; jj["rel"] = j.rel; jj["releaseAt"] = j.releaseAt; if (j.holdShut) jj["holdShut"] = true; }
        o["joints"].push_back(jj);
    }
    if (d.breech.on) {
        auto& r = d.breech;
        json jb = {{"breech", r.breech}, {"chamber", r.chamber}, {"impulse", r.impulse}, {"dur", r.dur}, {"feed", r.feed},
                   {"rounds", r.rounds}, {"auto", r.autoFire}, {"loaded", r.loaded}, {"round", {r.roundLen, r.roundDia}},
                   {"cartridge", cartToJson(r.cart)}, {"cartImpulse", r.cartImpulse}, {"feedMode", r.feedMode}, {"physicalFeed", r.physicalFeed},
                   {"ignition", r.ignition}, {"extractor", r.extractor}, {"striker", r.striker}, {"strikerTip", v2j(r.strikerTip)}, {"igniteSpeed", r.igniteSpeed},
                   {"hammer", r.hammer}, {"cockAt", r.cockAt}, {"fireMode", r.fireMode}, {"burst", r.burst},
                   {"doubleAction", r.doubleAction}, {"lockTime", r.lockTime}};
        jb["auto"] = r.fullAuto();
        jb["face"] = r.hasFace ? v2j(r.face) : json(nullptr);
        jb["muzzle"] = r.hasMuzzle ? v2j(r.muzzle) : json(nullptr);
        if (!r.more.empty()) {
            json a = json::array();
            for (auto& c : r.more)
            {
                json jc = {{"breech", c.breech}, {"chamber", c.chamber}, {"face", v2j(c.face)}, {"muzzle", c.hasMuzzle ? v2j(c.muzzle) : json(nullptr)}, {"loaded", c.loaded}};
                if (c.ownCart) jc["cartridge"] = cartToJson(c.cart);
                a.push_back(jc);
            }
            jb["more"] = a;
            jb["fireOrder"] = r.fireOrder;
        }
        o["breech"] = jb;
    } else o["breech"] = nullptr;
    {
        json P = json::object();
        auto hand = [](const HandHold& h) {
            json o{{"part", h.part}, {"at", v2j(h.at)}, {"angle", h.angle}};
            if (h.digitsSet) {
                json a = json::array();
                for (auto& d : h.digits) a.push_back({d.bend[0], d.bend[1], d.bend[2], d.spread, d.oppose});
                o["digits"] = a;
            }
            return o;
        };
        if (d.pose.firing.on) P["firing"] = hand(d.pose.firing);
        if (d.pose.support.on) P["support"] = hand(d.pose.support);
        if (d.pose.butt.on) P["butt"] = hand(d.pose.butt);
        for (int h = 0; h < HOLD_COUNT; h++) {
            const BodyHold& b = d.pose.hold[h];
            if (b.set) P[holdKey(h)] = {{"eye", v2j(b.eye)}, {"eyeZ", b.eyeZ}, {"yaw", b.yaw}, {"tilt", b.tilt}};
        }
        if (!P.empty()) o["pose"] = P;
        json An = json::object();
        for (auto& [slot, a] : d.anims) {
            if (a.keys.empty()) continue;
            json ks = json::array();
            for (auto& k : a.keys) {
                json jk{{"t", k.t}};
                if (k.firing.on) jk["firing"] = hand(k.firing);
                if (k.support.on) jk["support"] = hand(k.support);
                if (k.gripF) jk["gripF"] = true;
                if (k.gripS) jk["gripS"] = true;
                if (!k.event.empty()) jk["event"] = k.event;
                if (k.turn >= 0) jk["turn"] = k.turn;
                ks.push_back(jk);
            }
            An[slot] = ks;
        }
        if (!An.empty()) o["anims"] = An;
    }
    return pretty ? o.dump(2) : o.dump();
}

std::string blueprintVersionNote(const Design& d) {
    if (d.format <= GUNSMITH_BLUEPRINT_FORMAT) return "";
    return "This blueprint was saved by " + (d.savedWith.empty() ? std::string("a newer Gunsmith3D") : d.savedWith) +
           ", which is newer than this one (" + version::text() + "). Some of it may not load correctly.";
}

bool designFromJson(const std::string& text, Design& out, std::string* err) {
    json o;
    try { o = json::parse(text); } catch (std::exception& e) { if (err) *err = e.what(); return false; }
    if (!o.is_object() || !o.contains("bodies") || !o["bodies"].is_array() || !o.contains("joints") || !o["joints"].is_array()) {
        if (err) *err = "missing bodies or joints";
        return false;
    }
    Design d;
    d.format = opt<int>(o, "format", 1);
    d.savedWith = opt<std::string>(o, "savedWith", "");
    d.name = opt<std::string>(o, "name", "Untitled action");
    d.speed = opt<double>(o, "speed", 0.02);
    d.friction = opt<double>(o, "friction", 0.12);
    int n = 0;
    for (auto& jb : o["bodies"]) {
        BodyDef b;
        b.id = opt<std::string>(jb, "id", "b" + std::to_string(n++));
        b.name = opt<std::string>(jb, "name", b.id);
        b.fixed = opt<bool>(jb, "fixed", false);
        b.layer = opt<std::string>(jb, "layer", "none");
        b.z = opt<double>(jb, "z", 0.0);
        b.thick = opt<double>(jb, "thick", 10);
        b.mass = opt<double>(jb, "mass", 0);
        b.damp = opt<double>(jb, "damp", 0);
        b.color = opt<std::string>(jb, "color", "#8e9ab4");
        b.ghost = opt<bool>(jb, "ghost", b.fixed);
        b.smooth = std::clamp(opt<double>(jb, "smooth", 0.0), 0.0, 1.0);
        b.weld = opt<std::string>(jb, "weld", "");
        b.mirror = opt<std::string>(jb, "mirror", "");
        b.mirrorKind = opt<std::string>(jb, "mirrorKind", "depth");
        if (jb.contains("mirrorA")) b.mirrorA = j2v(jb["mirrorA"]);
        if (jb.contains("mirrorB")) b.mirrorB = j2v(jb["mirrorB"]);
        b.round = opt<bool>(jb, "round", false);
        b.texture = opt<std::string>(jb, "texture", "");
        b.texScale = std::clamp(opt<double>(jb, "texScale", 40.0), 1.0, 5000.0);
        b.roundSides = std::clamp(opt<int>(jb, "roundSides", 0), 0, 64);
        if (b.roundSides > 0 && b.roundSides < 3) b.roundSides = 3;
        b.roundTurn = opt<double>(jb, "roundTurn", 0.0);
        b.roundSweep = std::clamp(opt<double>(jb, "roundSweep", 360.0), 1.0, 360.0);
        b.roundAxis = std::clamp(opt<int>(jb, "roundAxis", 0), 0, 2);
        if (jb.contains("carves") && jb["carves"].is_array())
            for (auto& jc : jb["carves"]) {
                if (!jc.is_object()) continue;
                CarveDef c;
                c.shape = opt<std::string>(jc, "shape", "circle");
                if (jc.contains("at") && jc["at"].is_array() && jc["at"].size() == 3) { c.at = {jc["at"][0].get<double>(), jc["at"][1].get<double>()}; c.atZ = jc["at"][2].get<double>(); }
                if (jc.contains("dir") && jc["dir"].is_array() && jc["dir"].size() == 3) { c.dx = jc["dir"][0].get<double>(); c.dy = jc["dir"][1].get<double>(); c.dz = jc["dir"][2].get<double>(); }
                c.spin = opt<double>(jc, "spin", 0.0);
                c.w = std::max(0.2, opt<double>(jc, "w", 6.0));
                c.h = std::max(0.2, opt<double>(jc, "h", 6.0));
                c.depth = std::max(0.0, opt<double>(jc, "depth", 4.0));
                b.carves.push_back(c);
            }
        if (jb.contains("roll") && jb["roll"].is_object()) {
            auto& r = jb["roll"];
            b.rollOn = opt<bool>(r, "on", false);
            b.rollMin = opt<double>(r, "min", 0);
            b.rollMax = opt<double>(r, "max", 90);
            b.handle = opt<double>(r, "handle", 0);
        }
        if (jb.contains("barrel") && jb["barrel"].is_object()) {
            auto& jr = jb["barrel"];
            b.barrel.on = true;
            if (jr.contains("cartridge") && jr["cartridge"].is_object()) b.barrel.cart = cartFromJson(jr["cartridge"]);
            b.barrel.rifled = opt<bool>(jr, "rifled", true);
            b.barrel.twist = std::clamp(opt<double>(jr, "twist", 254.0), 50.0, 2000.0);
            b.barrel.grooves = std::clamp(opt<int>(jr, "grooves", 6), 2, 16);
            b.barrel.muzzleEnd = opt<int>(jr, "muzzleEnd", 1) < 0 ? -1 : 1;
        }
        if (jb.contains("tilt") && jb["tilt"].is_array() && jb["tilt"].size() >= 2) { b.tiltX = jb["tilt"][0].get<double>(); b.tiltY = jb["tilt"][1].get<double>(); }
        if (jb.contains("magazine") && jb["magazine"].is_object()) {
            auto& m = jb["magazine"];
            b.magazine = true; b.magCap = opt<int>(m, "cap", 10);
            b.lipHeight = std::clamp(opt<double>(m, "lipHeight", 0.0), -5.0, 5.0);
            b.lipRelease = std::clamp(opt<double>(m, "lipRelease", 0.45), 0.1, 0.95);
            b.magSpring = std::clamp(opt<double>(m, "spring", 1.0), 0.1, 3.0);
            b.doubleFeed = opt<std::string>(m, "feed", "single") == "double";
            b.magFixed = opt<bool>(m, "builtIn", false);
            b.roundStyle = opt<std::string>(m, "style", "side") == "line" ? "line" : "side";
            b.tubeFeed = opt<std::string>(m, "tubeFeed", "simple") == "physical" ? "physical" : "simple";  // older tubes: the simple way
            if (m.contains("pawl") && m["pawl"].is_object()) {
                b.feedPawl = opt<std::string>(m["pawl"], "part", "");
                b.pawlStroke = std::clamp(opt<double>(m["pawl"], "stroke", 10.0), 0.5, 500.0);
            }
            if (m.contains("clip") && m["clip"].is_object()) {
                std::string k = opt<std::string>(m["clip"], "kind", "none");
                b.clip = k == "stripper" || k == "enbloc" ? k : "none";
                b.clipSize = std::clamp(opt<int>(m["clip"], "size", 5), 1, 200);
            }
            if (m.contains("roundPath") && m["roundPath"].is_array()) for (auto& q : m["roundPath"]) b.roundPath.push_back(j2v(q));
            if (m.contains("gen") && m["gen"].is_object()) {
                auto& g = m["gen"];
                b.gen.on = true; b.gen.stack = opt<int>(g, "stack", 2); b.gen.curve = opt<double>(g, "curve", 0);
                b.gen.wall = opt<double>(g, "wall", 1.2); b.gen.angle = opt<double>(g, "angle", 0); b.gen.side = opt<int>(g, "side", 0);
                if (g.contains("top")) b.gen.top = j2v(g["top"]);
                b.gen.H = opt<double>(g, "H", 0); b.gen.W = opt<double>(g, "W", 0);
            }
        }
        if (jb.contains("control") && jb["control"].is_object()) {
            auto& c = jb["control"];
            b.ctrl = opt<std::string>(c, "kind", "button"); b.key = opt<std::string>(c, "key", "none");
            if (c.contains("at")) b.ctrlPt = j2v(c["at"]);
            b.ctrlDir = opt<double>(c, "dir", 180); b.ctrlForce = opt<double>(c, "force", 25);
        }
        if (jb.contains("pts")) for (auto& p : jb["pts"]) b.pts.push_back(j2v(p));
        if (jb.contains("depth") && jb["depth"].is_array() && jb["depth"].size() == b.pts.size()) {
            for (auto& q : jb["depth"]) {
                if (!q.is_array() || q.size() < 4) { b.depthOff.clear(); b.depthZn.clear(); b.depthZf.clear(); break; }
                b.depthOff.push_back({q[0].get<double>(), q[1].get<double>()});
                b.depthZn.push_back(q[2].get<double>()); b.depthZf.push_back(q[3].get<double>());
            }
            b.depthRef = b.pts;
        }
        if (jb.contains("rad") && jb["rad"].is_array()) for (auto& r : jb["rad"]) b.rad.push_back(r.get<double>());
        if (jb.contains("bulge") && jb["bulge"].is_array()) for (auto& r : jb["bulge"]) b.bulge.push_back(r.get<double>());
        if (jb.contains("marks") && jb["marks"].is_array())
            for (auto& jm : jb["marks"]) {
                if (!jm.is_object() || !jm.contains("pts") || !jm["pts"].is_array()) continue;
                MarkDef m;
                auto p3 = [](const json& q) { return q.is_array() && q.size() >= 3 ? P3d{q[0].get<double>(), q[1].get<double>(), q[2].get<double>()} : P3d{0, 0, 0}; };
                for (auto& q : jm["pts"]) m.pts.push_back(p3(q));
                if (jm.contains("n") && jm["n"].is_array()) for (auto& q : jm["n"]) m.n.push_back(p3(q));
                m.n.resize(m.pts.size(), P3d{0, 0, 1});
                m.width = opt<double>(jm, "width", 0.4); m.color = opt<std::string>(jm, "color", "#dcd8d0");
                m.text = opt<std::string>(jm, "text", ""); m.size = opt<double>(jm, "size", 4);
                if (jm.contains("right")) m.right = p3(jm["right"]);
                if (!m.pts.empty()) b.marks.push_back(m);
            }
        fixShape(b);
        b.markRef = b.pts; b.markRefZ = b.z; b.markRefThick = b.thick;
        if (b.pts.size() >= 3) d.bodies.push_back(b);
    }
    for (auto& jj : o["joints"]) {
        JointDef j;
        j.type = opt<std::string>(jj, "type", "");
        j.a = opt<std::string>(jj, "a", "");
        j.b = opt<std::string>(jj, "b", "world");
        if (jj.contains("p")) j.p = j2v(jj["p"]);
        if (jj.contains("pa")) j.pa = j2v(jj["pa"]);
        if (jj.contains("pb")) j.pb = j2v(jj["pb"]);
        if (jj.contains("path")) for (auto& p : jj["path"]) j.path.push_back(j2v(p));
        if (jj.contains("table")) for (auto& p : jj["table"]) j.table.push_back(j2v(p));
        j.lockRot = opt<bool>(jj, "lockRot", false);
        j.pinDia = std::clamp(opt<double>(jj, "dia", 3.2), 0.3, 40.0);
        j.k = opt<double>(jj, "k", 1);
        j.rest = opt<double>(jj, "rest", 0);
        j.damp = opt<double>(jj, "damp", 0);
        j.kind = opt<std::string>(jj, "kind", "coil");
        j.guide = opt<std::string>(jj, "guide", "");
        j.coilDia = opt<double>(jj, "coilDia", 8);
        j.wire = opt<double>(jj, "wire", 1);
        j.turns = opt<double>(jj, "turns", 12);
        j.preload = opt<double>(jj, "preload", 1);
        j.flip = opt<bool>(jj, "flip", false);
        j.unlock = opt<double>(jj, "unlock", 45);
        j.blockOpen = opt<bool>(jj, "blockOpen", false);
        j.catchAt = opt<double>(jj, "catchAt", -30);
        j.rel = opt<std::string>(jj, "rel", "");
        j.releaseAt = opt<double>(jj, "releaseAt", j.type == "magwell" ? 2.0 : -2.0);
        j.holdShut = opt<bool>(jj, "holdShut", false);
        if (jj.contains("pathAng") && jj["pathAng"].is_array()) for (auto& a : jj["pathAng"]) if (a.is_number()) j.pathAng.push_back(a.get<double>());
        static const char* ok[] = {"pin", "weld", "track", "spring", "cam", "lug", "latch", "magwell"};
        bool known = false;
        for (auto k : ok) if (j.type == k) known = true;
        if (known) d.joints.push_back(j);
    }
    weldJointsToTags(d);
    if (o.contains("breech") && o["breech"].is_object()) {
        auto& jb = o["breech"];
        auto& r = d.breech;
        r.on = true;
        r.breech = opt<std::string>(jb, "breech", "");
        r.chamber = opt<std::string>(jb, "chamber", "world");
        if (jb.contains("face") && jb["face"].is_array()) { r.hasFace = true; r.face = j2v(jb["face"]); }
        if (jb.contains("muzzle") && jb["muzzle"].is_array()) { r.hasMuzzle = true; r.muzzle = j2v(jb["muzzle"]); }
        r.impulse = opt<double>(jb, "impulse", 3);
        r.dur = opt<double>(jb, "dur", 1);
        r.feed = opt<double>(jb, "feed", 20);
        r.rounds = opt<int>(jb, "rounds", 10);
        r.autoFire = opt<bool>(jb, "auto", false);
        r.loaded = opt<bool>(jb, "loaded", true);
        if (jb.contains("round") && jb["round"].is_array() && jb["round"].size() >= 2) {
            r.roundLen = jb["round"][0].get<double>();
            r.roundDia = jb["round"][1].get<double>();
        }
        bool hasWell = false;
        for (auto& j : d.joints) if (j.type == "magwell") hasWell = true;
        if (jb.contains("cartridge") && jb["cartridge"].is_object()) {
            r.cart = cartFromJson(jb["cartridge"]);
            r.cartImpulse = opt<bool>(jb, "cartImpulse", true);
        } else if (jb.contains("cartridge") && jb["cartridge"].is_string()) {  // by preset name
            std::string nm = jb["cartridge"].get<std::string>();
            for (auto& c : cartridgePresets()) if (c.name == nm) r.cart = c;
            r.cartImpulse = opt<bool>(jb, "cartImpulse", true);
        } else {  // older blueprints: keep their own impulse and round size
            r.cartImpulse = false;
            r.cart.oal = r.roundLen; r.cart.rimDia = r.cart.baseDia = r.roundDia;
            r.cart.name = "Custom";
        }
        r.feedMode = opt<std::string>(jb, "feedMode", hasWell ? "magazine" : "internal");
        r.physicalFeed = opt<bool>(jb, "physicalFeed", false);
        r.ignition = opt<std::string>(jb, "ignition", "button");
        r.extractor = opt<std::string>(jb, "extractor", "auto");
        r.striker = opt<std::string>(jb, "striker", "");
        if (jb.contains("strikerTip")) r.strikerTip = j2v(jb["strikerTip"]);
        r.igniteSpeed = opt<double>(jb, "igniteSpeed", 1.5);
        r.hammer = opt<std::string>(jb, "hammer", "striker");
        r.cockAt = opt<double>(jb, "cockAt", 0);   // older blueprints: always cocked, as before
        r.fireMode = opt<std::string>(jb, "fireMode", r.autoFire ? "auto" : "semi");
        if (r.fireMode != "semi" && r.fireMode != "auto" && r.fireMode != "burst") r.fireMode = "semi";
        r.autoFire = r.fireMode == "auto";
        r.burst = std::max(2, opt<int>(jb, "burst", 3));
        r.doubleAction = opt<bool>(jb, "doubleAction", false);
        r.lockTime = std::max(0.0, opt<double>(jb, "lockTime", 0));
        if (jb.contains("more") && jb["more"].is_array())
            for (auto& jc : jb["more"]) {
                if (!jc.is_object() || !jc.contains("face") || !jc["face"].is_array()) continue;
                ChamberDef c;
                c.breech = opt<std::string>(jc, "breech", r.breech); c.chamber = opt<std::string>(jc, "chamber", r.chamber);
                c.face = j2v(jc["face"]);
                if (jc.contains("muzzle") && jc["muzzle"].is_array()) { c.hasMuzzle = true; c.muzzle = j2v(jc["muzzle"]); }
                c.loaded = opt<bool>(jc, "loaded", true);
                if (jc.contains("cartridge") && jc["cartridge"].is_object()) { c.ownCart = true; c.cart = cartFromJson(jc["cartridge"]); }
                r.more.push_back(c);
            }
        r.fireOrder = opt<std::string>(jb, "fireOrder", "turn") == "together" ? "together" : "turn";
        if (r.cartImpulse) applyCartridge(r);
    }
    auto readHand = [&](const json& jh, HandHold& h, const char* which) {
        if (!jh.is_object() || !jh.contains("at") || !jh["at"].is_array()) return;
        h.on = true; h.part = opt<std::string>(jh, "part", ""); h.at = j2v(jh["at"]);
        h.angle = opt<double>(jh, "angle", defaultHandAngle(which));
        if (jh.contains("digits") && jh["digits"].is_array() && jh["digits"].size() == DIGIT_COUNT) {
            h.digitsSet = true;
            for (int k = 0; k < DIGIT_COUNT; k++) {
                auto& q = jh["digits"][k];
                if (!q.is_array() || q.size() < 4) { h.digitsSet = false; break; }
                for (int s = 0; s < 3; s++) h.digits[k].bend[s] = q[s].get<double>();
                h.digits[k].spread = q[3].get<double>();
                h.digits[k].oppose = q.size() > 4 ? q[4].get<double>() : 0.0;
            }
        }
    };
    if (o.contains("pose") && o["pose"].is_object()) {
        auto& P = o["pose"];
        if (P.contains("firing")) readHand(P["firing"], d.pose.firing, "firing");
        if (P.contains("support")) readHand(P["support"], d.pose.support, "support");
        if (P.contains("butt")) readHand(P["butt"], d.pose.butt, "butt");
        for (int h = 0; h < HOLD_COUNT; h++) {
            if (!P.contains(holdKey(h)) || !P[holdKey(h)].is_object()) continue;
            auto& jh = P[holdKey(h)];
            BodyHold& b = d.pose.hold[h];
            b.set = true;
            if (jh.contains("eye")) b.eye = j2v(jh["eye"]);
            b.eyeZ = opt<double>(jh, "eyeZ", 0.0); b.yaw = opt<double>(jh, "yaw", 0.0); b.tilt = opt<double>(jh, "tilt", 0.0);
        }
    }
    if (o.contains("anims") && o["anims"].is_object())
        for (auto& [slot, ks] : o["anims"].items()) {
            if (!ks.is_array()) continue;
            Animation a;
            for (auto& jk : ks) {
                if (!jk.is_object()) continue;
                AnimKey k;
                k.t = std::max(0.0, opt<double>(jk, "t", 0.0));
                if (jk.contains("firing")) readHand(jk["firing"], k.firing, "firing");
                if (jk.contains("support")) readHand(jk["support"], k.support, "support");
                k.gripF = opt<bool>(jk, "gripF", false); k.gripS = opt<bool>(jk, "gripS", false);
                k.event = opt<std::string>(jk, "event", "");
                k.turn = opt<double>(jk, "turn", -1.0);
                a.keys.push_back(k);
            }
            std::stable_sort(a.keys.begin(), a.keys.end(), [](const AnimKey& x, const AnimKey& y) { return x.t < y.t; });
            if (!a.keys.empty()) d.anims[slot] = a;
        }
    if (!(o.contains("pose") && o["pose"].is_object()) && o.contains("grips") && o["grips"].is_array()) {  // grip markers (dev22-23): the first of each kind
        for (auto& jg : o["grips"]) {
            if (!jg.is_object()) continue;
            std::string k = opt<std::string>(jg, "kind", "firing");
            HandHold& h = k == "support" ? d.pose.support : k == "shoulder" ? d.pose.butt : d.pose.firing;
            if (!h.on) readHand(jg, h, k == "shoulder" ? "butt" : k.c_str());
        }
    }
    out = d;
    return true;
}

double groundLevelMM(const Design& d) {
    double y1 = 60, extra = 0;
    for (auto& b : d.bodies) for (auto& p : outlineOf(b)) y1 = std::max(y1, p.y);
    for (auto& j : d.joints) {
        if (j.type != "magwell" || j.path.size() < 2) continue;
        for (auto& b : d.bodies) {
            if (b.id != j.a) continue;
            double ymax = -1e9;
            for (auto& p : outlineOf(b)) ymax = std::max(ymax, p.y);
            double dropBottom = ymax + (j.path.back().y - j.path.front().y);
            extra = std::max(extra, dropBottom - y1);
        }
    }
    return y1 + std::max(0.0, extra) + 45;
}

// ---------------------------------------------------------------- presets (schematic, not copies of real parts)
#include "presets.inc"

const std::vector<PresetInfo>& presetList() {
    static std::vector<PresetInfo> v = [] {
        std::vector<PresetInfo> l;
        for (auto& p : PRESETS) if (p.menu) l.push_back({p.key, p.label});
        return l;
    }();
    return v;
}

Design loadPreset(const std::string& key) {
    const char* src = PRESET_BLANK;
    for (auto& p : PRESETS) if (key == p.key) src = p.json;
    Design d;
    designFromJson(src, d);
    for (size_t i = 0; i < d.bodies.size(); i++)
        if (d.bodies[i].magazine && d.bodies[i].gen.on) generateMagazine(d, (int)i);
    return d;
}

double bucklingSqueeze(double L0, double coilDia, double wire) {
    const double g = 0.385;   // shear over Young's modulus, spring steel
    const double nu = 0.5;    // seating: both ends flat against their parts
    double D = std::max(0.1, coilDia - wire);
    if (L0 <= 0) return 1e9;
    double t = (1 - g) / (0.5 + g) * std::pow(PI_ * D / (nu * L0), 2);
    if (t >= 1) return 1e9;   // short and stout: stable however far it's squeezed
    return L0 * 0.5 / (1 - g) * (1 - std::sqrt(1 - t));
}
