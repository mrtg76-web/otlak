// Otlak çekirdek testleri (JUCE'suz)
#include "Dice.h"
#include "FmVoice.h"
#include "Sequencer.h"
#include <cmath>
#include <cstdio>
#include <vector>

using namespace otlak;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static std::array<Params, kVoices> initBase()
{
    std::array<Params, kVoices> b {};
    for (int i = 0; i < kVoices; ++i)
        for (int k = 0; k < kParams; ++k) b[i][k] = kInit[i][k];
    return b;
}

static Kit emptyKit()
{
    Kit kit;
    kit.setDefaults();
    for (auto& L : kit.lane) { L.clearSteps(); L.len = 16; L.mult = kMultOne; L.shOn = false; }
    kit.swing = 50; kit.human = 0; kit.resync = 0;
    return kit;
}

// sequencer'ı serbest çalıştırıp tetiklerin mutlak örnek konumlarını topla
static std::vector<std::pair<int, long>> run(Kit& kit, double sr, double bpm, double gm, long total, int block = 512)
{
    Sequencer seq;
    seq.prepare(sr);
    auto base = initBase();
    std::vector<std::pair<int, long>> hits;
    std::vector<Trigger> trig;
    for (long pos = 0; pos < total; pos += block) {
        trig.clear();
        const int n = (int) std::min<long>(block, total - pos);
        seq.process(kit, base, n, bpm, gm, true, false, 0, trig);
        for (auto& t : trig) hits.push_back({t.voice, pos + t.offset});
    }
    return hits;
}

static void testVoice()
{
    std::mt19937 rng(1);
    std::vector<float> buf(48000 * 4);
    for (int a = 0; a < kAlgos; ++a) {
        for (int trial = 0; trial < 60; ++trial) {
            Sound s;
            for (auto& x : s.p) x = std::uniform_real_distribution<float>(0.f, 1.f)(rng);
            s.algo = a;
            s.r3 = 0.5f + 7.f * std::uniform_real_distribution<float>(0.f, 1.f)(rng);
            s.r4 = 0.5f + 7.f * std::uniform_real_distribution<float>(0.f, 1.f)(rng);
            const int n = renderHit(s, 1.f, 48000, buf.data(), (int) buf.size());
            float mx = 0;
            bool nan = false;
            for (int i = 0; i < n; ++i) {
                if (!std::isfinite(buf[i])) nan = true;
                mx = std::max(mx, std::abs(buf[i]));
            }
            CHECK(!nan, "algo %d NaN", a);
            CHECK(mx <= 1.01f, "algo %d peak %f", a, mx);
            CHECK(n < (int) buf.size(), "algo %d did not end", a);
        }
    }
    // varsayılan sesler duyulur seviyede
    for (int v = 0; v < kVoices; ++v) {
        Sound s;
        for (int k = 0; k < kParams; ++k) s.p[k] = kInit[v][k];
        s.algo = kInitAlgo[v]; s.r3 = kInitRatios[v][0]; s.r4 = kInitRatios[v][1];
        const int n = renderHit(s, 0.9f, 48000, buf.data(), (int) buf.size());
        float mx = 0;
        for (int i = 0; i < n; ++i) mx = std::max(mx, std::abs(buf[i]));
        CHECK(mx > 0.1f, "voice %d too quiet %f", v, mx);
    }
}

static void testTiming()
{
    const double sr = 48000, bpm = 120; // 16'lık = 6000 örnek
    {
        Kit kit = emptyKit();
        kit.lane[0].step.fill('x');
        auto h = run(kit, sr, bpm, 1.0, 45000);
        CHECK(h.size() == 8, "mult 1: %zu hits", h.size());
        for (size_t i = 0; i < h.size(); ++i) CHECK(std::abs(h[i].second - (long) i * 6000) <= 2, "hit %zu at %ld", i, h[i].second);
    }
    {
        Kit kit = emptyKit();
        kit.lane[2].step.fill('x');
        kit.lane[2].mult = 8; // ×2
        auto h = run(kit, sr, bpm, 1.0, 46500);
        CHECK(h.size() == 16, "mult 2: %zu hits", h.size());
        for (size_t i = 0; i < h.size(); ++i) CHECK(std::abs(h[i].second - (long) i * 3000) <= 2, "x2 hit %zu at %ld", i, h[i].second);
    }
    {
        Kit kit = emptyKit();
        kit.lane[1].step.fill('x');
        auto h = run(kit, sr, bpm, 2.0, 46500); // global ×2
        CHECK(h.size() == 16, "global x2: %zu", h.size());
    }
    {
        Kit kit = emptyKit();
        kit.lane[0].step[0] = 'x';
        kit.lane[0].len = 3;
        auto h = run(kit, sr, bpm, 1.0, 70000);
        CHECK(h.size() == 4, "len 3: %zu", h.size());
        for (size_t i = 0; i < h.size(); ++i) CHECK(std::abs(h[i].second - (long) i * 18000) <= 2, "len3 hit at %ld", h[i].second);
    }
    {   // swing: tek adımlar gecikir
        Kit kit = emptyKit();
        kit.lane[0].step.fill('x');
        kit.swing = 75; // yarım adım
        auto h = run(kit, sr, bpm, 1.0, 24000);
        CHECK(h.size() >= 4, "swing hits");
        if (h.size() >= 2) CHECK(std::abs(h[1].second - 9000) <= 2, "swing step1 at %ld", h[1].second);
    }
    {   // mikro zamanlama
        Kit kit = emptyKit();
        kit.lane[0].step[1] = 'x';
        kit.lane[0].micro[1] = -0.25f;
        auto h = run(kit, sr, bpm, 1.0, 12000);
        CHECK(h.size() == 1 && std::abs(h[0].second - 4500) <= 2, "micro at %ld", h.empty() ? -1 : h[0].second);
    }
    {   // resync: ×3/2 kanal her bar başında adım 0'a döner
        Kit kit = emptyKit();
        kit.lane[3].step[0] = 'x';
        kit.lane[3].len = 16;
        kit.lane[3].mult = 7; // 3/2
        kit.resync = 1;
        auto h = run(kit, sr, bpm, 1.0, 96000 * 2 + 100);
        bool at0 = false, atBar = false;
        for (auto& x : h) { if (std::abs(x.second) <= 2) at0 = true; if (std::abs(x.second - 96000) <= 2) atBar = true; }
        CHECK(at0 && atBar, "resync hits (%zu)", h.size());
    }
    {   // mute ve seqOn
        Kit kit = emptyKit();
        kit.lane[0].step.fill('x');
        kit.lane[0].mute = true;
        CHECK(run(kit, sr, bpm, 1.0, 24000).empty(), "mute");
        kit.lane[0].mute = false;
        kit.seqOn = false;
        CHECK(run(kit, sr, bpm, 1.0, 24000).empty(), "seqOn");
    }
}

