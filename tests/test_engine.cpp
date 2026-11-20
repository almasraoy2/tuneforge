// Offline tests for the TuneForge DSP engine.
// Build: g++ -O2 -std=c++17 -I../Source test_engine.cpp -o test_engine && ./test_engine
#include "PitchCorrector.h"

#include <cstdio>
#include <cstdlib>
#include <random>

static const double SR = 48000.0;

// Voice-like test signal: band-limited sawtooth through two resonators (formants).
struct VoiceSynth
{
    double phase = 0.0;
    double y1[2] = { 0, 0 }, y2[2] = { 0, 0 };
    double b1[2], b2[2], g[2];

    VoiceSynth()
    {
        const double fc[2] = { 700.0, 1200.0 }, bw[2] = { 90.0, 110.0 };
        for (int i = 0; i < 2; ++i)
        {
            const double r = std::exp (-tf::kPi * bw[i] / SR);
            b1[i] = 2.0 * r * std::cos (2.0 * tf::kPi * fc[i] / SR);
            b2[i] = -r * r;
            g[i]  = 1.0 - r;
        }
    }

    float next (double f0)
    {
        phase += f0 / SR;
        if (phase >= 1.0) phase -= 1.0;
        const double src = (phase * 2.0 - 1.0);
        double out = 0.0;
        for (int i = 0; i < 2; ++i)
        {
            const double y = g[i] * src + b1[i] * y1[i] + b2[i] * y2[i];
            y2[i] = y1[i]; y1[i] = y;
            out += y;
        }
        return (float) (out * 1.2);
    }
};

// Independent pitch estimator (long-window autocorrelation, full rate)
static double estimateF0 (const std::vector<float>& x, size_t start, size_t len, double lo, double hi)
{
    const int tmin = (int) (SR / hi), tmax = (int) (SR / lo);
    double bestVal = 1e30; int bestT = tmin;
    std::vector<double> d ((size_t) tmax + 2, 0.0);
    for (int t = tmin; t <= tmax; ++t)
    {
        double num = 0, en = 1e-12;
        for (size_t j = 0; j < len; ++j)
        {
            const double a = x[start + j], b = x[start + j + (size_t) t];
            num += (a - b) * (a - b); en += a * a + b * b;
        }
        d[(size_t) t] = num / en;
        if (d[(size_t) t] < bestVal) { bestVal = d[(size_t) t]; bestT = t; }
    }
    // prefer the first (shortest-period) local minimum that is nearly as good as the best
    // one, so that multiples of the period (octave-down errors) are not chosen
    for (int t = tmin + 1; t < tmax; ++t)
    {
        if (d[(size_t) t] <= bestVal * 2.0 + 0.004 && d[(size_t) t] <= d[(size_t) t - 1] && d[(size_t) t] <= d[(size_t) t + 1])
        {
            bestT = t;
            break;
        }
    }
    double T = bestT;
    if (bestT > tmin && bestT < tmax)
    {
        const double a = d[(size_t) bestT - 1], b = d[(size_t) bestT], c = d[(size_t) bestT + 1];
        const double den = a - 2 * b + c;
        if (std::abs (den) > 1e-15) T += 0.5 * (a - c) / den;
    }
    return SR / T;
}

static double cents (double f, double ref) { return 1200.0 * std::log2 (f / ref); }

static int failures = 0;
static void check (bool ok, const char* name, double value, const char* unit)
{
    std::printf ("  [%s] %-52s %8.2f %s\n", ok ? "PASS" : "FAIL", name, value, unit);
    if (! ok) ++failures;
}

static std::vector<float> run (tf::PitchCorrector& pc, const std::vector<float>& in)
{
    std::vector<float> out (in.size());
    for (size_t i = 0; i < in.size(); ++i) out[i] = pc.process (in[i]);
    return out;
}

static tf::PitchCorrector makeEngine (const tf::Params& p, float minHz = 80.f, float maxHz = 800.f, int extra = 0)
{
    tf::PitchCorrector pc;
    pc.prepare (SR);
    pc.configure (minHz, maxHz, extra);
    pc.reset();
    pc.setParams (p);
    return pc;
}

