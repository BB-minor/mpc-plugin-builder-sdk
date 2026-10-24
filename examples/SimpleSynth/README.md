# Simple Synth (example)

An 8-voice subtractive synthesizer for the Akai MPC. Instrument, 0 in / 2 out, MIDI in.

| | |
|---|---|
| Oscillators | 2 x PolyBLEP (sine / triangle / saw / square), octave, semitone, cent detune, pulse width |
| Filter | Cytomic SVF (LP / BP / HP / notch), resonance, drive, key tracking, ADSR with +/- amount |
| Amp | ADSR, velocity sensitivity, master volume |
| Modulation | LFO (5 shapes) to pitch and filter, free Hz or tempo-synced to the MPC sequencer, mod wheel vibrato |
| Performance | glide (portamento), pitch bend with adjustable range, sustain pedal, voice stealing |
| Programs | Init Saw, Fat Bass, Soft Pad, Pluck |

## Files

- `plugin.json` - manifest. `uid` "MxSy" equals `MPC_FOURCC('M','x','S','y')` in the source.
- `src/SimpleSynth.cpp` - the whole plugin (one file, ~330 lines).
- `ui.json` - four screen pages: OSC, FILTER, AMP, LFO. The LFO page shows "LFO Rate" only when "LFO Sync" is Off and "LFO Div" only when it is On (`visibleWhen`).

Build and test with the SDK tools:

```
python tools/build.py examples/SimpleSynth
```

## How the source is structured

1. **Parameters**: an `enum Params { kOsc1Wave, ... kNumParams }` and the constructor adds them in the same order, so `get(kCutoff)` always hits the right one. A `mpc::log()` check warns if the two drift apart.
2. **Programs**: a small lambda copies every parameter's default into a vector and overrides a few values, then calls `addProgram()`. Nothing else is needed; the framework stores and recalls them.
3. **Voice**: a `struct Voice` holds two `PolyBlepOsc`, one `SVF`, two `ADSR`, a `Smoother` for glide and the note/velocity. `VoiceAllocator<8>` decides which voice to use (oldest released voice first), handles the sustain pedal and pitch bend.
4. **Sample-accurate MIDI**: `process()` walks the events in order, renders audio up to each event's `sampleOffset`, then applies the event.
5. **Control rate**: pitch, cutoff modulation and the filter's `tan()` run every 16 samples; envelopes, oscillators and the filter core run every sample. This keeps CPU low without audible stepping.
6. **Tempo sync**: when "LFO Sync" is on, `lfo.setTempoSync(ctx.transport.bpm, divisionBeats(div))` and, while the sequencer plays, `lfo.syncToPpq(ctx.transport.ppqPos)` locks the LFO phase to the song.
7. **No allocation in process()**: the only scratch buffer is a fixed 64-sample array; longer blocks are rendered in chunks.

See `docs/dsp-cookbook.md` for the general recipe this follows.
