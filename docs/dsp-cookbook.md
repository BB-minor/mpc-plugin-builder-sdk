# DSP cookbook for MPC plugins

A practical guide for an AI coding agent (or a human) writing the audio code of an MPC
standalone plugin with this SDK. Read `framework/mpc_plugin.h` once; this document tells
you how to use it well on the hardware. The three projects under `examples/` follow every
rule below and are the best place to copy from.

Target hardware: Akai MPC standalone (Linux ARM). Audio runs at **44.1 kHz**, the host
tells you a block size of **128** (sometimes more) but may call you with **fewer frames**.
CPUs: Cortex-A17 32-bit (MPC X/Live/One) or Cortex-A55/A76 64-bit (newer models). Plan
for the slowest one.

---

## 1. Realtime rules (never break these in `process()`)

1. **No allocation.** No `new`, `malloc`, `std::vector::push_back`/`resize`, `std::string`,
   `std::function`, `std::map`. Allocate in `prepare()` (it runs before audio starts and
   again when the sample rate or block size changes). `dsp/delay_line.h` shows the pattern:
   `DelayLine::prepare(maxSamples)` allocates, nothing else does.
2. **No locks, no I/O, no logging.** `mpc::log()` is for the constructor/UI thread only.
3. **Block size is a maximum.** `ctx.numFrames` can be 32..512 on the MPC regardless of what
   `effSetBlockSize` said. Never assume 128. If you need a scratch buffer, make it a fixed
   array (e.g. 64 samples) and render longer blocks in chunks (see `SimpleSynth::render`).
4. **In-place processing.** The MPC host passes the *same* memory as `ctx.inputs[c]` and
   `ctx.outputs[c]`. The framework pre-clears only output channels that do not alias an
   input. So in every effect:
   - read all input samples of a frame into locals **before** writing any output of that frame,
   - never read ahead into later input frames after writing earlier outputs,
   - never clear/memset the outputs before you have read the inputs.
   ```cpp
   for (int i = 0; i < ctx.numFrames; ++i) {
       const float inL = ctx.inputs[0][i], inR = ctx.inputs[1][i];   // read first
       ... compute ...
       ctx.outputs[0][i] = outL; ctx.outputs[1][i] = outR;            // then write
   }
   ```
   Instruments with 0 inputs can simply accumulate into the (already zeroed) outputs.
5. **Parameters are atomics** written by the UI thread at any time. Read each one **once per
   block** into a local/struct (`SimpleSynth::updateBlockSettings`), never per sample.
6. **Smooth anything audible.** Gain, cutoff, delay time, mix: run them through
   `dsp::Smoother` (fixed time constant) or `dsp::LinearRamp` (ramps over the block).
   Unsmoothed changes click and zipper.
7. **Denormals** are flushed by the framework (FTZ around every `process()`), but a feedback
   loop with gain >= 1 still explodes: clamp feedback below 1, or put `softClip`/`fastTanh`
   in the loop (StereoDelay does). Add `flushDenormal()` only if you measure a problem.
8. **CPU budget.** A 128-frame block is 2.9 ms. On a Cortex-A17 one core is shared with the
   MPC's own sequencer, sampler and mixer: aim for **< 15 % of the block (~0.4 ms)** for a
   whole plugin, worst case. Rough cost model for the A17: a `sin`/`tan`/`pow`/`exp` call
   ~50-100 ns, a divide ~10-20 ns, a multiply-add ~1 ns. So: transcendental functions at
   control rate (every 16-32 samples), not per sample; `WavetableSine` instead of `std::sin`
   in voices; `fastTanh` instead of `std::tanh`. The host simulator prints timing for the
   desktop build; multiply by roughly 15-25 for the A17.
9. **Parameter 0 gets wiggled.** The MPC sets parameter 0 to 1.0, 0.0 and back on every
   prepare. Do nothing in `parameterChanged()` that has side effects, and never put a
   "trigger" or "panic" switch at index 0.
