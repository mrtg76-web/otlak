#include "Dice.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace otlak {

static const std::vector<int> kAlgoPref[kVoices] = {{0, 0, 1, 3}, {3, 3, 1, 4}, {1, 5, 0, 2}, {5, 1, 2, 4}, {4, 5, 2}, {2, 4, 5, 1, 3}};
static const float kRatioPool[] = {0.5f, 1.f, 1.41f, 1.5f, 2.f, 2.76f, 3.f, 3.14f, 3.5f, 4.2f, 5.4f, 7.13f};
static const double kDensity[kVoices] = {.28, .14, .3, .32, .62, .38};
static const std::vector<int> kLens[kVoices] = {{16}, {16}, {12, 16, 10, 14}, {13, 11, 9, 15}, {16, 12}, {7, 5, 9, 11, 13}};

static double u(std::mt19937& r) { return std::uniform_real_distribution<double>(0.0, 1.0)(r); }
template <typename T>
static T pick(const std::vector<T>& v, std::mt19937& r) { return v[std::min(v.size() - 1, (size_t) (u(r) * v.size()))]; }

void rollSound(int voice, Params& p, int& algo, VoiceExtra& x, bool mutate, std::mt19937& rng)
{
    for (int k = 0; k < kParams; ++k) {
        if (x.locks[k]) continue;
        if (mutate) p[k] = (float) std::clamp(p[k] + (u(rng) - 0.5) * 0.2, 0.0, 1.0);
        else p[k] = (float) (kRange[voice][k][0] + u(rng) * (kRange[voice][k][1] - kRange[voice][k][0]));
    }
    if (x.algoLock) return;
    if (mutate) {
        x.r3 = (float) (x.r3 * (1.0 + (u(rng) - 0.5) * 0.06));
        x.r4 = (float) (x.r4 * (1.0 + (u(rng) - 0.5) * 0.06));
    } else {
        algo = pick(kAlgoPref[voice], rng);
        const int n = (int) (sizeof kRatioPool / sizeof kRatioPool[0]);
        x.r3 = kRatioPool[std::min(n - 1, (int) (u(rng) * n))];
        x.r4 = kRatioPool[std::min(n - 1, (int) (u(rng) * n))];
    }
}

StepLock randomVariant(const Params& p, std::mt19937& rng)
{
    StepLock l;
    for (int k = 0; k < kParams; ++k) {
        l.v[k] = (float) std::clamp(p[k] + (u(rng) - 0.5) * 0.35, 0.0, 1.0);
        l.has[k] = true;
    }
    return l;
}

void regenVariants(Lane& lane, const Params& p, std::mt19937& rng)
{
    for (int j = 0; j < kSteps; ++j) {
        if (lane.step[j] != 'o') continue;
        auto& l = lane.lock[j];
        if (!l.any() || l.full()) l = randomVariant(p, rng);
    }
}

void rollPattern(Lane& lane, int voice, const Params& p, std::mt19937& rng)
{
    const int len = pick(kLens[voice], rng);
    lane.clearSteps();
    lane.setLength(len);
    bool anyOn = false;
    for (int j = 0; j < len; ++j) {
        double pr = kDensity[voice];
        if (voice == 0 && j % 4 == 0) pr += .35;
        if (voice == 1 && (j == 4 || j == 12)) pr += .7;
        if (u(rng) < pr) {
            const bool o = u(rng) < .22;
            lane.step[j] = o ? 'o' : 'x';
            if (o) lane.lock[j] = randomVariant(p, rng);
            anyOn = true;
        }
    }
    if (!anyOn) lane.step[0] = 'x';
}

} // namespace otlak
