// Gunsmith3D - the Faces tool: push, pull and extrude any face of a part in 3D (see faces.cpp).
#pragma once

void facesInput(bool overUI);   // the Faces tool, and E (extrude) on the selected part's face in the Select tool
bool facesActive();             // a face is being pushed or extruded
void drawFaces();               // the face under the mouse, and the one being moved
