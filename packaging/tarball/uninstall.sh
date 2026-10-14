#!/bin/sh
# Remove what install.sh installed. Your settings in ~/.config/razer-rebind stay.
set -eu

if [ "$(id -u)" -ne 0 ]; then
    echo "Please run with sudo: sudo ./uninstall.sh" >&2
    exit 1
fi

rm -f /usr/local/bin/razer-rebind \
    /usr/local/share/applications/razer-rebind.desktop \
    /usr/local/lib/systemd/user/razer-rebind.service \
    /etc/udev/rules.d/70-razer-rebind.rules
udevadm control --reload-rules 2>/dev/null || true
echo "Razer Rebind removed. Your settings in ~/.config/razer-rebind were kept."
