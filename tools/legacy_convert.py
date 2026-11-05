#!/usr/bin/env python3
"""legacy_convert.py - convert a community "mpc-vst-plugins" package to the SDK's plugin folder format.

Usage:
    legacy_convert.py SOURCE [SOURCE ...] [--out DIR] [--zip] [--force]

SOURCE is a release zip (for example Acid-2.1.0-mpc-armv7.zip), an unzipped release folder, or a
`portable/<Vendor> - VST - <Name>` folder. Give the 32-bit (armv7) and 64-bit (aarch64) zips of the same plugin
together and they are merged into one folder that works on both kinds of MPC.

The community packages (https://sd88me.github.io/mpc-vst-plugins/) were installed by a script that stops the
MPC, copies the plugin into /sdcard/Synths and edits MPC.settings. The plugin itself is an ordinary VST2 binary
with a normal "Plugin Skins" folder, so only the packaging differs. This tool turns each one into

    <Manufacturer> - VST - <Name>/
        plugin.json  version.xml  <the plugin's .so files, left where they were>  Plugin Skins/  (data, presets)

which you copy into a "VST2 Plugins" folder (or install with  mpc_remote.py ADDRESS import ...). The converted
folder is written to --out (default ./converted) and, with --zip, also zipped with an INSTALL.txt for sharing.

The plugin's own binaries are not changed. Check each plugin's licence before redistributing it: the licence is
recorded in plugin.json when the package names one.

Standard library only. Python 3.9+.
"""

import argparse
import json
import os
import re
import shutil
import struct
import sys
import tempfile
import xml.etree.ElementTree as ET
import zipfile

ILLEGAL_FILENAME_CHARS = '"#@,;:<>*^|?\\/'  # the set the MPC strips when it builds the folder name
JUNK_NAMES = {".DS_Store", "Thumbs.db", "desktop.ini"}
LEGACY_ONLY = {"plugin-meta.xml", "mpc-plugin.json"}  # packaging files of the old installer, not part of the plugin
ARCH_BY_MACHINE = {0x28: "arm32", 0xB7: "arm64"}
ARCH_KEY = {"arm32": "file32", "arm64": "file64"}


class ConvertError(Exception):
    pass


def legal_filename(s):
    return "".join(c for c in s if c not in ILLEGAL_FILENAME_CHARS)


def make_slug(name):
    return re.sub(r"[^a-z0-9]", "-", name.lower())


def safe_extract(zpath, dest):
    """Unpack a zip, refusing anything that would land outside dest."""
    try:
        z = zipfile.ZipFile(zpath)
    except zipfile.BadZipFile:
        raise ConvertError("%s is not a valid zip file" % zpath)
    with z:
        base = os.path.realpath(dest)
        for info in z.infolist():
            target = os.path.realpath(os.path.join(dest, info.filename))
            if target != base and not target.startswith(base + os.sep):
                raise ConvertError("%s contains an unsafe path (%s); refusing to unpack it" % (zpath, info.filename))
        z.extractall(dest)


def find_legacy_folders(root):
    """Folders holding a plugin-meta.xml, at most a few levels below root."""
    found = []
    root = os.path.abspath(root)
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = sorted(d for d in dirnames if d != "__MACOSX" and not d.startswith("."))
        if "plugin-meta.xml" in filenames:
            found.append(dirpath)
            dirnames[:] = []
        elif dirpath[len(root):].count(os.sep) >= 4:
            dirnames[:] = []
    return found


def elf_arch(path):
    with open(path, "rb") as f:
        head = f.read(20)
    if head[:4] != b"\x7fELF":
        raise ConvertError("%s is not an ELF binary" % os.path.basename(path))
    machine = struct.unpack_from("<H", head, 18)[0]
    arch = ARCH_BY_MACHINE.get(machine)
    if not arch:
        raise ConvertError("%s is not an ARM binary (machine 0x%x)" % (os.path.basename(path), machine))
    return arch


def convert_uid(text):
    """The old files give the VST unique id as 8 hex digits ('41634964' is the code 'AcId')."""
    t = (text or "").strip()
    if re.fullmatch(r"[0-9A-Fa-f]{8}", t):
        value = int(t, 16)
    elif t.isdigit():
        value = int(t)
    else:
        raise ConvertError("cannot read the plugin uid %r" % text)
    raw = value.to_bytes(4, "big")
    if all(0x20 < b < 0x7f for b in raw):
        return raw.decode("ascii")  # a four-character code, the SDK's preferred spelling
    return "0x%08X" % value


