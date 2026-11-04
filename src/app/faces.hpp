// Gunsmith3D - the Faces tool: push, pull and extrude any face of a part in 3D (see faces.cpp).
#pragma once

void facesInput(bool overUI);   // the Faces tool, and E (extrude) on the selected part's face in the Select tool
bool facesActive();             // a face is being pushed or extruded
void drawFaces();               // the face under the mouse, and the one being moved
bool selectFacesInput(bool overUI);   // Select tool: the selected part's faces and far corners (true: the mouse was used)
#include "app/app.hpp"
void facePushStart(const App::FacePick& f, bool extrude);   // start pushing (or extruding) that face; the mouse is down
