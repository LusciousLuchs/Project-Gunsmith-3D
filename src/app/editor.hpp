// Gunsmith3D - Build mode: editing operations and drawing tools.
#pragma once
#include "app/app.hpp"

V2 measureSnap(V2 w);  // the Measure tool's snapping
BodyDef makeBarrel(V2 rear, V2 muzzle, const Cartridge& cart);  // the Barrel tool's part
void rotateBody(int i, V2 centre, double deg);
bool partRingsInput(bool overUI);   // the selected part's rotation rings (Select tool): true while they have the mouse
void partRingsDraw();
bool cornerArrowsInput(bool overUI);   // Select tool: move arrows on the picked corners (true while they have the mouse)
void cornerArrowsDraw();
void partRingTest(int axis, double deg);   // (tests)  // turn in the side view (about Z), with what's attached
void moveBody(int i, double dx, double dy);
void deleteSel();
void duplicateSel();
void finishBody();
void finishTrack();
void makeMagwell(const BodyDef& b);
void buildInput(bool overUI);
bool splitReady();          // false if the last edge split was a moment ago (hammering can't pile up corners)
void cornerLimitToast();    // "a part can have at most MAX_CORNERS corners"

void drawMoveGuides();   // (overlay) the lines a dragged part snapped to, and a number being typed for a move
bool moveTypedAfter();   // (Move) a number typed just after a move, then Enter: that move made exact
bool moveTypingOpen();   // a number can be typed for a move now
void drawHoverHelp();     // (overlay) what a click or drag will do where the mouse rests
