// drum_test.cpp - offline DSP harness for ResoBody.
//
// SPDX-License-Identifier: MIT
//
// WHY THIS EXISTS
//   tools/host-sim checks the things the MPC's host does: the load sequence, the VST2
//   ABI, state chunks, in-place processing, block sizes. It feeds effects a sine plus a
//   short noise burst, and it has no way to pin a parameter at a chosen value. Neither
//   of those is enough for this plugin: the whole point of ResoBody is how it behaves on
//   drum transients with RESO and AUTO pushed to the top at the same time.
//
//   So this harness synthesises a drum loop (kick / snare / hat, 2 bars at 120 bpm plus
//   a silent tail) and runs it through the plugin at deliberately hostile settings,
//   driving the class through the same public entry points the framework's dispatcher
//   uses (internalSetSampleRate / internalResume / internalProcess), with the input and
//   output buffers aliased exactly as the MPC does it.
//
//   It is NOT part of the plugin: build.py only compiles src/*.cpp, and package.py only
//   ships the build output, so this file never reaches the MPC.
//
// BUILD AND RUN (from the SDK root)
//   .sdk-tools/zigenv/bin/python -m ziglang c++ -std=c++17 -O2 \
//       -I framework -I plugins/ResoBody/src \
//       -o /tmp/drum_test plugins/ResoBody/test/drum_test.cpp && /tmp/drum_test /tmp/out
//   (any C++17 compiler works: c++ -std=c++17 -O2 -I framework -I plugins/ResoBody/src ...)
//
//   With a directory argument it also writes a WAV per scenario so you can listen.
//   Exit code 0 = every scenario passed, 1 = at least one failure.

#include "ResoBody.cpp"   // the plugin under test, and the framework with it

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr double kSr = 44100.0;

// The block sizes the MPC lets the user choose (Preferences). We cycle through them
// inside one render so the plugin never sees a constant frame count.
const int kBlockSizes[] = { 128, 32, 256, 64, 512, 96, 192 };
constexpr int kNumBlockSizes = (int)(sizeof(kBlockSizes) / sizeof(kBlockSizes[0]));

// ---------------------------------------------------------------------------
// A drum loop to hit the resonator with: sharp transients, strong low end, and
// a silent tail so we can watch the body ring out.
// ---------------------------------------------------------------------------
struct DrumLoop
{
    std::vector<float> left, right;

    static float noise(uint32_t& s)
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return (float)(s >> 8) * (2.0f / 16777216.0f) - 1.0f;
    }

    void addKick(size_t at, float gain)
    {
        const int n = (int)(0.40 * kSr);
        float phase = 0.0f;
        for (int i = 0; i < n && at + (size_t)i < left.size(); ++i)
        {
            const float t = (float)i / (float)kSr;
            const float hz = 48.0f + 102.0f * std::exp(-t / 0.022f);      // 150 Hz -> 48 Hz
            const float amp = std::exp(-t / 0.085f) * gain;
            phase += hz / (float)kSr;
            if (phase >= 1.0f) phase -= 1.0f;
            const float click = i < 48 ? 0.35f * (1.0f - (float)i / 48.0f) : 0.0f;
            const float v = std::sin(6.2831853f * phase) * amp + click * gain;
            left[at + (size_t)i] += v;
            right[at + (size_t)i] += v;
        }
    }

    void addSnare(size_t at, float gain)
    {
        const int n = (int)(0.25 * kSr);
        uint32_t s = 0x9E3779B9u + (uint32_t)at;
        float phase = 0.0f, hp = 0.0f, prev = 0.0f;
        for (int i = 0; i < n && at + (size_t)i < left.size(); ++i)
        {
            const float t = (float)i / (float)kSr;
            phase += 190.0f / (float)kSr;
            if (phase >= 1.0f) phase -= 1.0f;
            const float body = std::sin(6.2831853f * phase) * std::exp(-t / 0.055f) * 0.5f;
            const float nz = noise(s);
            hp = 0.72f * (hp + nz - prev);                                  // crude high pass
            prev = nz;
            const float v = (body + hp * std::exp(-t / 0.075f) * 0.8f) * gain;
            left[at + (size_t)i] += v * 0.95f;
            right[at + (size_t)i] += v;
        }
    }

    void addHat(size_t at, float gain, bool open)
    {
        const int n = (int)((open ? 0.16 : 0.050) * kSr);
        uint32_t s = 0x1234567u + (uint32_t)at * 7919u;
        float hp = 0.0f, prev = 0.0f;
        for (int i = 0; i < n && at + (size_t)i < left.size(); ++i)
        {
            const float t = (float)i / (float)kSr;
            const float nz = noise(s);
            hp = 0.88f * (hp + nz - prev);
            prev = nz;
            const float v = hp * std::exp(-t / (open ? 0.060f : 0.016f)) * gain;
            left[at + (size_t)i] += v * 1.0f;                               // slightly off-centre
            right[at + (size_t)i] += v * 0.75f;
        }
    }

    // 2 bars of 4/4 at 120 bpm (eighth notes) plus 1.5 s of silence for the tail.
    void build(float peakTarget)
    {
        const double eighth = 0.25;                 // seconds, 120 bpm
        const size_t frames = (size_t)((16 * eighth + 1.5) * kSr);
        left.assign(frames, 0.0f);
        right.assign(frames, 0.0f);

        auto at = [&](int step) { return (size_t)(step * eighth * kSr); };
        const int kicks[]  = { 0, 3, 8, 10, 14 };
        const int snares[] = { 4, 12 };
        for (int k : kicks)  addKick(at(k), 0.95f);
        for (int k : snares) addSnare(at(k), 0.75f);
        for (int step = 0; step < 16; ++step)
            addHat(at(step), step % 2 == 0 ? 0.30f : 0.18f, step == 7 || step == 15);

        float peak = 0.0f;
        for (size_t i = 0; i < frames; ++i)
        {
            peak = std::fmax(peak, std::fabs(left[i]));
            peak = std::fmax(peak, std::fabs(right[i]));
        }
        if (peak > 0.0f)
        {
            const float g = peakTarget / peak;
            for (size_t i = 0; i < frames; ++i) { left[i] *= g; right[i] *= g; }
        }
    }
};

