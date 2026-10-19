# Razer Rebind

Rebind the keys and buttons of Razer keyboards and mice on Linux. Razer
Rebind works with the [OpenRazer](https://openrazer.github.io/) kernel
drivers, runs on X11 and Wayland, and needs no root once installed.

- **Rebind anything:** click a key on the keyboard picture or a button on the
  mouse picture, or press it on the device. Send another key, a mouse button,
  a shortcut such as Ctrl + C, or nothing at all. The scroll wheel can be
  rebound too, and a mouse button can type a shortcut.
- **Hypershift:** hold a key or button you choose to give every device a
  second set of bindings, like Razer Synapse's Hypershift. Hold a mouse side
  button and your keyboard keys do something else.
- **Macros:** record a sequence of keys and buttons, with its timing, and
  play it from any key or button: once, repeating while held, or on/off.
- **Launch anything:** a key or button can run a program, a script or any
  shell command.
- **Tap vs hold:** one key, two jobs. Tap Caps Lock for Esc, hold it for
  Ctrl; or tap Space for a space and hold it for Hypershift.
- **Turbo:** a key or button repeats as long as you hold it, from 1 to 50
  presses per second.
- **DPI and polling rate:** set up to five DPI stages for the button under
  the mouse, and the polling rate. Each profile keeps its own, so a game's
  profile can bring its own sensitivity.
- **Profiles per device:** keep separate sets of bindings, such as
  "Desktop", "Gaming" and "Editing", for each device. Switch the keyboard
  without touching the mouse, or switch everything at once.
- **Per-game profiles:** link a game to a profile, and your devices switch
  to it while the game runs and back when you quit. Steam, Lutris, Heroic
  and Proton games all work.
- **Global hotkeys:** switch profiles or turn remapping off from inside a
  game, with a key combination on your Razer keyboard or mouse.
- **Battery at a glance:** the tray icon shows a battery gauge for your
  wireless devices, and a desktop notification warns you when one drops
  below a level you choose.
- **Built for gaming:** adds about 0.015 ms of input delay, even with every
  CPU core busy (see [Performance](#performance)). Your KDE pointer settings
  keep working.
- **Stays out of the way:** runs in the system tray or as a background
  service. Devices with nothing to remap aren't touched at all.

> **About this project:** Razer Rebind is a personal project I built for my
> own keyboard and mouse. Anyone is free to use, modify and share it under
> the [GPL-2.0-or-later](LICENSE) license, but support and new features are
> best-effort.
>
> **AI disclosure:** this project was developed with the help of an AI
> assistant (Claude, by Anthropic), which wrote much of the code and
> documentation under my direction. I tested it on my own hardware.

## Supported devices

Razer Rebind was developed with the **Razer Huntsman Elite** keyboard and the
**Razer Viper Ultimate** mouse (wired and wireless), which get a full
clickable picture of the device. Any other keyboard or mouse driven by
OpenRazer shows up too, and all its keys and buttons can be bound through
the bindings list and **Detect**.

## Requirements

- Linux with the **OpenRazer kernel modules** loaded (`razerkbd`,
  `razermouse`). See [OpenRazer's install guide](https://openrazer.github.io/#download).
  The OpenRazer daemon and frontends such as Polychromatic are optional and
  can run alongside.
- A desktop session (KDE Plasma, GNOME, or others) on X11 or Wayland.

## Installation

Download the package for your system from the
[latest release](../../releases/latest). Every package installs the app, its
menu entry, the background service and the device permission rule.

| System | Download |
|---|---|
| Fedora 43, 44, Nobara | `razer-rebind-<version>.fc43.x86_64.rpm` or `.fc44` |
| Debian 12, 13 | `razer-rebind_<version>-1_debian12_amd64.deb` or `_debian13` |
| Ubuntu 24.04, 26.04 | `razer-rebind_<version>-1_ubuntu24.04_amd64.deb` or `_ubuntu26.04` |
| Any distribution, no install | `razer-rebind-<version>-x86_64.AppImage` |
| Other distributions | `razer-rebind-<version>-linux-x86_64.tar.gz` (pre-compiled) |

`SHA256SUMS` in the release lets you check that a download is intact
(`sha256sum -c SHA256SUMS --ignore-missing`).

### Fedora and Nobara

```bash
sudo dnf install ./razer-rebind-*.fc44.x86_64.rpm
```

### Debian and Ubuntu

```bash
sudo apt install ./razer-rebind_*_ubuntu24.04_amd64.deb
```

Use the file for your release; `apt` installs the Qt and libevdev libraries it
needs.

### AppImage

The AppImage bundles everything it needs and runs on distributions at least
as new as Debian 12. Make it executable and start it:

```bash
chmod +x razer-rebind-*-x86_64.AppImage && ./razer-rebind-*-x86_64.AppImage
```

An AppImage can't install the device permission rule by itself, so run this
once (Razer Rebind also shows this command, ready to copy, under
**Help → Device permissions**):

```bash
./razer-rebind-*-x86_64.AppImage --print-udev-rule | sudo tee /etc/udev/rules.d/70-razer-rebind.rules >/dev/null && sudo udevadm control --reload && sudo udevadm trigger
```

To start it at login, add the AppImage to your desktop's autostart with the
`--minimized` option.

### Other distributions

The pre-compiled tarball runs on distributions at least as new as Debian 12
(glibc 2.36 or newer, Qt 6.4 or newer). Install Qt 6 (base and Wayland) and
libevdev from your distribution first, for example on Arch:

```bash
sudo pacman -S qt6-base qt6-wayland libevdev
```

Then unpack it and run the installer, which puts the app in `/usr/local` and
the permission rule in `/etc/udev/rules.d`:

```bash
tar xf razer-rebind-*-linux-x86_64.tar.gz && cd razer-rebind-*-linux-x86_64 && sudo ./install.sh
```

`sudo ./uninstall.sh` in the same folder removes it again. Your settings are
kept.

To build from source instead, see [CONTRIBUTING.md](CONTRIBUTING.md#building).

After installing, start **Razer Rebind** from your application menu.

## Usage

### Rebinding keys and buttons

1. Pick your device on the left.
2. Click a key or button on the picture. You can also click **Add binding…**
   and press **Detect** to press the key on the device itself.
3. Choose what it should do: another key, a shortcut (tick the modifiers or
   use **Record shortcut**), **Do nothing**, or **Hypershift**. Tick
   **Turbo** to make it repeat while held.

To bind a combo instead of a single key, tick the modifiers under **Only
while holding** (or hold them while you press **Detect**): Ctrl + Shift + S
can play a macro or send F5 while S alone keeps its own binding. The
modifiers have to match exactly, either side counts, and remapped ones
count too (Caps Lock bound to Ctrl works as Ctrl). They're lifted while the
combo's output goes out, so F5 isn't sent as Ctrl + Shift + F5, and go back
down if you're still holding them. Combos are listed under the picture, not
drawn on it; a hotkey with the same keys wins.

Bound keys are highlighted, and keys light up while you hold them. Bindings
are saved and applied immediately. **Ctrl+E** turns all remapping off and
on; when it's off, your devices behave exactly as if the app weren't running.

### Hypershift

Hypershift gives every device a second layer of bindings that is used while
you hold a Hypershift key.

1. In the **Normal** layer, bind a key or button to **Hypershift**, for
   example Caps Lock or a mouse side button.
2. Switch the **Layer** to **Hypershift** and bind keys there as usual. Keys
   you don't bind in this layer keep their Normal binding; they're shown
   dimmed.

A Hypershift key works for all devices: holding a mouse button switches the
keyboard to its Hypershift layer too. Each device has its own Hypershift
layer in each profile. The Hypershift key itself sends nothing. A key you
press while holding it stays bound to its Hypershift action until you
release that key, even if you let go of Hypershift first.

### Macros

Choose **Play a macro** in a binding and click **Edit macro…**. Press
**Record**, then type or click on your Razer devices; every press and
release is added, with the pauses between them unless you untick **Record
pauses**. Click **Stop recording** when you're done (that click isn't
recorded). You can also add keys and pauses by hand, edit a step by
double-clicking it, and reorder or remove steps.

Pick how it plays:

- **Play once:** the whole macro runs once per press, even if you let go
  early.
- **Repeat while held:** it starts over until you let go, and stops at once
  when you do.
- **Press to start, press again to stop:** it repeats until you press the
  key again.

When a macro stops, any key it was holding is let go. A repeating macro
without pauses waits 10 ms between rounds. Macros can't type text (only keys
and buttons) and can't be combined with turbo or tap vs hold.

### Running commands

Choose **Run a command** in a binding and type a command, as you would in a
terminal (`steam`, `playerctl play-pause`, `~/bin/toggle-vpn.sh`), or pick a
program or script with **Browse…**. Each press runs it once, in the
background, as your user; holding the key doesn't run it again, and its
output is discarded. Commands work on combos and in the Hypershift layer, and
the background service runs them too.

When you import a profile that runs commands, Razer Rebind lists them and
asks before importing: a shared profile could otherwise run anything on your
computer.

### Tap vs hold

Tick **Do something else when held** in a binding to give the input a second
job. A tap sends what's chosen above it; holding sends the hold action, for
as long as you hold it. The hold action can be keys (just a modifier such as
Ctrl works) or Hypershift.

It counts as held once you've held it for the hold time (200 ms unless you
change it), or as soon as you press and release another key while holding
it, so a quick Caps Lock + C gives Ctrl + C even inside the hold time. If you
let go of it before the other key, as happens when typing fast, it's a tap.
Until it's decided, other keys on the same device wait a moment so they
come out in the right order; keys on other devices see the hold only after
the hold time.

### Turbo

Tick **Turbo** in a binding to repeat it for as long as you hold the input,
at 1 to 50 presses per second. Each repeat holds the key down for half the
interval, so games that check key state once per frame see every press. To
make a key repeat itself, bind it to itself with turbo on. Turbo works in
the Hypershift layer too. The scroll wheel can't use turbo or Hypershift,
since a notch can't be held.

### Profiles

Each device has its own profiles and its own active profile, so you can
switch your keyboard to "Gaming" while the mouse stays on "Desktop". Use the
**Profile** box next to the device's name to switch, and **Manage** to
create, duplicate, rename or delete that device's profiles. Edits always
apply to the device's active profile.

**Manage → Export this profile…** saves the profile to a file, to back it up
or share it; **Import profile…** adds one to the selected device as a new
profile and switches to it. A file holds the profile's bindings, Hypershift
layer, DPI and linked programs (programs another profile already uses are
left out on import).

Give profiles on different devices the same name to switch them together:
the **Profiles** menu (and the tray menu) lists every device's profiles, plus
an **All devices** section that switches every device that has a profile of
that name.

### DPI and polling rate

When a mouse is selected, the bar above its bindings sets its DPI stages and
polling rate. The DPI button under the mouse cycles through the stages; the
round button in front of a stage picks it now. Changes apply right away.

Each profile keeps its own DPI and polling rate, so switching profiles, by
hand, by hotkey or when a game starts, changes them too. The first time you
change them, your other profiles keep the mouse's current settings. With
openrazer-daemon running, settings go through it, so they stick when the
mouse reconnects; without it, Razer Rebind sets them again itself.

### Switching profiles automatically

A profile can switch on by itself while a game (or any program) runs:

1. Start the game.
2. In Razer Rebind, pick the device and the profile, then click
   **Programs…** next to the profile.
3. Click **Pick running program…** and choose the game. The list shows the
   most recently started programs first. Games running through Wine or
   Proton are listed by their `.exe` name, e.g. `eldenring.exe`.

From then on, the device switches to that profile whenever the game starts,
and back to whatever profile it had before when the game exits. Link the
same game on the keyboard and the mouse to switch both. If several linked
games run at once, the one started last wins. If you switch profile by hand
while a game runs, your choice stays, also after the game exits. Programs are
checked every two seconds; **Settings** can turn automatic switching off.

### Hotkeys

Open **Hotkeys…** to set up key combinations that work in every profile and
in any application, including full-screen games. A hotkey can:

- switch to a specific profile,
- go to the next or previous profile,
- turn remapping on or off.

Each hotkey applies to one device or to all devices. For example:

| Hotkey | Device | Action |
|---|---|---|
| Ctrl + Alt + 1 | All devices | Switch to "Desktop" |
| Ctrl + Alt + 2 | All devices | Switch to "Gaming" |
| Ctrl + Alt + 3 | Viper Ultimate | Switch to "FPS" |
| Ctrl + mouse side button | Huntsman Elite | Next profile |

With **All devices**, every device that has a profile of that name switches
to it, and next/previous moves each device through its own profiles. A
notification shows what you switched to; you can turn that off in
**Settings**.

Good to know:

- The key or button that triggers a hotkey must be on a Razer device. The
  modifiers can be on any Razer device, so Ctrl on the keyboard + a mouse
  button works.
- Applications don't see the hotkey, except while remapping is turned off.
  Hotkeys stay active then too, so you can always turn remapping back on.
- Avoid Super in hotkeys: KDE and GNOME open the launcher when they see Super
  pressed and released on its own.

### Low battery alerts

Razer Rebind checks the battery of wireless devices, such as the Viper
Ultimate, once a minute. When one drops to the level you set (20% by
default), you get a desktop notification, and another reminder every 5% after
that until you plug it in. The current level is shown in the device list and
the tray tooltip, and as a gauge on the tray icon: green, amber under 40%,
red at your alert level, with a bolt while charging. With several wireless
devices, the gauge shows the emptiest one.

Change the level, turn the alerts or the tray gauge off, or send a test
notification in **Settings**. Alerts work in the background service too, so you don't need
another battery app.

### Running in the background

Closing the window keeps Razer Rebind running in the system tray. To have
your bindings active from login:

- **With a tray icon:** add Razer Rebind to your desktop's autostart and
  give it the `--minimized` option.
- **Without a window:** enable the background service:

  ```bash
  systemctl --user enable --now razer-rebind.service
  ```

  Only one copy of the app can run at a time, so stop the service before
  opening the window to change bindings, then start it again.

### Command line

The running app (window or background service) can be controlled from a
terminal, a script, or a shortcut in your desktop's keyboard settings. That's
useful for switching profiles from a non-Razer keyboard:

```bash
razer-rebind --profile Gaming
```

| Option | Effect |
|---|---|
| `--profile <name>` | Switch every device that has this profile to it |
| `--next-profile`, `--previous-profile` | Move each device to its next or previous profile |
| `--device <name>` | Apply the options above to one device only; any unique part of its name works, e.g. `viper` |
| `--toggle` | Turn remapping on or off |
| `--list-profiles` | List each device's profiles; `*` marks the active one |

For example, to switch only the mouse:

```bash
razer-rebind --device viper --profile FPS
```

## Performance

Razer Rebind sits between your device and the desktop, so every event it
remaps passes through it. That path is short, and the app's input threads
run with real-time priority so a busy game can't delay them.

Delay added by the app, measured on a Viper Ultimate at 1000 events per
second, on a 16-thread CPU:

| Situation | Typical | 99% of events under | Worst seen |
|---|---|---|---|
| Idle desktop | 0.013 ms | 0.026 ms | 0.15 ms |
| All CPU cores at 100% | 0.014 ms | 0.022 ms | 0.027 ms |

For comparison, a 1000 Hz mouse reports every 1 ms, and a frame at 144 fps
lasts 6.9 ms.

A device with no bindings, and no hotkeys it could trigger, isn't touched at
all: its input goes straight to the desktop as usual. A device with bindings
appears to the desktop as a virtual device with the same name and IDs, so
per-device settings such as KDE's pointer acceleration profile and speed
still apply.

## Permissions and privacy

To remap a device, the app must read its input and create a virtual input
device. The included udev rule allows exactly that for the user sitting at
the computer: it covers Razer input devices (USB vendor `1532`) and
`/dev/uinput`, using the same mechanism that gives you access to sound cards
and game controllers. Other users and remote logins get nothing.

This means any program running as you can read your Razer keyboard's
keystrokes, which is the trade-off for remapping without root. Don't run
Razer Rebind with `sudo` instead.

Razer Rebind makes no network connections. Your settings stay in
`~/.config/razer-rebind/bindings.json`.

## Limitations

- **Fn key (Huntsman Elite):** handled inside the OpenRazer driver and never
  reaches applications, so it can't be rebound. Fn combinations can be,
  using whatever key the driver turns them into.
- **DPI button (Viper Ultimate):** handled by the mouse's firmware, so it
  can't be rebound. It cycles through the DPI stages you set.
- **Viper Ultimate side buttons:** with the factory settings, the right-hand
  buttons send the same signals as the left-hand ones, so each pair is
  rebound together. **Detect** shows what your mouse actually sends.
- **Automatic switching follows running programs, not the focused window:**
  alt-tabbing out of a game keeps its profile until you quit the game.
- **One remapper per device:** don't use another input remapper
  (input-remapper, keyd, kmonad) on the same device; only one program can
  take it over. Razer Rebind shows an error if it can't.

## Configuration file

Settings are saved to `~/.config/razer-rebind/bindings.json` whenever you
change something. The file can also be edited by hand while the app isn't
running. Keys use Linux input names such as `KEY_CAPSLOCK` and `BTN_SIDE`, with
modifiers in front for combos (`Ctrl+Shift+KEY_S`);
`WHEEL_UP` and `WHEEL_DOWN` are the scroll wheel, and an empty list disables
an input. `["HYPERSHIFT"]` makes a key a Hypershift key, a profile's
`hypershift` object holds its Hypershift layer, and a binding with turbo is
written as `{ "keys": [...], "turbo": 15 }`; tap vs hold adds `"hold": [...]`
and optionally `"holdTime"` in ms, and a macro is `"macro": [{"press":
"KEY_A"}, {"delay": 50}, {"release": "KEY_A"}]` with an optional
`"macroMode"` of `"while-held"` or `"toggle"`; `"command": "..."` runs a
shell command. A profile's `programs` lists the
programs that switch to it automatically, and `sensor` its DPI stages
(`activeStage` counts from 1) and polling rate.

```json
{
  "version": 3,
  "battery": { "alerts": true, "threshold": 20 },
  "devices": [
    {
      "name": "Razer Huntsman Elite",
      "enabled": true,
      "activeProfile": "Gaming",
      "profiles": [
        { "name": "Desktop", "bindings": { "KEY_CAPSLOCK": ["KEY_ESC"] } },
        {
          "name": "Gaming",
          "bindings": { "KEY_LEFTMETA": [], "KEY_CAPSLOCK": ["HYPERSHIFT"] },
          "hypershift": { "KEY_W": ["KEY_UP"], "KEY_F": { "keys": ["KEY_F"], "turbo": 15 } },
          "programs": ["cs2", "eldenring.exe"]
        }
      ]
    },
    {
      "name": "Razer Viper Ultimate",
      "enabled": true,
      "activeProfile": "Desktop",
      "profiles": [
        {
          "name": "Desktop",
          "bindings": { "BTN_SIDE": ["KEY_LEFTCTRL", "KEY_C"] },
          "sensor": { "dpiStages": [800, 1600, 3200], "activeStage": 1, "pollRate": 1000 }
        }
      ]
    }
  ],
  "hotkeys": [
    { "modifiers": ["Ctrl", "Alt"], "key": "KEY_2", "action": "switch-profile", "profile": "Gaming" },
    { "modifiers": ["Ctrl"], "key": "BTN_EXTRA", "action": "next-profile", "device": "Razer Huntsman Elite" }
  ]
}
```

Hotkey actions are `switch-profile`, `next-profile`, `previous-profile` and
`toggle-remapping`; leave out `device` to apply a hotkey to all devices.
Settings files from older versions are converted automatically: every device
gets each of your existing profiles.

## Contributing

Bug reports and pull requests are welcome. See
[CONTRIBUTING.md](CONTRIBUTING.md) for how to build the app, how it works
internally, and how releases are made.

## License

GPL-2.0-or-later, the same as OpenRazer. See [LICENSE](LICENSE).
