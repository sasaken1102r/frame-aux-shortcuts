# frame-aux-shortcuts

Shortcuts on the Steam Frame's **aux button** (the small button above the power button). Give each of six ways of pressing it — 1 to 5 presses and a long press — and two or three taps on the headset its own action: mute the mic, step through brightness levels, turn passthrough on and off, take a screenshot, show or hide [frame-perf-overlay](https://github.com/sasaken1102r/frame-perf-overlay), open the dashboard, recenter the view, do what aux normally does, play / pause or skip the music playing on the Frame, or run a command of your own. A short sound and a small notice in your view tell you what happened. Everything is set up from a tab in the SteamVR dashboard. It runs on the headset itself and needs no sudo.

[日本語版はこちら](README.ja.md)

> **Unofficial tool.** It is not affiliated with Valve. The passthrough action relies on a private SteamVR interface, so a SteamVR update may break it. Use it at your own risk.

![The Assign tab in the SteamVR dashboard](docs/images/panel-assign-en.png)

![The notice after a press](docs/images/hud-notice-en.png)

![The muted indicator](docs/images/hud-muted-en.png)

The images were written out by the programs themselves, not captured in the headset; the states, times, commands and IP address in them are sample values.

## What it does

| Press | Decided when | Default action |
|---|---|---|
| 1–5 presses | 0.35 s after you let go without pressing again | 1: mic mute, 2: brightness, 3: passthrough, 4: screenshot, 5: perf overlay |
| Long press | as soon as you've held it for 0.6 s | Dashboard |
| Tap the headset 2 or 3 times | 0.45 s after the last tap (right at the 2nd tap if nothing is assigned to 3 taps) | None |

Six or more presses do nothing, so a miscount never fires the wrong action. The volume buttons are not affected. While the app runs, SteamVR no longer sees aux (normally one press opens the dashboard and two toggle passthrough); your assignments replace that. If you use the head cursor (the dot in the middle of your view) instead of controllers, aux is your click: assign **Original aux**, or turn on **Normal aux when no controller is connected**.

**Headset taps** work only while you wear the headset (not while it is off your head). Knock lightly on its side with a finger. Bumps while your head is moving don't count, so turning your head or walking won't trigger them. If you also assign 3 taps, 2 taps waits for a possible third and acts about 0.45 s later. For example, assign Passthrough to 2 taps to toggle it by tapping, like on a Meta Quest.

Actions you can assign:

