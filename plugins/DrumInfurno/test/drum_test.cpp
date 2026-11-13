// drum_test.cpp - offline DSP harness for Drum Infurno.
//
// SPDX-License-Identifier: MIT
//
// WHY THIS EXISTS
//   tools/host-sim checks what the MPC's host does: load sequence, VST2 ABI, state
//   chunks, in-place processing, block sizes. It feeds effects a sine plus a noise burst
//   and cannot pin a parameter at a chosen value. This plugin needs more than that: its
//   whole point is drum transients through saturation into high-Q resonators, and the
//   dangerous settings are max Drive with max Ring and max Bloom at the same time.
//
//   This harness synthesises a drum loop (kick / snare / hat, 2 bars at 120 bpm plus a
//   silent tail) and runs it through the plugin at deliberately hostile settings, driving
//   the class through the same public entry points the framework's dispatcher uses, with
//   the input and output buffers aliased exactly as the MPC aliases them.
//
//   It also proves the three things that cannot be argued from the source alone:
//     * the safety clipper's ceiling is a hard guarantee, not an aspiration;
//     * DRIVE at 0 really is a bypass (all three algorithms then produce identical output);
//     * MIX at 0 really is the delay-aligned dry signal, sample for sample.
//
//   It is NOT part of the plugin: build.py compiles only src/*.cpp and package.py ships
//   only the build output, so this file never reaches the MPC.
//
// BUILD AND RUN (from the SDK root)
//   c++ -std=c++17 -O2 -I framework -I plugins/DrumInfurno/src \
//       -o /tmp/di_test plugins/DrumInfurno/test/drum_test.cpp && /tmp/di_test /tmp/out
//   With a directory argument it writes a WAV per scenario. Exit 0 = all passed.

#include "DrumInfurno.cpp"   // the plugin under test, and the framework with it

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr double kSr = 44100.0;
const int kBlockSizes[] = { 128, 32, 256, 64, 512, 96, 192 };
constexpr int kNumBlockSizes = (int)(sizeof(kBlockSizes) / sizeof(kBlockSizes[0]));

// The ceiling the safety clipper promises, as a linear peak.
constexpr float kCeilingLinear = 0.9440609f;      // -0.5 dBFS