// ---------------------------------------------------------------------------
// Measurements taken on a rendered buffer.
// ---------------------------------------------------------------------------
struct Stats
{
    int badSamples = 0;        // NaN or Inf
    int denormals = 0;         // |x| < 1e-30 and not zero
    int clipped = 0;           // |x| > 1.0
    float peak = 0.0f;
    double sumSq = 0.0;
    double sum = 0.0;          // for DC offset
    size_t count = 0;
    float tailPeak = 0.0f;     // peak in the last 0.25 s (should be near silence)

    void accumulate(const float* x, size_t n, bool inTail)
    {
        for (size_t i = 0; i < n; ++i)
        {
            const float v = x[i];
            if (!std::isfinite(v)) { ++badSamples; continue; }
            const float a = std::fabs(v);
            if (a > 1.0f) ++clipped;
            if (a != 0.0f && a < 1.0e-30f) ++denormals;
            peak = std::fmax(peak, a);
            if (inTail) tailPeak = std::fmax(tailPeak, a);
            sumSq += (double)v * v;
            sum += v;
            ++count;
        }
    }
    double rmsDb() const { return count ? 20.0 * std::log10(std::sqrt(sumSq / (double)count) + 1e-12) : -200.0; }
    double peakDb() const { return 20.0 * std::log10((double)peak + 1e-12); }
    double dcOffset() const { return count ? sum / (double)count : 0.0; }
    double tailDb() const { return 20.0 * std::log10((double)tailPeak + 1e-12); }
};

// ---------------------------------------------------------------------------
// One scenario: parameter values (plain, by index) and what we expect.
// ---------------------------------------------------------------------------
struct Scenario
{
    const char* name;
    float out, mix, scale, reso, autoAmt, atk, rel;
    int type;
    float inputPeak;    // how hot the drum loop is driven
};

struct Result
{
    Stats stats;
    double avgCpuPercent = 0.0;
    double worstCpuPercent = 0.0;
};

void writeWav(const std::string& path, const std::vector<float>& l, const std::vector<float>& r)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) return;
    const uint32_t frames = (uint32_t)l.size();
    const uint32_t dataBytes = frames * 2u * 2u;
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); u32(36 + dataBytes); std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f); u32(16); u16(1); u16(2);
    u32((uint32_t)kSr); u32((uint32_t)kSr * 4); u16(4); u16(16);
    std::fwrite("data", 1, 4, f); u32(dataBytes);
    for (uint32_t i = 0; i < frames; ++i)
        for (int c = 0; c < 2; ++c)
        {
            float v = c == 0 ? l[i] : r[i];
            if (!std::isfinite(v)) v = 0.0f;
            v = v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);
            u16((uint16_t)(int16_t)std::lround(v * 32767.0f));
        }
    std::fclose(f);
}

