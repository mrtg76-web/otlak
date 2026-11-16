#include "Sequencer.h"
#include <algorithm>
#include <cmath>

namespace otlak {

static int wrapIdx(long k, int len)
{
    len = std::max(1, len);
    return (int) (((k % len) + len) % len);
}

void Sequencer::stop()
{
    running = false;
    following = false;
    for (auto& lane : pend)
        for (auto& p : lane) p.used = false;
}

void Sequencer::startAt(Kit& kit, const std::array<Params, kVoices>& base, double basePos)
{
    basePhase = basePos;
    const int rs = kResyncBars[std::clamp(kit.resync, 0, kResync - 1)];
    const double origin = rs > 0 ? std::floor(basePos / (16.0 * rs)) * 16.0 * rs : 0.0;
    for (int i = 0; i < kVoices; ++i) {
        const double m = kMultValues[std::clamp(kit.lane[i].mult, 0, kMults - 1)];
        eff[i] = target[i] = m;
        for (auto& p : pend[i]) p.used = false;
        lp[i] = (basePos - origin) * m;
        const double fl = std::floor(lp[i]);
        const double fr = lp[i] - fl;
        if (fr < 0.02) scheduleStep(kit, base, i, (long) fl, (fl - lp[i]) / (incBase * m));
        else if (fr > 0.5) scheduleStep(kit, base, i, (long) fl + 1, (fl + 1.0 - lp[i]) / (incBase * m));
    }
}

void Sequencer::baseTick(Kit& kit, const std::array<Params, kVoices>& base, long k)
{
    for (int i = 0; i < kVoices; ++i) {
        const Lane& L = kit.lane[i];
        const double b = kMultValues[std::clamp(L.mult, 0, kMults - 1)];
        if (!L.shOn) {
            target[i] = b;
        } else if (k % kEveryBase[std::clamp(L.shEvery, 0, kEvery - 1)] == 0) {
            double v = b * std::pow(2.0, (rand01() * 2.0 - 1.0) * L.shAmt / 100.0 * 1.5);
            if (L.shQuant) v = nearestMult(v);
            target[i] = std::clamp(v, 0.125, 8.0);
        }
    }
    const int rs = kResyncBars[std::clamp(kit.resync, 0, kResync - 1)];
    if (rs > 0 && k > 0 && k % (16L * rs) == 0) {
        const double frac = basePhase - (double) k;
        for (int i = 0; i < kVoices; ++i) {
            for (auto& p : pend[i]) p.used = false;
            lp[i] = frac * eff[i];
            scheduleStep(kit, base, i, 0, -lp[i] / (incBase * eff[i]));
        }
    }
}

void Sequencer::scheduleStep(Kit& kit, const std::array<Params, kVoices>& base, int i, long k, double tts)
{
    const Lane& L = kit.lane[i];
    const double tgt = L.shOn ? target[i] : kMultValues[std::clamp(L.mult, 0, kMults - 1)];
    if (!L.shOn) target[i] = tgt;
    const double sl = L.shOn ? L.shSlew / 100.0 : 0.0;
    eff[i] = sl > 0 ? eff[i] + (tgt - eff[i]) * (1.0 - sl * 0.92) : tgt;

    const int idx = wrapIdx(k, L.len);
    const char c = L.step[idx];
    if (c == '.' || L.mute || !kit.seqOn) return;

    const double dur = 1.0 / (incBase * eff[i]);
    double d = tts + L.micro[idx] * dur;
    if (idx % 2 == 1) d += (kit.swing - 50) / 50.0 * dur;
    if (kit.human > 0) d += (rand01() * 2.0 - 1.0) * kit.human / 100.0 * 0.01 * sr;
    if (d < 0) d = 0;

    Params p = c == 'o' ? mergeLock(base[i], L.lock[idx]) : base[i];
    if (kit.human > 0)
        for (auto& x : p) x = (float) std::clamp(x + (rand01() - 0.5) * kit.human / 100.0 * 0.08, 0.0, 1.0);
    const float vel = (c == 'o' ? 1.f : 0.82f) * (float) (1.0 - rand01() * kit.human / 100.0 * 0.3);

    Pending* slot = nullptr;
    for (auto& q : pend[i])
        if (!q.used) { slot = &q; break; }
    if (!slot) slot = &pend[i][0];
    slot->used = true;
    slot->idx = idx;
    slot->delay = d;
    slot->vel = vel;
    slot->p = p;
}

void Sequencer::process(Kit& kit, const std::array<Params, kVoices>& base, int n, double bpm, double gmult,
                        bool run, bool hostPlaying, double ppq, std::vector<Trigger>& out)
{
    if (!run || bpm <= 0 || gmult <= 0) {
        if (running) stop();
        return;
    }
    incBase = bpm / 60.0 * 4.0 * gmult / sr;
    if (!running || following != hostPlaying) {
        running = true;
        following = hostPlaying;
        startAt(kit, base, hostPlaying ? ppq * 4.0 * gmult : 0.0);
    } else if (hostPlaying && std::abs(ppq - expectedPpq) > 0.02) {
        startAt(kit, base, ppq * 4.0 * gmult); // host'ta konum atladı
    }
    if (hostPlaying) expectedPpq = ppq + n / sr * bpm / 60.0;

    for (int s = 0; s < n; ++s) {
        for (int i = 0; i < kVoices; ++i)
            for (auto& p : pend[i])
                if (p.used) {
                    p.delay -= 1.0;
                    if (p.delay <= 0) {
                        Trigger t;
                        t.voice = i; t.offset = s; t.idx = p.idx; t.vel = p.vel; t.p = p.p;
                        out.push_back(t);
                        p.used = false;
                    }
                }
        const double prevB = basePhase;
        basePhase += incBase;
        if (std::floor(basePhase) > std::floor(prevB)) baseTick(kit, base, (long) std::floor(basePhase));
        for (int i = 0; i < kVoices; ++i) {
            const double e = eff[i];
            const double prev = lp[i];
            lp[i] += incBase * e;
            const long a = (long) std::floor(prev - 0.5);
            const long b = (long) std::floor(lp[i] - 0.5);
            if (b > a) scheduleStep(kit, base, i, b + 1, ((double) (b + 1) - lp[i]) / (incBase * e));
        }
    }
}

int Sequencer::playingStep(const Kit& kit, int lane) const
{
    if (!running) return -1;
    return wrapIdx((long) std::floor(lp[lane]), kit.lane[lane].len);
}

bool Sequencer::locate(const Kit& kit, int lane, int& idx, float& frac) const
{
    if (!running) return false;
    const double fl = std::floor(lp[lane]);
    double f = lp[lane] - fl;
    const int len = std::max(1, kit.lane[lane].len);
    idx = wrapIdx((long) fl, len);
    if (f > 0.5) {
        idx = (idx + 1) % len;
        f -= 1.0;
    }
    frac = (float) f;
    return true;
}

void Sequencer::cancelPending(int lane, int idx)
{
    for (auto& p : pend[lane])
        if (p.used && p.idx == idx) p.used = false;
}

} // namespace otlak
