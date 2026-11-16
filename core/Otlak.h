// Otlak — ortak sabitler ve veri yapıları (JUCE'dan bağımsız)
#pragma once
#include <array>
#include <string>

namespace otlak {

constexpr int kVoices = 6;
constexpr int kSteps = 16;
constexpr int kParams = 8;
constexpr int kAlgos = 6;
constexpr int kMults = 11;
constexpr int kEvery = 6;
constexpr int kResync = 5;
constexpr int kMultOne = 5; // "1"

enum Param { Pitch = 0, Ratio, Index, Feedback, PEnv, Decay, Fold, Crush };

extern const char* const kVoiceNames[kVoices];
extern const char* const kVoiceNotes[kVoices];
extern const int kVoiceMidi[kVoices];
extern const char* const kParamNames[kParams];
extern const char* const kMultNames[kMults];
extern const double kMultValues[kMults];
extern const char* const kEveryNames[kEvery];
extern const int kEveryBase[kEvery];
extern const int kResyncBars[kResync];
extern const char* const kAlgoNames[kAlgos];
extern const char* const kAlgoRoutes[kAlgos];
extern const float kRange[kVoices][kParams][2];
extern const float kInit[kVoices][kParams];
extern const int kInitAlgo[kVoices];
extern const float kInitRatios[kVoices][2];

using Params = std::array<float, kParams>;

std::string formatParam(int k, float v);
std::string formatMult(double v);
double nearestMult(double v);

// Bir adımın parametre kilitleri (p-lock). has[k] false ise sesin genel değeri kullanılır.
struct StepLock {
    Params v {};
    std::array<bool, kParams> has {};
    bool any() const;
    bool full() const;
    void clear();
};

struct Lane {
    std::array<char, kSteps> step {}; // '.' boş, 'x' trig, 'o' p-lock
    int len = 16;
    std::array<StepLock, kSteps> lock {};
    std::array<float, kSteps> micro {}; // -0.45..0.45 adım (kuantize kapalı kayıt)
    int mult = kMultOne;
    bool shOn = false;
    int shEvery = 2;
    int shAmt = 30;   // %
    bool shQuant = true;
    int shSlew = 0;   // %
    bool mute = false;
    Lane() { step.fill('.'); }
    void clearSteps();
    void setLength(int n);
};

struct VoiceExtra {
    float r3 = 2.f, r4 = 3.f;            // OP3 / OP4 oranları
    std::array<bool, kParams> locks {};   // zardan korunan parametreler
    bool algoLock = false;
};

struct Kit {
    std::array<Lane, kVoices> lane;
    std::array<VoiceExtra, kVoices> voice;
    int swing = 54;  // 50..75
    int human = 12;  // 0..50
    int resync = 2;  // kResyncBars indeksi
    bool seqOn = true;
    void setDefaults();
};

// Bir vuruşun tam ses tanımı
struct Sound {
    Params p {};
    int algo = 0;
    float r3 = 2.f, r4 = 3.f;
};

Params mergeLock(const Params& base, const StepLock& l);

} // namespace otlak
