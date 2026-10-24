# Giving your plugin a touch UI (the easy way)

Without any UI files, the MPC already gives your plugin a working screen: pages of 16 parameter sliders, hardware knobs mapped in order. That is enough to test a plugin. A real UI needs three things more: named pages, parameters grouped sensibly, and discrete steps for switches and menus. The SDK's `ui.json` gives you those in a few lines; `tools/gen_ui.py` turns it into the MPC's own skin files (`Plugin Skins/TUI.json` and `Q-Links.json`), which `build.py` runs automatically.

For full control over the look (images, custom widgets, envelopes), read `ui-reference.md` and write the skin files yourself in `plugins/MyPlugin/skins/`; the build copies them verbatim and skips the generator.

## The screen

- Pages are function keys along the bottom of the screen; the page name is the key label (keep it short, 8 characters or fewer).
- Each page is a 4 × 4 grid of control slots, matching the 16 hardware knobs (Q-Links) in their 4 × 4 arrangement on MPCs. Touching a control assigns that column of knobs. On products with knobs in a row (Force, MPC X) the mapping still works; the bank buttons select the column.
- Controls are listed in reading order: left to right, top to bottom. Leave a slot empty with `"empty"`.
- Each control shows the parameter's name and current value text from your code. Double-tap or Enter opens a large edit overlay.

## ui.json

```json
{
  "pages": [
    {
      "name": "OSC",
      "controls": [
        "Wave", "Detune", "Octave", "Level",
        { "param": "Sub Level", "widget": "knob" },
        { "param": "PWM Depth", "visibleWhen": { "param": "Wave", "choice": "Square" } },
        "empty",
        { "widget": "text", "text": "Oscillator 2" }
      ]
    },
    {
      "name": "FILTER",
      "controls": [ "Cutoff", "Reso", "Env Amt", "Drive", "Key Track" ]
    },
    {
      "name": "ENV",
      "controls": [ "Attack", "Decay", "Sustain", "Release" ]
    }
  ],
  "style": {
    "background": "ff1e1d1e",
    "accent": "ffd31145"
  }
}
```

| Field | Meaning |
|---|---|
| `pages[].name` | Function-key label. Up to 32 pages. |
| `pages[].controls[]` | Up to 16 entries. A plain string is a parameter **name** exactly as declared in code (`floatParam("cutoff", "Cutoff", ...)` → `"Cutoff"`). |
| `widget` | `"slider"` (default: name + value bar), `"knob"` (round knob with name and value), `"text"` (a static heading; needs `text`), `"empty"`. |
| `visibleWhen` | Show this control only while another parameter has a given value: `{"param": "Wave", "choice": "Square"}` or `{"param": "Sync", "index": 1}`. Works with choice and bool parameters. |
| `style` | Optional ARGB colours: `background`, `accent`, `text`, `muted`. |
| `knobImage`, `knobFrames` | Optional: use a film-strip PNG from your project's `assets/` folder for `"knob"` widgets instead of the generic knob. `tools/make_knob.py` can generate one. |

Every bool and choice parameter you reference is given discrete steps on the data wheel and the overlay, using the choice labels from your code.

Parameters you leave out of `ui.json` are still reachable through automation and still saved; they are simply not on screen. A common pattern is to put rarely used settings on a last page called `MORE`.

## Design tips for the MPC screen

- Put the four most important controls in the top row of the first page; they are what users see first and what the top knobs control.
- Group by signal flow: oscillators, filter, envelopes, modulation, effects, output.
- Short parameter names in code: `Cutoff`, `Reso`, `Env Amt`. They are shown in capitals above a value bar about 250 pixels wide.
- Value text should be short and have units: `1.2 kHz`, `-6.0 dB`, `1/8T`. Use `formatValue()` in the framework for custom text.
- Choices beat numbers for anything with modes. A choice parameter shows its label and steps cleanly on the wheel.
- Do not rely on colour alone; the MPC screen is small and often viewed from an angle.

## Regenerating

`build.py` regenerates the skins on every build unless `plugins/MyPlugin/skins/` exists. To go from generated to hand-written, copy `build/<folder>/Plugin Skins/` to `plugins/MyPlugin/skins/` and edit from there.

## What the generator cannot do

Custom graphics, film-strip buttons, envelope displays, XY pads and sub-tabs. All are possible in the full format; see `ui-reference.md`.
