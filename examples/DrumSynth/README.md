# Drum Synth (example)

An eight-voice analogue-style drum synthesizer with two stereo output pairs. Instrument, 0 in / 4 out, MIDI in.

| Note | Drum | Recipe |
|---|---|---|
| 36 | Kick | sine with a fast pitch sweep, tanh drive, noise click |
| 37 | Snare | two sines (body) + highpassed noise |
| 38 | Clap | bandpassed noise, three quick bursts then a tail |
| 39 | Closed Hat | six inharmonic squares -> highpass, short decay; chokes the open hat |
| 40 | Open Hat | same, long decay |
| 41 | Low Tom | sine with a slower pitch sweep |
| 42 | High Tom | same, higher |
| 43 | Perc | two squares -> bandpass (cowbell-like) |

Notes outside 36..43 wrap onto the same eight sounds (44 = kick again), so the kit plays from any pad bank or keyboard.

Each drum has **Tune** (semitones), **Decay** (time to -60 dB) and **Level**. Global: **Routing** and **Master**.

## Multi-out

`numOutputs = 4`. The MPC shows these as two stereo pairs (Main = outputs 1-2, Aux = outputs 3-4) that can be routed to separate mixer channels. The "Routing" parameter chooses what goes to Aux:

- **All Main** - everything on outputs 1-2 (Aux silent)
- **Hats>Aux** - closed and open hat on 3-4, the rest on 1-2
- **Split** - kick, snare, clap on 1-2; hats, toms, perc on 3-4

If a host only provides two outputs the plugin falls back to Main (`ctx.numOutputs` is checked).

## Files

- `plugin.json` - `numOutputs` 4, `uid` "MxDr" equals `MPC_FOURCC('M','x','D','r')`.
- `src/DrumSynth.cpp` - the plugin, ~300 lines.
- `ui.json` - "KIT A" (kick, snare, clap, closed hat) and "KIT B" (open hat, toms, perc); Master and Routing appear on both pages.

## How the source is structured

1. **Parameters in a loop**: three parameters per drum are added in a loop over the drum names; `paramIndex(drum, kDecay)` computes the index back. Global parameters follow.
2. **`struct DrumVoice`**: one struct serves all eight sounds; `trigger()` sets up decay coefficients, base frequency and filters per type, `process()` is a `switch` on the type. Envelopes are plain exponential decays (`amp *= coef`), the cheapest envelope there is.
3. **One-shots**: note-offs are ignored; a voice retriggers on each note-on; CC 120 (all sound off) kills everything.
4. **Rendering**: each active voice is rendered into the bus its routing selects, panned with `equalPowerPan`, then the smoothed master gain is applied to all four outputs.
5. **Programs**: "Clean Kit" (defaults) and "Boom Kit".