10. **Thread safety of state.** `loadExtraState()` and `setProgram()` are called from the UI
    thread while audio may be running; the framework defers `reset()` to the audio thread
    afterwards. If you have extra state, write it into a staging copy in `loadExtraState()`
    and swap it in from `reset()`.

---

## 2. The dsp headers (`framework/dsp/`)

All header-only, `namespace mpc::dsp`, no dependencies, allocation-free except where noted.

| Header | What | One-liner |
|---|---|---|
| `dsp/math.h` | clamp/lerp, dB<->gain, semitones/cents to ratio, fastTanh/softClip, ms<->samples, pan, denormal/NaN helpers | `float g = dbToGain(get(kVol));  y = fastTanh(x * drive);` |
| `dsp/smoother.h` | `Smoother` (one-pole, per sample), `LinearRamp` (per block) | `s.setTime(20, sr); s.setTarget(v); x *= s.process();` |
| `dsp/oscillator.h` | `PolyBlepOsc` (sine/tri/saw/square, pulse width, phase reset), `WavetableSine` | `osc.setWave(Wave::Saw); osc.setFrequency(hz); y = osc.process();` |
| `dsp/noise.h` | `Xorshift32`, `WhiteNoise`, `PinkNoise` | `WhiteNoise n(seed); y = n.process();` |
| `dsp/biquad.h` | RBJ `BiquadCoeffs::lowpass/highpass/bandpass/notch/allpass/peak/lowShelf/highShelf`, `Biquad`, `StereoBiquad` | `eq.setCoeffs(BiquadCoeffs::peak(sr, 1000, 1.0f, 6.0f)); y = eq.process(x);` |
| `dsp/svf.h` | Cytomic TPT state variable filter, LP/BP/HP/notch/peak, drive | `f.set(cutoffHz, reso01); y = f.process(x, SVF::Mode::Lowpass);` |
| `dsp/onepole.h` | `OnePoleLP`, `OnePoleHP`, `DCBlocker` | `lp.setCutoff(5000, sr); y = lp.process(x);` |
| `dsp/adsr.h` | `ADSR` exponential envelope, retrigger, `isActive()` | `env.set(a, d, s, r); env.noteOn(); amp = env.process();` |
| `dsp/lfo.h` | `LFO` sine/tri/saw/square/S&H, Hz or tempo-synced, `syncToPpq` | `lfo.setTempoSync(bpm, divisionBeats(i)); m = lfo.process();` |
| `dsp/delay_line.h` | `DelayLine` fractional read (linear/allpass); **allocates in `prepare()` only** | `dl.prepare(sr * 2); y = dl.readLinear(d); dl.write(x + y * fb);` |
| `dsp/voice.h` | `VoiceAllocator<N>`: note on/off, stealing, sustain pedal, pitch bend | `int v = alloc.noteOn(note, vel); ... alloc.voiceFinished(v);` |
| `dsp/tempo.h` | beats<->seconds<->samples, division table (1/64 .. 2/1, T and D), `divisionNames()` for a choice param | `secs = beatsToSeconds(divisionBeats(getInt(kDiv)), ctx.transport.bpm);` |
| `dsp/midi_cc.h` | CC numbers (`cc::ModWheel`, `cc::Sustain`...), `MidiControllers` smoothed wheel/bend/aftertouch | `if (ctl.handle(ev)) continue; ... ctl.tick(); w = ctl.modWheel();` |

Only `divisionNames()`, `LFO::shapeNames()` and `DelayLine::prepare()` allocate. Use them
in the constructor / `prepare()`.

---

## 3. Structuring a synth voice

```cpp
struct Voice {
    PolyBlepOsc osc;  SVF filter;  ADSR ampEnv, filtEnv;  Smoother glide;
    int note = -1;  float velGain = 1.0f;  int ctrl = 0;
};
Voice voices_[8];
VoiceAllocator<8> alloc_;
```

- `prepare()`: `setSampleRate` on every component. `reset()`: `env.reset()`, `filter.reset()`,
  `alloc_.reset()`.
