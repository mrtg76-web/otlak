#pragma once
#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

// Tamamen elle çizilen arayüz: iki görünüm (KAR açık, FJORD koyu), aynı motor.
class OtlakEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit OtlakEditor(OtlakProcessor&);
    ~OtlakEditor() override = default;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;

    void refreshNow();
    static constexpr int W = 1200, H = 760;

private:
    enum Kind {
        kNone, kPlay, kRec, kQuant, kRecMode, kGmDown, kGmUp, kVol, kSkin, kKit, kUndo,
        kSelVoice, kDice, kMutate, kAlgo, kAlgoSet, kAlgoLock, kKnob, kParamLock,
        kStep, kClear, kLen, kRate, kShOn, kShEvery, kShAmt, kShMode, kShSlew, kMute,
        kPattern, kResync, kSwing, kHuman, kSeqOn, kSelDone
    };
    struct Hit { juce::Rectangle<float> r; Kind kind; int a, b; };
    struct Pal {
        juce::Colour bg, panel, line, line2, text, muted, acc, accText, pl, cell, cellb, track, face, ring, btn, rowsel, env;
        float rad, radp;
        bool dark;
    };

    OtlakProcessor& proc;
    std::vector<Hit> hits;

    // önbellek (timer'da güncellenir)
    otlak::Kit kit;
    std::array<otlak::Params, otlak::kVoices> base {};
    std::array<int, otlak::kVoices> algos {};
    std::array<int, otlak::kVoices> steps {};
    std::array<float, otlak::kVoices> effs {};
    std::array<int, otlak::kVoices> lastHits {};
    std::array<float, otlak::kVoices> flash {};
    int lastMidi = 0;
    float midiFlash = 0;
    int sel = 0;

    // adım düzenleme seçimi
    int editLane = -1;
    std::vector<int> editSteps;
    bool editing(int v) const { return editLane == v && !editSteps.empty(); }

    struct Drag {
        bool active = false;
        Kind kind = kNone;
        int a = 0, b = 0;
        float lastY = 0, v = 0;
        bool moved = false;
        otlak::Lane snap;
        bool recMode = false;
    } drag;
    float tempKnob = -1; // REC sırasında geçici gösterim

    Pal pal() const;
    bool dark() const { return proc.getSkin() == "fjord"; }
    void add(juce::Rectangle<float> r, Kind k, int a = 0, int b = 0) { hits.push_back({r, k, a, b}); }
    const Hit* hitAt(juce::Point<float> p) const;
    juce::Point<float> toDesign(juce::Point<float> p) const;

    void timerCallback() override;

    // çizim
    void paintHeader(juce::Graphics&, const Pal&);
    void paintKar(juce::Graphics&, const Pal&);
    void paintFjord(juce::Graphics&, const Pal&);
    void paintSeq(juce::Graphics&, const Pal&, juce::Rectangle<float> area);
    void drawKnob(juce::Graphics&, const Pal&, juce::Rectangle<float> r, int v, int k, bool big);
    void drawKnobCell(juce::Graphics&, const Pal&, float cx, float y, int v, int k, bool big);
    void drawDice(juce::Graphics&, juce::Rectangle<float> r, juce::Colour c, float size);
    void drawLock(juce::Graphics&, float x, float y, juce::Colour c);

    // değerler
    struct DV { float v; bool locked; };
    DV dispVal(int v, int k) const;
    float dragValue(Kind k, int a, int b) const;
    void setDragValue(Kind k, int a, int b, float x);

    void click(const Hit&, const juce::MouseEvent&);
    void selectStep(int v, int j);
    void clearEdit() { editLane = -1; editSteps.clear(); }
    void hitLive(int v, bool accent);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OtlakEditor)
};
