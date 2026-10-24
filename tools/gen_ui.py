#!/usr/bin/env python3
"""gen_ui.py - generate MPC "Plugin Skins" (TUI.json + Q-Links.json) for a plugin.

Usage:
    gen_ui.py --params build/params.json [--ui ui.json] --out "<output folder>/Plugin Skins"
              [--assets-dir skins-assets]

Inputs
    params.json   dumped by host_sim from the native build (parameter names, kinds, choices).
    ui.json       OPTIONAL layout spec. Without it, pages of 16 parameters are generated in
                  index order, named "1", "2", ...

Output
    TUI.json      the scripted UI: one tab per page, a 4 x 4 grid of controls per tab.
    Q-Links.json  maps the 16 hardware Q-Link knobs of every tab to parameter indices.

The JSON written here mirrors the shapes the MPC firmware expects, including the DUPLICATE
"version" keys inside the KnobOverlay / MenuOverlay widget data. Python's json module cannot
write duplicate keys, so this file carries its own tiny ordered-pairs JSON writer.

Standard library only; Python 3.9+.
"""

import argparse
import difflib
import json
import os
import sys

# --------------------------------------------------------------------------------------------
# Layout constants (1280 x 628 tab area, 4 x 4 grid, reading order)
# --------------------------------------------------------------------------------------------
SLOT_X = [23, 343, 663, 983]
SLOT_Y = [45, 196, 346, 497]
SLOT_W, SLOT_H = 278, 107
QLINK_BOUNDS = ["38 0 248 624", "358 0 248 624", "678 0 248 624", "998 0 248 624"]
MAX_CONTROLS_PER_PAGE = 16
MAX_PAGES = 32
FONT_NAME = "Titillium Web"

DEFAULT_STYLE = {
    "background": "ff1e1d1e",
    "accent": "ffd31145",
    "text": "ffffffff",
    "muted": "ff6a6a6c",
}

WIDGETS = ("slider", "knob", "empty", "text")
OVERLAYS = ("knob", "menu")
CONTROL_KEYS = ("param", "widget", "visibleWhen", "text", "overlay")
PAGE_KEYS = ("name", "controls")
UI_KEYS = ("pages", "style", "knobImage", "knobFrames")


def slot_qlink(row, col):
    """Hardware Q-Link number (1..16) for grid slot (row, col), both 0-based."""
    return 13 + col - 4 * row


# --------------------------------------------------------------------------------------------
# Ordered JSON with duplicate keys
# --------------------------------------------------------------------------------------------
class Obj(list):
    """A JSON object as an ordered list of (key, value) pairs. Keys may repeat."""


def obj(*pairs):
    return Obj(pairs)


def dump_json(value, indent=0):
    """Pretty-print like json.dumps(indent=4) but accepting Obj (ordered, duplicate keys)."""
    pad = " " * indent
    inner_pad = " " * (indent + 4)
    if isinstance(value, Obj):
        if not value:
            return "{}"
        parts = [inner_pad + json.dumps(str(k)) + ": " + dump_json(v, indent + 4) for k, v in value]
        return "{\n" + ",\n".join(parts) + "\n" + pad + "}"
    if isinstance(value, dict):
        return dump_json(Obj(value.items()), indent)
    if isinstance(value, (list, tuple)):
        if not value:
            return "[]"
        parts = [inner_pad + dump_json(v, indent + 4) for v in value]
        return "[\n" + ",\n".join(parts) + "\n" + pad + "]"
    if isinstance(value, bool):
        return "true" if value else "false"
    if value is None:
        return "null"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        return repr(value)
    return json.dumps(value, ensure_ascii=False)


# --------------------------------------------------------------------------------------------
# Errors
# --------------------------------------------------------------------------------------------
class UiError(Exception):
    """A problem in ui.json / params.json, with a message aimed at whoever edits ui.json."""


def fail(where, msg, *hints):
    text = "ERROR in %s: %s" % (where, msg) if where else "ERROR: " + msg
    for h in hints:
        if h:
            text += "\n  " + h
    raise UiError(text)


# --------------------------------------------------------------------------------------------
# params.json
# --------------------------------------------------------------------------------------------
class Param:
    def __init__(self, index, name, kind, choices):
        self.index = index
        self.name = name
        self.kind = kind
        self.choices = choices  # list of labels for choice/bool kinds, else None

    @property
    def handle(self):
        return "Parameter %d" % self.index

    @property
    def is_discrete(self):
        return self.kind in ("choice", "bool")