// ---------------------------------------------------------------------------
// Drum loop: sharp transients, strong low end, silent tail to watch the ring out.
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
            const float hz = 48.0f + 102.0f * std::exp(-t / 0.022f);
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
            hp = 0.72f * (hp + nz - prev);
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
            left[at + (size_t)i] += v;
            right[at + (size_t)i] += v * 0.75f;
        }
    }

    void build(float peakTarget)
    {
        const double eighth = 0.25;
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

struct Stats
{
    int badSamples = 0, denormals = 0, overCeiling = 0;
    float peak = 0.0f, tailPeak = 0.0f;
    double sumSq = 0.0, sum = 0.0;
    size_t count = 0;

    void accumulate(const float* x, size_t n, bool inTail)
    {
        for (size_t i = 0; i < n; ++i)
        {
            const float v = x[i];
            if (!std::isfinite(v)) { ++badSamples; continue; }
            const float a = std::fabs(v);
            if (a > kCeilingLinear + 1.0e-6f) ++overCeiling;
            if (a != 0.0f && a < 1.1754944e-38f) ++denormals;   // true denormals: < FLT_MIN
            peak = std::fmax(peak, a);
            if (inTail) tailPeak = std::fmax(tailPeak, a);
            sumSq += (double)v * v; sum += v; ++count;
        }
    }
    double rmsDb() const { return count ? 20.0 * std::log10(std::sqrt(sumSq / (double)count) + 1e-12) : -200.0; }
    double peakDb() const { return 20.0 * std::log10((double)peak + 1e-12); }
    double dcOffset() const { return count ? sum / (double)count : 0.0; }
    double tailDb() const { return 20.0 * std::log10((double)tailPeak + 1e-12); }
};

struct Settings
{
    float out = 0, mix = 60, drive = 0, shape = 0, grain = 0;
    int   type = 0;
    float cutLo = 0, cutMid = 0, cutHi = 0;
    int   chamber = 1;
    float bore = 1.0f, ring = 45, bloom = 35, rise = 3, fall = 150;
    bool  makeup = true;
    float keepLo = 20.0f;     // 20 Hz = effectively off
    float tame = 20000.0f;    // fully open = bypassed
    float inputPeak = 0.5f;
    bool  clip = true;
};

struct Result { Stats stats; double avgCpu = 0, worstCpu = 0; std::vector<float> L, R; };

void writeWav(const std::string& path, const std::vector<float>& l, const std::vector<float>& r)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    const uint32_t frames = (uint32_t)l.size(), dataBytes = frames * 4u;
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

Result run(const Settings& s, const std::string& wavDir = "", const char* wavName = nullptr,
           bool slam = false)
{
    DrumLoop loop; loop.build(s.inputPeak);

    DrumInfurno p;
    p.set(DrumInfurno::kOutput, s.out);
    p.set(DrumInfurno::kMix, s.mix);
    p.set(DrumInfurno::kDamageType, (float)s.type);
    p.set(DrumInfurno::kDrive, s.drive);
    p.set(DrumInfurno::kShape, s.shape);
    p.set(DrumInfurno::kGrain, s.grain);
    p.set(DrumInfurno::kCutLo, s.cutLo);
    p.set(DrumInfurno::kCutMid, s.cutMid);
    p.set(DrumInfurno::kCutHi, s.cutHi);
    p.set(DrumInfurno::kChamber, (float)s.chamber);
    p.set(DrumInfurno::kBore, s.bore);
    p.set(DrumInfurno::kRing, s.ring);
    p.set(DrumInfurno::kBloom, s.bloom);
    p.set(DrumInfurno::kRise, s.rise);
    p.set(DrumInfurno::kFall, s.fall);
    p.set(DrumInfurno::kMakeup, s.makeup ? 1.0f : 0.0f);
    p.set(DrumInfurno::kKeepLo, s.keepLo);
    p.set(DrumInfurno::kTame, s.tame);
    p.setSafetyClipEnabled(s.clip);

    p.internalSetSampleRate(kSr);
    p.internalSetBlockSize(512);
    p.internalResume();

    Result res;
    res.L = loop.left; res.R = loop.right;
    const size_t frames = res.L.size();
    const size_t tailStart = frames > (size_t)(0.25 * kSr) ? frames - (size_t)(0.25 * kSr) : 0;
    double elapsed = 0.0, budget = 0.0;
    uint32_t rng = 0xBEEF1234u;
    size_t pos = 0; int bi = 0;

    while (pos < frames)
    {
        int n = kBlockSizes[bi++ % kNumBlockSizes];
        if (pos + (size_t)n > frames) n = (int)(frames - pos);
        if (slam)
        {
            auto rnd = [&]() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (float)(rng >> 8) / 16777216.0f; };
            for (int k = 0; k < p.numParams(); ++k) p.setNormalised(k, rnd());
        }
        float* buf[2] = { res.L.data() + pos, res.R.data() + pos };
        const auto t0 = std::chrono::steady_clock::now();
        p.internalProcess(buf, buf, n);
        const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        const double bud = (double)n / kSr;
        elapsed += dt; budget += bud;
        res.worstCpu = std::fmax(res.worstCpu, 100.0 * dt / bud);
        res.stats.accumulate(res.L.data() + pos, (size_t)n, pos >= tailStart);
        res.stats.accumulate(res.R.data() + pos, (size_t)n, pos >= tailStart);
        pos += (size_t)n;
    }
    res.avgCpu = budget > 0 ? 100.0 * elapsed / budget : 0.0;
    if (!wavDir.empty() && wavName) writeWav(wavDir + "/" + wavName + ".wav", res.L, res.R);
    return res;
}

