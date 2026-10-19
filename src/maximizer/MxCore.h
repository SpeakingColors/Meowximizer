#pragma once
// Meowximizer DSP core: plain C++17 (no JUCE), streaming, no allocation after prepare(), no locks.
// Port of the Cat FX Lab chain (mxProcess): 3 bands, squeeze styles, opto, bass harmonics, process delays,
// air exciter (2x oversampled, other bands delayed to match), warm stage (2x oversampled), closed-loop loudness
// match, post clipper (2x oversampled) and a lookahead brick-wall limiter (the ceiling holds, unlike juce::dsp::Limiter).
// Every level style knob moves through a 20 ms linear ramp, so a knob change is heard at once and never clicks.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

namespace mx
{
constexpr double kPi = 3.14159265358979323846;
inline float dbToGain(float db) { return std::pow(10.f, db * 0.05f); }
inline float gainToDb(float g, float floorDb = -100.f) { return std::max(floorDb, 20.f * std::log10(std::max(g, 1e-12f))); }
inline float jlimit(float lo, float hi, float v) { return v < lo ? lo : v > hi ? hi : v; }

// ---- squeeze styles (thr, ratio, up, lo, knee, att, rel, relAuto, floor, off[3], limit, sub, optoMix, mb, sat, maxi) ----
struct SqStyle { float thr, ratio, up, lo, knee, att, rel, relAuto, floor; float off[3]; bool limit, sub; float optoMix, mb, sat, maxi; };
constexpr int kNumStyles = 11;
constexpr SqStyle kStyles[kNumStyles] = {
    { -14.f, 2.5f, 0.20f, -52.f,  0.f,  4.f,  80.f,  0.f, 0.00f, { 0.f, 0.f, 0.f },   false, false, 0.f, 1.f, 0.f, 0.f },     // Gentle
    { -22.f, 5.0f, 0.45f, -48.f,  0.f,  4.f,  80.f,  0.f, 0.00f, { 0.f, 0.f, 0.f },   false, false, 0.f, 1.f, 0.10f, 2.f },    // OTT style
    { -18.f, 3.5f, 0.00f, -60.f,  6.f,  2.f,  40.f, 12.f, 0.35f, { 0.f, 0.f, -4.f },  true,  true, 0.f, 1.f, 0.15f, 1.f },     // Dance Master
    { -16.f, 2.0f, 0.10f, -56.f, 10.f, 10.f, 120.f,  6.f, 0.20f, { 0.f, 0.f, 0.f },   false, false, 0.20f, 0.80f, 0.20f, 0.f }, // Glue
    { -18.f, 2.2f, 0.10f, -56.f, 12.f, 10.f, 250.f, 10.f, 0.25f, { 0.f, 0.f, 0.f },   false, false, 0.50f, 0.50f, 0.15f, 0.f }, // Slow Smooth
    { -14.f, 3.0f, 0.00f, -60.f,  4.f, 20.f,  60.f,  0.f, 0.20f, { 0.f, 0.f, 0.f },   false, false, 0.f, 0.90f, 0.30f, 1.f },   // Punchy
    { -20.f, 6.0f, 0.00f, -60.f,  3.f,  1.f,  50.f, 10.f, 0.40f, { 0.f, 0.f, -2.f },  true,  true, 0.f, 1.f, 0.10f, 3.f },     // Tight Lock
    { -24.f, 1.5f, 0.60f, -55.f,  6.f,  5.f, 120.f,  0.f, 0.00f, { 0.f, 0.f, 0.f },   false, false, 0.f, 1.f, 0.f, 0.f },       // Lift
    { -20.f, 4.0f, 0.10f, -60.f,  4.f,  3.f,  30.f, 15.f, 0.40f, { 2.f, 0.f, -3.f },  true,  true, 0.10f, 1.f, 0.40f, 3.f },    // Hardstyle Lock
    { -16.f, 4.0f, 0.00f, -60.f,  8.f,  3.f,  60.f, 12.f, 0.40f, { 0.f, -2.f, -9.f }, true,  true, 0.25f, 1.f, 0.10f, 0.f },    // Highs Tamer
    { -20.f, 2.0f, 0.00f, -60.f, 12.f, 10.f, 200.f, 20.f, 0.30f, { 0.f, 0.f, -2.f },  false, false, 0.85f, 0.45f, 0.35f, 0.5f } }; // Thick Supersaw

// ---- PurrOne colour stack (same maths as the PurrOne synth colour stages) ----
inline float sstep(float t) { t = t < 0.f ? 0.f : t > 1.f ? 1.f : t; return t * t * (3.f - 2.f * t); }
inline float asymTanh(float u, float nl) { return u >= 0.f ? std::tanh(u) : nl * std::tanh(u / nl); }
inline float purrShaper(float x, float drive, float xs, float fuzz)
{
    const float dn = sstep((drive - 0.8f) * 5.f), xn = sstep((xs - 0.8f) * 5.f), fn = sstep((fuzz - 0.8f) * 5.f);
    float y = x;
    if (drive > 0.001f) { y = std::tanh(x * (1.f + drive * 5.f + dn * 30.f)); if (dn > 0.f) y += dn * (std::tanh(y * (2.f + 10.f * dn)) - y); }
    if (xs > 0.001f)
    {
        const float nl = std::max(0.25f, 1.f - 0.3f * xs - 0.25f * xn), u = y * (1.f + xs * 5.f + xn * 30.f);
        y = asymTanh(u, nl); y += (0.3f * xs + 0.25f * xn) * y * y;
        if (xn > 0.f) { const float y2 = asymTanh(y * (2.f + 10.f * xn) + 0.15f * xn, nl); y += xn * (y2 - y); }
    }
    if (fuzz > 0.001f)
    {
        const float fz = 1.f + fuzz * 14.f + fn * 70.f, bf = 0.5f * std::min(1.f, fuzz);
        y = std::tanh(y * fz + bf) - std::tanh(bf);
        if (fn > 0.f) { const float r = std::sqrt(y * y + 0.02f) - 0.1414f; y += fn * 0.7f * (r - 0.3f); y = std::tanh(y * (1.f + 0.8f * fn)); }
    }
    return y;
}
inline float purrMakeup(float drive, float xs, float fuzz, float ref)
{
    float a = 0.f, b = 0.f;
    for (int i = 0; i < 64; ++i) { const float x = ref * std::sin(2.f * (float) kPi * (float) i / 64.f), y1 = purrShaper(x, drive, xs, fuzz); a += x * x; b += y1 * y1; }
    const float m = b > 1e-9f ? std::sqrt(a / b) : 1.f;
    return jlimit(0.25f, 4.f, m);
}

// ---- small helpers ----
inline float bandSat(float x, float amt) { return x * (1.f - amt) + amt * 0.5f * std::tanh(2.f * x); }
inline float bandLimit(float x, float c) { const float a = std::abs(x), k = 0.7f * c; if (a <= k) return x; const float y = k + (c - k) * std::tanh((a - k) / (c - k)); return x < 0.f ? -y : y; }
// light peak clipper: pre-gain 9 dB per 100 %, smooth knee from half the ceiling, asymptote at c
inline float softClip(float v, float c, float amt) { const float u = v * dbToGain(amt * 9.f), a = std::abs(u), k = 0.5f * c; if (a <= k) return u; const float r = k + (c - k) * std::tanh((a - k) / (c - k)); return u < 0.f ? -r : r; }

// 20 ms linear ramp to a new target (no allocation, no locks)
struct Ramp
{
    float cur = 0.f, tgt = 0.f, step = 0.f; int left = 0;
    void reset(float v) { cur = tgt = v; step = 0.f; left = 0; }
    void set(float v, int len) { if (v == tgt) return; tgt = v; if (len < 1) { cur = v; left = 0; return; } left = len; step = (tgt - cur) / (float) len; }
    float next() { if (left > 0) { cur += step; if (--left == 0) cur = tgt; } return cur; }
    bool moving() const { return left > 0; }
};

struct Biquad
{
    float b0 = 1.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f, z1 = 0.f, z2 = 0.f;
    void set(const double c[6]) { const double a0 = c[3]; b0 = (float) (c[0] / a0); b1 = (float) (c[1] / a0); b2 = (float) (c[2] / a0); a1 = (float) (c[4] / a0); a2 = (float) (c[5] / a0); }
    void reset() { z1 = z2 = 0.f; }
    float tick(float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
};
inline void bqHighPass(double out[6], double fs, double f, double q = 0.7071)
{ const double n = std::tan(kPi * f / fs), n2 = n * n, c1 = 1.0 / (1.0 + n / q + n2); out[0] = c1; out[1] = -2 * c1; out[2] = c1; out[3] = 1; out[4] = c1 * 2 * (n2 - 1); out[5] = c1 * (1 - n / q + n2); }
inline void bqLowPass(double out[6], double fs, double f, double q = 0.7071)
{ const double n = 1.0 / std::tan(kPi * f / fs), n2 = n * n, c1 = 1.0 / (1.0 + n / q + n2); out[0] = c1; out[1] = c1 * 2; out[2] = c1; out[3] = 1; out[4] = c1 * 2 * (1 - n2); out[5] = c1 * (1 - n / q + n2); }
inline void bqLowShelf(double out[6], double fs, double f, double q, double gain)
{
    const double A = std::sqrt(gain), am1 = A - 1, ap1 = A + 1, w = 2 * kPi * std::max(f, 2.0) / fs, co = std::cos(w), beta = std::sin(w) * std::sqrt(A) / q, am1c = am1 * co;
    out[0] = A * (ap1 - am1c + beta); out[1] = A * 2 * (am1 - ap1 * co); out[2] = A * (ap1 - am1c - beta); out[3] = ap1 + am1c + beta; out[4] = -2 * (am1 + ap1 * co); out[5] = ap1 + am1c - beta;
}
inline void bqHighShelf(double out[6], double fs, double f, double q, double gain)
{
    const double A = std::sqrt(std::max(0.0, gain)), am = A - 1, ap = A + 1, om = 2 * kPi * std::max(f, 2.0) / fs, co = std::cos(om), be = std::sin(om) * std::sqrt(A) / q, amc = am * co;
    out[0] = A * (ap + amc + be); out[1] = A * -2 * (am + ap * co); out[2] = A * (ap + amc - be); out[3] = ap - amc + be; out[4] = 2 * (am - ap * co); out[5] = ap - amc - be;
}

// Linkwitz-Riley 24 dB split (same form as juce::dsp::LinkwitzRileyFilter): low and high outputs
struct LR
{
    float s1 = 0.f, s2 = 0.f, s3 = 0.f, s4 = 0.f, g = 0.f, h = 0.f; static constexpr float R2 = 1.41421356237f;
    void set(double fs, double fc) { g = (float) std::tan(kPi * fc / fs); h = 1.f / (1.f + R2 * g + g * g); }
    void reset() { s1 = s2 = s3 = s4 = 0.f; }
    void split(float x, float& lo, float& hi)
    {
        const float yH = (x - (R2 + g) * s1 - s2) * h, yB = g * yH + s1; s1 = g * yH + yB;
        const float yL = g * yB + s2; s2 = g * yB + yL;
        const float yH2 = (yL - (R2 + g) * s3 - s4) * h, yB2 = g * yH2 + s3; s3 = g * yH2 + yB2;
        const float yL2 = g * yB2 + s4; s4 = g * yB2 + yL2;
        lo = yL2; hi = yL - R2 * yB + yH - yL2;
    }
};

// Delay line with linear interpolation (fractional delay, so a moving Process knob never clicks)
struct Delay
{
    static constexpr int kSize = 16384; // power of two, above 0.025 s at 384 kHz
    std::array<float, kSize> buf {}; int w = 0;
    void reset() { buf.fill(0.f); w = 0; }
    void write(float x) { buf[(size_t) w] = x; w = (w + 1) & (kSize - 1); }
    // d samples ago, counted after write(): d = 0 returns the sample just written
    float read(float d) const
    {
        const int i = (int) d; const float f = d - (float) i;
        const float a = buf[(size_t) ((w - 1 - i) & (kSize - 1))], b = buf[(size_t) ((w - 2 - i) & (kSize - 1))];
        return a + f * (b - a);
    }
};

// ---- 2x oversampling with one half-band FIR (63 taps), streaming. Round trip latency: kLat samples at the base rate. ----
struct HalfBandTaps
{
    static constexpr int kLen = 63, kM = 31;
    int evenK[kLen] {}, oddK[kLen] {}, nE = 0, nO = 0; float evenV[kLen] {}, oddV[kLen] {}, dnK[kLen] {}; int dnI[kLen] {}; float dnV[kLen] {}; int nD = 0;
    HalfBandTaps()
    {
        auto i0 = [](double x) { double s = 1, t = 1; for (int k = 1; k < 30; ++k) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; } return s; };
        for (int i = 0; i < kLen; ++i)
        {
            const double r = 2.0 * i / (kLen - 1) - 1.0, w = i0(8.0 * std::sqrt(std::max(0.0, 1 - r * r))) / i0(8.0), k = i - kM;
            const double h = (k == 0 ? 0.5 : std::sin(kPi * k / 2) / (kPi * k)) * w;
            if (std::abs(h) <= 1e-12) continue;
            if (i % 2 == 0) { evenK[nE] = i / 2; evenV[nE++] = (float) (2.0 * h); } else { oddK[nO] = (i - 1) / 2; oddV[nO++] = (float) (2.0 * h); }
            dnI[nD] = i; dnV[nD++] = (float) h;
        }
    }
    static const HalfBandTaps& get() { static const HalfBandTaps t; return t; }
};
struct Os2
{
    static constexpr int kLat = HalfBandTaps::kM; // base-rate samples, up + down together
    std::array<float, 64> hx {}; int wx = 0; std::array<float, 128> hv {}; int wv = 0;
    void reset() { hx.fill(0.f); hv.fill(0.f); wx = wv = 0; }
    // one base-rate sample in, run the memoryless-or-stateful callable at the double rate, one base-rate sample out
    template <class F> float process(float x, F&& fn)
    {
        const auto& t = HalfBandTaps::get();
        wx = (wx + 1) & 63; hx[(size_t) wx] = x;
        float u0 = 0.f, u1 = 0.f;
        for (int j = 0; j < t.nE; ++j) u0 += t.evenV[j] * hx[(size_t) ((wx - t.evenK[j]) & 63)];
        for (int j = 0; j < t.nO; ++j) u1 += t.oddV[j] * hx[(size_t) ((wx - t.oddK[j]) & 63)];
        const float v0 = fn(u0), v1 = fn(u1);
        wv = (wv + 1) & 127; hv[(size_t) wv] = v0; wv = (wv + 1) & 127; hv[(size_t) wv] = v1;
        // output uses the even-index sample v0 as "now": it sits one step behind the write head
        float y = 0.f; const int base = (wv - 1) & 127;
        for (int j = 0; j < t.nD; ++j) y += t.dnV[j] * hv[(size_t) ((base - t.dnI[j]) & 127)];
        return y;
    }
};

struct Squeezer
{
    float env = 0.f, grAuto = 0.f; bool wide = false;   // wide: Nasty range lets a band swing further (-48 to +30 dB)
    float gain(float peak, float depth, const SqStyle& s, int band, float fs, float& gainDbOut)
    {
        const float att = std::exp(-1.f / (s.att * 0.001f * fs)), rel = std::exp(-1.f / ((s.rel + s.relAuto * grAuto) * 0.001f * fs));
        const float a = peak > env ? att : rel; env = a * env + (1.f - a) * peak;
        const float db = gainToDb(env), thr = s.thr + s.off[band], over = db - thr, slope = 1.f - 1.f / s.ratio;
        float red = 0.f;
        if (s.knee > 0.f && over > -s.knee * 0.5f && over < s.knee * 0.5f) { const float t = over + s.knee * 0.5f; red = slope * t * t / (2.f * s.knee); }
        else if (over >= s.knee * 0.5f && over > 0.f) red = over * slope;
        const float up = (db > s.lo && db < thr) ? (thr - db) * s.up : 0.f;
        grAuto += 0.001f * (red - grAuto);
        const float scale = depth > 0.f ? s.floor + (1.f - s.floor) * depth : 0.f;
        gainDbOut = wide ? jlimit(-48.f, 30.f, (up - red) * scale * s.mb) : jlimit(-24.f, 18.f, (up - red) * scale * s.mb);
        return dbToGain(gainDbOut);
    }
};
// LA-2A style electro-optical leveller
struct Opto
{
    float gr1 = 0.f, gr2 = 0.f, mem = 0.f, env = 0.f, gr = 0.f;
    float gain(float peak, float amount, float fs)
    {
        const float p = jlimit(0.f, 1.f, amount), k = 1.f - std::exp(-1.f / (0.0015f * fs));
        env += k * (peak - env);
        const float db = gainToDb(env), thr = -6.f - 30.f * p, knee = 12.f, over = db - thr;
        float target = 0.f;
        if (over > knee * 0.5f) target = over * (1.f - 1.f / 3.f);
        else if (over > -knee * 0.5f) { const float t = over + knee * 0.5f; target = (2.f / 3.f) * t * t / (2.f * knee); }
        const float att = 1.f - std::exp(-1.f / (0.010f * fs)), rel1 = 1.f - std::exp(-1.f / (0.0866f * fs)), tau2 = 1.f + 4.f * mem, rel2 = 1.f - std::exp(-1.f / (tau2 * fs));
        gr1 += (target > gr1 ? att : rel1) * (target - gr1); gr2 += (target > gr2 ? att : rel2) * (target - gr2);
        mem += (1.f - std::exp(-1.f / (2.f * fs))) * (jlimit(0.f, 1.f, 0.5f * (gr1 + gr2) / 15.f) - mem);
        gr = 0.5f * (gr1 + gr2);
        return dbToGain(-gr + 6.f * p);
    }
};

// Lookahead brick-wall limiter, linked over both channels: 1.5 ms lookahead, boxcar-smoothed attack, one-pole release.
// The output never exceeds the ceiling (the old juce::dsp::Limiter lifted the signal by -Ceiling and then hard clipped).
struct Lookahead
{
    int N = 66; float rel = 0.f, s2 = 1.f, acc = 0.f; long t = 0;
    std::vector<float> r, gm; std::array<std::vector<float>, 2> x; int pos = 0, xpos = 0;
    void prepare(double fs, int n)
    {
        N = std::max(1, (int) std::lround(0.0015 * fs)); (void) n;
        r.assign((size_t) N + 1, 1.f); gm.assign((size_t) N + 1, 0.f); for (auto& v : x) v.assign((size_t) N, 0.f);
        rel = 1.f - std::exp(-1.f / (0.08f * (float) fs)); s2 = 1.f; acc = 0.f; t = 0; pos = 0; xpos = 0;
    }
    void reset() { std::fill(r.begin(), r.end(), 1.f); std::fill(gm.begin(), gm.end(), 0.f); for (auto& v : x) std::fill(v.begin(), v.end(), 0.f); s2 = 1.f; acc = 0.f; t = 0; pos = 0; xpos = 0; }
    void process(float& l, float& rr, float c)
    {
        const float a = std::max(std::abs(l), std::abs(rr));
        r[(size_t) pos] = a > c ? c / a : 1.f;
        float m = 1.f; for (int k = 0; k <= N; ++k) m = std::min(m, r[(size_t) k]);   // min over the last N+1 samples
        acc += m - gm[(size_t) pos]; gm[(size_t) pos] = m;
        const float cnt = (float) std::min<long>((long) N + 1, t + 1), s = acc / cnt;
        s2 = s < s2 ? s : s2 + (s - s2) * rel;
        const float gdel = std::min(s2, s);
        const float dl = x[0][(size_t) xpos], dr = x[1][(size_t) xpos];   // the sample from N steps ago (ring of N)
        x[0][(size_t) xpos] = l; x[1][(size_t) xpos] = rr; if (++xpos >= N) xpos = 0;
        l = t >= N ? dl * gdel : 0.f; rr = t >= N ? dr * gdel : 0.f;
        ++t; if (++pos > N) pos = 0;
    }
};

struct Params
{
    float contour = 3.f, bass = 25.f, process = 5.f, air = 25.f, squeeze = 30.f, warm = 20.f, push = 0.f, opto = 0.f, ceil = -0.3f, mix = 100.f, out = 0.f, clip = 0.f;
    int mode = 0, wchar = 0, airchar = 0, clipPos = 1, lcmode = 0;   // lcmode: 0 auto, 1 off, 2 on
    bool nasty = false;
};

class Core
{
public:
    static constexpr int kMaxCh = 2;
    void prepare(double sampleRate)
    {
        fs = (float) sampleRate; rampLen = std::max(1, (int) std::lround(0.020 * sampleRate));
        look.prepare(sampleRate, 0);
        for (int c = 0; c < kMaxCh; ++c) { ch[c].lr1.set(sampleRate, 150.0); ch[c].lr2.set(sampleRate, 2500.0); ch[c].lrAp.set(sampleRate, 2500.0); }
        double co[6];
        for (int c = 0; c < kMaxCh; ++c)
        {
            bqHighPass(co, sampleRate, 22.0); ch[c].subHp.set(co);
            bqHighPass(co, sampleRate * 2.0, 5000.0); ch[c].airHp1.set(co);
            bqHighPass(co, sampleRate * 2.0, 7000.0); ch[c].airHp2.set(co);
        }
        bqHighPass(co, sampleRate, 150.0); bHp.set(co); bqLowPass(co, sampleRate, 500.0); bLp.set(co);
        reset();
    }
    void reset()
    {
        for (auto& c : ch) { c.lr1.reset(); c.lr2.reset(); c.lrAp.reset(); c.shelf.reset(); c.subHp.reset(); c.airHp1.reset(); c.airHp2.reset(); c.airShelf.reset(); c.osAir.reset(); c.osWarm.reset(); c.osClip.reset();
                             c.dLo.reset(); c.dMid.reset(); c.dHi.reset(); c.dWarm.reset(); c.dClip.reset(); c.dDry.reset(); c.dcz = 0.f; }
        bHp.reset(); bLp.reset(); look.reset(); opto = {}; for (auto& s : sq) s = {};
        bassEnv = bassDc = 0.f; midEnvDb = -60.f; liftDb = 0.f; pi = po = 0.f; gl = 1.f; first = true; lastContour = -1.f; lastAir = -1.f;
    }
    // exact latency in samples at the host rate: the air oversampler (all bands are aligned to it) + warm + clip oversamplers + the limiter lookahead
    int latency() const { return 3 * Os2::kLat + look.N; }