def read_legacy(folder):
    """Parse plugin-meta.xml (and mpc-plugin.json when there is one). Returns a dict."""
    try:
        root = ET.parse(os.path.join(folder, "plugin-meta.xml")).getroot()
    except (ET.ParseError, OSError) as e:
        raise ConvertError("%s: cannot read plugin-meta.xml (%s)" % (folder, e))
    plugins = [root] if root.tag == "PLUGIN" else list(root.iter("PLUGIN"))
    if len(plugins) != 1:
        raise ConvertError("%s: expected one plugin in plugin-meta.xml, found %d" % (folder, len(plugins)))
    p = plugins[0]
    if p.get("isShell") == "1":
        raise ConvertError("%s is a shell plugin (several plugins in one binary), which is not supported" % folder)
    extra = {}
    try:
        with open(os.path.join(folder, "mpc-plugin.json"), encoding="utf-8") as f:
            extra = json.load(f)
        if not isinstance(extra, dict):
            extra = {}
    except (OSError, ValueError):
        pass
    name, vendor = p.get("name"), p.get("manufacturer")
    if not name or not vendor:
        raise ConvertError("%s: plugin-meta.xml has no name or manufacturer" % folder)
    # file="%payload-path%/<folder name>/<binary path>": the binary's path inside the plugin folder is what follows
    file_attr = (p.get("file") or "").replace("\\", "/")
    rel = file_attr.split("/" + os.path.basename(folder) + "/", 1)[1] if ("/" + os.path.basename(folder) + "/") in file_attr \
        else os.path.basename(file_attr)
    if not rel or not os.path.isfile(os.path.join(folder, rel)):
        rel = extra.get("so") or rel
    full = os.path.normpath(os.path.join(folder, rel))
    if not rel or not full.startswith(os.path.normpath(folder) + os.sep) or not os.path.isfile(full):
        raise ConvertError("%s: the plugin binary %r named in plugin-meta.xml is missing" % (folder, rel))
    return {
        "name": name, "manufacturer": vendor, "uid": convert_uid(p.get("uid")),
        "kind": "instrument" if p.get("isInstrument") == "1" else "effect",
        "category": p.get("category") or "", "numInputs": int(p.get("numInputs") or 0),
        "numOutputs": int(p.get("numOutputs") or 2) or 2,
        "version": str(extra.get("version") or p.get("version") or "1.0.0"),
        "description": extra.get("about") or "", "license": extra.get("license") or "",
        "repo": extra.get("source_repo") or "", "rel": rel.replace(os.sep, "/"), "arch": elf_arch(full),
    }


def copy_missing(src, dst):
    """Copy a legacy plugin folder into dst, leaving files that are already there and the installer's own files."""
    for dirpath, dirnames, filenames in os.walk(src):
        dirnames[:] = sorted(d for d in dirnames if d != "__MACOSX" and not d.startswith("._"))
        out = os.path.join(dst, os.path.relpath(dirpath, src))
        os.makedirs(out, exist_ok=True)
        for fn in sorted(filenames):
            if fn in JUNK_NAMES or fn.startswith("._"):
                continue
            if dirpath == src and fn in LEGACY_ONLY:
                continue
            target = os.path.join(out, fn)
            if not os.path.exists(target):
                shutil.copy2(os.path.join(dirpath, fn), target)


def pad_version(v):
    nums = re.findall(r"\d+", v)[:4]
    return ".".join(nums + ["0"] * (4 - len(nums))) if nums else "1.0.0.0"


def convert_folder(legacy, out_dir, created):
    """Convert one legacy folder into out_dir. `created` maps output folder -> plugin.json for this run, so the
    32-bit and 64-bit packages of one plugin merge. Returns (output folder, warnings)."""
    meta = read_legacy(legacy)
    folder = legal_filename("%s - VST - %s" % (meta["manufacturer"], meta["name"]))
    dest = os.path.join(out_dir, folder)
    warnings = []
    if os.path.basename(legacy) != folder:
        warnings.append('the package folder was named "%s"; the plugin reports "%s"' % (os.path.basename(legacy), folder))
    key = ARCH_KEY[meta["arch"]]

    manifest = created.get(dest)
    first = manifest is None
    if first:
        os.makedirs(dest)
        manifest = {
            "name": meta["name"], "manufacturer": meta["manufacturer"], "version": meta["version"], "kind": meta["kind"],
            "uid": meta["uid"], "category": meta["category"] or ("Synth" if meta["kind"] == "instrument" else "Effect"),
            "numInputs": meta["numInputs"], "numOutputs": meta["numOutputs"],
        }
        if meta["description"]:
            manifest["description"] = meta["description"]
        if meta["license"]:
            manifest["license"] = meta["license"]
        if re.fullmatch(r"[\w.-]+/[\w.-]+", meta["repo"]):
            manifest["url"] = "https://github.com/" + meta["repo"]
        created[dest] = manifest
    elif manifest["uid"] != meta["uid"]:
        raise ConvertError("%s: uid differs from the other package for this plugin; they are not the same plugin" % legacy)
    if key in manifest:
        warnings.append("already have a %s build of %s; ignoring this one" % (meta["arch"], meta["name"]))
        return dest, warnings

    # The binary stays where the package had it: these plugins may look for their data next to the .so.
    rel = meta["rel"]
    if os.path.exists(os.path.join(dest, rel)):  # the other architecture's build has the same file name
        stem, ext = os.path.splitext(rel)
        rel = "%s-%s%s" % (stem, meta["arch"], ext)
    os.makedirs(os.path.dirname(os.path.join(dest, rel)), exist_ok=True)
    shutil.copy2(os.path.join(legacy, meta["rel"]), os.path.join(dest, rel))
    os.chmod(os.path.join(dest, rel), 0o755)
    manifest[key] = rel
    copy_missing(legacy, dest)
    if not os.path.isfile(os.path.join(dest, "version.xml")):
        with open(os.path.join(dest, "version.xml"), "w", encoding="utf-8", newline="\n") as f:
            f.write('<?xml version="1.0" encoding="utf-8"?>\n<plugincontent version="1.0">\n'
                    "    <version>%s</version>\n</plugincontent>\n" % pad_version(meta["version"]))
    return dest, warnings


