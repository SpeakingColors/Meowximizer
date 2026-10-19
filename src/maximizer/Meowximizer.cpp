#include "Meowximizer.h"

using namespace catfx;

Meowximizer::Meowximizer()
    : FxBase("Meowximizer",
        { { "contour", "Lo contour", "", 0.f, 10.f, 3.f, 0.f, "Low contour: a gentle low shelf for weight and warmth in the bottom end." },
          { "bass", "Bass harmonics", " %", 0.f, 100.f, 25.f, 0.f, "Bass harmonics: adds overtones to the bass so it stays audible on small speakers." },
          { "process", "Process", "", 0.f, 10.f, 5.f, 0.f, "Process: the sonic-maximizer effect. Delays the lows and mids a tiny bit behind the highs and lifts the highs when the mids are quiet, for clarity and punch." },
          { "air", "Air", " %", 0.f, 100.f, 25.f, 0.f, "Air: a smooth high shelf at 9 kHz (up to +4.5 dB) for sheen, plus an exciter that adds new harmonics above 5 kHz for brilliance and excitement. The exciter runs 2x oversampled, so it stays smooth." },
          { "squeeze", "Squeeze", " %", 0.f, 100.f, 30.f, 0.f, "Squeeze: how much each of the three bands is evened out, pressing loud parts down and lifting quiet ones up. More = denser." },
          { "warm", "Warm", " %", 0.f, 100.f, 20.f, 0.f, "Warm: saturation on the whole signal. Rounds peaks and adds body. With a Warm character other than tanh it adds disto, transistor or fuzz grit when pushed." },
          { "clip", "Clip", " %", 0.f, 100.f, 0.f, 0.f, "Light peak clipper: pushes the signal up to 9 dB harder into a smooth clipper at the Ceiling, then the limiter. Shaves the peaks so the whole thing can sit much louder." },
          { "push", "Push", " dB", 0.f, 18.f, 0.f, 0.f, "Push: raises the level going into the squeeze and the limiter for more loudness." },
          { "opto", "Opto", " %", 0.f, 100.f, 0.f, 0.f, "Opto: extra LA-2A style leveller on top of whatever the squeeze style already uses. Slow, smooth, program-dependent compression that thickens leads and bass. 0 = only what the style brings." },
          { "ceil", "Ceiling", " dB", -12.f, 0.f, -0.3f, 0.f, "Ceiling: the output limiter never lets the signal go above this level." },
          { "mix", "Mix", " %", 0.f, 100.f, 100.f, 0.f, "Mix: dry/wet blend for parallel processing." },
          { "out", "Output", " dB", -12.f, 12.f, 0.f, 0.f, "Output: final level. Match it with the bypassed level when comparing." } },
        { { "mode", "Squeeze mode", { "Gentle", "OTT style", "Dance Master", "Glue", "Slow Smooth", "Punchy", "Tight Lock", "Lift", "Hardstyle Lock", "Highs Tamer", "Thick Supersaw" }, 0, "Squeeze style: each style is a different chain. Gentle keeps dynamics natural. OTT presses the loud down and lifts the quiet up hard. Dance Master is a controlled multiband compressor with soft knee, auto release and a limiter on each band. Glue is mild and sticky. Slow Smooth is slow and smooth, like an opto. Punchy lets the attack through. Tight Lock clamps hard and fast. Lift brings up quiet parts. Hardstyle Lock is fast and firm with extra low-end control. Highs Tamer holds back harsh highs on leads (good after a reverb). Thick Supersaw is a slow, soft squeeze that pairs with the Opto knob to fatten supersaw leads." },
          { "lcmode", "Loudness match", { "Auto", "Off", "On" }, 0, "Loudness match: a slow level follower lifts the processed sound back up to the level that went in, so heavy squeeze or opto settings never lose volume or loudness. Auto switches it on as the Opto amount grows. It only ever lifts, and never past the Ceiling." },
          { "airchar", "Air exciter", { "Shipped: tanh(5x)", "PurrOne stack (gritty when pushed)" }, 0, "The shaper that makes the Air harmonics. The stack version gets gritty and fuzzy when Air is pushed high." },
          { "wchar", "Warm character", { "Shipped: tanh", "Drive (opamp tanh)", "Transistor (even harmonics)", "Fuzz (lopsided)", "Stack: all three, grows with Warm" }, 0, "What the Warm knob does. Shipped is the plain tanh. The others are the PurrOne colour stages, oversampled 2x. Stack grows from drive to transistor to fuzz as you push Warm." },
          { "clipPos", "Clipper position", { "Pre: on the input", "Post: just before the limiter" }, 1, "Where the Clip stage sits. Post (before the limiter) keeps the squeeze natural and is what mastering clippers do. Pre clips the input before everything." } })
{
    for (int i = 0; i < 3; ++i) { bandLv[i] = -100.f; bandGain[i] = 0.f; }
}

