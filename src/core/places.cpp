// Gunsmith3D - places in the US for career mode (see places.hpp).
#include "places.hpp"

#include <cmath>
#include <cstdio>

namespace places {

const std::vector<City>& cities() {
    // real cities and one of each's real ZIP codes; latitude and longitude for the map
    static const std::vector<City> c = {
        {"Austin", "TX", "78705", 30.27, -97.74}, {"Houston", "TX", "77002", 29.76, -95.37}, {"Dallas", "TX", "75201", 32.78, -96.80},
        {"San Antonio", "TX", "78205", 29.42, -98.49}, {"El Paso", "TX", "79901", 31.76, -106.49}, {"Amarillo", "TX", "79101", 35.22, -101.83},
        {"Phoenix", "AZ", "85004", 33.45, -112.07}, {"Tucson", "AZ", "85701", 32.22, -110.97}, {"Flagstaff", "AZ", "86001", 35.20, -111.65},
        {"Albuquerque", "NM", "87102", 35.08, -106.65}, {"Las Vegas", "NV", "89101", 36.17, -115.14}, {"Reno", "NV", "89501", 39.53, -119.81},
        {"Los Angeles", "CA", "90012", 34.05, -118.24}, {"San Diego", "CA", "92101", 32.72, -117.16}, {"Sacramento", "CA", "95814", 38.58, -121.49},
        {"Fresno", "CA", "93721", 36.74, -119.79}, {"Portland", "OR", "97204", 45.52, -122.68}, {"Bend", "OR", "97701", 44.06, -121.31},
        {"Seattle", "WA", "98101", 47.61, -122.33}, {"Spokane", "WA", "99201", 47.66, -117.43}, {"Boise", "ID", "83702", 43.62, -116.20},
        {"Salt Lake City", "UT", "84101", 40.76, -111.89}, {"Denver", "CO", "80202", 39.74, -104.99}, {"Colorado Springs", "CO", "80903", 38.83, -104.82},
        {"Cheyenne", "WY", "82001", 41.14, -104.82}, {"Casper", "WY", "82601", 42.85, -106.31}, {"Billings", "MT", "59101", 45.78, -108.50},
        {"Missoula", "MT", "59802", 46.87, -113.99}, {"Fargo", "ND", "58102", 46.88, -96.79}, {"Sioux Falls", "SD", "57104", 43.54, -96.73},
        {"Omaha", "NE", "68102", 41.26, -95.93}, {"Wichita", "KS", "67202", 37.69, -97.34}, {"Oklahoma City", "OK", "73102", 35.47, -97.52},
        {"Tulsa", "OK", "74103", 36.15, -95.99}, {"Minneapolis", "MN", "55401", 44.98, -93.27}, {"Duluth", "MN", "55802", 46.79, -92.10},
        {"Des Moines", "IA", "50309", 41.59, -93.62}, {"Kansas City", "MO", "64106", 39.10, -94.58}, {"St. Louis", "MO", "63101", 38.63, -90.20},
        {"Little Rock", "AR", "72201", 34.75, -92.29}, {"Shreveport", "LA", "71101", 32.53, -93.75}, {"New Orleans", "LA", "70112", 29.95, -90.07},
        {"Jackson", "MS", "39201", 32.30, -90.18}, {"Birmingham", "AL", "35203", 33.52, -86.80}, {"Mobile", "AL", "36602", 30.69, -88.04},
        {"Nashville", "TN", "37201", 36.16, -86.78}, {"Knoxville", "TN", "37902", 35.96, -83.92}, {"Memphis", "TN", "38103", 35.15, -90.05},
        {"Louisville", "KY", "40202", 38.25, -85.76}, {"Paducah", "KY", "42001", 37.08, -88.60}, {"Chicago", "IL", "60602", 41.88, -87.63},
        {"Peoria", "IL", "61602", 40.69, -89.59}, {"Milwaukee", "WI", "53202", 43.04, -87.91}, {"Green Bay", "WI", "54301", 44.51, -88.01},
        {"Detroit", "MI", "48226", 42.33, -83.05}, {"Grand Rapids", "MI", "49503", 42.96, -85.67}, {"Indianapolis", "IN", "46204", 39.77, -86.16},
        {"Columbus", "OH", "43215", 39.96, -83.00}, {"Cleveland", "OH", "44113", 41.50, -81.69}, {"Pittsburgh", "PA", "15222", 40.44, -80.00},
        {"Erie", "PA", "16501", 42.13, -80.09}, {"Philadelphia", "PA", "19107", 39.95, -75.16}, {"Buffalo", "NY", "14202", 42.89, -78.88},
        {"Albany", "NY", "12207", 42.65, -73.76}, {"Burlington", "VT", "05401", 44.48, -73.21}, {"Bangor", "ME", "04401", 44.80, -68.77},
        {"Portland", "ME", "04101", 43.66, -70.26}, {"Concord", "NH", "03301", 43.21, -71.54}, {"Hartford", "CT", "06103", 41.76, -72.67},
        {"Richmond", "VA", "23219", 37.54, -77.44}, {"Roanoke", "VA", "24011", 37.27, -79.94}, {"Charleston", "WV", "25301", 38.35, -81.63},
        {"Raleigh", "NC", "27601", 35.78, -78.64}, {"Charlotte", "NC", "28202", 35.23, -80.84}, {"Asheville", "NC", "28801", 35.60, -82.55},
        {"Columbia", "SC", "29201", 34.00, -81.03}, {"Atlanta", "GA", "30303", 33.75, -84.39}, {"Savannah", "GA", "31401", 32.08, -81.09},
        {"Jacksonville", "FL", "32202", 30.33, -81.66}, {"Tampa", "FL", "33602", 27.95, -82.46}, {"Miami", "FL", "33130", 25.77, -80.20},
        {"Tallahassee", "FL", "32301", 30.44, -84.28}, {"Pensacola", "FL", "32502", 30.42, -87.22}, {"Baltimore", "MD", "21202", 39.29, -76.61},
    };
    return c;
}
int nearestCity(double lat, double lon) {
    int best = 0; double bd = 1e18;
    const auto& c = cities();
    for (int i = 0; i < (int)c.size(); i++) {
        double dx = (c[i].lon - lon) * std::cos(lat * 3.14159265358979 / 180), dy = c[i].lat - lat, d = dx * dx + dy * dy;
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}
int cityByName(const std::string& name, const std::string& state) {
    const auto& c = cities();
    for (int i = 0; i < (int)c.size(); i++) if (name == c[i].name && state == c[i].state) return i;
    return -1;
}

StateTax stateTax(const std::string& s) {
    // sales tax (state and a typical local share) and tax on a business's profit, percent: close to the real ones
    static const std::vector<StateTax> t = {
        {"TX", 8.2, 0}, {"AZ", 8.4, 4.9}, {"NM", 7.6, 5.9}, {"NV", 8.2, 0}, {"CA", 8.8, 8.8}, {"OR", 0, 7.6}, {"WA", 9.4, 0},
        {"ID", 6.0, 5.8}, {"UT", 7.2, 4.65}, {"CO", 7.8, 4.4}, {"WY", 5.4, 0}, {"MT", 0, 6.75}, {"ND", 7.0, 4.3}, {"SD", 6.1, 0},
        {"NE", 6.9, 5.8}, {"KS", 8.7, 7.0}, {"OK", 9.0, 4.0}, {"MN", 8.0, 9.8}, {"IA", 6.9, 5.5}, {"MO", 8.4, 4.0}, {"AR", 9.5, 4.3},
        {"LA", 9.6, 7.5}, {"MS", 7.1, 5.0}, {"AL", 9.3, 6.5}, {"TN", 9.6, 6.5}, {"KY", 6.0, 5.0}, {"IL", 8.9, 9.5}, {"WI", 5.7, 7.9},
        {"MI", 6.0, 6.0}, {"IN", 7.0, 4.9}, {"OH", 7.2, 0}, {"PA", 6.3, 8.99}, {"NY", 8.5, 7.25}, {"VT", 6.4, 8.5}, {"ME", 5.5, 8.9},
        {"NH", 0, 7.5}, {"CT", 6.35, 7.5}, {"VA", 5.8, 6.0}, {"WV", 6.6, 6.5}, {"NC", 7.0, 2.5}, {"SC", 7.5, 5.0}, {"GA", 7.4, 5.75},
        {"FL", 7.0, 5.5}, {"MD", 6.0, 8.25}};
    for (auto& x : t) if (s == x.state) return x;
    return {"", 7.0, 5.0};
}

const std::vector<std::pair<double, double>>& usOutline() {
    static const std::vector<std::pair<double, double>> o = {
        {-124.7, 48.4}, {-123.0, 49.0}, {-95.2, 49.0}, {-95.2, 49.4}, {-94.8, 48.7}, {-89.6, 48.0}, {-84.6, 46.5}, {-82.4, 45.2},
        {-82.4, 42.9}, {-83.0, 42.0}, {-79.0, 43.3}, {-76.3, 44.2}, {-74.9, 45.0}, {-71.5, 45.0}, {-70.0, 46.7}, {-69.2, 47.4},
        {-67.8, 47.1}, {-67.0, 44.8}, {-70.0, 43.7}, {-70.6, 42.6}, {-70.0, 41.8}, {-71.9, 41.3}, {-74.0, 40.6}, {-74.0, 39.0},
        {-75.5, 38.4}, {-76.0, 37.0}, {-75.5, 35.3}, {-77.9, 33.9}, {-79.2, 33.2}, {-81.0, 32.0}, {-81.4, 30.4}, {-80.0, 26.8},
        {-80.4, 25.2}, {-81.8, 25.9}, {-82.8, 27.9}, {-83.0, 29.1}, {-84.3, 30.0}, {-86.5, 30.4}, {-89.0, 30.3}, {-89.6, 29.2},
        {-91.5, 29.4}, {-93.8, 29.7}, {-95.0, 29.2}, {-97.2, 27.6}, {-97.4, 26.0}, {-99.1, 26.4}, {-101.4, 29.8}, {-103.1, 29.0},
        {-104.6, 29.6}, {-106.5, 31.8}, {-108.2, 31.8}, {-111.1, 31.3}, {-114.8, 32.5}, {-117.1, 32.5}, {-118.5, 34.0}, {-120.6, 34.6},
        {-121.9, 36.6}, {-122.5, 37.8}, {-123.8, 39.4}, {-124.2, 41.0}, {-124.5, 42.8}, {-124.0, 46.2}, {-124.7, 48.4}};
    return o;
}

namespace {
unsigned long long mix(unsigned long long z) {
    z += 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
struct Draw {
    unsigned long long s;
    int operator()(int n) { s = mix(s); return (int)(s % (unsigned long long)n); }
};
}  // namespace

std::string streetLine(unsigned seed, bool suite) {
    // made-up street names: a first part, sometimes a second, and a kind of street; or a numbered street
    static const char* A[] = {"Willow", "Copper", "Harlan", "Birch", "Cedar", "Juniper", "Sable", "Orchard", "Mesa", "Prairie", "Granite",
                              "Larkspur", "Hawthorne", "Redstone", "Fox", "Mill", "Quarry", "Kestrel", "Ashby", "Calloway", "Pemberton",
                              "Delmar", "Bramble", "Saddle", "Wicker", "Tamarack", "Sorrel", "Briar", "Lantern", "Haverford", "Gilmore",
                              "Ridgeback", "Thistle", "Coldwater", "Sumner", "Carrow", "Ellery", "Winslow", "Bexley", "Marlow", "Dunmore",
                              "Fallow", "Greer", "Holloway", "Ivory", "Kettle", "Linden", "Mercer", "Nettle", "Oakhurst", "Penrose",
                              "Rowan", "Shale", "Tolliver", "Upton", "Vesper", "Wren", "Yarrow", "Alder", "Beacon", "Cutler", "Drover"};
    static const char* B[] = {"", "", "", "field", "wood", "creek", "view", "stone", "brook", "ridge", "haven", "gate", "crest", "ford", "dale", "wick"};
    static const char* K[] = {"St", "Ave", "Rd", "Ln", "Dr", "Blvd", "Way", "Ct", "Pkwy", "Trl", "Pl", "Cir", "Loop", "Pass", "Row"};
    static const char* D[] = {"", "", "", "", "N ", "S ", "E ", "W "};
    Draw r{seed * 2654435761ull + 77};
    char b[128];
    int num = 100 + r(9800);
    if (r(9) == 0) {   // a numbered street
        int n = 2 + r(98);
        const char* sfx = (n % 100 >= 11 && n % 100 <= 13) ? "th" : n % 10 == 1 ? "st" : n % 10 == 2 ? "nd" : n % 10 == 3 ? "rd" : "th";
        std::snprintf(b, sizeof b, "%d %s%d%s %s", num, D[r(8)], n, sfx, r(2) ? "St" : "Ave");
    } else {
        const char* a = A[r((int)(sizeof A / sizeof *A))];
        const char* bb = B[r((int)(sizeof B / sizeof *B))];
        const char* k = K[r((int)(sizeof K / sizeof *K))];
        std::snprintf(b, sizeof b, "%d %s%s%s %s", num, D[r(8)], a, bb, k);
    }
    std::string s = b;
    if (suite) { char u[24]; std::snprintf(u, sizeof u, ", Ste %d", 100 + r(30) * 2 + (r(4) == 0 ? 100 : 0)); s += u; }
    return s;
}
std::string cityLine(int city) {
    const auto& c = cities();
    if (city < 0 || city >= (int)c.size()) city = 0;
    return std::string(c[city].name) + ", " + c[city].state + " " + c[city].zip;
}
std::string address(unsigned seed, int city, bool suite) { return streetLine(seed, suite) + ", " + cityLine(city); }

std::string licenceNumber(unsigned seed) {
    Draw r{seed * 0x632BE59BD9B4E019ull + 5};
    char b[32];
    std::snprintf(b, sizeof b, "%c%c-%07d-%c", 'A' + r(26), 'A' + r(26), r(10000000), 'A' + r(26));
    return b;
}
std::string transferNumber(unsigned seed) {
    Draw r{seed * 0x8CB92BA72F3D8DD7ull + 11};
    char b[32];
    std::snprintf(b, sizeof b, "C-%04d-%02d%c-%03d", r(10000), r(100), 'A' + r(26), r(1000));
    return b;
}

}  // namespace places
