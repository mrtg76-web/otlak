#include "PluginEditor.h"
#include <cmath>

using namespace otlak;

// ---------------------------------------------------------------- yardımcılar
static juce::Font sans(float s, bool bold = false)
{
    return juce::Font(juce::FontOptions(s, bold ? juce::Font::bold : juce::Font::plain));
}
static juce::Font mono(float s)
{
    return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), s, juce::Font::plain));
}
static juce::Font lbl(float s, bool bold = false, float kern = 0.12f) { return sans(s, bold).withExtraKerningFactor(kern); }
static juce::String u8(const char* s) { return juce::String::fromUTF8(s); }
static const char* const kRoutes[kAlgos] = {"2→1", "2+3→1", "4→3→2→1", "2→1 + 4→3", "(2→1)×(4→3)", "4→1,2,3"};
static juce::String mult(double v) { return u8("×") + juce::String(formatMult(v)); }

static void text(juce::Graphics& g, const juce::String& s, juce::Rectangle<float> r, juce::Font f, juce::Colour c,
                 juce::Justification j = juce::Justification::centredLeft)
{
    g.setFont(f);
    g.setColour(c);
    g.drawText(s, r, j, false);
}

OtlakEditor::Pal OtlakEditor::pal() const
{
    using C = juce::Colour;
    if (dark())
        return {C(0xff1B1F23), C(0xff22272C), C(0xff343B42), C(0xff2A3036), C(0xffE6E4DF), C(0xff9BA1A7), C(0xff8FB8C9),
                C(0xff1B1F23), C(0xffD9A36A), C(0xff2A3036), C(0xff343B42), C(0xff343B42), C(0xff2A3036), C(0xff3A4148),
                C(0x00000000), C(0xff2B3238), C(0xff1E2327), 17.f, 13.f, true};
    return {C(0xffF4F3EF), C(0xffFBFAF7), C(0xffDEDBD3), C(0xffEEECE6), C(0xff23262A), C(0xff6B6E72), C(0xff3E6E8E),
            C(0xffFBFAF7), C(0xffB5643C), C(0xffF4F3EF), C(0xffDEDBD3), C(0xffE3E0D9), C(0xffFBFAF7), C(0xffCFCBC2),
            C(0xffF4F3EF), C(0xffECEAE4), C(0xffF4F3EF), 8.f, 6.f, false};
}