// Offline analysis helpers. These run on a rendered buffer, not in the audio path.
double bandRmsDb(const std::vector<float>& x, float hz, bool below)
{
    // Four cascaded one-poles (24 dB/oct). Two poles were not steep enough: at 70 Hz they
    // still passed the Body's 100 Hz resonance at -9.7 dB, so the "sub" reading was really
    // measuring body ring. Four poles put 100 Hz 25 dB down.
    const float a = 1.0f - std::exp(-2.0f * 3.14159265f * hz / (float)kSr);
    float z1 = 0.0f, z2 = 0.0f, z3 = 0.0f, z4 = 0.0f;
    double sum = 0.0;
    for (float v : x)
    {
        z1 += a * (v - z1);
        z2 += a * (z1 - z2);
        z3 += a * (z2 - z3);
        z4 += a * (z3 - z4);
        const double band = below ? z4 : (double)v - z4;
        sum += band * band;
    }
    return 20.0 * std::log10(std::sqrt(sum / (double)x.size()) + 1e-12);
}

// Normalised correlation of two signals' low bands, b delayed by `lag` samples relative
// to a. 1.0 = same waveform at any level; lower = a different (processed) waveform.
double subCorrelation(const std::vector<float>& out, const std::vector<float>& in, float hz, size_t lag)
{
    const float a = 1.0f - std::exp(-2.0f * 3.14159265f * hz / (float)kSr);
    auto band = [&](const std::vector<float>& x) {
        std::vector<double> y(x.size());
        float z1 = 0, z2 = 0, z3 = 0, z4 = 0;
        for (size_t i = 0; i < x.size(); ++i)
        {
            z1 += a * (x[i] - z1); z2 += a * (z1 - z2); z3 += a * (z2 - z3); z4 += a * (z3 - z4);
            y[i] = z4;
        }
        return y;
    };
    const std::vector<double> A = band(out), B = band(in);
    // Search the lag rather than assuming it. The crossover's lowpass has real group delay
    // at 50 Hz - several milliseconds - and a fixed lag would score that phase shift as if
    // it were distortion. What we want to know is whether the SAME waveform is there.
    double best = 0.0;
    for (size_t d = lag; d < lag + 700 && d < A.size(); ++d)
    {
        double ab = 0, aa = 0, bb = 0;
        for (size_t i = d; i < A.size(); ++i)
        {
            const double u = A[i], v = B[i - d];
            ab += u * v; aa += u * u; bb += v * v;
        }
        const double c = (aa > 0 && bb > 0) ? ab / std::sqrt(aa * bb) : 0.0;
        if (c > best) best = c;
    }
    return best;
}

// Magnitude response at one frequency, measured with a sine probe.
double probeDb(float hz, float mix, float keepLo)
{
    DrumInfurno p;
    p.set(DrumInfurno::kMix, mix);
    p.set(DrumInfurno::kKeepLo, keepLo);
    p.set(DrumInfurno::kDrive, 0.0f);
    p.set(DrumInfurno::kMakeup, 0.0f);
    p.internalSetSampleRate(kSr);
    p.internalSetBlockSize(128);
    p.internalResume();
    const size_t n = (size_t)(3.0 * kSr);
    std::vector<float> L(n), R(n);
    for (size_t i = 0; i < n; ++i) L[i] = R[i] = 0.2f * (float)std::sin(6.2831853 * hz * (double)i / kSr);
    for (size_t pos = 0; pos + 128 <= n; pos += 128)
    { float* b[2] = { L.data() + pos, R.data() + pos }; p.internalProcess(b, b, 128); }
    double sum = 0.0; size_t cnt = 0;
    for (size_t i = (size_t)kSr; i + 128 < n; ++i) { sum += (double)L[i] * L[i]; ++cnt; }
    return 20.0 * std::log10(std::sqrt(sum / (double)cnt) / (0.2 / std::sqrt(2.0)) + 1e-12);
}

int failures = 0;

