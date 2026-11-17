#include "FmVoice.h"
#include <algorithm>
#include <cmath>

namespace otlak {

static constexpr double TP = 6.283185307179586;

void FmVoice::trigger(const Sound& s, float v)
{
    const auto& p = s.p;
    inv = 1.0 / sr;
    algo = std::clamp(s.algo, 0, kAlgos - 1);
    f0 = 20.0 * std::pow(2.0, p[Pitch] * 9.0);
    ratio = 0.25 + p[Ratio] * 7.75;
    r3 = s.r3;
    r4 = s.r4;
    vel = std::clamp(v, 0.f, 1.f);
    I0 = p[Index] * 12.0 * (0.55 + 0.45 * vel);
    fbk = 0.5 * (p[Feedback] * 1.6) * 3.141592653589793;
    const double pd = p[PEnv] * 48.0;
    const double D = 0.008 * std::pow(2.0, p[Decay] * 9.0);
    subAmt = p[Sub];
    noiseAmt = p[Noise];
    const double Dsub = D * 1.8 + 0.08 * subAmt;
    const double Dn = std::max(0.004, D * 0.8);
    kS = std::exp(-1.0 / (Dsub / 6.9 * sr));
    kN = std::exp(-1.0 / (Dn / 6.9 * sr));
    sa = na = 1;
    const double fc = std::min(300.0 * std::pow(2.0, p[NTone] * 5.3), sr * 0.45);
    const double gg = std::tan(3.141592653589793 * fc / sr);
    svK = 1.0 / 0.7; // Q 0.7: geniş bant
    svA1 = 1.0 / (1.0 + gg * (gg + svK));
    svA2 = gg * svA1;
    svA3 = gg * svA2;
    ic1 = ic2 = 0;
    seed = 0x9E3779B9u ^ (unsigned int) (p[NTone] * 100000.f) ^ (unsigned int) (vel * 1000.f);
    fmMix = 1.0 - 0.4 * std::max(subAmt, noiseAmt);
    const double len = std::min(3.0, std::max(D, subAmt > 0.01 ? Dsub : 0.0) * 1.1 + 0.006);
    total = std::max(64, (int) std::ceil(len * sr));
    const double tauA = D / 6.9;
    const double tauI = tauA * 0.5 + 0.0015;
    const double tauP = std::min(0.003 + 0.03 * p[PEnv], tauA * 0.8 + 0.001);
    ka = std::exp(-1.0 / (tauA * sr));
    ki = std::exp(-1.0 / (tauI * sr));
    kp = std::exp(-1.0 / (tauP * sr));
    cP = pd / 12.0 * 0.6931471805599453;
    fold = p[Fold];
    foldG = (1.0 + fold * 7.0) * 1.5707963267948966;
    const float crush = p[Crush];
    const int bits = crush < 0.05f ? 0 : (int) std::lround(16.0 - crush * 14.0);
    q = bits ? std::pow(2.0, bits - 1) : 0.0;
    hold = crush < 0.05f ? 1 : 1 + (int) std::floor(crush * crush * 20.0);
    att = std::max(1, (int) std::lround(0.0008 * sr));
    p1 = p2 = p3 = p4 = m1 = m2 = 0;
    a = ie = pe = 1;
    held = 0;
    pos = 0;
    fadeGain = 1;
    fadeStep = 0;
    on = true;
}

void FmVoice::fadeOut()
{
    if (on) fadeStep = (float) (1.0 / (0.003 * sr));
}

void FmVoice::render(float* out, int n)
{
    if (!on) return;
    const int fadeLen = std::min(64, total);
    for (int i = 0; i < n; ++i) {
        double f = f0 * std::exp(cP * pe);
        if (f > 20000.0) f = 20000.0;
        pe *= kp;
        const double inc = f * inv;
        p1 += inc;          p1 -= std::floor(p1);
        p2 += inc * ratio;  p2 -= std::floor(p2);
        p3 += inc * r3;     p3 -= std::floor(p3);
        p4 += inc * r4;     p4 -= std::floor(p4);
        const double I = I0 * (ie * 0.92 + 0.08);
        ie *= ki;
        const double fv = (m1 + m2) * fbk;
        double y, fs, ma, mb, mc;
        switch (algo) {
            case 1: // Y: 2+3 > 1
                ma = std::sin(TP * p2 + fv); mb = std::sin(TP * p3);
                y = std::sin(TP * p1 + I * (ma + 0.7 * mb)); fs = ma; break;
            case 2: // SERIAL: 4>3>2>1
                ma = std::sin(TP * p4 + fv); mb = std::sin(TP * p3 + I * 0.6 * ma);
                mc = std::sin(TP * p2 + I * 0.7 * mb); y = std::sin(TP * p1 + I * mc); fs = ma; break;
            case 3: // PAIRS: 2>1 + 4>3
                ma = std::sin(TP * p2); mb = std::sin(TP * p4 + fv);
                y = 0.6 * std::sin(TP * p1 + I * ma) + 0.5 * std::sin(TP * p3 + I * 1.3 * mb); fs = mb; break;
            case 4: // RING: (2>1) x (4>3)
                ma = std::sin(TP * p2 + fv); mb = std::sin(TP * p4);
                y = std::sin(TP * p1 + I * ma) * std::sin(TP * p3 + I * 0.8 * mb); fs = ma; break;
            case 5: // BRANCH: 4 > 1,2,3
                ma = std::sin(TP * p4 + fv);
                y = 0.45 * (std::sin(TP * p1 + I * ma) + std::sin(TP * p2 + I * ma) + std::sin(TP * p3 + I * ma)); fs = ma; break;
            default: // 2-OP
                ma = std::sin(TP * p2 + fv); y = std::sin(TP * p1 + I * ma); fs = ma; break;
        }
        m2 = m1;
        m1 = fs;
        if (fold > 0.01f) y = std::sin(y * foldG);
        const double ramp = pos < att ? (double) pos / att : 1.0;
        y *= a * ramp * fmMix;
        a *= ka;
        if (subAmt > 0.001) { y += std::sin(TP * p1) * sa * subAmt * 0.9 * ramp; sa *= kS; }
        if (noiseAmt > 0.001) {
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
            const double w = (double) seed / 2147483648.0 - 1.0;
            const double v3 = w - ic2;
            const double v1 = svA1 * ic1 + svA2 * v3;
            const double v2 = ic2 + svA2 * ic1 + svA3 * v3;
            ic1 = 2 * v1 - ic1;
            ic2 = 2 * v2 - ic2;
            y += v1 * svK * na * noiseAmt * 0.9 * ramp;
            na *= kN;
        }
        float yf = (float) y;
        if (hold > 1) {
            if (pos % hold == 0) held = yf;
            yf = held;
        }
        if (q > 0) yf = (float) (std::round(yf * q) / q);
        const int left = total - pos;
        if (left < fadeLen) yf *= (float) left / (float) fadeLen;
        if (fadeStep > 0) {
            fadeGain -= fadeStep;
            if (fadeGain <= 0) { on = false; return; }
            yf *= fadeGain;
        }
        out[i] += yf * vel;
        if (++pos >= total) { on = false; return; }
    }
}

int renderHit(const Sound& s, float vel, double sr, float* out, int maxLen)
{
    FmVoice v;
    v.prepare(sr);
    v.trigger(s, vel);
    int n = 0;
    while (v.active() && n < maxLen) {
        const int blk = std::min(256, maxLen - n);
        std::fill(out + n, out + n + blk, 0.f);
        v.render(out + n, blk);
        n += blk;
    }
    return n;
}

} // namespace otlak
