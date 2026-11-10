// Gunsmith3D - Display units (see units.hpp).
#include "app/units.hpp"
#include "app/settings.hpp"

#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace {
struct UnitDef { const char* metric; const char* imperial; double toImperial; int decM, decI; };
const UnitDef UNITS[] = {
    {"mm", "in", 1 / 25.4, 1, 3},          // Length
    {"g", "gr", 15.4323584, 2, 1},          // BulletMass (powder too)
    {"g", "oz", 1 / 28.3495231, 0, 2},      // PartMass
    {"m/s", "ft/s", 3.2808399, 0, 0},       // Velocity
    {"J", "ft\xC2\xB7lbf", 0.737562149, 0, 0},  // Energy
    {"N", "lbf", 0.224808943, 0, 1},        // Force
    {"N/mm", "lb/in", 5.71014715, 2, 1},    // SpringRate
    {"MPa", "psi", 145.037738, 0, 0},       // Pressure
    {"m", "yd", 1.0936133, 0, 0},           // Distance
    {"N\xC2\xB7s", "lbf\xC2\xB7s", 0.224808943, 2, 3},  // Impulse
    {"N\xC2\xB7s/m", "lbf\xC2\xB7s/ft", 0.0685217659, 1, 2},  // Damping
};
bool imperial() { return CFG.units == Units::Imperial; }
}

double toDisp(Q q, double v) { return imperial() ? v * UNITS[(int)q].toImperial : v; }
double fromDisp(Q q, double v) { return imperial() ? v / UNITS[(int)q].toImperial : v; }
const char* unitOf(Q q) { return imperial() ? UNITS[(int)q].imperial : UNITS[(int)q].metric; }
int decimalsOf(Q q) { return imperial() ? UNITS[(int)q].decI : UNITS[(int)q].decM; }
std::string fmtQ(Q q, double v, int dec) {
    char buf[64];
    if (q == Q::PartMass && std::fabs(v) >= 1000) {   // heavy: kg or lb
        if (imperial()) std::snprintf(buf, sizeof buf, "%.2f lb", v / 453.59237);
        else std::snprintf(buf, sizeof buf, "%.2f kg", v / 1000);
        return buf;
    }
    std::snprintf(buf, sizeof buf, "%.*f %s", dec >= 0 ? dec : decimalsOf(q), toDisp(q, v), unitOf(q));
    return buf;
}
std::string fracInch(double mm) {
    double in = std::fabs(mm) / 25.4;
    long n = std::lround(in * 32), whole = n / 32, r = n % 32;
    int den = 32;
    while (r && r % 2 == 0) { r /= 2; den /= 2; }
    char b[80];
    std::string sgs = std::string(std::fabs(in * 32 - std::round(in * 32)) > 0.01 ? "~" : "") + (mm < 0 && n ? "-" : "");   // ≈ when rounded
    const char* sg = sgs.c_str();
    if (!r) std::snprintf(b, sizeof b, "%s%ld\"", sg, whole);
    else if (!whole) std::snprintf(b, sizeof b, "%s%ld/%d\"", sg, r, den);
    else std::snprintf(b, sizeof b, "%s%ld %ld/%d\"", sg, whole, r, den);
    return b;
}
double niceStep(Q q, double st) {
    double s = toDisp(q, st);
    if (s <= 0) return 0;
    double p = std::pow(10.0, std::floor(std::log10(s))), m = s / p;
    return (m < 1.5 ? 1 : m < 3.5 ? 2 : m < 7.5 ? 5 : 10) * p;
}

namespace {
// a unit name -> how many stored units one of it is (0: not this quantity)
double unitScale(Q q, std::string u) {
    for (auto& c : u) c = (char)std::tolower((unsigned char)c);
    if (q == Q::Length) {
        if (u == "mm" || u == "millimeter" || u == "millimeters" || u == "millimetre" || u == "millimetres") return 1;
        if (u == "cm") return 10;
        if (u == "m") return 1000;
        if (u == "in" || u == "inch" || u == "inches" || u == "\"" || u == "''") return 25.4;
        if (u == "thou" || u == "mil" || u == "mils") return 0.0254;
        if (u == "ft" || u == "foot" || u == "feet" || u == "'") return 304.8;
        return 0;
    }
    if (q == Q::Distance) {
        if (u == "m") return 1;
        if (u == "yd" || u == "yds" || u == "yard" || u == "yards") return 0.9144;
        if (u == "ft" || u == "'") return 0.3048;
        if (u == "km") return 1000;
        return 0;
    }
    const UnitDef& d = UNITS[(int)q];
    auto norm = [](std::string s) { std::string o; for (char c : s) if (c != ' ') o += (char)std::tolower((unsigned char)c);
        size_t k; while ((k = o.find("\xC2\xB7")) != std::string::npos) o.erase(k, 2);
        return o; };
    if (u == norm(d.metric)) return 1;
    if (u == norm(d.imperial)) return 1 / d.toImperial;
    if (q == Q::BulletMass || q == Q::PartMass) {
        if (u == "g" || u == "gram" || u == "grams") return 1;
        if (u == "gr" || u == "grain" || u == "grains") return 1 / 15.4323584;
        if (u == "oz") return 28.3495231;
        if (u == "kg") return 1000;
        if (u == "lb" || u == "lbs") return 453.59237;
    }
    if (q == Q::Velocity) { if (u == "fps" || u == "ft/s") return 0.3048; if (u == "m/s" || u == "mps") return 1; }
    if (q == Q::Energy) { if (u == "ftlb" || u == "ftlbs" || u == "ft-lb" || u == "ft-lbf" || u == "ft-lbs") return 1 / 0.737562149; }
    return 0;
}
}