- Per block: read parameters once, push the envelope/filter settings into every voice.
- Per event: `int v = alloc_.noteOn(note, vel)` then start `voices_[v]`; `alloc_.noteOff(note)`
  returns the voice to release or -1 (sustain pedal down). After rendering, if
  `!ampEnv.isActive()` call `alloc_.voiceFinished(v)`.
- Per sample inside the voice: envelopes and oscillator every sample; pitch, cutoff and
  `filter.set()` every 16 samples (`if ((ctrl++ % 16) == 0)`).
- Mix all voices into `outputs[0]`, apply the smoothed master gain, copy to `outputs[1]`.
- Scale the sum: 8 voices at full level clip. Multiply each voice by ~0.3-0.5 or by
  `1/sqrt(N)`.

Pitch: `alloc_.noteToHz(note)` includes the bend; `semitonesToRatio()` / `centsToRatio()`
for detune and octave switches; glide = `Smoother` on the MIDI note number (linear in pitch).

Full example: `examples/SimpleSynth`.

## 4. Structuring an effect

```cpp
void prepare(double sr, int) override { delay_.prepare((int)(sr * 2) + 1); time_.setTime(50, sr); }
void reset() override { delay_.reset(); filter_.reset(); time_.snap(time_.target()); }
void process(ProcessContext& ctx) override {
    time_.setTarget(msToSamples(get(kTime), ctx.sampleRate));      // per block
    filter_.setCoeffs(BiquadCoeffs::lowpass(ctx.sampleRate, get(kTone)));
    for (int i = 0; i < ctx.numFrames; ++i) {                       // per sample
        const float inL = ctx.inputs[0][i], inR = ctx.inputs[1][i]; // read first (in place!)
        ...
        ctx.outputs[0][i] = ...; ctx.outputs[1][i] = ...;
    }
}
```

- `isInstrument = false`, `numInputs = numOutputs = 2`. Effects never get MIDI on the MPC.
- Dry/wet: `out = dry + (wet - dry) * mix`.
- Coefficients that need `sin/cos/pow` (biquads) per block; the SVF is cheap enough to
  update at control rate when modulated.
- Set `tailSamples` in `PluginInfo` if you know the decay length (reverb/delay), otherwise 0.

Full example: `examples/StereoDelay`.

## 5. A sequencer-style instrument (using Transport)

`ctx.transport` gives `playing`, `bpm`, `ppqPos` (quarter notes since song start),
`barStartPpq`, time signature. Typical step sequencer:

```cpp
const Transport& t = ctx.transport;
const float stepBeats = divisionBeats(getInt(kStepDiv));              // e.g. 1/16 = 0.25
if (t.playing && t.ppqValid) {
    const double ppqPerSample = t.bpm / 60.0 / ctx.sampleRate;
    for (int i = 0; i < ctx.numFrames; ++i) {
        const double ppq = t.ppqPos + i * ppqPerSample;
        const long step = (long)std::floor(ppq / stepBeats);
        if (step != lastStep_) { lastStep_ = step; triggerStep((int)(step % kNumSteps), i); }
        ... render sample i ...
    }
} else lastStep_ = -1;   // re-arm when stopped
```

- `bpm` defaults to 120 and `ppqPos` to 0 when the host does not provide them
  (`tempoValid` / `ppqValid` tell you). `valid == false` means no transport at all.
- Trigger at the exact sample offset inside the block, as above, so steps do not jitter.
- Pattern data (steps, notes) is extra state: see section 8.
- Use `ppqPhase(ppq, beats)` from `dsp/tempo.h` for anything cyclic (LFO, tremolo, gate).

## 6. MIDI handling patterns

`ctx.midi[0..numMidiEvents)` is sorted by `sampleOffset`. The sample-accurate loop:

```cpp
int pos = 0;
for (int e = 0; e < ctx.numMidiEvents; ++e) {
    const MidiEvent& ev = ctx.midi[e];
    if (ev.sampleOffset > pos) { render(ctx, pos, ev.sampleOffset); pos = ev.sampleOffset; }
    handle(ev);
}
render(ctx, pos, ctx.numFrames);
```

Inside `handle()`:

