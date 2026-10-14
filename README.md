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

## Building

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

The app needs read access to the Razer event nodes and write access to
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
- **Headless (systemd user service):** after `sudo cmake --install build`:

  ```bash
  systemctl --user enable --now razer-rebind.service
  ```

  Only one instance runs at a time. Stop the service before editing bindings
  in the GUI, then restart it.

**Ctrl+E**, or the toolbar or tray toggle, turns all remapping off and on.
Turning it off hands the devices straight back to the system.

## Config file

Bindings are saved to `~/.config/razer-rebind/bindings.json` as soon as you
change them. They are keyed by device name, so the Viper Ultimate's wired and
wireless modes share bindings. Key names are evdev names, so the file can be
edited by hand:

```json
{
  "version": 1,
  "remappingEnabled": true,
  "devices": {
    "Razer Huntsman Elite": {
      "enabled": true,
      "bindings": {
        "KEY_CAPSLOCK": ["KEY_ESC"],
        "KEY_INSERT": [],
        "KEY_PAUSE": ["KEY_LEFTCTRL", "KEY_LEFTALT", "KEY_DELETE"]
      }
    },
    "Razer Viper Ultimate": {
      "enabled": true,
      "bindings": {
        "BTN_SIDE": ["KEY_LEFTCTRL", "KEY_C"],
        "WHEEL_DOWN": ["KEY_VOLUMEDOWN"]
      }
    }
  }
}
```

An empty list disables the input. `WHEEL_UP` and `WHEEL_DOWN` are the scroll
wheel directions.

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
  layers, timed macros, or per-application profiles.
- Don't run another evdev remapper (input-remapper, keyd, kmonad) on the same
  device. Only one program can grab it, and the app shows an error if it
  can't.

## Source layout

| File | Purpose |
|---|---|
| `src/razerdevice.*` | Device discovery through the OpenRazer sysfs tree |
| `src/remapper.*` | Per-device worker thread: grab, remap, uinput output, LED forwarding |
| `src/remapmanager.*` | Owns the remappers, follows hotplug via `/dev/input`, applies the profile |
| `src/profile.*` | JSON bindings storage |
| `src/keys.*` | evdev key names and labels, and which codes are safe to emit |
| `src/deviceview.*` | Clickable Huntsman Elite keyboard and Viper Ultimate mouse pictures |
| `src/bindingdialog.*` | Binding editor (search, detect, record shortcut) |
| `src/mainwindow.*` | Main window and tray |
