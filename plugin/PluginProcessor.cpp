#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace otlak;

// ---------------------------------------------------------------- parametreler
juce::String OtlakProcessor::pid(int v, int k)
{
    return "v" + juce::String(v + 1) + "_" + juce::String(kParamNames[k]).toLowerCase().removeCharacters(".");
}
juce::String OtlakProcessor::algoId(int v) { return "v" + juce::String(v + 1) + "_algo"; }

juce::AudioProcessorValueTreeState::ParameterLayout OtlakProcessor::layout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    juce::StringArray algos, mults;
    for (auto* a : kAlgoNames) algos.add(a);
    for (auto* m : kMultNames) mults.add(m);
    for (int v = 0; v < kVoices; ++v) {
        for (int k = 0; k < kParams; ++k) {
            const int kk = k;
            l.add(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID { pid(v, k), 1 }, juce::String(kVoiceNames[v]) + " " + kParamNames[k],
                juce::NormalisableRange<float>(0.f, 1.f), kInit[v][k],
                juce::AudioParameterFloatAttributes().withStringFromValueFunction(
                    [kk](float x, int) { return juce::String(formatParam(kk, x)); })));
        }
        l.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { algoId(v), 1 },
                                                           juce::String(kVoiceNames[v]) + " ALGO", algos, kInitAlgo[v]));
    }
    l.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { "gmult", 1 }, "Tempo x", mults, kMultOne));
    l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { "volume", 1 }, "Volume",
                                                      juce::NormalisableRange<float>(0.f, 1.f), 0.8f));
    return l;
}

OtlakProcessor::OtlakProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      params(*this, nullptr, "OTLAK", layout()),
      uiRng(std::random_device {}())
{
    for (int v = 0; v < kVoices; ++v) {
        for (int k = 0; k < kParams; ++k) pv[v][k] = params.getRawParameterValue(pid(v, k));
        palgo[v] = params.getRawParameterValue(algoId(v));
        playStep[v] = -1;
        effMult[v] = 1.f;
        hits[v] = 0;
    }
    pgm = params.getRawParameterValue("gmult");
    pvol = params.getRawParameterValue("volume");
    kit.setDefaults();
    for (int v = 0; v < kVoices; ++v) regenVariants(kit.lane[v], baseParams(v), uiRng);

    juce::PropertiesFile::Options o;
    o.applicationName = "Otlak";
    o.filenameSuffix = "settings";
    o.folderName = "Otlak";
    o.osxLibrarySubFolder = "Application Support";
    settings = std::make_unique<juce::PropertiesFile>(o);
    skin = settings->getValue("skin", "kar");
}

bool OtlakProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void OtlakProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    seq.prepare(sampleRate);
    seq.stop();
    for (auto& c : channels) c.prepare(sampleRate);
    trig.reserve(256);
    voiceBuf.assign((size_t) juce::jmax(64, samplesPerBlock), 0.f);
    mixBuf.assign(voiceBuf.size(), 0.f);
}

Params OtlakProcessor::baseParams(int v) const
{
    Params p;
    for (int k = 0; k < kParams; ++k) p[k] = pv[v][k]->load();
    return p;
}

Sound OtlakProcessor::soundFor(int v, const Params& p) const
{
    Sound s;
    s.p = p;
    s.algo = algo(v);
    s.r3 = kit.voice[v].r3;
    s.r4 = kit.voice[v].r4;
    return s;
}

// ---------------------------------------------------------------- ses
void OtlakProcessor::handleNote(int v, float vel, const std::array<Params, kVoices>& base)
{
    trig.push_back({v, 0, -1, vel, base[v]}); // offset sonra düzeltilir
    midiCount++;
    if (!(rec.load() && seq.isRunning())) return;
    int idx;
    float frac;
    if (!seq.locate(kit, v, idx, frac)) return;
    Lane& L = kit.lane[v];
    if (replaceMode.load() && !recTouched[v]) L.clearSteps();
    recTouched[v] = true;
    if (idx >= L.len) return;
    const bool accent = vel >= 110.f / 127.f;
    if (accent) {
        if (L.step[idx] != 'o') {
            L.step[idx] = 'o';
            L.lock[idx] = randomVariant(base[v], seq.rng);
        }
    } else if (L.step[idx] == '.') {
        L.step[idx] = 'x';
        L.lock[idx].clear();
    }
    L.micro[idx] = quant.load() ? 0.f : juce::jlimit(-0.45f, 0.45f, frac);
    if (frac < 0) seq.cancelPending(v, idx); // canlı vuruş zaten çaldı
}

void OtlakProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    if ((int) voiceBuf.size() < n) { voiceBuf.resize((size_t) n); mixBuf.resize((size_t) n); }

    double bpm = 120.0, ppq = 0.0;
    bool hostPlay = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition()) {
            if (auto b = pos->getBpm()) bpm = *b;
            hostPlay = pos->getIsPlaying();
            if (auto p = pos->getPpqPosition()) ppq = *p;
            else hostPlay = false;
        }
    hostBpm = bpm;
    hostPlaying = hostPlay;
    const bool run = hostPlay || freeRun.load();
    const double gm = kMultValues[gmult()];

    std::array<Params, kVoices> base;
    for (int v = 0; v < kVoices; ++v) base[v] = baseParams(v);

    const bool r = rec.load();
    if (r && !lastRec) recTouched.fill(false);
    lastRec = r;

    trig.clear();
    {
        const juce::SpinLock::ScopedLockType lock(kitLock);
        int cur = 0;
        auto runSeg = [&](int upTo) {
            if (upTo <= cur) return;
            const size_t before = trig.size();
            seq.process(kit, base, upTo - cur, bpm, gm, run, hostPlay, ppq + cur / sr * bpm / 60.0, trig);
            for (size_t t = before; t < trig.size(); ++t) trig[t].offset += cur;
            cur = upTo;
        };
        for (const auto meta : midi) {
            const auto m = meta.getMessage();
            if (!m.isNoteOn()) continue;
            const int pos = juce::jlimit(0, n - 1, meta.samplePosition);
            runSeg(pos);
            for (int v = 0; v < kVoices; ++v)
                if (kVoiceMidi[v] == m.getNoteNumber()) {
                    handleNote(v, m.getFloatVelocity(), base);
                    trig.back().offset = pos;
                }
        }
        runSeg(n);
        if (!run) seq.stop();

        // arayüzden gelen önizlemeler
        int s1, n1, s2, n2;
        uiFifo.prepareToRead(uiFifo.getNumReady(), s1, n1, s2, n2);
        auto takeUi = [&](const UiHit& u) {
            if (u.live) { handleNote(u.voice, u.vel, base); midiCount--; }
            else trig.push_back({u.voice, 0, -1, u.vel, u.p});
        };
        for (int i = 0; i < n1; ++i) takeUi(uiHits[(size_t) (s1 + i)]);
        for (int i = 0; i < n2; ++i) takeUi(uiHits[(size_t) (s2 + i)]);
        uiFifo.finishedRead(n1 + n2);

        for (int v = 0; v < kVoices; ++v) {
            playStep[v] = seq.playingStep(kit, v);
            effMult[v] = (float) (seq.isRunning() ? seq.effMult(v) : kMultValues[kit.lane[v].mult]);
        }
        seqRunning = seq.isRunning();

        // ses tanımlarını kilit altındayken hazırla
        std::stable_sort(trig.begin(), trig.end(), [](const Trigger& a, const Trigger& b) { return a.offset < b.offset; });
    }

    std::fill(mixBuf.begin(), mixBuf.begin() + n, 0.f);
    for (int v = 0; v < kVoices; ++v) {
        float* vb = voiceBuf.data();
        std::fill(vb, vb + n, 0.f);
        int pos = 0;
        for (const auto& t : trig) {
            if (t.voice != v) continue;
            channels[(size_t) v].render(vb + pos, t.offset - pos);
            pos = t.offset;
            Sound s;
            s.p = t.p;
            s.algo = algo(v);
            {
                const juce::SpinLock::ScopedLockType lock(kitLock);
                s.r3 = kit.voice[v].r3;
                s.r4 = kit.voice[v].r4;
            }
            channels[(size_t) v].trigger(s, t.vel);
            hits[v]++;
        }
        channels[(size_t) v].render(vb + pos, n - pos);
        for (int i = 0; i < n; ++i) mixBuf[(size_t) i] += vb[i] * 0.6f;
    }
    const float g = volume();
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        float* o = buffer.getWritePointer(ch);
        for (int i = 0; i < n; ++i) o[i] = std::tanh(mixBuf[(size_t) i] * g * 1.2f);
    }
}

