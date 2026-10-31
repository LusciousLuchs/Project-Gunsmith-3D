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
std::vector<Hit> bodiesUnderMouse();
TestHit testPick();
const Handle* handleAt(Vector2 m);
void fitView();
void updateCamera();
void beginViewport3D();
void endViewport3D();
