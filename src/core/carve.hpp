// Gunsmith3D - carves: cutters pushed into a part from any direction (see CarveDef).
// The physics sees a carved part as thin slices through its depth, each the side outline with the cuts' cross
// sections taken out (as "cavities"). The display cuts the real 3D solid (csg.hpp).
#pragma once
#include "design.hpp"
#include "csg.hpp"
#include <vector>

namespace carve {

using csg::P3;

// The cutter's own axes: u and v across the shape, d the way it goes in (design axes: x fwd, y down, z near).
void frame(const CarveDef& c, P3& u, P3& v, P3& d);
// The shape across the cutter, in (u, v) mm, counter-clockwise; convex.
std::vector<V2> shape2D(const CarveDef& c, int segs = 32);
// How far before the entry point the cutter starts, so a tilted cutter clears the surface (mm).
double leadIn(const CarveDef& c);
// The cutter as a solid: the shape at its start and at its end (design mm; z absolute, as BodyDef::z).
struct Prism { std::vector<P3> lo, hi; };
Prism prism(const CarveDef& c, const BodyDef& b);
// Its outward-facing polygons (with smooth normals round curves), for csg::subtract.
std::vector<csg::Poly> prismPolys(const Prism& p, const CarveDef& c);
// Where the cutter crosses the side-view plane at depth z: a convex polygon (design mm), or empty.
std::vector<V2> section(const Prism& p, double z);

struct Slice {
    double z0 = 0, z1 = 0;                       // depth range (absolute mm)
    std::vector<std::vector<V2>> cav;            // the cuts at this depth (design mm, convex)
};
// The part through its thickness. Empty when it has no carves (it is then one plain slab).
std::vector<Slice> slices(const BodyDef& b, int maxSlices = 16);

// Area, centroid and polar second moment (about the centroid) of `outline` minus `cav` (mm, mm^2, mm^4).
bool material(const std::vector<V2>& outline, const std::vector<std::vector<V2>>& cav, double& area, V2& c, double& J);

bool inConvex(const std::vector<V2>& poly, V2 p);  // either winding
double volume(const BodyDef& b);                  // mm^3 of material left, carves taken out

// Keep carves with a part through edits: moved by (dx, dy); mirrored about x = ax (flipX) or y = ay (flipY).
void moveCarves(BodyDef& b, double dx, double dy);
void mirrorCarves(BodyDef& b, V2 a, V2 c);    // mirrored across the side-view line a-c
void flipCarvesZ(BodyDef& b);                  // mirrored front to back about the part's centre
void rethick(BodyDef& b, double oldThick);     // carves on a face stay on that face when the thickness changes

}  // namespace carve