void report(const char* label, const Result& r, bool allowOverCeiling, bool expectQuietTail)
{
    const Stats& s = r.stats;
    char notes[512]; notes[0] = '\0';
    bool ok = true;
    auto note = [&](const char* fmt, auto v) {
        std::snprintf(notes + std::strlen(notes), sizeof(notes) - std::strlen(notes), fmt, v);
    };
    if (s.badSamples) { ok = false; note(" <- %d NaN/Inf", s.badSamples); }
    if (s.denormals)  { ok = false; note(" <- %d denormals", s.denormals); }
    if (!allowOverCeiling && s.overCeiling) { ok = false; note(" <- %d samples ABOVE THE CEILING", s.overCeiling); }
    if (std::fabs(s.dcOffset()) > 0.002) { ok = false; note(" <- DC %.5f", s.dcOffset()); }
    if (expectQuietTail && s.tailDb() > -45.0) { ok = false; note(" <- tail %.1f dBFS", s.tailDb()); }
    if (!ok) ++failures;
    std::printf("%-4s %-30s peak %7.2f  rms %7.2f  tail %7.1f  dc %+.5f  cpu %.2f%%%s\n",
                ok ? "PASS" : "FAIL", label, s.peakDb(), s.rmsDb(), s.tailDb(), s.dcOffset(), r.avgCpu, notes);
}

void check(bool ok, const char* fmt, ...)
{
    va_list a; va_start(a, fmt);
    std::printf("%-4s ", ok ? "PASS" : "FAIL");
    std::vprintf(fmt, a);
    std::printf("\n");
    va_end(a);
    if (!ok) ++failures;
}

} // namespace

