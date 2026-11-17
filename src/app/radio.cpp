// Gunsmith3D - radios on maps (see radio.hpp). The streams come from core/netradio; this mixes every radio you can
// hear into one sound stream on the audio thread, each with its own left/right gain and low-pass that follow where
// you stand and look (set each frame by radioListen, eased in the mix so nothing clicks).
#include "app/radio.hpp"
#include "app/app.hpp"
#include "app/gfx.hpp"
#include "app/settings.hpp"
#include "doors.hpp"
#include "netradio.hpp"
#include "imgui.h"
#include "raymath.h"

#include <algorithm>
#include <cmath>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <cstdlib>
#include <vector>

using gmap::V3;

namespace {
struct Source {          // one radio on the map, as heard
    std::string url;
    float l = 0, r = 0, a = 1;        // targets: left and right gain, low-pass coefficient
    float cl = 0, cr = 0, ca = 1;     // as mixed now (eased toward the targets)
    float zl = 0, zr = 0;             // the low-pass's memory
    bool live = false;                // heard this frame
};
struct Station { std::unique_ptr<netradio::Player> p; double lastHeard = 0; };
struct Mix {
    std::mutex m;
    std::map<std::string, Source> src;       // by radio (entity index + link)
    std::map<std::string, Station> st;       // by link: radios on one station share it (they play in step)
    float master = 1;
    std::vector<float> tmp;
} gMix;
AudioStream gStream{};
bool gOk = false;
bool gSilent = false;    // (tests: no sound device; the mix still runs, pulled by radioTick, and its levels are kept)
double gLevelL = 0, gLevelR = 0, gPulled = 0;
double gListened = -1;   // when radioListen last ran

void mixCallback(void* buffer, unsigned int frames) {
    float* out = (float*)buffer;
    std::fill(out, out + frames * 2, 0.0f);
    std::lock_guard<std::mutex> l(gMix.m);
    if (gMix.tmp.size() < frames * 2) gMix.tmp.resize(frames * 2);   // (only grows the first few times)
    float* tmp = gMix.tmp.data();
    for (auto& [url, st] : gMix.st) {
        if (!st.p) continue;
        bool used = false;
        for (auto& [k, s] : gMix.src) if (s.url == url && (s.live || s.cl > 1e-4f || s.cr > 1e-4f)) { used = true; break; }
        if (!used) continue;
        st.p->read(tmp, (int)frames);   // (one read per station: radios on it share the sound)
        for (auto& [k, s] : gMix.src) {
            if (s.url != url) continue;
            float tl = s.live ? s.l * gMix.master : 0, tr = s.live ? s.r * gMix.master : 0;
            float dl = (tl - s.cl) / frames, dr = (tr - s.cr) / frames, da = (s.a - s.ca) / frames;
            for (unsigned i = 0; i < frames; i++) {
                s.cl += dl; s.cr += dr; s.ca += da;
                s.zl += s.ca * (tmp[2 * i] - s.zl);
                s.zr += s.ca * (tmp[2 * i + 1] - s.zr);
                out[2 * i] += s.zl * s.cl;
                out[2 * i + 1] += s.zr * s.cr;
            }
            s.cl = tl; s.cr = tr; s.ca = s.a;
        }
    }
    for (unsigned i = 0; i < frames * 2; i++) out[i] = out[i] / (1 + std::fabs(out[i]) * 0.25f);   // (several loud radios: soft limit)
}
}  // namespace

void radioInit() {
    if (gOk) return;
    if (!IsAudioDeviceReady()) { if (std::getenv("GUNSMITH_RADIOSILENT")) { gOk = gSilent = true; gPulled = GetTime(); } return; }
    SetAudioStreamBufferSizeDefault(2048);
    gStream = LoadAudioStream(netradio::RATE, 32, 2);
    SetAudioStreamBufferSizeDefault(0);
    if (!IsAudioStreamValid(gStream)) return;
    SetAudioStreamCallback(gStream, mixCallback);
    PlayAudioStream(gStream);
    gOk = true;
}
void radioShutdown() {
    if (!gOk) return;
    if (!gSilent) { StopAudioStream(gStream); UnloadAudioStream(gStream); }
    gOk = false;
    std::lock_guard<std::mutex> l(gMix.m);
    gMix.src.clear(); gMix.st.clear();
}