int main()
{
    std::printf ("TuneForge engine tests @ %.0f Hz\n", SR);
    const size_t N = (size_t) (SR * 2.5);

    {   // 1. Hard tune: A3 sung 30 cents flat must land on 220 Hz
        std::printf ("Test 1: hard tune, chromatic\n");
        tf::Params p; p.retuneMs = 0.f; p.humanize = 0.f; p.flex = 0.f;
        auto pc = makeEngine (p);
        VoiceSynth v; std::vector<float> in (N);
        const double f = 220.0 * std::pow (2.0, -30.0 / 1200.0);
        for (auto& s : in) s = 0.5f * v.next (f);
        auto out = run (pc, in);
        const int L = pc.getLatency();
        const double fin  = estimateF0 (in,  (size_t) (SR * 1.5), 4096, 100, 500);
        const double fout = estimateF0 (out, (size_t) (SR * 1.5) + (size_t) L, 4096, 100, 500);
        check (std::abs (cents (fin, 220.0) + 30.0) < 3.0, "input is ~30 cents flat", cents (fin, 220.0), "cents");
        check (std::abs (cents (fout, 220.0)) < 6.0, "output within 6 cents of A3 (220 Hz)", cents (fout, 220.0), "cents");
    }

    {   // 2. Scale restriction: C major, input between C#4 and D4 -> D4
        std::printf ("Test 2: C major scale mask\n");
        tf::Params p; p.retuneMs = 0.f; p.humanize = 0.f; p.flex = 0.f;
        const int major[7] = { 0, 2, 4, 5, 7, 9, 11 };
        p.scale.fill (false);
        for (int d : major) p.scale[(size_t) d] = true;
        auto pc = makeEngine (p);
        VoiceSynth v; std::vector<float> in (N);
        const double d4 = 440.0 * std::pow (2.0, -7.0 / 12.0);    // D4
        const double f = d4 * std::pow (2.0, -40.0 / 1200.0);     // 40 cents flat (closer to C#4, which is not allowed)
        for (auto& s : in) s = 0.5f * v.next (f);
        auto out = run (pc, in);
        const double fout = estimateF0 (out, (size_t) (SR * 1.5) + (size_t) pc.getLatency(), 4096, 100, 600);
        check (std::abs (cents (fout, d4)) < 6.0, "C#4-ish input snaps to D4 (C# disabled)", cents (fout, d4), "cents");
    }

    {   // 3. Slow retune keeps natural pitch
        std::printf ("Test 3: slow retune leaves pitch mostly natural\n");
        tf::Params p; p.retuneMs = 400.f; p.humanize = 100.f; p.flex = 100.f;
        auto pc = makeEngine (p);
        VoiceSynth v; std::vector<float> in (N);
        const double f = 220.0 * std::pow (2.0, 25.0 / 1200.0);
        for (auto& s : in) s = 0.5f * v.next (f);
        auto out = run (pc, in);
        const double fout = estimateF0 (out, (size_t) (SR * 1.0) + (size_t) pc.getLatency(), 4096, 100, 500);
        check (std::abs (cents (fout, f)) < 8.0, "25 cents sharp stays within 8 cents of input", cents (fout, f), "cents");
    }

    {   // 4. Vibrato: preserve on keeps it, preserve off (hard tune) flattens it
        // The centre pitch can be overridden with TF_TEST_F0 (must be an exact note, e.g. 130.8128 or 440)
        const char* envF0 = std::getenv ("TF_TEST_F0");
        const double centre = envF0 ? std::atof (envF0) : 220.0;
        std::printf ("Test 4: vibrato preserve (centre %.2f Hz)\n", centre);
        for (int preserve = 0; preserve < 2; ++preserve)
        {
            tf::Params p; p.retuneMs = 0.f; p.humanize = 0.f; p.flex = 0.f; p.vibratoPreserve = preserve == 1;
            auto pc = makeEngine (p);
            VoiceSynth v; std::vector<float> in (N);
            for (size_t i = 0; i < N; ++i)
            {
                const double t = (double) i / SR;
                const double f = centre * std::pow (2.0, (40.0 * std::sin (2.0 * tf::kPi * 5.5 * t)) / 1200.0);
                in[i] = 0.5f * v.next (f);
            }
            auto out = run (pc, in);
            double mn = 1e9, mx = -1e9;
            for (size_t s = (size_t) (SR * 1.0); s + 2048 + 2048 < N - (size_t) pc.getLatency(); s += 512)
            {
                const double f = estimateF0 (out, s + (size_t) pc.getLatency(), 2048, centre * 0.6, centre * 1.6);
                const double c = cents (f, centre);
                mn = std::min (mn, c); mx = std::max (mx, c);
            }
            const double swing = mx - mn;
            if (preserve) check (swing > 25.0, "vibrato preserved (peak-to-peak > 25 cents)", swing, "cents p-p");
            else          check (swing < 20.0, "vibrato flattened (peak-to-peak < 20 cents)", swing, "cents p-p");
        }
    }

    {   // 5. Transpose +12 st doubles the pitch
        std::printf ("Test 5: transpose +12 semitones\n");
        tf::Params p; p.retuneMs = 400.f; p.humanize = 0.f; p.flex = 100.f; p.transposeSt = 12.f;
        auto pc = makeEngine (p);
        VoiceSynth v; std::vector<float> in (N);
        for (auto& s : in) s = 0.5f * v.next (200.0);
        auto out = run (pc, in);
        const double fout = estimateF0 (out, (size_t) (SR * 1.5) + (size_t) pc.getLatency(), 4096, 150, 900);
        check (std::abs (cents (fout, 400.0)) < 15.0, "200 Hz -> ~400 Hz", cents (fout, 400.0), "cents");
    }

    {   // 6. Formant shift leaves pitch unchanged
        std::printf ("Test 6: formant shift keeps pitch\n");
        tf::Params p; p.retuneMs = 400.f; p.humanize = 0.f; p.flex = 100.f; p.formantSt = 5.f;
        auto pc = makeEngine (p);
        VoiceSynth v; std::vector<float> in (N);
        for (auto& s : in) s = 0.5f * v.next (200.0);
        auto out = run (pc, in);
        const double fout = estimateF0 (out, (size_t) (SR * 1.5) + (size_t) pc.getLatency(), 4096, 100, 500);
        check (std::abs (cents (fout, 200.0)) < 15.0, "pitch stays at 200 Hz with +5 st formant", cents (fout, 200.0), "cents");
    }

    {   // 7. Level: voiced output should be about as loud as input
        std::printf ("Test 7: level stays consistent\n");
        tf::Params p; p.retuneMs = 0.f; p.humanize = 0.f; p.flex = 0.f;
        auto pc = makeEngine (p);
        VoiceSynth v; std::vector<float> in (N);
        for (auto& s : in) s = 0.5f * v.next (233.0);
        auto out = run (pc, in);
        double ei = 0, eo = 0; size_t a = (size_t) (SR * 1.5);
        for (size_t i = a; i < a + 16384; ++i) { ei += in[i] * in[i]; eo += out[i + (size_t) pc.getLatency()] * out[i + (size_t) pc.getLatency()]; }
        const double db = 10.0 * std::log10 (eo / ei);
        check (std::abs (db) < 1.5, "output level within 1.5 dB of input", db, "dB");
    }

    {   // 8. Noise / silence: no NaNs, unvoiced passes through at the same level
        std::printf ("Test 8: noise passthrough and silence\n");
        tf::Params p;
        auto pc = makeEngine (p);
        std::mt19937 rng (1);
        std::normal_distribution<float> nd (0.f, 0.1f);
        std::vector<float> in (N);
        for (auto& s : in) s = nd (rng);
        auto out = run (pc, in);
        bool finite = true;
        for (float s : out) if (! std::isfinite (s)) finite = false;
        check (finite, "all output samples finite", finite ? 1 : 0, "");
        double ei = 0, eo = 0;
        for (size_t i = (size_t) SR; i < N - (size_t) pc.getLatency(); ++i) { ei += in[i] * in[i]; eo += out[i + (size_t) pc.getLatency()] * out[i + (size_t) pc.getLatency()]; }
        check (std::abs (10.0 * std::log10 (eo / ei)) < 1.0, "noise level preserved", 10.0 * std::log10 (eo / ei), "dB");
        std::vector<float> sil (N, 0.f);
        auto pc2 = makeEngine (p);
        auto o2 = run (pc2, sil);
        double mx = 0; for (float s : o2) mx = std::max (mx, (double) std::abs (s));
        check (mx == 0.0, "silence stays silent", mx, "peak");
    }

    {   // 9. Latency report
        tf::Params p;
        auto a = makeEngine (p, 80.f, 800.f, 0);
        auto b = makeEngine (p, 140.f, 1400.f, 0);
        std::printf ("Latency: Alto/Tenor %.1f ms, Soprano %.1f ms\n",
                     1000.0 * a.getLatency() / SR, 1000.0 * b.getLatency() / SR);
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
