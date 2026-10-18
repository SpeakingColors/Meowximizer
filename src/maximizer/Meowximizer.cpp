#include "Meowximizer.h"

using namespace catfx;

Meowximizer::Meowximizer()
    : FxBase("Meowximizer",
        { { "contour", "Lo contour", "", 0.f, 10.f, 3.f, 0.f, "Low contour: a gentle low shelf for weight and warmth in the bottom end." },
          { "bass", "Bass harmonics", " %", 0.f, 100.f, 25.f, 0.f, "Bass harmonics: adds overtones to the bass so it stays audible on small speakers." },
          { "process", "Process", "", 0.f, 10.f, 5.f, 0.f, "Process: the sonic-maximizer effect. Delays the lows and mids a tiny bit behind the highs and lifts the highs when the mids are quiet, for clarity and punch." },
          { "air", "Air", " %", 0.f, 100.f, 25.f, 0.f, "Air: adds sparkle above about 5 kHz with a gentle exciter." },
          { "squeeze", "Squeeze", " %", 0.f, 100.f, 30.f, 0.f, "Squeeze: how much each of the three bands is evened out, pressing loud parts down and lifting quiet ones up. More = denser." },
          { "warm", "Warm", " %", 0.f, 100.f, 20.f, 0.f, "Warm: light saturation on the whole signal. Rounds peaks and adds body." },
          { "push", "Push", " dB", 0.f, 18.f, 0.f, 0.f, "Push: raises the level going into the squeeze and the limiter for more loudness." },
          { "ceil", "Ceiling", " dB", -12.f, 0.f, -0.3f, 0.f, "Ceiling: the output limiter never lets the signal go above this level." },
          { "mix", "Mix", " %", 0.f, 100.f, 100.f, 0.f, "Mix: dry/wet blend for parallel processing." },
          { "out", "Output", " dB", -12.f, 12.f, 0.f, 0.f, "Output: final level. Match it with the bypassed level when comparing." } },
        { { "mode", "Squeeze mode", { "Gentle", "OTT style" }, 0, "Squeeze mode: Gentle is a mild squeeze that keeps dynamics natural, OTT style presses the loud down and lifts the quiet up hard for a big, dense sound." } })
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

std::vector<catfx::PanelSpec> Meowximizer::panels() const
{
    return { { "Tone", { "contour", "bass", "process", "air" }, {}, 3.2f },
             { "Squeeze", { "squeeze", "push" }, { "mode" }, 1.9f },
             { "Saturation and ceiling", { "warm", "ceil" }, {}, 1.8f },
             { "Level", { "mix", "out" }, {}, 1.7f } };
}

std::vector<FxBase::Preset> Meowximizer::presets() const
{
    return {
        { "Init", {} },
        { "Loud Master", { { "push", 6.f }, { "squeeze", 40.f }, { "air", 30.f }, { "warm", 25.f }, { "ceil", -0.5f } } },
        { "Warm Glue", { { "contour", 4.f }, { "squeeze", 25.f }, { "warm", 45.f }, { "air", 15.f } } },
        { "OTT Hardcore", { { "mode", 1.f }, { "squeeze", 65.f }, { "push", 4.f }, { "air", 40.f }, { "bass", 40.f }, { "mix", 70.f } } },
        { "Vocal Air", { { "process", 6.f }, { "air", 55.f }, { "contour", 1.f }, { "squeeze", 20.f } } },
        { "Bass Boost", { { "contour", 6.f }, { "bass", 55.f }, { "squeeze", 25.f } } },
        { "Drum Smack", { { "process", 7.f }, { "squeeze", 50.f }, { "warm", 35.f }, { "air", 20.f } } } };
}