static std::string keyOf(int i, const gmap::Entity& e) { return std::to_string(i) + "|" + e.ref; }

void radioListen(const gmap::Map& m, V3 ear, V3 fwd) {
    if (!gOk) return;
    double now = GetTime();
    gListened = now;
    V3 f = gmap::norm(V3{fwd.x, 0, fwd.z});
    if (gmap::len(f) < 1e-6) f = {1, 0, 0};
    V3 right{-f.z, 0, f.x};
    std::lock_guard<std::mutex> l(gMix.m);
    gMix.master = A.sound ? CFG.volRadio : 0.0f;
    for (auto& [k, s] : gMix.src) s.live = false;
    for (int i = 0; i < (int)m.ents.size(); i++) {
        const auto& e = m.ents[i];
        if (e.type != "radio" || !e.playing || e.ref.empty()) continue;
        V3 at = e.pos + V3{0, 0.18, 0};
        V3 d = at - ear;
        double dist = gmap::len(d);
        if (dist > e.range) continue;
        V3 dir = dist > 1e-6 ? d * (1.0 / dist) : V3{1, 0, 0};
        bool blocked = false;
        if (dist > 0.6) {   // a wall or a shut door between you and it
            gmap::Hit h = gmap::rayMap(m, ear, dir, dist - 0.4);
            blocked = h.brush >= 0 || doors::ray(m, ear, dir, dist - 0.4) >= 0;
        }
        netradio::Spatial sp = netradio::spatial(gmap::dot(d, right), gmap::dot(d, f), dist, e.power, e.range, blocked);
        Source& s = gMix.src[keyOf(i, e)];
        s.url = e.ref;
        s.l = (float)sp.left; s.r = (float)sp.right;
        s.a = (float)(1 - std::exp(-2 * PI * sp.cutoff / netradio::RATE));
        s.live = true;
        Station& st = gMix.st[e.ref];
        if (!st.p) st.p = std::make_unique<netradio::Player>(e.ref);
        st.lastHeard = now;
    }
}
void radioTick() {
    if (!gOk) return;
    double now = GetTime();
    if (gSilent) {   // (tests) pull what the sound card would have, and keep how loud each ear was
        int n = std::clamp((int)((now - gPulled) * netradio::RATE), 0, netradio::RATE);
        gPulled = now;
        if (n > 0) {
            std::vector<float> buf((size_t)n * 2);
            mixCallback(buf.data(), (unsigned)n);
            double l = 0, r = 0;
            for (int i = 0; i < n; i++) { l += buf[2 * i] * buf[2 * i]; r += buf[2 * i + 1] * buf[2 * i + 1]; }
            gLevelL = std::sqrt(l / n); gLevelR = std::sqrt(r / n);
        }
    }
    std::lock_guard<std::mutex> l(gMix.m);
    if (now - gListened > 0.25) for (auto& [k, s] : gMix.src) s.live = false;   // (a screen without radios: they fade out)
    for (auto it = gMix.src.begin(); it != gMix.src.end();)   // radios gone quiet and faded: forgotten
        if (!it->second.live && it->second.cl < 1e-4f && it->second.cr < 1e-4f) it = gMix.src.erase(it); else ++it;
    for (auto it = gMix.st.begin(); it != gMix.st.end();) {   // stations nobody has heard for a while: hung up
        bool used = std::any_of(gMix.src.begin(), gMix.src.end(), [&](auto& kv) { return kv.second.url == it->first; });
        if (!used && now - it->second.lastHeard > 4) it = gMix.st.erase(it); else ++it;
    }
}