// ---------------------------------------------------------------- editor API
void OtlakProcessor::setParam(int v, int k, float x)
{
    if (auto* p = params.getParameter(pid(v, k))) {
        p->beginChangeGesture();
        p->setValueNotifyingHost(juce::jlimit(0.f, 1.f, x));
        p->endChangeGesture();
    }
}
void OtlakProcessor::beginParam(int v, int k) { if (auto* p = params.getParameter(pid(v, k))) p->beginChangeGesture(); }
void OtlakProcessor::dragParam(int v, int k, float x) { if (auto* p = params.getParameter(pid(v, k))) p->setValueNotifyingHost(juce::jlimit(0.f, 1.f, x)); }
void OtlakProcessor::endParam(int v, int k) { if (auto* p = params.getParameter(pid(v, k))) p->endChangeGesture(); }
void OtlakProcessor::setChoice(const juce::String& id, int index)
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(params.getParameter(id))) {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1((float) juce::jlimit(0, p->choices.size() - 1, index)));
        p->endChangeGesture();
    }
}
void OtlakProcessor::setFloat(const juce::String& id, float x)
{
    if (auto* p = params.getParameter(id)) p->setValueNotifyingHost(juce::jlimit(0.f, 1.f, x));
}

Kit OtlakProcessor::copyKit() const
{
    const juce::SpinLock::ScopedLockType l(kitLock);
    return kit;
}

void OtlakProcessor::pushUndo()
{
    Snap s;
    for (int v = 0; v < kVoices; ++v) { s.p[(size_t) v] = baseParams(v); s.algo[(size_t) v] = algo(v); }
    s.kit = copyKit();
    undoStack.push_back(s);
    if (undoStack.size() > 24) undoStack.erase(undoStack.begin());
}

void OtlakProcessor::undo()
{
    if (undoStack.empty()) return;
    const Snap s = undoStack.back();
    undoStack.pop_back();
    for (int v = 0; v < kVoices; ++v) {
        for (int k = 0; k < kParams; ++k) setParam(v, k, s.p[(size_t) v][(size_t) k]);
        setChoice(algoId(v), s.algo[(size_t) v]);
    }
    const bool keepSeqOn = copyKit().seqOn;
    editKit([&](Kit& k) { k = s.kit; k.seqOn = keepSeqOn; });
}

void OtlakProcessor::rollVoice(int v, bool mutate)
{
    Params p = baseParams(v);
    int a = algo(v);
    VoiceExtra x = copyKit().voice[(size_t) v];
    rollSound(v, p, a, x, mutate, uiRng);
    for (int k = 0; k < kParams; ++k) setParam(v, k, p[(size_t) k]);
    setChoice(algoId(v), a);
    editKit([&](Kit& kt) {
        kt.voice[(size_t) v].r3 = x.r3;
        kt.voice[(size_t) v].r4 = x.r4;
        regenVariants(kt.lane[(size_t) v], p, uiRng);
    });
}

void OtlakProcessor::rollKit(bool mutate)
{
    for (int v = 0; v < kVoices; ++v) rollVoice(v, mutate);
}

void OtlakProcessor::rollPatterns(int only)
{
    std::array<Params, kVoices> base;
    for (int v = 0; v < kVoices; ++v) base[(size_t) v] = baseParams(v);
    editKit([&](Kit& kt) {
        for (int v = 0; v < kVoices; ++v)
            if (only < 0 || only == v) rollPattern(kt.lane[(size_t) v], v, base[(size_t) v], uiRng);
    });
}

