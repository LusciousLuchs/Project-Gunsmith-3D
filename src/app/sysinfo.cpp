// Gunsmith3D - what the game runs on (see sysinfo.hpp).
#include "app/sysinfo.hpp"
#include "raylib.h"
#include "rlgl.h"
#include "imgui.h"
#include "json.hpp"
#include "version.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace {
GpuInfo gGpu;
void keep(const char* line, const char* key, std::string& into) {
    size_t n = std::strlen(key);
    if (std::strncmp(line, key, n) == 0) { const char* v = line + n; while (*v == ' ') v++; into = v; }
}
void logLine(int level, const char* text, va_list args) {
    char buf[1024];
    std::vsnprintf(buf, sizeof buf, text, args);
    keep(buf, "    > Vendor:", gGpu.vendor);
    keep(buf, "    > Renderer:", gGpu.renderer);
    keep(buf, "    > Version:", gGpu.version);
    keep(buf, "    > GLSL:", gGpu.glsl);
    // (printed the way raylib prints its own log)
    const char* tag = level == LOG_TRACE ? "TRACE: " : level == LOG_DEBUG ? "DEBUG: " : level == LOG_INFO ? "INFO: "
                    : level == LOG_WARNING ? "WARNING: " : level == LOG_ERROR ? "ERROR: " : level == LOG_FATAL ? "FATAL: " : "";
    std::printf("%s%s\n", tag, buf);
    std::fflush(stdout);
}
}  // namespace

void installLogCapture() { SetTraceLogCallback(logLine); }
const GpuInfo& gpuInfo() { return gGpu; }

std::string requestedOpenGL() {
    switch (rlGetVersion()) {
        case RL_OPENGL_11: return "1.1";
        case RL_OPENGL_21: return "2.1";
        case RL_OPENGL_33: return "3.3 (core profile)";
        case RL_OPENGL_43: return "4.3 (core profile)";
        case RL_OPENGL_ES_20: return "ES 2.0";
        case RL_OPENGL_ES_30: return "ES 3.0";
        default: return "unknown";
    }
}

std::string aboutText() {
    char b[2048];
    const GpuInfo& g = gpuInfo();
    std::snprintf(b, sizeof b,
                  "Gunsmith3D %s\nOAB Foundry, The One Armed Bandit\n\n"
                  "raylib %s\nOpenGL %s\nDriver's OpenGL: %s\nShading language: GLSL %s\nGraphics card: %s (%s)\n"
                  "Dear ImGui %s\nnlohmann/json %d.%d.%d\n",
                  version::full(), RAYLIB_VERSION, requestedOpenGL().c_str(), g.version.empty() ? "?" : g.version.c_str(),
                  g.glsl.empty() ? "?" : g.glsl.c_str(), g.renderer.empty() ? "?" : g.renderer.c_str(), g.vendor.empty() ? "?" : g.vendor.c_str(),
                  IMGUI_VERSION, NLOHMANN_JSON_VERSION_MAJOR, NLOHMANN_JSON_VERSION_MINOR, NLOHMANN_JSON_VERSION_PATCH);
    return b;
}
