// Gunsmith3D - the 3D vertices tool: a part as a free 3D mesh, edited corner by corner (see meshtool.cpp).
#pragma once
#include <vector>

void meshInput(bool overUI);        // the tool's mouse and keys (Build mode)
void drawMeshTool();                // the selected mesh part's edges and corners, the face under the mouse
// for the panel
std::vector<int>& meshSel();        // the picked corners of the selected part
int& meshFace();                    // the picked face (-1 none)
double& meshExtrudeBy();            // how far E pushes a face out (mm)
void meshMakeFromSelected();        // the selected part becomes a 3D mesh (its shape as it is now)
void meshFlatten();                 // back to an ordinary part, its outline the mesh's biggest cross-section
void meshAddRing(int axis, double at);
void meshExtrudePicked();
void meshRemovePicked();
void meshScalePicked(double sx, double sy, double sz);   // about the picked corners' middle
void meshMovePicked(double dx, double dy, double dz);
