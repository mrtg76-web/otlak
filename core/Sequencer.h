// Kanal başına tempo çarpanlı, S&H'li poliritmik step sequencer
#pragma once
#include "Otlak.h"
#include <random>
#include <vector>

namespace otlak {

struct Trigger {
    int voice = 0;
    int offset = 0;   // blok içindeki örnek
    int idx = 0;      // adım
    float vel = 1.f;
    Params p {};
};

class Sequencer {
public:
    void prepare(double sampleRate) { sr = sampleRate; }
    void stop();
    bool isRunning() const { return running; }

    // base: her sesin genel parametreleri. run: sequencer çalsın mı.
    // hostPlaying: zaman host'tan mı geliyor (ppq geçerli). bpm: tempo.
    void process(Kit& kit, const std::array<Params, kVoices>& base, int numSamples, double bpm, double gmult,
                 bool run, bool hostPlaying, double ppq, std::vector<Trigger>& out);

    int playingStep(const Kit& kit, int lane) const;
    double effMult(int lane) const { return eff[lane]; }
    // Kayıt için: şu an hangi adım + adım içindeki konum (-0.5..0.5; eksi = gelecek adım)
    bool locate(const Kit& kit, int lane, int& idx, float& frac) const;
    void cancelPending(int lane, int idx);

    std::mt19937 rng { 0x07714c };

private:
    struct Pending {
        bool used = false;
        int idx = 0;
        double delay = 0;
        float vel = 1;
        Params p {};
    };
    double sr = 48000.0;
    bool running = false;
    bool following = false; // host'a mı bağlı
    double basePhase = 0;   // 16'lık (× global çarpan)
    double expectedPpq = 0;
    double incBase = 0;
    std::array<double, kVoices> lp {}, eff {}, target {};
    std::array<std::array<Pending, 4>, kVoices> pend {};

    double rand01() { return std::uniform_real_distribution<double>(0.0, 1.0)(rng); }
    void startAt(Kit& kit, const std::array<Params, kVoices>& base, double basePos);
    void baseTick(Kit& kit, const std::array<Params, kVoices>& base, long k);
    void scheduleStep(Kit& kit, const std::array<Params, kVoices>& base, int lane, long k, double timeToStart);
};

} // namespace otlak
