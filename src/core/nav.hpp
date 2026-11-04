// Gunsmith3D - Path-finding for NPCs over a map's brushes.
//
// The map is sampled on a grid seen from above. In every column, each walkable floor (the top of a brush, flat enough
// to stand on, with nothing on top of it) becomes a node if the NPC's body fits standing there, so floors above floors
// (a balcony, the top of a crate, a second storey) each get their own nodes. A node links to the floors around it that
// it can walk to (up or down no more than the NPC can step) and drops down off ledges (one way). A* finds the route;
// the route is pulled straight wherever a straight walk works. One grid per NPC size (width, height, step), built once
// per map. No graphics here.
#pragma once
#include "map.hpp"
#include <memory>
#include <vector>

namespace nav {

struct Agent {
    double halfWidth = 0.24;   // m: half its width (its body is an upright box, as the player's)
    double height = 1.8;
    double step = 0.42;        // how high a step it walks up
};

struct Grid {
    Agent agent;
    double cell = 0.25;        // m
    double x0 = 0, z0 = 0;     // the corner of column (0, 0)
    int nx = 0, nz = 0;
    std::vector<int> colStart; // nodes of column c (= ix + iz * nx) are colStart[c] .. colStart[c + 1] - 1
    std::vector<float> y;      // each node's floor height
    std::vector<int> col;      // each node's column
    std::vector<int> link;     // node * 8 + direction: the node it walks (or drops) to, or -1
    size_t nodes() const { return y.size(); }
    gmap::V3 at(int n) const;  // a node's spot on its floor
};

// Build the grid for a body this size. Large maps get a coarser grid (at most about 400 000 columns).
Grid build(const gmap::Map& m, const Agent& a);
// The node nearest `p` that it could be standing on (within `reach` metres to the side), or -1
int nearestNode(const Grid& g, gmap::V3 p, double reach = 0.8);
// A route from `from` to `to` (feet positions), pulled straight: the points to walk through, ending at `to` (or, if
// `to` can't be reached, at the nearest place that can). False if `from` isn't on the grid (in the air, say).
bool findPath(const Grid& g, gmap::V3 from, gmap::V3 to, std::vector<gmap::V3>& out, bool* reached = nullptr);
// Can it walk straight from a to b on the grid (no drops)?
bool straightWalk(const Grid& g, gmap::V3 a, gmap::V3 b);

// Grids per NPC size for one map, built when first needed
struct Cache {
    const gmap::Map* map = nullptr;
    size_t brushes = 0;
    std::vector<std::pair<Agent, std::shared_ptr<Grid>>> grids;
    const Grid& get(const gmap::Map& m, Agent a);   // (sizes are rounded so similar NPCs share a grid)
    void clear() { grids.clear(); map = nullptr; }
};

}  // namespace nav