- **Mic mute**: toggles the default mic's mute (`wpctl`).
- **Brightness**: steps through 2–4 levels (40–125 %, default 100 → 60 → 40 %), the same setting as SteamVR's brightness slider.
- **Passthrough**: on / off, through a private SteamVR interface (see [How it works](#how-it-works)).
- **Screenshot**: saves what you see to `~/Pictures/frame-aux-shortcuts/` and adds it to Steam's screenshots.
- **Perf overlay**: shows or hides [frame-perf-overlay](https://github.com/sasaken1102r/frame-perf-overlay), if it is installed and running.
- **Dashboard**: opens the SteamVR dashboard.
- **Recenter view**: makes the direction you are facing the front, the same as **Recenter** in the dashboard (or holding the controller's System button). It works only while you are wearing the headset. Like the built-in one, SteamVR keeps the new direction.
- **Original aux**: the same as one press of aux without this app. It opens the dashboard if it is closed, and clicks with the head cursor when the cursor is showing (clicking outside the dashboard closes it). For a moment (about 0.08 s) the app puts the key map back and passes an aux press to SteamVR.
- **Play / pause**, **Next track**, **Previous track** (the **Music** side of the list): control music or video playing on the Frame itself, in apps that support MPRIS such as Chrome and Chromium. If several are playing, it picks the one it controlled last; after a pause, play / pause resumes the same one. The notice shows the track title. It can't control audio from a PC streamed over Steam Link.
- **A command** of your own (see [Commands](#commands)), or **None**.

In your view, a notice shows what happened for about 2 seconds, and "Mic muted" stays at the top while the mic is muted (however it was muted). A short sound plays on each press, a different one when the action failed. Each of these can be turned off.

## Settings panel

Open the SteamVR dashboard and pick the **aux Shortcuts** tab at the bottom. Changes apply at once and are saved. The panel is in English or Japanese (it starts in your Steam language). It reopens on the tab you used last.

![The Commands tab](docs/images/panel-commands-en.png)

- **Assign**: a card for each press and tap with its current state; pick a card, then an action, a music control or one of your commands.
- **Brightness**: the brightness levels.
- **Presses**: the long press time (0.3–1.5 s) and the count gap (0.2–0.8 s), with a timeline.
- **Commands**: add (also from presets), edit, try, delete and reorder your commands.
- **Display**: sound, notice and muted indicator on / off.
- **App**: updates, keep aux on when the panel is closed, normal aux when no controller is connected, start with SteamVR, language, reset, quit. On the right, the apps it uses (frame-perf-overlay, frame-jp-keyboard) and a list of all my Frame apps; **Install** opens Konsole with the one-line installer, after showing you the command.

## Requirements

- A Steam Frame with Developer Mode on (Steam Settings → System → "Enable Developer Mode"), and SteamVR.
- No sudo; everything goes into your home folder. Optional: [frame-perf-overlay](https://github.com/sasaken1102r/frame-perf-overlay) for the perf overlay action.

## Install

### Easiest: install right inside the Frame (recommended)

No PC needed. In Konsole on the Frame (+ on the bar at the bottom → the list of programs → Konsole), type this command, press Enter, and pick **frame-aux-shortcuts** from the menu.

```sh
curl -fsSL https://frame.sasaken1102s.net | sh
```

- Do this once first: turn on Developer Mode (while it's off, Konsole doesn't show up in the + list).
- The other apps (frameeyeosc, frame-jp-keyboard, frame-mic-tuner, frame-perf-overlay) can be installed from the same menu.
- To install without any prompts: `curl -fsSL https://frame.sasaken1102s.net | sh -s -- install aux`
- Step-by-step guide and video: https://frame.sasaken1102s.net

If SteamVR is running it starts right away, and from then on it starts together with SteamVR.

### Install from a PC

Copy `frame-aux-shortcuts-<version>.tar.gz` from the [releases page](https://github.com/sasaken1102r/frame-aux-shortcuts/releases) to the headset, then on the headset:

```sh
tar xzf frame-aux-shortcuts-*.tar.gz
cd frame-aux-shortcuts
./install.sh                 # install or update (--no-autostart: start it yourself from the dashboard's +)
./install.sh --uninstall     # remove it
```

It installs `~/.local/bin/frame-aux-shortcuts` (the background part) and `frame-aux-shortcuts-panel` (the panel), two systemd user services, a launcher entry with icons, and the updater script.

## Updates and uninstall

- **Update**: run the same one-line command and pick the app again, or press **Update** in the panel's **App** tab (it checks GitHub at start and about once an hour; turn that off with **Check for updates**).
- **Uninstall**: pick `u` in the same menu (or `./install.sh --uninstall`). aux goes back to normal. Your settings are kept unless you choose to delete them, and your screenshots stay.

## Usage

- **Press aux.** The shortcuts work whenever SteamVR runs; nothing else needs to be open.
- **The + button** in the dashboard lists **Frame Aux Shortcuts**; it opens the panel and starts the background part if needed.
- **Closing the panel** keeps the shortcuts working (unless you turn off **Keep aux on when closed**).
- **Normal aux when no controller is connected** (App tab, off by default): while no controller is connected (SteamVR uses the head cursor), aux goes back to SteamVR so you can click with it. When a controller connects, the shortcuts come back. A notice in your view tells you when it switches (`original_aux_on_head_pointer` in the settings file).
- **Quit panel and background** (App tab, press twice) gives aux back to SteamVR until the next SteamVR start.
- **Reset everything** (App tab, press twice) resets all settings except the language — **including your registered commands**.

## Settings file

`~/.config/frame-aux-shortcuts/config.json`. The panel writes it; you only need to edit it by hand for what the panel doesn't show: the sound files, the screenshot folder and `screenshot_to_steam`. Changes are picked up within a second. Keys you leave out keep their defaults. A full example is in [`contrib/config.example.json`](contrib/config.example.json):

```json
{
  "version": 3,
  "sound": { "enabled": true, "success": "/path/to/ok.wav", "failure": "/path/to/fail.wav" },
  "screenshot_dir": "~/Pictures/frame-aux-shortcuts",
  "screenshot_to_steam": false
}
```

## Commands

- A command is a program and its arguments, run as your user **without a shell** (no pipes, variables or `&&`; write a script for those). aux doesn't wait for it to finish.
- In the panel, spaces separate arguments, `"…"` keeps text together, and a leading `~/` is your home folder.
- Executable files in `~/.config/frame-aux-shortcuts/commands/` are listed too, named after the file.
- Only add commands you trust: a command can do anything your user can.
- **Presets**: **+ Add command** → **Presets** adds one of these; after that it is a normal command.
  - **VRChat mute (PC)**: sends OSC (`/input/Voice`) to VRChat on the PC you stream from with Steam Link, toggling VRChat's mute. It finds the PC by itself (or enter its IP). First turn on OSC in VRChat on the PC and set the mic to toggle mode. If nothing arrives, allow VRChat on public networks too in Windows Firewall (Windows treats the link to the Frame as an unidentified, public network).
  - **Mute speakers**: `wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle`.

![Adding the VRChat mute preset](docs/images/panel-presets-en.png)

Tap the **Name** or **Command to run** field and the SteamVR keyboard opens; text appears as you type. Move the cursor with the left and right arrow keys, or tap where you want it in the field. For Japanese names, use [frame-jp-keyboard](https://github.com/sasaken1102r/frame-jp-keyboard).

## Troubleshooting

- **aux does nothing at all** (no shortcuts, no dashboard): the background part stopped without giving aux back (for example after `kill -9`). Run `~/.local/bin/frame-aux-shortcuts --restore` or restart the headset.
- **Passthrough fails** with "A SteamVR update may have broken this": SteamVR changed the private interface. Check for a newer version of this app.
- **Logs**: `journalctl --user -u frame-aux-shortcuts -u frame-aux-shortcuts-panel` (add `-f` to follow). Messages are in Japanese.

## How it works

- Linux sees aux as `KEY_SELECT` on the `gpio-keys` input device. The background part changes that device's key map so aux sends an unused key; SteamVR then ignores it and the app reads the presses itself. It doesn't grab the device, so volume up still works. **Original aux** puts the key map back for a moment, writes a `KEY_SELECT` press and release to the same device (writing to an evdev device is input for its other readers), and changes the key map again right away. The key map lives only in kernel memory: it is put back when the app ends (including crashes), and a restart always resets it. No sudo is needed (the `steamos` user is in the `input` group).
- For headset taps, the app reads the headset's angular velocity through the public OpenVR API (`GetDeviceToAbsoluteTrackingPose`) every 4 ms (250 Hz) and counts the brief jolt of each tap (a spike in angular acceleration). Only jolts while the head was nearly still count, and taps 0.1–0.45 s apart form one group. It reads the pose only while something is assigned to a tap and you are wearing the headset.
- Passthrough has no public OpenVR call, so the app sends the request to a **private**, undocumented interface of SteamVR's local web server (`127.0.0.1`, on the headset). Everything else uses the public OpenVR API and standard tools (`wpctl`, `pw-play`, `systemctl`).
- **Recenter view** calls the public OpenVR API `IVRChaperone::ResetZeroPose` for the standing and the seated pose, as the built-in recenter does. It doesn't change the floor height, and the app writes no files for it. A second request within 1 s is ignored.
- The music controls talk to the player over the session D-Bus (the standard MPRIS interface `org.mpris.MediaPlayer2.Player`, through `libsystemd`'s sd-bus): they read the players' state and call `Play`, `Pause`, `Next` or `Previous`. Each call gives up after 1 s or less, and it runs off the main loop.
- For VRChat mute, the app reads from `/proc` which PC the Steam Link client (`vrlink`) is connected to over UDP (read only), then runs `frame-aux-shortcuts --send-osc auto:9000 /input/Voice`: it sends 1, then 0 after 0.15 s.
- It never writes to sysfs, GPIO, `/persist`, `/opt`, `/etc` or Steam's files, never touches the cameras, and never restarts PipeWire or SteamVR.

## Privacy

- No telemetry. The only outside network access is the update check to GitHub, and the OSC message (UDP port 9000) to the connected PC when you use the VRChat mute preset. With **Check for updates** on, it also fetches the list of apps and their icons from frame.sasaken1102s.net (at most once an hour). When you open the installer from the App tab, it connects to frame.sasaken1102s.net and GitHub inside Konsole.
- It never records audio; for the mic it only reads and toggles the mute state.
- Logs stay on the headset. For commands they record the name, not the arguments, and keyboard input is not logged. Track titles are shown in the notice but not logged.
- The settings file is saved with permissions `0600` (only you can read it, since commands may contain secret values), and a new settings folder is created `0700`. If `/run/user/<uid>` is missing, the lock and status files go in `/tmp/frame-aux-shortcuts-<uid>/` (`0700`), and the app refuses to use that folder if it is a symlink or belongs to someone else.

## Development

Build on the headset (the tools and libraries come with SteamOS):

```sh
cmake -G Ninja -S . -B build && cmake --build build
ctest --test-dir build --output-on-failure   # unit tests, no hardware needed
./install.sh                                 # installs the programs from build/
scripts/package.sh                           # release tarball and SHA256SUMS in dist/
```

Release builds use link-time optimization when the compiler supports it and remove unused code, keeping the usual speed optimization. Set `-DFRAME_AUX_OPTIMIZE_RELEASE=OFF` to compare with the original build settings.

`--help` lists the options of each program (for example `--show-keymap`, `--test-action TYPE`, `--dump-png`). `vendor/frame-updater/` and `vendor/frame-apps/` are copies of the author's shared updater and app list; don't edit them here.

## License

MIT. See [LICENSE](LICENSE). Bundled third-party code and what it uses at run time are listed in [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md); changes are in [CHANGELOG.md](CHANGELOG.md) (in Japanese).

This is an unofficial project, not affiliated with or endorsed by Valve Corporation. Steam, Steam Frame and SteamVR are trademarks and/or registered trademarks of Valve Corporation. VRChat is a trademark of VRChat Inc.; this project is not affiliated with VRChat. The software comes with no warranty, and it was made with an AI assistant (Claude) and tested on the author's own Steam Frame.
