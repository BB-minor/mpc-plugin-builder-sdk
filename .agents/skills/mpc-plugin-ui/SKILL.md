---
name: mpc-plugin-ui
description: Design or change an MPC plugin's touch-screen UI and hardware knob (Q-Link) mapping, via the simple ui.json layout (film-strip knobs, faders and switches from the SDK's UI kit) or the full Plugin Skins JSON format (TUI.json, Q-Links.json). Use when the user wants pages, knobs, faders, switches, layout, labels, themes, conditional controls, images or custom skins for their plugin.
---

# Plugin UI on the MPC

The MPC draws plugin UIs from JSON skins in `Plugin Skins/`. Two ways to produce them. Use A unless the user asks for something it cannot do.

## A. The simple way: `ui.json` with the UI kit (default)

Edit `plugins/<Name>/ui.json`; `tools/build.py` turns it into the skin files. Full format: `docs/ui-guide.md`; schema: `schemas/ui.schema.json`. **Always give every parameter a real widget (`knob`, `fader` or `switch`) rather than the generic `slider`**; the generic bar is a fallback for long choice lists only. The kit graphics live in `ui-kit/` (look at `ui-kit/preview.png`; `ui-kit/README.md` explains them).

```json
{ "kit": "mpc-red",
  "pages": [ { "name": "FILTER", "controls": [
      { "param": "Cutoff",  "widget": "knob" },
      { "param": "Reso",    "widget": "knob" },
      { "param": "Mode",    "widget": "switch" },
      { "param": "Env Amt", "widget": "knob", "polarity": "bipolar" },
      { "param": "Attack",  "widget": "fader" }, { "param": "Decay", "widget": "fader" },
      { "param": "Sustain", "widget": "fader" }, { "param": "Release", "widget": "fader" },
      { "param": "Shape",   "widget": "slider", "overlay": "menu" },
      { "param": "Rate",    "widget": "knob", "visibleWhen": { "param": "Sync", "choice": "Off" } },
      "empty", { "widget": "text", "text": "Modulation" } ] } ] }
```

- Controls are parameter **names** exactly as declared in code (not ids). `build/params.json` lists them after a native build.
- ≤ 16 controls per page, reading order into a 4 × 4 grid that matches the 16 hardware knobs; page names ≤ 8 characters; ≤ 32 pages.
- Widgets: `knob`, `fader`, `switch` (kit), `slider` (generic bar), `text`, `empty`. `polarity` (`auto` / `unipolar` / `bipolar`) on knobs and faders. `overlay: "menu"` for long choice lists. `visibleWhen` shows a control only for a given choice/bool value.
- `kit`: `mpc-red` (default), `amber`, `cyan`, `lime`, `violet`, `mono`, or `{ "theme": "...", "accent": "AARRGGBB" }`. One theme per plugin. The build renders it (cached in `build/`) and copies only the PNGs the layout uses.
- `switch` needs a bool or a choice parameter with ≤ 4 choices; the generator fails with a hint otherwise.
- Bool and choice parameters automatically get discrete steps and labels.

### Which widget (apply these, in order)

1. **Bool → `switch`.** Choice with 2–4 options → `switch` (captioned segments). Choice with 5+ → `slider` + `"overlay": "menu"`.
2. **Levels, mix, dry/wet, sends, envelope stages → `fader`.** Put A, D, S, R as four faders in one row or one column.
3. **Anything centred on zero (pan, tune, pitch offset, signed mod amounts) → `knob`, bipolar.** Auto-detection catches centre-zero defaults; set `"polarity": "bipolar"` when the range is signed but the default is not zero.
4. **Every other continuous parameter (cutoff, resonance, drive, time, feedback, rate, depth, width) → `knob`.**
5. `slider` only when the value text is the point (note names, divisions, ratios) or the list is long.
6. Same role, same widget on every page. Top row of page 1 = the four controls touched first, knobs or faders, never switches. Group by signal flow; keep a section's mode switch beside the knobs it affects. Rarely used settings go on a `MORE` page; leave them out of the UI if they are rarely touched (still saved and automatable).
7. Theme: `mpc-red` unless the plugin has a character (`amber` drums/analogue, `cyan` delay/reverb/digital, `lime`/`violet` playful, `mono` utilities) or the user names a colour (`accent`).

Regenerate by rebuilding (`python tools/build.py plugins/<Name>`), or run the generator alone for a quick check:
`python tools/gen_ui.py --params plugins/<Name>/build/params.json --ui plugins/<Name>/ui.json --out "plugins/<Name>/build/<Manufacturer> - VST - <Name>/Plugin Skins" --kit-cache plugins/<Name>/build`. Read its NOTE lines (bipolar auto-detection) and fix any ERROR (it names the control and the allowed values).

## B. Full control: hand-written skins

Create `plugins/<Name>/skins/` containing `TUI.json`, `Q-Links.json` and any images; the build copies it verbatim and does not run the generator. Start from the generated files in `build/<folder>/Plugin Skins/` (they already contain the kit PNGs and the component definitions for knobs, faders, toggles and segments) and edit. `python tools/make_ui_kit.py --out plugins/<Name>/skins --theme cyan` renders a full kit there, including vertical faders, small knobs and LED buttons that `ui.json` does not use; `ui-kit/README.md` gives the TUI.json snippet for each. The complete format, every widget and every key, is in `docs/ui-reference.md`; read the relevant sections before editing. Critical facts:
- Every object has an integer `version` the MPC checks; wrong versions or a missing key make the **whole file fail**, and the plugin then shows no tabs at all.
- `"Parameter N"` handles are 0-based parameter indices; `Q-Links.json` `Tab`/`SubTab` are 1-based; knob values are 0-based indices or -1.
- Overlay widgets need the documented duplicate `version` keys; do not "fix" them.
- Film-strip frames and button images are drawn at their native pixel size, never scaled: size the entry bounds to the asset. Labels do not intercept touches, so captions can sit over buttons.
- Tab area is 1280 × 628; overlays 1280 × 800; fonts Titillium Web / Roboto / Nexa only; images PNG/JPEG relative to the file; colours `AARRGGBB`.
- Run `python tools/validate.py plugins/<Name>` after editing; it checks versions, indices, image presence and JSON validity.

Own knob artwork instead of the kit: `python tools/make_knob.py plugins/<Name>/assets/knob.png --frames 127 --size 90` then `"knobImage": "knob.png"` in `ui.json` (files in `assets/` are copied into `Plugin Skins/`).