int main(int argc, char** argv)
{
    const std::string wavDir = argc > 1 ? argv[1] : "";
    std::printf("Drum Infurno offline DSP test - drum loop (kick/snare/hat, 2 bars @ 120 bpm + 1.5 s tail)\n");
    std::printf("44100 Hz, in place, block sizes cycling 128/32/256/64/512/96/192\n");
    std::printf("Safety ceiling: %.7f linear (-0.5 dBFS)\n\n", kCeilingLinear);

    // ----- 1. the factory programs ----------------------------------------
    std::printf("-- factory programs (input peak -6 dBFS) --\n");
    {
        Settings s; report("Init", run(s, wavDir, "01-init"), false, true);
        s = Settings(); s.mix=70; s.type=0; s.drive=45; s.shape=-25; s.grain=40; s.cutLo=2; s.cutMid=-1; s.chamber=0; s.bore=0.85f; s.ring=55; s.rise=5; s.fall=220;
        report("Tube Weight", run(s, wavDir, "02-tube-weight"), false, true);
        s = Settings(); s.mix=65; s.type=1; s.drive=35; s.shape=-40; s.grain=55; s.cutLo=1; s.cutHi=-2; s.bloom=45; s.ring=40; s.rise=6; s.fall=250;
        report("Tape Glue", run(s, wavDir, "03-tape-glue"), false, true);
        s = Settings(); s.mix=70; s.type=2; s.drive=55; s.shape=35; s.grain=45; s.cutLo=-1; s.cutMid=2; s.cutHi=3; s.chamber=2; s.bore=1.35f; s.ring=55; s.bloom=60; s.rise=2; s.fall=120;
        report("Overdrive Snap", run(s, wavDir, "04-od-snap"), false, true);
        s = Settings(); s.out=-2; s.mix=75; s.type=2; s.drive=65; s.shape=50; s.grain=70; s.cutLo=-2; s.cutMid=1; s.cutHi=4; s.chamber=3; s.bore=1.8f; s.ring=85; s.rise=1.5f; s.fall=300;
        report("Metal Ring", run(s, wavDir, "05-metal-ring"), false, true);
        s = Settings(); s.mix=65; s.type=0; s.drive=60; s.shape=-50; s.grain=70; s.cutLo=4; s.cutMid=-2; s.cutHi=-3; s.chamber=0; s.bore=0.5f; s.ring=65; s.bloom=25; s.rise=10; s.fall=320;
        report("808 Furnace", run(s, wavDir, "06-808-furnace"), false, true);
    }

    // ----- 2. the stability requirement ------------------------------------
    std::printf("\n-- hostile: DRIVE 100%% + RING 100%% + BLOOM 100%%, each algorithm --\n");
    for (int t = 0; t < di::DamageStage::kNumTypes; ++t)
        for (int sh = -100; sh <= 100; sh += 100)
        {
            Settings s;
            s.type = t; s.drive = 100; s.ring = 100; s.bloom = 100; s.grain = 100;
            s.shape = (float)sh; s.mix = 100; s.rise = 0.3f; s.fall = 1000;
            char label[80];
            std::snprintf(label, sizeof(label), "%s shape%+d grain100", di::DamageStage::typeName(t), sh);
            char wav[80]; std::snprintf(wav, sizeof(wav), "10-max-%s-%+d", di::DamageStage::typeName(t), sh);
            report(label, run(s, wavDir, wav), false, true);
        }

    std::printf("\n-- hostile: everything maxed, Cut boosted, hot input, Output +12 dB --\n");
    {
        Settings s;
        s.type = 2; s.drive = 100; s.ring = 100; s.bloom = 100; s.grain = 100; s.shape = 100;
        s.cutLo = 12; s.cutMid = 12; s.cutHi = 12; s.mix = 100; s.out = 12;
        s.chamber = 4; s.bore = 4.0f; s.rise = 0.3f; s.fall = 1000; s.inputPeak = 1.0f;
        report("max everything + 12 dB out", run(s, wavDir, "11-max-everything"), false, true);

        s.bore = 0.25f; s.chamber = 0;
        report("max everything, bore 0.25x", run(s, wavDir, "12-max-bore-min"), false, true);
    }

    // ----- 3. the ceiling guarantee ----------------------------------------
    std::printf("\n-- safety clipper: ceiling is a guarantee, not an aspiration --\n");
    {
        Settings s;
        s.type = 2; s.drive = 100; s.ring = 100; s.bloom = 100; s.grain = 100;
        s.cutLo = 12; s.cutMid = 12; s.cutHi = 12; s.mix = 100; s.out = 12; s.inputPeak = 1.0f;
        Result withClip = run(s, wavDir, "13-ceiling-on");
        s.clip = false;
        Result noClip = run(s, wavDir, "14-ceiling-off-reference");
        check(withClip.stats.overCeiling == 0,
              "clip on:  peak %.3f dBFS, %d samples above the ceiling",
              withClip.stats.peakDb(), withClip.stats.overCeiling);
        std::printf("INFO clip off: peak %.2f dBFS (%.2f dB of peak the clipper is absorbing)\n",
                    noClip.stats.peakDb(), noClip.stats.peakDb() - withClip.stats.peakDb());
        check(noClip.stats.peakDb() > withClip.stats.peakDb() + 1.0,
              "the clipper is actually doing work (unclipped peak is higher)");
    }

    // ----- 4. DRIVE 0 is a true bypass -------------------------------------
    std::printf("\n-- DRIVE 0 = clean: all three algorithms must be bit-identical --\n");
    {
        Settings a; a.drive = 0; a.type = 0; a.mix = 100; a.makeup = false;
        Settings b = a; b.type = 1;
        Settings c = a; c.type = 2;
        Result ra = run(a), rb = run(b), rc = run(c);
        double maxAB = 0, maxAC = 0;
        for (size_t i = 0; i < ra.L.size(); ++i)
        {
            maxAB = std::fmax(maxAB, std::fabs((double)ra.L[i] - rb.L[i]));
            maxAC = std::fmax(maxAC, std::fabs((double)ra.L[i] - rc.L[i]));
        }
        check(maxAB < 1e-7 && maxAC < 1e-7,
              "Tube vs Tape max diff %.2e, Tube vs Overdrive %.2e (Damage is bypassed at Drive 0)",
              maxAB > maxAC ? maxAB : maxAC);
    }

    // ----- 5. MIX 0 is the delay-aligned dry signal ------------------------
    std::printf("\n-- MIX 0 = dry, delay-aligned and sample-accurate --\n");
    {
        Settings s; s.mix = 0; s.drive = 100; s.ring = 100; s.makeup = false;
        s.inputPeak = 0.25f;                 // stay below the clipper's knee so it is unity
        Result r = run(s);
        DrumLoop ref; ref.build(0.25f);
        double worst = 0.0;
        const size_t lat = (size_t)di::kFlutterNominal;
        for (size_t i = lat; i < r.L.size(); ++i)
            worst = std::fmax(worst, std::fabs((double)r.L[i] - ref.left[i - lat]));
        check(worst < 1e-6, "dry path matches the input delayed by %d samples (max diff %.2e)",
              (int)lat, worst);
    }

    // ----- 6. auto-makeup brings the wet back to input level ---------------
    std::printf("\n-- auto-makeup: wet should land near input level --\n");
    {
        Settings on; on.mix = 100; on.drive = 80; on.ring = 80; on.makeup = true;  on.type = 0;
        Settings off = on; off.makeup = false;
        Result ron = run(on, wavDir, "20-makeup-on"), roff = run(off, wavDir, "21-makeup-off");
        DrumLoop ref; ref.build(0.5f);
        double refSq = 0.0;
        for (float v : ref.left) refSq += (double)v * v;
        const double refRms = 20.0 * std::log10(std::sqrt(refSq / (double)ref.left.size()) + 1e-12);
        std::printf("INFO input rms %.2f dBFS | makeup off %.2f dBFS | makeup on %.2f dBFS\n",
                    refRms, roff.stats.rmsDb(), ron.stats.rmsDb());
        check(std::fabs(ron.stats.rmsDb() - refRms) < 6.0,
              "makeup lands within 6 dB of the input (off by %.2f dB)", ron.stats.rmsDb() - refRms);
        check(std::fabs(ron.stats.rmsDb() - refRms) < std::fabs(roff.stats.rmsDb() - refRms),
              "makeup on is closer to input level than makeup off");
    }

    // ----- 7. parameters written while audio runs --------------------------
    std::printf("\n-- every parameter randomised between every block --\n");
    {
        Settings s; s.inputPeak = 1.0f;
        report("parameter slam", run(s, wavDir, "30-slam", true), false, false);
    }

    // ----- 8. self-oscillation proof, through the whole chain ---------------
    std::printf("\n-- impulse + 10 s silence at max Drive/Ring/Bloom (self-oscillation proof) --\n");
    for (int t = 0; t < di::DamageStage::kNumTypes; ++t)
    {
        DrumInfurno p;
        p.set(DrumInfurno::kMix, 100.0f);
        p.set(DrumInfurno::kDamageType, (float)t);
        p.set(DrumInfurno::kDrive, 100.0f);
        p.set(DrumInfurno::kRing, 100.0f);
        p.set(DrumInfurno::kBloom, 100.0f);
        p.set(DrumInfurno::kBore, 0.25f);
        p.set(DrumInfurno::kFall, 1000.0f);
        p.set(DrumInfurno::kMakeup, 0.0f);     // makeup would mask a decay by raising gain
        p.internalSetSampleRate(kSr);
        p.internalSetBlockSize(128);
        p.internalResume();

        const size_t frames = (size_t)(10.0 * kSr);
        std::vector<float> L(frames, 0.0f), R(frames, 0.0f);
        L[0] = R[0] = 1.0f;
        float lastSecond = 0.0f; bool finite = true;
        for (size_t pos = 0; pos + 128 <= frames; pos += 128)
        {
            float* buf[2] = { L.data() + pos, R.data() + pos };
            p.internalProcess(buf, buf, 128);
            for (int i = 0; i < 128; ++i)
                if (!std::isfinite(L[pos + i]) || !std::isfinite(R[pos + i])) finite = false;
            if (pos > frames - (size_t)kSr)
                for (int i = 0; i < 128; ++i) lastSecond = std::fmax(lastSecond, std::fabs(L[pos + i]));
        }
        const double lastDb = 20.0 * std::log10(lastSecond + 1e-12);
        check(finite && lastDb < -100.0, "impulse decay, %-9s -> last second %.1f dBFS",
              di::DamageStage::typeName(t), lastDb);
    }

    // ----- 8b. KEEP LO protects the low end from the chain ------------------
    std::printf("\n-- KEEP LO: the sub reaching the output should be the ORIGINAL sub --\n");
    {
        // Level is the wrong success criterion here: clipping a kick squares it up and
        // regenerates fundamental energy, so with Keep Lo OFF the output can hold MORE
        // sub than the input - just not the input's sub. What "untouched" means is that
        // the waveform is the original one, so this correlates the output's sub band with
        // the input's. Scale-invariant by construction.
        Settings off; off.mix = 100; off.drive = 100; off.ring = 80; off.type = 2;
        off.makeup = false; off.keepLo = 20.0f;
        Settings on = off; on.keepLo = 150.0f;
        Result roff = run(off, wavDir, "40-keeplo-off"), ron = run(on, wavDir, "41-keeplo-150");
        DrumLoop ref; ref.build(0.5f);
        const size_t lag = (size_t)di::kFlutterNominal;
        const double cOff = subCorrelation(roff.L, ref.left, 55.0f, lag);
        const double cOn  = subCorrelation(ron.L,  ref.left, 55.0f, lag);
        const double refLo = bandRmsDb(ref.left, 55.0f, true);
        std::printf("INFO sub (<55 Hz) rms: input %.2f | off %.2f | on %.2f dBFS\n",
                    refLo, bandRmsDb(roff.L, 55.0f, true), bandRmsDb(ron.L, 55.0f, true));
        std::printf("INFO sub correlation with the original (best lag): off %.3f | on %.3f\n", cOff, cOn);
        check(cOn > 0.95, "with Keep Lo on the sub is the original waveform (correlation %.3f)", cOn);
        check(cOn > cOff + 0.05, "Keep Lo on correlates better than off (%.3f vs %.3f)", cOn, cOff);
        check(ron.stats.badSamples == 0 && ron.stats.overCeiling == 0,
              "Keep Lo stays finite and under the ceiling");

        // And the plain level case, where nothing is regenerating sub: Body alone.
        Settings q = on; q.drive = 0; q.inputPeak = 0.25f;
        Settings qoff = q; qoff.keepLo = 20.0f;
        DrumLoop r2; r2.build(0.25f);
        const double base = bandRmsDb(r2.left, 55.0f, true);
        const double lOff = bandRmsDb(run(qoff).L, 55.0f, true);
        const double lOn  = bandRmsDb(run(q).L, 55.0f, true);
        std::printf("INFO Body alone, sub vs input: off %+.2f dB | on %+.2f dB\n", lOff - base, lOn - base);
        check(std::fabs(lOn - base) < std::fabs(lOff - base),
              "with the resonator alone, Keep Lo holds the sub %.2f dB closer",
              std::fabs(lOff - base) - std::fabs(lOn - base));
    }

    // ----- 8b2. no cancellation notch at the crossover ---------------------
    // Regression guard. LR4's two bands sum to an allpass, which at the crossover sits
    // 180 degrees from the unfiltered input - so crossfading the clean low against a
    // FULL-RANGE dry subtracts there. That shipped once, measuring -22.5 dB at 150 Hz
    // with Mix 60. MIX now crossfades band-for-band and the low end sits outside it.
    std::printf("\n-- MIX must not notch at the Keep Lo crossover --\n");
    {
        const double full = probeDb(150.0f, 100.0f, 150.0f);
        const double half = probeDb(150.0f,  60.0f, 150.0f);
        const double sub100 = probeDb(45.0f, 100.0f, 150.0f);
        const double sub60  = probeDb(45.0f,  60.0f, 150.0f);
        std::printf("INFO 150 Hz (the crossover): Mix 100 %.2f | Mix 60 %.2f dB\n", full, half);
        std::printf("INFO  45 Hz (kept sub):      Mix 100 %.2f | Mix 60 %.2f dB\n", sub100, sub60);
        check(std::fabs(half - full) < 6.0,
              "no notch at the crossover (Mix 60 is %.2f dB from Mix 100)", half - full);
        check(std::fabs(sub60 - sub100) < 1.0,
              "the kept sub is independent of MIX (%.2f dB apart)", sub60 - sub100);
    }

    // ----- 8c. TAME actually removes high end, and stays stable -------------
    std::printf("\n-- TAME: the ladder should take the glare off, not blow up --\n");
    {
        Settings open_; open_.mix = 100; open_.drive = 100; open_.type = 2; open_.grain = 100;
        open_.tame = 20000.0f; open_.makeup = false;
        open_.chamber = 4; open_.bore = 4.0f; open_.ring = 0.0f;   // widest bands: HF reaches the output
        Settings shut = open_; shut.tame = 1000.0f;
        Result ro = run(open_, wavDir, "42-tame-open"), rs = run(shut, wavDir, "43-tame-1k");
        const double hiOpen = bandRmsDb(ro.L, 4000.0f, false);
        const double hiShut = bandRmsDb(rs.L, 4000.0f, false);
        std::printf("INFO HF (>4 kHz) rms: Tame open %.2f | Tame 1 kHz %.2f dBFS (%.1f dB removed)\n",
                    hiOpen, hiShut, hiOpen - hiShut);
        check(hiShut < hiOpen - 6.0, "the ladder removes real high end (%.1f dB)", hiOpen - hiShut);
        check(rs.stats.badSamples == 0 && rs.stats.overCeiling == 0 && std::fabs(rs.stats.dcOffset()) < 0.002,
              "ladder output is finite, under the ceiling and DC-free");

        Settings worst = shut; worst.tame = 200.0f; worst.keepLo = 250.0f; worst.bloom = 100;
        worst.ring = 100; worst.out = 12; worst.inputPeak = 1.0f;
        worst.cutLo = 12; worst.cutMid = 12; worst.cutHi = 12;
        report("ladder min + everything max", run(worst, wavDir, "44-tame-min-max"), false, true);
    }

    // ----- 8d. the ladder must not self-oscillate --------------------------
    std::printf("\n-- ladder self-oscillation check: impulse, then 10 s of silence --\n");
    {
        DrumInfurno p;
        p.set(DrumInfurno::kMix, 100.0f);
        p.set(DrumInfurno::kDrive, 100.0f);
        p.set(DrumInfurno::kRing, 100.0f);
        p.set(DrumInfurno::kBloom, 100.0f);
        p.set(DrumInfurno::kTame, 200.0f);          // lowest cutoff: most resonant setting
        p.set(DrumInfurno::kKeepLo, 250.0f);
        p.set(DrumInfurno::kMakeup, 0.0f);
        p.internalSetSampleRate(kSr);
        p.internalSetBlockSize(128);
        p.internalResume();
        const size_t frames = (size_t)(10.0 * kSr);
        std::vector<float> L(frames, 0.0f), R(frames, 0.0f);
        L[0] = R[0] = 1.0f;
        float lastSecond = 0.0f; bool finite = true;
        for (size_t pos = 0; pos + 128 <= frames; pos += 128)
        {
            float* buf[2] = { L.data() + pos, R.data() + pos };
            p.internalProcess(buf, buf, 128);
            for (int i = 0; i < 128; ++i)
                if (!std::isfinite(L[pos + i])) finite = false;
            if (pos > frames - (size_t)kSr)
                for (int i = 0; i < 128; ++i) lastSecond = std::fmax(lastSecond, std::fabs(L[pos + i]));
        }
        const double lastDb = 20.0 * std::log10(lastSecond + 1e-12);
        check(finite && lastDb < -100.0, "ladder at minimum cutoff decays to %.1f dBFS", lastDb);
    }

    // ----- 9. CPU with two instances ---------------------------------------
    std::printf("\n-- CPU: two instances in series, both maxed --\n");
    {
        DrumLoop loop; loop.build(0.5f);
        DrumInfurno a, b;
        for (DrumInfurno* p : { &a, &b })
        {
            p->set(DrumInfurno::kMix, 100.0f);
            p->set(DrumInfurno::kDrive, 100.0f);
            p->set(DrumInfurno::kRing, 100.0f);
            p->set(DrumInfurno::kBloom, 100.0f);
            p->set(DrumInfurno::kDamageType, 1.0f);   // Tape: the most expensive path
            p->set(DrumInfurno::kGrain, 100.0f);
            p->set(DrumInfurno::kKeepLo, 150.0f);
            p->set(DrumInfurno::kTame, 1200.0f);   // ladder active: the expensive path
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
        check(finite && pct < 50.0, "two instances: %.2f%% of the real-time budget here, output %s",
              pct, finite ? "finite" : "NOT FINITE");
    }

    std::printf("\n%s (%d failure%s)\n", failures == 0 ? "Result: PASS" : "Result: FAIL",
                failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
