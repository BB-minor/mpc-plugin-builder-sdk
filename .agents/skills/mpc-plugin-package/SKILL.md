---
name: mpc-plugin-package
description: Package a built MPC plugin into a distributable zip with installation instructions for MPC owners. Use when the user wants to share, release, publish, upload or send their plugin, or asks how others install it.
---

# Package for distribution

Prerequisite: a clean build (`python tools/build.py plugins/<Name>` with no validator errors) and ideally a pass on the MPC itself.

```
python tools/package.py plugins/<Name>
```

Produces `plugins/<Name>/dist/<slug>-<version>-mpc.zip` containing:
- `<Manufacturer> - VST - <Name>/` the complete plugin folder (both ARM builds, plugin.json, version.xml, Plugin Skins, Content)
- `INSTALL.txt` step-by-step install instructions for non-technical users (requires MPC OS 3.10.1 or later; copy to `VST2 Plugins` on a drive; enable VST plugins on the device via Preferences → Enhancements (Beta) → Enable Enhancements + External Plugin Support; find it under External Plugins; warnings; how to uninstall)
- the project's `README.md` if present

Before packaging:
1. Set `version` in `plugin.json` (semantic: `1.0.0`, bump for every release) and make sure `description`, `author`, `url` and `license` keys are filled; INSTALL.txt uses them.
2. Make the project `README.md` useful to players: what it does, parameter list by page, tips, known limits, licence, credits for any DSP code you borrowed.
3. Never change `uid` or parameter ids between releases.

Hand the zip path to the user and summarise what is inside. If they want to publish, suggest a GitHub release with the zip attached and the README as the release notes.
