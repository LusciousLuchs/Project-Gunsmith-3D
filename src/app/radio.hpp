// Gunsmith3D - radios on maps: internet stations playing out of where the radio stands (spatial sound: louder near,
// from the side it's on, muffled through walls and from behind), the radio model, and its window.
#pragma once
#include "map.hpp"
#include "raylib.h"
#include <string>

void radioInit();      // after the audio device is up
void radioShutdown();
// Each frame on a map where radios play: you hear them from `ear` facing `fwd` (world metres). Screens that don't call
// it go quiet; a station not heard for a few seconds is let go.
void radioListen(const gmap::Map& m, gmap::V3 ear, gmap::V3 fwd);
void radioTick();      // once a frame from the main loop
// what a radio is doing: "Playing", "Buffering", "Error: ...", "Off"; and the song, if the station says
std::string radioState(const gmap::Entity& e);
std::string radioSong(const gmap::Entity& e);
void radioLevels(double& left, double& right);   // (tests, with GUNSMITH_RADIOSILENT) how loud each ear was lately
// the radio in front of you within reach (-1 if none)
int radioFacing(const gmap::Map& m, gmap::V3 eye, gmap::V3 fwd);
void drawRadio(Vector3 base, float yawDeg, Color tint = Color{0, 0, 0, 0}, bool on = true);
gmap::Entity newRadio();   // a radio as placed: the first built-in station, on

// the radio's controls (the editor's panel and the window on foot); true if something changed
bool radioControls(gmap::Entity& e);
// the window you get pressing F at a radio
void radioOpen(gmap::Map* m, int ent);
bool radioWindowOpen();
void radioClose();
void radioWindow();
