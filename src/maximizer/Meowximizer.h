#pragma once
#include "common/FxBase.h"

// Meowximizer: sonic enhancer, bass enhancer and exciter in one.
// 3 phase-aligned bands. Low: contour shelf, bass harmonics, time offset. Mid: time offset.
// High: dynamic lift, air exciter. Every band can be squeezed (gentle or OTT-style up and down compression).
// Then light saturation, output limiter, dry/wet mix. The time offset idea (lows and mids slightly behind the highs)
// follows the general public description of sonic maximizers; all numbers here are our own design choices.
class Meowximizer : public catfx::FxBase
{
public:
    Meowximizer();
    void prepare(double sr, int maxBlock) override;
    void process(juce::AudioBuffer<float>&, const juce::AudioBuffer<float>*) override;
    void paintViz(juce::Graphics&, juce::Rectangle<float>) override;
    juce::String vizCaption() const override { return "Bands: level (grey), after squeeze (colour); gain shown in dB; delay in ms"; }
private:
    static constexpr int kRing = 1024;
    struct Squeezer
    {
        float env = 0.f;
        // returns linear gain; mode 0 gentle, 1 OTT-like
        float gain(float peak, float depth, int mode, float aA, float aR, float& gainDbOut)
        {
            const float a = peak > env ? aA : aR; env = a * env + (1.f - a) * peak;
            const float db = juce::Decibels::gainToDecibels(env, -100.f);
            const float thrHi = mode ? -22.f : -14.f, thrLo = mode ? -48.f : -52.f;
            const float down = db > thrHi ? -(db - thrHi) * (1.f - 1.f / (mode ? 5.f : 2.5f)) : 0.f;
            const float up = db < thrLo ? 0.f : (db < thrHi ? (thrHi - db) * (mode ? 0.45f : 0.2f) : 0.f); // upward: lift quieter parts above the gate
            gainDbOut = juce::jlimit(-24.f, 18.f, (down + up) * depth);
            return juce::Decibels::decibelsToGain(gainDbOut);
        }
    };
    juce::dsp::LinkwitzRileyFilter<float> s1[2], s2[2], ap[2];
    juce::dsp::IIR::Filter<float> shelf[2], bassHp, bassLp, airHp1[2], airHp2[2];
    juce::dsp::Limiter<float> limiter;
    Squeezer sq[3];
    std::vector<float> ringLo[2], ringMid[2], dryBuf[2];
    int ringPos = 0;
    float bassEnv = 0.f, bassDc = 0.f, midEnvDb = -60.f, liftDb = 0.f;
    std::atomic<float> bandLv[3], bandGain[3], dLo { 0.f }, dMid { 0.f }, lift { 0.f };
};