def load_params(path):
    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
    except OSError as e:
        fail(None, "cannot read params.json: %s" % e,
             "params.json is produced by the build (host_sim <native lib> dump). Run tools/build.py first.")
    except ValueError as e:
        fail(path, "not valid JSON (%s)" % e)
    if not isinstance(data, dict) or not isinstance(data.get("params"), list):
        fail(path, 'expected an object with a "params" array (as written by host_sim dump).')
    params = []
    for i, p in enumerate(data["params"]):
        if not isinstance(p, dict) or "name" not in p:
            fail(path, "params[%d] is not an object with a \"name\"" % i)
        index = p.get("index", i)
        if not isinstance(index, int):
            fail(path, "params[%d].index must be an integer" % i)
        kind = p.get("kind") or "float"
        choices = p.get("choices")
        if kind == "bool":
            if not (isinstance(choices, list) and len(choices) == 2):
                choices = ["Off", "On"]
        elif kind == "choice":
            if not (isinstance(choices, list) and len(choices) >= 1):
                fail(path, 'params[%d] ("%s") has kind "choice" but no "choices" list' % (i, p["name"]))
        else:
            choices = None
        params.append(Param(index, str(p["name"]), kind, [str(c) for c in choices] if choices else None))
    if not params:
        fail(path, "the plugin reports no parameters; there is nothing to lay out.",
             "Add parameters with addParam() in the plugin source, or use --no-ui in build.py.")
    return params


class ParamTable:
    def __init__(self, params):
        self.params = params
        self.by_name = {}
        self.by_lower = {}
        for p in params:
            self.by_name.setdefault(p.name, p)
            self.by_lower.setdefault(p.name.lower(), p)

    def lookup(self, name, where):
        if not isinstance(name, str):
            fail(where, '"param" must be a parameter name (a string), got %s' % json.dumps(name))
        p = self.by_name.get(name)
        if p is None:
            p = self.by_lower.get(name.lower())
        if p is None:
            names = [q.name for q in self.params]
            close = difflib.get_close_matches(name, names, n=3, cutoff=0.5)
            fail(where, 'unknown parameter "%s".' % name,
                 "Available parameter names (from params.json): " + ", ".join('"%s"' % n for n in names),
                 ("Did you mean " + " or ".join('"%s"' % c for c in close) + "?") if close else "")
        return p


# --------------------------------------------------------------------------------------------
# ui.json
# --------------------------------------------------------------------------------------------
class Control:
    def __init__(self, widget, param=None, text=None, visible=None, overlay="knob"):
        self.widget = widget      # slider | knob | empty | text
        self.param = param        # Param or None
        self.text = text          # for widget "text"
        self.visible = visible    # None or (Param, choiceIndex)
        self.overlay = overlay    # knob | menu


class Page:
    def __init__(self, name, controls):
        self.name = name
        self.controls = controls  # list of Control, at most 16, in reading order


def check_keys(where, d, allowed):
    for k in d:
        if k not in allowed:
            close = difflib.get_close_matches(k, allowed, n=1)
            fail(where, 'unknown key "%s".' % k,
                 "Allowed keys: " + ", ".join('"%s"' % a for a in allowed),
                 ('Did you mean "%s"?' % close[0]) if close else "")


def parse_visible_when(spec, table, where):
    if not isinstance(spec, dict):
        fail(where, '"visibleWhen" must be an object like {"param": "Mode", "choice": "Dual"} or {"param": "Mode", "index": 1}')
    check_keys(where, spec, ("param", "choice", "index"))
    if "param" not in spec:
        fail(where, '"visibleWhen" needs a "param" (the name of the choice/bool parameter it depends on)')
    p = table.lookup(spec["param"], where)
    if not p.is_discrete:
        fail(where, '"visibleWhen" parameter "%s" is kind "%s"; it must be a choice or bool parameter.' % (p.name, p.kind),
             "Only discrete parameters can drive IndexedEnabling. Choice/bool parameters: "
             + (", ".join('"%s"' % q.name for q in table.params if q.is_discrete) or "(none)"))
    if ("choice" in spec) == ("index" in spec):
        fail(where, '"visibleWhen" needs exactly one of "choice" (a label) or "index" (0-based number)')
    if "index" in spec:
        idx = spec["index"]
        if not isinstance(idx, int) or isinstance(idx, bool) or not (0 <= idx < len(p.choices)):
            fail(where, '"visibleWhen".index must be an integer in 0..%d for "%s" (choices: %s)'
                 % (len(p.choices) - 1, p.name, ", ".join('"%s"' % c for c in p.choices)))
        return (p, idx)
    label = spec["choice"]
    if not isinstance(label, str):
        fail(where, '"visibleWhen".choice must be a string label')
    if label in p.choices:
        return (p, p.choices.index(label))
    lowered = [c.lower() for c in p.choices]
    if label.lower() in lowered:
        return (p, lowered.index(label.lower()))
    fail(where, 'parameter "%s" has no choice "%s".' % (p.name, label),
         "Its choices are: " + ", ".join('"%s"' % c for c in p.choices))


