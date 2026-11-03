// Gunsmith3D - recorded hand animations (see AnimKey in design.hpp): the slots, and where the hands are at a moment.
// The hands start in the aiming hold, pass smoothly through the keys (a curve through the points, not straight
// lines between them) and come back to the hold at the end.
#include "design.hpp"
#include "hand.hpp"
#include <algorithm>
#include <cmath>

std::vector<std::string> animSlots(const Design& d) {
    std::vector<std::string> s = {"inspect", "reload", "reload_empty", "clear_jam"};
    for (auto& b : d.bodies)
        if (b.ctrl != "none" && b.ctrl != "trigger" && b.key != "fire") s.push_back("control:" + b.id);
    return s;
}
std::string animSlotName(const Design& d, const std::string& slot) {
    if (slot == "inspect") return "Inspect";
    if (slot == "reload") return "Reload";
    if (slot == "reload_empty") return "Reload (empty)";
    if (slot == "clear_jam") return "Clear a jam";
    if (slot.rfind("control:", 0) == 0) {
        int i = d.bodyIndex(slot.substr(8));
        return "Work: " + (i >= 0 ? d.bodies[i].name : std::string("a removed control"));
    }
    return slot;
}

static const double BACK = 0.4;   // seconds to return to the hold after the last key
double animLength(const Animation& a) { return a.keys.empty() ? 0 : a.keys.back().t + BACK; }

namespace {
struct Pt { double t; const HandHold* h; };
double smooth(double u) { return u * u * (3 - 2 * u); }
// a curve through the points (Catmull-Rom with uneven spacing): it passes every key, without corners
double curve(double t, const std::vector<Pt>& P, size_t i, double (*get)(const HandHold&)) {
    auto at = [&](size_t k) { return get(*P[k].h); };
    double t0 = P[i].t, t1 = P[i + 1].t, h = std::max(1e-6, t1 - t0), u = std::clamp((t - t0) / h, 0.0, 1.0);
    double p0 = at(i), p1 = at(i + 1);
    double m0 = i > 0 ? (p1 - at(i - 1)) / std::max(1e-6, t1 - P[i - 1].t) * h : 0;
    double m1 = i + 2 < P.size() ? (at(i + 2) - p0) / std::max(1e-6, P[i + 2].t - t0) * h : 0;
    double u2 = u * u, u3 = u2 * u;
    return (2 * u3 - 3 * u2 + 1) * p0 + (u3 - 2 * u2 + u) * m0 + (-2 * u3 + 3 * u2) * p1 + (u3 - u2) * m1;
}
HandHold handAt(const HandHold& base, const std::vector<AnimKey>& keys, bool firing, double t, double end) {
    // the points this hand passes: the hold, every key where the hand is on the gun, the hold again
    std::vector<HandHold> holds;
    std::vector<double> times;
    holds.push_back(base); times.push_back(0);
    for (auto& k : keys) { holds.push_back(firing ? k.firing : k.support); times.push_back(k.t); }
    holds.push_back(base); times.push_back(end);
    size_t i = 0;
    while (i + 2 < holds.size() && t >= times[i + 1]) i++;
    const HandHold &a = holds[i], &b = holds[i + 1];
    if (!a.on || !b.on) return t - times[i] < times[i + 1] - t ? a : b;   // off the gun: it lets go or takes hold at the nearer key
    // the run of keys around this one where the hand stays on the gun: the curve goes through them
    size_t lo = i, hi = i + 1;
    while (lo > 0 && holds[lo - 1].on) lo--;
    while (hi + 1 < holds.size() && holds[hi + 1].on) hi++;
    std::vector<Pt> P;
    for (size_t k = lo; k <= hi; k++) P.push_back({times[k], &holds[k]});
    size_t j = i - lo;
    HandHold out = a;
    out.at.x = curve(t, P, j, [](const HandHold& h) { return h.at.x; });
    out.at.y = curve(t, P, j, [](const HandHold& h) { return h.at.y; });
    double u = smooth(std::clamp((t - times[i]) / std::max(1e-6, times[i + 1] - times[i]), 0.0, 1.0));
    out.angle = a.angle + std::remainder(b.angle - a.angle, 360.0) * u;
    if (a.digitsSet || b.digitsSet) {   // the fingers: from one key's to the next's
        DigitPose da[DIGIT_COUNT], db[DIGIT_COUNT];
        digitsOf(a, firing ? "firing" : "support", da);
        digitsOf(b, firing ? "firing" : "support", db);
        out.digitsSet = true;
        for (int d = 0; d < DIGIT_COUNT; d++) {
            for (int s = 0; s < 3; s++) out.digits[d].bend[s] = da[d].bend[s] + (db[d].bend[s] - da[d].bend[s]) * u;
            out.digits[d].spread = da[d].spread + (db[d].spread - da[d].spread) * u;
        }
    }
    if (u > 0.5) out.part = b.part;
    return out;
}
}  // namespace

AnimHands animAt(const Design& d, const Animation& a, double t) {
    AnimHands o;
    o.firing = d.pose.firing; o.support = d.pose.support;
    if (a.keys.empty()) return o;
    double end = animLength(a);
    t = std::clamp(t, 0.0, end);
    o.firing = handAt(d.pose.firing, a.keys, true, t, end);
    o.support = handAt(d.pose.support, a.keys, false, t, end);
    for (auto& k : a.keys) if (k.t <= t + 1e-9) { o.gripF = k.gripF; o.gripS = k.gripS; }   // the latest key so far says
    if (t > a.keys.back().t + 1e-9) o.gripF = o.gripS = false;                               // (going back to the hold: let go)
    return o;
}
