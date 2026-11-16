#pragma once
#include "Dice.h"
#include "FmVoice.h"
#include "Otlak.h"
#include "Sequencer.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <random>

class OtlakProcessor : public juce::AudioProcessor {
public:
    OtlakProcessor();
    ~OtlakProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Otlak"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // ---------------------------------------------------------------- editor API (message thread)
    juce::AudioProcessorValueTreeState params;
    static juce::String pid(int voice, int k);
    static juce::String algoId(int voice);

    float paramValue(int v, int k) const { return pv[v][k]->load(); }
    otlak::Params baseParams(int v) const;
    int algo(int v) const { return juce::jlimit(0, otlak::kAlgos - 1, (int) palgo[v]->load()); }
    int gmult() const { return juce::jlimit(0, otlak::kMults - 1, (int) pgm->load()); }
    float volume() const { return pvol->load(); }
    void setParam(int v, int k, float x);          // tek seferlik (gesture dahil)
    void beginParam(int v, int k);
    void dragParam(int v, int k, float x);
    void endParam(int v, int k);
    void setChoice(const juce::String& id, int index);
    void setFloat(const juce::String& id, float x); // 0..1

    otlak::Kit copyKit() const;
    template <typename F> void editKit(F&& fn)
    {
        const juce::SpinLock::ScopedLockType l(kitLock);
        fn(kit);
    }

    void rollVoice(int v, bool mutate);
    void rollKit(bool mutate);
    void rollPatterns(int onlyLane); // -1 = hepsi
    void clearLanes(int onlyLane);
    void pushUndo();
    void undo();
    int undoCount() const { return (int) undoStack.size(); }

    void audition(int v, const otlak::Params& p, float vel);
    void liveHit(int v, float vel); // klavye: MIDI notası gibi davranır (kayıt dahil)
    void auditionStep(int v, int step);
    void recLock(int v, int k, float x); // REC sırasında knob hareketi

    juce::String getSkin() const { return skin; }
    void setSkin(const juce::String& s);

    // ---------------------------------------------------------------- audio -> UI
    std::atomic<bool> freeRun { false }, rec { false }, quant { true }, replaceMode { false };
    std::atomic<double> hostBpm { 120.0 };
    std::atomic<bool> hostPlaying { false }, seqRunning { false };
    std::array<std::atomic<int>, otlak::kVoices> playStep {};
    std::array<std::atomic<float>, otlak::kVoices> effMult {};
    std::array<std::atomic<int>, otlak::kVoices> hits {};
    std::atomic<int> midiCount { 0 };

private:
    std::array<std::array<std::atomic<float>*, otlak::kParams>, otlak::kVoices> pv {};
    std::array<std::atomic<float>*, otlak::kVoices> palgo {};
    std::atomic<float>* pgm = nullptr;
    std::atomic<float>* pvol = nullptr;

    otlak::Kit kit;
    mutable juce::SpinLock kitLock;
    otlak::Sequencer seq;
    std::array<otlak::Channel, otlak::kVoices> channels;
    std::vector<otlak::Trigger> trig;
    std::vector<float> voiceBuf, mixBuf;
    double sr = 48000.0;
    bool lastRec = false;
    std::array<bool, otlak::kVoices> recTouched {};
    std::mt19937 uiRng;

    struct UiHit { int voice; otlak::Params p; float vel; bool live; };
    juce::AbstractFifo uiFifo { 32 };
    std::array<UiHit, 32> uiHits {};

    struct Snap {
        std::array<otlak::Params, otlak::kVoices> p;
        std::array<int, otlak::kVoices> algo;
        otlak::Kit kit;
    };
    std::vector<Snap> undoStack;

    juce::String skin { "kar" };
    std::unique_ptr<juce::PropertiesFile> settings;

    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    void handleNote(int voice, float vel, const std::array<otlak::Params, otlak::kVoices>& base);
    otlak::Sound soundFor(int v, const otlak::Params& p) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OtlakProcessor)
};
