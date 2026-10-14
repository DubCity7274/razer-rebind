# Contributing to Razer Rebind

## Building

Install the dependencies.

Fedora:

```bash
sudo dnf install cmake gcc-c++ qt6-qtbase-devel libevdev-devel
```

Debian / Ubuntu:

```bash
sudo apt install cmake g++ qt6-base-dev libevdev-dev pkg-config
```

Then build and run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
```

```bash
./build/razer-rebind
```

Running it needs the udev rule from `data/`. udev only reads rules from
`/etc` and `/usr`, so install it explicitly (the RPM does this for you):

```bash
sudo install -m644 data/70-razer-rebind.rules /etc/udev/rules.d/ && sudo udevadm control --reload && sudo udevadm trigger
```

### Building the RPM

`packaging/build-rpm.sh` builds the RPM from the committed `HEAD` in a clean
Fedora container with podman and puts the packages in `dist/`:

```bash
./packaging/build-rpm.sh
```

Set `FEDORA=43` (for example) to target another release. CI uses
`./packaging/build-rpm.sh --direct`, which builds on the current machine.

## How it works

1. **Discovery through OpenRazer.** The app reads
   `/sys/bus/hid/drivers/razerkbd|razermouse/*`. The HID interface with a
   `device_type` attribute is the device; its sibling interfaces on the same
   USB device provide all of its `/dev/input/eventN` nodes. Keys are spread
   over several nodes (media keys and the dial arrive on a different one than
   letters), so every node is used. Serial, firmware and battery level come
   from the same sysfs attributes.
2. **Exclusive grab.** When a device has bindings, or a hotkey it can
   trigger, a per-device worker thread takes all its event nodes with
   `EVIOCGRAB`, so the desktop stops reading them directly. It waits until
   no key is held, so nothing gets stuck down.
3. **Re-emit through uinput.** Events go out through a virtual device with
   the bindings applied. It uses the physical device's name and vendor and
   product IDs, so per-device desktop settings still match. Caps, Num and
   Scroll Lock LED changes are forwarded back to the real keyboard.
4. **Real-time priority.** The worker threads ask for real-time priority
   10: `SCHED_FIFO` when the user is allowed to set it, otherwise through
   RealtimeKit (which grants `SCHED_RR`), with `RLIMIT_RTTIME` as a safety
   net. Without it, remapped input can lag by a millisecond when every CPU
   core is busy.

Hotkeys are matched inside the worker threads. Modifier state is shared
between devices, so Ctrl on the keyboard combines with a mouse button. A
local socket lets `razer-rebind --profile ...` control the running
instance. Desktop notifications go through `org.freedesktop.Notifications`.

## Project layout

| Path | Purpose |
|---|---|
| `src/razerdevice.*` | Device discovery through the OpenRazer sysfs tree |
| `src/remapper.*` | Per-device worker thread: grab, remap, hotkeys, uinput output, LED forwarding |
| `src/remapmanager.*` | Owns the remappers, follows hotplug, applies the active profile, runs hotkey actions, command socket, notifications |
| `src/config.*` | Profiles, hotkeys, JSON storage and migration |
| `src/keys.*` | Key names and labels, and which codes are safe to emit |
| `src/deviceview.*` | Clickable Huntsman Elite and Viper Ultimate pictures |
| `src/bindingdialog.*` | Binding editor (search, detect, record shortcut) |
| `src/hotkeydialog.*` | Hotkey list and editor |
| `src/inputdetector.*` | "Press it on the device" detection used by both editors |
| `src/mainwindow.*` | Main window, profiles UI and tray |
| `data/` | udev rule, desktop entry, systemd user service |
| `packaging/` | RPM spec and build script |

## Making a release

1. Bump the version in `CMakeLists.txt` (`project(... VERSION x.y.z)`) and in
   `packaging/razer-rebind.spec` (`Version:`), and add a `%changelog` entry
   to the spec.
2. Commit, then tag and push the tag:

   ```bash
   git tag vX.Y.Z && git push origin vX.Y.Z
   ```

The RPM workflow builds the packages and publishes a GitHub Release with them
attached. It refuses to publish if the tag doesn't match the spec's
`Version:`. Every push and pull request also builds the RPM; it can be
downloaded from the workflow run's **Artifacts**.
