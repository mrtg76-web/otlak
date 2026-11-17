// 4 operatörlü FM davul sesi, 6 algoritma
#pragma once
#include "Otlak.h"

namespace otlak {

class FmVoice {
public:
    void prepare(double sampleRate) { sr = sampleRate; }
    void trigger(const Sound& s, float vel);
    void fadeOut();
    bool active() const { return on; }
    // çıktıyı out'a EKLER
    void render(float* out, int n);

private:
    double sr = 48000.0;
    bool on = false;
    // tetiklemede hesaplanan sabitler
    int algo = 0;
    double f0 = 100, ratio = 1, r3 = 2, r4 = 3, I0 = 0, fbk = 0, cP = 0;
    double ka = 0, ki = 0, kp = 0, foldG = 1, q = 0, inv = 1.0 / 48000.0;
    float fold = 0, vel = 1;
    int hold = 1, att = 1, total = 0, pos = 0;
    // durum
    double p1 = 0, p2 = 0, p3 = 0, p4 = 0, m1 = 0, m2 = 0, a = 1, ie = 1, pe = 1;
    float held = 0;
    // SUB (temiz sinüs gövde) ve NOISE (filtreli beyaz gürültü)
    double subAmt = 0, sa = 1, kS = 0, noiseAmt = 0, na = 1, kN = 0;
    double svA1 = 0, svA2 = 0, svA3 = 0, svK = 1, ic1 = 0, ic2 = 0, fmMix = 1;
    unsigned int seed = 0x12345u;
    float fadeGain = 1, fadeStep = 0;
};

// Bir kanal: iki yuva, yeni vuruş eskisini 3 ms'de susturur (choke)
class Channel {
public:
    void prepare(double sr) { for (auto& v : slot) v.prepare(sr); }
    void trigger(const Sound& s, float vel)
    {
        slot[cur].fadeOut();
        cur ^= 1;
        slot[cur].trigger(s, vel);
    }
    void render(float* out, int n) { for (auto& v : slot) v.render(out, n); }
    bool active() const { return slot[0].active() || slot[1].active(); }

private:
    FmVoice slot[2];
    int cur = 0;
};

// Test ve önizleme için: bir vuruşu baştan sona hesapla
int renderHit(const Sound& s, float vel, double sr, float* out, int maxLen);

} // namespace otlak
