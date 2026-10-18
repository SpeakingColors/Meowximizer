#pragma once
#include "common/FxBase.h"
#include "common/Viz.h"

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
    std::vector<catfx::PanelSpec> panels() const override;
    void paintPanel(int index, juce::Graphics&, juce::Rectangle<float>) override;
    std::vector<Preset> presets() const override;
    juce::StringArray randomExclude() const override { return { "out", "ceil", "mix" }; }
private:
    static constexpr int kRing = 1024;
    struct Squeezer
    {
        float env = 0.f, grAuto = 0.f;
        // returns linear gain; mode 0 gentle, 1 OTT-like, 2 Dance Master style (soft knee, auto release)
        float gain(float peak, float depth, int mode, float aA, float aR, float& gainDbOut, float fs = 44100.f, bool highBand = false)
        {
            if (mode == 2)
            {
                const float att = std::exp(-1.f / (0.002f * fs));
                const float rel = std::exp(-1.f / ((0.04f + 0.012f * grAuto) * fs));   // auto release: deeper reduction releases slower
                const float a = peak > env ? att : rel; env = a * env + (1.f - a) * peak;
                const float db = juce::Decibels::gainToDecibels(env, -100.f);
                const float thr = highBand ? -22.f : -18.f, knee = 6.f, ratio = 3.5f, over = db - thr;
                float red = 0.f;
                if (over > knee * 0.5f) red = over * (1.f - 1.f / ratio);
                else if (over > -knee * 0.5f) { const float t = over + knee * 0.5f; red = (1.f - 1.f / ratio) * t * t / (2.f * knee); }
                grAuto += 0.001f * (red - grAuto);
                gainDbOut = -juce::jlimit(0.f, 24.f, red * (0.35f + 0.65f * depth) * (depth > 0.f ? 1.f : 0.f));
                return juce::Decibels::decibelsToGain(gainDbOut);
            }
            const float a = peak > env ? aA : aR; env = a * env + (1.f - a) * peak;
            const float db = juce::Decibels::gainToDecibels(env, -100.f);
            const float thrHi = mode ? -22.f : -14.f, thrLo = mode ? -48.f : -52.f;
            const float down = db > thrHi ? -(db - thrHi) * (1.f - 1.f / (mode ? 5.f : 2.5f)) : 0.f;
            const float up = db < thrLo ? 0.f : (db < thrHi ? (thrHi - db) * (mode ? 0.45f : 0.2f) : 0.f); // upward: lift quieter parts above the gate
            gainDbOut = juce::jlimit(-24.f, 18.f, (down + up) * depth);
            juce::ignoreUnused(fs, highBand);
            return juce::Decibels::decibelsToGain(gainDbOut);
        }
    };
    // LA-2A style electro-optical leveller: ~10 ms attack, two-stage release (half in ~60 ms, rest in seconds, slower after long heavy compression)
    struct Opto
    {
        float gr1 = 0.f, gr2 = 0.f, mem = 0.f, env = 0.f;
        float gain(float peak, float amount, float fs, float& grDbOut)
        {
            const float p = juce::jlimit(0.f, 1.f, amount);
            const float k = 1.f - std::exp(-1.f / (0.0015f * fs));
            env += k * (peak - env);                                  // fast detector
            const float db = juce::Decibels::gainToDecibels(env, -100.f);
            const float thr = -6.f - 30.f * p, knee = 12.f, over = db - thr;
            float target = 0.f;
            if (over > knee * 0.5f) target = over * (1.f - 1.f / 3.f);
            else if (over > -knee * 0.5f) { const float t = over + knee * 0.5f; target = (2.f / 3.f) * t * t / (2.f * knee); }
            const float att = 1.f - std::exp(-1.f / (0.010f * fs));
            const float rel1 = 1.f - std::exp(-1.f / (0.0866f * fs));          // half-life about 60 ms
            const float tau2 = 1.f + 4.f * mem;                                  // seconds, grows with programme memory
            const float rel2 = 1.f - std::exp(-1.f / (tau2 * fs));
            gr1 += (target > gr1 ? att : rel1) * (target - gr1);
            gr2 += (target > gr2 ? att : rel2) * (target - gr2);
            mem += (1.f - std::exp(-1.f / (2.f * fs))) * (juce::jlimit(0.f, 1.f, 0.5f * (gr1 + gr2) / 15.f) - mem);
            grDbOut = 0.5f * (gr1 + gr2);
            return juce::Decibels::decibelsToGain(-grDbOut + 6.f * p);       // 6 dB make-up at full amount
        }
    };
    juce::dsp::LinkwitzRileyFilter<float> s1[2], s2[2], ap[2];
    juce::dsp::IIR::Filter<float> shelf[2], bassHp, bassLp, airHp1[2], airHp2[2];
    juce::dsp::Limiter<float> limiter;
    Squeezer sq[3];
    Opto opto;
    juce::dsp::IIR::Filter<float> subHp[2];
    std::atomic<float> optoGr { 0.f };
    std::vector<float> ringLo[2], ringMid[2], dryBuf[2];
    int ringPos = 0;
    float bassEnv = 0.f, bassDc = 0.f, midEnvDb = -60.f, liftDb = 0.f;
    std::atomic<float> bandLv[3], bandGain[3], dLo { 0.f }, dMid { 0.f }, lift { 0.f };
};