OtlakEditor::OtlakEditor(OtlakProcessor& p) : AudioProcessorEditor(&p), proc(p)
{
    setResizable(true, true);
    setResizeLimits(W * 3 / 5, H * 3 / 5, W * 2, H * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio((double) W / H);
    setSize(W, H);
    setWantsKeyboardFocus(true);
    refreshNow();
    startTimerHz(30);
}

void OtlakEditor::refreshNow()
{
    kit = proc.copyKit();
    for (int v = 0; v < kVoices; ++v) {
        base[(size_t) v] = proc.baseParams(v);
        algos[(size_t) v] = proc.algo(v);
        steps[(size_t) v] = proc.playStep[v].load();
        effs[(size_t) v] = proc.effMult[v].load();
    }
}

void OtlakEditor::timerCallback()
{
    refreshNow();
    for (int v = 0; v < kVoices; ++v) {
        const int h = proc.hits[v].load();
        if (h != lastHits[(size_t) v]) { flash[(size_t) v] = 1.f; lastHits[(size_t) v] = h; }
        else flash[(size_t) v] = juce::jmax(0.f, flash[(size_t) v] - 0.34f);
    }
    const int m = proc.midiCount.load();
    if (m != lastMidi) { midiFlash = 1.f; lastMidi = m; }
    else midiFlash = juce::jmax(0.f, midiFlash - 0.1f);
    if (editLane >= 0) {
        const int len = kit.lane[(size_t) editLane].len;
        editSteps.erase(std::remove_if(editSteps.begin(), editSteps.end(), [len](int j) { return j >= len; }), editSteps.end());
        if (editSteps.empty()) clearEdit();
    }
    repaint();
}

juce::Point<float> OtlakEditor::toDesign(juce::Point<float> p) const
{
    const float s = (float) getWidth() / (float) W;
    return p / s;
}

const OtlakEditor::Hit* OtlakEditor::hitAt(juce::Point<float> p) const
{
    for (auto it = hits.rbegin(); it != hits.rend(); ++it)
        if (it->r.contains(p)) return &*it;
    return nullptr;
}

OtlakEditor::DV OtlakEditor::dispVal(int v, int k) const
{
    if (k == Level) return {base[(size_t) v][(size_t) k], false};
    if (drag.active && drag.kind == kKnob && drag.a == v && drag.b == k && tempKnob >= 0) return {tempKnob, true};
    if (editing(v)) {
        const auto& l = kit.lane[(size_t) v].lock[(size_t) editSteps.front()];
        if (kit.lane[(size_t) v].step[(size_t) editSteps.front()] == 'o' && l.has[(size_t) k]) return {l.v[(size_t) k], true};
    }
    return {base[(size_t) v][(size_t) k], false};
}

// ---------------------------------------------------------------- çizim
void OtlakEditor::drawDice(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float size)
{
    auto b = juce::Rectangle<float>(size, size).withCentre(r.getCentre());
    g.setColour(c);
    g.drawRoundedRectangle(b.reduced(size * 0.12f), size * 0.18f, 1.5f);
    const float d = size * 0.12f, cx = b.getCentreX(), cy = b.getCentreY(), o = size * 0.24f;
    for (auto pt : {juce::Point<float>(cx - o, cy - o), {cx + o, cy - o}, {cx, cy}, {cx - o, cy + o}, {cx + o, cy + o}})
        g.fillEllipse(pt.x - d / 2, pt.y - d / 2, d, d);
}

void OtlakEditor::drawLock(juce::Graphics& g, float x, float y, juce::Colour c)
{
    g.setColour(c);
    g.fillRoundedRectangle(x, y + 4, 7, 5, 1);
    juce::Path p;
    p.addCentredArc(x + 3.5f, y + 4, 2.f, 2.6f, 0, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
    g.strokePath(p, juce::PathStrokeType(1.2f));
}

void OtlakEditor::drawKnobRaw(juce::Graphics& g, const Pal& P, juce::Rectangle<float> r, float value, bool locked)
{
    const float size = r.getWidth();
    const float cx = r.getCentreX(), cy = r.getCentreY();
    const float rad = size * 0.4f, w = size > 40 ? 3.f : 2.5f;
    const float a0 = -2.35619f, a1 = 2.35619f, av = a0 + value * (a1 - a0);
    juce::Path tr, val;
    tr.addCentredArc(cx, cy, rad, rad, 0, a0, a1, true);
    g.setColour(P.track);
    g.strokePath(tr, juce::PathStrokeType(w));
    if (value > 0.002f) {
        val.addCentredArc(cx, cy, rad, rad, 0, a0, av, true);
        g.setColour(locked ? P.pl : P.acc);
        g.strokePath(val, juce::PathStrokeType(w));
    }
    const float face = size * 0.28f;
    g.setColour(P.face);
    g.fillEllipse(cx - face, cy - face, face * 2, face * 2);
    if (!P.dark) {
        g.setColour(P.ring);
        g.drawEllipse(cx - face, cy - face, face * 2, face * 2, 1.f);
    }
    const float len = size * 0.23f;
    g.setColour(P.text);
    g.drawLine(cx, cy, cx + std::sin(av) * len, cy - std::cos(av) * len, size > 40 ? 2.f : 1.6f);
}

void OtlakEditor::drawKnobCell(juce::Graphics& g, const Pal& P, float cx, float y, int v, int k, float ks)
{
    const bool big = ks > 40;
    auto kr = juce::Rectangle<float>(ks, ks).withCentre({cx, y + ks / 2});
    const auto dv = dispVal(v, k);
    drawKnobRaw(g, P, kr, dv.v, dv.locked);
    add(kr, kKnob, v, k);
    const bool lk = kit.voice[(size_t) v].locks[(size_t) k] && k != Level;
    const auto f = lbl(big ? 9.5f : 9.f, false, 0.08f);
    const juce::String name = kParamNames[k];
    const float tw = f.getStringWidthFloat(name) + (lk ? 11.f : 0.f);
    auto lr = juce::Rectangle<float>(tw + 8, 14).withCentre({cx, y + ks + (big ? 8.f : 7.f)});
    if (lk) drawLock(g, lr.getX() + 4, lr.getY() + 2, P.pl);
    text(g, name, lr.withTrimmedLeft(lk ? 15.f : 4.f), f, lk ? P.pl : (k == Level ? P.text : P.muted), juce::Justification::centredLeft);
    if (k != Level) add(lr, kParamLock, v, k);
    text(g, formatParam(k, dv.v), juce::Rectangle<float>(90, 14).withCentre({cx, y + ks + (big ? 22.f : 20.f)}), mono(big ? 11.f : 10.f),
         dv.locked ? P.pl : P.text, juce::Justification::centred);
}

void OtlakEditor::paint(juce::Graphics& g)
{
    hits.clear();
    const Pal P = pal();
    g.fillAll(P.bg);
    g.addTransform(juce::AffineTransform::scale((float) getWidth() / (float) W));
    paintHeader(g, P);
    if (!P.dark) {
        paintKar(g, P);
        paintSeq(g, P, {24.f, 414.f, 1152.f, 326.f});
    } else {
        paintFjord(g, P);
        paintSeq(g, P, {24.f, 466.f, 1152.f, 274.f});
    }
}

void OtlakEditor::paintHeader(juce::Graphics& g, const Pal& P)
{
    g.setColour(P.line);
    g.fillRect(0.f, 63.f, (float) W, 1.f);
    text(g, "OTLAK", {24, 0, 150, 64}, lbl(15, true, 0.22f), P.text);

    const bool running = proc.seqRunning.load();
    const bool hostPlay = proc.hostPlaying.load();
    // play
    juce::Rectangle<float> play(200, 10, 44, 44);
    if (P.dark) { g.setColour(P.acc); g.fillEllipse(play); }
    else { g.setColour(P.panel); g.fillEllipse(play); g.setColour(P.ring); g.drawEllipse(play.reduced(0.5f), 1.f); }
    g.setColour(P.dark ? P.accText : P.text);
    if (running) g.fillRoundedRectangle(play.withSizeKeepingCentre(10, 10), 1.5f);
    else {
        juce::Path t;
        t.addTriangle(play.getCentreX() - 3.5f, play.getCentreY() - 5.5f, play.getCentreX() - 3.5f, play.getCentreY() + 5.5f,
                      play.getCentreX() + 6.f, play.getCentreY());
        g.fillPath(t);
    }
    add(play, kPlay);
    // rec
    juce::Rectangle<float> rec(252, 10, 44, 44);
    const bool r = proc.rec.load();
    g.setColour(r ? P.pl : P.panel);
    g.fillEllipse(rec);
    if (!r) { g.setColour(P.ring); g.drawEllipse(rec.reduced(0.5f), 1.f); }
    g.setColour(r ? P.panel : P.pl);
    g.fillEllipse(rec.withSizeKeepingCentre(11, 11));
    add(rec, kRec);
    // quant / mode
    auto tog = [&](juce::Rectangle<float> b, const juce::String& a, const juce::String& bTxt, Kind k) {
        g.setColour(P.line);
        g.drawRoundedRectangle(b.reduced(0.5f), P.radp, 1.f);
        g.setFont(lbl(9));
        g.setColour(P.muted);
        g.drawText(a, b.withTrimmedLeft(8), juce::Justification::centredLeft);
        const float aw = a.isEmpty() ? 0.f : lbl(9).getStringWidthFloat(a) + 5.f;
        text(g, bTxt, b.withTrimmedLeft(8 + aw), lbl(9, true), P.text);
        add(b, k);
    };
    tog({304, 9, 92, 22}, "QUANT", proc.quant.load() ? "ON" : "OFF", kQuant);
    tog({304, 34, 92, 22}, "", proc.replaceMode.load() ? "REPLACE" : "OVERDUB", kRecMode);
    // bpm
    text(g, hostPlay ? "HOST" : "BPM", {410, 14, 70, 12}, lbl(9), P.muted);
    text(g, juce::String(proc.hostBpm.load(), 2), {410, 28, 80, 20}, mono(13), P.text);
    g.setColour(P.line);
    g.fillRect(488.f, 18.f, 1.f, 28.f);
    text(g, u8("TEMPO ×"), {500, 0, 60, 64}, lbl(9), P.muted);
    auto small = [&](juce::Rectangle<float> b, const juce::String& s, Kind k) {
        g.setColour(P.line);
        g.drawRoundedRectangle(b.reduced(0.5f), P.radp, 1.f);
        text(g, s, b, sans(14), P.text, juce::Justification::centred);
        add(b, k);
    };
    small({562, 18, 28, 28}, u8("−"), kGmDown);
    text(g, kMultNames[proc.gmult()], {592, 0, 40, 64}, mono(14), P.text, juce::Justification::centred);
    small({634, 18, 28, 28}, "+", kGmUp);
    g.setColour(P.line);
    g.fillRect(674.f, 18.f, 1.f, 28.f);
    text(g, "MASTER", {686, 8, 80, 12}, lbl(9), P.muted);
    juce::Rectangle<float> vk(686, 24, 32, 32);
    drawKnobRaw(g, P, vk, proc.volume(), false);
    text(g, juce::String(juce::roundToInt(proc.volume() * 100)), {722, 24, 40, 32}, mono(12), P.text);
    add(vk.getUnion({722, 24, 40, 32}), kVol);

    // sağ grup
    juce::Rectangle<float> seg(800, 15, 128, 34);
    g.setColour(P.line);
    g.drawRoundedRectangle(seg.reduced(0.5f), P.rad, 1.f);
    auto half = seg.withWidth(64);
    for (int i = 0; i < 2; ++i) {
        const bool on = (i == 1) == P.dark;
        auto b = half.translated(64.f * i, 0).reduced(1);
        if (on) { g.setColour(P.rowsel); g.fillRoundedRectangle(b, P.rad - 1); }
        text(g, i ? "FJORD" : "KAR", b, lbl(10, on), on ? P.text : P.muted, juce::Justification::centred);
        add(b, kSkin, i);
    }
    juce::Rectangle<float> kitB(938, 15, 76, 34);
    if (P.dark) { g.setColour(P.acc); g.drawRoundedRectangle(kitB.reduced(0.5f), P.rad, 1.f); }
    else { g.setColour(P.text); g.fillRoundedRectangle(kitB, P.rad); }
    const auto kc = P.dark ? P.acc : P.bg;
    drawDice(g, kitB.withWidth(36).translated(4, 0), kc, 17);
    text(g, "KIT", kitB.withTrimmedLeft(38), lbl(10), kc);
    add(kitB, kKit);
    juce::Rectangle<float> und(1022, 15, 70, 34);
    g.setColour(P.line);
    g.drawRoundedRectangle(und.reduced(0.5f), P.rad, 1.f);
    text(g, "UNDO", und.withTrimmedLeft(10), lbl(10), P.text);
    text(g, juce::String(proc.undoCount()), und.withTrimmedRight(10), mono(11), P.muted, juce::Justification::centredRight);
    add(und, kUndo);
    g.setColour(midiFlash > 0 ? P.acc.withAlpha(0.3f + 0.7f * midiFlash) : P.track);
    g.fillEllipse(1104, 29, 7, 7);
    text(g, "MIDI", {1116, 0, 60, 64}, lbl(9), P.muted);
}

void OtlakEditor::paintKar(juce::Graphics& g, const Pal& P)
{
    const float cw = (1152.f - 60.f) / 6.f;
    for (int v = 0; v < kVoices; ++v) {
        juce::Rectangle<float> c(24.f + v * (cw + 12.f), 80.f, cw, 318.f);
        g.setColour(P.panel);
        g.fillRoundedRectangle(c, 10);
        const bool ed = editing(v);
        g.setColour(ed ? P.pl : (sel == v ? P.acc : P.line));
        g.drawRoundedRectangle(c.reduced(0.5f), 10, ed ? 2.f : 1.f);
        // başlık
        juce::Rectangle<float> nm(c.getX() + 12, c.getY() + 10, cw - 24 - 48, 44);
        text(g, kVoiceNames[v], nm.withHeight(24).translated(0, 2), lbl(12, true, 0.14f), P.text, juce::Justification::bottomLeft);
        text(g, juce::String("MIDI ") + kVoiceNotes[v] + u8(" · key ") + juce::String(v + 1), nm.withTrimmedTop(26), mono(10), P.muted,
             juce::Justification::topLeft);
        add(nm, kSelVoice, v);
        juce::Rectangle<float> dice(c.getRight() - 12 - 44, c.getY() + 10, 44, 44);
        g.setColour(P.btn);
        g.fillRoundedRectangle(dice, 10);
        g.setColour(P.line);
        g.drawRoundedRectangle(dice.reduced(0.5f), 10, 1);
        drawDice(g, dice, P.text, 22);
        add(dice, kDice, v);
        // algoritma satırı
        const float ay = c.getY() + 60;
        g.setColour(P.line2);
        g.fillRect(c.getX() + 12, ay, cw - 24, 1.f);
        g.fillRect(c.getX() + 12, ay + 34, cw - 24, 1.f);
        juce::Rectangle<float> ab(c.getX() + 12, ay + 1, cw - 24 - 50, 33);
        text(g, kAlgoNames[algos[(size_t) v]], ab.withHeight(17), lbl(10, true, 0.1f), P.acc, juce::Justification::bottomLeft);
        text(g, u8(kRoutes[algos[(size_t) v]]), ab.withTrimmedTop(17), mono(9), P.muted, juce::Justification::topLeft);
        add(ab, kAlgo, v);
        const bool al = kit.voice[(size_t) v].algoLock;
        juce::Rectangle<float> alr(c.getRight() - 12 - 48, ay + 1, 48, 33);
        if (al) drawLock(g, alr.getX() + 4, alr.getCentreY() - 6, P.pl);
        text(g, "ALGO", alr.withTrimmedLeft(al ? 15.f : 8.f), lbl(9, false, 0.08f), al ? P.pl : P.muted);
        add(alr, kAlgoLock, v);
        // knob'lar
        const float colW = (cw - 24) / 4.f;
        for (int k = 0; k < kParams; ++k) {
            const float cx = c.getX() + 12 + colW * (k % 4 + 0.5f);
            const float y = c.getY() + 104 + (k / 4) * 70.f;
            drawKnobCell(g, P, cx, y, v, k, 36.f);
        }
    }
}

void OtlakEditor::paintFjord(juce::Graphics& g, const Pal& P)
{
    // ses listesi
    juce::Rectangle<float> nav(24, 80, 256, 370);
    g.setColour(P.panel);
    g.fillRoundedRectangle(nav, 12);
    for (int v = 0; v < kVoices; ++v) {
        juce::Rectangle<float> row(32, 88.f + v * 50.f, 240, 48);
        if (sel == v) { g.setColour(P.rowsel); g.fillRoundedRectangle(row, 8); }
        g.setColour(flash[(size_t) v] > 0 ? P.acc.withAlpha(0.3f + 0.7f * flash[(size_t) v]) : P.track);
        g.fillEllipse(row.getX() + 12, row.getCentreY() - 3.5f, 7, 7);
        juce::Rectangle<float> nb(row.getX() + 26, row.getY(), 160, 48);
        text(g, kVoiceNames[v], nb.withHeight(26), lbl(12, true, 0.14f), P.text, juce::Justification::bottomLeft);
        text(g, juce::String(kVoiceNotes[v]) + u8(" · ") + mult(effs[(size_t) v]) + u8(" · ") + kAlgoNames[algos[(size_t) v]],
             nb.withTrimmedTop(27), mono(10), P.muted, juce::Justification::topLeft);
        add(nb, kSelVoice, v);
        juce::Rectangle<float> dice(row.getRight() - 46, row.getY() + 4, 40, 40);
        g.setColour(P.line);
        g.drawEllipse(dice.reduced(0.5f), 1);
        drawDice(g, dice, P.text, 18);
        add(dice, kDice, v);
    }
    // editör
    const int v = sel;
    juce::Rectangle<float> ed(296, 80, 880, 370);
    g.setColour(P.panel);
    g.fillRoundedRectangle(ed, 12);
    if (editing(v)) { g.setColour(P.pl); g.drawRoundedRectangle(ed.reduced(1), 12, 2); }
    text(g, kVoiceNames[v], {318, 98, 200, 36}, lbl(24, false, 0.2f), P.text);
    const float tw = lbl(24, false, 0.2f).getStringWidthFloat(kVoiceNames[v]);
    text(g, juce::String("MIDI ") + kVoiceNotes[v] + u8(" · 4-OP FM · ") + kAlgoNames[algos[(size_t) v]], {318 + tw + 16, 104, 300, 30},
         mono(11), P.muted);
    juce::Rectangle<float> mut(940, 98, 110, 36), roll(1060, 98, 94, 36);
    g.setColour(P.line);
    g.drawRoundedRectangle(mut.reduced(0.5f), 18, 1);
    {
        juce::Path w;
        const float x0 = mut.getX() + 14, y0 = mut.getCentreY();
        w.startNewSubPath(x0, y0);
        for (int i = 1; i <= 18; ++i) w.lineTo(x0 + i, y0 - 4.f * std::sin(i / 18.f * 6.2832f * 1.5f));
        g.setColour(P.text);
        g.strokePath(w, juce::PathStrokeType(1.5f));
    }
    text(g, "MUTATE", mut.withTrimmedLeft(42), lbl(10), P.text);
    add(mut, kMutate, v);
    g.setColour(P.acc);
    g.fillRoundedRectangle(roll, 18);
    drawDice(g, roll.withWidth(40).translated(4, 0), P.accText, 16);
    text(g, "ROLL", roll.withTrimmedLeft(42), lbl(10, true), P.accText);
    add(roll, kDice, v);

    for (int k = 0; k < kParams; ++k) drawKnobCell(g, P, 318 + 90.f * (k % 4 + 0.5f), 146 + (k / 4) * 92.f, v, k, 50.f);

    // zarf grafiği
    juce::Rectangle<float> env(706, 150, 448, 160);
    g.setColour(P.env);
    g.fillRoundedRectangle(env, 10);
    text(g, "AMP", {720, 158, 60, 14}, lbl(9), P.muted);
    g.setColour(P.acc);
    g.fillRect(720.f, 178.f, 10.f, 2.f);
    text(g, "PITCH", {760, 158, 60, 14}, lbl(9), P.muted);
    g.setColour(P.pl);
    g.fillRect(760.f, 178.f, 10.f, 2.f);
    Params p;
    for (int k = 0; k < kParams; ++k) p[(size_t) k] = dispVal(v, k).v;
    const double D = 0.008 * std::pow(2.0, p[Decay] * 9.0), Wd = juce::jmax(0.05, D * 1.1), tauA = D / 6.9;
    const double tauP = juce::jmin(0.003 + 0.03 * p[PEnv], tauA * 0.8 + 0.001);
    text(g, juce::String("0 - ") + (Wd < 1 ? juce::String(juce::roundToInt(Wd * 1000)) + " ms" : juce::String(Wd, 2) + " s"),
         {1000, 158, 140, 14}, mono(9), P.muted, juce::Justification::centredRight);
    juce::Rectangle<float> plot(720, 184, 420, 118);
    g.setColour(P.line2);
    for (int i = 1; i < 4; ++i) g.fillRect(plot.getX() + plot.getWidth() * i / 4.f, plot.getY(), 1.f, plot.getHeight());
    g.setColour(P.line);
    g.fillRect(plot.getX(), plot.getBottom() - 1, plot.getWidth(), 1.f);
    juce::Path pa, pp;
    for (int i = 0; i <= 80; ++i) {
        const double t = i / 80.0 * Wd;
        const float x = plot.getX() + plot.getWidth() * i / 80.f;
        const float ya = plot.getBottom() - 2 - (float) std::exp(-t / tauA) * (plot.getHeight() - 6);
        const float yp = plot.getBottom() - 2 - (float) (p[PEnv] * std::exp(-t / tauP)) * (plot.getHeight() - 6);
        if (i == 0) { pa.startNewSubPath(x, ya); pp.startNewSubPath(x, yp); }
        else { pa.lineTo(x, ya); pp.lineTo(x, yp); }
    }
    g.setColour(P.pl);
    juce::Path dashed;
    const float dash[] = {4.f, 3.f};
    juce::PathStrokeType(1.6f).createDashedStroke(dashed, pp, dash, 2);
    g.fillPath(dashed);
    g.setColour(P.acc);
    g.strokePath(pa, juce::PathStrokeType(2.f));

    // algoritmalar
    float x = 706;
    for (int a = 0; a < kAlgos; ++a) {
        const float w = lbl(9).getStringWidthFloat(kAlgoNames[a]) + 20;
        juce::Rectangle<float> b(x, 324, w, 28);
        const bool on = algos[(size_t) v] == a;
        if (on) { g.setColour(P.acc); g.fillRoundedRectangle(b, P.radp); }
        else { g.setColour(P.line); g.drawRoundedRectangle(b.reduced(0.5f), P.radp, 1); }
        text(g, kAlgoNames[a], b, lbl(9, on), on ? P.accText : P.muted, juce::Justification::centred);
        add(b, kAlgoSet, v, a);
        x += w + 4;
    }
    const bool al = kit.voice[(size_t) v].algoLock;
    juce::Rectangle<float> alr(x + 6, 324, 60, 28);
    if (al) drawLock(g, alr.getX(), alr.getCentreY() - 6, P.pl);
    text(g, "LOCK", alr.withTrimmedLeft(al ? 12.f : 0.f), lbl(9), al ? P.pl : P.muted);
    add(alr, kAlgoLock, v);
    // operatör oranları
    text(g, u8(kRoutes[algos[(size_t) v]]), {706, 364, 120, 34}, mono(13), P.text);
    const juce::String opv[] = {"1.00", juce::String(0.25 + p[Ratio] * 7.75, 2), juce::String(kit.voice[(size_t) v].r3, 2),
                                juce::String(kit.voice[(size_t) v].r4, 2), formatParam(Feedback, p[Feedback]), formatParam(Index, p[Index])};
    const char* opn[] = {"OP1", "OP2", "OP3", "OP4", "FB", "IDX"};
    for (int i = 0; i < 6; ++i) {
        const float ox = 836 + i * 52.f;
        text(g, opn[i], {ox, 364, 50, 14}, lbl(9), P.muted);
        text(g, opv[i], {ox, 379, 50, 16}, mono(12), i == 4 ? P.pl : P.text);
    }
}

void OtlakEditor::paintSeq(juce::Graphics& g, const Pal& P, juce::Rectangle<float> area)
{
    g.setColour(P.panel);
    g.fillRoundedRectangle(area, P.dark ? 12.f : 10.f);
    if (!P.dark) { g.setColour(P.line); g.drawRoundedRectangle(area.reduced(0.5f), 10, 1); }
    const float x0 = area.getX() + 18;
    // sütunlar
    const float cName = x0, cSteps = cName + 90, cClr = cSteps + 510, cLen = cClr + 34, cRate = cLen + 44, cSh = cRate + 74,
                cEv = cSh + 50, cAmt = cEv + 54, cMode = cAmt + 84, cSlew = cMode + 64, cNow = cSlew + 50;
    const float hy = area.getY() + 12;
    const auto hf = lbl(9);
    text(g, "SEQUENCER", {cName, hy, 90, 20}, hf, P.muted);
    g.setColour(P.acc);
    g.fillRoundedRectangle(cSteps, hy + 6, 8, 8, 2);
    text(g, "TRIG", {cSteps + 13, hy, 50, 20}, hf, P.muted);
    g.setColour(P.pl);
    g.fillRoundedRectangle(cSteps + 60, hy + 6, 8, 8, 2);
    text(g, "P-LOCK", {cSteps + 73, hy, 60, 20}, hf, P.muted);
    text(g, "LEN", {cLen, hy, 40, 20}, hf, P.muted);
    text(g, u8("RATE ×"), {cRate, hy, 64, 20}, hf, P.muted);
    text(g, "S&H", {cSh, hy, 40, 20}, hf, P.muted);
    text(g, "EVERY", {cEv, hy, 50, 20}, hf, P.muted);
    text(g, "AMOUNT", {cAmt, hy, 74, 20}, hf, P.muted);
    text(g, "MODE", {cMode, hy, 54, 20}, hf, P.muted);
    text(g, "SLEW", {cSlew, hy, 44, 20}, hf, P.muted);
    text(g, "NOW", {cNow, hy, 50, 20}, hf, P.muted);

    const float rowH = P.dark ? 30.f : 40.f;
    const float sw = 26.f, sh = P.dark ? 22.f : 26.f;
    for (int v = 0; v < kVoices; ++v) {
        const auto& L = kit.lane[(size_t) v];
        const float ry = area.getY() + 32 + v * rowH;
        g.setColour(P.line2);
        g.fillRect(x0, ry, area.getWidth() - 36, 1.f);
        const float cy = ry + rowH / 2;
        // ad / mute
        juce::Rectangle<float> nm(cName, ry + 2, 84, rowH - 4);
        const float al = L.mute ? 0.35f : 1.f;
        g.setColour((flash[(size_t) v] > 0 && !L.mute ? P.acc.withAlpha(0.3f + 0.7f * flash[(size_t) v]) : P.track).withMultipliedAlpha(al));
        g.fillEllipse(cName, cy - 3.5f, 7, 7);
        text(g, kVoiceNames[v], nm.withTrimmedLeft(15), lbl(11, true), P.text.withAlpha(al));
        add(nm, kMute, v);
        // adımlar
        float sx = cSteps;
        for (int j = 0; j < kSteps; ++j) {
            if (j > 0 && j % 4 == 0) sx += 6;
            juce::Rectangle<float> b(sx, cy - sh / 2, sw, sh);
            const bool inside = j < L.len;
            const char c = inside ? L.step[(size_t) j] : '-';
            const float rr = P.dark ? 4.f : 5.f;
            if (c == 'x') { g.setColour(P.acc); g.fillRoundedRectangle(b, rr); }
            else if (c == 'o') { g.setColour(P.pl); g.fillRoundedRectangle(b, rr); }
            else if (c == '.') {
                g.setColour(P.cell); g.fillRoundedRectangle(b, rr);
                g.setColour(P.cellb); g.drawRoundedRectangle(b.reduced(0.5f), rr, 1);
            } else {
                g.setColour(P.line.withAlpha(0.7f));
                juce::Path pth, ds;
                pth.addRoundedRectangle(b.reduced(0.5f), rr);
                const float dl[] = {2.f, 2.f};
                juce::PathStrokeType(1.f).createDashedStroke(ds, pth, dl, 2);
                g.fillPath(ds);
            }
            if (inside && L.micro[(size_t) j] != 0.f && c != '.') {
                g.setColour(P.bg.withAlpha(0.8f));
                const float mx = b.getCentreX() + L.micro[(size_t) j] * sw;
                g.fillRect(mx - 0.75f, b.getBottom() - 5, 1.5f, 3.f);
            }
            if (steps[(size_t) v] == j && proc.seqRunning.load()) { g.setColour(P.text); g.drawRoundedRectangle(b.expanded(2), rr + 2, 2); }
            if (editing(v) && std::find(editSteps.begin(), editSteps.end(), j) != editSteps.end()) {
                g.setColour(P.pl);
                g.drawRoundedRectangle(b.expanded(3.5f), rr + 3, 2);
            }
            add(b, kStep, v, j);
            sx += sw + 4;
        }
        // temizle
        juce::Rectangle<float> clr(cClr, cy - 12, 24, 24);
        g.setColour(P.line);
        g.drawRoundedRectangle(clr.reduced(0.5f), P.radp > 8 ? 12.f : 6.f, 1);
        g.setColour(P.muted);
        g.drawLine(clr.getCentreX() - 4, clr.getCentreY() - 4, clr.getCentreX() + 4, clr.getCentreY() + 4, 1.4f);
        g.drawLine(clr.getCentreX() + 4, clr.getCentreY() - 4, clr.getCentreX() - 4, clr.getCentreY() + 4, 1.4f);
        add(clr, kClear, v);
        // uzunluk
        juce::Rectangle<float> len(cLen, cy - 14, 34, 28);
        text(g, juce::String(L.len), len, mono(12), P.text);
        add(len, kLen, v);
        // çarpan
        juce::Rectangle<float> rate(cRate, cy - 14, 64, 28);
        g.setColour(P.btn);
        g.fillRoundedRectangle(rate, P.radp);
        g.setColour(P.line);
        g.drawRoundedRectangle(rate.reduced(0.5f), P.radp, 1);
        text(g, mult(kMultValues[L.mult]), rate, mono(12), P.text, juce::Justification::centred);
        add(rate, kRate, v);
        // S&H
        const float dim = L.shOn ? 1.f : 0.4f;
        juce::Rectangle<float> pill(cSh, cy - 10, 40, 20);
        g.setColour(L.shOn ? P.acc : P.line2);
        g.fillRoundedRectangle(pill, 10);
        text(g, "S&H", pill, lbl(9), L.shOn ? P.accText : P.muted, juce::Justification::centred);
        add(pill, kShOn, v);
        juce::Rectangle<float> ev(cEv, cy - 14, 50, 28);
        text(g, kEveryNames[L.shEvery], ev, mono(11), P.text.withAlpha(dim));
        add(ev, kShEvery, v);
        juce::Rectangle<float> amt(cAmt, cy - 14, 76, 28);
        g.setColour(P.track.withMultipliedAlpha(dim));
        g.fillRoundedRectangle(cAmt, cy - 1.5f, 40, 3, 1.5f);
        g.setColour(P.acc.withMultipliedAlpha(dim));
        g.fillRoundedRectangle(cAmt, cy - 1.5f, 40.f * L.shAmt / 100.f, 3, 1.5f);
        text(g, juce::String(L.shAmt) + "%", amt.withTrimmedLeft(46), mono(10), P.muted.withAlpha(dim));
        add(amt, kShAmt, v);
        juce::Rectangle<float> md(cMode, cy - 14, 56, 28);
        text(g, L.shQuant ? "QUANT" : "FREE", md, lbl(10, false, 0.08f), P.muted.withAlpha(dim));
        add(md, kShMode, v);
        juce::Rectangle<float> sl(cSlew, cy - 14, 44, 28);
        text(g, juce::String(L.shSlew) + "%", sl, mono(11), P.text.withAlpha(dim));
        add(sl, kShSlew, v);
        text(g, mult(effs[(size_t) v]), {cNow, cy - 14, 60, 28}, mono(12), P.acc);
    }

    // alt satır
    const float fy = area.getBottom() - 42;
    g.setColour(P.line2);
    g.fillRect(x0, fy, area.getWidth() - 36, 1.f);
    const float by = fy + 8;
    juce::Rectangle<float> pat(x0, by, 112, 28);
    g.setColour(P.line);
    g.drawRoundedRectangle(pat.reduced(0.5f), P.rad > 10 ? 14.f : P.rad, 1);
    drawDice(g, pat.withWidth(32).translated(2, 0), P.text, 14);
    text(g, "PATTERN", pat.withTrimmedLeft(32), lbl(10), P.text);
    add(pat, kPattern);
    auto pair = [&](float x, const juce::String& a, const juce::String& b, Kind k) {
        const float aw = lbl(10).getStringWidthFloat(a);
        text(g, a, {x, by, aw + 2, 28}, lbl(10), P.muted);
        const float bw = mono(11).getStringWidthFloat(b) + 6;
        text(g, b, {x + aw + 6, by, bw, 28}, mono(11), P.text);
        add({x, by, aw + 6 + bw, 28}, k);
        return x + aw + 6 + bw + 20;
    };
    float fx = x0 + 132;
    const int rs = kResyncBars[kit.resync];
    fx = pair(fx, "RESYNC", rs ? juce::String(rs) + " BAR" : juce::String("OFF"), kResync);
    fx = pair(fx, "SWING", juce::String(kit.swing) + "%", kSwing);
    fx = pair(fx, "HUMANIZE", juce::String(kit.human) + "%", kHuman);
    fx = pair(fx, "SEQ", kit.seqOn ? "ON" : "OFF", kSeqOn);
    if (editLane >= 0 && !editSteps.empty()) {
        auto s = editSteps;
        std::sort(s.begin(), s.end());
        juce::String list;
        for (size_t i = 0; i < s.size(); ++i) list << (i ? ", " : "") << (s[i] + 1);
        juce::Rectangle<float> done(area.getRight() - 18 - 64, by, 64, 28);
        g.setColour(P.pl);
        g.drawRoundedRectangle(done.reduced(0.5f), P.radp, 1);
        text(g, "DONE", done, lbl(10, true), P.pl, juce::Justification::centred);
        add(done, kSelDone);
        text(g, juce::String("EDITING ") + kVoiceNames[editLane] + u8(" · STEP ") + list + u8(" — knobs set step values · double-click clears"),
             {fx, by, done.getX() - fx - 10, 28}, lbl(10, false, 0.04f), P.pl, juce::Justification::centredRight);
    } else {
        text(g, u8("Right-click / Alt-click steps to edit their sound · keys 1–6 play"), {fx, by, area.getRight() - 18 - fx, 28},
             lbl(10, false, 0.02f), P.muted, juce::Justification::centredRight);
    }
}

// ---------------------------------------------------------------- etkileşim
float OtlakEditor::dragValue(Kind k, int a, int b) const
{
    switch (k) {
        case kKnob: return dispVal(a, b).v;
        case kVol: return proc.volume();
        case kLen: return (kit.lane[(size_t) a].len - 1) / 15.f;
        case kShAmt: return kit.lane[(size_t) a].shAmt / 100.f;
        case kShSlew: return kit.lane[(size_t) a].shSlew / 100.f;
        case kSwing: return (kit.swing - 50) / 25.f;
        case kHuman: return kit.human / 50.f;
        default: return 0.f;
    }
}

void OtlakEditor::setDragValue(Kind k, int a, int b, float x)
{
    x = juce::jlimit(0.f, 1.f, x);
    switch (k) {
        case kKnob:
            if (b != Level && editing(a)) {
                const auto st = editSteps;
                proc.editKit([&](Kit& kt) {
                    auto& L = kt.lane[(size_t) a];
                    for (int j : st) {
                        if (j >= L.len) continue;
                        if (L.step[(size_t) j] != 'o') { L.step[(size_t) j] = 'o'; L.lock[(size_t) j].clear(); }
                        L.lock[(size_t) j].has[(size_t) b] = true;
                        L.lock[(size_t) j].v[(size_t) b] = x;
                    }
                });
            } else if (drag.recMode) {
                proc.recLock(a, b, x);
                tempKnob = x;
            } else {
                proc.dragParam(a, b, x);
            }
            break;
        case kVol: proc.setFloat("volume", x); break;
        case kLen: {
            const int nl = 1 + juce::roundToInt(x * 15.f);
            const auto snap = drag.active ? drag.snap : kit.lane[(size_t) a];
            proc.editKit([&](Kit& kt) {
                auto& L = kt.lane[(size_t) a];
                for (int j = 0; j < kSteps; ++j) {
                    // sürükleme içinde kısaltıp uzatınca adımlar geri gelsin
                    L.step[(size_t) j] = j < nl ? (j < snap.len ? snap.step[(size_t) j] : '.') : snap.step[(size_t) j];
                    if (j >= nl) L.step[(size_t) j] = '.';
                    L.lock[(size_t) j] = j < nl && j < snap.len ? snap.lock[(size_t) j] : StepLock();
                    L.micro[(size_t) j] = j < nl && j < snap.len ? snap.micro[(size_t) j] : 0.f;
                }
                L.setLength(nl);
            });
            break;
        }
        case kShAmt: proc.editKit([&](Kit& kt) { kt.lane[(size_t) a].shAmt = juce::roundToInt(x * 100); }); break;
        case kShSlew: proc.editKit([&](Kit& kt) { kt.lane[(size_t) a].shSlew = juce::roundToInt(x * 100); }); break;
        case kSwing: proc.editKit([&](Kit& kt) { kt.swing = 50 + juce::roundToInt(x * 25); }); break;
        case kHuman: proc.editKit([&](Kit& kt) { kt.human = juce::roundToInt(x * 50); }); break;
        default: break;
    }
    refreshNow();
    repaint();
}

void OtlakEditor::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    const auto p = toDesign(e.position);
    const Hit* h = hitAt(p);
    if (!h) return;
    const Kind k = h->kind;
    const bool draggable = k == kKnob || k == kVol || k == kLen || k == kShAmt || k == kShSlew || k == kSwing || k == kHuman;
    if (draggable) {
        drag = Drag();
        drag.active = true;
        drag.kind = k;
        drag.a = h->a;
        drag.b = h->b;
        drag.lastY = p.y;
        drag.v = dragValue(k, h->a, h->b);
        if (k == kLen) drag.snap = kit.lane[(size_t) h->a];
        if (k == kKnob) {
            drag.recMode = !editing(h->a) && proc.rec.load() && proc.seqRunning.load();
            if (h->b == Level) drag.recMode = false;
            if ((h->b == Level || !editing(h->a)) && !drag.recMode) proc.beginParam(h->a, h->b);
            tempKnob = -1;
        }
        return;
    }
    if (k == kStep && (e.mods.isPopupMenu() || e.mods.isAltDown())) {
        selectStep(h->a, h->b);
        return;
    }
    click(*h, e);
}

void OtlakEditor::mouseDrag(const juce::MouseEvent& e)
{
    if (!drag.active) return;
    const auto p = toDesign(e.position);
    const float dy = drag.lastY - p.y;
    drag.lastY = p.y;
    if (std::abs(dy) > 0) drag.moved = true;
    drag.v = juce::jlimit(0.f, 1.f, drag.v + dy * (e.mods.isShiftDown() ? 0.0012f : 0.005f));
    setDragValue(drag.kind, drag.a, drag.b, drag.v);
}

void OtlakEditor::mouseUp(const juce::MouseEvent&)
{
    if (!drag.active) return;
    if (drag.kind == kKnob) {
        const int v = drag.a;
        if (drag.b != Level && editing(v)) {
            if (drag.moved && !proc.seqRunning.load()) proc.auditionStep(v, editSteps.front());
        } else if (!drag.recMode) {
            proc.endParam(v, drag.b);
            if (drag.moved && !proc.seqRunning.load()) proc.audition(v, proc.baseParams(v), 0.9f);
        }
    }
    drag = Drag();
    tempKnob = -1;
    refreshNow();
    repaint();
}

void OtlakEditor::mouseDoubleClick(const juce::MouseEvent& e)
{
    const Hit* h = hitAt(toDesign(e.position));
    if (!h || h->kind != kKnob) return;
    const int v = h->a, k = h->b;
    if (k != Level && editing(v)) {
        const auto st = editSteps;
        proc.editKit([&](Kit& kt) {
            auto& L = kt.lane[(size_t) v];
            for (int j : st) {
                L.lock[(size_t) j].has[(size_t) k] = false;
                if (!L.lock[(size_t) j].any() && L.step[(size_t) j] == 'o') L.step[(size_t) j] = 'x';
            }
        });
    } else {
        proc.setParam(v, k, kInit[v][k]);
    }
    refreshNow();
    repaint();
}

void OtlakEditor::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const Hit* h = hitAt(toDesign(e.position));
    if (!h) return;
    const float dir = w.deltaY > 0 ? 1.f : (w.deltaY < 0 ? -1.f : 0.f);
    if (dir == 0) return;
    switch (h->kind) {
        case kKnob:
            if (h->b != Level && editing(h->a)) { drag.active = false; setDragValue(kKnob, h->a, h->b, dispVal(h->a, h->b).v + dir * 0.02f); }
            else proc.setParam(h->a, h->b, base[(size_t) h->a][(size_t) h->b] + dir * (e.mods.isShiftDown() ? 0.004f : 0.02f));
            break;
        case kLen: setDragValue(kLen, h->a, 0, dragValue(kLen, h->a, 0) + dir / 15.f); break;
        case kVol: case kShAmt: case kShSlew: case kSwing: case kHuman:
            setDragValue(h->kind, h->a, h->b, dragValue(h->kind, h->a, h->b) + dir * 0.02f); break;
        default: break;
    }
    refreshNow();
    repaint();
}

