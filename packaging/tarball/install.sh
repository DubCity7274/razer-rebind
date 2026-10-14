#!/bin/sh
# Install Razer Rebind from this folder: the app goes to /usr/local, the
# device permission rule to /etc/udev/rules.d. Run with sudo.
set -eu

if [ "$(id -u)" -ne 0 ]; then
    echo "Please run with sudo: sudo ./install.sh" >&2
    exit 1
fi
cd "$(dirname "$0")"

install -Dm755 razer-rebind /usr/local/bin/razer-rebind
install -Dm644 razer-rebind.desktop /usr/local/share/applications/razer-rebind.desktop
install -Dm644 razer-rebind.service /usr/local/lib/systemd/user/razer-rebind.service
install -Dm644 70-razer-rebind.rules /etc/udev/rules.d/70-razer-rebind.rules

# Apply the permission rule to devices that are already plugged in.
udevadm control --reload-rules 2>/dev/null || true
udevadm trigger --subsystem-match=input --subsystem-match=misc --action=change 2>/dev/null || true

echo "Installed Razer Rebind to /usr/local."

missing=$(ldd /usr/local/bin/razer-rebind 2>/dev/null | awk '/not found/ { print "  " $1 }')
if [ -n "$missing" ]; then
    echo
    echo "Some libraries it needs aren't installed:"
    echo "$missing"
    echo "Install your distribution's Qt 6 base, Qt 6 Wayland and libevdev packages,"
    echo "for example on Arch: sudo pacman -S qt6-base qt6-wayland libevdev"
fi
