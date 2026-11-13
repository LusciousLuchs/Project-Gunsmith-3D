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
Pool gPistol, gRifle, gCrack, gAction, gBoom, gTail;
Pool gBoltLift, gBoltBack, gBoltFwd, gBoltLock;
Pool gSlideBack, gSlideFwd;   // the owner's slide racked by hand (slide-racking): drawn back to its stop, sent home   // the owner's Mosin worked by hand: lifted, pulled back, pushed home, turned down
Sound fromSamples(const std::vector<float>& y, int rate) {
    short* d = (short*)MemAlloc((unsigned)(y.size() * sizeof(short)));
    for (size_t i = 0; i < y.size(); i++) d[i] = (short)(std::clamp(y[i], -1.f, 1.f) * 32000);
    Wave w{(unsigned)y.size(), (unsigned)rate, 16, 1, d};
    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}
void addSynth(Pool& p, const std::vector<float>& y) {
    Sound s = fromSamples(y, 48000);
    std::vector<Sound> v{s};
    for (int k = 0; k < 3; k++) v.push_back(LoadSoundAlias(s));
    p.voices.push_back(v);
}
}
static void loadRecordings() {
    for (auto& b : BUILTIN_SND) {
        Wave w = LoadWaveFromMemory(".ogg", b.data, b.size);
        if (!w.frameCount) continue;
        Sound s = LoadSoundFromWave(w);
        UnloadWave(w);
        std::string n = b.name;
        Pool* p = n.rfind("pistol_", 0) == 0 ? &gPistol : n.rfind("rifle_", 0) == 0 ? &gRifle : n.rfind("crack_", 0) == 0 ? &gCrack : n.rfind("action_", 0) == 0 ? &gAction
                : n.rfind("bolt_lift_", 0) == 0 ? &gBoltLift : n.rfind("bolt_back_", 0) == 0 ? &gBoltBack : n.rfind("bolt_fwd_", 0) == 0 ? &gBoltFwd : n.rfind("bolt_lock_", 0) == 0 ? &gBoltLock
                : n.rfind("slide_back_", 0) == 0 ? &gSlideBack : n.rfind("slide_fwd_", 0) == 0 ? &gSlideFwd : nullptr;
        if (!p) { UnloadSound(s); continue; }
        std::vector<Sound> v{s};
        int extra = p == &gPistol || p == &gRifle || p == &gCrack ? 3 : 1;   // shots ring on: more voices for quick fire
        for (int k = 0; k < extra; k++) v.push_back(LoadSoundAlias(s));
        p->voices.push_back(v);
    }
    for (int k = 0; k < 3; k++) { addSynth(gBoom, synthBoom(k, 48000)); addSynth(gTail, synthTail(k, 48000)); }
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
    if (m.boomGain > 0) gBoom.play(g * m.boomGain, m.boomPitch);
    if (m.tailGain > 0) gTail.play(g * m.tailGain, m.tailPitch);
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
// The action's sounds, chosen by how it moves, not by what kind of gun it is. Driven by the shot (a slide, a carrier:
// fast), the recorded slide on its events; worked at hand speed, the recorded bolt from the breech's own motion: a
// clack when it stops at the back, another as it arrives home, and clicks as a turning bolt lifts and locks.
namespace {
struct ActionWatch {
    const void* sim = nullptr;
    double homeX = 0, homeY = 0;     // the breech where it started (in battery), m
    double peak = 0;                 // its fastest lately (m/s), fading over about 0.15 s
    double lastT = 0, lastD = 0, lastV = 0, lastRoll = -1;
    bool out = false;                // it has been opened since it was last home
} gW;
const sim::Body* breechRoot(const sim::Sim& S) { return S.BR ? (S.BR->root ? S.BR->root : S.BR) : nullptr; }
bool byHand() { return gW.peak < 2.5 && !gBoltBack.empty() && !gBoltFwd.empty(); }
float boltPitch(const sim::Sim& S) { return (float)std::clamp(std::pow(std::max(0.05, actionMass(S)) / 0.35, -0.12), 0.8, 1.25); }   // (the Mosin's bolt: about 0.35 kg)
}  // namespace
void actionEventSound(const sim::Sim& S, sim::Ev kind) {
    (void)kind;
    if (!SFX.ok || !A.sound) return;
    if (gW.sim == &S && byHand()) return;   // (worked by hand: its motion makes the sounds, below)
    playAction(actionMass(S));
}
void actionRollSound(const sim::Sim& S) {
    const sim::Body* B = breechRoot(S);
    if (!B) { gW.sim = nullptr; return; }
    if (gW.sim != (const void*)&S) {   // a new gun: where its breech starts is home
        gW = ActionWatch(); gW.sim = &S; gW.homeX = B->x; gW.homeY = B->y; gW.lastT = S.t;
        return;
    }
    double dt = std::max(1e-6, S.t - gW.lastT);
    double speed = std::hypot(B->vx, B->vy);
    gW.peak = std::max(speed, gW.peak * std::exp(-dt / 0.15));
    double d = std::hypot(B->x - gW.homeX, B->y - gW.homeY);
    double v = (d - gW.lastD) / dt;   // + opening, - closing
    bool sound = SFX.ok && A.sound && S.t > gW.lastT;
    float g = 0.9f * CFG.volMechanical;
    if (sound && byHand()) {
        // a part that slides straight (a slide, a straight-pull bolt): the racked slide; one that turns to lock: the bolt
        bool turns = B->d && B->d->rollOn;
        Pool& back = !turns && !gSlideBack.empty() ? gSlideBack : gBoltBack;
        Pool& home = !turns && !gSlideFwd.empty() ? gSlideFwd : gBoltFwd;
        if (d > 0.012 && gW.lastV > 0.05 && v <= 0.02 && !gW.out) { back.play(g, boltPitch(S)); gW.out = true; }   // stopped at the back
        if (gW.out && d < 0.002 && gW.lastD >= 0.002) { home.play(g, boltPitch(S)); gW.out = false; }              // home
        if (B->d && B->d->rollOn) {   // a bolt that turns to lock
            double span = (B->d->rollMax - B->d->rollMin) * DEG2RAD;
            if (std::fabs(span) > 1e-6) {
                double f = (B->roll - B->d->rollMin * DEG2RAD) / span;
                if (gW.lastRoll >= 0 && gW.lastRoll < 0.6 && f >= 0.6 && !gBoltLift.empty()) gBoltLift.play(g * 0.85f, 1);
                if (gW.lastRoll > 0.3 && f <= 0.3 && !gBoltLock.empty()) gBoltLock.play(g * 0.85f, 1);
                gW.lastRoll = f;
            }
        }
    } else if (d < 0.002) gW.out = false;
    if (S.t > gW.lastT) { gW.lastD = d; gW.lastV = v; gW.lastT = S.t; }
}
double actionMass(const sim::Sim& S) {
    if (!S.BR) return -1;
    return (S.BR->root ? S.BR->root : S.BR)->m;   // (a root carries its welded pieces' mass)
}
