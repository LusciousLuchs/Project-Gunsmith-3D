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
