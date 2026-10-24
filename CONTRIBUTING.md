# Contributing

Thanks for helping. A few ground rules keep the SDK safe to redistribute:

- **No Steinberg VST SDK code**, ever, in any form. The ABI header is a clean-room description; keep it that way.
- **No files extracted from MPC firmware** (skins, images, fonts, binaries, settings files). Describe formats; do not copy artefacts.
- **No MPC system modifications.** The SDK targets the official plugin folder and preference; pull requests that depend on root access, patched binaries or custom firmware will be declined.
- **MIT only** for anything added to `framework/`, `tools/`, `templates/` and `docs/`. Examples may use other OSI licences if clearly marked, but prefer MIT so people can reuse them freely.
- **Keep it simple for an AI agent.** Docs are read by coding agents; be exact about names, versions and paths. Tools are stdlib Python with friendly error messages.

Workflow: fork, branch, run `python tools/build.py examples/SimpleSynth` (all three examples should build and validate), open a pull request describing what changed and on which MPC you tested.

Reporting problems: open an issue with your MPC model, firmware version, the output of `python tools/validate.py` and the host simulator report.