void Meowximizer::prepare(double sr, int)
{
    core.prepare(sr);
    setLatencySamples(core.latency());   // exact: 3 oversampled stages (air, warm, clipper) plus the 1.5 ms limiter lookahead
}

void Meowximizer::process(juce::AudioBuffer<float>& buf, const juce::AudioBuffer<float>*)
{
    const int n = buf.getNumSamples(), nch = juce::jmin(2, buf.getNumChannels());
    if (n <= 0 || nch <= 0) return;
    mx::Params p;
    p.contour = get("contour"); p.bass = get("bass"); p.process = get("process"); p.air = get("air"); p.squeeze = get("squeeze"); p.warm = get("warm");
    p.push = get("push"); p.opto = get("opto"); p.ceil = get("ceil"); p.mix = get("mix"); p.out = get("out"); p.clip = get("clip");
    p.mode = juce::jlimit(0, kNumStyles - 1, (int) get("mode")); p.wchar = (int) get("wchar"); p.airchar = (int) get("airchar");
    p.clipPos = (int) get("clipPos"); p.lcmode = (int) get("lcmode");
    core.process(buf.getArrayOfWritePointers(), nch, n, p);
    optoGr = core.optoGrDb; lift = core.liftNow; dLo = core.delayLoMs; dMid = core.delayMidMs;
    for (int k = 0; k < 3; ++k) { bandLv[k] = core.bandPeak[k]; bandGain[k] = core.bandGainDb[k]; }
}

// Picking a squeeze style moves Squeeze, Push and Opto to the amounts that suit that style (you can still change them afterwards).
std::vector<std::pair<juce::String, float>> Meowximizer::choiceDefaults(const juce::String& id, int item) const
{
    if (id != "mode") return {};
    struct D { float squeeze, push, opto; };
    static const D d[kNumStyles] = { { 25.f, 0.f, 0.f }, { 55.f, 0.f, 0.f }, { 55.f, 2.f, 0.f }, { 45.f, 0.f, 0.f }, { 55.f, 1.f, 0.f }, { 55.f, 2.f, 0.f },
                                     { 60.f, 0.f, 0.f }, { 50.f, 0.f, 0.f }, { 60.f, 1.f, 0.f }, { 65.f, 1.f, 0.f }, { 85.f, 0.f, 0.f } };
    const auto& v = d[juce::jlimit(0, kNumStyles - 1, item)];
    return { { "squeeze", v.squeeze }, { "push", v.push }, { "opto", v.opto } };
}