def parse_control(spec, table, where):
    if isinstance(spec, str):
        return Control("slider", param=table.lookup(spec, where))
    if not isinstance(spec, dict):
        fail(where, "each control must be a parameter name (string) or an object, got %s" % json.dumps(spec))
    check_keys(where, spec, CONTROL_KEYS)
    widget = spec.get("widget", "slider")
    if widget not in WIDGETS:
        fail(where, '"widget" must be one of %s, got %s' % (", ".join('"%s"' % w for w in WIDGETS), json.dumps(widget)))
    overlay = spec.get("overlay", "knob")
    if overlay not in OVERLAYS:
        fail(where, '"overlay" must be "knob" or "menu", got %s' % json.dumps(overlay))
    visible = parse_visible_when(spec["visibleWhen"], table, where) if "visibleWhen" in spec else None
    if widget == "empty":
        return Control("empty", visible=visible)
    if widget == "text":
        text = spec.get("text")
        if not isinstance(text, str):
            fail(where, '"widget": "text" needs a "text" string (the heading to show in this slot)')
        return Control("text", text=text, visible=visible)
    if "param" not in spec:
        fail(where, 'control needs a "param" (parameter name); use "widget": "empty" for a blank slot')
    return Control(widget, param=table.lookup(spec["param"], where), visible=visible, overlay=overlay)


def load_ui(path, table):
    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
    except OSError as e:
        fail(None, "cannot read ui.json: %s" % e)
    except ValueError as e:
        fail(path, "not valid JSON (%s)" % e, "Check for trailing commas or comments; ui.json must be strict JSON.")
    if not isinstance(data, dict):
        fail(path, 'top level must be an object like {"pages": [...]}')
    check_keys(path, data, UI_KEYS)
    pages_spec = data.get("pages")
    if not isinstance(pages_spec, list) or not pages_spec:
        fail(path, '"pages" must be a non-empty array of pages ({"name": "...", "controls": [...]})')
    if len(pages_spec) > MAX_PAGES:
        fail(path, "%d pages given; the MPC supports at most %d tabs." % (len(pages_spec), MAX_PAGES))

    style = dict(DEFAULT_STYLE)
    if "style" in data:
        if not isinstance(data["style"], dict):
            fail(path + ' "style"', "must be an object of ARGB hex colours")
        check_keys(path + ' "style"', data["style"], tuple(DEFAULT_STYLE))
        for k, v in data["style"].items():
            if not (isinstance(v, str) and len(v) == 8 and all(ch in "0123456789abcdefABCDEF" for ch in v)):
                fail(path + ' "style"', '"%s" must be an 8-digit ARGB hex string like "ff1e1d1e", got %s' % (k, json.dumps(v)))
            style[k] = v.lower()

    knob_image = data.get("knobImage")
    if knob_image is not None and (not isinstance(knob_image, str) or not knob_image.strip()):
        fail(path, '"knobImage" must be a file name (a PNG film strip in the project\'s assets/ folder, which build.py copies into Plugin Skins/)')
    frames = data.get("knobFrames", 127)
    if not isinstance(frames, int) or isinstance(frames, bool) or frames < 2:
        fail(path, '"knobFrames" must be an integer >= 2 (number of frames in the film strip), got %s' % json.dumps(frames))

    pages = []
    for pi, pspec in enumerate(pages_spec):
        where = "%s pages[%d]" % (path, pi)
        if not isinstance(pspec, dict):
            fail(where, 'each page must be an object like {"name": "OSC", "controls": [...]}')
        check_keys(where, pspec, PAGE_KEYS)
        name = pspec.get("name", str(pi + 1))
        if not isinstance(name, str) or not name:
            fail(where, '"name" must be a non-empty string (the tab label)')
        if len(name) > 8:
            print('gen_ui: WARNING: %s name "%s" is longer than 8 characters and may be truncated on the tab.' % (where, name))
        controls_spec = pspec.get("controls", [])
        if not isinstance(controls_spec, list):
            fail(where, '"controls" must be an array')
        if len(controls_spec) > MAX_CONTROLS_PER_PAGE:
            fail(where, "has %d controls but a page is a 4 x 4 grid with at most %d slots."
                 % (len(controls_spec), MAX_CONTROLS_PER_PAGE),
                 "Split the controls over more pages (add another entry to \"pages\").")
        controls = [parse_control(c, table, "%s.controls[%d]" % (where, ci)) for ci, c in enumerate(controls_spec)]
        pages.append(Page(name, controls))
    return pages, style, knob_image, frames


