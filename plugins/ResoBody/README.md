# Reason 2 Scream

**INFUR — insert effect for Akai MPC standalone**

A dynamic modal resonator for drum colouration: a parallel bank of four resonant bands
that gives a drum a physical body to ring in, with an envelope follower that makes that
body bloom on every hit. A standalone re-creation of the Body section of a classic
rack distortion, as an MPC insert.

> **Names.** The plugin appears on the MPC as **Reason 2 Scream** by **INFUR**
> (`uid` `IYkr`, folder `INFUR - VST - Reason 2 Scream`). The project folder, source
> file and C++ class are called **ResoBody** — that is the engine's internal name, and
> it is what you type in build commands. Nothing on the device shows it.

This is deliberately **only** the resonant body. There is no distortion and no bitcrush
in it, because your MPC already has both on board and they are good. Reason 2 Scream is the
piece that is missing between them.

## Where it goes in the chain

```
drum bus ──► MPC distortion ──► Reason 2 Scream ──► MPC bitcrush ──► out
             (grit, harmonics)   (body, ring)  (grain, aliasing)
```

Put it **after** a native distortion: the distortion generates the harmonics, Reason 2 Scream
decides which of them ring and for how long. Feeding an already-dirty signal into the
resonator is the whole point — a clean sine has almost nothing for the bank to find,
while a distorted kick has energy everywhere and the body picks its modes out of it.

Put it **before** bitcrush, so the crusher grains the resonance rather than the
resonator ringing on an already-quantised signal.

It works on a drum bus, on single pads, and on anything percussive. It is not a reverb:
the tail is tens to hundreds of milliseconds, the length of a body ringing, not a room.

## Controls

One page, `BODY`. The left column of four is on Q-Link knobs 13/9/5/1 (top to bottom of
the leftmost bank), so **Scale, Reso, Auto and Mix** are the four knobs under your hand
while a loop plays. The second column holds the setup controls.

| Control | Range | Default | What it does |
|---|---|---|---|
| **Scale** | 0.25× – 4.00× | 1.00× | Body size. Multiplies all four mode frequencies at once. Down = bigger, lower, boomier; up = smaller, tighter, brighter. The knob is centre-detented: 1.00× sits exactly at twelve o'clock. |
| **Reso** | 0 – 100 % | 45 % | Global resonance. Scales all four Qs together, exponentially, from a wide colouration (Q 0.7) to a clear ring (Q 14). Longer ring, not louder — see *Gain staging*. |
| **Auto** | 0 – 100 % | 35 % | Envelope-follower depth. Pushes Reso upward on transients (and Scale up by 18 % as much), so the body blooms on the hit and settles between hits. At 0 % the body is static. |
| **Mix** | 0 – 100 % | 50 % | Dry/wet. 0 % is a clean bypass of the resonator. |
| **Body Type** | A – E | B Cab | The modal fingerprint: which four frequencies, at what relative levels. See below. |
| **Atk** | 0.3 – 100 ms | 3.0 ms | Follower attack. Short = the bloom lands with the transient. Long = the body swells slightly behind the hit. |
| **Rel** | 10 – 1000 ms | 150 ms | Follower release. Short = the body closes between hits (tight, rhythmic). Long = it stays open across a fill. |
| **Output** | −24 – +12 dB | 0.0 dB | Output trim, after the mix. |

### Body types

Each type is a fixed set of four frequency ratios, relative gains and Q weights — the
character of the plugin lives in this table. The ratios are deliberately **inharmonic**,
because real boxes, cones and shells do not resonate in integer multiples; integer
ratios would sound like a chord instead of an object. Frequencies shown at Scale 1.00×:

| Type | Modes at 1.00× | Character |
|---|---|---|
| **A Tight** | 72 / 148 / 242 / 405 Hz | Small sealed cabinet. Low, thumpy, closed. Kicks and 808s. |
| **B Cab** | 100 / 205 / 342 / 585 Hz | Bigger cabinet. The general-purpose body, and the default. |
| **C Wood** | 128 / 272 / 510 / 860 Hz | Hollow wooden shell. Snares, rims, claps. |
| **D Bright** | 165 / 395 / 740 / 1260 Hz | Metallic. Toms, rims, anything that should ping. |
| **E Wide** | 210 / 530 / 1120 / 2150 Hz | Open and wide. Hats, shakers, top end. |

A is tight and low; E is wide and bright. Scale moves any of them up or down by two
octaves either way, so there is a lot of overlap — the types set the *shape* of the body,
Scale sets its *size*.

### Factory programs

8 programs, covering distinct jobs rather than variations of one sound:

`Init` · `Kick Weight` · `Snare Crack` · `Hat Tighten` · `Room Bloom` · `Metal Ring` ·
`808 Body` · `Full Resonator`

