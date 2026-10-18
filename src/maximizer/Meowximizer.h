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
    std::vector<std::pair<juce::String, float>> choiceDefaults(const juce::String& id, int item) const override;
    juce::StringArray randomExclude() const override { return { "out", "ceil", "mix" }; }
private:
    static constexpr int kRing = 1024;
    // Squeeze styles: each one is a full set of compressor behaviour for the three bands.
    // thr/ratio/knee: downward compression; up/lo: upward lift of quiet parts above the floor "lo";
    // att/rel (ms) with relAuto (extra ms of release per dB of reduction); floor: share of the effect that is always on when Squeeze > 0;
    // off: threshold offset per band (low, mid, high); limit: soft limiter on each band; sub: 22 Hz high-pass before the squeeze.
    // Chain recipe (all scaled by the Squeeze amount): optoMix = LA-2A style leveller added in front, mb = share of the multiband compression,
    // sat = extra saturation on each band, maxi = extra push (dB) into the output limiter. The Opto knob adds more leveller on top.
    struct SqStyle { float thr, ratio, up, lo, knee, att, rel, relAuto, floor; float off[3]; bool limit, sub; float optoMix, mb, sat, maxi; };
    static constexpr int kNumStyles = 11;
    static constexpr SqStyle kStyles[kNumStyles] = {
        { -14.f, 2.5f, 0.20f, -52.f,  0.f,  4.f,  80.f,  0.f, 0.00f, { 0.f, 0.f, 0.f },   false, false, 0.f, 1.f, 0.f, 0.f },   // Gentle
        { -22.f, 5.0f, 0.45f, -48.f,  0.f,  4.f,  80.f,  0.f, 0.00f, { 0.f, 0.f, 0.f },   false, false, 0.f, 1.f, 0.10f, 2.f },   // OTT style
        { -18.f, 3.5f, 0.00f, -60.f,  6.f,  2.f,  40.f, 12.f, 0.35f, { 0.f, 0.f, -4.f },  true,  true, 0.f, 1.f, 0.15f, 1.f },   // Dance Master
        { -16.f, 2.0f, 0.10f, -56.f, 10.f, 10.f, 120.f,  6.f, 0.20f, { 0.f, 0.f, 0.f },   false, false, 0.20f, 0.80f, 0.20f, 0.f },   // Glue
        { -18.f, 2.2f, 0.10f, -56.f, 12.f, 10.f, 250.f, 10.f, 0.25f, { 0.f, 0.f, 0.f },   false, false, 0.50f, 0.50f, 0.15f, 0.f },   // Slow Smooth
        { -14.f, 3.0f, 0.00f, -60.f,  4.f, 20.f,  60.f,  0.f, 0.20f, { 0.f, 0.f, 0.f },   false, false, 0.f, 0.90f, 0.30f, 1.f },   // Punchy (slow attack keeps transients)
        { -20.f, 6.0f, 0.00f, -60.f,  3.f,  1.f,  50.f, 10.f, 0.40f, { 0.f, 0.f, -2.f },  true,  true, 0.f, 1.f, 0.10f, 3.f },   // Tight Lock
        { -24.f, 1.5f, 0.60f, -55.f,  6.f,  5.f, 120.f,  0.f, 0.00f, { 0.f, 0.f, 0.f },   false, false, 0.f, 1.f, 0.f, 0.f },   // Lift (brings quiet parts up)
        { -20.f, 4.0f, 0.10f, -60.f,  4.f,  3.f,  30.f, 15.f, 0.40f, { 2.f, 0.f, -3.f },  true,  true, 0.10f, 1.f, 0.40f, 3.f },   // Hardstyle Lock
        { -16.f, 4.0f, 0.00f, -60.f,  8.f,  3.f,  60.f, 12.f, 0.40f, { 0.f, -2.f, -9.f },  true,  true, 0.25f, 1.f, 0.10f, 0.f },   // Highs Tamer
        { -20.f, 2.0f, 0.00f, -60.f, 12.f, 10.f, 200.f, 20.f, 0.30f, { 0.f, 0.f, -2.f }, false, false, 0.85f, 0.45f, 0.35f, 0.5f } };   // Thick Supersaw (slow, soft, opto-like partner)
    struct Squeezer
    {
        float env = 0.f, grAuto = 0.f;
        float gain(float peak, float depth, const SqStyle& s, int band, float fs, float& gainDbOut)
        {
            const float att = std::exp(-1.f / (s.att * 0.001f * fs));
            const float rel = std::exp(-1.f / ((s.rel + s.relAuto * grAuto) * 0.001f * fs));
            const float a = peak > env ? att : rel; env = a * env + (1.f - a) * peak;
            const float db = juce::Decibels::gainToDecibels(env, -100.f);
            const float thr = s.thr + s.off[band], over = db - thr, slope = 1.f - 1.f / s.ratio;
            float red = 0.f;
            if (s.knee > 0.f && over > -s.knee * 0.5f && over < s.knee * 0.5f) { const float t = over + s.knee * 0.5f; red = slope * t * t / (2.f * s.knee); }
            else if (over >= s.knee * 0.5f && over > 0.f) red = over * slope;
            const float up = (db > s.lo && db < thr) ? (thr - db) * s.up : 0.f;       // lift quiet parts above the floor
            grAuto += 0.001f * (red - grAuto);
            const float scale = depth > 0.f ? s.floor + (1.f - s.floor) * depth : 0.f;
            gainDbOut = juce::jlimit(-24.f, 18.f, (up - red) * scale * s.mb);
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