    // meters for the editor (written once per call)
    float bandPeak[3] {}, bandGainDb[3] {}, optoGrDb = 0.f, liftNow = 0.f, delayLoMs = 0.f, delayMidMs = 0.f;

    // in place, 1 or 2 channels
    void process(float* const* io, int nch, int n, const Params& p)
    {
        nch = std::min(nch, kMaxCh);
        const int mode = std::max(0, std::min(kNumStyles - 1, p.mode)); const SqStyle& sty = kStyles[mode];
        for (int b = 0; b < 3; ++b) sq[b].wide = p.nasty;
        const float depthT = p.squeeze * 0.01f, warmT = p.warm * 0.01f;
        const float optoAmtT = jlimit(0.f, 1.f, p.opto * 0.01f + sty.optoMix * depthT);
        const bool nChn = nch > 1;
        if (first)
        {
            pushR.reset(dbToGain(p.push)); depthR.reset(depthT); procR.reset(p.process * 0.1f); bassR.reset(p.bass * 0.01f); airR.reset(p.air * 0.01f); warmR.reset(warmT); clipR.reset(p.clip * 0.01f);
            mixR.reset(p.mix * 0.01f); outR.reset(dbToGain(p.out)); ceilR.reset(dbToGain(p.ceil)); contourR.reset(p.contour); optoR.reset(optoAmtT);
            lcR.reset(p.lcmode == 1 ? 0.f : p.lcmode == 2 ? 1.f : std::min(1.f, optoAmtT * 5.f)); first = false;
        }
        pushR.set(dbToGain(p.push), rampLen); depthR.set(depthT, rampLen); procR.set(p.process * 0.1f, rampLen); bassR.set(p.bass * 0.01f, rampLen); airR.set(p.air * 0.01f, rampLen);
        warmR.set(warmT, rampLen); clipR.set(p.clip * 0.01f, rampLen); mixR.set(p.mix * 0.01f, rampLen); outR.set(dbToGain(p.out), rampLen); ceilR.set(dbToGain(p.ceil), rampLen);
        contourR.set(p.contour, rampLen); optoR.set(optoAmtT, rampLen);
        lcR.set(p.lcmode == 1 ? 0.f : p.lcmode == 2 ? 1.f : std::min(1.f, optoAmtT * 5.f), rampLen);

        const float aMid = std::exp(-1.f / (0.05f * fs)), aA = std::exp(-1.f / (0.004f * fs)), aR = std::exp(-1.f / (0.08f * fs));
        const float aE = std::exp(-1.f / (0.25f * fs)), gA = std::exp(-1.f / (0.05f * fs)), gR = std::exp(-1.f / (0.4f * fs));
        const float lat = (float) Os2::kLat;
        float pkLv[3] {}, gSum[3] {}, optoSum = 0.f; float lastLo = 0.f, lastMid = 0.f;

        for (int i = 0; i < n; ++i)
        {
            const float pushG = pushR.next(), depth = depthR.next(), proc = procR.next(), bassAmt = bassR.next(), airAmt = airR.next(), warm = jlimit(0.f, 1.f, warmR.next());
            const float clipAmt = clipR.next(), mix = mixR.next(), outG = outR.next(), ceilLin = ceilR.next(), contour = contourR.next(), optoAmt = optoR.next(), lc = lcR.next();
            const float dLoS = std::min(0.0025f * fs * proc, 12000.f), dMidS = std::min(0.0010f * fs * proc, 12000.f);
            lastLo = 1000.f * dLoS / fs; lastMid = 1000.f * dMidS / fs;
            const float satAmt = sty.sat * depth, maxG = dbToGain(sty.maxi * depth);
            if ((i & 15) == 0)
            {
                double co[6];
                if (contour != lastContour) { for (int c = 0; c < nch; ++c) { bqLowShelf(co, fs, 70.0, 0.7071, std::pow(10.0, contour * 1.1 / 20.0)); ch[c].shelf.set(co); } lastContour = contour; }
                if (airAmt != lastAir) { for (int c = 0; c < nch; ++c) { bqHighShelf(co, fs, 9000.0, 0.7071, std::pow(10.0, std::min(airAmt, 3.f) * 4.5 / 20.0)); ch[c].airShelf.set(co); } lastAir = airAmt; }
                // warm stage settings for this stretch (the PurrOne maths is too heavy to redo per sample)
                const float w = warmR.cur;
                wD = 1.f + w * 1.5f;
                wDr = p.wchar == 1 ? w : p.wchar == 4 ? std::min(w, 1.5f) : 0.f; wXs = p.wchar == 2 ? w : p.wchar == 4 ? std::max(0.f, w - 0.3f) : 0.f; wFz = p.wchar == 3 ? w : p.wchar == 4 ? std::max(0.f, w - 0.6f) : 0.f;
                wMk = p.wchar > 0 ? purrMakeup(wDr, wXs, wFz, 0.5f) : 1.f;
                aDr = std::min(1.5f, airAmt); aXs = airAmt * 0.6f; aFz = std::max(0.f, airAmt - 0.5f);
            }
            // ---- input, opto, band split ----
            float x[2] {}, v[2] {}, lo[2] {}, mid[2] {}, hi[2] {}, pkIn = 0.f, dryIn[2] {};
            for (int c = 0; c < nch; ++c) { dryIn[c] = io[c][i]; x[c] = dryIn[c] * pushG; pkIn = std::max(pkIn, std::abs(x[c])); ch[c].dDry.write(dryIn[c]); }
            float og = 1.f; if (optoAmt > 0.f) { og = opto.gain(pkIn, optoAmt, fs); optoSum += opto.gr; }
            for (int c = 0; c < nch; ++c)
            {
                v[c] = x[c];
                if (clipAmt > 0.f && p.clipPos == 0) v[c] = softClip(v[c], 0.9f, clipAmt);
                if (sty.sub) v[c] = ch[c].subHp.tick(v[c]);
                v[c] *= og;
                float l1, r1, a, b, m, h; ch[c].lr1.split(v[c], l1, r1); ch[c].lrAp.split(l1, a, b); lo[c] = a + b; ch[c].lr2.split(r1, m, h); mid[c] = m; hi[c] = h;
            }
            // ---- air exciter on the high band, 2x oversampled (its latency is matched by delaying every other band) ----
            float ex[2] {};
            if (airAmt > 0.f || airR.moving())
                for (int c = 0; c < nch; ++c)
                {
                    auto& cc = ch[c];
                    ex[c] = cc.osAir.process(hi[c], [&](float s)
                    {
                        const float t = cc.airHp1.tick(s);
                        const float sh = p.airchar ? purrShaper(t * 4.f, aDr, aXs, aFz) * 0.5f : std::tanh(5.f * t);
                        return cc.airHp2.tick(sh);
                    });
                }
            // ---- squeeze, bass harmonics, lift ----
            float pk[3] {}; for (int c = 0; c < nch; ++c) { pk[0] = std::max(pk[0], std::abs(lo[c])); pk[1] = std::max(pk[1], std::abs(mid[c])); pk[2] = std::max(pk[2], std::abs(hi[c])); }
            float gDb[3]; const float g0 = sq[0].gain(pk[0], depth, sty, 0, fs, gDb[0]), g1 = sq[1].gain(pk[1], depth, sty, 1, fs, gDb[1]), g2 = sq[2].gain(pk[2], depth, sty, 2, fs, gDb[2]);
            for (int k = 0; k < 3; ++k) { pkLv[k] = std::max(pkLv[k], pk[k]); gSum[k] += gDb[k]; }
            const float mono = nChn ? 0.5f * (lo[0] + lo[1]) : lo[0], am = std::abs(mono), kk = am > bassEnv ? aA : aR;
            bassEnv = kk * bassEnv + (1.f - kk) * am; float ev = am; bassDc += 0.002f * (ev - bassDc); ev -= bassDc;
            float bh = (ev * 2.f * 0.7f + std::tanh(4.f * mono) * 0.5f * 0.3f) * jlimit(0.f, 6.f, std::pow(std::max(bassEnv, 0.003f) / 0.25f, -0.5f)) * 0.25f;
            bh = bLp.tick(bHp.tick(bh)) * bassAmt * 7.5f;
            const float midDb = gainToDb(pk[1]); midEnvDb = aMid * midEnvDb + (1.f - aMid) * midDb;
            const float tgt = proc * (7.f + (-2.f - 7.f) * jlimit(0.f, 1.f, (midEnvDb + 50.f) / 35.f)); liftDb = aMid * liftDb + (1.f - aMid) * tgt; const float liftG = dbToGain(liftDb);
            // ---- recombine, then the warm stage ----
            float y[2] {};
            for (int c = 0; c < nch; ++c)
            {
                auto& cc = ch[c];
                float l = cc.shelf.tick(lo[c]) * g0 + bh, m = mid[c] * g1;
                if (satAmt > 0.f) { l = bandSat(l, satAmt); m = bandSat(m, satAmt); }
                if (sty.limit) { l = bandLimit(l, 0.85f); m = bandLimit(m, 0.8f); }
                cc.dLo.write(l); cc.dMid.write(m);
                l = cc.dLo.read(dLoS + lat); m = cc.dMid.read(dMidS + lat);
                float h = hi[c] * g2 * liftG; if (satAmt > 0.f) h = bandSat(h, satAmt); if (sty.limit) h = bandLimit(h, 0.6f);
                cc.dHi.write(h); h = cc.dHi.read(lat); h += ex[c] * airAmt * 0.5f;
                float o = l + m + h; o = cc.airShelf.tick(o);
                // warm: always runs the 2x path (constant latency); shipped = tanh, others = PurrOne stages
                cc.dWarm.write(o); const float od = cc.dWarm.read(lat);
                float sh = cc.osWarm.process(o, [&](float s) { return p.wchar == 0 ? std::tanh(wD * s) / wD : purrShaper(s * 1.5f, wDr, wXs, wFz) * wMk / 1.5f; });
                if (p.wchar != 0) { cc.dcz += 0.0005f * (sh - cc.dcz); sh -= cc.dcz; }
                y[c] = warmT > 0.f || warmR.moving() ? od * (1.f - warm) + warm * sh : od;
                y[c] *= maxG;
            }
            // ---- closed-loop loudness match: a slow RMS follower lifts the processed signal back to the level that went in ----
            if (lc > 0.f)
            {
                float xi2 = 0.f, ya2 = 0.f; for (int c = 0; c < nch; ++c) { xi2 += x[c] * x[c]; const float ya = jlimit(-ceilLin, ceilLin, y[c] * gl); ya2 += ya * ya; }
                xi2 /= (float) nch; ya2 /= (float) nch;
                pi = aE * pi + (1.f - aE) * xi2; po = aE * po + (1.f - aE) * ya2;
                const float tg = (po > 1e-10f && pi > 1e-10f) ? jlimit(1.f, 16.f, gl * std::sqrt(pi / po)) : 1.f, k2 = tg > gl ? gA : gR;
                gl = k2 * gl + (1.f - k2) * tg;
                const float mg = 1.f + (gl - 1.f) * lc; for (int c = 0; c < nch; ++c) y[c] *= mg;
            }
            // ---- post clipper (2x oversampled), constant latency ----
            for (int c = 0; c < nch; ++c)
            {
                auto& cc = ch[c]; cc.dClip.write(y[c]); const float od = cc.dClip.read(lat);
                const float sh = cc.osClip.process(y[c], [&](float s) { return clipAmt > 0.f ? softClip(s, ceilLin, clipAmt) : s; });
                y[c] = (clipAmt > 0.f && p.clipPos != 0) ? sh : od;
            }
            // ---- lookahead limiter, dry/wet, output gain ----
            float yl = y[0], yr = nChn ? y[1] : y[0]; look.process(yl, yr, ceilLin); y[0] = yl; y[1] = yr;
            const float latTot = (float) latency();
            for (int c = 0; c < nch; ++c) { const float dry = ch[c].dDry.read(latTot); io[c][i] = (y[c] * mix + dry * (1.f - mix)) * outG; }
        }
        optoGrDb = optoSum / (float) std::max(1, n); liftNow = liftDb; delayLoMs = lastLo; delayMidMs = lastMid;
        for (int k = 0; k < 3; ++k) { bandPeak[k] = gainToDb(pkLv[k]); bandGainDb[k] = gSum[k] / (float) std::max(1, n); }
    }

private:
    struct Chan
    {
        LR lr1, lr2, lrAp; Biquad shelf, subHp, airHp1, airHp2, airShelf; Os2 osAir, osWarm, osClip; Delay dLo, dMid, dHi, dWarm, dClip, dDry; float dcz = 0.f;
    };
    Chan ch[kMaxCh]; Biquad bHp, bLp; Squeezer sq[3]; Opto opto; Lookahead look;
    Ramp pushR, depthR, procR, bassR, airR, warmR, clipR, mixR, outR, ceilR, contourR, optoR, lcR;
    float fs = 44100.f; int rampLen = 882; bool first = true;
    float bassEnv = 0.f, bassDc = 0.f, midEnvDb = -60.f, liftDb = 0.f, pi = 0.f, po = 0.f, gl = 1.f, lastContour = -1.f, lastAir = -1.f;
    float wD = 1.f, wDr = 0.f, wXs = 0.f, wFz = 0.f, wMk = 1.f, aDr = 0.f, aXs = 0.f, aFz = 0.f;
};
} // namespace mx
