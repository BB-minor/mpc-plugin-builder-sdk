# MPC scripted UI reference (TUI.json and Q-Links.json)

This is the complete reference for the JSON "scripted UI" system that gives a
VST2 plugin a touch-screen interface and a hardware knob (Q-Link) mapping on
Akai MPC standalone hardware.

**You probably do not need this document.** The SDK ships a generator,
`tools/gen_ui.py`, which produces a valid `TUI.json` and `Q-Links.json` from a
small `ui.json` layout file (pages of up to 16 named parameters, optional knob
widgets, optional conditional visibility). That path is described in
[docs/ui-guide.md](ui-guide.md). Read this reference when you want more than the
generator gives: custom images, film-strip knobs, buttons, envelopes, your own
component layout, hardware-specific skins, and so on.

If you hand-write skins, put them in your project's `skins/` folder. The build
copies that folder verbatim to the output `Plugin Skins/` folder and skips the
generator.

This document is written to be exact. Key names, casing, version numbers and
enum strings are load-bearing: the MPC rejects a whole file over a single wrong
one.

---

## Table of contents

1. [Overview](#1-overview)
2. [version.xml and the uiversion rule](#2-versionxml-and-the-uiversion-rule)
3. [TUI.json schema](#3-tuijson-schema)
4. [CustomComponentDefinition](#4-customcomponentdefinition)
5. [The bounds object](#5-the-bounds-object)
6. [Widget catalogue](#6-widget-catalogue)
7. [Handles (data binding)](#7-handles-data-binding)
8. [Q-Links.json schema](#8-q-linksjson-schema)
9. [Complete worked example](#9-complete-worked-example)
10. [Checklist and common mistakes](#10-checklist-and-common-mistakes)

---

## 1. Overview

### 1.1 What the files are and where they live

A deployable MPC plugin is a folder. Inside it, a `Plugin Skins/` folder holds
the UI description:

```
<Manufacturer> - VST - <Name>/
    plugin.json
    version.xml
    VST/<slug>-arm32.so
    VST/<slug>-arm64.so
    Plugin Skins/
        TUI.json          touch-screen UI (tabs, components, data binding)
        Q-Links.json      which parameter each hardware Q-Link knob controls, per tab
        <images>.png      any images the skin references
```

| File | Purpose |
| --- | --- |
| `TUI.json` | The touch UI: the list of tabs shown as function-key buttons along the bottom of the screen, and for each tab a tree of components (knobs, sliders, labels, images, overlays) bound to plugin parameters. Always loaded, because it also supplies the tab names. |
| `Q-Links.json` | For every tab (and sub-tab), which 0-based parameter index each of the 16 Q-Link knobs controls, plus the default "Program Mode" knob assignment. |

Other skin files (`GUI.json`, `GUI-ProgramEdit.json`, `GUI-Popout.json`) exist
for the desktop MPC application and are ignored by the hardware; they are not
covered here.

### 1.2 How the skin is matched to the plugin

The MPC looks skins up using two things together:

1. The strings the loaded plugin reports: its vendor string (must equal the
   `manufacturer` in `plugin.json`) and its effect name (must equal `name`).
2. The folder name `<Manufacturer> - VST - <Name>` (characters that are
   illegal in file names are removed, spaces are kept).

It searches `<plugin location>/<Manufacturer> - VST - <Name>/Plugin Skins/`
(and, as fallbacks, the same path under `Instruments/`, `Insert Effects/` or
`Plugin/` subfolders, and `<plugin location>/<Name>/Plugin Skins/`). If the
vendor or name string reported by the plugin binary does not match the folder
name, the skin is not found and the default skin is used.

### 1.3 What happens when files are absent or broken

| Situation | Result |
| --- | --- |
| No `Plugin Skins/` folder at all | **Default skin.** Pages named `1`, `2`, `3`... each showing 16 generic sliders in a 4 x 4 grid (one per parameter, in index order). Each slider opens the knob overlay on double-tap or Enter and selects its Q-Link bank on touch. Sliders for invalid parameters are hidden. The knobs get the default column-major mapping of section 8.2 (parameters run down the columns from the top-left knob). |
| `Plugin Skins/` exists but has no `TUI.json` | Default skin, as above. |
| `Plugin Skins/` exists but has no `Q-Links.json` | Q-Links are **auto-mapped sequentially**: parameter `i` lands on page `i / 16` (integer division, pages named `Page n`), Q-Link `(i mod 16) + 1`, bank direction `Column`. This is not the column-major layout, so ship a `Q-Links.json`. |
| `TUI.json` exists but fails to parse | The plugin loads, but its Program Edit screen shows **no tabs at all**. The default skin is NOT used, because a file was found. |
| `Q-Links.json` exists but fails to parse | Falls back to the sequential auto-mapping above. |
| A component `type` string is unknown and matches no custom component | That component is empty (draws nothing). |
| An image path does not resolve | Nothing is drawn for that image. No error. |
| An `importFiles` entry does not resolve | Silently contributes nothing. |

"Fails to parse" includes: invalid JSON, a missing required key anywhere in the
tree, or a `version` value the MPC does not accept. Any of these makes the
**whole file** fail, not just the offending object.

### 1.4 Screen geometry

| Region | Size | Notes |
| --- | --- | --- |
| Whole screen | 1280 x 800 | |
| A tab's content area | 1280 x 628, starting 110 px from the top of the screen | The tab's root component is sized to `0 0 1280 628`. Coordinates inside a tab are relative to this area, so `y = 0` is 110 px down the physical screen. |
| An overlay | 1280 x 800 | Overlays (knob overlay, menu overlay, etc.) are created full-screen. |

There is no grid or flow layout: every component has explicit bounds (see the
`repeats` feature in section 4 for the one exception). Tabs correspond to the
function keys under the screen; sub-tabs are reached by pressing the same key
again.

The default skin's 4 x 4 slot grid, which the generator also uses, is:

| | Column 0 | Column 1 | Column 2 | Column 3 |
| --- | --- | --- | --- | --- |
| Row 0 | `23 45 278 107` | `343 45 278 107` | `663 45 278 107` | `983 45 278 107` |
| Row 1 | `23 196 278 107` | `343 196 278 107` | `663 196 278 107` | `983 196 278 107` |
| Row 2 | `23 346 278 107` | `343 346 278 107` | `663 346 278 107` | `983 346 278 107` |
| Row 3 | `23 497 278 107` | `343 497 278 107` | `663 497 278 107` | `983 497 278 107` |

That is: `x` in {23, 343, 663, 983}, `y` in {45, 196, 346, 497}, size 278 x 107.

The hardware can show a Q-Link sidebar over the right edge of the screen
(roughly `x >= 1025`). Keep controls you care about to the left of that, or
supply `qlinkBoundsData` on the tab (section 3.6) so the MPC knows where your
Q-Link columns are.

### 1.5 Fonts

Three font families are available. There is no way to ship a custom font.

| Font `name` | Notes |
| --- | --- |
| `Titillium Web` | The MPC's own UI font. Unknown font names render as this. |
| `Roboto` | |
| `Nexa` | |

Font `style` values: `Black`, `Bold`, `BoldItalic`, `Italic`, `Light`,
`LightItalic`, `Regular`, `Medium` (treated as `Regular`), `SemiBold`,
`SemiBoldItalic`, `Thin`, `ThinItalic`, `Book`, `BookItalic`. Not every family
has every style; an unavailable style falls back to something close.

### 1.6 Value formats

| Kind | Format | Examples |
| --- | --- | --- |
| Colour | 8 hex digits `AARRGGBB` (alpha first). `"0"` or `""` means fully transparent. | `"ffffffff"` white, `"ff1e1d1e"` near-black, `"19ffffff"` white at 10 % alpha, `"ffe81f40"` red |
| Rectangle | A string `"x y w h"`. Separators may be space, comma, semicolon, tab or newline. Decimals are accepted. | `"23 45 278 107"`, `"0 0 1280 628"`, `"0.5 0.5 0.25 0.1"` |
| Image path | PNG or JPEG, relative to the folder of the JSON file that defines the component. No leading slash. | `"knob.png"`, `"images/bg.jpg"` |
| Film strip | A single PNG with all frames stacked **vertically**. Frame height = image height / `numFrames`. | A 100 x 12700 image with `numFrames: 127` gives 100 x 100 frames. |
| Enum | A string. An unknown string silently falls back to the **first** entry of that enum. | `"Vertical"` |

Because unknown enum strings silently fall back rather than fail, a typo in an
enum value produces a working but wrong file. Copy enum strings exactly,
including capitalisation.

### 1.7 Versioning rules

Every object in both files carries an integer `"version"`. The MPC checks it
against the version it supports. Two kinds of check exist:

- **Exact**: the value must equal the number given here (for example
  `pageData.version` must be `1`).
- **At most**: the value must be less than or equal to the number given here;
  a lower version selects an older shape of the object with fewer keys.

A version that fails the check makes the **whole file** fail. This reference
states for every object which rule applies and which value to use.

Unless you have a reason not to, use the versions shown in the examples in this
document; they are the shapes the bundled skins use.

### 1.8 The duplicate "version" key quirk (overlay widgets)

Overlay widgets (`KnobOverlay`, `MenuOverlay`, `NumericOverlay`,
`TextEditOverlay`, `EnvelopeOverlay`) have **two consecutive `"version"` keys**
in their `data` object: the first is the widget's own version, the second is
the version of the shared overlay base (always `1`), followed by the overlay
base's `actions` array. For example:

```json
{
  "version": 2,
  "version": 1,
  "actions": [
    {"version": 1, "onAction": "Focus Lost", "handler": "Focus Lost", "handleName": "Focus Lost", "additionalData": ""}
  ],
  "overlayType": "Knob With Header",
  "scaleType": "Linear",
  "polarityType": "Unipolar",
  "showMinusSign": true,
  "handleName": "Data"
}
```

This is valid for the MPC's parser (see 1.9) but not for strict JSON tools:
most JSON libraries keep only the last duplicate key, and some linters reject
the file. If you generate these files programmatically, write the text by hand
or use a list-of-pairs encoder. The SDK's own tools (`gen_ui.py`,
`validate.py`) handle the duplicate keys correctly. Do not "fix" the
duplicate by removing one of the keys: the file will fail to load.

The legacy version-1 component entry (section 4.5) has the same quirk.

### 1.9 How the parser reads a file

Knowing how the MPC reads JSON explains most failure modes:

- Keys are consumed **in order** first. When the parser wants key `K` and the
  next unread key is `K`, it takes it. Otherwise it searches for the first
  occurrence of `K` in the object. This is why duplicate `"version"` keys work
  (they are consumed in sequence) and why writing keys in the documented order
  is the safest choice.
- A **missing required key** fails the whole file. Keys documented as
  optional may be omitted; everything else must be present.
- **Maps** (dictionaries) are written as arrays of `{"key": ..., "value": ...}`
  objects, not as JSON objects. `localComponentDefinitions`,
  `replacementStrings` and `handle remapping.map` are all of this form.
- Enums are strings; unknown strings fall back to the first entry (1.6).
- Numbers: integers where this document says int; floats accept `1` or `1.0`.

---

## 2. version.xml and the uiversion rule

`version.xml` sits at the root of the plugin folder, beside `plugin.json`:

```xml
<?xml version="1.0" encoding="utf-8"?>
<plugincontent version="1.0">
    <version>1.0.0.0</version>
    <uiversion>0.1.0.0</uiversion>
</plugincontent>
```

| Element | Meaning |
| --- | --- |
| `<version>` | The plugin's own version, four numeric components. |
| `<uiversion>` | The scripted UI format version the skins require. The MPC firmware's scripted UI version is **`0.1.0.0`**. If `<uiversion>` is **greater** than the firmware's version, the plugin refuses to load and the MPC shows a "please update the software" style message. |

Rules:

- Write exactly `0.1.0.0` when the plugin ships skins.
- **Never write `1`.** A single component is parsed as `1.0.0.0`, which is
  greater than `0.1.0.0`, so the plugin is refused.
- A missing `version.xml`, or a missing `<uiversion>` element, is treated as
  compatible.

The SDK's `build.py` writes this file for you (and only adds `<uiversion>`
when a `Plugin Skins/` folder is present).

---

## 3. TUI.json schema

### 3.1 Top level

Blocks in this section that contain `...` are shape sketches, not literal
JSON; every other JSON block in this document is complete and parses as-is.

```jsonc
{
  "pageData": {
    "version": 1,
    "componentDefinitions": { ... },
    "replacementStrings": [ ... ],
    "info": { ... },
    "tabs": [ ... ]
  }
}
```

| Key | Type | Required | Rule | Description |
| --- | --- | --- | --- | --- |
| `pageData` | object | yes | | The single root object. |
| `pageData.version` | int | yes | exactly `1` | |
| `pageData.componentDefinitions` | object | yes | | Component library: imports and locally defined custom components (3.2). |
| `pageData.replacementStrings` | array of `{"key","value"}` | no | | Turns a continuous parameter into N named discrete steps on the UI (3.3). |
| `pageData.info` | object | yes | | `CompleteDescription` or `PartialDescription` (3.4). |
| `pageData.tabs` | array of Tab | yes | | The tabs (3.5). |

### 3.2 componentDefinitions

```jsonc
{
  "version": 2,
  "importFiles": [],
  "localComponentDefinitions": [
    {"key": "sliderWithFocus", "value": { ...CustomComponentDefinition... }},
    {"key": "knob overlay",    "value": { ...CustomComponentDefinition... }}
  ]
}
```

| Key | Type | Required | Rule | Description |
| --- | --- | --- | --- | --- |
| `version` | int | yes | at most `2`; **use `2`** | Version 1 resolves `importFiles` relative to an internal MPC folder and is not useful for third-party skins. Version 2 resolves them relative to the folder containing this file. |
| `importFiles` | array of string | yes | | Paths of other JSON files containing component definitions, relative to this file's folder. Loaded recursively (an imported file can import further files). A path that does not exist contributes nothing, silently. Use `[]` when you have none. |
| `localComponentDefinitions` | array of `{"key": string, "value": CustomComponentDefinition}` | yes | | Custom components defined inline. The `key` is the component name; use it as a `type` string in `componentsData` entries, as `componentName` on a tab, or as `additionalData` of a `Show Overlay` action. |

Name resolution: a local definition wins over an imported one with the same
name. Between two imports that define the same name the winner is undefined,
so keep names unique.

An imported file has the same shape as `componentDefinitions` itself: a
top-level object with `version`, `importFiles` and `localComponentDefinitions`.
Image paths inside an imported file are relative to **that** file's folder.

### 3.3 replacementStrings

```json
"replacementStrings": [
  {"key": "Parameter 0", "value": ["Sine", "Saw", "Square", "Noise"]},
  {"key": "Parameter 7", "value": ["Off", "On"]}
]
```

Each entry replaces, for the UI only, the behaviour of one handle (normally a
`Parameter N`). The parameter becomes a discrete control with `N` evenly spaced
steps (value range `0 .. N-1`, increment `1`, default `0`), and the given
strings are shown as the value text instead of the plugin's own display
string. The data wheel, overlays, Q-Link knobs and `IndexedEnabling` all
honour the steps.

Rules:

- The `key` must be the **final** handle name after any `PartialDescription`
  offsetting (3.4), i.e. the actual `Parameter N` index the control resolves to.
- The number of strings must equal the number of choices the plugin's
  parameter actually has, so that step `k` of the UI maps to the plugin's
  choice `k`. For a 4-way choice parameter give 4 strings; for a bool give 2
  (`["Off", "On"]`).
- Without a replacement entry the UI shows the plugin's own parameter **name**
  and its own **display string** (from the plugin's parameter display
  callback). That is the normal case and is correct for continuous
  parameters. The "coarse" increment is the data wheel; "fine" is the data
  wheel with Shift.

### 3.4 info (OffsetInfo)

```json
{"version": 1, "type": "CompleteDescription"}
```

```json
{"version": 1, "type": "PartialDescription", "offset": 16}
```

| Key | Type | Required | Rule | Description |
| --- | --- | --- | --- | --- |
| `version` | int | yes | exactly `1` | |
| `type` | enum | yes | `"CompleteDescription"` or `"PartialDescription"` | |
| `offset` | int | only for `PartialDescription` | | How many parameters each repeated tab covers. |

- **`CompleteDescription`** (recommended): the file describes every tab
  explicitly. Handles mean exactly what they say.
- **`PartialDescription`**: the file defines **exactly one** tab (at
  `fnKeyIndex` 0, `fnKeySubIndex` 0). The MPC repeats that tab
  `ceil(numParameters / offset)` times. In repetition `t` (0-based), every
  `Parameter N` handle is shifted to `Parameter (N + t * offset)`. The tabs are
  named `"1"`, `"2"`, `"3"`, ... (the `tabName` you wrote is ignored).
  Sub-tabs are not supported in this mode. This is how the default skin is
  built (offset 16, 16 sliders). It is handy for plugins with many identical
  parameters, but `replacementStrings` keys must then name the shifted
  parameters.

The same `info` object appears in `Q-Links.json` with the same meaning.

### 3.5 Tab object

A tab exists in three versions. Version 1 defines its root component inline;
versions 2 and 3 reference a named custom component. The rule is "at most 3".

**Version 1 (inline root, the shape the generator writes):**

```jsonc
{
  "version": 1,
  "tabName": "OSC",
  "fnKeyIndex": 0,
  "fnKeySubIndex": 0,
  "componentDefinition": { ...CustomComponentDefinition... },
  "qlinkBoundsData": ["38 0 248 624", "358 0 248 624", "678 0 248 624", "998 0 248 624"]
}
```

**Version 3 (named root, with scale):**

```json
{
  "version": 3,
  "tabName": "OSC",
  "fnKeyIndex": 0,
  "fnKeySubIndex": 0,
  "qlinkBoundsData": [],
  "componentName": "oscPage",
  "initialSize": "0 0 1280 628",
  "scale": 1.0
}
```

| Key | Type | Required | Versions | Description |
| --- | --- | --- | --- | --- |
| `version` | int | yes | | `1`, `2` or `3`. |
| `tabName` | string | yes | all | Text shown on the function-key button. Keep it short (about 8 characters). |
| `fnKeyIndex` | int | yes | all | 0-based index of the function key this tab sits on. |
| `fnKeySubIndex` | int | yes | all | 0-based sub-tab index. Pressing the same function key again cycles sub-tabs. Maximum 8 sub-tabs per key. |
| `qlinkBoundsData` | array of rect string | yes | all | One rectangle per Q-Link column, in tab coordinates. When the hardware highlights the active Q-Link column, this is the area it highlights. May be `[]`. See 3.6. |
| `componentDefinition` | CustomComponentDefinition | yes (v1 only) | 1 | The tab's root component, defined inline. It gets a synthetic internal name and its size is forced to `0 0 1280 628`. |
| `componentName` | string | yes (v2, v3) | 2, 3 | Name of a custom component (from `componentDefinitions`) to use as the root. |
| `initialSize` | rect string | yes (v2, v3) | 2, 3 | Root size; use `"0 0 1280 628"`. |
| `scale` | float | yes (v3) | 3 | Scale factor applied to the root. Default `1.0`. |

Limits: at most **32 tabs** in total and at most **8 sub-tabs** per function
key. Tabs beyond those limits are not supported.

Key order matters for the parser (1.9). Write the keys in the order shown.
Version 1 tabs: `version, tabName, fnKeyIndex, fnKeySubIndex,
componentDefinition, qlinkBoundsData`. Versions 2/3:
`version, tabName, fnKeyIndex, fnKeySubIndex, qlinkBoundsData, componentName,
initialSize, scale`.

### 3.6 qlinkBoundsData

An array of `"x y w h"` strings, one per Q-Link **column** of the layout, in
tab coordinates. On hardware whose Q-Links are arranged in columns (the 4 x 4
layout), the MPC highlights the rectangle of the column whose knobs are
currently active. With the default slot grid, four columns of 248 px width
with 20 px margins around the slots look like this:

```json
"qlinkBoundsData": ["38 0 248 624", "358 0 248 624", "678 0 248 624", "998 0 248 624"]
```

Use `[]` to draw no highlight. A custom component can also suppress the
highlight with `hideQLinkBounds` (4.1).

---

## 4. CustomComponentDefinition

A custom component is a reusable container: a background, a list of actions
(what happens when it is touched or when Enter is pressed), and a list of
child components, each with bounds and a handle remapping. Tabs, overlays and
reusable "slot" widgets are all custom components.

### 4.1 Shape

The block below is a shape sketch (`...Entry...` stands for the entries of
4.4); section 9 has complete definitions.

```jsonc
{
  "version": 2,
  "actions": [
    {"version": 1, "onAction": "Mouse Down",    "handler": "Q-Link",       "handleName": "Data", "additionalData": ""},
    {"version": 1, "onAction": "Double Click",  "handler": "Show Overlay", "handleName": "Data", "additionalData": "knob overlay"},
    {"version": 1, "onAction": "Enter Pressed", "handler": "Show Overlay", "handleName": "Data", "additionalData": "knob overlay"}
  ],
  "backgroundData": {
    "version": 1,
    "focussed":   {"version": 1, "colour": "0", "image": ""},
    "unfocussed": {"version": 1, "colour": "0", "image": ""}
  },
  "ignoreMousePresses": false,
  "disableCoarseDataWheel": false,
  "componentsData": [ ...Entry... ]
}
```

| Key | Type | Required | Versions | Default | Description |
| --- | --- | --- | --- | --- | --- |
| `version` | int | yes | | | At most `4`. **Use `2`** unless you need `repeats` (3) or `hideQLinkBounds` (4). |
| `actions` | array of Action | yes | all | | See 4.2. May be `[]`. |
| `backgroundData` | TouchBackgroundData | yes | all | black | See 4.3. |
| `ignoreMousePresses` | bool | yes | all | `false` | When `true` the component does not consume touches (used for overlays, so the overlay widget inside gets them). |
| `disableCoarseDataWheel` | bool | yes | 2+ | `false` | When `true` the data wheel always uses the fine increment inside this component. |
| `repeats` | int | no | 3+ | `1` | Lay the children out `repeats` times side by side. Each repeat is offset horizontally by the child's width. See 4.6. |
| `hideQLinkBounds` | bool | no | 4+ | `false` | Suppress the Q-Link column highlight while this component is shown. |
| `componentsData` | array of Entry | yes | all | | The children, in paint order (first = bottom). See 4.4. |

Key order: `version, actions, backgroundData, ignoreMousePresses,
disableCoarseDataWheel, [repeats], [hideQLinkBounds], componentsData`.

### 4.2 Actions

```json
{"version": 1, "onAction": "Double Click", "handler": "Show Overlay", "handleName": "Data", "additionalData": "knob overlay"}
```

| Key | Type | Required | Description |
| --- | --- | --- | --- |
| `version` | int | yes | At most `2`. Use `1`. Version 2 adds a `"handle remapping"` key (a Remap object, 4.5) applied before the handler runs. |
| `onAction` | enum | yes | `"Invalid"`, `"Mouse Down"`, `"Double Click"`, `"Enter Pressed"`. |
| `handler` | string | yes | What to do. See table below. |
| `handleName` | string | yes | The handle the handler acts on (normally `"Data"` inside a reusable component, or a `Parameter N`). `""` when unused. |
| `additionalData` | string | yes | Extra argument for the handler. `""` when unused. |

| `onAction` | Fires when |
| --- | --- |
| `Mouse Down` | The component (or a child that does not consume the touch) is touched. |
| `Double Click` | Double-tapped. |
| `Enter Pressed` | The component has hardware focus and the Enter key / data-wheel push is pressed. |
| `Invalid` | Never. Placeholder. |

| `handler` | `handleName` | `additionalData` | Effect |
| --- | --- | --- | --- |
| `Q-Link` | a parameter handle | `""` | Selects (banks the hardware knobs to) the Q-Link bank that contains this handle's parameter, as defined in `Q-Links.json`. Usually attached to `Mouse Down`, so touching a control makes its knob live. |
| `Show Overlay` | the handle to edit | custom component name | Creates that custom component full-screen (`0 0 1280 800`) as an overlay on top of everything, with its `Data` handle bound to `handleName`. Usually attached to `Double Click` and `Enter Pressed`. |
| `Toggle Switch` | a parameter handle | `""` | Flips the handle between its minimum and maximum values. Attach to `Mouse Down` for a tap-to-toggle control. |
| `Focus Lost` | `"Focus Lost"` | `""` | Hides the overlay. Only meaningful inside an overlay widget's own `actions` list with `onAction: "Focus Lost"` (see 6.10). |

### 4.3 backgroundData (TouchBackgroundData)

```json
{
  "version": 1,
  "focussed":   {"version": 1, "colour": "ff1e1d1e", "image": ""},
  "unfocussed": {"version": 1, "colour": "ff1e1d1e", "image": ""}
}
```

| Key | Type | Required | Description |
| --- | --- | --- | --- |
| `version` | int | yes | exactly `1`. |
| `focussed` | ImageData | yes | Background drawn while the component has hardware focus. |
| `unfocussed` | ImageData | yes | Background drawn otherwise. |

ImageData: `{"version": 1, "colour": "AARRGGBB", "image": "relative/path.png"}`.
The colour is filled first, then the image (if any) is drawn over it. Use
`"colour": "0", "image": ""` for a transparent background. If both are omitted
the default background is black, so a tab root normally sets an explicit
colour.

### 4.4 componentsData entries

Each child is an Entry:

```json
{
  "version": 2,
  "componentData": {
    "version": 1,
    "name": "cutoff",
    "type": "sliderWithFocus",
    "data": {"version": 1, "handleName": "Data"}
  },
  "handle remapping": {
    "version": 1,
    "map": [ {"key": "Data", "value": "Parameter 2"} ]
  },
  "bounds": {
    "version": 1,
    "acceptsHWFocus": "Yes",
    "showWhenDataModelInvalid": "Hide",
    "whenVisible": "Always",
    "boundsType": "Absolute",
    "bounds": "23 45 278 107"
  }
}
```

| Key | Type | Required | Description |
| --- | --- | --- | --- |
| `version` | int | yes | At most `2`. **Use `2`.** Version 1 is a legacy shape with the remap inlined (4.5). |
| `componentData.version` | int | yes | exactly `1`. |
| `componentData.name` | string | yes | Any name; for your own reference. Need not be unique. |
| `componentData.type` | string | yes | A built-in widget type from section 6 (`Knob`, `Slider`, `Label`, ...) or the name of a custom component. |
| `componentData.data` | object | yes | The widget's data (section 6). For a custom component, always `{"version": 1, "handleName": "Data"}`. |
| `handle remapping` | Remap | yes (v2) | Maps handle names used inside the child to handle names in this component's scope (4.5). Use `{"version": 1, "map": []}` when nothing needs remapping. |
| `bounds` | BoundsData | yes | Position, focus and visibility rules (section 5). |

**Using a custom component as a child.** Set `type` to the component's name
and `data` to `{"version": 1, "handleName": "Data"}`. This exposes one handle
called `Data` to the component's children and actions; the entry's
`handle remapping` then binds `Data` to a real parameter:

```json
{
  "version": 2,
  "componentData": {"version": 1, "name": "attack", "type": "knobWithFocus", "data": {"version": 1, "handleName": "Data"}},
  "handle remapping": {"version": 1, "map": [{"key": "Data", "value": "Parameter 5"}]},
  "bounds": {"version": 1, "acceptsHWFocus": "Yes", "showWhenDataModelInvalid": "Hide", "whenVisible": "Always", "boundsType": "Absolute", "bounds": "23 45 278 107"}
}
```

Inside `knobWithFocus`, every widget simply uses `"handleName": "Data"`.

### 4.5 Handle remapping

```json
{"version": 1, "map": [ {"key": "Data", "value": "Parameter 2"} ]}
```

| Key | Type | Description |
| --- | --- | --- |
| `version` | int | exactly `1`. |
| `map` | array of `{"key","value"}` | `key` is a handle name as used **inside** the child; `value` is what it becomes in the **parent's** scope. |

Rules:

- Lookups are literal string matches on the child's handle name, after any
  modifier prefix is taken into account by the wildcard rule below.
- `"*"` is a wildcard that carries the rest of the name through. For example
  `{"key": "Invert/*", "value": "*"}` turns `Invert/Data` inside the child
  into `Data` in the parent (stripping the modifier), and
  `{"key": "*", "value": "Parameter *"}` would map `3` to `Parameter 3`.
- Remaps **chain**: a handle inside a grandchild is remapped by the child's
  entry first, then by the parent's entry, and so on up to the tab, where it
  must finally resolve to a real handle (`Parameter N`, `PageIndex ...`, a
  `Local/...` handle, etc.).
- A handle that is not in the map passes through unchanged. So a child can
  reference `Parameter 3` directly; it just is not reusable then.

**Legacy version-1 entry.** A version-1 `componentsData` entry has no
`"handle remapping"` key; instead the remap's `version` and `map` keys are
written directly in the entry, which produces a duplicate `version` key:

```json
{
  "version": 1,
  "componentData": {"version": 1, "name": "x", "type": "Slider", "data": {"version": 1, "handleName": "Data"}},
  "version": 1,
  "map": [{"key": "Data", "value": "Parameter 0"}],
  "bounds": {"version": 1, "acceptsHWFocus": "Yes", "showWhenDataModelInvalid": "Hide", "whenVisible": "Always", "boundsType": "Absolute", "bounds": "0 0 100 50"}
}
```

You may encounter this in older skins. Write version 2 entries instead.

### 4.6 repeats and the {Increment} substitution (component version 3+)

With `"version": 3` (or higher) a custom component accepts an optional
`"repeats": N`. The children are laid out `N` times from left to right; copy
`r` (0-based) is offset horizontally by `r * (child width)`. This is the only
automatic layout facility.

To make each copy bind to a different parameter, write the placeholder
`{Increment:Base=n,Step=s,Offset=o}` inside any handle value. In copy `r` it
becomes the integer `max(0, n + r * s + o)`. `Step` defaults to `1` and
`Offset` to `0`, so `{Increment:Base=4}` yields 4, 5, 6, ... across the copies.

```json
{
  "version": 3,
  "actions": [],
  "backgroundData": {"version": 1, "focussed": {"version": 1, "colour": "0", "image": ""}, "unfocussed": {"version": 1, "colour": "0", "image": ""}},
  "ignoreMousePresses": false,
  "disableCoarseDataWheel": false,
  "repeats": 8,
  "componentsData": [
    {
      "version": 2,
      "componentData": {"version": 1, "name": "step", "type": "Slider", "data": {"version": 2, "sliderType": "ColourBar", "direction": "Vertical", "backgroundColour": "ff2a2a2a", "outlineColour": "ff6a6a6c", "valueColour": "ffd31145", "isBipolar": false, "handleName": "Data"}},
      "handle remapping": {"version": 1, "map": [{"key": "Data", "value": "Parameter {Increment:Base=10}"}]},
      "bounds": {"version": 1, "acceptsHWFocus": "Yes", "showWhenDataModelInvalid": "Hide", "whenVisible": "Always", "boundsType": "Absolute", "bounds": "0 0 150 300"}
    }
  ]
}
```

This produces eight 150 px wide sliders bound to parameters 10 .. 17.

---

## 5. The bounds object

Every `componentsData` entry has a `bounds` object (BoundsData). It controls
where the child is, whether it can take hardware focus, and when it is visible.

### 5.1 Shape

Version 1:

```json
{
  "version": 1,
  "acceptsHWFocus": "Yes",
  "showWhenDataModelInvalid": "Hide",
  "whenVisible": "Always",
  "boundsType": "Absolute",
  "bounds": "23 45 278 107"
}
```

Version 2 adds `additionalInvalidatingHandles`:

```json
{
  "version": 2,
  "acceptsHWFocus": "Yes",
  "showWhenDataModelInvalid": "Hide",
  "whenVisible": "Always",
  "boundsType": "Absolute",
  "bounds": "343 45 278 107",
  "additionalInvalidatingHandles": ["IndexedEnabling/1/4/Parameter 0"]
}
```

### 5.2 Keys

| Key | Type | Required | Versions | Default | Values and meaning |
| --- | --- | --- | --- | --- | --- |
| `version` | int | yes | | | At most `2`. Use `1`, or `2` when you need `additionalInvalidatingHandles`. |
| `acceptsHWFocus` | enum | yes | all | `"Yes"` | `"No"`: never focusable. `"Children"`: this component is not focusable itself but its children may be. `"Yes"`: focusable by the hardware cursor keys, and touching it also gives it focus. |
| `showWhenDataModelInvalid` | enum | yes | all | `"Show"` | `"Show"`: always drawn. `"Hide"`: hidden when **all** of the component's handles are invalid (e.g. bound to a parameter index the plugin does not have, or to `Invalid`). `"HideOnInvalidName"`: as `Hide`, and additionally hidden when the bound parameter's name is the empty string. |
| `whenVisible` | enum | yes | all | `"Always"` | `"Always"`, `"WhenFocussed"` (only while the parent component has hardware focus; used for focus rings), `"WhenNotFocussed"`. |
| `boundsType` | enum | yes | all | `"Absolute"` | `"Absolute"`: `bounds` is in pixels relative to the parent's top-left. `"Proportional"`: `bounds` values are fractions `0..1` of the parent's width/height. `"Relative"`: `relativeBounds` is read **instead of** `bounds`. |
| `bounds` | rect string | yes unless `Relative` | all | | `"x y w h"`, floats accepted. |
| `relativeBounds` | string | only when `Relative` | all | | A relative-rectangle expression string in the form `"left, top, right, bottom"` where each term may reference the parent's size (for example `"parent.width - 100, 0, parent.width, 50"`). Prefer `Absolute` or `Proportional`; `Relative` is rarely needed. |
| `additionalInvalidatingHandles` | array of handle string | yes (v2) | 2 | `[]` | The component is hidden whenever **any** of these handles is invalid, in addition to the `showWhenDataModelInvalid` rule. This is the conditional-visibility mechanism (7.4). Handles here are remapped through the entry's `handle remapping` like any other handle. |

Key order: `version, acceptsHWFocus, showWhenDataModelInvalid, whenVisible,
boundsType, bounds` (or `relativeBounds`), `[additionalInvalidatingHandles]`.

Notes:

- `acceptsHWFocus: "Yes"` on a slot and `"No"` on the slot's children is the
  usual pattern: the slot as a whole is one focus stop, and its inner `Focus`
  widget appears with `whenVisible: "WhenFocussed"`.
- Setting `showWhenDataModelInvalid: "Hide"` on parameter-bound slots makes
  the skin degrade gracefully if the plugin has fewer parameters than the
  skin expects.
- A proportional example that fills the right half of the parent:
  `"boundsType": "Proportional", "bounds": "0.5 0 0.5 1"`.

---

## 6. Widget catalogue

Every widget is used as a `componentsData` entry whose `componentData.type` is
the type string in the heading and whose `componentData.data` is the object
documented. "Handle" keys hold handle names (section 7); inside a reusable
custom component they are normally `"Data"`; `"Invalid"` means unbound.

Widgets marked **well-trodden** are used heavily by the bundled MPC skins and
are the safe choice. Widgets marked **less tested** exist in the firmware and
work as described, but have had little use with third-party plugins; test them
on hardware.

The default visuals (generic knob, generic slider, overlays, focus ring) follow
the MPC's own theme, so a skin built from them looks native with no artwork at
all.

### 6.1 Slider (well-trodden)

A bar that shows and edits a parameter. The generic form draws the MPC's
standard "name + value + bar" slot.

| Key | Type | Versions | Values / default | Description |
| --- | --- | --- | --- | --- |
| `version` | int | | at most `4` | `1` = generic bar; `2`+ add `sliderType`; `3` adds `thumbTrackVisible`, `revealedImage`; `4` adds `revealType`. |
| `sliderType` | enum | 2+ | `"GenericLegacy"`, `"ColourBar"`, `"TrackAndThumb"` | Version 1 implies `GenericLegacy`. |
| `direction` | enum | 2+, not for `GenericLegacy` | `"Vertical"`, `"Horizontal"` | Fill direction. |
| `backgroundColour` | colour | `ColourBar` | | Track colour. |
| `outlineColour` | colour | `ColourBar` | | Outline colour. |
| `valueColour` | colour | `ColourBar` | | Filled part colour. |
| `isBipolar` | bool | `ColourBar` | `false` | Fill from the centre rather than from the minimum end. |
| `thumbImage` | path | `TrackAndThumb` | | Image drawn at the value position. |
| `thumbTrackVisible` | bool | `TrackAndThumb`, 3+ | `true` | Draw the track behind the thumb. |
| `revealedImage` | path | `TrackAndThumb`, 3+ | `""` | An image the same size as the slider, revealed up to the value position. |
| `revealType` | enum | `TrackAndThumb`, 4+ | `"Normal"`, `"Inverted"`, `"Bipolar"` | How much of `revealedImage` is shown. |
| `handleName` | handle | all | `"Data"` | The parameter. |

Key order: `version, [sliderType], [direction], [type-specific keys], handleName`.

`GenericLegacy` is a horizontal bar with a header showing the parameter's name
in upper case and its display value. It does not take `direction`. This is the
widget the default skin uses:

```json
{"version": 1, "handleName": "Data"}
```

A vertical colour bar:

```json
{
  "version": 2,
  "sliderType": "ColourBar",
  "direction": "Vertical",
  "backgroundColour": "ff2a2a2a",
  "outlineColour": "ff6a6a6c",
  "valueColour": "ffd31145",
  "isBipolar": false,
  "handleName": "Data"
}
```

A horizontal fader with artwork:

```json
{
  "version": 4,
  "sliderType": "TrackAndThumb",
  "direction": "Horizontal",
  "thumbImage": "fader_cap.png",
  "thumbTrackVisible": true,
  "revealedImage": "fader_glow.png",
  "revealType": "Normal",
  "handleName": "Data"
}
```

Binds to: one parameter handle. Touch-drag edits it along `direction`; the
data wheel edits it when focused.

### 6.2 Knob (well-trodden)

A rotary control.

| Key | Type | Versions | Values / default | Description |
| --- | --- | --- | --- | --- |
| `version` | int | | at most `5` | `2` adds `invert` for film strips; `3` adds `dragOrientation` for `FilmStrip`; `4` adds `dragOrientation` for all types; `5` adds `polarity` for the generic types. |
| `knobType` | enum | all | `"FilmStrip"`, `"ValueSlider"`, `"GenericLegacy"`, `"GenericNoLabel"`, `"RotatingImage"` | |
| `filmStrip` | path | `FilmStrip` | | Vertical film strip PNG. |
| `numFrames` | int | `FilmStrip` | | Number of frames in the strip. |
| `invert` | bool | `FilmStrip`, 2+ | `false` | Play the strip backwards (frame 0 = maximum). |
| `textStyle` | TextStyleData | `ValueSlider` | | Shows only the value text, no graphic (see 6.3 for TextStyleData). |
| `polarity` | enum | `GenericLegacy` / `GenericNoLabel`, 5+ | `"UniPolar"`, `"BiPolar"` | Draw the value arc from the minimum or from the centre. |
| `foregroundImage` | path | `RotatingImage` | | Image rotated with the value. |
| `backgroundImage` | path | `RotatingImage` | | Static image behind it. |
| `imageScale` | float | `RotatingImage` | `1.0` | Scale of both images. |
| `dragOrientation` | enum | 4+ (3+ for `FilmStrip`) | `"Horizontal"`, `"Vertical"` (default), `"AbsoluteRotaryKnob"` | Which finger movement changes the value. `AbsoluteRotaryKnob` sets the value from the touch angle. |
| `handleName` | handle | all | `"Data"` | |

Key order: `version, knobType, [type-specific keys], [dragOrientation], handleName`.

`GenericLegacy` draws the MPC's standard knob and shows the value under it;
`GenericNoLabel` draws the knob only (pair it with `Label` widgets).

Generic knob (the shape the generator writes):

```json
{"version": 5, "knobType": "GenericLegacy", "polarity": "UniPolar", "dragOrientation": "Vertical", "handleName": "Data"}
```

Film strip knob with 127 frames in a vertical strip:

```json
{"version": 2, "knobType": "FilmStrip", "filmStrip": "knob.png", "numFrames": 127, "invert": false, "handleName": "Data"}
```

The same with a horizontal drag:

```json
{"version": 3, "knobType": "FilmStrip", "filmStrip": "knob.png", "numFrames": 127, "invert": false, "dragOrientation": "Horizontal", "handleName": "Data"}
```

Rotating image knob:

```json
{"version": 4, "knobType": "RotatingImage", "foregroundImage": "pointer.png", "backgroundImage": "knob_body.png", "imageScale": 1.0, "dragOrientation": "Vertical", "handleName": "Data"}
```

Value-only text "knob" (drag to edit, shows just the value):

```json
{
  "version": 4,
  "knobType": "ValueSlider",
  "textStyle": {"version": 1, "font": {"version": 1, "name": "Titillium Web", "style": "Regular", "height": 28.0}, "colour": "ffffffff", "justification": "horizontallyCentred verticallyCentred", "case": "Original"},
  "dragOrientation": "Vertical",
  "handleName": "Data"
}
```

Version rules: a `FilmStrip` knob at version 1 has no `invert` key; at
versions 1 and 2 no `dragOrientation`. The generic types at version 4 or below
have no `polarity`. Each key listed for the version you choose must be present.

### 6.3 Label (well-trodden)

Text: the parameter's name, its current value string, or fixed text.

| Key | Type | Values / default | Description |
| --- | --- | --- | --- |
| `version` | int | exactly `1` | |
| `textStyle` | TextStyleData | | Font, colour, justification, case. |
| `type` | enum | `"Name"`, `"Value"`, `"Text"` | `Name` shows the bound parameter's name; `Value` its display string (or the `replacementStrings` label); `Text` shows the `text` key. |
| `handleName` | handle | `"Data"` | Ignored for `Text`, but must still be present (use `"Invalid"` or `"Data"`). |
| `text` | string | only when `type` is `"Text"` | The fixed text. |

Key order: `version, textStyle, type, handleName, [text]`.

TextStyleData:

```json
{
  "version": 1,
  "font": {"version": 1, "name": "Titillium Web", "style": "Regular", "height": 30.0},
  "colour": "ff6a6a6c",
  "justification": "left verticallyCentred",
  "case": "Original"
}
```

| TextStyleData key | Values |
| --- | --- |
| `font.name` | `"Titillium Web"`, `"Roboto"`, `"Nexa"` (1.5). |
| `font.style` | One of the styles in 1.5, e.g. `"Regular"`, `"Bold"`, `"Light"`. |
| `font.height` | Float, in pixels. |
| `colour` | `AARRGGBB`. |
| `justification` | Space-separated flags from `left`, `right`, `horizontallyCentred`, `top`, `bottom`, `verticallyCentred`, `horizontallyJustified`. E.g. `"left"`, `"horizontallyCentred verticallyCentred"`. |
| `case` | `"Original"`, `"Upper Case"`, `"Lower Case"`. |

Name label:

```json
{
  "version": 1,
  "textStyle": {"version": 1, "font": {"version": 1, "name": "Titillium Web", "style": "SemiBold", "height": 26.0}, "colour": "ffffffff", "justification": "left verticallyCentred", "case": "Upper Case"},
  "type": "Name",
  "handleName": "Data"
}
```

Fixed text:

```json
{
  "version": 1,
  "textStyle": {"version": 1, "font": {"version": 1, "name": "Roboto", "style": "Light", "height": 22.0}, "colour": "ff6a6a6c", "justification": "horizontallyCentred verticallyCentred", "case": "Original"},
  "type": "Text",
  "handleName": "Invalid",
  "text": "FILTER"
}
```

Binds to: a parameter handle for `Name` and `Value`. Labels do not edit the
value and do not take focus; set `acceptsHWFocus: "No"` on their entry.

### 6.4 Button (well-trodden)

An image button bound to a parameter: a toggle, or one member of a radio group.

| Key | Type | Versions | Values / default | Description |
| --- | --- | --- | --- | --- |
| `version` | int | | at most `2` | `2` adds `gestureBehaviour`. |
| `onImage` | path | all | | Drawn when lit. |
| `offImage` | path | all | | Drawn when not lit. |
| `buttonId` | int | all | `1` | Which step of the parameter this button represents. |
| `numButtonsInGroup` | int | all | `1` | Number of buttons sharing the parameter. |
| `handleName` | handle | all | `"Data"` | |
| `gestureBehaviour` | enum | 2 | `"Instant"`, `"Hold"` | `Instant`: a tap acts immediately. `Hold`: the button acts on press and also lets you drag or use the data wheel while held. |

Key order: `version, onImage, offImage, buttonId, numButtonsInGroup, handleName, [gestureBehaviour]`.

Semantics: the parameter is quantised into `max(numButtonsInGroup, 2)` steps.

- **Toggle** (`numButtonsInGroup: 1`, `buttonId: 1`): pressing flips the
  value between 0 and 1. The button is lit when the step index equals
  `buttonId`, so use `buttonId: 1` for "lit when on".
- **Radio group** (`numButtonsInGroup: N > 1`): place N buttons bound to the
  same parameter with `buttonId` 0 .. N-1. Pressing one sets the value to its
  `buttonId` (a 0-based step index); the button whose `buttonId` matches the
  current step is lit. Combine with a `replacementStrings` entry of N strings
  so the data wheel and Q-Link knob step through the same N choices.

Toggle:

```json
{"version": 2, "onImage": "power_on.png", "offImage": "power_off.png", "buttonId": 1, "numButtonsInGroup": 1, "handleName": "Data", "gestureBehaviour": "Instant"}
```

Third of a four-way radio group (waveform `Square`):

```json
{"version": 2, "onImage": "wave_square_on.png", "offImage": "wave_square_off.png", "buttonId": 2, "numButtonsInGroup": 4, "handleName": "Data", "gestureBehaviour": "Instant"}
```

Binds to: one parameter handle (shared by all buttons of a group). Images are
scaled to the entry's bounds.

### 6.5 Focus (well-trodden)

A focus ring. Draws a translucent fill and an outline over the parent's area.
It has no handle; it is paired with `whenVisible: "WhenFocussed"` on its entry
so it appears only while the parent slot has hardware focus.

| Key | Type | Values / default | Description |
| --- | --- | --- | --- |
| `version` | int | exactly `1` | |
| `backgroundColour` | colour | white at 10 % (`"19ffffff"`) | Fill. |
| `outlineColour` | colour | the theme focus colour | Outline. The bundled skins use `"ffe81f40"`. |
| `backgroundInset` | float | | Inset of the fill from the entry bounds, in pixels. |
| `outlineThickness` | float | | Outline width in pixels. |

```json
{"version": 1, "backgroundColour": "19ffffff", "outlineColour": "ffe81f40", "backgroundInset": 4.0, "outlineThickness": 1.0}
```

Typical entry:

```json
{
  "version": 2,
  "componentData": {"version": 1, "name": "focus", "type": "Focus", "data": {"version": 1, "backgroundColour": "19ffffff", "outlineColour": "ffe81f40", "backgroundInset": 4.0, "outlineThickness": 1.0}},
  "handle remapping": {"version": 1, "map": []},
  "bounds": {"version": 1, "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Show", "whenVisible": "WhenFocussed", "boundsType": "Absolute", "bounds": "0 0 278 107"}
}
```

### 6.6 Image (well-trodden)

A static image, or a film strip whose frame follows a parameter.

| Key | Type | Versions | Values / default | Description |
| --- | --- | --- | --- | --- |
| `version` | int | | at most `2` | Use `2`. |
| `imageType` | enum | 2 | `"Regular"`, `"FilmStrip"` | |
| `colour` | colour | all | | Filled behind the image. `"0"` for none. |
| `image` | path | all | | PNG/JPEG, relative to the defining file. |
| `numFrames` | int | 2, `FilmStrip` | | Frames in the vertical strip. |
| `handleName` | handle | 2, `FilmStrip` | | The parameter whose value selects the frame. |
| `animated` | bool | 2, `FilmStrip`, optional | `false` | Cycle through the frames continuously instead of following the handle. |

Key order: `version, [imageType], colour, image, [numFrames, handleName, [animated]]`.

Version 1 is just `{"version": 1, "colour": ..., "image": ...}`.

Static background image:

```json
{"version": 2, "imageType": "Regular", "colour": "0", "image": "bg.png"}
```

Film-strip indicator (for example an LED ladder with 8 frames bound to a
parameter):

```json
{"version": 2, "imageType": "FilmStrip", "colour": "0", "image": "led_ladder.png", "numFrames": 8, "handleName": "Data"}
```

Images are scaled to the entry bounds. Place a full-tab background image as
the first `componentsData` entry with bounds `"0 0 1280 628"` and
`acceptsHWFocus: "No"`.

### 6.7 Separator (less tested)

A one-pixel-ish line.

| Key | Type | Values | Description |
| --- | --- | --- | --- |
| `version` | int | exactly `1` | |
| `colour` | colour | | |
| `separatorType` | **int** | `0` = Vertical, `1` = Horizontal | Quirk: unlike every other enum this one is written as an integer, not a string. |

```json
{"version": 1, "colour": "ff3a3a3a", "separatorType": 1}
```

### 6.8 ComboBox (less tested)

A drop-down list bound to a parameter.

| Key | Type | Description |
| --- | --- | --- |
| `version` | int | exactly `1`. |
| `textStyle` | TextStyleData | See 6.3. |
| `handleName` | handle | |

```json
{
  "version": 1,
  "textStyle": {"version": 1, "font": {"version": 1, "name": "Titillium Web", "style": "Regular", "height": 26.0}, "colour": "ffffffff", "justification": "left verticallyCentred", "case": "Original"},
  "handleName": "Data"
}
```

Known limitation: for an ordinary continuous VST parameter the list is empty,
because the plugin cannot tell the MPC what the choices are. Prefer a
`replacementStrings` entry plus a `Slider`/`Label` that opens a `MenuOverlay`
(6.11), which is what the bundled skins do.

### 6.9 Overlay widgets: common rules (well-trodden)

Overlays are the full-screen editors the MPC pops up when a control is
double-tapped or Enter is pressed on it. To use one:

1. Define a custom component (say `"knob overlay"`) with
   `ignoreMousePresses: true`, a transparent background, and a single child:
   the overlay widget at bounds `"0 0 1280 800"`.
2. On the control that should open it, add actions
   `Double Click -> Show Overlay` and `Enter Pressed -> Show Overlay` with
   `additionalData` set to the component name and `handleName` set to the
   handle to edit.
3. In the overlay widget's `data`, write the two `version` keys (1.8) and an
   `actions` array containing the `Focus Lost` action, which closes the
   overlay when the user taps outside it or presses Enter again:

```json
"actions": [
  {"version": 1, "onAction": "Focus Lost", "handler": "Focus Lost", "handleName": "Focus Lost", "additionalData": ""}
]
```

The `onAction` enum inside an overlay's `actions` is `"Invalid"` or
`"Focus Lost"` only (it is a different action type from the custom component
actions of 4.2, though the keys look the same).

Overlay component definition (shared by all overlay types; only the child
changes):

```json
{
  "key": "knob overlay",
  "value": {
    "version": 2,
    "actions": [],
    "backgroundData": {"version": 1, "focussed": {"version": 1, "colour": "0", "image": ""}, "unfocussed": {"version": 1, "colour": "0", "image": ""}},
    "ignoreMousePresses": true,
    "disableCoarseDataWheel": false,
    "componentsData": [
      {
        "version": 2,
        "componentData": {
          "version": 1,
          "name": "overlay",
          "type": "KnobOverlay",
          "data": {
            "version": 2,
            "version": 1,
            "actions": [{"version": 1, "onAction": "Focus Lost", "handler": "Focus Lost", "handleName": "Focus Lost", "additionalData": ""}],
            "overlayType": "Knob With Header",
            "scaleType": "Linear",
            "polarityType": "Unipolar",
            "showMinusSign": true,
            "handleName": "Data"
          }
        },
        "handle remapping": {"version": 1, "map": []},
        "bounds": {"version": 1, "acceptsHWFocus": "Yes", "showWhenDataModelInvalid": "Show", "whenVisible": "Always", "boundsType": "Absolute", "bounds": "0 0 1280 800"}
      }
    ]
  }
}
```

### 6.10 KnobOverlay (well-trodden)

The large value editor.

| Key | Type | Versions | Values / default | Description |
| --- | --- | --- | --- | --- |
| `version` (1st) | int | | at most `2` | Use `2`. |
| `version` (2nd) | int | | exactly `1` | Overlay base version. |
| `actions` | array | | | Overlay actions (6.9). |
| `overlayType` | enum | | `"Knob With Header"`, `"Slider"`, `"HorizontalBarSlider"`, `"FaderCapGeneric"`, `"FaderCapMidi"`, `"FaderCapVolume"`, `"FaderThresholdArrow"`, `"FaderCapMinimal"` | Visual style. `Knob With Header` is the usual one. |
| `scaleType` | enum | | `"Square Law"`, `"Square Law 24dB"`, `"Square Law Attenuating"`, `"Linear"`, `"Linear dB"` | How the graphic maps the 0..1 value to its arc/length. `Linear` for most parameters. |
| `polarityType` | enum | only for `Knob With Header`, `Slider`, `HorizontalBarSlider` | `"Unipolar"`, `"Bipolar"` | |
| `showMinusSign` | bool | only for the same three types | `true` | |
| `handleName` | handle | | `"Data"` | |

Key order: `version, version, actions, overlayType, scaleType, [polarityType, showMinusSign], handleName`.

For the fader-style `overlayType` values omit `polarityType` and
`showMinusSign`:

```json
{
  "version": 2,
  "version": 1,
  "actions": [{"version": 1, "onAction": "Focus Lost", "handler": "Focus Lost", "handleName": "Focus Lost", "additionalData": ""}],
  "overlayType": "FaderCapVolume",
  "scaleType": "Linear dB",
  "handleName": "Data"
}
```

### 6.11 MenuOverlay (well-trodden)

A full-screen list of the parameter's choices. Only useful with a parameter
that has discrete steps, i.e. one listed in `replacementStrings`.

| Key | Type | Values / default | Description |
| --- | --- | --- | --- |
| `version` (1st) | int | exactly `1` | |
| `version` (2nd) | int | exactly `1` | Overlay base. |
| `actions` | array | | Overlay actions (6.9). |
| `withNumbers` | bool | `false` | Prefix each entry with its index. |
| `handleName` | handle | `"Data"` | |

```json
{
  "version": 1,
  "version": 1,
  "actions": [{"version": 1, "onAction": "Focus Lost", "handler": "Focus Lost", "handleName": "Focus Lost", "additionalData": ""}],
  "withNumbers": false,
  "handleName": "Data"
}
```

### 6.12 NumericOverlay and TextEditOverlay (less tested)

`NumericOverlay` shows a numeric keypad for the value; `TextEditOverlay` shows
a keyboard (meaningful only for text-valued handles, which third-party VST
parameters are not). Both have the same data shape:

```json
{
  "version": 1,
  "version": 1,
  "actions": [{"version": 1, "onAction": "Focus Lost", "handler": "Focus Lost", "handleName": "Focus Lost", "additionalData": ""}],
  "handleName": "Data"
}
```

### 6.13 Envelope and EnvelopeOverlay (advanced; test on hardware)

Draws an editable envelope curve whose stages are bound to separate parameter
handles. `EnvelopeOverlay` is the same widget as a full-screen overlay (its
data starts with the two `version` keys and `actions`, then the Envelope keys).

The key list is long and the set of stage handles present depends on
`envelopeType`. The recommended, best-understood configuration is `ADSR`.

| Key | Type | Values / default | Description |
| --- | --- | --- | --- |
| `version` | int | exactly `1` | (`EnvelopeOverlay`: `1`, then overlay base `1`, then `actions`.) |
| `envelopeType` | enum | `"AD"`, `"AHDS"`, `"ADSR"`, `"ADSFR"`, `"Pitch"`, `"DADDSR"`, `"DADSR"`, `"DAHDSR"`, `"Pumper"`, `"AHDSR"` | Which stages exist. |
| `envelopeStyle` | enum | `"WithHeader"`, `"WithoutHeader"` | Header shows `envelopeName`. |
| `envelopeName` | string | | Title text. |
| `backgroundColour` | colour | | |
| `scaleColour` | colour | | Grid/scale lines. |
| `lineColour` | colour | | Curve. |
| `fillColour` | colour | | Area under the curve. |
| `highlightColour` | colour | | Active stage highlight. |
| `lineWidth` | int | `3` | |
| `handleWidth` | int | `15` | Drag handle size. |
| `handleHeight` | int | `10` | |
| `handleShape` | enum | `"Circle"`, `"Square"` | |
| `handleLineWidth` | int | | |
| `handleLineColour` | colour | | |
| `handleFillColour` | colour | | |
| `handleTextColour` | colour | | |
| `handleLineWidthOnPressed` | int | | |
| `handleLineColourOnPressed` | colour | | |
| `handleFillColourOnPressed` | colour | | |
| `handleTextColourOnPressed` | colour | | |
| `handleArrowLineColourOnPressed` | colour | | |
| `handleArrowFillColourOnPressed` | colour | | |
| *stage handles* | handle | | Present according to `envelopeType`; see the table below, in this order. |
| `forceHideShapeHandles` | bool, optional | `false` | |
| `forceCurveShape` | float, optional | `0.5` | |
| `decayAndReleaseCurvesLinked` | bool, optional | | |
| `curveType` | enum, optional | `"Quadratic"`, `"Exponential"` | |

Stage handle keys by envelope type (write them in this order, only those that
apply):

| Key | Present for |
| --- | --- |
| `delayHandle` | `DADDSR`, `DADSR`, `DAHDSR` |
| `attackHandle` | all except `Pitch` |
| `holdHandle` | `AHDS`, `AHDSR`, `DAHDSR`, `Pumper` |
| `decay1Handle`, `level1Handle` | `DADDSR` |
| `decayHandle` | all except `Pitch`, `Pumper` |
| `sustainHandle` | all except `AD`, `Pitch`, `Pumper` |
| `fadeHandle` | `ADSFR` |
| `releaseHandle` | `AHDSR`, `ADSR`, `ADSFR`, `DADDSR`, `DADSR`, `DAHDSR`, `Pumper` |
| `decayFromEndHandle` | `AD` |
| `pitchEnvelopeLevel1Handle`, `pitchEnvelopeDelayHandle`, `pitchEnvelopeLevel2Handle`, `pitchEnvelopeAttackHandle`, `pitchEnvelopeLevel3Handle`, `pitchEnvelopeDecayHandle`, `pitchEnvelopeSustainHandle`, `pitchEnvelopeReleaseHandle`, `pitchEnvelopeLevel5Handle` | `Pitch` |
| `attackShapeHandle`, `decay1ShapeHandle`, `decayShapeHandle`, `releaseShapeHandle` | Shape (curve) handles for the corresponding stages; bind to `"Invalid"` when the plugin has no shape parameters. |
| `currentPositionHandle`, `depthHandle` | `Pumper` |

An ADSR envelope bound to four parameters:

```json
{
  "version": 1,
  "envelopeType": "ADSR",
  "envelopeStyle": "WithHeader",
  "envelopeName": "AMP ENV",
  "backgroundColour": "ff1e1d1e",
  "scaleColour": "ff3a3a3a",
  "lineColour": "ffd31145",
  "fillColour": "40d31145",
  "highlightColour": "ffffffff",
  "lineWidth": 3,
  "handleWidth": 15,
  "handleHeight": 10,
  "handleShape": "Circle",
  "handleLineWidth": 2,
  "handleLineColour": "ffffffff",
  "handleFillColour": "ff1e1d1e",
  "handleTextColour": "ffffffff",
  "handleLineWidthOnPressed": 2,
  "handleLineColourOnPressed": "ffffffff",
  "handleFillColourOnPressed": "ffd31145",
  "handleTextColourOnPressed": "ffffffff",
  "handleArrowLineColourOnPressed": "ffffffff",
  "handleArrowFillColourOnPressed": "ffd31145",
  "attackHandle": "Parameter 5",
  "decayHandle": "Parameter 6",
  "sustainHandle": "Parameter 7",
  "releaseHandle": "Parameter 8",
  "attackShapeHandle": "Invalid",
  "decayShapeHandle": "Invalid",
  "releaseShapeHandle": "Invalid",
  "forceHideShapeHandles": true,
  "forceCurveShape": 0.5,
  "curveType": "Quadratic"
}
```

Binds to: one parameter per stage. Dragging a stage handle edits that
parameter. The exact per-type key presence is subtle; if a file with an
`Envelope` fails to load, the stage-handle set is the first thing to check.

### 6.14 XYPad (less tested)

A two-dimensional touch pad.

| Key | Type | Description |
| --- | --- | --- |
| `version` | int | `1`. |
| `xPositionHandle` | handle | Parameter driven by the horizontal position. |
| `yPositionHandle` | handle | Parameter driven by the vertical position. |
| `image` | path | Puck image drawn at the position. |

```json
{"version": 1, "xPositionHandle": "Parameter 2", "yPositionHandle": "Parameter 3", "image": "puck.png"}
```

### 6.15 Meter (less tested)

A level meter drawn from images.

| Key | Type | Versions | Values / default | Description |
| --- | --- | --- | --- | --- |
| `version` | int | | at most `3` | |
| `direction` | enum | | `"Up"`, `"Down"`, `"Left"`, `"Right"`, `"UpDown"`, `"LeftRight"` | |
| `invert` | bool | | | |
| `inactiveImage` | path | | | Full meter, unlit. |
| `peakImage` | path | | | Revealed up to the peak value. |
| `rmsImage` | path | | | Revealed up to the RMS value. |
| `peakHandle` | handle | | | |
| `rmsHandle` | handle | | | |
| `peakHoldHandle` | handle | | | |
| `peakHoldSize` | int | 3 | `10` | Peak-hold bar thickness. |
| `peakHoldColour` | colour | 3 | | |

Note for third-party plugins: the MPC has no audio-level feed from a VST
plugin, so the only way to drive a `Meter` is from a **parameter** the plugin
updates itself (write the level into a read-only parameter from the audio
thread). `Hold/` (section 7) can provide the peak hold.

```json
{"version": 3, "direction": "Up", "invert": false, "inactiveImage": "meter_off.png", "peakImage": "meter_peak.png", "rmsImage": "meter_rms.png", "peakHandle": "Parameter 12", "rmsHandle": "Parameter 12", "peakHoldHandle": "Hold/Parameter 12", "peakHoldSize": 10, "peakHoldColour": "ffffffff"}
```

### 6.16 Plotter (less tested)

Draws a value as a line with optional history (a scrolling trace).

| Key | Type | Versions | Default | Description |
| --- | --- | --- | --- | --- |
| `version` | int | | at most `2` | |
| `backgroundColour` | colour | | | |
| `drawLine` | bool, optional | 2 | `true` | |
| `lineDirection` | enum, optional | 2 | | `"Up"`, `"Down"`, `"Left"`, `"Right"`. |
| `tightClip` | bool, optional | 2 | | |
| `lineColourStart` | colour | | | |
| `lineColourEnd` | colour | | | |
| `horizLineGradient` | bool | | | |
| `lineWidth` | float | | | |
| `fillColourStart` | colour | | | |
| `fillColourEnd` | colour | | | |
| `horizFillGradient` | bool | | | |
| `fillZero` | float | | | Value level the fill is drawn from. |
| `dataHandle` | handle | | | |
| `dataMin` | float, optional | | `0` | |
| `dataMax` | float, optional | | `1` | |
| `drawHistory` | bool, optional | | | |
| `historyDirection` | enum, optional | | | `"Up"`, `"Down"`, `"Left"`, `"Right"`. |
| `historyColour1` | colour, optional | | | |
| `historyColour2` | colour, optional | | | |
| `historyTime` | float, optional | | `10` | Seconds of history. |
| `historySkipFrames` | int, optional | | `5` | |

```json
{"version": 1, "backgroundColour": "ff1e1d1e", "lineColourStart": "ffd31145", "lineColourEnd": "ffd31145", "horizLineGradient": false, "lineWidth": 2.0, "fillColourStart": "40d31145", "fillColourEnd": "00d31145", "horizFillGradient": false, "fillZero": 0.0, "dataHandle": "Parameter 12"}
```

### 6.17 HaloDisplay (less tested)

An arc or bar "halo" showing a value, typically drawn around a knob.

| Key | Type | Default | Description |
| --- | --- | --- | --- |
| `version` | int | `1` | |
| `lineWidth` | float | `5` | |
| `lineColour` | colour | | |
| `clipColour` | colour | | Colour when the value is at maximum. |
| `style` | enum | | `"Circular"`, `"Horizontal"`, `"Vertical"`. |
| `handleName` | handle | | |
| `arcMin` | float | `-0.67` | `Circular` only: start angle as a fraction of a turn relative to the top. |
| `arcMax` | float | `0.67` | `Circular` only. |

```json
{"version": 1, "lineWidth": 5.0, "lineColour": "ffd31145", "clipColour": "ffff0000", "style": "Circular", "handleName": "Data", "arcMin": -0.67, "arcMax": 0.67}
```

### 6.18 Indicator (less tested)

A non-interactive lamp: like `Button` but it does not respond to touch.

| Key | Type | Description |
| --- | --- | --- |
| `version` | int | `1`. |
| `onImage` | path | |
| `offImage` | path | |
| `indicatorId` | int | Lit when the parameter's step index equals this. |
| `numIndicatorsInGroup` | int | Number of steps the parameter is quantised to (at least 2). |
| `handleName` | handle | |

```json
{"version": 1, "onImage": "lamp_on.png", "offImage": "lamp_off.png", "indicatorId": 1, "numIndicatorsInGroup": 1, "handleName": "Data"}
```

### 6.19 Path (less tested)

Draws a vector path, optionally scaled by a value.

| Key | Type | Description |
| --- | --- | --- |
| `version` | int | `1`. |
| `handleName` | handle | |
| `path` | string | A path description string: a sequence of `m x y` (move), `l x y` (line), `q x1 y1 x2 y2` (quadratic), `c x1 y1 x2 y2 x3 y3` (cubic) and `z` (close) commands. |
| `drawPath` | bool | |
| `colour` | colour | |
| `opacity` | float | 0..1. |

```json
{"version": 1, "handleName": "Invalid", "path": "m 0 0 l 100 0 l 100 100 l 0 100 z", "drawPath": true, "colour": "ff6a6a6c", "opacity": 1.0}
```

### 6.20 Desktop-oriented widgets (not recommended on hardware)

`GuiButton`, `GuiComboBox`, `GuiKnob`, `EditNumberSlider` and `Decorator`
exist for the desktop application's mouse-driven skins. They load on
hardware but are not designed for touch and are not documented here. Use the
touch widgets above.

---

## 7. Handles (data binding)

A handle is a string that names a value a widget reads and writes. Every
`handleName`-type key, every `handle remapping` value, every action
`handleName` and every entry in `additionalInvalidatingHandles` is a handle.

### 7.1 Base handles

| Handle | Meaning |
| --- | --- |
| `Parameter N` | The plugin's VST parameter with **0-based** index `N` (`Parameter 0` is the first). Under `PartialDescription`, `N` is shifted by `tab * offset` (3.4). |
| `Parameter/Named/<name>` | The parameter whose name (as reported by the plugin) is exactly `<name>`, e.g. `Parameter/Named/Cutoff`. Useful when indices may change; the name must match exactly, including case and spaces. |
| `PageIndex T, Q` | The parameter assigned to Q-Link `Q` (1..16) on tab `T` in `Q-Links.json` "Screen Mode Q-Links". `T` is **1-based and relative to the current tab**: `PageIndex 1, 13` is "this tab's Q-Link 13". Sub-tab defaults to 1. |
| `PageIndex T, S, Q` | As above with an explicit 1-based sub-tab `S`. |
| `Invalid` | Unbound. A widget bound to `Invalid` has no value; with `showWhenDataModelInvalid: "Hide"` it disappears. |
| `Data` | Not a real handle: the conventional name of the single handle a custom component exposes to its children, resolved through `handle remapping` (4.5). |
| `Local/<range>/<id>` | UI-only state (7.3). |

`PageIndex` handles are resolved through `Q-Links.json`, which lets one tab
definition be reused on several tabs while each tab's controls follow that
tab's knob assignments. `Parameter N` is simpler and is what the generator
writes.

Separator rules: `PageIndex` uses `", "` (comma space) between numbers.

### 7.2 Modifiers

A modifier wraps a handle: `Modifier/<handle>`. Modifiers nest
(`Invert/Hold/Parameter 3`). A few take arguments in square brackets. The
inner handle may be anything in 7.1, including `Data` inside a reusable
component (then remap `Invert/*` -> `*` or let the modifier pass through).

| Modifier | Meaning | Example |
| --- | --- | --- |
| `Hold/<h>` | Peak-hold of `h`: tracks the maximum and decays slowly. | `Hold/Parameter 12` |
| `Invert/<h>` | `1 - value`. | `Invert/Data` |
| `AsDB/<h>` | Shows a 0..1 value as `-100 .. 0 dB`. | `AsDB/Parameter 9` |
| `AsDBFromGain/<h>` | Shows a linear gain value as dB. | `AsDBFromGain/Parameter 9` |
| `Max[<h1>, <h2>]` | The larger of two handles. The separator is exactly `", "`. | `Max[Parameter 12, Parameter 13]` |
| `Min[<h1>, <h2>]` | The smaller of two handles. | `Min[Parameter 12, Parameter 13]` |
| `Fixed/...` | A constant element with 18 tokens: `Fixed/displayRange a b valueRange a b fineIncrement x coarseIncrement x defaultValue x displayString 'str' name 'str' value x`. | `Fixed/displayRange 0 1 valueRange 0 1 fineIncrement 0.01 coarseIncrement 0.1 defaultValue 0.5 displayString 'Half' name 'Const' value 0.5` |
| `NonZero/<h>` | Valid only while `h` is not zero (use for visibility). | `NonZero/Parameter 4` |
| `DisableCoarseIncrement/<h>` | Data wheel always uses the fine increment. | `DisableCoarseIncrement/Data` |
| `FromGesture/<h>` | The value only changes from a touch gesture (not from the data wheel or knobs). | `FromGesture/Data` |
| `FilterEmptyStrings/<h>` | Invalid while the display string is empty. | `FilterEmptyStrings/Parameter 2` |
| `IndexedEnabling/<i>/<n>/<h>` | Valid only while `h`, quantised to `n` steps, is at step `i` (0-based). The conditional-visibility workhorse (7.4). | `IndexedEnabling/1/4/Parameter 0` |
| `RangedEnabling/<min>/<max>/<n>/<h>` | Valid only while the step index of `h` (quantised to `n` steps) is in `min .. max` inclusive. | `RangedEnabling/1/3/4/Parameter 0` |
| `Local/<range>/<id>` | A UI-only integer `0 .. range-1` named `id`, remembered per page. Not a plugin parameter. | `Local/2/showAdvanced` |
| `TabLimited/<page>/<tab>/<reset>/<h>` | `h`, but only live while the given page/tab is shown; `reset` (0/1) says whether it resets when leaving. | `TabLimited/1/1/1/Parameter 3` |
| `WithPrefix[str]/<h>` | Prefixes the display string. | `WithPrefix[> ]/Data` |
| `WithSuffix[str]/<h>` | Suffixes the display string. | `WithSuffix[ Hz]/Parameter 2` |
| `AffineMap[k,a]/<h>` | Shows `k * value + a`. | `AffineMap[100,0]/Parameter 7` |
| `DisplayValue[%fmt]/<h>` | Formats the numeric value with a printf-style format instead of the plugin's display string. | `DisplayValue[%.1f]/Parameter 3` |
| `WithName[Label]/<h>` | Replaces the parameter name shown by `Name` labels and overlays. | `WithName[CUTOFF]/Parameter 2` |
| `ResetOnGestureEnd/<h>` | Returns to the default value when the finger lifts (momentary control). | `ResetOnGestureEnd/Parameter 11` |
| `ModulatedParameter[value,modAmount(,polarity,min,max)]` | Displays a value with a modulation amount overlaid; rarely useful for VST plugins. | |
| `CommandGroup<n>[cmd]/<h>` | Internal command grouping; not needed for third-party plugins. | |

Display modifiers (`WithPrefix`, `WithSuffix`, `AffineMap`, `DisplayValue`,
`WithName`, `AsDB`) change only what is **shown**; the value sent to the plugin
is unchanged. Normally you need none of them, because the MPC shows the
plugin's own parameter name and display string.

### 7.3 Local handles (UI-only state)

`Local/<range>/<id>` creates a value that lives only in the UI, with integer
steps `0 .. range-1`, identified by `<id>`. Every widget on the page that names
the same id shares it. Use it for a UI-only toggle that shows or hides a panel:

```json
{
  "version": 2,
  "componentData": {"version": 1, "name": "advanced toggle", "type": "Button", "data": {"version": 2, "onImage": "adv_on.png", "offImage": "adv_off.png", "buttonId": 1, "numButtonsInGroup": 1, "handleName": "Local/2/showAdvanced", "gestureBehaviour": "Instant"}},
  "handle remapping": {"version": 1, "map": []},
  "bounds": {"version": 1, "acceptsHWFocus": "Yes", "showWhenDataModelInvalid": "Show", "whenVisible": "Always", "boundsType": "Absolute", "bounds": "1100 20 120 40"}
}
```

and on the panel entry:

```json
"bounds": {
  "version": 2,
  "acceptsHWFocus": "Children",
  "showWhenDataModelInvalid": "Show",
  "whenVisible": "Always",
  "boundsType": "Absolute",
  "bounds": "0 300 1280 328",
  "additionalInvalidatingHandles": ["IndexedEnabling/1/2/Local/2/showAdvanced"]
}
```

The panel is visible only while the local value is `1`. The `Toggle Switch`
action handler also works with a `Local/` handle, for tap-to-toggle on any
component.

### 7.4 Conditional visibility recipes

**Show a control only when a choice parameter has a given value.** Use a
version-2 `bounds` with `IndexedEnabling/<choice>/<numChoices>/<handle>` in
`additionalInvalidatingHandles`. Here "Detune" (parameter 1) is shown only
while "Wave" (parameter 0, four choices) is at choice 1 (`Saw`):

```json
"bounds": {
  "version": 2,
  "acceptsHWFocus": "Yes",
  "showWhenDataModelInvalid": "Hide",
  "whenVisible": "Always",
  "boundsType": "Absolute",
  "bounds": "343 45 278 107",
  "additionalInvalidatingHandles": ["IndexedEnabling/1/4/Parameter 0"]
}
```

`numChoices` must equal the number of steps the controlling parameter has (the
number of strings in its `replacementStrings` entry; `2` for a boolean).

**Show for a range of choices**: `RangedEnabling/1/2/4/Parameter 0` (choices 1
and 2 of 4).

**Show when a parameter is non-zero**: `NonZero/Parameter 4`.

**Hide controls that the plugin does not have**: `showWhenDataModelInvalid:
"Hide"` on every parameter-bound entry (no `additionalInvalidatingHandles`
needed).

A hidden component cannot take hardware focus, so cursor navigation skips it.

---

## 8. Q-Links.json schema

`Q-Links.json` tells the MPC which parameter each of the 16 Q-Link knobs
controls on each tab, and which parameters the knobs control in the
hardware's Program (Track) Q-Link mode.

### 8.1 Shape

```json
{
  "version": 4,
  "info": {"version": 1, "type": "CompleteDescription"},
  "Screen Mode Q-Links": {
    "version": 4,
    "map": [
      {
        "Tab": 1,
        "SubTab": 1,
        "Bank Direction": "Column",
        "Q-Links": {
          "Q-Link 1": 3,  "Q-Link 2": 7,  "Q-Link 3": 11, "Q-Link 4": 15,
          "Q-Link 5": 2,  "Q-Link 6": 6,  "Q-Link 7": 10, "Q-Link 8": 14,
          "Q-Link 9": 1,  "Q-Link 10": 5, "Q-Link 11": 9, "Q-Link 12": 13,
          "Q-Link 13": 0, "Q-Link 14": 4, "Q-Link 15": 8, "Q-Link 16": 12
        }
      }
    ]
  },
  "Program Mode Q-Links": {
    "Q-Link 1": 0,  "Q-Link 2": 1,  "Q-Link 3": 2,  "Q-Link 4": 3,
    "Q-Link 5": 4,  "Q-Link 6": 5,  "Q-Link 7": 6,  "Q-Link 8": 7,
    "Q-Link 9": 8,  "Q-Link 10": 9, "Q-Link 11": 10, "Q-Link 12": 11,
    "Q-Link 13": 12, "Q-Link 14": 13, "Q-Link 15": 14, "Q-Link 16": 15
  }
}
```

| Key | Type | Required | Rule | Description |
| --- | --- | --- | --- | --- |
| `version` | int | yes | `4` | |
| `info` | OffsetInfo | yes | | Same object as `pageData.info` in `TUI.json` (3.4). With `PartialDescription` the single map entry is repeated with shifted parameter indices. |
| `Screen Mode Q-Links.version` | int | yes | `4` | |
| `Screen Mode Q-Links.map` | array | yes | | One entry per tab/sub-tab. |
| `map[].Tab` | int | yes | | **1-based** tab number (the first tab is `1`). Tabs are numbered in the order of their function keys. |
| `map[].SubTab` | int | yes | | **1-based** sub-tab number; `1` when the tab has no sub-tabs. |
| `map[]."Bank Direction"` | enum | yes | `"Column"`, `"ColumnBottomUp"`, `"Row"`, `"RowTopDown"` | How hardware with fewer than 16 knobs banks through the 16 assignments. See 8.3. |
| `map[]."Q-Links"` | object | yes | | Keys `"Q-Link 1"` .. `"Q-Link 16"`, all present. Value: **0-based** parameter index, or `-1` for unassigned. |
| `Program Mode Q-Links` | object | yes | | Keys `"Q-Link 1"` .. `"Q-Link 16"`. The parameters the knobs control in Program/Track Q-Link mode, i.e. when the user has banked the knobs to "the program's plugin" rather than to the screen. Use the plugin's 16 most important parameters, or the first 16 in index order. |

Entries for a tab/sub-tab that is not in the map have no assignments. A tab
whose controls are bound with `Parameter N` still works on the touch screen
without a map entry; only the knobs are unassigned.

### 8.2 Physical knob numbering (4 x 4 layout)

On a 16-knob MPC the Q-Links are a 4 x 4 grid of knobs to the right of the
screen, numbered from the **bottom-left**:

```
 13  14  15  16      <- top row
  9  10  11  12
  5   6   7   8
  1   2   3   4      <- bottom row
```

So Q-Link 1 is bottom-left, 13 is top-left, 16 is top-right, and a column read
top to bottom is 13, 9, 5, 1. A screen slot at row `r`, column `c` (0-based,
row 0 at the top) corresponds to Q-Link `13 + c - 4 * r`, which is how the
default grid (1.4) lines up with the knobs: the knob physically next to a
control edits that control.

The default mapping for a 16-parameter page runs parameters **down the
columns from the top-left**:

| Q-Link | 13 | 9 | 5 | 1 | 14 | 10 | 6 | 2 | 15 | 11 | 7 | 3 | 16 | 12 | 8 | 4 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Parameter | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 |

The default `Program Mode Q-Links` is `Q-Link n -> parameter n - 1`.

### 8.3 Bank Direction

Hardware with fewer than 16 knobs (a single column of 4, a row of 8, or one
knob) reaches all 16 assignments by banking. `Bank Direction` says in which
order the 16 slots are grouped into banks:

| Value | Banks are |
| --- | --- |
| `Column` | Columns of the 4 x 4 grid, left to right, each read top to bottom (13, 9, 5, 1 then 14, 10, 6, 2 ...). Use this with the default column-major mapping. |
| `ColumnBottomUp` | Columns, each read bottom to top (1, 5, 9, 13 ...). |
| `Row` | Rows, bottom row first (1, 2, 3, 4 then 5, 6, 7, 8 ...). |
| `RowTopDown` | Rows, top row first (13, 14, 15, 16 then 9, 10, 11, 12 ...). |

The `Q-Link` action handler (4.2) selects the bank that contains the touched
control's parameter, so with the right direction a touch always brings the
relevant knobs under the user's hand.

### 8.4 Hardware variant files

Both `TUI` and `Q-Links` may have hardware-specific variants beside the plain
file. The MPC tries these base names in order and uses the first that exists:

| Order | Suffix | Examples |
| --- | --- | --- |
| 1 | ` - <product name>` | `TUI - MPC X.json`, `Q-Links - MPC Live Mk II.json`, `TUI - MPC One+.json`, `TUI - MPC Key 61.json`, `TUI - Force.json` |
| 2 | ` - <product family>` | `TUI - MPC.json`, `TUI - Force.json` |
| 3 | ` - <Q-Link layout>` | `Q-Links - 4by4.json`, `Q-Links - 1by4.json`, `Q-Links - 8by1.json`, `Q-Links - Single.json` (and the same suffixes on `TUI`) |
| 4 | none | `TUI.json`, `Q-Links.json` |

The most specific match wins. `TUI.json` and `Q-Links.json` with no suffix
must always exist as the fallback. The layouts are: `4by4` (16 knobs in a 4 x 4
grid), `8by1` (a row of 8), `1by4` (a column of 4), `Single` (one knob).

### 8.5 Behaviour when missing or broken

If `Q-Links.json` is absent or fails to parse, the MPC assigns knobs
sequentially: parameter `i` goes to page `i / 16` (named `Page n`) at Q-Link
`(i mod 16) + 1` with `Bank Direction` `Column`. This does not match a
column-major screen layout, so always ship a `Q-Links.json`.

---

## 9. Complete worked example

A two-page skin for a synth with ten parameters:

| Index | Name | Kind | Page | Slot | Q-Link |
| --- | --- | --- | --- | --- | --- |
| 0 | Wave | 4-way choice: Sine, Saw, Square, Noise | OSC | row 0, col 0 | 13 |
| 1 | Detune | continuous; shown only when Wave = Saw | OSC | row 0, col 1 | 14 |
| 2 | Cutoff | continuous (knob) | OSC | row 1, col 0 | 9 |
| 3 | Resonance | continuous (knob) | OSC | row 1, col 1 | 10 |
| 4 | Env Amount | continuous | OSC | row 2, col 0 | 5 |
| 9 | Volume | continuous | OSC | row 3, col 0 | 1 |
| 5 | Attack | continuous | ENV | row 0, col 0 | 13 |
| 6 | Decay | continuous | ENV | row 0, col 1 | 14 |
| 7 | Sustain | continuous | ENV | row 0, col 2 | 15 |
| 8 | Release | continuous | ENV | row 0, col 3 | 16 |

Five local components are defined inline:

- `sliderWithFocus`: focus ring + generic slider; touch selects the Q-Link
  bank, double-tap/Enter opens `knob overlay`.
- `knobWithFocus`: focus ring + generic knob + name label; same actions.
- `choiceWithFocus`: like `sliderWithFocus` but opens `menu overlay`.
- `knob overlay`: full-screen `KnobOverlay`.
- `menu overlay`: full-screen `MenuOverlay`.

Both files are complete and can be copied as-is (remember the duplicate
`version` keys in the two overlay widgets are intentional).

### 9.1 Plugin Skins/TUI.json

```json
{
  "pageData": {
    "version": 1,
    "componentDefinitions": {
      "version": 2,
      "importFiles": [],
      "localComponentDefinitions": [
        {
          "key": "sliderWithFocus",
          "value": {
            "version": 2,
            "actions": [
              {"version": 1, "onAction": "Mouse Down", "handler": "Q-Link", "handleName": "Data", "additionalData": ""},
              {"version": 1, "onAction": "Double Click", "handler": "Show Overlay", "handleName": "Data", "additionalData": "knob overlay"},
              {"version": 1, "onAction": "Enter Pressed", "handler": "Show Overlay", "handleName": "Data", "additionalData": "knob overlay"}
            ],
            "backgroundData": {
              "version": 1,
              "focussed": {"version": 1, "colour": "0", "image": ""},
              "unfocussed": {"version": 1, "colour": "0", "image": ""}
            },
            "ignoreMousePresses": false,
            "disableCoarseDataWheel": false,
            "componentsData": [
              {
                "version": 2,
                "componentData": {
                  "version": 1,
                  "name": "focus",
                  "type": "Focus",
                  "data": {"version": 1, "backgroundColour": "19ffffff", "outlineColour": "ffe81f40", "backgroundInset": 4.0, "outlineThickness": 1.0}
                },
                "handle remapping": {"version": 1, "map": []},
                "bounds": {"version": 1, "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Show", "whenVisible": "WhenFocussed", "boundsType": "Absolute", "bounds": "0 0 278 107"}
              },
              {
                "version": 2,
                "componentData": {
                  "version": 1,
                  "name": "slider",
                  "type": "Slider",
                  "data": {"version": 1, "handleName": "Data"}
                },
                "handle remapping": {"version": 1, "map": []},
                "bounds": {"version": 1, "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Show", "whenVisible": "Always", "boundsType": "Absolute", "bounds": "15 15 248 77"}
              }
            ]
          }
        },
        {
          "key": "knobWithFocus",
          "value": {
            "version": 2,
            "actions": [
              {"version": 1, "onAction": "Mouse Down", "handler": "Q-Link", "handleName": "Data", "additionalData": ""},
              {"version": 1, "onAction": "Double Click", "handler": "Show Overlay", "handleName": "Data", "additionalData": "knob overlay"},
              {"version": 1, "onAction": "Enter Pressed", "handler": "Show Overlay", "handleName": "Data", "additionalData": "knob overlay"}
            ],
            "backgroundData": {
              "version": 1,
              "focussed": {"version": 1, "colour": "0", "image": ""},
              "unfocussed": {"version": 1, "colour": "0", "image": ""}
            },
            "ignoreMousePresses": false,
            "disableCoarseDataWheel": false,
            "componentsData": [
              {
                "version": 2,
                "componentData": {
                  "version": 1,
                  "name": "focus",
                  "type": "Focus",
                  "data": {"version": 1, "backgroundColour": "19ffffff", "outlineColour": "ffe81f40", "backgroundInset": 4.0, "outlineThickness": 1.0}
                },
                "handle remapping": {"version": 1, "map": []},
                "bounds": {"version": 1, "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Show", "whenVisible": "WhenFocussed", "boundsType": "Absolute", "bounds": "0 0 278 107"}
              },
              {
                "version": 2,
                "componentData": {
                  "version": 1,
                  "name": "knob",
                  "type": "Knob",
                  "data": {"version": 5, "knobType": "GenericLegacy", "polarity": "UniPolar", "dragOrientation": "Vertical", "handleName": "Data"}
                },
                "handle remapping": {"version": 1, "map": []},
                "bounds": {"version": 1, "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Show", "whenVisible": "Always", "boundsType": "Absolute", "bounds": "15 10 90 90"}
              },
              {
                "version": 2,
                "componentData": {
                  "version": 1,
                  "name": "name",
                  "type": "Label",
                  "data": {
                    "version": 1,
                    "textStyle": {"version": 1, "font": {"version": 1, "name": "Titillium Web", "style": "SemiBold", "height": 26.0}, "colour": "ffffffff", "justification": "left verticallyCentred", "case": "Upper Case"},
                    "type": "Name",
                    "handleName": "Data"
                  }
                },
                "handle remapping": {"version": 1, "map": []},
                "bounds": {"version": 1, "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Show", "whenVisible": "Always", "boundsType": "Absolute", "bounds": "115 15 150 40"}
              }
            ]
          }
        },
        {
          "key": "choiceWithFocus",
          "value": {
            "version": 2,
            "actions": [
              {"version": 1, "onAction": "Mouse Down", "handler": "Q-Link", "handleName": "Data", "additionalData": ""},
              {"version": 1, "onAction": "Double Click", "handler": "Show Overlay", "handleName": "Data", "additionalData": "menu overlay"},
              {"version": 1, "onAction": "Enter Pressed", "handler": "Show Overlay", "handleName": "Data", "additionalData": "menu overlay"}
            ],
            "backgroundData": {
              "version": 1,
              "focussed": {"version": 1, "colour": "0", "image": ""},
              "unfocussed": {"version": 1, "colour": "0", "image": ""}
            },
            "ignoreMousePresses": false,
            "disableCoarseDataWheel": false,
            "componentsData": [
              {
                "version": 2,
                "componentData": {
                  "version": 1,
                  "name": "focus",
                  "type": "Focus",
                  "data": {"version": 1, "backgroundColour": "19ffffff", "outlineColour": "ffe81f40", "backgroundInset": 4.0, "outlineThickness": 1.0}
                },
                "handle remapping": {"version": 1, "map": []},
                "bounds": {"version": 1, "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Show", "whenVisible": "WhenFocussed", "boundsType": "Absolute", "bounds": "0 0 278 107"}
              },
              {
                "version": 2,
                "componentData": {
                  "version": 1,
                  "name": "slider",
                  "type": "Slider",
                  "data": {"version": 1, "handleName": "Data"}
                },
                "handle remapping": {"version": 1, "map": []},
                "bounds": {"version": 1, "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Show", "whenVisible": "Always", "boundsType": "Absolute", "bounds": "15 15 248 77"}
              }
            ]
          }
        },
        {
          "key": "knob overlay",
          "value": {
            "version": 2,
            "actions": [],
            "backgroundData": {
              "version": 1,
              "focussed": {"version": 1, "colour": "0", "image": ""},
              "unfocussed": {"version": 1, "colour": "0", "image": ""}
            },
            "ignoreMousePresses": true,
            "disableCoarseDataWheel": false,
            "componentsData": [
              {
                "version": 2,
                "componentData": {
                  "version": 1,
                  "name": "overlay",
                  "type": "KnobOverlay",
                  "data": {
                    "version": 2,
                    "version": 1,
                    "actions": [
                      {"version": 1, "onAction": "Focus Lost", "handler": "Focus Lost", "handleName": "Focus Lost", "additionalData": ""}
                    ],
                    "overlayType": "Knob With Header",
                    "scaleType": "Linear",
                    "polarityType": "Unipolar",
                    "showMinusSign": true,
                    "handleName": "Data"
                  }
                },
                "handle remapping": {"version": 1, "map": []},
                "bounds": {"version": 1, "acceptsHWFocus": "Yes", "showWhenDataModelInvalid": "Show", "whenVisible": "Always", "boundsType": "Absolute", "bounds": "0 0 1280 800"}
              }
            ]
          }
        },
        {
          "key": "menu overlay",
          "value": {
            "version": 2,
            "actions": [],
            "backgroundData": {
              "version": 1,
              "focussed": {"version": 1, "colour": "0", "image": ""},
              "unfocussed": {"version": 1, "colour": "0", "image": ""}
            },
            "ignoreMousePresses": true,
            "disableCoarseDataWheel": false,
            "componentsData": [
              {
                "version": 2,
                "componentData": {
                  "version": 1,
                  "name": "overlay",
                  "type": "MenuOverlay",
                  "data": {
                    "version": 1,
                    "version": 1,
                    "actions": [
                      {"version": 1, "onAction": "Focus Lost", "handler": "Focus Lost", "handleName": "Focus Lost", "additionalData": ""}
                    ],
                    "withNumbers": false,
                    "handleName": "Data"
                  }
                },
                "handle remapping": {"version": 1, "map": []},
                "bounds": {"version": 1, "acceptsHWFocus": "Yes", "showWhenDataModelInvalid": "Show", "whenVisible": "Always", "boundsType": "Absolute", "bounds": "0 0 1280 800"}
              }
            ]
          }
        }
      ]
    },
    "replacementStrings": [
      {"key": "Parameter 0", "value": ["Sine", "Saw", "Square", "Noise"]}
    ],
    "info": {"version": 1, "type": "CompleteDescription"},
    "tabs": [
      {
        "version": 1,
        "tabName": "OSC",
        "fnKeyIndex": 0,
        "fnKeySubIndex": 0,
        "componentDefinition": {
          "version": 2,
          "actions": [],
          "backgroundData": {
            "version": 1,
            "focussed": {"version": 1, "colour": "ff1e1d1e", "image": ""},
            "unfocussed": {"version": 1, "colour": "ff1e1d1e", "image": ""}
          },
          "ignoreMousePresses": false,
          "disableCoarseDataWheel": false,
          "componentsData": [
            {
              "version": 2,
              "componentData": {"version": 1, "name": "wave", "type": "choiceWithFocus", "data": {"version": 1, "handleName": "Data"}},
              "handle remapping": {"version": 1, "map": [{"key": "Data", "value": "Parameter 0"}]},
              "bounds": {"version": 1, "acceptsHWFocus": "Yes", "showWhenDataModelInvalid": "Hide", "whenVisible": "Always", "boundsType": "Absolute", "bounds": "23 45 278 107"}
            },
            {
              "version": 2,
              "componentData": {"version": 1, "name": "detune", "type": "sliderWithFocus", "data": {"version": 1, "handleName": "Data"}},
              "handle remapping": {"version": 1, "map": [{"key": "Data", "value": "Parameter 1"}]},
              "bounds": {"version": 2, "acceptsHWFocus": "Yes", "showWhenDataModelInvalid": "Hide", "whenVisible": "Always", "boundsType": "Absolute", "bounds": "343 45 278 107", "additionalInvalidatingHandles": ["IndexedEnabling/1/4/Parameter 0"]}
            },
            {
              "version": 2,
              "componentData": {"version": 1, "name": "cutoff", "type": "knobWithFocus", "data": {"version": 1, "handleName": "Data"}},
              "handle remapping": {"version": 1, "map": [{"key": "Data", "value": "Parameter 2"}]},
              "bounds": {"version": 1, "acceptsHWFocus": "Yes", "showWhenDataModelInvalid": "Hide", "whenVisible": "Always", "boundsType": "Absolute", "bounds": "23 196 278 107"}
            },
            {
              "version": 2,
              "componentData": {"version": 1, "name": "resonance", "type": "knobWithFocus", "data": {"version": 1, "handleName": "Data"}},
              "handle remapping": {"version": 1, "map": [{"key": "Data", "value": "Parameter 3"}]},
              "bounds": {"version": 1, "acceptsHWFocus": "Yes", "showWhenDataModelInvalid": "Hide", "whenVisible": "Always", "boundsType": "Absolute", "bounds": "343 196 278 107"}
            },
            {
              "version": 2,
              "componentData": {"version": 1, "name": "env amount", "type": "sliderWithFocus", "data": {"version": 1, "handleName": "Data"}},
              "handle remapping": {"version": 1, "map": [{"key": "Data", "value": "Parameter 4"}]},
              "bounds": {"version": 1, "acceptsHWFocus": "Yes", "showWhenDataModelInvalid": "Hide", "whenVisible": "Always", "boundsType": "Absolute", "bounds": "23 346 278 107"}
            },
            {
              "version": 2,
              "componentData": {"version": 1, "name": "volume", "type": "sliderWithFocus", "data": {"version": 1, "handleName": "Data"}},
              "handle remapping": {"version": 1, "map": [{"key": "Data", "value": "Parameter 9"}]},
              "bounds": {"version": 1, "acceptsHWFocus": "Yes", "showWhenDataModelInvalid": "Hide", "whenVisible": "Always", "boundsType": "Absolute", "bounds": "23 497 278 107"}
            }
          ]
        },
        "qlinkBoundsData": ["38 0 248 624", "358 0 248 624", "678 0 248 624", "998 0 248 624"]
      },
      {
        "version": 1,
        "tabName": "ENV",
        "fnKeyIndex": 1,
        "fnKeySubIndex": 0,
        "componentDefinition": {
          "version": 2,
          "actions": [],
          "backgroundData": {
            "version": 1,
            "focussed": {"version": 1, "colour": "ff1e1d1e", "image": ""},
            "unfocussed": {"version": 1, "colour": "ff1e1d1e", "image": ""}
          },
          "ignoreMousePresses": false,
          "disableCoarseDataWheel": false,
          "componentsData": [
            {
              "version": 2,
              "componentData": {"version": 1, "name": "attack", "type": "sliderWithFocus", "data": {"version": 1, "handleName": "Data"}},
              "handle remapping": {"version": 1, "map": [{"key": "Data", "value": "Parameter 5"}]},
              "bounds": {"version": 1, "acceptsHWFocus": "Yes", "showWhenDataModelInvalid": "Hide", "whenVisible": "Always", "boundsType": "Absolute", "bounds": "23 45 278 107"}
            },
            {
              "version": 2,
              "componentData": {"version": 1, "name": "decay", "type": "sliderWithFocus", "data": {"version": 1, "handleName": "Data"}},
              "handle remapping": {"version": 1, "map": [{"key": "Data", "value": "Parameter 6"}]},
              "bounds": {"version": 1, "acceptsHWFocus": "Yes", "showWhenDataModelInvalid": "Hide", "whenVisible": "Always", "boundsType": "Absolute", "bounds": "343 45 278 107"}
            },
            {
              "version": 2,
              "componentData": {"version": 1, "name": "sustain", "type": "sliderWithFocus", "data": {"version": 1, "handleName": "Data"}},
              "handle remapping": {"version": 1, "map": [{"key": "Data", "value": "Parameter 7"}]},
              "bounds": {"version": 1, "acceptsHWFocus": "Yes", "showWhenDataModelInvalid": "Hide", "whenVisible": "Always", "boundsType": "Absolute", "bounds": "663 45 278 107"}
            },
            {
              "version": 2,
              "componentData": {"version": 1, "name": "release", "type": "sliderWithFocus", "data": {"version": 1, "handleName": "Data"}},
              "handle remapping": {"version": 1, "map": [{"key": "Data", "value": "Parameter 8"}]},
              "bounds": {"version": 1, "acceptsHWFocus": "Yes", "showWhenDataModelInvalid": "Hide", "whenVisible": "Always", "boundsType": "Absolute", "bounds": "983 45 278 107"}
            }
          ]
        },
        "qlinkBoundsData": ["38 0 248 624", "358 0 248 624", "678 0 248 624", "998 0 248 624"]
      }
    ]
  }
}
```

### 9.2 Plugin Skins/Q-Links.json

```json
{
  "version": 4,
  "info": {"version": 1, "type": "CompleteDescription"},
  "Screen Mode Q-Links": {
    "version": 4,
    "map": [
      {
        "Tab": 1,
        "SubTab": 1,
        "Bank Direction": "Column",
        "Q-Links": {
          "Q-Link 1": 9,   "Q-Link 2": -1,  "Q-Link 3": -1,  "Q-Link 4": -1,
          "Q-Link 5": 4,   "Q-Link 6": -1,  "Q-Link 7": -1,  "Q-Link 8": -1,
          "Q-Link 9": 2,   "Q-Link 10": 3,  "Q-Link 11": -1, "Q-Link 12": -1,
          "Q-Link 13": 0,  "Q-Link 14": 1,  "Q-Link 15": -1, "Q-Link 16": -1
        }
      },
      {
        "Tab": 2,
        "SubTab": 1,
        "Bank Direction": "Column",
        "Q-Links": {
          "Q-Link 1": -1,  "Q-Link 2": -1,  "Q-Link 3": -1,  "Q-Link 4": -1,
          "Q-Link 5": -1,  "Q-Link 6": -1,  "Q-Link 7": -1,  "Q-Link 8": -1,
          "Q-Link 9": -1,  "Q-Link 10": -1, "Q-Link 11": -1, "Q-Link 12": -1,
          "Q-Link 13": 5,  "Q-Link 14": 6,  "Q-Link 15": 7,  "Q-Link 16": 8
        }
      }
    ]
  },
  "Program Mode Q-Links": {
    "Q-Link 1": 0,   "Q-Link 2": 1,   "Q-Link 3": 2,   "Q-Link 4": 3,
    "Q-Link 5": 4,   "Q-Link 6": 5,   "Q-Link 7": 6,   "Q-Link 8": 7,
    "Q-Link 9": 8,   "Q-Link 10": 9,  "Q-Link 11": -1, "Q-Link 12": -1,
    "Q-Link 13": -1, "Q-Link 14": -1, "Q-Link 15": -1, "Q-Link 16": -1
  }
}
```

### 9.3 version.xml

```xml
<?xml version="1.0" encoding="utf-8"?>
<plugincontent version="1.0">
    <version>1.0.0.0</version>
    <uiversion>0.1.0.0</uiversion>
</plugincontent>
```

### 9.4 Walking through it

- Tab `OSC` is `fnKeyIndex` 0 and therefore `Tab` 1 in `Q-Links.json`;
  `ENV` is `fnKeyIndex` 1 / `Tab` 2. Each slot's screen position matches its
  knob: the `Wave` slot at row 0, column 0 is next to Q-Link 13, and
  `Q-Link 13` on Tab 1 is parameter 0.
- Touching any slot fires `Mouse Down -> Q-Link`, which banks the knobs to the
  column containing that parameter (relevant on hardware with fewer knobs).
- Double-tapping `Wave` opens `menu overlay`, which lists `Sine`, `Saw`,
  `Square`, `Noise` from the `replacementStrings` entry; the same entry makes
  the knob and the data wheel step through those four values.
- `Detune` uses a version-2 `bounds` with
  `IndexedEnabling/1/4/Parameter 0`, so it is only visible while `Wave` is
  `Saw` (choice index 1 of 4).
- Every slot has `showWhenDataModelInvalid: "Hide"`; if the plugin were
  loaded with fewer parameters, the missing slots would disappear instead of
  showing junk.
- `Program Mode Q-Links` gives the knobs the ten parameters in index order
  when the user banks the hardware to the program instead of the screen.

---

## 10. Checklist and common mistakes

Run `tools/validate.py` on the output folder; it catches most of these. Then
check on hardware.

**Versions**

- `pageData.version` is `1`; `componentDefinitions.version` is `2`;
  `Q-Links.json` top-level and `Screen Mode Q-Links` versions are `4`;
  `info.version` is `1`. A wrong version anywhere fails the **whole** file.
- The two overlay widgets keep their duplicate `version` keys
  (`2, 1` for `KnobOverlay`, `1, 1` for `MenuOverlay`). Do not let a JSON
  formatter collapse them.
- `<uiversion>` in `version.xml` is `0.1.0.0`. Writing `1` makes the MPC
  refuse to load the plugin.

**Indices**

- `Parameter N` is **0-based**. `Tab`, `SubTab` and `Q-Link n` in
  `Q-Links.json` are **1-based**. `PageIndex T, Q` is 1-based.
- `IndexedEnabling/<i>/<n>/...`: `i` is 0-based, `n` is the total step count.
- `buttonId` in a radio group is 0-based; a lone toggle uses `buttonId: 1`.

**Matching**

- The folder is named exactly `<Manufacturer> - VST - <Name>` using the same
  strings the plugin reports as vendor and effect name (`manufacturer` and
  `name` in `plugin.json`). A mismatch means the default skin is used.

**Layout**

- At most 16 controls per page on 4 x 4 hardware, placed so each sits by its
  knob (`Q-Link 13 + c - 4r`). The `Q-Links.json` entry for that tab must
  agree with the on-screen positions.
- Keep important controls away from the right edge (`x >= ~1025`) where the
  Q-Link sidebar can appear, or supply `qlinkBoundsData`.
- Tab coordinates run `0 .. 1280` by `0 .. 628`; overlays `0 .. 1280` by
  `0 .. 800`. There is no automatic layout.
- At most 32 tabs and 8 sub-tabs per function key.

**Assets and text**

- Only `Titillium Web`, `Roboto` and `Nexa`; a wrong font name silently
  becomes Titillium Web.
- Image paths are relative to the JSON file that defines the component
  (`TUI.json` for inline definitions, the imported file for imported
  definitions). A missing image draws nothing with no error.
- Film strips are vertical. `numFrames` must divide the image height.
- Colours are `AARRGGBB`: `ffd31145` not `#d31145`, and the alpha comes
  first. `"0"` is transparent.

**JSON shape**

- Maps are arrays of `{"key", "value"}` objects: `localComponentDefinitions`,
  `replacementStrings`, `handle remapping.map`.
- Every documented non-optional key must be present, in the documented
  order, even when its value is empty (`"actions": []`, `"importFiles": []`,
  `"map": []`, `"additionalData": ""`).
- Enum strings are exact and case-sensitive; a typo falls back silently to
  the first enum value (`"vertical"` becomes `"Horizontal"` for
  `dragOrientation`, for example).
- `Separator.separatorType` is an integer, not a string.

**Behaviour**

- A broken `TUI.json` gives the plugin **no tabs at all**; the default skin is
  not used once a file is found. If the plugin loads but the Program Edit
  screen is empty, the file failed to parse.
- A broken or missing `Q-Links.json` gives a sequential knob mapping that
  does not match a column-major layout.
- `replacementStrings` keys must name the parameter index the control
  actually resolves to (after any `PartialDescription` offset).
- `ComboBox` shows an empty list for VST parameters; use `replacementStrings`
  plus `MenuOverlay` instead.
- `Meter`, `Plotter` and `HaloDisplay` can only be driven from a parameter the
  plugin updates itself.