def finish(dest, manifest, make_zip, out_dir):
    """Write plugin.json (binaries in a stable key order) and optionally the zip. Returns the zip path or None."""
    ordered = {k: manifest[k] for k in ("name", "manufacturer", "version", "kind", "uid", "category", "numInputs",
                                         "numOutputs", "file32", "file64", "description", "license", "url") if k in manifest}
    ordered["convertedFrom"] = "community mpc-vst-plugins package"
    with open(os.path.join(dest, "plugin.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(ordered, f, indent=2, ensure_ascii=False)
        f.write("\n")
    if not make_zip:
        return None
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from package import install_text  # same INSTALL.txt as plugins built with the SDK
    folder = os.path.basename(dest)
    version = legal_filename(str(manifest["version"])).replace(" ", "-")
    zpath = os.path.join(out_dir, "%s-%s-mpc.zip" % (make_slug(manifest["name"]), version))
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as zf:
        for root, dirs, files in os.walk(dest):
            dirs.sort()
            for fn in sorted(files):
                full = os.path.join(root, fn)
                info = zipfile.ZipInfo.from_file(full, os.path.join(folder, os.path.relpath(full, dest)).replace(os.sep, "/"))
                info.compress_type = zipfile.ZIP_DEFLATED
                with open(full, "rb") as fh:
                    zf.writestr(info, fh.read())
        zf.writestr("INSTALL.txt", install_text(ordered, folder))
    return zpath


def convert(sources, out_dir, make_zip=False, force=False):
    """Convert packages. Returns a list of dicts: folder, zip, manifest, warnings."""
    out_dir = os.path.abspath(out_dir)
    os.makedirs(out_dir, exist_ok=True)
    work = tempfile.mkdtemp(prefix="legacy-")
    created, warns = {}, {}
    try:
        for src in sources:
            src = os.path.expanduser(src)
            if not os.path.exists(src):
                raise ConvertError("%s does not exist" % src)
            if os.path.isfile(src):
                root = tempfile.mkdtemp(dir=work)
                safe_extract(src, root)
            else:
                root = src
            legacy = find_legacy_folders(root)
            if not legacy:
                raise ConvertError("%s has no plugin-meta.xml, so it is not a community mpc-vst-plugins package" % src)
            for folder in legacy:
                meta = read_legacy(folder)
                dest = os.path.join(out_dir, legal_filename("%s - VST - %s" % (meta["manufacturer"], meta["name"])))
                if dest not in created and os.path.exists(dest):
                    if not force:
                        raise ConvertError("%s already exists; use --force to replace it" % dest)
                    shutil.rmtree(dest)
                dest, w = convert_folder(folder, out_dir, created)
                warns.setdefault(dest, []).extend(w)
        results = []
        for dest, manifest in created.items():
            w = warns.get(dest, [])
            if "file32" not in manifest:
                w.append("no 32-bit build: it will not run on MPC X / Live / Live II / One / Key / Force")
            if "file64" not in manifest:
                w.append("no 64-bit build: it will not run on MPC Live III / XL (add the aarch64 zip if the author has one)")
            results.append({"folder": dest, "zip": finish(dest, manifest, make_zip, out_dir), "manifest": manifest, "warnings": w})
        return results
    finally:
        shutil.rmtree(work, ignore_errors=True)


def main(argv=None):
    ap = argparse.ArgumentParser(description="Convert community mpc-vst-plugins packages to the SDK plugin folder format.")
    ap.add_argument("sources", nargs="+", help="release zip(s) or unzipped folder(s); give armv7 and aarch64 zips of one plugin together")
    ap.add_argument("--out", default="converted", help="where to write the converted plugin folders (default ./converted)")
    ap.add_argument("--zip", action="store_true", help="also write a zip with INSTALL.txt for sharing")
    ap.add_argument("--force", action="store_true", help="replace a converted folder that already exists")
    args = ap.parse_args(argv)
    try:
        results = convert(args.sources, args.out, args.zip, args.force)
    except ConvertError as e:
        print("ERROR: %s" % e)
        return 1
    for r in results:
        m = r["manifest"]
        archs = "/".join(a for a, k in (("arm32", "file32"), ("arm64", "file64")) if k in m)
        print("Converted %s %s (%s)" % (m["name"], m["version"], archs))
        print("  folder: %s" % r["folder"])
        if r["zip"]:
            print("  zip:    %s" % r["zip"])
        for w in r["warnings"]:
            print("  WARNING: %s" % w)
    return 0


if __name__ == "__main__":
    sys.exit(main())
