# Razer Rebind

Rebind the keys and buttons of Razer keyboards and mice on Linux. Razer
Rebind works with the [OpenRazer](https://openrazer.github.io/) kernel
drivers, runs on X11 and Wayland, and needs no root once installed.

- **Rebind anything:** click a key on the keyboard picture or a button on the
  mouse picture, or press it on the device. Send another key, a mouse button,
  a shortcut such as Ctrl + C, or nothing at all. The scroll wheel can be
  rebound too, and a mouse button can type a shortcut.
- **Profiles per device:** keep separate sets of bindings, such as
  "Desktop", "Gaming" and "Editing", for each device. Switch the keyboard
  without touching the mouse, or switch everything at once.
- **Global hotkeys:** switch profiles or turn remapping off from inside a
  game, with a key combination on your Razer keyboard or mouse.
- **Low battery alerts:** get a desktop notification when a wireless
  device's battery drops below a level you choose.
- **Built for gaming:** adds about 0.015 ms of input delay, even with every
  CPU core busy (see [Performance](#performance)). Your KDE pointer settings
  keep working.
- **Stays out of the way:** runs in the system tray or as a background
  service. Devices with nothing to remap aren't touched at all.

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

### Fedora and Nobara

Download the latest `razer-rebind-*.x86_64.rpm` from
[Releases](../../releases/latest), then install it:

```bash
sudo dnf install ./razer-rebind-*.x86_64.rpm
```

The package sets up device permissions for you. Then start **Razer Rebind**
from your application menu.

### Other distributions

Build from source as described in [CONTRIBUTING.md](CONTRIBUTING.md#building),
then install the app and the permission rule (see
[Permissions](#permissions-and-privacy)):

```bash
sudo cmake --install build
```

```bash
sudo install -m644 data/70-razer-rebind.rules /etc/udev/rules.d/ && sudo udevadm control --reload && sudo udevadm trigger
```

## Usage

### Rebinding keys and buttons

1. Pick your device on the left.
2. Click a key or button on the picture. You can also click **Add binding…**
   and press **Detect** to press the key on the device itself.
3. Choose what it should do: another key, a shortcut (tick the modifiers or
   use **Record shortcut**), or **Do nothing**.

Bound keys are highlighted, and keys light up while you hold them. Bindings
are saved and applied immediately. **Ctrl+E** turns all remapping off and
on; when it's off, your devices behave exactly as if the app weren't running.

### Profiles

Each device has its own profiles and its own active profile, so you can
switch your keyboard to "Gaming" while the mouse stays on "Desktop". Use the
**Profile** box next to the device's name to switch, and **Manage** to
create, duplicate, rename or delete that device's profiles. Edits always
apply to the device's active profile.

Give profiles on different devices the same name to switch them together:
the **Profiles** menu (and the tray menu) lists every device's profiles, plus
an **All devices** section that switches every device that has a profile of
that name.

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
the tray tooltip.

Change the level, turn the alerts off, or send a test notification in
**Settings**. Alerts work in the background service too, so you don't need
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
- **DPI button (Viper Ultimate):** handled by the mouse's firmware.
- **Viper Ultimate side buttons:** with the factory settings, the right-hand
  buttons send the same signals as the left-hand ones, so each pair is
  rebound together. **Detect** shows what your mouse actually sends.
- **One-to-one bindings:** one input sends one key or shortcut. There are no
  layers or timed macros, and profiles don't switch automatically when you
  change applications.
- **One remapper per device:** don't use another input remapper
  (input-remapper, keyd, kmonad) on the same device; only one program can
  take it over. Razer Rebind shows an error if it can't.

## Configuration file

Settings are saved to `~/.config/razer-rebind/bindings.json` whenever you
change something. The file can also be edited by hand while the app isn't
running. Keys use Linux input names such as `KEY_CAPSLOCK` and `BTN_SIDE`;
`WHEEL_UP` and `WHEEL_DOWN` are the scroll wheel, and an empty list disables
an input.

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
        { "name": "Gaming", "bindings": { "KEY_LEFTMETA": [] } }
      ]
    },
    {
      "name": "Razer Viper Ultimate",
      "enabled": true,
      "activeProfile": "Desktop",
      "profiles": [
        { "name": "Desktop", "bindings": { "BTN_SIDE": ["KEY_LEFTCTRL", "KEY_C"] } }
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