Result run(const Scenario& sc, const std::string& wavDir, bool slamParameters = false)
{
    DrumLoop loop;
    loop.build(sc.inputPeak);

    ResoBody plugin;
    plugin.set(ResoBody::kOutput,  sc.out);
    plugin.set(ResoBody::kMix,     sc.mix);
    plugin.set(ResoBody::kScale,   sc.scale);
    plugin.set(ResoBody::kReso,    sc.reso);
    plugin.set(ResoBody::kAuto,    sc.autoAmt);
    plugin.set(ResoBody::kAttack,  sc.atk);
    plugin.set(ResoBody::kRelease, sc.rel);
    plugin.set(ResoBody::kType,    (float)sc.type);

    plugin.internalSetSampleRate(kSr);
    plugin.internalSetBlockSize(512);      // announce the largest, deliver smaller
    plugin.internalResume();               // prepare() + reset(), like effMainsChanged(1)

    // In place, exactly like the MPC: one buffer per channel, used for input and output.
    std::vector<float> L = loop.left, R = loop.right;
    const size_t frames = L.size();
    const size_t tailStart = frames > (size_t)(0.25 * kSr) ? frames - (size_t)(0.25 * kSr) : 0;

    Result res;
    double totalSeconds = 0.0, totalBudget = 0.0;
    uint32_t rng = 0xBEEF1234u;
    size_t pos = 0;
    int bi = 0;

    while (pos < frames)
    {
        int n = kBlockSizes[bi++ % kNumBlockSizes];
        if (pos + (size_t)n > frames) n = (int)(frames - pos);

        if (slamParameters)
        {
            // Jump every parameter to a random value between blocks: the UI thread can
            // write parameters at any time, and nothing may click, blow up or go NaN.
            auto rnd = [&]() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (float)(rng >> 8) / 16777216.0f; };
            for (int p = 0; p < plugin.numParams(); ++p) plugin.setNormalised(p, rnd());
        }

        float* ins[2]  = { L.data() + pos, R.data() + pos };
        float* outs[2] = { L.data() + pos, R.data() + pos };

        const auto t0 = std::chrono::steady_clock::now();
        plugin.internalProcess(ins, outs, n);
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

        const double budget = (double)n / kSr;
        totalSeconds += elapsed;
        totalBudget += budget;
        res.worstCpuPercent = std::fmax(res.worstCpuPercent, 100.0 * elapsed / budget);

        res.stats.accumulate(L.data() + pos, (size_t)n, pos >= tailStart);
        res.stats.accumulate(R.data() + pos, (size_t)n, pos >= tailStart);
        pos += (size_t)n;
    }

    res.avgCpuPercent = totalBudget > 0.0 ? 100.0 * totalSeconds / totalBudget : 0.0;

    if (!wavDir.empty())
    {
        std::string safe(sc.name);
        for (char& c : safe) if (c == ' ' || c == '/' || c == '+') c = '_';
        writeWav(wavDir + "/" + safe + ".wav", L, R);
    }
    return res;
}

int failures = 0;

void report(const char* label, const Result& r, bool expectClipping, bool expectQuietTail)
{
    const Stats& s = r.stats;
    bool ok = true;
    char notes[512];
    notes[0] = '\0';

    if (s.badSamples > 0) { ok = false; std::snprintf(notes, sizeof(notes), " <- %d NaN/Inf samples", s.badSamples); }
    if (s.denormals > 0)  { ok = false; std::snprintf(notes + std::strlen(notes), sizeof(notes) - std::strlen(notes), " <- %d denormals", s.denormals); }
    if (!expectClipping && s.clipped > 0)
    { ok = false; std::snprintf(notes + std::strlen(notes), sizeof(notes) - std::strlen(notes), " <- %d samples over 0 dBFS", s.clipped); }
    if (std::fabs(s.dcOffset()) > 0.002)
    { ok = false; std::snprintf(notes + std::strlen(notes), sizeof(notes) - std::strlen(notes), " <- DC offset %.5f", s.dcOffset()); }
    if (expectQuietTail && s.tailDb() > -45.0)
    { ok = false; std::snprintf(notes + std::strlen(notes), sizeof(notes) - std::strlen(notes), " <- tail still at %.1f dBFS (ringing forever?)", s.tailDb()); }

    if (!ok) ++failures;
    std::printf("%-4s %-26s peak %7.2f dBFS  rms %7.2f dBFS  tail %7.1f dBFS  dc %+.5f  cpu %.2f%% (worst %.2f%%)%s\n",
                ok ? "PASS" : "FAIL", label, s.peakDb(), s.rmsDb(), s.tailDb(), s.dcOffset(),
                r.avgCpuPercent, r.worstCpuPercent, notes);
}

} // namespace