def auto_pages(params):
    pages = []
    for start in range(0, len(params), MAX_CONTROLS_PER_PAGE):
        chunk = params[start:start + MAX_CONTROLS_PER_PAGE]
        pages.append(Page(str(len(pages) + 1), [Control("slider", param=p) for p in chunk]))
    if len(pages) > MAX_PAGES:
        fail(None, "the plugin has %d parameters, which needs %d pages; the MPC supports at most %d tabs."
             % (len(params), len(pages), MAX_PAGES), "Write a ui.json that lays out only the parameters you need.")
    return pages


# --------------------------------------------------------------------------------------------
# TUI.json building blocks (shapes mirror the firmware's generic skins)
# --------------------------------------------------------------------------------------------
def action(on_action, handler, handle_name, additional=""):
    return obj(("version", 1), ("onAction", on_action), ("handler", handler),
               ("handleName", handle_name), ("additionalData", additional))


def background_data(colour):
    def side():
        return obj(("version", 1), ("colour", colour), ("image", ""))
    return obj(("version", 1), ("focussed", side()), ("unfocussed", side()))


def component_definition(actions, children, bg_colour, ignore_mouse=False):
    return obj(("version", 2), ("actions", actions), ("backgroundData", background_data(bg_colour)),
               ("ignoreMousePresses", ignore_mouse), ("disableCoarseDataWheel", False),
               ("componentsData", children))


def entry(name, type_name, data, bounds, accepts_focus, show_invalid, when_visible, remap=(), invalidating=None):
    bounds_pairs = [("version", 2 if invalidating else 1), ("acceptsHWFocus", accepts_focus),
                    ("showWhenDataModelInvalid", show_invalid), ("whenVisible", when_visible),
                    ("boundsType", "Absolute"), ("bounds", bounds)]
    if invalidating:
        bounds_pairs.append(("additionalInvalidatingHandles", list(invalidating)))
    return obj(("version", 2),
               ("componentData", obj(("version", 1), ("name", name), ("type", type_name), ("data", data))),
               ("handle remapping", obj(("version", 1), ("map", [obj(("key", k), ("value", v)) for k, v in remap]))),
               ("bounds", Obj(bounds_pairs)))


def slider_data():
    return obj(("version", 1), ("handleName", "Data"))


def knob_generic_data():
    return obj(("version", 5), ("knobType", "GenericLegacy"), ("polarity", "UniPolar"),
               ("dragOrientation", "Vertical"), ("handleName", "Data"))


def knob_filmstrip_data(image, frames):
    return obj(("version", 2), ("knobType", "FilmStrip"), ("filmStrip", image), ("numFrames", frames),
               ("invert", False), ("handleName", "Data"))


def label_data(label_type, colour, height=30.0, justification="left", text=None):
    pairs = [("version", 1),
             ("textStyle", obj(("version", 1),
                               ("font", obj(("version", 1), ("name", FONT_NAME), ("style", "Regular"), ("height", float(height)))),
                               ("colour", colour), ("justification", justification), ("case", "Original"))),
             ("type", label_type), ("handleName", "Data")]
    if text is not None:
        pairs.append(("text", text))
    return Obj(pairs)


def focus_data(accent):
    return obj(("version", 1), ("backgroundColour", "19ffffff"), ("outlineColour", accent),
               ("backgroundInset", 4.0), ("outlineThickness", 1.0))


def focus_lost_action():
    return action("Focus Lost", "Focus Lost", "Focus Lost", "")