| Event | Test | Do |
|---|---|---|
| Note on | `ev.isNoteOn()` (velocity 0 counts as off) | `alloc_.noteOn(ev.note(), ev.velocity())`, start voice |
| Note off | `ev.isNoteOff()` | `int v = alloc_.noteOff(ev.note()); if (v >= 0) release(v);` |
| Pitch bend | `ev.isPitchBend()` | `alloc_.setPitchBend(ev.pitchBend())` (-8192..8191) |
| Sustain | `ev.isController() && ev.controller() == cc::Sustain` | `alloc_.setSustain(ev.controllerValue() >= 64, releaseLambda)` |
| Mod wheel / CC / aftertouch | `controllers_.handle(ev)` | read `controllers_.modWheel()` etc. per sample after `tick()` |
| All notes off | `ev.isAllNotesOff()` (CC 120 or 123) | release every voice (`alloc_.allNotesOff(...)`); drums: kill on 120 |

- Drum machines: ignore note-offs, retrigger on every note-on, implement choke groups by
  killing the other voice (`DrumSynth`).
- Never leave a voice sounding after all-notes-off: the MPC sends it when you press Stop.
- Only instruments receive MIDI. An effect that wants MIDI (`wantsMidi = true`) will still
  get none on the MPC.

## 7. Parameter design for the MPC screen

- **Names <= 12 characters** ("Filt Release", "Osc2 Detune"). Longer names are clipped on the
  touch screen and in the Q-Link labels.
- **Units**: pass "Hz", "ms", "dB", "%", "st" (semitones), "ct" (cents). The display is
  `value + unit`; `decimals` controls precision (use 0 for %, 1 for dB).
- **Ranges**: `logParam` for frequencies and times (20..18000 Hz, 1..5000 ms), `floatParam`
  with `skew` 0.3-0.5 for things that need resolution near zero (glide, LFO depth).
- **Choices**: `choiceParam("mode", "Mode", {"LP","BP","HP"})` - keep labels short, order them
  musically. The data wheel steps through them; `ui.json` can hide controls depending on a
  choice (`visibleWhen`).
- **Bools**: `boolParam` shows Off/On.
- **Order matters**: the first 16 parameters are the default Q-Link assignments in Program
  mode and the default UI page. Put the most played-with ones first. Parameter 0 should be
  harmless when toggled (see rule 9).
- **ids never change** once a plugin is released: saved projects look parameters up by id.
  Renaming the display name is fine; renaming the id loses the saved value.
- 16 controls per page, at most ~4 pages: more than ~50 parameters is hard to use on the
  hardware. Prefer macro parameters to dozens of fine ones.

## 8. State: programs vs extra state

- **Parameters** are saved automatically (id + plain value) in the chunk the MPC stores in
  the project. You never write code for this.
- **Built-in programs** (`addProgram(name, plainValues)`) are factory presets. The user picks
  them from the MPC's program list. They are just parameter sets; the current one is
  restored by the parameter chunk anyway.
- **Extra state** (`saveExtraState`/`loadExtraState`): anything that is not a parameter -
  sequencer patterns, a user sample, a random seed. Write a small versioned binary blob:
  ```cpp
  void saveExtraState(std::vector<uint8_t>& out) override {
      out.push_back(1);                                        // version
      out.insert(out.end(), (uint8_t*)steps_, (uint8_t*)steps_ + sizeof(steps_));
  }
  void loadExtraState(const uint8_t* d, size_t n) override {
      if (n < 1 + sizeof(steps_) || d[0] != 1) return;         // unknown version: keep defaults
      std::memcpy(stagedSteps_, d + 1, sizeof(steps_)); stagePending_ = true;   // swap in reset()
  }
  ```
  `loadExtraState` runs on the UI thread; copy into a staging area and apply in `reset()`
  (which the framework calls on the audio thread right after a load).
- Plain float values of log/skewed parameters may drift by ~1e-7 relative across repeated
  save/load cycles (normalised <-> plain rounding). Harmless; do not compare state bytes.

## 9. Tempo sync

