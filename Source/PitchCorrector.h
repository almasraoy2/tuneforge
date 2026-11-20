// TuneForge - pitch correction engine (header-only, no JUCE dependency)
//
// Pipeline (per channel):
//   1. YIN pitch detection on a 16 kHz decimated copy, refined at full rate
//   2. Scale quantisation + Retune / Humanize / Flex-Tune / Vibrato logic
//   3. Formant-preserving TD-PSOLA resynthesis (pitch-synchronous grains)
//   Unvoiced sounds (s, t, breaths...) pass through untouched.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace tf
{
constexpr float kFormantMaxSt = 7.0f;
constexpr float kPi = 3.14159265358979323846f;

struct Params
{
    float retuneMs        = 30.f;   // 0 = instant (hard tune) .. 400 = very natural
    float humanize        = 50.f;   // 0..100
    float flex            = 30.f;   // 0..100
    bool  classic         = false;  // false = Modern, true = Classic
    float transposeSt     = 0.f;    // -12..12
    float formantSt       = 0.f;    // -7..7
    bool  vibratoPreserve = false;
    float vibratoCutBoost = 0.f;    // -100..100
    float tuningHz        = 440.f;
    bool  bypass          = false;
    std::array<bool, 12> scale { { true, true, true, true, true, true,
                                   true, true, true, true, true, true } };
};

// std::atomic that can be copied/moved (the copy just reads the current value),
// so the engine stays a normal value type.
template <typename T>
struct AtomicValue : std::atomic<T>
{
    AtomicValue (T v = T()) : std::atomic<T> (v) {}
    AtomicValue (const AtomicValue& o) : std::atomic<T> (o.load()) {}
    AtomicValue& operator= (const AtomicValue& o) { this->store (o.load()); return *this; }
};

class PitchCorrector
{
public:
    PitchCorrector() { buildHann(); }

    // Allocates everything. Call from the non-realtime thread.
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        const int worst = (int) std::ceil (5.5 * sr / 40.0) + 16384;
        ringSize = 1;
        while (ringSize < worst) ringSize <<= 1;
        ringMask = (int64_t) ringSize - 1;

        inBuf.assign ((size_t) ringSize, 0.f);
        ola.assign   ((size_t) ringSize, 0.f);
        wsum.assign  ((size_t) ringSize, 0.f);
        tmpF.assign  ((size_t) (4.0 * sr / 40.0) + 256, 0.f);

        D = std::max (1, (int) std::lround (sr / 16000.0));
        srD = sr / D;
        dec.assign (8192, 0.f);
        decMask = 8191;
        tmp.assign (4096, 0.f);
        yin.assign (2048, 0.f);
        cmnd.assign (2048, 0.f);

        hop = std::max (64, (int) (sr / 190.0));
        bypassCoef = 1.f - std::exp (-1.f / (0.005f * (float) sr));

        configure (80.f, 800.f, 0);
        reset();
    }

    // Safe to call from the audio thread (no allocation). Call reset() afterwards.
    void configure (float minHz_, float maxHz_, int extraLag_)
    {
        minHz = std::clamp (minHz_, 40.f, 400.f);
        maxHz = std::clamp (maxHz_, 200.f, 2000.f);
        tauMax = (int) std::ceil (srD / minHz) + 2;
        tauMin = std::max (2, (int) std::floor (srD / maxHz));
        W = std::max (2 * tauMax, 256);

        Tmax    = (float) (sr / minHz);
        readLag = (int) std::ceil (1.75f * Tmax) + 8 + std::max (0, extraLag_);
        synLag  = (int) std::ceil (Tmax) + 8;
        hwCap   = synLag - 8;
        totalLag = readLag + synLag;
    }

    void reset()
    {
        std::fill (inBuf.begin(), inBuf.end(), 0.f);
        std::fill (ola.begin(), ola.end(), 0.f);
        std::fill (wsum.begin(), wsum.end(), 0.f);
        std::fill (dec.begin(), dec.end(), 0.f);
        n = -1; decN = 0; decAcc = 0.f; decCnt = 0; hopCnt = 0;
        voiced = false; haveM = false; havePeriod = false; ctlInit = false;
        holdAcc = 0; jumpCount = 0;
        lastMarkValid = false; markCount = 0; segStart = 0; Tlast = 0.f;
        synActive = false; cNext = 0.0; curShift = 0.f; corr = 0.f;
        bypassMix = 0.f;
        uiVoiced.store (false);
    }

    int getLatency() const { return totalLag; }

    void setParams (const Params& p)
    {
        prm = p;
        prm.formantSt = std::clamp (p.formantSt, -kFormantMaxSt, kFormantMaxSt);
        formantRatio = std::exp2 (prm.formantSt / 12.f);
    }

    float process (float x)
    {
        ++n;
        inBuf[(size_t) (n & ringMask)] = x;

        decAcc += x;
        if (++decCnt >= D)
        {
            dec[(size_t) (decN & decMask)] = decAcc / (float) D;
            ++decN; decAcc = 0.f; decCnt = 0;
        }

        if (++hopCnt >= hop) { hopCnt = 0; controlStep(); }

        generateMarks();
        synth();

        const int64_t o  = n - totalLag;
        const size_t  io = (size_t) (o & ringMask);
        const float wet = ola[io];
        const float ws  = wsum[io];
        const float dry = inBuf[io];
        ola[io] = 0.f;
        wsum[io] = 0.f;

        const float dm = std::clamp (1.f - ws, 0.f, 1.f);
        float y = wet + dry * dm;

        bypassMix += ((prm.bypass ? 1.f : 0.f) - bypassMix) * bypassCoef;
        y = y * (1.f - bypassMix) + dry * bypassMix;
        return y;
    }

    // ---- values for the UI (written by the audio thread) ----
    AtomicValue<bool>  uiVoiced { false };
    AtomicValue<float> uiMidi   { 0.f };   // detected pitch, fractional MIDI note
    AtomicValue<float> uiTarget { 0.f };   // target MIDI note
    AtomicValue<float> uiCents  { 0.f };   // detected - target, in cents
    AtomicValue<float> uiVib    { 0.f };   // vibrato deviation in semitones
    AtomicValue<float> uiF0     { 0.f };

private:
    // ------------------------------------------------------------------ setup
    void buildHann()
    {
        for (int i = 0; i <= kHannN; ++i)
            hann[(size_t) i] = 0.5f * (1.f + std::cos (kPi * (float) i / (float) kHannN));
    }

    float hannAt (float u) const   // u in 0..1
    {
        const float p = std::min (u, 1.f) * (float) kHannN;
        const int i = (int) p;
        const float f = p - (float) i;
        const float a = hann[(size_t) i];
        const float b = hann[(size_t) std::min (i + 1, kHannN)];
        return a + f * (b - a);
    }

    // -------------------------------------------------------------- detection
    float detect (float& rmsOut)
    {
        const int need = W + tauMax + 1;
        if (decN < need) { rmsOut = 0.f; return 0.f; }

        const int64_t base = decN - need;
        for (int i = 0; i < need; ++i)
            tmp[(size_t) i] = dec[(size_t) ((base + i) & decMask)];

        double e = 0.0;
        for (int i = need - W; i < need; ++i) e += (double) tmp[(size_t) i] * tmp[(size_t) i];
        rmsOut = (float) std::sqrt (e / W);
        if (rmsOut < kGate) return 0.f;

        for (int tau = 1; tau <= tauMax; ++tau)
        {
            const float* a = tmp.data();
            const float* b = tmp.data() + tau;
            float acc = 0.f;
            for (int j = 0; j < W; ++j) { const float d = a[j] - b[j]; acc += d * d; }
            yin[(size_t) tau] = acc;
        }

        float run = yin[1];
        cmnd[1] = 1.f;
        for (int tau = 2; tau <= tauMax; ++tau)
        {
            run += yin[(size_t) tau];
            cmnd[(size_t) tau] = yin[(size_t) tau] * (float) tau / std::max (run, 1e-12f);
        }

        int tau = -1;
        for (int t = tauMin; t < tauMax; ++t)
        {
            if (cmnd[(size_t) t] < 0.15f)
            {
                while (t + 1 < tauMax && cmnd[(size_t) t + 1] < cmnd[(size_t) t]) ++t;
                tau = t;
                break;
            }
        }
        if (tau < 0)
        {
            int gm = tauMin;
            for (int t = tauMin; t < tauMax; ++t)
                if (cmnd[(size_t) t] < cmnd[(size_t) gm]) gm = t;
            if (cmnd[(size_t) gm] < 0.25f) tau = gm; else return 0.f;
        }

        float better = (float) tau;
        if (tau > 1 && tau < tauMax)
        {
            const float a = cmnd[(size_t) tau - 1], b = cmnd[(size_t) tau], c = cmnd[(size_t) tau + 1];
            const float den = a - 2.f * b + c;
            if (std::abs (den) > 1e-9f) better += 0.5f * (a - c) / den;
        }
        float T = better * (float) D;

        // refine at full sample rate
        if (D > 1)
        {
            const int c0 = (int) std::lround (T);
            const int lo = std::max (8, c0 - D - 1);
            const int hi = c0 + D + 1;
            const int Wf = std::min (2 * c0, (int) (2.0f * Tmax));
            if (n > Wf + hi + 4 && (size_t) (Wf + hi + 1) < tmpF.size())
            {
                const int64_t a0 = n - (Wf + hi);
                for (int i = 0; i <= Wf + hi; ++i)
                    tmpF[(size_t) i] = inBuf[(size_t) ((a0 + i) & ringMask)];

                float vals[64] = {};
                const int cnt = std::min (hi - lo + 1, 64);
                int bi = 0;
                for (int k = 0; k < cnt; ++k)
                {
                    const int t = lo + k;
                    const float* a = tmpF.data();
                    const float* b = tmpF.data() + t;
                    float num = 0.f, en = 1e-9f;
                    for (int j = 0; j < Wf; ++j)
                    {
                        const float d = a[j] - b[j];
                        num += d * d;
                        en += a[j] * a[j] + b[j] * b[j];
                    }
                    vals[k] = num / en;
                    if (vals[k] < vals[bi]) bi = k;
                }
                float tf_ = (float) (lo + bi);
                if (bi > 0 && bi < cnt - 1)
                {
                    const float a = vals[bi - 1], b = vals[bi], c = vals[bi + 1];
                    const float den = a - 2.f * b + c;
                    if (std::abs (den) > 1e-12f) tf_ += 0.5f * (a - c) / den;
                }
                T = tf_;
            }
        }

        return (float) (sr / std::max (T, 2.f));
    }

    // ---------------------------------------------------------------- control
    int nearestAllowed (float x) const
    {
        const int c = (int) std::floor (x);
        int best = -1;
        float bestD = 1e9f;
        for (int i = c - 7; i <= c + 8; ++i)
        {
            const int pc = ((i % 12) + 12) % 12;
            if (!prm.scale[(size_t) pc]) continue;
            const float d = std::abs (x - (float) i);
            if (d < bestD) { bestD = d; best = i; }
        }
        return best >= 0 ? best : (int) std::lround (x);
    }

    void controlStep()
    {
        float rms = 0.f;
        const float f0 = detect (rms);
        const float dt = (float) hop / (float) sr;
        const float holdMax = 0.030f * (float) sr;

        bool accepted = false;
        if (f0 > 0.f)
        {
            const float m = 69.f + 12.f * std::log2 (f0 / prm.tuningHz);
            bool ok = true;
            if (haveM && std::abs (m - lastM) > 2.5f)
            {
                if (++jumpCount < 3) ok = false;
            }
            if (ok)
            {
                jumpCount = 0;
                lastM = m; haveM = true;
                voiced = true; holdAcc = 0;
                const float T = (float) (sr / f0);
                Tlast = T;
                Tcur = havePeriod ? Tcur + 0.5f * (T - Tcur) : T;
                havePeriod = true;
                updateControl (m, dt, f0);
                accepted = true;
            }
        }

        if (!accepted)
        {
            holdAcc += (float) hop;
            if (holdAcc > holdMax)
            {
                voiced = false; haveM = false; havePeriod = false; ctlInit = false;
                jumpCount = 0;
                uiVoiced.store (false);
            }
        }
    }

    void updateControl (float m, float dt, float f0)
    {
        if (!ctlInit)
        {
            slow = m; corr = 0.f; target = nearestAllowed (m); noteAge = 0.f; ctlInit = true;
        }

        slow += (1.f - std::exp (-2.f * kPi * 2.0f * dt)) * (m - slow);
        const float vib = m - slow;
        const float base = prm.vibratoPreserve ? slow : m;

        const int cand = nearestAllowed (base);
        if (cand != target && std::abs (base - (float) target) > std::abs (base - (float) cand) + 0.15f)
        {
            target = cand;
            noteAge = 0.f;
        }
        noteAge += dt;

        const float zone = prm.classic ? 0.f : prm.flex / 100.f * 0.5f;
        const float dev = base - (float) target;
        const float desired = (dev - std::clamp (dev, -zone, zone)) * -1.f;   // correction wanted

        float tau = prm.retuneMs / 1000.f;
        if (!prm.classic)
            tau *= 1.f + (prm.humanize / 100.f) * 4.f * std::clamp (noteAge / 0.4f, 0.f, 1.f);

        if (tau < 1e-4f)
            corr = desired - 0.f;
        else if (prm.classic)
        {
            const float step = dt / tau;   // semitones per hop (1 st per tau)
            corr += std::clamp (desired - corr, -step, step);
        }
        else
            corr += (1.f - std::exp (-dt / tau)) * (desired - corr);

        // correction is defined against `base`; when the target is quantised from the
        // smoothed pitch this still lands the full pitch (slow + vib) on the target.
        float shift = corr;
        if (prm.vibratoPreserve)
            shift += (prm.vibratoCutBoost / 100.f) * vib;
        shift += prm.transposeSt;
        curShift = std::clamp (shift, -16.f, 16.f);

        uiVoiced.store (true);
        uiMidi.store (m);
        uiTarget.store ((float) target);
        uiCents.store ((m - (float) target) * 100.f);
        uiVib.store (vib);
        uiF0.store (f0);
    }

    // ------------------------------------------------------------ pitch marks
    float smoothAt (int64_t i) const
    {
        return 0.25f * (inBuf[(size_t) ((i - 1) & ringMask)]
                        + 2.f * inBuf[(size_t) (i & ringMask)]
                        + inBuf[(size_t) ((i + 1) & ringMask)]);
    }

    // Every mark carries the period estimate and the pitch shift that were computed from the
    // SAME measurement. Output period = period / 2^(shift/12), so the result lands exactly on
    // the target pitch no matter how old the measurement is.
    void addMark (int64_t pos)
    {
        markPos  [(size_t) (markCount & 511)] = pos;
        markPer  [(size_t) (markCount & 511)] = Tlast;
        markShift[(size_t) (markCount & 511)] = curShift;
        ++markCount;
        lastMarkPos = pos;
    }

    void generateMarks()
    {
        if (!voiced || !havePeriod) { lastMarkValid = false; return; }
        const float T = Tcur;

        for (int guard = 0; guard < 4; ++guard)
        {
            if (!lastMarkValid)
            {
                const int64_t lo = n - (int64_t) (2.2f * T);
                const int64_t hi = n - (int64_t) (1.2f * T);
                if (lo < 2) return;

                float mx = 0.f;
                for (int64_t i = lo; i <= hi; ++i)
                {
                    const float v = std::abs (smoothAt (i));
                    if (v > mx) mx = v;
                }
                if (mx < 1e-6f) return;

                // polarity of the strongest peak in the window
                float bestSigned = 0.f;
                int64_t bestPos = lo;
                for (int64_t i = lo; i <= hi; ++i)
                {
                    const float v = smoothAt (i);
                    if (std::abs (v) > std::abs (bestSigned)) { bestSigned = v; bestPos = i; }
                }
                polarity = bestSigned >= 0.f ? 1.f : -1.f;

                addMark (bestPos);
                lastMarkValid = true;
                segStart = markCount - 1;
            }
            else
            {
                const int64_t cand = lastMarkPos + (int64_t) std::lround (T);
                const int64_t half = std::max<int64_t> (2, (int64_t) (0.25f * T));
                if (cand + half + 1 > n) return;

                float best = -1e30f;
                int64_t bp = cand;
                for (int64_t i = cand - half; i <= cand + half; ++i)
                {
                    const float v = polarity * smoothAt (i);
                    if (v > best) { best = v; bp = i; }
                }
                if (bp <= lastMarkPos + (int64_t) (0.5f * T)) bp = cand;
                addMark (bp);
            }
        }
    }

    // -------------------------------------------------------------- synthesis
    float readInput (double pos) const
    {
        const int64_t i0 = (int64_t) std::floor (pos);
        if (i0 + 1 > n || i0 < n - (int64_t) ringSize + 16) return 0.f;
        const float f = (float) (pos - (double) i0);
        const float a = inBuf[(size_t) (i0 & ringMask)];
        const float b = inBuf[(size_t) ((i0 + 1) & ringMask)];
        return a + f * (b - a);
    }

    void synth()
    {
        const int64_t sPos = n - readLag;
        if (!synActive)
        {
            if (voiced && lastMarkValid && markCount > segStart)
            {
                cNext = std::max ((double) markPos[(size_t) (segStart & 511)], (double) sPos);
                synActive = true;
            }
            else
                return;
        }
        for (int guard = 0; guard < 8 && synActive && cNext <= (double) sPos; ++guard)
            placeGrain (n - totalLag);
    }

    void placeGrain (int64_t oMin)
    {
        int64_t best = -1;
        double bestD = 1e30;
        const int64_t lowK = std::max<int64_t> (0, markCount - 200);
        for (int64_t k = markCount - 1; k >= lowK; --k)
        {
            const double pos = (double) markPos[(size_t) (k & 511)];
            const double d = std::abs (pos - cNext);
            if (d < bestD) { bestD = d; best = k; }
            else if (pos < cNext) break;
        }

        if (best < 0) { synActive = false; return; }
        const float Tm = markPer[(size_t) (best & 511)];
        if (bestD > 1.25 * (double) Tm) { synActive = false; return; }

        const float r = std::exp2 (markShift[(size_t) (best & 511)] / 12.f);
        const float Tout = std::max (Tm / r, 20.f);
        const int hw = std::max (4, std::min ((int) std::ceil (std::max (Tm, Tout)), hwCap));
        const float gain = Tout / (float) hw;
        const double pm = (double) markPos[(size_t) (best & 511)];
        const int64_t c = (int64_t) std::llround (cNext);

        for (int k = -hw; k <= hw; ++k)
        {
            const int64_t idx = c + k;
            if (idx < oMin) continue;
            const float w = hannAt ((float) std::abs (k) / (float) hw) * gain;
            const float v = readInput (pm + (double) k * (double) formantRatio);
            ola [(size_t) (idx & ringMask)] += v * w;
            wsum[(size_t) (idx & ringMask)] += w;
        }
        cNext += (double) Tout;
    }

    // ------------------------------------------------------------------ state
    static constexpr float kGate = 0.002f;
    static constexpr int   kHannN = 1024;

    Params prm;
    double sr = 44100.0, srD = 16000.0;
    int D = 3, hop = 232, tauMax = 202, tauMin = 20, W = 404;
    float minHz = 80.f, maxHz = 800.f, Tmax = 551.f;
    int readLag = 0, synLag = 0, hwCap = 0, totalLag = 0;
    float formantRatio = 1.f, bypassMix = 0.f, bypassCoef = 0.01f;

    std::vector<float> inBuf, ola, wsum, tmpF, dec, tmp, yin, cmnd;
    int64_t ringSize = 0, ringMask = 0;
    int decMask = 8191;
    int64_t n = -1, decN = 0;
    float decAcc = 0.f;
    int decCnt = 0, hopCnt = 0;
    std::array<float, kHannN + 1> hann {};

    // control state
    bool voiced = false, haveM = false, havePeriod = false, ctlInit = false;
    float holdAcc = 0.f, lastM = 0.f, Tcur = 0.f, slow = 0.f, corr = 0.f, noteAge = 0.f, curShift = 0.f;
    int jumpCount = 0, target = 60;

    float Tlast = 0.f;

    // pitch marks
    std::array<int64_t, 512> markPos {};
    std::array<float, 512> markPer {};
    std::array<float, 512> markShift {};
    int64_t markCount = 0, segStart = 0, lastMarkPos = 0;
    bool lastMarkValid = false;
    float polarity = 1.f;

    // synthesis
    bool synActive = false;
    double cNext = 0.0;
};
} // namespace tf
