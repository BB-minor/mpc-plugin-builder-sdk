# Drum Infurno

**INFUR — insert effect for Akai MPC standalone**

A drum-colouration unit: saturation into a tone stage into a dynamic modal resonator.
Three sections in one insert, voiced for drums.

> The project folder, source files and C++ class are `DrumInfurno`; the MPC shows the
> plugin as **Drum Infurno** by **INFUR** (`uid` `ICPO`, folder
> `INFUR - VST - Drum Infurno`).

## Signal order

Strictly in series, in this order:

```
in ──┬───────────────────── dry, delay-aligned ──────────────────────────┐
     │                                                                   │
     ├─► KEEP LO ─ low band ─────────────── clean, untouched ───────┐    │
     │      │                                                       │    │
     │   high band                                                  ▼    ▼
     └──────┴─► DAMAGE ─► TAME ─► CUT ─► BODY ─► AUTO-MAKEUP ─► + ─► MIX ─► OUTPUT ─► SAFETY CLIP ─► out
                Tube      ladder  3-band  modal   slow RMS
                Tape      LP      EQ      resonator
                Overdrive
```

**Drive at minimum plus Cut flat gives you Body alone.** DRIVE is a parallel blend with
the clean signal as well as an input gain, so at DRIVE 0 the Damage stage is bit-identical
to its input — all three algorithms then produce exactly the same output, which the test
suite verifies to zero difference. At CUT centre each of the three EQ sections is an exact
identity. So with Drive 0 and Cut flat, what you hear is the resonator and nothing else.

## DAMAGE

**TYPE** picks one of three algorithms. **DRIVE** is the master input drive. The two
character knobs keep the names **SHAPE** and **GRAIN** whatever the type, but what they do
changes — and the screen tells you, because the value text carries the active function
("Focus +40", "Squeeze 55", "Bite 70").

| TYPE | SHAPE | GRAIN |
|---|---|---|
| **Tube** | **Focus** — a mid contour applied *before* the waveshaper and inverted *after* it, so it decides which band drives the shaper hardest rather than acting as a tone control. An RBJ peaking cut is the exact reciprocal of the boost, so the pair is magnitude-flat. Centre is flat. | **Heat** — DC bias into the shaper. Asymmetry generates *even* harmonics, which is the fat kind of distortion rather than the harsh kind. A DC blocker follows. |
| **Tape** | **Reel** — tape-speed macro. Turn down for more HF rolloff, more low-mid head bump and more flutter; turn up for flatter, brighter and tighter. | **Squeeze** — level-dependent compression that rounds the transient before the shaper. |
| **Overdrive** | **Slope** — broad brightness tilt of the clipped signal, a shelf pair at 800 Hz, after the clipper. | **Bite** — narrow upper-mid/HF emphasis at 3.2 kHz, after the clipper. |

Tube and Tape use the SDK's `fastTanh`; Overdrive uses `softClip`, the cubic curve with a
harder knee. The HF loss on Tape sits *after* its saturation, so the harmonics get rolled
off too, which is what makes it read as tape rather than as an EQ.

Tape's flutter modulates a delay line, and its depth scales with DRIVE — flutter is part
of Damage, so DRIVE 0 has to leave the signal completely alone.

## KEEP LO — protecting the low end

Turn **Keep Lo** up and everything below it bypasses the entire chain and rejoins clean,
after the auto-makeup. Point it just above your 808's fundamental (they usually sit at
30–60 Hz, so 70–100 Hz is the useful region) and the sub comes through untouched while
everything above it still gets cooked. Fully anticlockwise reads **Off**.

It is a **Linkwitz-Riley 4th-order crossover**, not the obvious `high = x − lowpass(x)`
subtractive split. That matters: a subtractive split reconstructs the input exactly, but
the high band it produces is only a *6 dB/oct* highpass however steep you make the
lowpass. Measured on this plugin, a 2-pole version at a 150 Hz crossover still passed
55 Hz into the distortion at −3.7 dB, so the "kept" sub came out as a blend of clean and
processed. LR4 gives 24 dB/oct on both sides — the same leak is about −35 dB — and its two
bands sum to an allpass, so the magnitude stays flat across the crossover with no polarity
trickery. The clean band is DC-blocked at 8 Hz, because bypassing the chain also bypasses
Damage's DC blocker and the Body's bandpass.

