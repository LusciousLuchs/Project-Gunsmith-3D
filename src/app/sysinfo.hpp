// Gunsmith3D - what the game runs on, for the About window: the graphics driver's own report (caught from raylib's
// start-up log, which asks the driver), and the versions of the libraries built in.
#pragma once
#include <string>

struct GpuInfo {
    std::string vendor, renderer, version, glsl;   // as the driver reports them ("" until the window is open)
};
void installLogCapture();          // before InitWindow: raylib's log still prints as before, and the driver's report is kept
const GpuInfo& gpuInfo();
std::string requestedOpenGL();     // the OpenGL the game is built to use, e.g. "3.3 (core profile)"
std::string aboutText();           // everything in the About window, as plain text (to copy into a bug report)