## How it works

```
         ┌───────────────────────── dry ─────────────────────────┐
         │                                                       │
in ──────┼──► 4 × resonant band (parallel) ──► Σ ──► tanh ───► wet
         │        ▲             ▲                                │
         │     centre         Q (Reso)                           ▼
         │     (Scale)          ▲                          dry/wet Mix ──► Output ──► out
         │                      │
         └──► envelope follower (INPUT ONLY) ──► Auto ───┘
              Atk / Rel
```

Four Cytomic/Simper TPT state-variable filters per channel (`framework/dsp/svf.h`) in
parallel, taking their bandpass output. The TPT topology is stable under fast cutoff and
Q modulation, which matters because Auto moves both on every transient; a biquad bank
would need `sin`/`cos`/`pow` per update and could misbehave when swept hard.

**The follower watches the input, never the resonator output.** A follower on the output
is a feedback loop — more ring raises the envelope, which raises Q, which raises the ring.
Driving it from the input makes the modulation strictly feed-forward, so it cannot run
away whatever the knobs are set to. This is the single most important stability decision
in the plugin, and it is why Reso and Auto can both be at 100 % safely.

### Gain staging

The SVF's bandpass peaks at Q, so a raw bank would be a +26 dB surprise at high Reso.
Each band is therefore normalised to unity peak gain, and then given a resonance makeup
of `sqrt(Q)` **hard-limited to +9.5 dB** — a narrow band captures so little of a drum's
broadband energy that without it the body vanishes as you turn Reso up. The two together
keep the wet level inside about a 10 dB window across the whole Reso range, and the
ceiling is a compile-time constant that no knob can influence.

A gentle `tanh` soft clip sits on the wet path after the bank, before the mix, to catch
resonant peaks. At normal levels it is effectively unity gain; it is a safety net, not a
distortion stage.

### Stability

This plugin runs inside the MPC application with no sandbox, so a blow-up is not a bad
sound, it is a restarted MPC. The measures taken:

- **Q is hard-clamped to 20** after every modulation. The TPT filter only self-oscillates
  at `k = 1/Q = 0`, i.e. infinite Q, so a finite ceiling makes self-oscillation
  arithmetically impossible. The filter's own 0–1 resonance input is independently
  clamped to 0.985 as a second belt.
- Centre frequencies are clamped to 20 Hz – 16 kHz before they reach a filter.
- Every coefficient is NaN-checked; anything non-finite falls back to an inaudible
  1 kHz / zero-gain setting instead of an unknown state.
- The wet sum is checked per sample. If a non-finite value ever appeared, the bank is
  zeroed at the end of that block (clearing floats, no allocation).
- Denormals: the framework sets flush-to-zero around `process()`, and the follower state
  and wet sums are flushed explicitly because they are the long-tail paths.

### Realtime behaviour

No allocation, no locks, no logging and no file access in `process()`. Parameters are
read once per block into a snapshot; filter coefficients are recomputed at control rate
(every 16 samples) because they cost a `tan()`. Scale is smoothed in log2 space so a
sweep is even in pitch rather than in Hz; Reso, Mix and Output are smoothed too, so no
knob move clicks.

The control-rate counter runs **across** block boundaries, so the output is identical
whether the MPC delivers 32, 128 or 512 frames per call — verified, see below.

Parameter 0 is **Output**, a smoothed gain. The MPC nudges parameter 0 to 1.0/0.0 and
back every time it prepares a plugin, so parameter 0 has to be something that tolerates
being wiggled; a smoothed trim does, and `parameterChanged()` is not implemented at all.

## CPU

Measured by `host_sim` and by the offline harness on a desktop x86 CPU, at 128 frames /
44.1 kHz: **0.17 – 0.31 % of the real-time budget per instance**, worst block 0.6 – 2.2 %.
Two instances in series: 0.35 – 0.62 %.

The MPC's ARM core is roughly 15–25× slower than that (the SDK's own estimate), which
puts one instance in the low single digits of a percent on a 32-bit MPC X / Live / One
and leaves plenty of room for the two instances you asked about, or more. The cost is
flat: there are no data-dependent branches in the audio path, so a busy drum loop costs
the same as silence.

## Test results

Both test paths pass with zero failures and zero warnings.

`tools/host-sim` (the MPC host contract — load sequence, ABI, state, in-place, block sizes):

- **20 passed, 0 warnings, 0 failures** at 128 frames; also run at 32 and 512 frames, and
  for 30 s at 32 frames, all passing
- In-place processing matches out-of-place — the effect reads its inputs before writing
  its outputs, which is how the MPC calls it
