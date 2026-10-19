# Changelog

## 0.9.0 (unreleased)

### New features

- **Macros.** Choose **Play a macro** in a binding to play back a sequence
  of key and button presses with pauses. **Record** captures what you press
  on your Razer devices with its timing; steps can also be added, edited,
  reordered and removed by hand. A macro can play once per press, repeat
  while held, or start and stop with each press. Keys a macro holds are let
  go when it stops.

- **Combos as inputs.** A binding can be for a key combo such as
  Ctrl + Shift + S, separate from the key on its own. Tick the modifiers
  under **Only while holding**, or hold them while pressing **Detect**. The
  combo's modifiers are lifted while its output goes out, so it can send a
  clean F5 or play a macro, and go back down if still held.

- **Run a command.** A binding can run a program, script or shell command,
  once per press and in the background. Importing a profile that runs
  commands lists them and asks first.

### Changes

- Other Razer keyboards and mice no longer show the Huntsman Elite or
  Viper picture, which didn't match them. They get a grid of keycaps
  instead: their bound inputs plus every key or button pressed on them,
  each clickable to rebind it, lighting up while held.
- Counts such as "1 binding applied" read properly instead of
  "binding(s)".

## 0.8.0 (2026-10-17)

### New features

- **Tap vs hold.** A binding can do one thing when tapped and another when
  held: tap Caps Lock for Esc, hold it for Ctrl, or hold Space for
  Hypershift. Tick **Do something else when held** in the binding editor.
  Pressing and releasing another key while holding counts as held, so a
  quick Caps Lock + C gives Ctrl + C; letting go first, as in fast typing,
  stays a tap. The hold time is 200 ms by default and can be changed per
  binding.
- **Battery on the tray icon.** A gauge across the tray icon shows the
  emptiest wireless device's charge: green, amber under 40%, red at your low
  battery alert level, with a bolt while charging. It can be turned off in
  Settings.
- **Import and export profiles.** **Manage → Export this profile…** saves a
  profile (bindings, Hypershift layer, DPI and linked programs) to a file to
  back up or share; **Import profile…** adds one to the selected device.

## 0.7.0 (2026-10-17)

### New features

- **DPI and polling rate.** A bar above a mouse's bindings sets up to five
  DPI stages, which the DPI button under the mouse cycles through, and the
  polling rate. Changes apply right away.
- **Per-profile sensitivity.** Each profile keeps its own DPI stages and
  polling rate, so switching profiles (by hand, by hotkey or when a game
  starts) changes them too. The first time you change them, your other
  profiles keep the mouse's current settings.

### Changes

- Settings go through openrazer-daemon when it runs, so they stick when the
  mouse reconnects and stay in step with other OpenRazer apps.

## 0.6.0 (2026-10-17)

### New features

- **Per-game profiles.** Link programs to a profile with the **Programs…**
  button next to it, and the device switches to that profile while one of
  them runs, then back to the profile it had before when it exits. **Pick
  running program…** lists what's running, newest first, so you can choose
  the game instead of typing its name. Games running through
  Wine or Proton (Steam, Lutris, Heroic) are matched by their `.exe` name.
  Each device switches on its own, so the keyboard and mouse can each have a
  profile for the same game.

### Changes

- A profile picked by hand while a linked game runs is kept, also after the
  game exits.
- A new setting turns automatic switching off, and profile-switch
  notifications now also cover automatic switches.

## 0.5.0 (2026-10-17)

### New features

- **Hypershift.** Bind any key or button to **Hypershift**. While you hold
  it, every device switches to a second layer of bindings, like Razer
  Synapse's Hypershift. Holding a mouse side button can change what your
  keyboard keys do. Keys you don't bind in the Hypershift layer keep their
  normal binding. Each profile has its own Hypershift layer.
- **Turbo.** Any binding can repeat for as long as you hold it, at 1 to 50
  presses per second. Each press is held down for half the interval, so games
  that check key state once per frame see every press. To make a key repeat
  itself, bind it to itself with turbo on. Turbo works in the Hypershift
  layer too.

### Changes

- A **Layer: Normal / Hypershift** switch above the device picture chooses
  which layer you're editing. Bound keys are purple in the Hypershift layer
  and blue in the normal one, and keys that use their normal binding are
  shown dimmed.
- Bindings with turbo show their rate, e.g. "F · turbo 15/s".
- Automated tests of the remapper run during every build, with no Razer
  hardware needed.

### Upgrading

Your settings carry over unchanged. Bindings with turbo are saved in a new
form; if you go back to 0.4.x, those bindings turn into "Do nothing".

## 0.4.1 (2026-10-14)

- Packages for Debian 12 and 13, Ubuntu 24.04 and 26.04, and Fedora 43, a
  pre-compiled tarball, and an AppImage.
- App icon. `razer-rebind --print-udev-rule` installs the permission rule
  where no package did.
- The command socket moved to the private per-user runtime directory.

## 0.4.0 (2026-10-14)

- Profiles per device: switch the keyboard and mouse independently, or every
  device with a shared profile name. Hotkeys and `--device` can target one
  device.
- Low battery notifications for wireless devices at a level you choose.
- Settings window. Battery level shown in the device list and tray.

## 0.3.0 (2026-10-14)

- Real-time priority for input threads: remapped input stays under 0.03 ms
  of added delay even with every CPU core busy.
- Virtual devices use the physical device's name, so per-device desktop
  settings (KDE pointer acceleration and speed) keep applying.

## 0.2.0 (2026-10-14)

- Profiles, global hotkeys and command-line profile switching.
- Fixed notifications not showing after the first one expired.

## 0.1.0

- First release.
