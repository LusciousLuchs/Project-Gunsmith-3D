// Gunsmith3D - Camera, screen/world conversion and picking.
#pragma once
#include "app/app.hpp"

struct Hit { int body; float t; V2 local; };                     // a design part under the cursor
struct TestHit { sim::Body* b = nullptr; int tmpl = -1; V2 mm; float z = 0; bool handle = false; };  // a live body (or its bolt handle) under the cursor

bool mouseInViewport();
Vector2 toScreen(Vector3 p);
Ray mouseRay();                  // the ray under the cursor, in world space
V2 mouseMM(float z = 0);
V2 snapMM(V2 p);
std::vector<Hit> bodiesUnderMouse();   // nearest first; a cut through a part is open: the part behind is picked there
bool solidAt(int body, V2 mm, double zmm);   // is there material of this part at this point (design mm, depth mm)? Cuts are empty
double depthMM(int body, float worldZ);      // a world depth as this part's depth in mm (undoing its anti-flicker nudge)
TestHit testPick();
const Handle* handleAt(Vector2 m);
void fitView();
void updateCamera();
void beginViewport3D();
void endViewport3D();

enum class View { Right, Left, Top, Bottom, Front, Back };
void setOrtho(bool on);        // orthographic (no perspective) from wherever the camera is
void snapView(View v);         // straight at one face of the gun, orthographic
void sideView();               // the right side, flat on: the classic drawing view