void Meowximizer::paintPanel(int idx, juce::Graphics& g, juce::Rectangle<float> r)
{
    using namespace catfx::viz;
    if (idx == 0)       // approximate tone curve of the enhancer (dB against Hz)
    {
        const float contour = get("contour"), bassAmt = get("bass") / 100.f, airAmt = get("air") / 100.f;
        auto p = r.withTrimmedLeft(34.f).withTrimmedRight(8.f).withTrimmedTop(26.f).withTrimmedBottom(14.f);
        g.setColour(bg().brighter(0.04f)); g.fillRect(p);
        dbAxis(g, p, -6.f, 12.f, 6.f); freqAxis(g, p);
        const float lift = this->lift.load();
        juce::Path c;
        for (int i = 0; i <= 240; ++i)
        {
            const float hz = 20.f * std::pow(1000.f, (float) i / 240.f);
            const float shelfDb = contour * 1.1f / std::sqrt(1.f + std::pow(hz / 70.f, 4.f));
            const float bumpDb = 6.f * bassAmt * std::exp(-std::pow(std::log(hz / 250.f), 2.f) / 0.5f);
            const float liftDb = lift / std::sqrt(1.f + std::pow(2500.f / hz, 4.f));
            const float airDb = 4.f * airAmt / std::sqrt(1.f + std::pow(7000.f / hz, 4.f));
            const float db = juce::jlimit(-6.f, 12.f, shelfDb + bumpDb + liftDb + airDb);
            const float x = p.getX() + p.getWidth() * (float) i / 240.f, y = p.getBottom() - (db + 6.f) / 18.f * p.getHeight();
            i ? c.lineTo(x, y) : c.startNewSubPath(x, y);
        }
        g.setColour(green()); g.strokePath(c, juce::PathStrokeType(2.5f));
        g.setColour(green()); g.setFont(juce::FontOptions(11.f));
        g.drawText("approximate tone curve", (int) r.getX() + 4, (int) r.getY() + 1, (int) r.getWidth(), 12, juce::Justification::left);
        g.drawText("lift " + juce::String(lift, 1) + " dB  delays " + juce::String(dLo.load(), 1) + " / " + juce::String(dMid.load(), 1) + " ms",
                   p.toNearestInt().withTrimmedTop(2).withHeight(12).withTrimmedRight(4), juce::Justification::right);
    }
    else if (idx == 1)  // three bands: level before (grey) and after the squeeze (colour), in dB
    {
        static const char* nm[] = { "Low", "Mid", "High" };
        const juce::Colour col[] = { blue(), green(), red() };
        auto p = r.withTrimmedLeft(34.f).withTrimmedRight(8.f).withTrimmedTop(26.f).withTrimmedBottom(28.f);
        g.setColour(bg().brighter(0.04f)); g.fillRect(p);
        dbAxis(g, p, -60.f, 0.f, 12.f);
        const float w = p.getWidth() / 3.f;
        for (int b = 0; b < 3; ++b)
        {
            auto c = juce::Rectangle<float>(p.getX() + w * (float) b + 8.f, p.getY(), w - 16.f, p.getHeight());
            const float lv = bandLv[b].load(), gd = bandGain[b].load();
            const float h1 = juce::jlimit(0.f, 1.f, (lv + 60.f) / 60.f) * c.getHeight(), h2 = juce::jlimit(0.f, 1.f, (lv + gd + 60.f) / 60.f) * c.getHeight();
            g.setColour(juce::Colour(0xff2a3335)); g.fillRect(c.getX(), c.getBottom() - h1, c.getWidth(), h1);
            g.setColour(col[b].withAlpha(0.85f)); g.fillRect(c.getX() + c.getWidth() * 0.25f, c.getBottom() - h2, c.getWidth() * 0.5f, h2);
            g.setColour(juce::Colours::white); g.setFont(juce::FontOptions(11.f));
            const float shown = b == 2 ? gd + lift.load() : gd;
            g.drawText(juce::String(nm[b]) + " " + juce::String(shown > 0.f ? "+" : "") + juce::String(shown, 1), c.withY(p.getBottom() + 2.f).withHeight(13.f).toNearestInt(), juce::Justification::centred);
        }
        g.setColour(dim()); g.setFont(juce::FontOptions(11.f));
        g.drawText("bands: before (grey), after", (int) r.getX() + 4, (int) r.getY() + 1, (int) r.getWidth(), 12, juce::Justification::left);
    }
    else if (idx == 2) // saturation curve and the limiter ceiling
    {
        const float warm = get("warm") / 100.f, d = 1.f + warm * 1.5f, ceil = get("ceil");
        const float ceilLin = juce::Decibels::decibelsToGain(ceil);
        auto p = r.withTrimmedLeft(8.f).withTrimmedRight(8.f).withTrimmedTop(26.f).withTrimmedBottom(14.f);
        g.setColour(bg().brighter(0.04f)); g.fillRect(p);
        g.setColour(grid()); g.drawHorizontalLine((int) p.getCentreY(), p.getX(), p.getRight()); g.drawVerticalLine((int) p.getCentreX(), p.getY(), p.getBottom());
        auto Y = [&](float v) { return p.getCentreY() - juce::jlimit(-1.2f, 1.2f, v) / 1.2f * p.getHeight() * 0.5f; };
        auto X = [&](float v) { return p.getCentreX() + v / 1.0f * p.getWidth() * 0.5f; };
        g.setColour(red().withAlpha(0.7f)); g.drawHorizontalLine((int) Y(ceilLin), p.getX(), p.getRight()); g.drawHorizontalLine((int) Y(-ceilLin), p.getX(), p.getRight());
        juce::Path c;
        for (int i = 0; i <= 200; ++i)
        {
            const float x = -1.f + (float) i / 100.f, y = juce::jlimit(-ceilLin, ceilLin, (1.f - warm) * x + warm * std::tanh(d * x) / d);
            i ? c.lineTo(X(x), Y(y)) : c.startNewSubPath(X(x), Y(y));
        }
        g.setColour(accent()); g.strokePath(c, juce::PathStrokeType(2.5f));
        const float lv = juce::jlimit(0.f, 1.f, inLevel.load()), yy = juce::jlimit(-ceilLin, ceilLin, (1.f - warm) * lv + warm * std::tanh(d * lv) / d);
        g.setColour(juce::Colours::white); g.fillEllipse(X(lv) - 3.f, Y(yy) - 3.f, 6.f, 6.f);
        g.setColour(accent()); g.setFont(juce::FontOptions(11.f));
        g.drawText("saturation and limiter ceiling", (int) r.getX() + 4, (int) r.getY() + 1, (int) r.getWidth(), 12, juce::Justification::left);
        g.setColour(red()); g.drawText("ceiling " + juce::String(ceil, 1) + " dB", p.toNearestInt().withTrimmedTop(2).withHeight(12).withTrimmedRight(4), juce::Justification::right);
        g.setColour(dim()); g.drawText("input level", p.toNearestInt().withY((int) p.getBottom() + 1).withHeight(12), juce::Justification::centred);
    }
    else                // level: IN and OUT meters
    {
        auto area = r.reduced(6.f, 6.f);
        auto left = area.removeFromLeft(area.getWidth() * 0.5f);
        meter(g, left, inLevel.load(), "IN dB");
        meter(g, area, outLevel.load(), "OUT dB");
    }
}