std::vector<catfx::PanelSpec> Meowximizer::panels() const
{
    return { { "Tone", { "contour", "bass", "process", "air" }, { "airchar" }, 3.2f },
             { "Squeeze", { "squeeze", "push", "opto" }, { "mode", "lcmode" }, 2.4f },
             { "Saturation and ceiling", { "warm", "clip", "ceil" }, { "wchar", "clipPos" }, 2.1f },
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
        { "Drum Smack", { { "process", 7.f }, { "squeeze", 50.f }, { "warm", 35.f }, { "air", 20.f } } },
        { "Thick and Smooth", { { "mode", 2.f }, { "opto", 45.f }, { "squeeze", 35.f }, { "push", 2.f }, { "warm", 30.f }, { "process", 4.f }, { "contour", 3.f }, { "air", 15.f } } },
        { "LA-2A Lead Thicken", { { "mode", 0.f }, { "opto", 55.f }, { "squeeze", 15.f }, { "push", 2.f }, { "warm", 35.f }, { "contour", 2.f }, { "air", 10.f }, { "bass", 10.f }, { "process", 3.f } } },
        { "LA-2A Bass Smooth", { { "mode", 0.f }, { "opto", 60.f }, { "squeeze", 10.f }, { "warm", 30.f }, { "contour", 3.f }, { "bass", 30.f }, { "air", 5.f }, { "process", 2.f } } },
        { "Dance Master Lead", { { "mode", 2.f }, { "squeeze", 55.f }, { "push", 3.f }, { "air", 20.f }, { "process", 4.f }, { "warm", 20.f }, { "ceil", -0.5f } } },
        { "Dance Master Bass", { { "mode", 2.f }, { "squeeze", 60.f }, { "push", 3.f }, { "contour", 4.f }, { "bass", 35.f }, { "process", 3.f }, { "air", 5.f }, { "warm", 25.f } } },
        { "Reverb Lead Control", { { "mode", 2.f }, { "squeeze", 65.f }, { "opto", 25.f }, { "air", 10.f }, { "process", 3.f }, { "warm", 15.f }, { "push", 2.f } } },
        { "Goodizer Light", { { "mode", 1.f }, { "squeeze", 20.f }, { "push", 1.f }, { "warm", 20.f }, { "air", 25.f }, { "contour", 3.f } } },
        { "Goodizer Medium", { { "mode", 1.f }, { "squeeze", 40.f }, { "push", 3.f }, { "warm", 30.f }, { "air", 30.f }, { "contour", 4.f }, { "bass", 30.f } } },
        { "Sub Bass Thick", { { "mode", 2.f }, { "opto", 50.f }, { "squeeze", 40.f }, { "push", 2.f }, { "contour", 5.f }, { "bass", 40.f }, { "process", 2.f }, { "air", 0.f }, { "warm", 30.f } } },
        { "Pluck Punch", { { "mode", 2.f }, { "squeeze", 50.f }, { "push", 3.f }, { "process", 6.f }, { "air", 25.f }, { "warm", 20.f }, { "contour", 2.f } } },
        { "Pad Smooth", { { "mode", 0.f }, { "opto", 35.f }, { "squeeze", 20.f }, { "process", 3.f }, { "air", 30.f }, { "warm", 15.f }, { "contour", 1.f } } },
        { "Vocal Leveller", { { "mode", 0.f }, { "opto", 50.f }, { "squeeze", 15.f }, { "process", 5.f }, { "air", 45.f }, { "warm", 10.f }, { "contour", 1.f } } },
        { "Hardstyle Kick Glue", { { "mode", 2.f }, { "squeeze", 55.f }, { "push", 4.f }, { "contour", 5.f }, { "bass", 35.f }, { "process", 5.f }, { "air", 15.f }, { "warm", 35.f } } },
        { "Supersaw Control", { { "mode", 2.f }, { "squeeze", 60.f }, { "push", 2.f }, { "air", 5.f }, { "process", 3.f }, { "warm", 15.f }, { "opto", 20.f } } },
        { "Mix Bus Glue", { { "mode", 0.f }, { "squeeze", 20.f }, { "opto", 25.f }, { "warm", 15.f }, { "process", 2.f }, { "air", 10.f }, { "contour", 1.f } } },
        { "Glue Bus", { { "mode", 3.f }, { "squeeze", 40.f }, { "warm", 20.f }, { "process", 2.f }, { "air", 10.f } } },
        { "Smooth Lead (Slow)", { { "mode", 4.f }, { "squeeze", 45.f }, { "opto", 35.f }, { "warm", 30.f }, { "contour", 2.f }, { "process", 3.f }, { "push", 2.f } } },
        { "Punchy Drums", { { "mode", 5.f }, { "squeeze", 55.f }, { "push", 3.f }, { "process", 6.f }, { "warm", 25.f }, { "air", 20.f } } },
        { "Tight Bass Lock", { { "mode", 6.f }, { "squeeze", 60.f }, { "push", 2.f }, { "contour", 4.f }, { "bass", 30.f }, { "warm", 25.f }, { "air", 0.f } } },
        { "Quiet Lift", { { "mode", 7.f }, { "squeeze", 50.f }, { "air", 20.f }, { "process", 4.f }, { "warm", 10.f } } },
        { "Hardstyle Lead Lock", { { "mode", 8.f }, { "squeeze", 60.f }, { "push", 4.f }, { "air", 20.f }, { "process", 5.f }, { "warm", 30.f }, { "ceil", -0.5f } } },
        { "Harsh Highs Tamer", { { "mode", 9.f }, { "squeeze", 65.f }, { "opto", 20.f }, { "air", 5.f }, { "process", 3.f }, { "warm", 15.f }, { "push", 2.f } } },
        { "Thick Supersaw", { { "mode", 10.f }, { "squeeze", 85.f }, { "opto", 0.f }, { "warm", 35.f }, { "process", 3.f }, { "contour", 1.f }, { "air", 10.f }, { "bass", 0.f } } },
        { "Kick Beef (Parallel)", { { "mode", 5.f }, { "squeeze", 70.f }, { "push", 2.f }, { "mix", 50.f }, { "contour", 4.f }, { "bass", 20.f }, { "process", 2.f }, { "air", 0.f }, { "warm", 25.f } } },
        { "Mastering Touch", { { "mode", 0.f }, { "squeeze", 15.f }, { "push", 1.f }, { "warm", 10.f }, { "air", 12.f }, { "process", 3.f }, { "contour", 1.f }, { "ceil", -0.3f } } } };
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
        g.setFont(juce::FontOptions(11.f));
        g.setColour(green()); g.drawText("bands: before (grey),", (int) r.getX() + 4, (int) r.getY() + 1, (int) r.getWidth(), 12, juce::Justification::left);
        g.setColour(accent()); g.drawText("after", (int) r.getX() + 120, (int) r.getY() + 1, 60, 12, juce::Justification::left);
        {   // what the chosen style is running right now
            const auto& sty = mx::kStyles[juce::jlimit(0, kNumStyles - 1, (int) get("mode"))];
            const float depth = get("squeeze") / 100.f, oe = juce::jlimit(0.f, 1.f, get("opto") / 100.f + sty.optoMix * depth);
            g.setFont(juce::FontOptions(10.f));
            g.setColour(accent()); g.drawText("opto " + juce::String((int) std::round(oe * 100.f)), (int) r.getX() + 34, (int) r.getY() + 13, 52, 11, juce::Justification::left);
            g.setColour(blue());   g.drawText("mb " + juce::String((int) std::round(sty.mb * depth * 100.f)), (int) r.getX() + 84, (int) r.getY() + 13, 40, 11, juce::Justification::left);
            g.setColour(green());  g.drawText("sat " + juce::String((int) std::round(sty.sat * depth * 100.f)), (int) r.getX() + 122, (int) r.getY() + 13, 44, 11, juce::Justification::left);
            g.setColour(red());    g.drawText("+" + juce::String(sty.maxi * depth, 1), (int) r.getX() + 160, (int) r.getY() + 13, 40, 11, juce::Justification::left);
        }
    }
    else if (idx == 2) // saturation curve and the limiter ceiling
    {
        const float warm = get("warm") / 100.f, d = 1.f + warm * 1.5f, ceil = get("ceil"), clipAmt = get("clip") / 100.f, preG = juce::Decibels::decibelsToGain(9.f * clipAmt);
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
            const float x = -1.f + (float) i / 100.f, s = (1.f - warm) * x + warm * std::tanh(d * x) / d, y = juce::jlimit(-ceilLin, ceilLin, clipAmt > 0.f ? std::tanh(s * preG * 1.2f) / 1.2f : s);
            i ? c.lineTo(X(x), Y(y)) : c.startNewSubPath(X(x), Y(y));
        }
        g.setColour(accent()); g.strokePath(c, juce::PathStrokeType(2.5f));
        const float lv = juce::jlimit(0.f, 1.f, inLevel.load()), sl = (1.f - warm) * lv + warm * std::tanh(d * lv) / d, yy = juce::jlimit(-ceilLin, ceilLin, clipAmt > 0.f ? std::tanh(sl * preG * 1.2f) / 1.2f : sl);
        g.setColour(juce::Colours::white); g.fillEllipse(X(lv) - 3.f, Y(yy) - 3.f, 6.f, 6.f);
        g.setColour(accent()); g.setFont(juce::FontOptions(11.f));
        g.drawText("saturation and limiter ceiling", (int) r.getX() + 4, (int) r.getY() + 1, (int) r.getWidth(), 12, juce::Justification::left);
        g.setColour(red()); g.drawText("ceiling " + juce::String(ceil, 1) + " dB", p.toNearestInt().withTrimmedTop(2).withHeight(12).withTrimmedRight(4), juce::Justification::right);
        g.setColour(blue()); g.drawText("input level", p.toNearestInt().withY((int) p.getBottom() + 1).withHeight(12), juce::Justification::centred);
    }
    else                // level: IN and OUT meters
    {
        auto area = r.reduced(6.f, 6.f);
        auto left = area.removeFromLeft(area.getWidth() * 0.5f);
        meter(g, left, inLevel.load(), "IN dB");
        meter(g, area, outLevel.load(), "OUT dB");
    }
}