void OtlakProcessor::clearLanes(int only)
{
    editKit([&](Kit& kt) {
        for (int v = 0; v < kVoices; ++v)
            if (only < 0 || only == v) kt.lane[(size_t) v].clearSteps();
    });
}

void OtlakProcessor::audition(int v, const Params& p, float vel)
{
    int s1, n1, s2, n2;
    uiFifo.prepareToWrite(1, s1, n1, s2, n2);
    if (n1 > 0) uiHits[(size_t) s1] = {v, p, vel, false};
    else if (n2 > 0) uiHits[(size_t) s2] = {v, p, vel, false};
    uiFifo.finishedWrite(n1 + n2 > 0 ? 1 : 0);
}

void OtlakProcessor::liveHit(int v, float vel)
{
    int s1, n1, s2, n2;
    uiFifo.prepareToWrite(1, s1, n1, s2, n2);
    if (n1 > 0) uiHits[(size_t) s1] = {v, {}, vel, true};
    else if (n2 > 0) uiHits[(size_t) s2] = {v, {}, vel, true};
    uiFifo.finishedWrite(n1 + n2 > 0 ? 1 : 0);
}

void OtlakProcessor::auditionStep(int v, int step)
{
    const Kit k = copyKit();
    const auto& L = k.lane[(size_t) v];
    Params p = baseParams(v);
    if (step >= 0 && step < kSteps && L.step[(size_t) step] == 'o') p = mergeLock(p, L.lock[(size_t) step]);
    audition(v, p, 1.f);
}

void OtlakProcessor::recLock(int v, int k, float x)
{
    const int j = playStep[v].load();
    if (j < 0) return;
    const Params base = baseParams(v);
    editKit([&](Kit& kt) {
        auto& L = kt.lane[(size_t) v];
        if (j >= L.len) return;
        if (L.step[(size_t) j] == 'x') { L.step[(size_t) j] = 'o'; L.lock[(size_t) j].clear(); }
        if (L.step[(size_t) j] == 'o') {
            L.lock[(size_t) j].has[(size_t) k] = true;
            L.lock[(size_t) j].v[(size_t) k] = x;
        }
        juce::ignoreUnused(base);
    });
}

void OtlakProcessor::setSkin(const juce::String& s)
{
    skin = s;
    settings->setValue("skin", s);
    settings->saveIfNeeded();
}

// ---------------------------------------------------------------- durum
static juce::ValueTree kitToTree(const Kit& k)
{
    juce::ValueTree t("KIT");
    t.setProperty("swing", k.swing, nullptr);
    t.setProperty("human", k.human, nullptr);
    t.setProperty("resync", k.resync, nullptr);
    t.setProperty("seqOn", k.seqOn, nullptr);
    for (int v = 0; v < kVoices; ++v) {
        const auto& L = k.lane[(size_t) v];
        const auto& X = k.voice[(size_t) v];
        juce::ValueTree c("LANE");
        c.setProperty("steps", juce::String(L.step.data(), (size_t) kSteps), nullptr);
        c.setProperty("len", L.len, nullptr);
        c.setProperty("mult", L.mult, nullptr);
        c.setProperty("shOn", L.shOn, nullptr);
        c.setProperty("shEvery", L.shEvery, nullptr);
        c.setProperty("shAmt", L.shAmt, nullptr);
        c.setProperty("shQuant", L.shQuant, nullptr);
        c.setProperty("shSlew", L.shSlew, nullptr);
        c.setProperty("mute", L.mute, nullptr);
        juce::StringArray micro, locks;
        for (int j = 0; j < kSteps; ++j) {
            micro.add(juce::String(L.micro[(size_t) j], 3));
            juce::String s;
            for (int p = 0; p < kParams; ++p)
                if (L.lock[(size_t) j].has[(size_t) p]) s << p << "=" << juce::String(L.lock[(size_t) j].v[(size_t) p], 4) << ";";
            locks.add(s);
        }
        c.setProperty("micro", micro.joinIntoString(","), nullptr);
        c.setProperty("locks", locks.joinIntoString("|"), nullptr);
        c.setProperty("r3", X.r3, nullptr);
        c.setProperty("r4", X.r4, nullptr);
        int mask = 0;
        for (int p = 0; p < kParams; ++p) if (X.locks[(size_t) p]) mask |= 1 << p;
        c.setProperty("plocks", mask, nullptr);
        c.setProperty("algoLock", X.algoLock, nullptr);
        t.appendChild(c, nullptr);
    }
    return t;
}

