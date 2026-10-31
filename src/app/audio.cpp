// Gunsmith3D - Synthesised sound effects (no asset files).
#include "app/app.hpp"
#include "app/audio.hpp"

#include <algorithm>
#include <cmath>

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
void initSfx() {
    InitAudioDevice();
    if (!IsAudioDeviceReady()) return;
    SFX.bang = synth(0.45f, 9.f, 0.35f, 1.6f, 0);
    SFX.clack = synth(0.09f, 55.f, 0.8f, 0.7f, 1800);
    SFX.click = synth(0.05f, 90.f, 0.9f, 0.5f, 2600);
    SFX.tick = synth(0.03f, 160.f, 0.5f, 0.25f, 1200);
    SFX.ok = true;
}
void uiTick() { if (SFX.ok && A.sound) PlaySound(SFX.tick); }