static const netradio::Player* playerFor(const std::string& url) {
    auto it = gMix.st.find(url);
    return it == gMix.st.end() ? nullptr : it->second.p.get();
}
std::string radioState(const gmap::Entity& e) {
    if (!e.playing) return "Off";
    if (!gOk) return "No sound device";
    if (e.ref.empty()) return "No station chosen";
    std::lock_guard<std::mutex> l(gMix.m);
    const netradio::Player* p = playerFor(e.ref);
    return p ? p->state() : "Out of earshot";
}
void radioLevels(double& l, double& r) { l = gLevelL; r = gLevelR; }
std::string radioSong(const gmap::Entity& e) {
    std::lock_guard<std::mutex> l(gMix.m);
    const netradio::Player* p = playerFor(e.ref);
    return p && e.playing ? p->title() : "";
}

int radioFacing(const gmap::Map& m, V3 eye, V3 fwd) {
    int best = -1;
    double bd = 1e9;
    for (int i = 0; i < (int)m.ents.size(); i++) {
        if (m.ents[i].type != "radio") continue;
        V3 d = m.ents[i].pos + V3{0, 0.15, 0} - eye;
        double l = gmap::len(d);
        if (l > 2.3 || l < 1e-6) continue;
        if (gmap::dot(d * (1.0 / l), gmap::norm(fwd)) < 0.88) continue;   // (looking at it: within ~28 degrees)
        if (l < bd) { bd = l; best = i; }
    }
    return best;
}

gmap::Entity newRadio() {
    gmap::Entity e;
    e.type = "radio";
    const auto& b = netradio::builtinStations();
    if (!b.empty()) { e.ref = b[0].url; e.label = b[0].name; }
    e.power = 0.6; e.range = 20; e.playing = true;
    return e;
}

void drawRadio(Vector3 base, float yawDeg, Color tint, bool on) {
    // a small boombox: a body with two speakers on the front, a dial between them, a handle on top
    Matrix R = MatrixRotateY(-yawDeg * DEG2RAD);
    auto at = [&](float x, float y, float z) { return Vector3Add(base, Vector3Transform({x, y, z}, R)); };
    auto box = [&](float x, float y, float z, float sx, float sy, float sz, Color c) { drawBox(at(x, y, z), {sx, sy, sz}, tint.a ? tint : c, R); };
    const Color body{46, 50, 56, 255}, grille{24, 26, 30, 255}, chrome{170, 176, 184, 255}, dial{on ? (unsigned char)255 : (unsigned char)90, on ? (unsigned char)176 : (unsigned char)90, 60, 255};
    box(0, 0.13f, 0, 0.13f, 0.24f, 0.46f, body);
    for (float z : {-0.14f, 0.14f}) {   // the speakers: a grille ring and its cone
        drawCyl(at(0.062f, 0.12f, z), at(0.07f, 0.12f, z), 0.075f, tint.a ? tint : grille);
        drawCyl(at(0.066f, 0.12f, z), at(0.074f, 0.12f, z), 0.03f, tint.a ? tint : chrome);
    }
    box(0.068f, 0.19f, 0, 0.01f, 0.04f, 0.12f, dial);              // the station dial (lit when it's on)
    box(0, 0.27f, 0, 0.03f, 0.02f, 0.34f, chrome);                // the handle
    for (float z : {-0.16f, 0.16f}) box(0, 0.255f, z, 0.03f, 0.03f, 0.02f, chrome);
    box(-0.02f, 0.32f, 0.17f, 0.006f, 0.12f, 0.006f, chrome);     // the aerial
}

// ---------------------------------------------------------------- controls
namespace {
struct Search {
    char query[96] = "";
    std::future<std::vector<netradio::StationInfo>> job;
    std::vector<netradio::StationInfo> found;
    std::string err;
    bool busy = false;
} gSearch;
char gLink[512] = "";
gmap::Map* gWinMap = nullptr;
int gWinEnt = -1;
}

