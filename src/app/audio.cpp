#include "app/audio.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace kara {
namespace {

constexpr float kTwoPi = 6.28318530718f;

struct Noise {
    uint32_t s = 0x12345678u;
    float next() {
        s = s * 1664525u + 1013904223u;
        return float(s >> 8) / float(1u << 23) - 1.f;
    }
};

// One-pole low-pass filter.
struct LowPass {
    float y = 0.f;
    float operator()(float x, float k) { return y += (x - y) * k; }
};

// Two-pole resonator (a formant or a band-pass), retunable every sample.
struct Reson {
    float y1 = 0.f, y2 = 0.f;
    float operator()(float x, float freq, float bandwidth, float rate) {
        const float r = std::exp(-3.14159265f * bandwidth / rate);
        const float c = 2.f * r * std::cos(kTwoPi * freq / rate);
        const float y = x * (1.f - r) + c * y1 - r * r * y2;
        y2 = y1;
        y1 = y;
        return y;
    }
};

float smoothstep(float e0, float e1, float x) {
    const float t = std::clamp((x - e0) / (e1 - e0), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

}  // namespace

bool Audio::init() {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        SDL_Log("Audio unavailable: %s", SDL_GetError());
        return false;
    }
    SDL_AudioSpec want{}, have{};
    want.freq = 44100;
    want.format = AUDIO_F32SYS;
    want.channels = 1;
    want.samples = 512;
    want.callback = &Audio::callback;
    want.userdata = this;
    device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (!device_) {
        SDL_Log("No audio device: %s", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }
    rate_ = have.freq;
    synthesize();
    voices_.reserve(32);
    SDL_PauseAudioDevice(device_, 0);
    return true;
}

void Audio::shutdown() {
    if (device_) {
        SDL_CloseAudioDevice(device_);
        device_ = 0;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
}

void Audio::play(Sfx sfx, float volume, float pitch) {
    if (!device_ || muted_) return;
    SDL_LockAudioDevice(device_);
    if (voices_.size() < 24) voices_.push_back(Voice{int(sfx), 0.0, std::clamp(pitch, 0.25f, 4.f), volume});
    SDL_UnlockAudioDevice(device_);
}

void SDLCALL Audio::callback(void* user, Uint8* stream, int len) {
    auto* self = static_cast<Audio*>(user);
    float* out = reinterpret_cast<float*>(stream);
    const size_t frames = size_t(len) / sizeof(float);
    std::fill(out, out + frames, 0.f);
    for (size_t v = 0; v < self->voices_.size();) {
        Voice& voice = self->voices_[v];
        const std::vector<float>& snd = self->sounds_[voice.sound];
        const double last = double(snd.size()) - 1.0;
        size_t i = 0;
        for (; i < frames && voice.pos < last; ++i) {
            const size_t k = size_t(voice.pos);
            const float f = float(voice.pos - double(k));
            out[i] += (snd[k] + (snd[k + 1] - snd[k]) * f) * voice.volume;
            voice.pos += double(voice.step);
        }
        if (voice.pos >= last) {
            self->voices_[v] = self->voices_.back();
            self->voices_.pop_back();
        } else {
            ++v;
        }
    }
    // Soft clip so stacked hits saturate instead of crackling.
    for (size_t i = 0; i < frames; ++i) out[i] = std::tanh(out[i] * 1.2f) * 0.85f;
}

void Audio::synthesize() {
    const float rate = float(rate_);
    auto make = [&](float seconds) { return std::vector<float>(size_t(seconds * rate)); };
    Noise noise;

    // Whooshes: band-passed noise sweeping up and back down.
    auto swish = [&](std::vector<float>& s, float lo, float hi, float gain) {
        Reson bp;
        const float len = float(s.size()) / rate;
        for (size_t i = 0; i < s.size(); ++i) {
            const float t = float(i) / rate, k = t / len;
            const float f = lo + (hi - lo) * std::sin(3.14159f * k);
            const float env = std::sin(3.14159f * std::pow(k, 0.6f));
            s[i] = bp(noise.next(), f, f * 0.5f, rate) * env * gain;
        }
    };
    swish(sounds_[int(Sfx::Swish)] = make(0.13f), 700.f, 2600.f, 2.2f);
    swish(sounds_[int(Sfx::SwishHeavy)] = make(0.24f), 400.f, 1800.f, 2.6f);
    swish(sounds_[int(Sfx::Jump)] = make(0.12f), 300.f, 900.f, 1.2f);
    swish(sounds_[int(Sfx::Roll)] = make(0.3f), 200.f, 600.f, 1.6f);

    // Impacts: a pitched thump under a slap of noise.
    auto impact = [&](std::vector<float>& s, float f0, float f1, float decay, float slap, float gain) {
        LowPass lp;
        float phase = 0.f;
        for (size_t i = 0; i < s.size(); ++i) {
            const float t = float(i) / rate;
            phase += kTwoPi * (f1 + (f0 - f1) * std::exp(-t * 30.f)) / rate;
            const float body = std::sin(phase) * std::exp(-t * decay);
            const float n = lp(noise.next(), 0.5f) * std::exp(-t * 60.f) * slap;
            s[i] = (body + n) * std::min(1.f, t * 2000.f) * gain;
        }
    };
    impact(sounds_[int(Sfx::Hit)] = make(0.2f), 220.f, 80.f, 22.f, 1.6f, 0.7f);
    impact(sounds_[int(Sfx::HeavyHit)] = make(0.38f), 180.f, 52.f, 11.f, 2.2f, 0.9f);
    impact(sounds_[int(Sfx::Land)] = make(0.1f), 120.f, 60.f, 40.f, 0.5f, 0.35f);
    impact(sounds_[int(Sfx::Thud)] = make(0.35f), 110.f, 45.f, 12.f, 1.4f, 0.8f);

    {  // Block: a hard clack.
        auto& s = sounds_[int(Sfx::Block)] = make(0.12f);
        Reson a, b;
        for (size_t i = 0; i < s.size(); ++i) {
            const float t = float(i) / rate;
            const float n = noise.next() * std::exp(-t * 90.f);
            s[i] = (a(n, 1800.f, 300.f, rate) * 3.f + b(n, 3400.f, 500.f, rate) * 2.f +
                    std::sin(kTwoPi * 950.f * t) * 0.3f * std::exp(-t * 50.f)) *
                   0.8f;
        }
    }
    // Wood: cracks of resonant noise.
    auto crack = [&](std::vector<float>& s, float start, float gain) {
        Reson a, b, c;
        for (size_t i = size_t(start * rate); i < s.size(); ++i) {
            const float t = float(i) / rate - start;
            const float n = noise.next() * std::exp(-t * 45.f);
            s[i] += (a(n, 520.f, 120.f, rate) * 2.5f + b(n, 1350.f, 260.f, rate) * 2.f + c(n, 2900.f, 600.f, rate)) *
                    gain;
        }
    };
    crack(sounds_[int(Sfx::Crack)] = make(0.18f), 0.f, 1.f);
    {
        auto& s = sounds_[int(Sfx::Crash)] = make(0.9f);
        const float starts[7] = {0.f, 0.05f, 0.09f, 0.16f, 0.27f, 0.41f, 0.58f};
        for (int k = 0; k < 7; ++k) crack(s, starts[k], 1.1f - 0.13f * float(k));
    }
    {  // Glass: bright shards tinkling down.
        auto& s = sounds_[int(Sfx::Glass)] = make(0.8f);
        for (int k = 0; k < 14; ++k) {
            const float start = 0.02f * float(k) + 0.03f * float(k % 3) * float(k % 2);
            const float f = 2600.f + 3200.f * float((k * 7919) % 100) / 100.f;
            for (size_t i = size_t(start * rate); i < s.size(); ++i) {
                const float t = float(i) / rate - start;
                s[i] += std::sin(kTwoPi * f * t) * std::exp(-t * 18.f) * 0.09f;
            }
        }
        for (size_t i = 0; i < s.size(); ++i) {
            const float t = float(i) / rate;
            s[i] += noise.next() * std::exp(-t * 25.f) * 0.35f;
        }
    }
    // Shouts: a buzzy voice through moving formants, "hyah!" and "hai!".
    auto kiai = [&](std::vector<float>& s, float pitch, float f2end) {
        Reson f1, f2, f3;
        float phase = 0.f;
        const float len = float(s.size()) / rate;
        for (size_t i = 0; i < s.size(); ++i) {
            const float t = float(i) / rate, k = t / len;
            const float f0 = pitch * (1.f + 0.25f * std::sin(3.14159f * std::min(1.f, k * 1.6f))) *
                             (1.f + 0.02f * std::sin(kTwoPi * 30.f * t));
            phase += f0 / rate;
            phase -= std::floor(phase);
            const float glottal = (phase < 0.6f ? std::sin(3.14159f * phase / 0.6f) : 0.f) - 0.38f;
            const float breath = noise.next() * (t < 0.05f ? 1.f : 0.15f);
            const float src = t < 0.04f ? breath : glottal + breath * 0.3f;
            const float m = smoothstep(0.08f, 0.2f, t);
            const float F1 = 300.f + (800.f - 300.f) * m, F2 = 2200.f + (f2end - 2200.f) * m, F3 = 2900.f;
            float v = f1(src, F1, 90.f, rate) * 1.4f + f2(src, F2, 140.f, rate) * 0.9f + f3(src, F3, 200.f, rate) * 0.4f;
            const float env = std::min(1.f, t * 40.f) * (k < 0.55f ? 1.f : std::exp(-(k - 0.55f) * 9.f));
            s[i] = v * env * 2.2f;
        }
    };
    kiai(sounds_[int(Sfx::Kiai0)] = make(0.34f), 190.f, 1250.f);
    kiai(sounds_[int(Sfx::Kiai1)] = make(0.3f), 285.f, 1500.f);

    // Bells: inharmonic partials.
    auto bell = [&](std::vector<float>& s, float f, float decay, float gain, float start) {
        const float partials[4] = {1.f, 2.76f, 5.4f, 8.93f};
        const float amps[4] = {1.f, 0.6f, 0.35f, 0.2f};
        for (size_t i = size_t(start * rate); i < s.size(); ++i) {
            const float t = float(i) / rate - start;
            float v = 0.f;
            for (int p = 0; p < 4; ++p)
                v += std::sin(kTwoPi * f * partials[p] * t) * amps[p] * std::exp(-t * decay * (1.f + float(p) * 0.8f));
            s[i] += v * std::min(1.f, t * 1500.f) * gain;
        }
    };
    {
        auto& s = sounds_[int(Sfx::Bell)] = make(1.3f);
        bell(s, 1046.5f, 3.2f, 0.18f, 0.f);
        bell(s, 1046.5f, 3.2f, 0.16f, 0.16f);
    }
    {  // Gong for a knockout: low, slow, beating.
        auto& s = sounds_[int(Sfx::Gong)] = make(2.4f);
        const float fs[5] = {98.f, 101.f, 163.f, 231.f, 311.f};
        const float as[5] = {0.5f, 0.45f, 0.35f, 0.25f, 0.18f};
        for (size_t i = 0; i < s.size(); ++i) {
            const float t = float(i) / rate;
            float v = 0.f;
            for (int p = 0; p < 5; ++p) v += std::sin(kTwoPi * fs[p] * t) * as[p] * std::exp(-t * (1.2f + float(p) * 0.4f));
            v += noise.next() * 0.15f * std::exp(-t * 30.f);
            s[i] = v * std::min(1.f, t * 300.f) * 0.8f;
        }
    }
    {  // Tick: menu navigation.
        auto& s = sounds_[int(Sfx::Tick)] = make(0.035f);
        for (size_t i = 0; i < s.size(); ++i) {
            const float t = float(i) / rate;
            s[i] = std::sin(kTwoPi * 1250.f * t) * std::exp(-t * 140.f) * 0.10f;
        }
    }
    {  // Fanfare: a bright rising arpeggio for the winner.
        auto& s = sounds_[int(Sfx::Fanfare)] = make(1.4f);
        const float notes[6] = {392.f, 523.25f, 659.25f, 783.99f, 659.25f, 1046.5f};
        const float starts[6] = {0.f, 0.11f, 0.22f, 0.33f, 0.50f, 0.58f};
        for (int n = 0; n < 6; ++n) {
            for (size_t i = size_t(starts[n] * rate); i < s.size(); ++i) {
                const float t = float(i) / rate - starts[n];
                const float env = std::exp(-t * (n == 5 ? 2.5f : 7.f)) * std::min(1.f, t * 300.f);
                const float ph = kTwoPi * notes[n] * t;
                // A square-ish chiptune voice.
                const float sq = std::sin(ph) + 0.33f * std::sin(3.f * ph) + 0.2f * std::sin(5.f * ph);
                s[i] += sq * env * 0.09f;
            }
        }
    }
}

}  // namespace kara