void OtlakEditor::hitLive(int v, bool accent)
{
    proc.liveHit(v, accent ? 1.f : 0.8f);
}

bool OtlakEditor::keyPressed(const juce::KeyPress& key)
{
    const int c = key.getKeyCode();
    if (c >= '1' && c <= '6') { hitLive(c - '1', key.getModifiers().isShiftDown()); return true; }
    // Shift'li rakamlar bazı klavyelerde farklı karakter üretir
    const juce::juce_wchar ch = key.getTextCharacter();
    const char* shifted = "!\"#$%&";
    for (int i = 0; i < 6; ++i)
        if (ch == (juce::juce_wchar) shifted[i] || (i == 1 && ch == '@')) { hitLive(i, true); return true; }
    if (key == juce::KeyPress::escapeKey && editLane >= 0) { clearEdit(); repaint(); return true; }
    return false;
}

void OtlakEditor::selectStep(int v, int j)
{
    if (j >= kit.lane[(size_t) v].len) return;
    if (editLane != v) { editLane = v; editSteps.clear(); }
    auto it = std::find(editSteps.begin(), editSteps.end(), j);
    if (it != editSteps.end()) editSteps.erase(it);
    else editSteps.push_back(j);
    if (editSteps.empty()) clearEdit();
    else sel = v;
    repaint();
}

