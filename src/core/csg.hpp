// Gunsmith3D - solid geometry: subtract one closed polygon mesh from another (BSP trees, after the classic
// csg.js method). Used to cut carves out of a part's mesh for display and picking; the physics never needs it.
#pragma once
#include <vector>

namespace csg {

struct P3 {
    double x = 0, y = 0, z = 0;
    P3 operator+(const P3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    P3 operator-(const P3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    P3 operator*(double s) const { return {x * s, y * s, z * s}; }
};
double dot(P3 a, P3 b);
P3 cross(P3 a, P3 b);
P3 unit(P3 a);
P3 lerp(P3 a, P3 b, double t);

struct Vert { P3 p, n; };                       // position and shading normal
struct Poly { std::vector<Vert> v; P3 pn; double pw = 0; };  // convex, planar; plane pn.x = pw (outward pn)

Poly makePoly(std::vector<Vert> v);              // works out the plane from the winding (counter-clockwise = front)
// A minus B. Both must be closed solids with outward-facing polygons.
std::vector<Poly> subtract(const std::vector<Poly>& a, const std::vector<Poly>& b);

}  // namespace csg
