// Gunsmith3D - fitting a modelled character (a skinned glTF from a 3D artist) onto an NPC's bones: which of the model's
// joints follows which bone, and the joints posed as the NPC stands, walks or falls. No graphics: the app loads the
// model and skins it with these.
#pragma once
#include "npc.hpp"
#include <string>
#include <vector>

namespace rig {

struct Xf { double p[3] = {0, 0, 0}; double q[4] = {0, 0, 0, 1}; };   // a position (metres) and a turn (quaternion x, y, z, w)
struct Joint { std::string name; int parent = -1; Xf bind; };          // the model's joints as modelled (in the model's own space: y up, facing +z)

// Which bone of the NPC each joint follows (-1: none; it's carried by the joint above it). By the bone names first
// (case and spaces aside), else the usual names for a human (Mixamo's, Blender's and others: hips, spine, chest,
// neck, head, upper arm, forearm, hand, thigh, shin, foot); left and right by which side of the model the joint is on.
std::vector<int> mapJoints(const std::vector<Joint>& J, const NpcDef& n);
// what was found, for the panel: "14 of the 15 body bones" and the ones missing
std::string mapReport(const std::vector<Joint>& J, const std::vector<int>& map, const NpcDef& n);

// The joints posed like the NPC's bones `P` (npcPose, mm): each joint's place and turn in the model's space. The body
// (hips, spine, neck, head) turns as its bone turns; arms and legs swing to point along their bones; joints that
// follow nothing ride along with the joint above them. In the model's own size: the hips go where the NPC's pelvis
// is, in proportion to their heights (the app then draws it at the NPC's size, modelScale).
std::vector<Xf> poseJoints(const std::vector<Joint>& J, const std::vector<int>& map, const NpcDef& n, const std::vector<NpcBoneAt>& P);

// metres of the model per mm of the NPC: its hips' height against the NPC's pelvis (0.001 if they can't be compared)
double modelScale(const std::vector<Joint>& J, const std::vector<int>& map, const NpcDef& n);

// the matrix (column-major, 16) taking a point as modelled to where the posed joint puts it: posed x inverse(bind)
void skinMatrix(const Xf& bind, const Xf& posed, float out[16]);

}  // namespace rig