bool parseQ(Q q, const char* text, double& out) {
    const char* p = text;
    auto ws = [&] { while (*p == ' ' || *p == '\t') p++; };
    auto num = [&](double& v) {   // a number, a fraction, or a whole number and a fraction
        char* e;
        double a = std::strtod(p, &e);
        if (e == p) return false;
        p = e;
        if (*p == '/') {
            double b = std::strtod(p + 1, &e);
            if (e == p + 1 || b == 0) return false;
            p = e; v = a / b; return true;
        }
        const char* save = p;
        ws();
        if (std::isdigit((unsigned char)*p)) {   // "1 1/2"
            double n = std::strtod(p, &e);
            if (*e == '/') {
                double b = std::strtod(e + 1, &e);
                if (b != 0) { p = e; v = a + n / b; return true; }
            }
        }
        p = save; v = a; return true;
    };
    double total = 0, sign = 1;
    int terms = 0;
    ws();
    while (*p) {
        if (*p == '+') { p++; ws(); sign = 1; continue; }
        if (*p == '-' && terms > 0) { p++; ws(); sign = -1; continue; }
        double v;
        bool neg = false;
        if (*p == '-') { neg = true; p++; }
        if (!num(v)) return false;
        if (neg) v = -v;
        ws();
        std::string u;
        while (*p && (std::isalpha((unsigned char)*p) || *p == '"' || *p == '\'' || *p == '/' || *p == '-' || *p == '\xC2' || *p == '\xB7')) {
            if ((*p == '-' || *p == '/') && (u.empty() || !std::isalpha((unsigned char)p[1]))) break;   // "ft-lbf", "m/s"; else a minus
            u += *p++;
        }
        if (u.find("\xC2\xB7") != std::string::npos) u.erase(u.find("\xC2\xB7"), 2);   // ft·lbf
        double k;
        if (u.empty()) k = fromDisp(q, 1.0);
        else if ((k = unitScale(q, u)) == 0) return false;
        total += sign * v * k;
        sign = 1;
        terms++;
        ws();
    }
    if (!terms) return false;
    out = total;
    return true;
}

std::string typedQ(Q q, double v, int dec) {
    // an exact 32nd of an inch is typed as a fraction: always when inches are shown, and when mm are shown unless it's
    // also a round number of mm (so a 1/32 in snap reads back as 1/32 in, not 0.79 mm)
    bool roundMM = std::fabs(v * 100 - std::round(v * 100)) < 1e-6;
    if (q == Q::Length && v > 0 && (imperial() || !roundMM)) {
        double in = v / 25.4;
        for (int den : {1, 2, 4, 8, 16, 32}) {
            double n = in * den;
            if (std::fabs(n - std::round(n)) < 1e-6) {
                long w = std::lround(n);
                if (den > 1 && w % 2 == 0) continue;   // reduced by a smaller denominator already
                char b[48];
                if (den == 1) std::snprintf(b, sizeof b, "%ld in", w);
                else if (w >= den) { long whole = w / den, r = w % den; std::snprintf(b, sizeof b, "%ld %ld/%d in", whole, r, den); }
                else std::snprintf(b, sizeof b, "%ld/%d in", w, den);
                return b;
            }
        }
    }
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.*f", dec >= 0 ? dec : decimalsOf(q) + 2, toDisp(q, v));
    std::string s = buf;   // trim zeros
    if (s.find('.') != std::string::npos) { while (s.back() == '0') s.pop_back(); if (s.back() == '.') s.pop_back(); }
    return s + " " + unitOf(q);
}
