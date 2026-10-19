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

DPI and polling rate go through openrazer-daemon's D-Bus API
(`razer.device.dpi`, `razer.device.misc`) when it runs, so the daemon's own
saved state stays in step; otherwise the `dpi_stages`, `dpi` and `poll_rate`
sysfs attributes are written directly. They're written only when the active
profile's values differ from the last ones written to that mouse.

Lighting works the same way through the daemon's `razer.device.lighting.*`
interfaces (the `logo` one on mice), falling back to the `matrix_effect_*`
attributes. Only the daemon can say which effect is on, so without it the
current lighting isn't shown until one is chosen. Ripple is animated by the
daemon and needs it.

Automatic profile switching reads `/proc` every two seconds for the user's
processes. A process is known by argv[0] (which holds the Windows path for
Wine and Proton games), its executable and its kernel name. Before switching,
the device's previous profile is saved to the config as `autoRestore`, so a
restart in the middle of a game still switches back afterwards.

Command bindings are signalled from the worker thread and started by the
RemapManager with `QProcess::startDetached()` through `/bin/sh -c`, with
standard input and output on `/dev/null`.

Macros play in the worker thread on the same `ppoll()` timeouts as turbo:
each playing macro has a step index and the time its next step is due, and
remembers which keys it holds so stopping it releases them.

A tap-or-hold key sends nothing when pressed. Key events from the same device
queue up behind it until its release (a tap), the hold time passing, or a
queued key's release (both holds); then the queue is replayed through the
normal path, where another tap-or-hold key can start its own wait.

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
| `src/sensor.*` | Mouse DPI stages and polling rate, through openrazer-daemon or sysfs |
| `src/sensorpanel.*` | The DPI / polling rate bar in the main window |
| `src/lighting.*` | Lighting effects and brightness, through openrazer-daemon or sysfs |
| `src/lightingpanel.*` | The lighting bar in the main window |
| `src/processes.*` | The user's running programs, read from `/proc` |
| `src/autoswitcher.*` | Switches profiles while their linked programs run |
| `src/programsdialog.*` | Editor for a profile's programs, with a running-program picker |
| `src/batterymonitor.*` | Battery polling through OpenRazer and low battery alerts |
| `src/notifications.*` | Desktop notifications over `org.freedesktop.Notifications` |
| `src/settingsdialog.*` | Notification and battery settings |
| `src/keys.*` | Key names and labels, and which codes are safe to emit |
| `src/deviceview.*` | Clickable Huntsman Elite and Viper pictures, and the generic keycap grid for other devices |
| `src/macrodialog.*` | Macro recorder and step editor |
| `src/bindingdialog.*` | Binding editor (search, detect, record shortcut) |
| `src/hotkeydialog.*` | Hotkey list and editor |
| `src/inputdetector.*` | "Press it on the device" detection used by both editors |
| `src/mainwindow.*` | Main window, profiles UI and tray |
| `tests/` | Remapper test with stubbed evdev and uinput; auto-switch test with stand-in processes |
| `data/` | udev rule, desktop entry, icon, systemd user service |
| `packaging/` | RPM spec, Debian maintainer scripts, tarball installer, and the build scripts |

## Making a release

1. Bump the version in `CMakeLists.txt` (`project(... VERSION x.y.z)`) and in
   `packaging/razer-rebind.spec` (`Version:`), and add a `%changelog` entry
   to the spec.
2. Add a `## x.y.z (date)` section to `CHANGELOG.md`, written for users. It
   becomes the text of the GitHub release, followed by a download guide.
   Preview it with `./packaging/release-notes.sh x.y.z`.
3. Commit, then tag and push the tag:

   ```bash
   git tag vX.Y.Z && git push origin vX.Y.Z
   ```

The Packages workflow builds the RPMs, .debs, tarball and AppImage, and publishes a
GitHub Release with all of them and a `SHA256SUMS` file attached. It refuses
to run if the spec, `CMakeLists.txt` and tag versions disagree, or if
`CHANGELOG.md` has no entry for the version. Every push and
pull request also builds all packages; they can be downloaded from the
workflow run's **Artifacts**.