static void treeToKit(const juce::ValueTree& t, Kit& k)
{
    k.swing = juce::jlimit(50, 75, (int) t.getProperty("swing", k.swing));
    k.human = juce::jlimit(0, 50, (int) t.getProperty("human", k.human));
    k.resync = juce::jlimit(0, kResync - 1, (int) t.getProperty("resync", k.resync));
    k.seqOn = (bool) t.getProperty("seqOn", true);
    for (int v = 0; v < kVoices && v < t.getNumChildren(); ++v) {
        const auto c = t.getChild(v);
        auto& L = k.lane[(size_t) v];
        auto& X = k.voice[(size_t) v];
        L.clearSteps();
        const juce::String steps = c.getProperty("steps").toString();
        for (int j = 0; j < kSteps && j < steps.length(); ++j) {
            const auto ch = (char) steps[j];
            L.step[(size_t) j] = (ch == 'x' || ch == 'o') ? ch : '.';
        }
        L.setLength((int) c.getProperty("len", 16));
        L.mult = juce::jlimit(0, kMults - 1, (int) c.getProperty("mult", kMultOne));
        L.shOn = c.getProperty("shOn", false);
        L.shEvery = juce::jlimit(0, kEvery - 1, (int) c.getProperty("shEvery", 2));
        L.shAmt = juce::jlimit(0, 100, (int) c.getProperty("shAmt", 30));
        L.shQuant = c.getProperty("shQuant", true);
        L.shSlew = juce::jlimit(0, 100, (int) c.getProperty("shSlew", 0));
        L.mute = c.getProperty("mute", false);
        auto micro = juce::StringArray::fromTokens(c.getProperty("micro").toString(), ",", "");
        for (int j = 0; j < kSteps && j < micro.size(); ++j) L.micro[(size_t) j] = juce::jlimit(-0.45f, 0.45f, micro[j].getFloatValue());
        auto locks = juce::StringArray::fromTokens(c.getProperty("locks").toString(), "|", "");
        for (int j = 0; j < kSteps && j < locks.size(); ++j) {
            auto parts = juce::StringArray::fromTokens(locks[j], ";", "");
            for (auto& part : parts) {
                if (!part.contains("=")) continue;
                const int p = part.upToFirstOccurrenceOf("=", false, false).getIntValue();
                if (p < 0 || p >= kParams) continue;
                L.lock[(size_t) j].has[(size_t) p] = true;
                L.lock[(size_t) j].v[(size_t) p] = juce::jlimit(0.f, 1.f, part.fromFirstOccurrenceOf("=", false, false).getFloatValue());
            }
        }
        X.r3 = (float) c.getProperty("r3", X.r3);
        X.r4 = (float) c.getProperty("r4", X.r4);
        const int mask = c.getProperty("plocks", 0);
        for (int p = 0; p < kParams; ++p) X.locks[(size_t) p] = (mask >> p) & 1;
        X.algoLock = c.getProperty("algoLock", false);
    }
}

void OtlakProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto st = params.copyState();
    if (auto old = st.getChildWithName("KIT"); old.isValid()) st.removeChild(old, nullptr);
    st.appendChild(kitToTree(copyKit()), nullptr);
    if (auto xml = st.createXml()) copyXmlToBinary(*xml, destData);
}

void OtlakProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (!xml) return;
    auto st = juce::ValueTree::fromXml(*xml);
    if (!st.hasType(params.state.getType())) return;
    auto kt = st.getChildWithName("KIT");
    if (kt.isValid()) {
        Kit k;
        k.setDefaults();
        treeToKit(kt, k);
        editKit([&](Kit& dst) { dst = k; });
        st.removeChild(kt, nullptr);
    }
    params.replaceState(st);
}

juce::AudioProcessorEditor* OtlakProcessor::createEditor() { return new OtlakEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new OtlakProcessor(); }