void OtlakEditor::click(const Hit& h, const juce::MouseEvent& e)
{
    const bool sh = e.mods.isShiftDown();
    const int v = h.a;
    switch (h.kind) {
        case kPlay: proc.freeRun = !proc.freeRun.load(); if (proc.hostPlaying.load()) proc.freeRun = false; break;
        case kRec: {
            const bool on = !proc.rec.load();
            proc.rec = on;
            if (on && !proc.hostPlaying.load()) proc.freeRun = true;
            break;
        }
        case kQuant: proc.quant = !proc.quant.load(); break;
        case kRecMode: proc.replaceMode = !proc.replaceMode.load(); break;
        case kGmDown: proc.setChoice("gmult", proc.gmult() - 1); break;
        case kGmUp: proc.setChoice("gmult", proc.gmult() + 1); break;
        case kSkin: proc.setSkin(h.a ? "fjord" : "kar"); break;
        case kKit: proc.pushUndo(); proc.rollKit(sh); if (!proc.seqRunning.load()) for (int i = 0; i < kVoices; ++i) proc.audition(i, proc.baseParams(i), 0.9f); break;
        case kUndo: proc.undo(); break;
        case kSelVoice:
            if (editLane != v) clearEdit();
            sel = v;
            proc.audition(v, proc.baseParams(v), 0.9f);
            break;
        case kDice: proc.pushUndo(); proc.rollVoice(v, sh); sel = v; proc.audition(v, proc.baseParams(v), 0.9f); break;
        case kMutate: proc.pushUndo(); proc.rollVoice(v, true); proc.audition(v, proc.baseParams(v), 0.9f); break;
        case kAlgo: proc.setChoice(OtlakProcessor::algoId(v), (algos[(size_t) v] + (sh ? kAlgos - 1 : 1)) % kAlgos); proc.audition(v, proc.baseParams(v), 0.9f); break;
        case kAlgoSet: proc.setChoice(OtlakProcessor::algoId(v), h.b); proc.audition(v, proc.baseParams(v), 0.9f); break;
        case kAlgoLock: proc.editKit([&](Kit& kt) { kt.voice[(size_t) v].algoLock = !kt.voice[(size_t) v].algoLock; }); break;
        case kParamLock: proc.editKit([&](Kit& kt) { auto& l = kt.voice[(size_t) v].locks[(size_t) h.b]; l = !l; }); break;
        case kStep: {
            const int j = h.b;
            const Params bp = proc.baseParams(v);
            bool audition = false;
            proc.editKit([&](Kit& kt) {
                auto& L = kt.lane[(size_t) v];
                if (j >= L.len) {
                    for (int q = L.len; q <= j; ++q) { L.step[(size_t) q] = '.'; L.lock[(size_t) q].clear(); L.micro[(size_t) q] = 0; }
                    L.setLength(j + 1);
                    return;
                }
                char& c = L.step[(size_t) j];
                if (c == 'o' && sh) {
                    std::mt19937 r((unsigned) juce::Random::getSystemRandom().nextInt());
                    L.lock[(size_t) j] = randomVariant(bp, r);
                    audition = true;
                    return;
                }
                L.micro[(size_t) j] = 0;
                if (c == '.') c = 'x';
                else if (c == 'x') {
                    c = 'o';
                    std::mt19937 r((unsigned) juce::Random::getSystemRandom().nextInt());
                    L.lock[(size_t) j] = randomVariant(bp, r);
                    audition = true;
                } else { c = '.'; L.lock[(size_t) j].clear(); }
            });
            if (audition && !proc.seqRunning.load()) proc.auditionStep(v, j);
            break;
        }
        case kClear: proc.pushUndo(); clearEdit(); proc.clearLanes(sh ? -1 : v); break;
        case kRate: proc.editKit([&](Kit& kt) { auto& m = kt.lane[(size_t) v].mult; m = (m + (sh ? kMults - 1 : 1)) % kMults; }); break;
        case kShOn: proc.editKit([&](Kit& kt) { kt.lane[(size_t) v].shOn = !kt.lane[(size_t) v].shOn; }); break;
        case kShEvery: proc.editKit([&](Kit& kt) { auto& x = kt.lane[(size_t) v].shEvery; x = (x + (sh ? kEvery - 1 : 1)) % kEvery; }); break;
        case kShMode: proc.editKit([&](Kit& kt) { kt.lane[(size_t) v].shQuant = !kt.lane[(size_t) v].shQuant; }); break;
        case kMute: proc.editKit([&](Kit& kt) { kt.lane[(size_t) v].mute = !kt.lane[(size_t) v].mute; }); break;
        case kPattern: proc.pushUndo(); clearEdit(); proc.rollPatterns(sh ? sel : -1); break;
        case kResync: proc.editKit([&](Kit& kt) { kt.resync = (kt.resync + (sh ? kResync - 1 : 1)) % kResync; }); break;
        case kSeqOn: proc.editKit([&](Kit& kt) { kt.seqOn = !kt.seqOn; }); break;
        case kSelDone: clearEdit(); break;
        default: break;
    }
    refreshNow();
    repaint();
}