static void testSampleHold()
{
    Kit kit = emptyKit();
    kit.lane[5].step.fill('x');
    kit.lane[5].shOn = true;
    kit.lane[5].shAmt = 90;
    kit.lane[5].shEvery = 0;
    kit.lane[5].shQuant = true;
    kit.lane[5].shSlew = 0;
    Sequencer seq;
    seq.prepare(48000);
    auto base = initBase();
    std::vector<Trigger> trig;
    int distinct = 0;
    double last = -1;
    for (int b = 0; b < 400; ++b) {
        trig.clear();
        seq.process(kit, base, 512, 120, 1.0, true, false, 0, trig);
        const double e = seq.effMult(5);
        bool inSet = false;
        for (double m : kMultValues) if (std::abs(m - e) < 1e-9) inSet = true;
        CHECK(inSet, "quantized S&H value %f", e);
        if (e != last) { ++distinct; last = e; }
    }
    CHECK(distinct > 3, "S&H changes (%d)", distinct);
}

static void testLocks()
{
    Kit kit = emptyKit();
    kit.lane[0].step[0] = 'o';
    kit.lane[0].lock[0].has[Pitch] = true;
    kit.lane[0].lock[0].v[Pitch] = 0.9f;
    Sequencer seq;
    seq.prepare(48000);
    auto base = initBase();
    std::vector<Trigger> trig;
    seq.process(kit, base, 512, 120, 1.0, true, false, 0, trig);
    CHECK(trig.size() == 1, "lock trig %zu", trig.size());
    if (!trig.empty()) {
        CHECK(std::abs(trig[0].p[Pitch] - 0.9f) < 1e-6, "locked pitch %f", trig[0].p[Pitch]);
        CHECK(std::abs(trig[0].p[Decay] - kInit[0][Decay]) < 1e-6, "unlocked decay");
        CHECK(trig[0].vel == 1.f, "p-lock vel");
    }
    // konum bulma
    int idx; float frac;
    for (int i = 0; i < 6; ++i) { trig.clear(); seq.process(kit, base, 1000, 120, 1.0, true, false, 0, trig); }
    // ~6512 örnek: adım 1, frac ~0.085
    CHECK(seq.locate(kit, 0, idx, frac) && idx == 1 && frac > 0 && frac < 0.2f, "locate %d %f", idx, frac);
    for (int i = 0; i < 4; ++i) { trig.clear(); seq.process(kit, base, 1000, 120, 1.0, true, false, 0, trig); }
    // ~10512: adım 1'in %75'i -> sonraki adım (2), frac eksi
    CHECK(seq.locate(kit, 0, idx, frac) && idx == 2 && frac < 0, "locate next %d %f", idx, frac);
}

static void testDice()
{
    std::mt19937 rng(7);
    for (int v = 0; v < kVoices; ++v) {
        Params p {};
        int algo = 0;
        VoiceExtra x;
        x.locks[Pitch] = true;
        p[Pitch] = 0.123f;
        for (int t = 0; t < 50; ++t) {
            rollSound(v, p, algo, x, t % 3 == 0, rng);
            CHECK(p[Pitch] == 0.123f, "locked param changed");
            for (int k = 1; k < kParams; ++k) CHECK(p[k] >= 0.f && p[k] <= 1.f, "range");
            CHECK(algo >= 0 && algo < kAlgos, "algo");
        }
        Lane L;
        rollPattern(L, v, p, rng);
        bool any = false;
        for (int j = 0; j < L.len; ++j) any |= L.step[j] != '.';
        CHECK(any && L.len >= 1 && L.len <= 16, "pattern");
        for (int j = L.len; j < kSteps; ++j) CHECK(L.step[j] == '.', "beyond len empty");
    }
    // elle kısmi kilit korunur
    Lane L;
    L.step[3] = 'o';
    L.lock[3].has[Pitch] = true;
    L.lock[3].v[Pitch] = 0.5f;
    L.step[5] = 'o';
    Params p {};
    regenVariants(L, p, rng);
    CHECK(!L.lock[3].full() && L.lock[3].v[Pitch] == 0.5f, "partial lock kept");
    CHECK(L.lock[5].full(), "variant generated");
}

int main()
{
    testVoice();
    testTiming();
    testSampleHold();
    testLocks();
    testDice();
    std::printf(fails ? "%d FAILURES\n" : "all tests passed\n", fails);
    return fails ? 1 : 0;
}
