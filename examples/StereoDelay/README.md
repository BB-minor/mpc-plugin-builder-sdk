# Stereo Delay (example)

A stereo delay effect for the Akai MPC. Effect, 2 in / 2 out.

| Parameter | Range | Notes |
|---|---|---|
| Sync | Off / On | On: delay time follows the MPC tempo |
| Division | 1/64 ... 2/1 incl. triplets and dotted | shown only when Sync is On |
| Time | 1 .. 2000 ms | shown only when Sync is Off |
| Feedback | 0 .. 100 % | soft-clipped inside the loop so it can never blow up |
| Low Cut / High Cut | Hz | one-pole filters in the feedback path (darker, thinner repeats) |
| Ping Pong | Off / On | repeats alternate left / right |
| Mix | 0 .. 100 % | dry/wet |

## Files

- `plugin.json` - manifest, `uid` "MxSd" equals `MPC_FOURCC('M','x','S','d')`.
- `src/StereoDelay.cpp` - the plugin, ~130 lines.
- `ui.json` - one page; Division and Time share the "Sync" condition via `visibleWhen`.

## How the source is structured

1. **Effect info**: `isInstrument = false`, 2 inputs, 2 outputs. Effects never receive MIDI on the MPC.
2. **Allocation only in `prepare()`**: `DelayLine::prepare(sampleRate * 4 s)` is the only place memory is allocated. `process()` only reads and writes.
3. **Tempo**: `beatsToSeconds(divisionBeats(getInt(kDivision)), ctx.transport.bpm)`; the framework fills `bpm` with 120 when the host does not report a tempo.
4. **Smoothing**: delay time (80 ms), mix and feedback (20 ms) go through `Smoother` so turning knobs never clicks; the delay time glide also gives the familiar tape pitch-wobble.
5. **In-place safe**: the MPC passes the same buffers as input and output. Each frame reads `inL[i]`/`inR[i]` into locals first, then writes `outL[i]`/`outR[i]`.
6. **Feedback path**: read -> low cut -> high cut -> `softClip` -> write back with feedback. Ping-pong writes the mono input into the left line, feeds left's output into the right line and right's output back into the left.

Nothing beyond the parameters is saved: the framework's default state (all parameter values) is enough.