def knob_overlay_data():
    return obj(("version", 2), ("version", 1), ("actions", [focus_lost_action()]),
               ("overlayType", "Knob With Header"), ("scaleType", "Linear"), ("polarityType", "Unipolar"),
               ("showMinusSign", True), ("handleName", "Data"))


def menu_overlay_data():
    return obj(("version", 1), ("version", 1), ("actions", [focus_lost_action()]),
               ("withNumbers", False), ("handleName", "Data"))


def control_actions(overlay_name):
    return [action("Mouse Down", "Q-Link", "Data", ""),
            action("Double Click", "Show Overlay", "Data", overlay_name),
            action("Enter Pressed", "Show Overlay", "Data", overlay_name)]


def slot_bounds_str():
    return "0 0 %d %d" % (SLOT_W, SLOT_H)


def focus_child(style):
    return entry("focus", "Focus", focus_data(style["accent"]), slot_bounds_str(), "No", "Show", "WhenFocussed")


def slider_component(style, overlay_name):
    children = [focus_child(style),
                entry("slider", "Slider", slider_data(), "15 15 248 77", "No", "Show", "Always")]
    return component_definition(control_actions(overlay_name), children, "00000000")


def knob_component(style, overlay_name, knob_image, frames):
    knob = knob_filmstrip_data(knob_image, frames) if knob_image else knob_generic_data()
    children = [focus_child(style),
                entry("knob", "Knob", knob, "15 15 77 77", "No", "Show", "Always"),
                entry("name", "Label", label_data("Name", style["muted"], 26.0), "104 14 160 38", "No", "Show", "Always"),
                entry("value", "Label", label_data("Value", style["text"], 30.0), "104 54 160 40", "No", "Show", "Always")]
    return component_definition(control_actions(overlay_name), children, "00000000")


def overlay_component(type_name, data, bg_colour):
    child = entry("overlay", type_name, data, "0 0 1280 800", "No", "Show", "Always")
    return component_definition([], [child], bg_colour, ignore_mouse=True)


# --------------------------------------------------------------------------------------------
# Assembling the two files
# --------------------------------------------------------------------------------------------
def tab_component(page, style):
    children = []
    for i, ctl in enumerate(page.controls):
        row, col = divmod(i, 4)
        bounds = "%d %d %d %d" % (SLOT_X[col], SLOT_Y[row], SLOT_W, SLOT_H)
        invalidating = None
        if ctl.visible:
            vp, idx = ctl.visible
            invalidating = ["IndexedEnabling/%d/%d/%s" % (idx, len(vp.choices), vp.handle)]
        if ctl.widget == "empty":
            continue
        if ctl.widget == "text":
            children.append(entry("text %d" % i, "Label", label_data("Text", style["text"], 30.0, "left", ctl.text),
                                  bounds, "No", "Show", "Always", invalidating=invalidating))
            continue
        base = "sliderWithFocus" if ctl.widget == "slider" else "knobWithFocus"
        type_name = base + ("Menu" if ctl.overlay == "menu" else "")
        children.append(entry(ctl.param.name, type_name, obj(("version", 1), ("handleName", "Data")), bounds,
                              "Yes", "Hide", "Always", remap=[("Data", ctl.param.handle)], invalidating=invalidating))
    return component_definition([], children, style["background"])


def build_tui(pages, params, style, knob_image, frames):
    used_types = set()
    for page in pages:
        for ctl in page.controls:
            if ctl.widget in ("slider", "knob"):
                used_types.add(("sliderWithFocus" if ctl.widget == "slider" else "knobWithFocus")
                               + ("Menu" if ctl.overlay == "menu" else ""))
    defs = [("sliderWithFocus", slider_component(style, "knob overlay")),
            ("knobWithFocus", knob_component(style, "knob overlay", knob_image, frames))]
    if "sliderWithFocusMenu" in used_types:
        defs.append(("sliderWithFocusMenu", slider_component(style, "menu overlay")))
    if "knobWithFocusMenu" in used_types:
        defs.append(("knobWithFocusMenu", knob_component(style, "menu overlay", knob_image, frames)))
    defs.append(("knob overlay", overlay_component("KnobOverlay", knob_overlay_data(), style["background"])))
    defs.append(("menu overlay", overlay_component("MenuOverlay", menu_overlay_data(), style["background"])))

    # replacementStrings: every discrete parameter referenced by a control or a visibleWhen.
    referenced = {}
    for page in pages:
        for ctl in page.controls:
            if ctl.param is not None:
                referenced[ctl.param.index] = ctl.param
            if ctl.visible:
                referenced[ctl.visible[0].index] = ctl.visible[0]
    replacements = [obj(("key", p.handle), ("value", list(p.choices)))
                    for _, p in sorted(referenced.items()) if p.is_discrete]

    tabs = []
    for ti, page in enumerate(pages):
        tabs.append(obj(("version", 1), ("tabName", page.name), ("fnKeyIndex", ti), ("fnKeySubIndex", 0),
                        ("componentDefinition", tab_component(page, style)),
                        ("qlinkBoundsData", list(QLINK_BOUNDS))))

    page_data = obj(
        ("version", 1),
        ("componentDefinitions", obj(("version", 2), ("importFiles", []),
                                     ("localComponentDefinitions", [obj(("key", k), ("value", v)) for k, v in defs]))),
        ("replacementStrings", replacements),
        ("info", obj(("version", 1), ("type", "CompleteDescription"))),
        ("tabs", tabs))
    return obj(("pageData", page_data))


