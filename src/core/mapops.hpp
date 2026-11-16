// Gunsmith3D - what the level editor does to a map, with no graphics: turning and scaling brushes about any axis,
// editing a brush's corners, snapping to other brushes, aligning and spacing, arrays, props and new shapes, pieces
// that can be copied and shared, and measuring. The editor's mouse and keys only call these.
#pragma once
#include "map.hpp"
#include <string>
#include <vector>

namespace gmap {

// ---- turning, scaling and mirroring (any axis; 0 = X, 1 = Y, 2 = Z)
struct Mat3 { double m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}; };   // row by row
Mat3 rotationAbout(int axis, double radians);               // +: the right-hand way (Y: matches rotateY)
Mat3 scalingBy(V3 s);
V3 apply(const Mat3& a, V3 v);
// p -> A (p - pivot) + pivot + move, for every point of the brush. A can be any turn or stretch that isn't flat.
bool transform(Brush& b, const Mat3& a, V3 pivot, V3 move = {});
bool scaleBrush(Brush& b, V3 factors, V3 pivot);            // false (unchanged) if a factor is 0 or less
void rotateAbout(Brush& b, int axis, double radians, V3 pivot);
void mirrorAbout(Brush& b, int axis, V3 pivot);             // X, Y or Z

// ---- things in the map, picked: some brushes and some entities
struct Selection { std::vector<int> brushes, ents; bool empty() const { return brushes.empty() && ents.empty(); } };
void selectionBounds(const Map& m, const Selection& s, V3& mn, V3& mx);
V3 selectionCentre(const Map& m, const Selection& s);       // the middle of its bounds
// Turn / stretch / move the whole selection about a pivot. An entity's position follows; turning about Y also turns
// which way it faces.
void transformSelection(Map& m, const Selection& s, const Mat3& a, V3 pivot, V3 move, double yawChangeDegrees);
void rotateSelection(Map& m, const Selection& s, int axis, double radians, V3 pivot);
bool scaleSelection(Map& m, const Selection& s, V3 factors, V3 pivot);
void moveSelection(Map& m, const Selection& s, V3 d);
void mirrorSelection(Map& m, const Selection& s, int axis, V3 pivot);

// ---- a brush's corners
std::vector<V3> brushVertices(const Brush& b);              // each corner once
std::vector<std::pair<int, int>> brushEdges(const Brush& b, const std::vector<V3>& verts);   // pairs of corner numbers
// The convex solid round some points (materials and texture settings from the nearest faces of `like`). False if flat.
bool brushFromPoints(const std::vector<V3>& pts, const Brush& like, Brush& out);
// Move some corners by `delta`; the brush becomes the solid round the new corner positions, so it stays convex.
bool moveVertices(const Brush& b, const std::vector<int>& which, V3 delta, Brush& out);
// A new brush standing out from one face by `amount` (a face you pull a new piece from).
bool extrudeFace(const Brush& b, int face, double amount, Brush& out);

// ---- snapping: landing on other brushes' corners, edge middles and face middles (and things)
struct SnapHit { bool hit = false; V3 delta; V3 from, to; };   // `delta` moves `from` onto `to`
// Where moving the selection by `delta` would put it, nudged so one of its corners (or middle) lands on a nearby point
// of something else, if one is within `radius` metres. `delta` returned is the nudged move.
SnapHit snapMove(const Map& m, const Selection& s, V3 delta, double radius);
// the nearest snap point to p (corners, edge middles, face middles, things), skipping the selection; hit = false if none
SnapHit snapPoint(const Map& m, const Selection& skip, V3 p, double radius);

// ---- aligning and spacing
enum class AlignTo { Low, Middle, High };
void alignSelection(Map& m, const Selection& s, int axis, AlignTo to);       // line them up along the axis
void distributeSelection(Map& m, const Selection& s, int axis);              // even gaps between the first and last

// ---- pieces: a copy of some of a map, to place again, copy between maps, share, or make arrays from
struct Piece { std::vector<Brush> brushes; std::vector<Entity> ents; bool empty() const { return brushes.empty() && ents.empty(); } };
Piece copyPiece(const Map& m, const Selection& s);
Selection pastePiece(Map& m, const Piece& p, V3 move = {});                  // added to the map; the new things picked
void transformPiece(Piece& p, const Mat3& a, V3 pivot, V3 move, double yawChangeDegrees);
std::string pieceToJson(const Piece& p);                                     // marker "gunsmithMapPiece"
bool pieceFromJson(const std::string& text, Piece& out);
// Making copies. `count` is the number of new copies; each one steps further along.
Selection linearArray(Map& m, const Selection& s, int count, V3 step);
// copies round an axis through `pivot`; `totalDegrees` of 360 spreads the original and copies evenly all the way round
Selection radialArray(Map& m, const Selection& s, int count, V3 pivot, int axis, double totalDegrees, bool turnCopies);

// ---- sharing a whole map as text: "GS3D-MAP1:" and the file in base64 (paste it anywhere)
std::string mapShareCode(const Map& m);
bool mapFromShareText(const std::string& text, Map& out);                    // the code, or plain map JSON

// ---- more shapes (all convex; some are several brushes)
Brush makeCone(V3 mn, V3 mx, int sides, int mat);                            // a pyramid with 4 sides, a cone with more
Brush makeSphere(V3 mn, V3 mx, int rings, int sides, int mat);              // a faceted ball (or egg) filling the box
Brush makeQuarterRound(V3 mn, V3 mx, int steps, int dir, int mat);          // a rounded corner (a quarter of a cylinder); dir 0..3
std::vector<Brush> makeArch(V3 mn, V3 mx, int segments, double wall, int dir, int mat);   // a doorway arch: opening along X (dir 0/2) or Z (1/3)
std::vector<Brush> makePipe(V3 mn, V3 mx, int sides, double wall, int mat);  // an upright tube with a hole through it
// ---- props: furniture and clutter made of brushes, so they collide like anything else
const std::vector<std::string>& propKinds();
std::vector<Brush> makeProp(const std::string& kind, V3 base, double yawDegrees);   // base: the middle of its footprint, on the floor

// ---- measuring
struct Measure { double distance = 0, dx = 0, dy = 0, dz = 0, flat = 0, slopeDegrees = 0; };
Measure measure(V3 a, V3 b);

}  // namespace gmap
