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

Run the tests (no Razer hardware needed):

```bash
ctest --test-dir build --output-on-failure
```

Running it needs the udev rule from `data/`. udev only reads rules from
`/etc` and `/usr`, so install it explicitly (the RPM does this for you):

```bash
sudo install -m644 data/70-razer-rebind.rules /etc/udev/rules.d/ && sudo udevadm control --reload && sudo udevadm trigger
```

### Building the packages

Each script builds from the committed `HEAD` in a clean container with podman
and puts the result in `dist/`:

```bash
./packaging/build-rpm.sh
```

```bash
./packaging/build-deb.sh ubuntu:24.04
```

```bash
./packaging/build-tarball.sh
```

```bash
./packaging/build-appimage.sh
```

`FEDORA=43 ./packaging/build-rpm.sh` targets another Fedora release, and
`build-deb.sh` takes any of `debian:12`, `debian:13`, `ubuntu:24.04` or
`ubuntu:26.04`. The tarball and AppImage are built on Debian 12, the oldest supported
base, so they run on anything newer. The AppImage uses
[linuxdeploy](https://github.com/linuxdeploy/linuxdeploy) and its Qt plugin at
pinned releases, checked against SHA-256 checksums in the script. CI calls the same scripts with `--direct`, which
builds on the current machine.

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

Hypershift and turbo also live in the worker threads. A shared counter of
held Hypershift keys makes every device look up its Hypershift layer first.
Turbo keys are timed with `ppoll()` timeouts and pulse their outputs at the
chosen rate, each press held for half the interval.

Hotkeys are matched inside the worker threads. Modifier state is shared
between devices, so Ctrl on the keyboard combines with a mouse button. A
local socket in the user's runtime directory lets
`razer-rebind --profile ...` control the running instance. Desktop notifications go through `org.freedesktop.Notifications`.

Battery levels come from OpenRazer's `charge_level` and `charge_status`
attributes. Each read is a USB round trip to the wireless device, so they
are polled once a minute on a background thread.

## Project layout

| Path | Purpose |
|---|---|
| `src/razerdevice.*` | Device discovery through the OpenRazer sysfs tree |
| `src/remapper.*` | Per-device worker thread: grab, remap, Hypershift, turbo, hotkeys, uinput output, LED forwarding |
| `src/remapmanager.*` | Owns the remappers, follows hotplug, applies the active profile, runs hotkey actions, command socket, notifications |
| `src/config.*` | Per-device profiles, hotkeys, settings; JSON storage and migration |
| `src/batterymonitor.*` | Battery polling through OpenRazer and low battery alerts |
| `src/notifications.*` | Desktop notifications over `org.freedesktop.Notifications` |
| `src/settingsdialog.*` | Notification and battery settings |
| `src/keys.*` | Key names and labels, and which codes are safe to emit |
| `src/deviceview.*` | Clickable Huntsman Elite and Viper Ultimate pictures |
| `src/bindingdialog.*` | Binding editor (search, detect, record shortcut) |
| `src/hotkeydialog.*` | Hotkey list and editor |
| `src/inputdetector.*` | "Press it on the device" detection used by both editors |
| `src/mainwindow.*` | Main window, profiles UI and tray |
| `tests/` | Remapper test with stubbed evdev and uinput |
| `data/` | udev rule, desktop entry, icon, systemd user service |
| `packaging/` | RPM spec, Debian maintainer scripts, tarball installer, and the build scripts |

## Making a release

1. Bump the version in `CMakeLists.txt` (`project(... VERSION x.y.z)`) and in
   `packaging/razer-rebind.spec` (`Version:`), and add a `%changelog` entry
   to the spec.
2. Commit, then tag and push the tag:

   ```bash
   git tag vX.Y.Z && git push origin vX.Y.Z
   ```

The Packages workflow builds the RPMs, .debs, tarball and AppImage, and publishes a
GitHub Release with all of them and a `SHA256SUMS` file attached. It refuses
to run if the spec, `CMakeLists.txt` and tag versions disagree. Every push and
pull request also builds all packages; they can be downloaded from the
workflow run's **Artifacts**.
