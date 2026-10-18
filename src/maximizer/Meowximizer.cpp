#include "Meowximizer.h"

using namespace catfx;

Meowximizer::Meowximizer()
    : FxBase("Meowximizer",
        { { "contour", "Lo contour", "", 0.f, 10.f, 3.f },
          { "bass", "Bass harmonics", " %", 0.f, 100.f, 25.f },
          { "process", "Process", "", 0.f, 10.f, 5.f },
          { "air", "Air", " %", 0.f, 100.f, 25.f },
          { "squeeze", "Squeeze", " %", 0.f, 100.f, 30.f },
          { "warm", "Warm", " %", 0.f, 100.f, 20.f },
          { "push", "Push", " dB", 0.f, 18.f, 0.f },
          { "ceil", "Ceiling", " dB", -12.f, 0.f, -0.3f },
          { "mix", "Mix", " %", 0.f, 100.f, 100.f },
          { "out", "Output", " dB", -12.f, 12.f, 0.f } },
        { { "mode", "Squeeze mode", { "Gentle", "OTT style" }, 0 } })
{
    for (int i = 0; i < 3; ++i) { bandLv[i] = -100.f; bandGain[i] = 0.f; }
}

void Meowximizer::prepare(double sr, int bs)
{
    juce::dsp::ProcessSpec spec { sr, (juce::uint32) bs, 1 }, spec2 { sr, (juce::uint32) bs, 2 };
    for (int c = 0; c < 2; ++c)
    {
        for (auto* f : { &s1[c], &s2[c], &ap[c] }) { f->prepare(spec); f->reset(); }
        shelf[c].reset(); airHp1[c].reset(); airHp2[c].reset();
        airHp1[c].coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, 5000.0, 0.7071f);
        airHp2[c].coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, 7000.0, 0.7071f);
        ringLo[c].assign((size_t) kRing, 0.f); ringMid[c].assign((size_t) kRing, 0.f);
    }
    bassHp.reset(); bassLp.reset();
    bassHp.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, 150.0, 0.7071f);
    bassLp.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, 500.0, 0.7071f);
    limiter.prepare(spec2); limiter.reset();
    for (auto& s : sq) s.env = 0.f;
    for (auto& d : dryBuf) d.assign((size_t) bs, 0.f);
    ringPos = 0; bassEnv = bassDc = 0.f; midEnvDb = -60.f; liftDb = 0.f;
}

