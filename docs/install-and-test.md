# Testing and installing

## 1. Test on your computer first

The host simulator (`tools/host-sim`) is a small command-line VST2 host that behaves like the MPC's host. `build.py` builds it automatically and uses it to extract the parameter list; you can also run it yourself:

```
# Load sequence + render + state round trip + parameter sweep + stability checks
.sdk-tools/host_sim build/native/<slug>.dylib test --seconds 3 --wav build/test.wav

# Hear it: open build/test.wav in any audio player.
# Instruments: --notes 48,52,55 picks the notes played. --playing simulates transport running.
# Effects: a sine tone and a noise burst are fed through the plugin.

# Print the parameter list the MPC will see
.sdk-tools/host_sim build/native/<slug>.dylib dump
```

The `test` command fails on NaN/infinite output, crashes, and state that does not round-trip, and warns about clipping, denormals, silence and high CPU. Fix every FAIL before copying anything to the MPC. The MPC has no crash protection for plugins.

## 2. Copy to the MPC

1. Build: `python tools/build.py plugins/MyPlugin`.
2. Copy the whole folder `build/<Manufacturer> - VST - <Name>/` into a folder named exactly `VST2 Plugins` at the root of a USB drive or SD card (create `VST2 Plugins` if it does not exist). You can also copy it to the MPC's internal drive over USB in computer mode, or with the MPC's file browser from a drive.
3. Insert the drive in the MPC.
4. On the MPC (must be running MPC OS 3.10.1 or later; check Preferences → Info and update if older): **Preferences → Enhancements (Beta)**: turn on **Enable Enhancements** and **External Plugin Support**. Read the warning. The plugin stays hidden until both switches are on.
5. Open the plugin browser (instrument track → plugin program → browse, or an insert slot). Your plugin is listed under **External Plugins**.

Replacing a plugin: copy the new folder over the old one, then remove and re-add the plugin on the track (or restart the MPC if the skins changed).

### Or install over the network

If the MPC is on the same network as your computer, `tools/mpc_remote.py` copies the plugin for you and can also list, update, remove and export plugins on the device:

```
python3 tools/mpc_remote.py 192.168.1.50                 # interactive menu
python3 tools/mpc_remote.py 192.168.1.50 add plugins/MyPlugin
python3 tools/mpc_remote.py 192.168.1.50 add some-plugin.zip
python3 tools/mpc_remote.py 192.168.1.50 export --all --out ~/mpc-backup
```

It needs the setup from step 4 above (MPC OS 3.10.1+, Enable Enhancements, External Plugin Support) plus **developer mode**, which starts the MPC's SSH server: Preferences → Info, tap the version value 3 times, the serial number value 2 times, the firmware version once. Run `python3 tools/mpc_remote.py <ip> doctor` if it will not connect; it explains what is missing. It only writes inside the `VST2 Plugins` folder of the drive you choose. Switch developer mode off again when you are done on a network you do not control. With Claude Code or Codex, just ask: "install this on my MPC at 192.168.1.50".

## 3. Check the UI

- With no `Plugin Skins`, the MPC shows pages of 16 sliders, one per parameter, with the hardware knobs mapped in order.
- With generated skins, your pages appear on the function keys at the bottom of the screen. Touch a control to assign the knob column; double-tap or press Enter for a big edit overlay; turn the data wheel to change the focused control.
- If the plugin's edit screen shows **no tabs at all**, `TUI.json` failed to parse. Run `python tools/validate.py plugins/MyPlugin` and check the JSON.

## 4. Save a project, reload it

Make sure the plugin, its parameters and its program come back. The framework's state format is stable across versions as long as parameter ids do not change.

## 5. Watch CPU

The MPC shows CPU use in its own meters. A single plugin instance should stay well under 15% on a 32-bit MPC at 128-frame blocks. Several instances multiply that.

## Uninstalling

Delete the folder from `VST2 Plugins`. Projects that used it will load without the plugin.
