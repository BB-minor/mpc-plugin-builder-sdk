# Getting started

You will have a plugin running on your MPC in about half an hour. You need: a Mac, Windows or Linux computer with Python 3.9+ and an AI coding agent (Claude Code or OpenAI Codex), an MPC standalone running **MPC OS 3.10.1 or later** (earlier versions cannot load third-party plugins), and a USB stick or SD card.

## 1. Get the SDK

```
git clone https://github.com/<org>/mpc-plugin-sdk.git
cd mpc-plugin-sdk
```

Nothing else to install. The build script downloads the Zig cross compiler on first use (about 50 MB).

## 2. Check that the examples build

```
python tools/build.py examples/StereoDelay
```

You should see a native build, two ARM builds, generated skins and a validator report ending in OK. The output is `examples/StereoDelay/build/MPC SDK Examples - VST - Stereo Delay/`.

## 3. Try an example on the MPC

1. Copy that folder into a folder called `VST2 Plugins` at the root of your USB stick or SD card (create it if it is not there).
2. Put the drive in the MPC.
3. Enable VST plugins on the device: Preferences → Enhancements (Beta) → turn on **Enable Enhancements** and **VST2 Support**. This switch only exists on MPC OS 3.10.1 and later; update the MPC first if you do not see it. Read the warning: third-party plugins can crash the MPC; save often.
4. Add an insert effect to a track; the browser lists **External VST Plugins → Stereo Delay**. Play something. Press the function keys to switch pages; touch a control to grab the knobs.

## 4. Make your own

Open the SDK folder in your AI agent and describe what you want. Examples of prompts that work well:

- "Make a monophonic acid bass synth: saw/square oscillator, resonant low-pass filter with envelope and accent, glide, a drive stage, and 8 presets."
- "Build a tape-style delay effect with tempo sync, feedback filter, wow and flutter, and a ping-pong mode."
- "Create a 4-voice drum synth for notes 36–39: kick, snare, closed hat, clap, each with tune, decay and level, plus a master drive. Put each drum on its own page."
- "An instrument that is a generative sequencer: picks notes from a chosen scale on each 16th note while the MPC is playing, with density and octave range controls, and plays them on a simple pluck synth."
- "A stereo widener and bass mono effect with a crossover frequency."

The agent scaffolds a project under `plugins/`, writes the code and UI, builds, and tests. When it says it is done, the output folder is `plugins/<Name>/build/<Manufacturer> - VST - <Name>/`; copy it to `VST2 Plugins` as in step 3.

## 5. Iterate

Tell the agent what you hear. "The filter is too dark", "add an LFO to pitch", "put the envelope controls on their own page". Rebuild, recopy, retry. On the MPC, remove and re-add the plugin after replacing its folder (restart if the UI changed).

## 6. Share it

```
python tools/package.py plugins/<Name>
```

gives you a zip with install instructions. Attach it to a GitHub release or post it in the community.

## Doing it without an AI agent

The project layout is small enough to write by hand: copy `templates/effect` or `templates/instrument` to `plugins/<Name>`, edit `plugin.json`, `src/*.cpp` and `ui.json`, build. `docs/` has everything.
