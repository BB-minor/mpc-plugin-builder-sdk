---
name: mpc-plugin-ui
description: Design or change an MPC plugin's touch-screen UI and hardware knob (Q-Link) mapping, via the simple ui.json layout or the full Plugin Skins JSON format (TUI.json, Q-Links.json). Use when the user wants pages, knobs, layout, labels, conditional controls, images or custom skins for their plugin.
---

# Plugin UI on the MPC

The MPC draws plugin UIs from JSON skins in `Plugin Skins/`. Two ways to produce them:

## A. The simple way: `ui.json` (default)

Edit `plugins/<Name>/ui.json`; `tools/build.py` turns it into the skin files. Format (`docs/ui-guide.md`, schema `schemas/ui.schema.json`):

```json
{ "pages": [ { "name": "OSC", "controls": [ "Wave", "Detune", { "param": "PWM", "visibleWhen": { "param": "Wave", "choice": "Square" } }, "empty", { "widget": "text", "text": "Oscillator" } ] } ],
  "style": { "background": "ff1e1d1e", "accent": "ffd31145" } }
```

- Controls are parameter **names** exactly as declared in code (not ids). `build/params.json` lists them after a native build.
- ≤ 16 controls per page, reading order into a 4 × 4 grid that matches the 16 hardware knobs; page names ≤ 8 characters; ≤ 32 pages.
- Widgets: `slider` (default), `knob`, `text`, `empty`. `visibleWhen` shows a control only for a given choice/bool value.
- Bool and choice parameters automatically get discrete steps and labels.

Good layouts: most important four controls top row of page 1; group by signal flow; a `MORE` page for the rest. Leave parameters out of the UI if they are rarely touched (still saved and automatable).

Regenerate by rebuilding: `python tools/build.py plugins/<Name> --target native` is enough to refresh skins (`--no-ui` skips them).

## B. Full control: hand-written skins

Create `plugins/<Name>/skins/` containing `TUI.json`, `Q-Links.json` and any images; the build copies it verbatim and does not run the generator. Start from the generated files in `build/<folder>/Plugin Skins/` and edit. The complete format, every widget and every key, is in `docs/ui-reference.md`; read the relevant sections before editing. Critical facts:
- Every object has an integer `version` the MPC checks; wrong versions or a missing key make the **whole file fail**, and the plugin then shows no tabs at all.
- `"Parameter N"` handles are 0-based parameter indices; `Q-Links.json` `Tab`/`SubTab` are 1-based; knob values are 0-based indices or -1.
- Overlay widgets need the documented duplicate `version` keys; do not "fix" them.
- Tab area is 1280 × 628; overlays 1280 × 800; fonts Titillium Web / Roboto / Nexa only; images PNG/JPEG relative to the file; colours `AARRGGBB`.
- Run `python tools/validate.py plugins/<Name>` after editing; it checks versions, indices and JSON validity.

Custom knob graphics: `python tools/make_knob.py --frames 127 --size 120 --out plugins/<Name>/assets/knob.png` then `"knobImage": "knob.png"` in `ui.json`.