**It does not and cannot follow the notes you play.** Effects on the MPC receive no MIDI
at all, so the plugin has no idea what is playing; the alternative would be pitch-tracking
the audio, which on a drum bus is unreliable and costs latency. A fixed crossover you place
by ear is both simpler and more predictable.

## TAME — the ladder lowpass

A **zero-delay-feedback Moog-style ladder**, 24 dB/oct, with a fixed resonance of 0.9
against the 4.0 that would make a ladder self-oscillate. Use it when a hard drive gets
glarey: it takes the fizz off without simply dulling the sound, because the feedback path
is saturated, so the resonance and the distortion interact the way they do in an amp
rather than sitting on top of each other. Fully clockwise reads **Open** and bypasses the
stage outright, so "off" is exactly transparent.

It sits **immediately after Damage**, not after the Body. The Body is a bandpass bank, so
by the time the signal leaves it there is frequently no high end left to remove — a ladder
after it measured 1 dB of effect where the same filter before it measures 21 dB. Here it
catches the fizz where the fizz is made, which is also the classic amp order (drive → tone
→ cabinet), and it is still ahead of the auto-makeup and the clipper.

## CUT

Three transparent-at-centre bands: **Cut Lo** (low shelf, 100 Hz), **Cut Mid** (bell,
900 Hz, Q 0.9), **Cut Hi** (high shelf, 4.5 kHz), each ±12 dB.

## BODY

