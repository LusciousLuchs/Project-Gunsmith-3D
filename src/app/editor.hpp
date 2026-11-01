// Gunsmith3D - Build mode: editing operations and drawing tools.
#pragma once
#include "app/app.hpp"

void moveBody(int i, double dx, double dy);
void deleteSel();
void duplicateSel();
void finishBody();
void finishTrack();
void makeMagwell(const BodyDef& b);
void buildInput(bool overUI);
bool splitReady();          // false if the last edge split was a moment ago (hammering can't pile up corners)
void cornerLimitToast();    // "a part can have at most 32 corners"
