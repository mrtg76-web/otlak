#include "Otlak.h"
#include <cmath>
#include <cstdio>

namespace otlak {

const char* const kVoiceNames[kVoices] = {"KICK", "SNARE", "PERC 1", "PERC 2", "HAT", "GLITCH"};
const char* const kVoiceNotes[kVoices] = {"C1", "D1", "E1", "F1", "F#1", "G1"};
const int kVoiceMidi[kVoices] = {36, 38, 40, 41, 42, 43};
const char* const kParamNames[kParams] = {"PITCH", "RATIO", "INDEX", "FDBK", "P.ENV", "DECAY", "FOLD", "CRUSH"};
const char* const kMultNames[kMults] = {"1/4", "1/3", "1/2", "2/3", "3/4", "1", "5/4", "3/2", "2", "3", "4"};
const double kMultValues[kMults] = {0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0, 0.75, 1.0, 1.25, 1.5, 2.0, 3.0, 4.0};
const char* const kEveryNames[kEvery] = {"1/16", "1/8", "1/4", "1/2", "1 bar", "2 bar"};
const int kEveryBase[kEvery] = {1, 2, 4, 8, 16, 32};
const int kResyncBars[kResync] = {0, 1, 2, 4, 8};
const char* const kAlgoNames[kAlgos] = {"2-OP", "Y", "SERIAL", "PAIRS", "RING", "BRANCH"};
const char* const kAlgoRoutes[kAlgos] = {"2>1", "2+3>1", "4>3>2>1", "2>1 + 4>3", "(2>1)x(4>3)", "4>1,2,3"};

// Zarın her ses için kullandığı aralıklar (0..1)
const float kRange[kVoices][kParams][2] = {
    {{0.f, .35f}, {0.f, .25f}, {.1f, .5f}, {0.f, .3f}, {.4f, .9f}, {.4f, .7f}, {0.f, .4f}, {0.f, .3f}},
    {{.3f, .6f}, {.2f, .7f}, {.3f, .8f}, {.3f, .8f}, {.1f, .5f}, {.3f, .6f}, {0.f, .6f}, {0.f, .5f}},
    {{.35f, .75f}, {.1f, 1.f}, {.2f, .9f}, {0.f, .6f}, {0.f, .6f}, {.15f, .5f}, {0.f, .8f}, {0.f, .6f}},
    {{.4f, .85f}, {.1f, 1.f}, {.3f, 1.f}, {.2f, .9f}, {0.f, .4f}, {.1f, .45f}, {0.f, 1.f}, {0.f, .8f}},
    {{.75f, 1.f}, {.5f, 1.f}, {.5f, 1.f}, {.5f, 1.f}, {0.f, .2f}, {.05f, .4f}, {0.f, .5f}, {0.f, .6f}},
    {{0.f, 1.f}, {0.f, 1.f}, {.4f, 1.f}, {.4f, 1.f}, {0.f, 1.f}, {0.f, .35f}, {.3f, 1.f}, {.3f, 1.f}},
};

const float kInit[kVoices][kParams] = {
    {.18f, .08f, .32f, .10f, .72f, .55f, .10f, 0.f},
    {.45f, .41f, .55f, .60f, .30f, .45f, .20f, .15f},
    {.60f, .73f, .62f, .25f, .20f, .35f, .35f, .10f},
    {.66f, .90f, .70f, .55f, .12f, .30f, .60f, .45f},
    {.88f, .83f, .78f, .72f, .05f, .25f, .15f, .30f},
    {.50f, .37f, .85f, .80f, .60f, .20f, .70f, .65f},
};
const int kInitAlgo[kVoices] = {0, 3, 1, 5, 4, 2};
const float kInitRatios[kVoices][2] = {{2.f, 3.f}, {1.5f, 5.4f}, {2.76f, 1.41f}, {3.14f, 4.2f}, {3.5f, 7.13f}, {1.41f, 2.76f}};

std::string formatParam(int k, float v)
{
    char b[32];
    switch (k) {
        case Pitch: std::snprintf(b, sizeof b, "%dHz", (int) std::lround(20.0 * std::pow(2.0, v * 9.0))); break;
        case Ratio: std::snprintf(b, sizeof b, "%.2f", 0.25 + v * 7.75); break;
        case Index: std::snprintf(b, sizeof b, "%.1f", v * 12.0); break;
        case PEnv: std::snprintf(b, sizeof b, "%dst", (int) std::lround(v * 48.0)); break;
        case Decay: {
            const double ms = 8.0 * std::pow(2.0, v * 9.0);
            if (ms < 1000.0) std::snprintf(b, sizeof b, "%dms", (int) std::lround(ms));
            else std::snprintf(b, sizeof b, "%.2fs", ms / 1000.0);
            break;
        }
        case Crush:
            if (v < 0.05f) std::snprintf(b, sizeof b, "off");
            else std::snprintf(b, sizeof b, "%dbit", (int) std::lround(16.0 - v * 14.0));
            break;
        default: std::snprintf(b, sizeof b, "%d%%", (int) std::lround(v * 100.0)); break;
    }
    return b;
}

std::string formatMult(double v)
{
    for (int i = 0; i < kMults; ++i)
        if (std::abs(kMultValues[i] - v) < 0.002) return kMultNames[i];
    char b[16];
    std::snprintf(b, sizeof b, "%.2f", v);
    return b;
}

double nearestMult(double v)
{
    double best = kMultValues[0];
    for (double m : kMultValues)
        if (std::abs(std::log(m / v)) < std::abs(std::log(best / v))) best = m;
    return best;
}

bool StepLock::any() const
{
    for (bool h : has) if (h) return true;
    return false;
}
bool StepLock::full() const
{
    for (bool h : has) if (!h) return false;
    return true;
}
void StepLock::clear()
{
    has.fill(false);
}

void Lane::clearSteps()
{
    step.fill('.');
    for (auto& l : lock) l.clear();
    micro.fill(0.f);
}

void Lane::setLength(int n)
{
    len = n < 1 ? 1 : (n > kSteps ? kSteps : n);
}

void Kit::setDefaults()
{
    static const char* pats[kVoices] = {"x..o..x...x.o...", "....x.......x..o", "..x.ox..x..o", "x..x.o..x.x..", "xxoxx.xoxx.xxoxo", "o..xo.x"};
    static const int mults[kVoices] = {5, 5, 7, 3, 8, 9};
    struct Sh { bool on; int ev, amt; bool q; int slew; };
    static const Sh sh[kVoices] = {{false, 2, 30, true, 0}, {false, 3, 30, true, 0}, {true, 2, 30, true, 0},
                                    {true, 4, 55, false, 40}, {false, 1, 30, true, 0}, {true, 1, 80, true, 10}};
    for (int i = 0; i < kVoices; ++i) {
        Lane& L = lane[i];
        L = Lane();
        int n = 0;
        for (const char* c = pats[i]; *c && n < kSteps; ++c) L.step[n++] = *c;
        L.len = n;
        L.mult = mults[i];
        L.shOn = sh[i].on; L.shEvery = sh[i].ev; L.shAmt = sh[i].amt; L.shQuant = sh[i].q; L.shSlew = sh[i].slew;
        voice[i] = VoiceExtra();
        voice[i].r3 = kInitRatios[i][0];
        voice[i].r4 = kInitRatios[i][1];
    }
    swing = 54; human = 12; resync = 2; seqOn = true;
}

Params mergeLock(const Params& base, const StepLock& l)
{
    Params p = base;
    for (int k = 0; k < kParams; ++k)
        if (l.has[k]) p[k] = l.v[k];
    return p;
}

} // namespace otlak
