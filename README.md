# Razer Rebind

A Qt 6 app for rebinding keys and buttons on Razer devices that use the
[OpenRazer](https://openrazer.github.io/) kernel drivers. It was built for the
**Razer Huntsman Elite** (`razerkbd`) and the **Razer Viper Ultimate**
(`razermouse`, wired and wireless), but any keyboard or mouse that OpenRazer
drives will show up.

- Click a key on the keyboard picture, or a button on the mouse picture, to
  rebind it. You can also use **Detect** and press the key on the device.
- Send any key, mouse button, or shortcut (Ctrl/Shift/Alt/Super + key), or
  disable the input. **Record shortcut** captures a combination as you type it.
- The scroll wheel can be rebound too, for example wheel down → volume down.
- A mouse button can send a keyboard shortcut, and a key can send a click.
- Keys and buttons light up live as you press them.
- **Profiles**: named sets of bindings for all your devices, such as "Default",
  "Gaming" and "Editing". Switch from the toolbar, the tray, a **hotkey**, or the
  command line.
- Runs in the tray; can also run headless as a systemd user service.

## How it works

The OpenRazer kernel modules handle lighting, DPI, battery and similar
settings, but they don't provide a key-remapping interface. Remapping is done
in userspace:

1. **Discovery through OpenRazer.** The app reads
   `/sys/bus/hid/drivers/razerkbd|razermouse/*`. The interface with a
   `device_type` attribute is the device. Its sibling HID interfaces on the
   same USB device give all of its `/dev/input/eventN` nodes. Media keys and
   the dial arrive on a different node than the letters, so every node is
   needed. Serial, firmware and battery level come from the same sysfs
   attributes.
2. **Exclusive grab.** When a device has bindings, the app takes all of its
   event nodes with `EVIOCGRAB`, so the desktop stops seeing the device
   directly. It waits until no key is held, so nothing gets stuck down.
3. **Re-emit through uinput.** Events go out through a virtual device named
   `Razer Rebind: <device>`, with the bindings applied. Caps/Num/Scroll Lock
   LED changes are passed back to the real keyboard.

A device with no bindings is never grabbed. The app only watches it so it can
show live key presses.

## Installing on Fedora / Nobara

Build the RPM in a clean Fedora container (needs `podman`):

```bash
./packaging/build-rpm.sh
```

Then install it:

```bash
sudo dnf install ./dist/razer-rebind-*.x86_64.rpm
```

The package installs the app, its menu entry, the udev permission rule
described below (applied right away, no replug needed), and the
`razer-rebind` systemd user service. Use `FEDORA=43 ./packaging/build-rpm.sh`
to build for another Fedora release.

## Building from source

Fedora / Nobara:

```bash
sudo dnf install cmake gcc-c++ qt6-qtbase-devel libevdev-devel
```

Debian / Ubuntu:

```bash
sudo apt install cmake g++ qt6-base-dev libevdev-dev pkg-config
```

Then:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
```

```bash
./build/razer-rebind
```

## Permissions

The RPM sets this up for you. When building from source, note that the app needs read access to the Razer event nodes and write access to
`/dev/uinput`. The bundled udev rule grants both to the user logged in at the
local seat, using `uaccess`, the same mechanism used for sound cards and game
controllers:

```bash
sudo install -m644 data/70-razer-rebind.rules /etc/udev/rules.d/ && sudo udevadm control --reload && sudo udevadm trigger
```

Anything you run as your user can then read your Razer keyboard's keystrokes.
That is the price of doing remapping without root. Don't run the app with
`sudo` instead: it would put its config in root's home directory and couldn't
show a tray icon.

## Running in the background

- **With a tray icon:** closing the window keeps it running in the tray. Use
  `razer-rebind --minimized` in your desktop's autostart.
- **Headless (systemd user service):** installed by the RPM, or by
  `sudo cmake --install build`:

  ```bash
  systemctl --user enable --now razer-rebind.service
  ```

  Only one instance runs at a time. Stop the service before editing bindings
  in the GUI, then restart it.

**Ctrl+E**, or the toolbar or tray toggle, turns all remapping off and on.
Turning it off hands the devices straight back to the system.

## Profiles and hotkeys

Each profile holds its own bindings for every device. The toolbar's
**Profile** box and the **Profiles** menu switch between them and create,
duplicate, rename and delete them. Everything you edit applies to the active
profile.

**Hotkeys** (toolbar → Hotkeys…) work in every profile and in any
application. A hotkey can switch to a named profile, go to the next or
previous profile, or turn remapping on/off:

| Example hotkey | Action |
|---|---|
| Ctrl + Alt + 1 | Switch to "Default" |
| Ctrl + Alt + 2 | Switch to "Gaming" |
| Ctrl + mouse forward button | Next profile |
| Ctrl + Alt + Pause | Turn remapping on/off |

- Hotkeys are handled by the remapper, not by the desktop, so they work the
  same on X11 and Wayland. The trigger key must be on a Razer device.
  Modifiers count from any Razer device, so Ctrl on the Huntsman + a Viper
  button works.
- A hotkey takes priority over a binding on the same key, and is swallowed so
  applications don't also see it. Hotkeys keep working while remapping is
  off, so you can always turn it back on, but in that state the key also
  reaches the application.
- Avoid Super in hotkeys. GNOME and KDE open the launcher when Super is
  released on its own, which is what they see when the rest of the hotkey is
  swallowed.
- A desktop notification shows which profile you switched to. You can turn
  this off in the Hotkeys dialog.

### Command line

These options control the already-running instance (GUI or `--headless`):

```bash
razer-rebind --profile Gaming
```

```bash
razer-rebind --next-profile
```

`--previous-profile`, `--toggle` and `--list-profiles` work the same way.
Bind them to shortcuts in your desktop's keyboard settings if you want a
trigger on a non-Razer keyboard or a script. Launching `razer-rebind` again
brings the existing window to the front.

## Config file

Everything is saved to `~/.config/razer-rebind/bindings.json` as soon as you
change it. Bindings are keyed by device name, so the Viper Ultimate's wired
and wireless modes share bindings. Key names are evdev names, so the file can
be edited by hand. Files from version 0.1 are migrated into a "Default"
profile automatically.

```json
{
  "version": 2,
  "remappingEnabled": true,
  "notifications": true,
  "activeProfile": "Gaming",
  "profiles": [
    {
      "name": "Default",
      "devices": {
        "Razer Huntsman Elite": {
          "enabled": true,
          "bindings": { "KEY_CAPSLOCK": ["KEY_ESC"] }
        }
      }
    },
    {
      "name": "Gaming",
      "devices": {
        "Razer Huntsman Elite": {
          "enabled": true,
          "bindings": { "KEY_LEFTMETA": [], "KEY_PAUSE": ["KEY_LEFTCTRL", "KEY_LEFTALT", "KEY_DELETE"] }
        },
        "Razer Viper Ultimate": {
          "enabled": true,
          "bindings": { "BTN_SIDE": ["KEY_LEFTCTRL", "KEY_C"], "WHEEL_DOWN": ["KEY_VOLUMEDOWN"] }
        }
      }
    }
  ],
  "hotkeys": [
    { "modifiers": ["Ctrl", "Alt"], "key": "KEY_1", "action": "switch-profile", "profile": "Default" },
    { "modifiers": ["Ctrl"], "key": "BTN_EXTRA", "action": "next-profile" }
  ]
}
```

An empty list disables the input. `WHEEL_UP` and `WHEEL_DOWN` are the scroll
wheel directions. Hotkey actions are `switch-profile`, `next-profile`,
`previous-profile` and `toggle-remapping`.

## Limitations

- **Fn (Huntsman Elite)** is handled inside the `razerkbd` driver and never
  reaches userspace, so it can't be rebound. Fn combinations reach the app as
  whatever key the driver turns them into, which can be bound.
- **DPI button (Viper Ultimate)** is handled by the mouse firmware.
- **Viper Ultimate side buttons:** with the factory onboard mapping, the
  right-hand pair sends the same codes as the left-hand pair (mouse 4/5), so
  each pair is rebound together. Use **Detect** to see what your mouse
  actually sends.
- Bindings are one-to-one: one input sends one key or shortcut. There are no
  layers or timed macros, and profiles don't switch automatically per
  application.
- Don't run another evdev remapper (input-remapper, keyd, kmonad) on the same
  device. Only one program can grab it, and the app shows an error if it
  can't.

## Source layout

| File | Purpose |
|---|---|
| `src/razerdevice.*` | Device discovery through the OpenRazer sysfs tree |
| `src/remapper.*` | Per-device worker thread: grab, remap, hotkeys, uinput output, LED forwarding |
| `src/remapmanager.*` | Owns the remappers, follows hotplug, applies the profile, runs hotkey actions, command socket, notifications |
| `src/config.*` | Profiles, hotkeys, JSON storage |
| `src/hotkeydialog.*` | Hotkey list and editor |
| `src/inputdetector.*` | "Press it on the device" detection used by both editors |
| `src/keys.*` | evdev key names and labels, and which codes are safe to emit |
| `src/deviceview.*` | Clickable Huntsman Elite keyboard and Viper Ultimate mouse pictures |
| `src/bindingdialog.*` | Binding editor (search, detect, record shortcut) |
| `src/mainwindow.*` | Main window and tray |

## License

GPL-2.0-or-later, the same as OpenRazer. See [LICENSE](LICENSE).