def build_qlinks(pages, params):
    entries = []
    for ti, page in enumerate(pages):
        assigned = {}
        for i, ctl in enumerate(page.controls):
            if ctl.param is not None:
                row, col = divmod(i, 4)
                assigned[slot_qlink(row, col)] = ctl.param.index
        qmap = Obj(("Q-Link %d" % q, assigned.get(q, -1)) for q in range(1, 17))
        entries.append(obj(("Tab", ti + 1), ("SubTab", 1), ("Bank Direction", "Column"), ("Q-Links", qmap)))
    by_index = {p.index: p for p in params}
    first16 = sorted(by_index)[:16]
    program = Obj(("Q-Link %d" % (q + 1), first16[q] if q < len(first16) else -1) for q in range(16))
    return obj(("version", 4),
               ("info", obj(("version", 1), ("type", "CompleteDescription"))),
               ("Screen Mode Q-Links", obj(("version", 4), ("map", entries))),
               ("Program Mode Q-Links", program))


def generate(params_path, ui_path, out_dir, assets_dir=None, quiet=False):
    params = load_params(params_path)
    table = ParamTable(params)
    if ui_path:
        pages, style, knob_image, frames = load_ui(ui_path, table)
    else:
        pages, style, knob_image, frames = auto_pages(params), dict(DEFAULT_STYLE), None, 127
    if knob_image:
        knob_image = os.path.basename(knob_image)
        if assets_dir and not os.path.isfile(os.path.join(assets_dir, knob_image)):
            print('gen_ui: WARNING: knobImage "%s" not found in %s; the knob will render blank on the MPC.'
                  ' (tools/make_knob.py can generate one.)' % (knob_image, assets_dir))
    if len(pages) > 6 and not quiet:
        print("gen_ui: NOTE: %d pages; the MPC screen has six function keys, so tabs beyond the sixth are untested." % len(pages))

    tui = build_tui(pages, params, style, knob_image, frames)
    qlinks = build_qlinks(pages, params)
    os.makedirs(out_dir, exist_ok=True)
    tui_path = os.path.join(out_dir, "TUI.json")
    q_path = os.path.join(out_dir, "Q-Links.json")
    with open(tui_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(dump_json(tui) + "\n")
    with open(q_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(dump_json(qlinks) + "\n")
    if not quiet:
        n_controls = sum(1 for pg in pages for c in pg.controls if c.param is not None)
        print("gen_ui: wrote %s and %s (%d page%s, %d bound control%s, %s)"
              % (tui_path, q_path, len(pages), "" if len(pages) == 1 else "s",
                 n_controls, "" if n_controls == 1 else "s",
                 "layout from " + ui_path if ui_path else "auto layout"))
    return tui_path, q_path


def main(argv=None):
    ap = argparse.ArgumentParser(description="Generate MPC Plugin Skins (TUI.json + Q-Links.json) from params.json and an optional ui.json.")
    ap.add_argument("--params", required=True, help="build/params.json written by host_sim dump")
    ap.add_argument("--ui", help="ui.json layout spec (omit for an automatic layout of all parameters)")
    ap.add_argument("--out", required=True, help='output directory, normally "<plugin folder>/Plugin Skins"')
    ap.add_argument("--assets-dir", help="folder holding knobImage (only used to warn when the file is missing)")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args(argv)
    try:
        generate(args.params, args.ui, args.out, args.assets_dir, args.quiet)
    except UiError as e:
        print("gen_ui: " + str(e), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
