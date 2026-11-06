---
name: mpc-plugin-deploy
description: Install, list, update, remove or export VST2 plugins on a real Akai MPC over the network (SSH, developer mode) with tools/mpc_remote.py, and walk the user through setting the MPC up (Enhancements, External Plugin Support, developer mode). Use when the user wants a plugin put on their MPC automatically, gives an MPC IP address, or asks to back up or manage plugins on the device.
---

# Deploy to an MPC over the network

`tools/mpc_remote.py` talks to an MPC on the local network over SSH and manages the `VST2 Plugins` folder of one of its drives. It needs only Python 3.9+ and the computer's `ssh` command (use `python3` if `python` is not found).

## What the user must have done on the MPC

Check these with the user before connecting (the script also diagnoses them, but the user has to do them on the device):

1. MPC OS **3.10.1 or later** (Preferences → Info).
2. **Preferences → Enhancements (Beta)**: turn on **Enable Enhancements**, then **External Plugin Support**. Read the warning with them.
3. **Developer mode** on: Preferences → Info; tap the version value 3 times, the serial number value 2 times, then the firmware version once. This starts the MPC's SSH server. (If it does not seem to work, ask the user to describe what the Info page shows.)
4. The MPC and the computer on the same network. The MPC's IP address is in Preferences, on the Wi-Fi tab (hold Shift to access it) or on the Ethernet tab, depending on how it is connected.

Developer mode is an official, supported switch, but it opens a login on the device: tell the user to switch it off again when finished on a network they do not control. This skill only ever writes inside the `VST2 Plugins` folder of the chosen drive. Never use it, or any SSH access, to change system files, firmware or settings.

## Ask for what you need

- The MPC's **IP address** (ask if not given). A password is only needed if the user has set one; do not guess passwords and do not put one in files. If one is needed pass it with the `MPC_SSH_PASSWORD` environment variable (preferred) or `--password`.
- Which drive, if several are mounted (the script lists them; `--drive` takes the number, name or mount point). Units with an internal NVMe drive list it too (plugins go in `/nvme/VST2 Plugins`). Removable drives may be mounted `noexec`; the script warns, and the internal drive is the fallback.

## Commands

```
python3 tools/mpc_remote.py <ip>                         # interactive menu (needs a real terminal)
python3 tools/mpc_remote.py <ip> info                    # model, serial number, firmware version, CPU, drives
python3 tools/mpc_remote.py <ip> doctor                  # setup diagnostics
python3 tools/mpc_remote.py <ip> restart --yes           # restart the MPC app so it rescans VSTs
python3 tools/mpc_remote.py <ip> list                    [--drive N]
python3 tools/mpc_remote.py <ip> add plugins/<Name>      # an SDK project (its build/ output) ...
python3 tools/mpc_remote.py <ip> add some-plugin.zip     # ... or a zip from the package tool / a download
python3 tools/mpc_remote.py <ip> update plugins/<Name>   # replace an installed copy, no question asked
python3 tools/mpc_remote.py <ip> import <release>.zip [<aarch64>.zip] --out converted --zip   # convert a community mpc-vst-plugins package (needs no hacked MPC OS), save it, then add it
python3 tools/mpc_remote.py <ip> remove "<folder or name>" --yes
python3 tools/mpc_remote.py <ip> export <name> | --all --out <dir>    # always written as zip files
```

Always pass `--drive` and `--yes` when you drive it from an agent shell: there is no terminal to answer prompts, and `--drive` is required when more than one drive is mounted. If the user has several drives and has not said which, run `info` first, show them the list and ask.

Add `--no-color` when output is captured. Exit codes: 0 ok, 1 an operation failed, 2 the MPC is not reachable or not set up (read the diagnostic panel).

## Workflow for "put my plugin on my MPC"

1. Make sure the plugin is built and tested (`mpc-plugin-build`, `mpc-plugin-test`): `plugins/<Name>/build/` must hold the `"<Manufacturer> - VST - <Name>"` folder. Never deploy something that has not passed the build and `host_sim` checks.
2. Run `info` (or `doctor`) to confirm the connection and pick the drive.
3. `add plugins/<Name> --drive <d>` (first time) or `update plugins/<Name> --drive <d>` (new build). The tool checks that the ARM build matching the MPC's processor is present, copies via a hidden staging folder, verifies every file size, and keeps the previous version until the new one has arrived intact.
4. After every successful `add`, `update` or `import`, tell the user the MPC only scans for VST2 plugins when its app starts, so the plugin will not appear until it is restarted. **Suggest the restart** and offer to do it with `restart` (see below); remind them to save their project first and only run it once they agree. They can also restart by powering the MPC off and on. Then tell them the plugin is under **External Plugins** in the plugin browser; for a track that already uses an updated plugin, remove and re-add it (a restart also picks up changed skins). If it still does not show after the restart, run `doctor` and check the Enhancements switches.

## Restarting the MPC app

`import` takes zips or folders from the community plugin store (https://sd88me.github.io/mpc-vst-plugins/, made for a modified MPC OS) and rewrites them into the SDK folder format with `tools/legacy_convert.py` (also usable alone, no MPC needed). Give the armv7 and aarch64 zips of one plugin together to get one folder that works on every model. The converted folder (and a zip with `--zip`) is saved to `--out` for redistribution, and installing it asks the same questions as `add`. The plugin binaries are not modified; mention the plugin's licence (recorded in `plugin.json`) before redistributing, and that a plugin written for the modified OS may rely on features the stock host lacks, so test it.

`restart` restarts the MPC application service (`acvs`) so it rescans VST2 plugins; it needs no drive. **Any unsaved project data on the MPC is lost**, so never run it (or pass `--yes`) without the user explicitly agreeing, and remind them to save first. Suggest it after every install or update, since the MPC only rescans VSTs at startup; it is not needed after `list`, `export` or other read-only commands.

## When it will not connect

Read the panel the script prints; it names the cause:

| Panel | Meaning | What to do |
|---|---|---|
| "can't reach" | Timeout: wrong address, different network, client isolation | Check the IP in Preferences (Wi-Fi tab, hold Shift; or Ethernet tab), same Wi-Fi, try Ethernet |
| "SSH server is off" | Connection refused: developer mode is not on | Do steps 1-3 above, then run again |
| "Login refused" | SSH is up but wants a password or another user | Ask the user for it; `--password`/`MPC_SSH_PASSWORD`, `--user` |
| "No drives found" | Nothing mounted under /media | Insert a USB drive / SD card, check the internal storage |
| "read-only" | The drive is write protected or mounted read-only | Another drive, or the card's lock switch |

Remember `doctor` cannot read the Enhancements / External Plugin Support switches remotely; if everything installs fine but the plugin never appears in the browser, those switches (and MPC OS 3.10.1+) are the first thing to check. Then `mpc-plugin-troubleshoot`.

## What the tool does not do

It does not build, rename or edit plugins, and never installs outside `VST2 Plugins`. Apart from the app restart above, it does not run anything else on the device. Pass-through of unknown commands to the device is not offered on purpose.
