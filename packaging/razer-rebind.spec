Name:           razer-rebind
Version:        0.4.1
Release:        1%{?dist}
Summary:        Key and button rebinding for Razer keyboards and mice (OpenRazer)

License:        GPL-2.0-or-later
URL:            https://github.com/DubCity7274/razer-rebind
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake
BuildRequires:  gcc-c++
BuildRequires:  cmake(Qt6Widgets)
BuildRequires:  cmake(Qt6Network)
BuildRequires:  cmake(Qt6DBus)
BuildRequires:  pkgconfig(libevdev)
BuildRequires:  desktop-file-utils
BuildRequires:  systemd-rpm-macros

%description
Razer Rebind is a Qt 6 app for rebinding keys and buttons on Razer devices
driven by the OpenRazer kernel drivers (razerkbd, razermouse), such as the
Huntsman Elite and Viper Ultimate. Keys, mouse buttons and the scroll wheel
can send any key, shortcut or click, or be disabled. Bindings are grouped in
profiles that can be switched with global hotkeys, from the tray or from the
command line.

The package installs a udev rule that gives the user at the local seat
access to Razer input devices and /dev/uinput, which the remapper needs.

%prep
%autosetup

%build
%cmake -DCMAKE_BUILD_TYPE=Release
%cmake_build

%install
%cmake_install

%check
desktop-file-validate %{buildroot}%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/scalable/apps/%{name}.svg

%post
%udev_rules_update
# Apply the new permissions to devices that are already plugged in.
udevadm trigger --subsystem-match=input --subsystem-match=misc --action=change >/dev/null 2>&1 || :
%systemd_user_post %{name}.service

%preun
%systemd_user_preun %{name}.service

%postun
%udev_rules_update

%files
%license LICENSE
%doc README.md
%{_bindir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_userunitdir}/%{name}.service
%{_udevrulesdir}/70-%{name}.rules

%changelog
* Wed Oct 14 2026 dub <111620773+DubCity7274@users.noreply.github.com> - 0.4.1-1
- Packages for Debian 12/13, Ubuntu 24.04/26.04 and Fedora 43, a
  pre-compiled tarball, and an AppImage
- App icon; --print-udev-rule installs the permission rule without a package
- Command socket moved to the private per-user runtime directory

* Wed Oct 14 2026 dub <111620773+DubCity7274@users.noreply.github.com> - 0.4.0-1
- Profiles per device: switch the keyboard and mouse independently, or all
  devices with a shared profile name; hotkeys and --device target one device
- Low battery notifications for wireless devices at a configurable level
- Settings window; battery level shown in the device list and tray

* Wed Oct 14 2026 dub <111620773+DubCity7274@users.noreply.github.com> - 0.3.0-1
- Real-time priority for input threads: remapped input stays under 0.03 ms
  of added delay even with every CPU core busy
- Virtual devices use the physical device's name, so per-device desktop
  settings (KDE pointer acceleration, speed) keep applying

* Wed Oct 14 2026 dub <111620773+DubCity7274@users.noreply.github.com> - 0.2.0-1
- Profiles, global hotkeys and command-line profile switching
- Fix notifications not showing after the first one expired

* Wed Oct 14 2026 dub <111620773+DubCity7274@users.noreply.github.com> - 0.1.0-1
- Initial release
