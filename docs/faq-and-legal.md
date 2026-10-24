# FAQ and legal notes

## Do I need JUCE or another plugin framework?

No. The SDK's framework (`framework/mpc_plugin.h`, about 600 lines) covers everything the MPC host uses: parameters, MIDI, transport, state, programs. JUCE and similar frameworks are built for desktop plugin GUIs and multiple formats; on the MPC there is no GUI and only one format. They also need VST2 headers that cannot be licensed any more (see below), and their Linux builds expect X11, which the MPC does not have. AI coding agents produce correct, compact plugins against the SDK framework from a one-paragraph description; they struggle far more with a JUCE cross-compile.

If you already have DSP code written for JUCE, iPlug2 or DPF, keep the DSP classes and replace the plugin wrapper with a class derived from `mpc::Plugin`.

## Which MPCs are supported?

Any standalone MPC running **MPC OS 3.10.1 or later**, with VST plugins enabled on the device (Preferences → Enhancements (Beta) → Enable Enhancements and VST2 Support). Older firmware cannot load third-party plugins at all. 32-bit products (MPC X, X SE, Live, Live II, One, One+, Key 61, Key 37, Force) load the `file32` build; 64-bit products (MPC Live III, MPC XL and later) load the `file64` build. The MPC Studio, Studio Mk II and other controllers run on a computer and are not targets. The desktop MPC software is not a target either, though SDK plugins may well load there.

## Can my plugin have a custom graphical interface?

Only through the JSON skin system: knobs, sliders, buttons, labels, images, overlays, envelope displays, XY pads. There is no way to draw arbitrary graphics from code. See `docs/ui-reference.md`.

## Can my plugin send MIDI to MPC tracks, or receive MIDI as an effect?

No. Instruments receive MIDI; effects do not; MIDI output from plugins is discarded by the host. A step sequencer or arpeggiator must therefore be an instrument that produces its own sound (it can still follow the MPC's tempo and transport).

## Can I ship presets?

Built-in programs declared in code are shown in the MPC's preset list. See `docs/presets.md`.

## Can I sell my plugin?

That is between you and your users. inMusic does not review, sign or distribute third-party plugins. The MPC shows a warning that third-party plugins are unsupported. Note the licence of any code you include (open-source DSP libraries often carry obligations).

## Why "VST2"? Isn't VST2 dead?

Steinberg stopped licensing the VST2 SDK in 2018, but the binary interface is simple, stable, and what the MPC's host speaks. The SDK does not contain or require Steinberg's SDK: `framework/mpc_vst2_abi.h` is an independent description of the public binary interface, like the ones used by many open-source projects. "VST" is a trademark of Steinberg Media Technologies GmbH; it is used here only to describe the plugin format the MPC loads.

## What about the glibc / libstdc++ versions?

The MPC's Linux has glibc 2.39 (MPC OS 3) and the GCC 13 C++ runtime. The SDK builds against glibc 2.31 and links the C++ runtime statically so the result loads on any firmware from 3.10.1 onwards and does not depend on the MPC's compiler. `tools/validate.py` checks this.

## The community

Hobbyists worked out how to run plugins on the MPC before this SDK existed, and there is an active community with a catalogue of open-source plugins, a Discord, and tools of its own (search for "Open MPC" and "mpc-vst-plugins" on GitHub). Their plugins use the same binary interface and the same skin format, so porting between the two worlds is mostly a matter of the folder layout and `plugin.json`. The community route involved modifying the MPC's system files; this SDK's route needs no modification, just the `VST2 Plugins` folder and the preference switch.

## Licence

The SDK (framework, tools, docs, examples) is MIT licensed; see `LICENSE`. Plugins you build with it are yours. The MPC firmware, its skins and content are not part of this SDK.
