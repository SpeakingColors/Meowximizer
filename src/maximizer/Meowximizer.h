#pragma once
#include "common/FxBase.h"
#include "common/Viz.h"
#include "maximizer/MxCore.h"

// Meowximizer: sonic enhancer, bass enhancer and exciter in one.
// 3 phase-aligned bands. Low: contour shelf, bass harmonics, time offset. Mid: time offset.
// High: dynamic lift, air exciter. Every band can be squeezed (gentle or OTT-style up and down compression).
// Air exciter, warm stage and clipper are 2x oversampled; the output limiter is a lookahead limiter, so the Ceiling always holds.
// Then light saturation, clipper, output limiter, dry/wet mix. The plugin reports its exact latency to the host. The time offset idea (lows and mids slightly behind the highs)
// follows the general public description of sonic maximizers; all numbers here are our own design choices.
class Meowximizer : public catfx::FxBase
{
public:
    Meowximizer();
    void prepare(double sr, int maxBlock) override;
    void process(juce::AudioBuffer<float>&, const juce::AudioBuffer<float>*) override;
    std::vector<catfx::PanelSpec> panels() const override;
    void paintPanel(int index, juce::Graphics&, juce::Rectangle<float>) override;
    std::vector<Preset> presets() const override;
    std::vector<std::pair<juce::String, float>> choiceDefaults(const juce::String& id, int item) const override;
    juce::StringArray randomExclude() const override { return { "out", "ceil", "mix" }; }
private:
    static constexpr int kNumStyles = mx::kNumStyles;
    mx::Core core;   // all audio-thread state lives here, allocated in prepare(); 20 ms ramps on every level-type knob
    std::atomic<float> optoGr { 0.f };
    std::atomic<float> bandLv[3], bandGain[3], dLo { 0.f }, dMid { 0.f }, lift { 0.f };
};
