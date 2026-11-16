// Geliştirme aracı: arayüzü ekransız çizip PNG'ye kaydeder (cmake -DOT_BUILD_SNAPSHOT=ON)
#include "PluginEditor.h"
#include "PluginProcessor.h"

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::String outDir = argc > 1 ? argv[1] : "/tmp";
    OtlakProcessor proc;
    proc.prepareToPlay(48000, 512);
    // birkaç blok çal: imleçler ve NOW değerleri dolsun
    proc.freeRun = true;
    juce::AudioBuffer<float> buf(2, 512);
    juce::MidiBuffer midi;
    for (int i = 0; i < 40; ++i) proc.processBlock(buf, midi);
    const char* skins[] = {"kar", "fjord"};
    for (auto* s : skins) {
        proc.setSkin(s);
        std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
        if (auto* oe = dynamic_cast<OtlakEditor*>(ed.get())) oe->refreshNow();
        auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
        juce::File f(outDir + "/otlak_" + s + ".png");
        f.deleteFile();
        juce::FileOutputStream os(f);
        juce::PNGImageFormat().writeImageToStream(img, os);
        std::printf("%s\n", f.getFullPathName().toRawUTF8());
    }
    proc.setSkin("kar");
    // ses çıkıyor mu
    float peak = 0;
    for (int i = 0; i < 200; ++i) { proc.processBlock(buf, midi); peak = juce::jmax(peak, buf.getMagnitude(0, 512)); }
    std::printf("peak %.3f\n", peak);
    return 0;
}