- State round-trip: all 8 parameters restored, re-saved chunk byte-identical, invalid
  chunk data tolerated without crashing
- Main-pass peak identical (−6.34 dBFS) at 32, 128 and 512 frames — block-size independent
- No NaN/Inf, no denormals, no clipping; passes silence

`test/drum_test.cpp` (the DSP, on a synthesised kick/snare/hat loop — see *Testing* below):

- **23 scenarios, 0 failures.** Every scenario checks NaN/Inf, denormals, DC offset and
  that the tail actually decays
- **Max Reso + max Auto simultaneously, on all five body types**: stable, peak −8.3 to
  −16.4 dBFS, no DC, tails to silence
- Same with Scale at both extremes (0.25× and 4.00×), with Atk/Rel at their minimums, and
  with the drum loop driven at 0 dBFS: all stable, worst peak −3.5 dBFS (3.5 dB of headroom)
- **Self-oscillation proof**: one impulse then 10 s of silence at max Reso + max Auto +
  lowest Scale decays to −240 dBFS (true zero) on all five types, with no envelope growth
  after the initial peak
- **Parameter slam**: every parameter randomised between every block — stable and finite
- DC offset is below 0.0005 everywhere

### Known limitations

- **Body Type is a menu, not a 5-way switch.** The SDK's UI kit fits at most 4 captioned
  segments in a slot (`MAX_SWITCH_CHOICES = 4` in `tools/gen_ui.py`), and there are five
  body types. It is therefore the MPC's generic value bar with a menu overlay: turn the
  Q-Link, or double-tap for the list. A hand-written skin could draw a real 5-way
  switch; this one is generated.
- **Half the page is empty.** The eight controls occupy the two left-hand columns,
  because `gen_ui.py` banks the Q-Links by column and draws the focus rectangle around a
  full column. Keeping each bank a complete, coherent column of four is what makes the
  knobs land where you expect; spreading eight controls over four columns would look
  fuller and feel worse.
- **No tempo sync.** Nothing here is periodic, so there is nothing to sync.
- **Not tested on hardware yet.** Everything above is the simulator and the offline
  harness on a desktop CPU. The CPU figures for the MPC are scaled estimates, not
  measurements on the device.
- The wet path is mono-compatible but not stereo-widening: the two channels run
  independent followers and filter states, so a stereo input keeps its image, but the
  plugin does not create width.

## Testing it yourself

```sh
# the MPC host contract
python3 tools/build.py plugins/ResoBody
.sdk-tools/host_sim plugins/ResoBody/build/native/reason-2-scream.so test \
    --seconds 4 --wav plugins/ResoBody/build/test.wav

# the DSP, on a drum loop, at hostile settings (writes a WAV per scenario)
c++ -std=c++17 -O2 -I framework -I plugins/ResoBody/src \
    -o /tmp/drum_test plugins/ResoBody/test/drum_test.cpp
/tmp/drum_test /tmp/resobody-wav
```

`test/drum_test.cpp` exists because `host_sim` feeds effects a sine plus a noise burst
and has no way to pin a parameter at a chosen value — neither of which can show how this
plugin behaves on drum transients with Reso and Auto both at the top. It drives the
plugin class through the same entry points the framework's dispatcher uses, with the
input and output buffers aliased the way the MPC aliases them. `build.py` compiles only
`src/*.cpp` and `package.py` ships only the build output, so it never reaches the MPC.

## Installing

Requires **MPC OS 3.10.1 or later**, with **Preferences → Enhancements (Beta) → Enable
Enhancements** and **External Plugin Support** both switched on. Without both switches
the plugin stays hidden.

Copy `build/INFUR - VST - Reason 2 Scream/` into a folder named exactly `VST2 Plugins` at the
root of a USB drive, SD card or the internal drive, then find it under **External
Plugins** in an insert slot. The MPC only scans for plugins when its application starts,
so restart it after copying.

## Licence

MIT. All DSP is original or built from the SDK's own MIT-licensed `framework/dsp/`
primitives (`svf.h`, `smoother.h`, `math.h`); no third-party code is included. The
state-variable filter in the SDK is the Cytomic / Andy Simper TPT design.

### On the name

The DSP here is an independent implementation written from the description of what a
modal body section does. No code, no data and no measurements were taken from any other
manufacturer's product, and nothing in this repository is derived from one.

"Reason" and "Scream" are product names of Reason Studios, and neither they nor
inMusic/Akai are affiliated with or endorse this plugin. The name was chosen as a
personal homage for private use. **If you ever decide to share or sell this, rename it
first** — and do that before you save MPC projects with it, because the `uid` and the
plugin name are what projects use to find the plugin again. Changing either one later
makes existing projects lose it.
