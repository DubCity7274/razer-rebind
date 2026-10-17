#pragma once

#include "keys.h"

#include <QHash>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

// What a remapper needs for one device: whether to remap it at all, and the
// bindings of its active profile.
struct DeviceProfile
{
    bool enabled = true;
    BindingMap bindings;
    // Used instead while any Hypershift key is held (on any device); keys
    // without a binding here fall back to `bindings`.
    BindingMap hypershift;

    bool hasHypershiftKey() const;
};

// A global shortcut handled by the remapper itself, so it works on any desktop
// (X11 or Wayland) without registering anything with the compositor.
struct Hotkey
{
    enum Action { SwitchProfile, NextProfile, PreviousProfile, ToggleRemapping };

    unsigned modifiers = 0; // Keys::Modifier bits
    int code = 0;           // key, mouse button or wheel pseudo code
    Action action = NextProfile;
    QString profile;        // for SwitchProfile
    QString device;         // device key the action applies to; empty = all devices

    QString triggerLabel() const { return Keys::shortcutLabel(modifiers, code); }
    QString actionLabel() const;
};

// "Razer Viper Ultimate" -> "Viper Ultimate", for compact labels.
QString shortDeviceName(const QString &deviceKey);

// Everything the user configures, stored as JSON in
// ~/.config/razer-rebind/bindings.json.
//
// Every device has its own named profiles and its own active profile, so the
// keyboard can switch without touching the mouse. Profiles with the same name
// on several devices can be switched together ("all devices"). Hotkeys are
// shared by all profiles so there is always a way to switch back.
class Config
{
public:
    static QString defaultPath();

    bool load(const QString &path, QString *error);
    bool save(QString *error) const;
    QString path() const { return m_path; }

    bool remappingEnabled = true;
    bool notifications = true;   // on hotkey / command-line profile switches
    bool batteryAlerts = true;
    int batteryThreshold = 20;   // percent
    QList<Hotkey> hotkeys;

    // Devices that have settings, in a stable order. ensureDevice() adds one
    // with a single "Default" profile; returns true if it was new.
    QStringList deviceKeys() const { return m_deviceOrder; }
    bool ensureDevice(const QString &device);

    // Bindings of the device's active profile.
    DeviceProfile device(const QString &device) const;
    void setDevice(const QString &device, const DeviceProfile &profile);

    QStringList profileNames(const QString &device) const;
    QString activeProfile(const QString &device) const;
    bool setActiveProfile(const QString &device, const QString &name);
    QString adjacentProfile(const QString &device, int step) const; // wraps around
    bool hasProfile(const QString &device, const QString &name) const;
    // Profile names across all devices, in first-seen order.
    QStringList allProfileNames() const;

    bool addProfile(const QString &device, const QString &name, const QString &copyFrom = {});
    bool renameProfile(const QString &device, const QString &from, const QString &to);
    bool removeProfile(const QString &device, const QString &name);

private:
    struct Profile
    {
        QString name;
        BindingMap bindings;
        BindingMap hypershift;
    };
    struct DeviceSettings
    {
        bool enabled = true;
        QString active;
        QList<Profile> profiles;
    };

    const Profile *findProfile(const QString &device, const QString &name) const;
    bool anyDeviceHasProfile(const QString &name) const;

    QString m_path;
    QStringList m_deviceOrder;
    QHash<QString, DeviceSettings> m_devices;
};