bool radioControls(gmap::Entity& e) {
    bool changed = false;
    ImGui::PushID("radio");
    if (ImGui::Checkbox("On", &e.playing)) changed = true;
    ImGui::SameLine();
    std::string st = radioState(e), song = radioSong(e);
    ImGui::TextDisabled("%s", st.c_str());
    ImGui::TextWrapped("%s", e.label.empty() ? e.ref.c_str() : e.label.c_str());
    if (!song.empty()) ImGui::TextWrapped("Now: %s", song.c_str());
    float vol = (float)e.power;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##vol", &vol, 0, 1, "Volume %.2f")) { e.power = vol; changed = true; }
    float reach = (float)e.range;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##reach", &reach, 3, 80, "Heard up to %.0f m")) { e.range = reach; changed = true; }
    ImGui::Spacing();
    auto pick = [&](const netradio::StationInfo& s) { e.ref = s.url; e.label = s.name; e.playing = true; changed = true; };
    if (ImGui::BeginCombo("##stations", "Stations")) {
        for (auto& s : netradio::builtinStations())
            if (ImGui::Selectable((s.name + "  (" + s.tags + ")").c_str(), s.url == e.ref)) pick(s);
        ImGui::EndCombo();
    }
    // the public station directory: by name or genre
    ImGui::SetNextItemWidth(-80);
    bool go = ImGui::InputTextWithHint("##q", "Find: jazz, rock, a station's name...", gSearch.query, sizeof gSearch.query, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    go |= ImGui::Button(gSearch.busy ? "..." : "Find", {-1, 0});
    if (go && !gSearch.busy && gSearch.query[0]) {
        std::string q = gSearch.query;
        gSearch.busy = true; gSearch.err.clear();
        gSearch.job = std::async(std::launch::async, [q] { std::string err; auto r = netradio::searchStations(q, 30, &err); if (r.empty() && !err.empty()) r.push_back({"(" + err + ")", "", "", "", "", 0}); return r; });
    }
    if (gSearch.busy && gSearch.job.valid() && gSearch.job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        gSearch.found = gSearch.job.get();
        gSearch.busy = false;
        if (gSearch.found.empty()) gSearch.err = "Nothing found (MP3 stations only).";
    }
    if (!gSearch.err.empty()) ImGui::TextDisabled("%s", gSearch.err.c_str());
    if (!gSearch.found.empty() && ImGui::BeginListBox("##found", {-1, 150})) {
        for (auto& s : gSearch.found) {
            if (s.url.empty()) { ImGui::TextDisabled("%s", s.name.c_str()); continue; }
            std::string label = s.name + (s.country.empty() ? "" : "  [" + s.country + "]") + (s.tags.empty() ? "" : "  " + s.tags.substr(0, 40));
            if (ImGui::Selectable((label + "##" + s.url).c_str(), s.url == e.ref)) pick(s);
        }
        ImGui::EndListBox();
    }
    // or a link of your own
    ImGui::SetNextItemWidth(-80);
    bool use = ImGui::InputTextWithHint("##link", "Paste a stream link (MP3, .pls, .m3u)", gLink, sizeof gLink, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    use |= ImGui::Button("Play", {-1, 0});
    if (use && gLink[0]) {
        e.ref = gLink; e.label.clear(); e.playing = true; changed = true;
        gLink[0] = 0;
    }
    if (st.rfind("Error", 0) == 0) ImGui::TextColored({1, 0.55f, 0.45f, 1}, "%s", st.c_str());
    ImGui::PopID();
    return changed;
}

void radioOpen(gmap::Map* m, int ent) { gWinMap = m; gWinEnt = ent; }
bool radioWindowOpen() { return gWinMap && gWinEnt >= 0 && gWinEnt < (int)gWinMap->ents.size() && gWinMap->ents[gWinEnt].type == "radio"; }
void radioClose() { gWinMap = nullptr; gWinEnt = -1; }
void radioWindow() {
    if (!radioWindowOpen()) return;
    gmap::Entity& e = gWinMap->ents[gWinEnt];
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f}, ImGuiCond_Appearing, {0.5f, 0.5f});
    ImGui::SetNextWindowSizeConstraints({460 * ImGui::GetFontSize() / 16, 0}, {1e6f, 1e6f});
    bool open = true;
    if (ImGui::Begin("Radio", &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
        radioControls(e);
        ImGui::Spacing();
        if (ImGui::Button("Close (Esc)", {-1, 0})) open = false;
    }
    ImGui::End();
    if (!io.WantTextInput && IsKeyPressed(KEY_ESCAPE)) open = false;
    if (!open) radioClose();
}
