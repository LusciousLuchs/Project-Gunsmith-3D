// Gunsmith3D - places in the US for career mode: real cities (their state and a real ZIP code), each state's taxes, a
// rough outline of the lower 48 for a map to pick a spot on, and made-up street addresses and ID numbers (never in a
// real Social Security or licence format). No graphics.
#pragma once
#include <string>
#include <vector>

namespace places {

struct City { const char* name; const char* state; const char* zip; double lat, lon; };
const std::vector<City>& cities();
int nearestCity(double lat, double lon);
int cityByName(const std::string& name, const std::string& state);   // -1 if not listed

struct StateTax { const char* state; double sales, business; };   // percent: sales tax, tax on business profit
StateTax stateTax(const std::string& state);
constexpr double FEDERAL_TAX = 21;   // percent of profit

// the lower 48's outline (lon, lat pairs, clockwise from the north-west), rough: for drawing a map to click on
const std::vector<std::pair<double, double>>& usOutline();

// a made-up street address in a city ("4127 Copperfield Ln, Austin, TX 78705"); `seed` picks it, the same each time.
// suite: a unit number (a strip-mall store, an office)
std::string address(unsigned seed, int city, bool suite = false);
std::string streetLine(unsigned seed, bool suite = false);           // just "4127 Copperfield Ln"
std::string cityLine(int city);                                      // "Austin, TX 78705"
// a made-up driver's licence number in a format no real state uses: two letters, a dash, seven digits, a letter
std::string licenceNumber(unsigned seed);
// a made-up customer number for the background check's form (not a Social Security number): "C-" and three groups
std::string transferNumber(unsigned seed);

}  // namespace places
