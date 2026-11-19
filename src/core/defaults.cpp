// Gunsmith3D - built-in defaults (see defaults.hpp).
#include "defaults.hpp"
#include <cstring>
#include <fstream>
#include <sstream>

namespace defaults {
namespace {
#include "builtin_defaults.inc"
std::string gDir;
std::string readFile(const std::string& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return "";
    std::stringstream ss; ss << in.rdbuf();
    return ss.str();
}
}  // namespace
void setOverrideDir(const std::string& dir) { gDir = dir; }
std::string overrideDir() { return gDir; }
std::string mapText(const std::string& kind) {
    if (!gDir.empty()) { std::string t = readFile(gDir + "/maps/" + kind + ".gsmap"); if (!t.empty()) return t; }
    for (const BuiltinDefault* b = BUILTIN_MAPS; b->kind; b++)
        if (kind == b->kind) return std::string((const char*)b->data, b->size);
    return "";
}
std::string settingsText() {
    if (!gDir.empty()) { std::string t = readFile(gDir + "/settings.json"); if (!t.empty()) return t; }
    return std::string((const char*)SETTINGS_DEFAULT, SETTINGS_DEFAULT_SIZE);
}
}  // namespace defaults