The resonator from ResoBody, **DSP unchanged** — same mode table, same Q law, same clamps,
same input-driven envelope follower. Only the control names changed, plus two removals:
its internal dry/wet (the unit's global MIX does that now) and its wet-path soft clip
(there is one safety clipper at the end and clips must not be stacked).

| Control | Was | What it does |
|---|---|---|
| **Chamber** | Type | The modal set, A–E. A Tight (72–405 Hz, small sealed cabinet) through E Wide (210–2150 Hz, open and bright). Ratios are inharmonic on purpose: integer ratios sound like a chord, not an object. |
| **Bore** | Scale | Body size. Multiplies all four mode frequencies, 0.25×–4.00×. Centre-detented at 1.00×. |
| **Ring** | Reso | Resonance intensity. Scales all four Qs together, exponentially, Q 0.7 to Q 14. |
| **Bloom** | Auto | Envelope depth. Pushes Ring up on transients (and Bore up by 18 % as much) so the body blooms on each hit. |
| **Rise** | Attack | Bloom attack, 0.3–100 ms. |
| **Fall** | Release | Bloom release, 10–1000 ms. |

Only the band above **Keep Lo** reaches the Body at all.

The Body runs **100 % wet in series**: it is a bank of four resonant bands, so on its own
it passes only those bands. MIX is what puts the direct signal back.

## MIX, OUTPUT, AUTO-MAKEUP and the safety clipper

- **MIX** is a single global dry/wet, default 60 %, and it crossfades **band for band**:
  the dry side of the fade is the dry signal's *high* band, not the full-range input, and
  the kept low end sits outside the fade at unity. That is not a detail — LR4's two bands
  sum to an allpass, which at the crossover is 180° from the unfiltered input, so
  crossfading the clean low against a full-range dry *subtracts* there. The first version
  of this measured a **22.5 dB notch** at 150 Hz with Keep Lo at 150 Hz and Mix at 60 %.
  Crossfading band-for-band keeps both sides of the fade in the same phase (now 1.4 dB
  from the Mix 100 reading) and makes the kept sub independent of MIX, which is what
  "untouched" ought to mean. There is a regression test for it.
  Worth knowing: because MIX blends the *unprocessed* signal back in, lowering it dilutes
  the Damage and the Cut as well as filling in the Body's missing broadband energy. For
  full-series colouration leave it at 100 % and use Drive and Ring for taste; pull it down
  when you want the whole unit backed off, drum-bus style.
- **Auto-makeup** (toggle, default on) measures the input and the wet signal with a
  ~300 ms RMS average and applies a smoothed gain so the wet lands near input level,
  bounded to ±12 dB. It only equalises *level* — the Damage and clipper harmonics are
  already baked into the waveform and survive it untouched. It holds its gain through
  silence instead of winding up on noise, and it sits before the clipper so it can never
  push the signal past the ceiling.
- **Output** is a trim, −24 to +12 dB. It is parameter 0, because the MPC nudges parameter
  0 to 1.0/0.0 and back on every prepare and a smoothed gain is the one thing that
  tolerates being wiggled.
- **The safety soft-clip** is one always-on stage at the very end: unity below its knee,
  then a tanh bending asymptotically onto a fixed ceiling of **−0.5 dBFS**, with the knee
  6 dB below that. The slope at the knee is exactly 1, so there is no corner to hear. It
  is progressive — the harder you drive, the more the peaks flatten, which is part of the
  tone — and the ceiling is arithmetic, not an aspiration.

## Stability

This runs inside the MPC application with no sandbox, so a blow-up is a restarted MPC, not
a bad noise.

- The Body's Q is hard-clamped to 20 after modulation. The TPT filter only self-oscillates
  at `k = 1/Q = 0`, so a finite ceiling rules it out arithmetically. Its resonance input is
  clamped again at 0.985.
- Damage post-trims by up to −10 dB as Drive rises, keeping headroom going into the
  resonators. A hot distorted signal into high-Q bands is exactly what spikes.
- Every filter coefficient is range-checked and NaN-checked before use; anything
  non-finite falls back to an inaudible setting.
- Each stage reports a non-finite result, and the plugin then clears all filter state for
  that block — floats only, no allocation, realtime safe.
- Denormals: the framework sets flush-to-zero around `process()`, and the follower states,
  the RMS detectors and the wet sums are flushed explicitly as the long-tail paths.

## Realtime behaviour

No allocation, locks, logging or file access in `process()`. Parameters are snapshotted
once per block. Biquad coefficients (sin/cos/pow) are computed per block; the Body's SVF
coefficients at control rate, every 16 samples; waveshaper gains per sample from smoothers.
Drive, Shape, Bore, Ring, the three Cut bands, Mix, Output and the makeup gain are all
smoothed. The Body's control-rate counter runs across block boundaries, so output does not
depend on the host's block size.

**Latency is 64 samples (1.45 ms)** and is reported to the host. The wet path always runs
through the flutter delay line whatever the algorithm, so switching TYPE never changes
latency, and the dry path is delayed to match so MIX stays phase-coherent.

## CPU

Measured on a desktop x86 CPU at 128 frames / 44.1 kHz: **0.49–0.67 % of the real-time
budget per instance**; two instances in series with both maxed, on the most expensive
algorithm (Tape) with the crossover and the ladder both active, **1.70 %**. The SDK's own estimate is that the MPC's ARM core is
15–25× slower, which puts one instance in the mid single digits of a percent on a 32-bit
MPC X / Live / One, with room for the two instances the brief asked for. Cost is flat:
there are no data-dependent branches in the audio path.

## Test results

Both paths pass with zero failures and zero warnings.

`tools/host-sim` (the MPC host contract):

- **20 passed, 0 warnings, 0 failures** at 32, 128 and 512 frames, plus a 30 s run
- In-place processing matches out-of-place; state round-trip exact; latency reported as 64
- No NaN/Inf, no denormals, no clipping; passes silence

`test/drum_test.cpp` (the DSP, on a synthesised kick/snare/hat loop):

- **38 checks, 0 failures.** Every scenario tests NaN/Inf, denormals, DC and tail decay
- **Drive 100 % + Ring 100 % + Bloom 100 % simultaneously**, on all three algorithms at
  Shape −100/0/+100 with Grain 100: stable, peaks −1.0 to −2.6 dBFS
- Everything maxed with all three Cut bands at +12 dB, a 0 dBFS input and Output +12 dB,
  at both Bore extremes: stable
- **Ceiling guarantee**: 0 samples above −0.5 dBFS with the clipper on; with it bypassed
  the same settings peak at +13.6 dBFS, so it is absorbing 14.1 dB
- **Drive 0 is a true bypass**: Tube, Tape and Overdrive outputs differ by exactly 0.0
- **Mix 0 is the dry signal**: matches the input delayed by 64 samples, difference exactly 0.0
- **Auto-makeup** lands within 1.8 dB of input level and is measurably closer than with it off
- **Parameter slam**: all 16 parameters randomised between every block — stable
- **Self-oscillation proof**: impulse then 10 s of silence at max Drive/Ring/Bloom decays
  to −240 dBFS (true zero) on all three algorithms, and again with the ladder at its
  lowest cutoff (its most resonant setting)
- **Keep Lo**: at Drive 100 the sub arriving at the output correlates **0.964** with the
  original sub (0.793 with Keep Lo off) and its level lands 0.08 dB from the input's. With
  the resonator alone it holds the sub 6.4 dB closer than without
- **Tame**: removes 21 dB above 4 kHz at a 1 kHz cutoff, staying finite, under the ceiling
  and DC-free
- **No crossover notch**: with Keep Lo at 150 Hz, the response at 150 Hz moves only 1.1 dB
  between Mix 100 % and Mix 60 %, and the kept sub at 45 Hz moves 0.02 dB

### Known limitations

- **Not tested on hardware.** Everything above is the simulator and the offline harness on
  a desktop CPU. The MPC CPU figures are scaled estimates, not device measurements.
- **The SHAPE/GRAIN sublabel refreshes on redraw**, not instantly when you change TYPE. The
  value text is always correct when the MPC asks for it, but the plugin does not force a
  display update — doing that means calling a host callback from the audio thread, which
  is not worth the risk. Changing TYPE is itself a screen interaction, so in practice it
  redraws.
- **"Overdrive" slightly overruns its segment button.** The caption is centred over an
  80 px segment and the word is wider at the kit's caption size. Labels do not intercept
  touches so it is cosmetic, but it wants checking on a real screen.
- **Switching TYPE clicks.** Each algorithm uses the filter slots for different jobs, so
  their state is cleared on a switch. A discrete algorithm change is audible by nature;
  this keeps it to one click rather than a ring-out of stale state.
- **Tape flutter combs slightly against the dry path at partial MIX.** The nominal delay is
  compensated exactly, but the flutter modulation around it is not — that is what flutter
  is. It is subtle at the depths used here.
- **MIX dilutes Damage as well as Body**, by design (see above).
- **Keep Lo shifts the sub's phase.** LR4 is flat in magnitude but not in phase, so the
  kept low end is time-shifted a few milliseconds relative to a completely unprocessed
  copy. Against the plugin's own dry path this is consistent and inaudible; if you run
  Drum Infurno in parallel with an untreated copy of the same drums outside the plugin,
  expect some comb filtering in the crossover region.
- **Keep Lo at its minimum bypasses the crossover outright**, so "Off" is a true bypass
  and Mix 0 then returns the input bit-for-bit. Engaged, the dry side of MIX is the dry
  high band, which is magnitude-flat but phase-shifted, so Mix 0 is no longer bit-exact —
  audibly identical, not numerically identical.
- **The clean low still passes through Output and the safety clipper.** It has to: the
  clipper is the last line of defence and must see the sum. Drive hard enough and the
  clipper will act on the sub along with everything else.

## Testing it yourself

```sh
python3 tools/build.py plugins/DrumInfurno
.sdk-tools/host_sim plugins/DrumInfurno/build/native/drum-infurno.so test \
    --seconds 4 --wav plugins/DrumInfurno/build/test.wav

c++ -std=c++17 -O2 -I framework -I plugins/DrumInfurno/src \
    -o /tmp/di_test plugins/DrumInfurno/test/drum_test.cpp
/tmp/di_test /tmp/di-wav        # a WAV per scenario
```

The harness drives the plugin class through the same entry points the framework's
dispatcher uses, with input and output buffers aliased the way the MPC aliases them.
`build.py` compiles only `src/*.cpp` and `package.py` ships only the build output, so it
never reaches the MPC.

## Installing

Requires **MPC OS 3.10.1 or later** with **Preferences → Enhancements (Beta) → Enable
Enhancements** and **External Plugin Support** both on.

Copy `build/INFUR - VST - Drum Infurno/` into a folder named exactly `VST2 Plugins` at the
root of a drive, then find it under **External Plugins** in an insert slot. The MPC only
scans for plugins when its application starts, so restart it after copying.

## Licence

MIT. All DSP is original or built from the SDK's own MIT-licensed `framework/dsp/`
primitives (`svf.h`, `biquad.h`, `onepole.h`, `delay_line.h`, `lfo.h`, `smoother.h`,
`math.h`); no third-party code is included. The state-variable filter in the SDK is the
Cytomic / Andy Simper TPT design.

The algorithms here are independent implementations written from a description of what
each kind of saturation and body resonance does. No code, data or measurements were taken
from any other manufacturer's product. Not affiliated with or endorsed by inMusic/Akai or
any other company whose hardware inspired it.