```cpp
addParam(choiceParam("sync", "Sync", {"Off", "On"}, 1));
addParam(choiceParam("div", "Division", divisionNames(), kDivisionEighth));
addParam(logParam("time", "Time", 1, 2000, 350, "ms", 0));
...
float ms = getInt(kSync) ? beatsToSeconds(divisionBeats(getInt(kDiv)), ctx.transport.bpm) * 1000.0f
                         : get(kTime);
```

- Clamp the result to what you allocated (`delay.maxDelay()`); 1/1 at 60 bpm is 4 seconds.
- Smooth the time so tempo changes glide instead of click.
- LFOs: `lfo.setTempoSync(bpm, beats)` and, while `transport.playing`, `lfo.syncToPpq(ppqPos)`
  once per block so the LFO stays locked to the bars.
- In `ui.json` show "Division" when Sync is On and "Time" when it is Off.

## 10. Multi-out

- `numOutputs = 4, 6, 8...` (always even). The MPC exposes stereo pairs: outputs 1-2 are the
  main pair, 3-4 the next, etc. Each pair can be routed to its own mixer channel.
- Decide per voice which pair it goes to and add into `ctx.outputs[pair * 2]` and
  `[pair * 2 + 1]`. Offer a "Routing" choice so users without the extra channels still hear
  everything on the main pair ("All Main").
- Guard with `ctx.numOutputs` in case a host gives you fewer channels than declared.
- `numOutputs` must be > 0 or the plugin is not listed at all.
- Example: `examples/DrumSynth` (kick/snare/clap main, hats/toms/perc aux in "Split").

## 11. Common bugs

| Symptom | Cause | Fix |
|---|---|---|
| Silence on the MPC, works in the simulator | effect expected MIDI, or `numOutputs == 0`, or output written before input read (in place) | effects get no MIDI; set outputs; read-then-write |
| Clicks when turning a knob | unsmoothed gain/cutoff/time | `Smoother`/`LinearRamp` |
| Clicks on note start | oscillator phase not reset or envelope restarted from 0 while sounding | `resetPhase()` only when the voice was silent; `ADSR::noteOn()` retriggers from the current level |
| Crackles / dropouts, CPU spikes | `std::sin`/`pow` per sample x voices; allocation in `process()`; denormals in a filter with no FTZ (debug builds) | control rate, wavetable, allocate in `prepare()` |
| Volume slowly explodes | feedback >= 1, resonance 1.0 with drive | clamp feedback, `softClip` in the loop |
| Notes stuck | voice freed before `noteOff`, sustain logic, no all-notes-off handling | `VoiceAllocator`, handle `isAllNotesOff()` |
| Wrong pitch after bend | bend applied twice or range mismatch | compute Hz once: `alloc_.noteToHz(note)` |
| Parameter resets to default on project load | parameter id changed, or program recall overriding | keep ids stable; programs only on `effSetProgram` |
| Wrong value on screen | `decimals`/unit, or choice list order differs from the enum | match `choiceParam` order to the `enum` |
| Aliasing / harsh highs | naive saw/square | `PolyBlepOsc`; keep SVF cutoff <= 0.49 * sr (it clamps) |
| Different sound at 32-frame blocks | per-block logic assumed 128 frames (e.g. LFO stepped per block) | compute per sample or per fixed sub-chunk |
| Works native, crashes on ARM32 | unaligned access, `double` assumed fast, large stack arrays | keep buffers in members, use `float`, stack arrays < 16 kB |

## 12. Checklist before building

- [ ] `uid` in `plugin.json` equals the `MPC_FOURCC` in the source.
- [ ] `kind` / `isInstrument`, `numInputs`, `numOutputs` agree between `plugin.json` and `PluginInfo`.
- [ ] Every parameter added in the enum order, names <= 12 chars, units set.
- [ ] `prepare()` allocates, `reset()` clears, `process()` does neither.
- [ ] Effects read inputs before writing outputs (in place).
- [ ] All-notes-off handled; no sound 1 s after it.
- [ ] `tools/build.py <project>` then `host_sim <native> test` passes with no FAIL.
