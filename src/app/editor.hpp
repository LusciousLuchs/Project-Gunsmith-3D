// Gunsmith3D - Build mode: editing operations and drawing tools.
#pragma once
#include "app/app.hpp"

V2 measureSnap(V2 w);  // the Measure tool's snapping
BodyDef makeBarrel(V2 rear, V2 muzzle, const Cartridge& cart);  // the Barrel tool's part
void rotateBody(int i, V2 centre, double deg);  // turn in the side view (about Z), with what's attached
void moveBody(int i, double dx, double dy);
void deleteSel();
void duplicateSel();
void finishBody();
void finishTrack();
void makeMagwell(const BodyDef& b);
void buildInput(bool overUI);
bool splitReady();          // false if the last edge split was a moment ago (hammering can't pile up corners)
void cornerLimitToast();    // "a part can have at most 32 corners"