void Meowximizer::process(juce::AudioBuffer<float>& buf, const juce::AudioBuffer<float>*)
{
    const int n = buf.getNumSamples(), nch = juce::jmin(2, buf.getNumChannels());
    const float contour = get("contour"), bassAmt = get("bass") / 100.f, proc = get("process") / 10.f, airAmt = get("air") / 100.f;
    const float depth = get("squeeze") / 100.f, warm = get("warm") / 100.f, mix = get("mix") / 100.f;
    const float push = juce::Decibels::decibelsToGain(get("push")), outG = juce::Decibels::decibelsToGain(get("out"));
    const int mode = (int) get("mode");
    const float x1 = 150.f, x2 = 2500.f;
    const int dLoS = juce::jmin(kRing - 2, (int) (proc * 0.0025f * (float) fs)), dMidS = juce::jmin(kRing - 2, (int) (proc * 0.0010f * (float) fs));
    dLo = 1000.f * (float) dLoS / (float) fs; dMid = 1000.f * (float) dMidS / (float) fs;
    for (int c = 0; c < nch; ++c)
    {
        s1[c].setCutoffFrequency(x1); s2[c].setCutoffFrequency(x2); ap[c].setCutoffFrequency(x2);
        shelf[c].coefficients = juce::dsp::IIR::Coefficients<float>::makeLowShelf(fs, 70.0, 0.7071f, juce::Decibels::decibelsToGain(contour * 1.1f));
    }
    const float aA = std::exp(-1.f / (0.004f * (float) fs)), aR = std::exp(-1.f / (0.08f * (float) fs));
    const float aMid = std::exp(-1.f / (0.05f * (float) fs));
    const float d = 1.f + warm * 1.5f;
    float pkLv[3] {}, gSum[3] {};
    for (int i = 0; i < n; ++i)
    {
        float lo[2] {}, mid[2] {}, hi[2] {}, dry[2] {};
        for (int c = 0; c < nch; ++c)
        {
            dry[c] = buf.getReadPointer(c)[i]; dryBuf[c][(size_t) i] = dry[c];
            const float x = dry[c] * push;
            float l1, r1, a, b, m, h;
            s1[c].processSample(0, x, l1, r1);
            ap[c].processSample(0, l1, a, b); lo[c] = a + b;       // keep the low band phase-aligned with the 2.5 kHz split
            s2[c].processSample(0, r1, m, h);
            mid[c] = m; hi[c] = h;
        }
        // linked detectors
        float pk[3] {};
        for (int c = 0; c < nch; ++c) { pk[0] = std::max(pk[0], std::abs(lo[c])); pk[1] = std::max(pk[1], std::abs(mid[c])); pk[2] = std::max(pk[2], std::abs(hi[c])); }
        float gDb[3];
        const float g0 = sq[0].gain(pk[0], depth, mode, aA, aR, gDb[0]);
        const float g1 = sq[1].gain(pk[1], depth, mode, aA, aR, gDb[1]);
        const float g2 = sq[2].gain(pk[2], depth, mode, aA, aR, gDb[2]);
        for (int k = 0; k < 3; ++k) { pkLv[k] = std::max(pkLv[k], pk[k]); gSum[k] += gDb[k]; }
        // bass harmonics from the mono low band
        const float mono = nch > 1 ? 0.5f * (lo[0] + lo[1]) : lo[0];
        bassEnv = (std::abs(mono) > bassEnv ? aA : aR) * bassEnv + (1.f - (std::abs(mono) > bassEnv ? aA : aR)) * std::abs(mono);
        float ev = std::abs(mono); bassDc += 0.002f * (ev - bassDc); ev -= bassDc;
        float bh = (ev * 2.f * (1.f - 0.3f) + std::tanh(4.f * mono) * 0.5f * 0.3f) * juce::jlimit(0.f, 6.f, std::pow(std::max(bassEnv, 0.003f) / 0.25f, -0.5f)) * 0.25f;
        bh = bassLp.processSample(bassHp.processSample(bh)) * bassAmt * 3.f;
        // high band: dynamic lift by the mid level
        const float midDb = juce::Decibels::gainToDecibels(pk[1], -100.f);
        midEnvDb = aMid * midEnvDb + (1.f - aMid) * midDb;
        const float tgt = proc * juce::jmap(juce::jlimit(0.f, 1.f, (midEnvDb + 50.f) / 35.f), 7.f, -2.f);
        liftDb = aMid * liftDb + (1.f - aMid) * tgt;
        const float liftG = juce::Decibels::decibelsToGain(liftDb);
        for (int c = 0; c < nch; ++c)
        {
            float l = shelf[c].processSample(lo[c]) * g0 + bh;
            float m = mid[c] * g1;
            // time offsets: lows and mids arrive slightly after the highs
            auto& rl = ringLo[c]; auto& rm = ringMid[c];
            rl[(size_t) ringPos] = l; rm[(size_t) ringPos] = m;
            l = rl[(size_t) ((ringPos - dLoS + kRing) % kRing)]; m = rm[(size_t) ((ringPos - dMidS + kRing) % kRing)];
            float h = hi[c] * g2 * liftG;
            const float ex = airHp2[c].processSample(std::tanh(5.f * airHp1[c].processSample(hi[c])));
            h += ex * airAmt * 0.5f;
            float y = l + m + h;
            y = y * (1.f - warm) + warm * std::tanh(d * y) / d;
            buf.getWritePointer(c)[i] = y;
            if (c == 0 && i == 0) lift = liftDb;
        }
        ringPos = (ringPos + 1) % kRing;
    }
    limiter.setThreshold(get("ceil")); limiter.setRelease(80.f);
    juce::dsp::AudioBlock<float> blk(buf);
    limiter.process(juce::dsp::ProcessContextReplacing<float>(blk));
    for (int c = 0; c < nch; ++c)
    {
        auto* w = buf.getWritePointer(c);
        for (int i = 0; i < n; ++i) w[i] = (w[i] * mix + dryBuf[c][(size_t) i] * (1.f - mix)) * outG; // the chain has no latency, so dry lines up
    }
    for (int k = 0; k < 3; ++k) { bandLv[k] = juce::Decibels::gainToDecibels(pkLv[k], -100.f); bandGain[k] = gSum[k] / (float) std::max(1, n); }
}

void Meowximizer::paintViz(juce::Graphics& g, juce::Rectangle<float> r)
{
    r = r.reduced(6.f, 20.f); r.removeFromRight(24.f);
    static const char* nm[] = { "Low", "Mid", "High" };
    static const juce::uint32 col[] = { 0xff5aa8ff, 0xff58e08a, 0xffff7ac8 };
    const float w = r.getWidth() / 3.f;
    for (int b = 0; b < 3; ++b)
    {
        auto c = juce::Rectangle<float>(r.getX() + w * (float) b + 10.f, r.getY(), w - 20.f, r.getHeight() - 22.f);
        const float lv = bandLv[b].load(), gd = bandGain[b].load();
        const float h1 = juce::jlimit(0.f, 1.f, (lv + 70.f) / 70.f) * c.getHeight();
        const float h2 = juce::jlimit(0.f, 1.f, (lv + gd + 70.f) / 70.f) * c.getHeight();
        g.setColour(juce::Colour(0xff2a3335)); g.fillRect(c.getX(), c.getBottom() - h1, c.getWidth(), h1);
        g.setColour(juce::Colour(col[b]).withAlpha(0.75f)); g.fillRect(c.getX() + c.getWidth() * 0.25f, c.getBottom() - h2, c.getWidth() * 0.5f, h2);
        g.setColour(juce::Colours::white); g.setFont(juce::FontOptions(12.f));
        const float shown = b == 2 ? gd + lift.load() : gd;
        g.drawText(juce::String(nm[b]) + "  " + juce::String(shown, 1) + " dB", c.withY(c.getBottom() + 2.f).withHeight(16.f).toNearestInt(), juce::Justification::centred);
    }
    g.setColour(juce::Colour(0xff9a9690));
    g.drawText("Low delay " + juce::String(dLo.load(), 2) + " ms   Mid delay " + juce::String(dMid.load(), 2) + " ms   High lift " + juce::String(lift.load(), 1) + " dB",
               r.toNearestInt().withHeight(14).withY((int) r.getY() - 2), juce::Justification::right);
}