int main(int argc, char** argv)
{
    const std::string wavDir = argc > 1 ? argv[1] : "";

    std::printf("ResoBody offline DSP test - drum loop (kick/snare/hat, 2 bars @ 120 bpm + 1.5 s tail)\n");
    std::printf("44100 Hz, in place, block sizes cycling 128/32/256/64/512/96/192\n\n");

    // ----- 1. musical settings ---------------------------------------------
    std::printf("-- normal use (input peak -6 dBFS) --\n");
    const Scenario normal[] = {
        { "Init defaults",        0.0f,  50.0f, 1.00f, 45.0f, 35.0f, 3.0f, 150.0f, 1, 0.5f },
        { "Kick Weight preset",   0.0f,  40.0f, 0.80f, 55.0f, 30.0f, 5.0f, 220.0f, 0, 0.5f },
        { "Snare Crack preset",  -1.0f,  55.0f, 1.35f, 60.0f, 55.0f, 2.0f, 120.0f, 2, 0.5f },
        { "Metal Ring preset",   -3.0f,  70.0f, 1.80f, 85.0f, 45.0f, 1.5f, 300.0f, 3, 0.5f },
        { "Full Resonator",      -4.0f, 100.0f, 1.20f, 75.0f, 60.0f, 2.0f, 250.0f, 2, 0.5f },
        { "Dry (mix 0)",          0.0f,   0.0f, 1.00f, 45.0f, 35.0f, 3.0f, 150.0f, 1, 0.5f },
    };
    for (const Scenario& sc : normal) report(sc.name, run(sc, wavDir), false, true);

    // ----- 2. the stability requirement: max RESO and max AUTO together ----
    std::printf("\n-- hostile: RESO 100%% and AUTO 100%% together, every body type --\n");
    for (int t = 0; t < kNumTypes; ++t)
    {
        char name[64];
        std::snprintf(name, sizeof(name), "max reso+auto, type %s", kTypeNames[t]);
        const Scenario sc = { name, 0.0f, 100.0f, 1.00f, 100.0f, 100.0f, 0.3f, 1000.0f, t, 0.5f };
        report(name, run(sc, wavDir), false, true);
    }

    std::printf("\n-- hostile: extremes of SCALE with max reso+auto --\n");
    const Scenario extremes[] = {
        { "max all, scale 0.25x",  0.0f, 100.0f, 0.25f, 100.0f, 100.0f, 0.3f, 1000.0f, 0, 0.5f },
        { "max all, scale 4.00x",  0.0f, 100.0f, 4.00f, 100.0f, 100.0f, 0.3f, 1000.0f, 4, 0.5f },
        { "max all, atk/rel min",  0.0f, 100.0f, 1.00f, 100.0f, 100.0f, 0.3f,   10.0f, 1, 0.5f },
        { "max all, hot input",    0.0f, 100.0f, 1.00f, 100.0f, 100.0f, 0.3f, 1000.0f, 1, 1.0f },
    };
    for (const Scenario& sc : extremes) report(sc.name, run(sc, wavDir), false, true);

    // Output trim at +12 dB is allowed to clip - it is a trim, the user asked for it -
    // but it must not produce NaN, denormals or DC.
    std::printf("\n-- output trim +12 dB on a hot input (clipping expected, instability is not) --\n");
    const Scenario loud = { "out +12 dB, max all", 12.0f, 100.0f, 1.00f, 100.0f, 100.0f, 0.3f, 1000.0f, 1, 1.0f };
    report(loud.name, run(loud, wavDir), true, true);

    // ----- 3. parameters being written while audio runs --------------------
    std::printf("\n-- every parameter randomised between every block (UI thread races audio) --\n");
    const Scenario slam = { "parameter slam", 0.0f, 100.0f, 1.00f, 100.0f, 100.0f, 0.3f, 1000.0f, 1, 0.5f };
    report(slam.name, run(slam, wavDir, true), true, false);

    // ----- 3b. the self-oscillation proof ----------------------------------
    // One impulse, then ten seconds of pure silence with RESO and AUTO maxed. A
    // self-oscillating filter would sustain or grow; a stable resonator must decay
    // monotonically (envelope-wise) to nothing. This is the claim the Q ceiling makes.
    std::printf("\n-- one impulse then 10 s of silence at max reso+auto (self-oscillation proof) --\n");
    for (int t = 0; t < kNumTypes; ++t)
    {
        ResoBody plugin;
        plugin.set(ResoBody::kMix, 100.0f);
        plugin.set(ResoBody::kReso, 100.0f);
        plugin.set(ResoBody::kAuto, 100.0f);
        plugin.set(ResoBody::kScale, 0.25f);          // lowest modes = longest ring
        plugin.set(ResoBody::kRelease, 1000.0f);
        plugin.set(ResoBody::kType, (float)t);
        plugin.internalSetSampleRate(kSr);
        plugin.internalSetBlockSize(128);
        plugin.internalResume();

        const size_t frames = (size_t)(10.0 * kSr);
        std::vector<float> L(frames, 0.0f), R(frames, 0.0f);
        L[0] = R[0] = 1.0f;                            // the impulse

        float firstSecond = 0.0f, lastSecond = 0.0f, growthAfterPeak = 0.0f, seen = 0.0f;
        bool finite = true;
        for (size_t pos = 0; pos + 128 <= frames; pos += 128)
        {
            float* buf[2] = { L.data() + pos, R.data() + pos };
            plugin.internalProcess(buf, buf, 128);
            float blockPeak = 0.0f;
            for (int i = 0; i < 128; ++i)
            {
                if (!std::isfinite(L[pos + i]) || !std::isfinite(R[pos + i])) finite = false;
                blockPeak = std::fmax(blockPeak, std::fabs(L[pos + i]));
            }
            if (pos < (size_t)kSr) firstSecond = std::fmax(firstSecond, blockPeak);
            if (pos > frames - (size_t)kSr) lastSecond = std::fmax(lastSecond, blockPeak);
            // After the first 0.5 s the envelope must never climb back up.
            if (pos > (size_t)(0.5 * kSr))
            {
                if (blockPeak > seen) growthAfterPeak = std::fmax(growthAfterPeak, blockPeak - seen);
                seen = std::fmax(blockPeak, seen * 0.999f);
            }
            else seen = std::fmax(seen, blockPeak);
        }

        const double firstDb = 20.0 * std::log10(firstSecond + 1e-12);
        const double lastDb  = 20.0 * std::log10(lastSecond + 1e-12);
        const bool ok = finite && lastDb < -100.0 && growthAfterPeak < 1.0e-6f;
        if (!ok) ++failures;
        std::printf("%-4s impulse decay, type %-8s first second %7.1f dBFS -> last second %7.1f dBFS%s\n",
                    ok ? "PASS" : "FAIL", kTypeNames[t], firstDb, lastDb,
                    ok ? "" : (finite ? " <- did not decay / grew again" : " <- NOT FINITE"));
    }

    // ----- 4. CPU with two instances in series -----------------------------
    std::printf("\n-- CPU: two instances processing the same audio (worst case: both maxed) --\n");
    {
        DrumLoop loop; loop.build(0.5f);
        ResoBody a, b;
        for (ResoBody* p : { &a, &b })
        {
            p->set(ResoBody::kMix, 100.0f);
            p->set(ResoBody::kReso, 100.0f);
            p->set(ResoBody::kAuto, 100.0f);
            p->internalSetSampleRate(kSr);
            p->internalSetBlockSize(128);
            p->internalResume();
        }
        std::vector<float> L = loop.left, R = loop.right;
        double elapsed = 0.0, budget = 0.0;
        for (size_t pos = 0; pos + 128 <= L.size(); pos += 128)
        {
            float* buf[2] = { L.data() + pos, R.data() + pos };
            const auto t0 = std::chrono::steady_clock::now();
            a.internalProcess(buf, buf, 128);
            b.internalProcess(buf, buf, 128);
            elapsed += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            budget += 128.0 / kSr;
        }
        bool finite = true;
        for (float v : L) if (!std::isfinite(v)) { finite = false; break; }
        const double pct = 100.0 * elapsed / budget;
        std::printf("%-4s two instances in series      %.2f%% of the real-time budget on this machine, output %s\n",
                    (finite && pct < 50.0) ? "PASS" : "FAIL", pct, finite ? "finite" : "NOT FINITE");
        if (!finite || pct >= 50.0) ++failures;
    }

    std::printf("\n%s (%d failure%s)\n", failures == 0 ? "Result: PASS" : "Result: FAIL",
                failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
