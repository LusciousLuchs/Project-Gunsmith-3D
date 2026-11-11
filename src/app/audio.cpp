// Gunsmith3D - Sound effects: the owner's recordings (built in, from assets/sounds) and a few synthesised clicks.
#include "app/app.hpp"
#include "app/audio.hpp"

#include "app/settings.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "app/builtin_sounds.inc"

// ---------------------------------------------------------------- sounds (synthesised, no asset files)
Sfx SFX;
static Sound synth(float seconds, float decay, float lowpass, float gain, float tone) {
    int sr = 22050, n = (int)(seconds * sr);
    short* data = (short*)MemAlloc(n * sizeof(short));
    float lp = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / sr, env = std::exp(-t * decay);
        float noise = (GetRandomValue(-1000, 1000) / 1000.f);
        lp += (noise - lp) * lowpass;
        float s = (lp + (tone > 0 ? 0.6f * std::sin(t * tone * 2 * PI) : 0)) * env * gain;
        data[i] = (short)(std::clamp(s, -1.f, 1.f) * 32000);
    }
    Wave w{(unsigned)n, (unsigned)sr, 16, 1, data};
    Sound snd = LoadSoundFromWave(w);
    UnloadWave(w);
    return snd;
}
// ---------------------------------------------------------------- the recordings
// Each recording gets a few voices (aliases of the same data) so quick shots overlap instead of cutting each other off.
namespace {
struct Pool {
    std::vector<std::vector<Sound>> voices;   // per recording: its voices (the first is the loaded sound)
    int last = -1;
    bool empty() const { return voices.empty(); }
    void play(float vol, float pitch) {
        if (voices.empty()) return;
        int n = (int)voices.size(), i = GetRandomValue(0, n - 1);
        if (n > 1 && i == last) i = (i + 1) % n;   // never the same one twice running
        last = i;
        Sound* pick = nullptr;
        for (auto& v : voices[i]) if (!IsSoundPlaying(v)) { pick = &v; break; }
        if (!pick) pick = &voices[i][GetRandomValue(0, (int)voices[i].size() - 1)];
        SetSoundVolume(*pick, std::clamp(vol, 0.f, 1.f));
        SetSoundPitch(*pick, std::clamp(pitch * (1 + GetRandomValue(-30, 30) / 1000.f), 0.5f, 2.f));  // +-3%: no two alike
        PlaySound(*pick);
    }
};
Pool gPistol, gRifle, gCrack, gAction;
}
static void loadRecordings() {
    for (auto& b : BUILTIN_SND) {
        Wave w = LoadWaveFromMemory(".ogg", b.data, b.size);
        if (!w.frameCount) continue;
        Sound s = LoadSoundFromWave(w);
        UnloadWave(w);
        std::string n = b.name;
        Pool* p = n.rfind("pistol_", 0) == 0 ? &gPistol : n.rfind("rifle_", 0) == 0 ? &gRifle : n.rfind("crack_", 0) == 0 ? &gCrack : n.rfind("action_", 0) == 0 ? &gAction : nullptr;
        if (!p) { UnloadSound(s); continue; }
        std::vector<Sound> v{s};
        int extra = p == &gAction ? 1 : 3;   // shots ring on: more voices for quick fire
        for (int k = 0; k < extra; k++) v.push_back(LoadSoundAlias(s));
        p->voices.push_back(v);
    }
    SFX.recorded = !gPistol.empty() && !gRifle.empty();
}

double barrelLength(const Design& d, int k) {
    const auto& br = d.breech;
    if (k == 0) return br.hasMuzzle && br.hasFace ? std::hypot(br.muzzle.x - br.face.x, br.muzzle.y - br.face.y) : -1;
    if (k - 1 >= (int)br.more.size()) return -1;
    const auto& c = br.more[k - 1];
    return c.hasMuzzle ? std::hypot(c.muzzle.x - c.face.x, c.muzzle.y - c.face.y) : -1;
}
void playShotMix(const ShotMix& m, float vol) {
    if (!SFX.ok || !A.sound) return;
    float g = vol * CFG.volGunfire;
    if (!SFX.recorded) { SetSoundVolume(SFX.bang, std::clamp(g, 0.f, 1.f)); PlaySound(SFX.bang); SetSoundVolume(SFX.bang, CFG.volGunfire); return; }
    if (m.pistolGain > 0) gPistol.play(g * m.pistolGain, m.pistolPitch);
    if (m.rifleGain > 0) gRifle.play(g * m.rifleGain, m.riflePitch);
    if (m.crackGain > 0 && !gCrack.empty()) gCrack.play(g * m.crackGain, 1);
}
void playShot(const Design& d, int k, float vol) {
    const auto& br = d.breech;
    const Cartridge& c = k > 0 && k - 1 < (int)br.more.size() && br.more[k - 1].ownCart ? br.more[k - 1].cart : br.cart;
    playShotMix(shotMix(c, barrelLength(d, k)), vol);
}
void playAction(double massKg, double speed, float vol) {
    if (!SFX.ok || !A.sound) return;
    if (gAction.empty()) { PlaySound(SFX.clack); return; }
    ActionMix a = actionMix(massKg, speed);
    gAction.play(vol * a.gain * CFG.volMechanical, a.pitch);
}

void initSfx() {
    InitAudioDevice();
    if (!IsAudioDeviceReady()) return;
    SFX.bang = synth(0.45f, 9.f, 0.35f, 1.6f, 0);
    SFX.clack = synth(0.09f, 55.f, 0.8f, 0.7f, 1800);
    SFX.click = synth(0.05f, 90.f, 0.9f, 0.5f, 2600);
    SFX.tick = synth(0.03f, 160.f, 0.5f, 0.25f, 1200);
    SFX.ok = true;
    loadRecordings();
}
void uiTick() { if (SFX.ok && A.sound) PlaySound(SFX.tick); }
double actionMass(const sim::Sim& S) {
    if (!S.BR) return -1;
    return (S.BR->root ? S.BR->root : S.BR)->m;   // (a root carries its welded pieces' mass)
}
