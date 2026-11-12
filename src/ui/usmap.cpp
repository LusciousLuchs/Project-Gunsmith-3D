// Gunsmith3D - a map of the US to pick a city on (career properties): the lower 48's outline and a dot per city.
#include "ui/ui.hpp"
#include "places.hpp"
#include "app/settings.hpp"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <vector>

bool usMapPicker(const char* id, int& city, float width) {
    const double lon0 = -125.5, lon1 = -66.0, lat0 = 24.0, lat1 = 50.0, k = std::cos(38.0 * 3.14159265358979 / 180);
    float h = (float)(width * (lat1 - lat0) / ((lon1 - lon0) * k));
    ImGui::PushID(id);
    ImVec2 o = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##map", {width, h});
    bool hov = ImGui::IsItemHovered(), clicked = ImGui::IsItemClicked();
    auto* dl = ImGui::GetWindowDrawList();
    auto P = [&](double lon, double lat) { return ImVec2(o.x + (float)((lon - lon0) / (lon1 - lon0)) * width, o.y + (float)((lat1 - lat) / (lat1 - lat0)) * h); };
    dl->AddRectFilled(o, {o.x + width, o.y + h}, IM_COL32(26, 34, 44, 255), 6);
    const auto& out = places::usOutline();
    std::vector<ImVec2> pts;
    for (auto& [lon, lat] : out) pts.push_back(P(lon, lat));
    // (the outline isn't convex: filled a row of pixels at a time, between where the row crosses it)
    for (float y = o.y + 0.5f; y < o.y + h; y += 1) {
        std::vector<float> xs;
        for (size_t i = 0; i + 1 < pts.size(); i++) {
            ImVec2 a = pts[i], b = pts[i + 1];
            if ((a.y <= y) != (b.y <= y)) xs.push_back(a.x + (y - a.y) / (b.y - a.y) * (b.x - a.x));
        }
        std::sort(xs.begin(), xs.end());
        for (size_t i = 0; i + 1 < xs.size(); i += 2) dl->AddRectFilled({xs[i], y - 0.5f}, {xs[i + 1], y + 0.5f}, IM_COL32(62, 78, 64, 255));
    }
    dl->AddPolyline(pts.data(), (int)pts.size(), IM_COL32(150, 175, 150, 255), ImDrawFlags_Closed, 1.5f);
    const auto& cs = places::cities();
    int hot = -1; float hd = 14;
    ImVec2 m = ImGui::GetMousePos();
    for (int i = 0; i < (int)cs.size(); i++) {
        ImVec2 p = P(cs[i].lon, cs[i].lat);
        float d = std::hypot(p.x - m.x, p.y - m.y);
        if (hov && d < hd) { hd = d; hot = i; }
    }
    for (int i = 0; i < (int)cs.size(); i++) {
        ImVec2 p = P(cs[i].lon, cs[i].lat);
        bool on = i == city;
        dl->AddCircleFilled(p, on ? 5.5f : i == hot ? 4.5f : 3.0f, on ? IM_COL32(255, 196, 70, 255) : i == hot ? IM_COL32(255, 240, 200, 255) : IM_COL32(210, 220, 230, 200));
    }
    if (city >= 0 && city < (int)cs.size()) {
        ImVec2 p = P(cs[city].lon, cs[city].lat);
        dl->AddText({p.x + 8, p.y - 8}, IM_COL32(255, 220, 140, 255), (std::string(cs[city].name) + ", " + cs[city].state).c_str());
    }
    bool changed = false;
    if (hot >= 0) {
        ImGui::SetTooltip("%s, %s %s", cs[hot].name, cs[hot].state, cs[hot].zip);
        if (clicked) { city = hot; changed = true; }
    }
    ImGui::PopID();
    return changed;
}
