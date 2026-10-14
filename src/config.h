#pragma once

#include "keys.h"

#include <QHash>
#include <QMap>
#include <QString>
#include <QStringList>

struct DeviceProfile
{
    bool enabled = true;
    BindingMap bindings;
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

    QString triggerLabel() const { return Keys::shortcutLabel(modifiers, code); }
    QString actionLabel() const;
};

// Everything the user configures, stored as JSON in
// ~/.config/razer-rebind/bindings.json. A profile is a named set of bindings
// for every device; exactly one profile is active. Hotkeys are shared by all
// profiles so there is always a way to switch back.
class Config
{
public:
    static QString defaultPath();

    bool load(const QString &path, QString *error);
    bool save(QString *error) const;
    QString path() const { return m_path; }

    bool remappingEnabled = true;
    bool notifications = true;
    QList<Hotkey> hotkeys;

    QStringList profileNames() const { return m_order; }
    bool hasProfile(const QString &name) const { return m_profiles.contains(name); }
    QString activeProfile() const { return m_active; }
    bool setActiveProfile(const QString &name);
    QString adjacentProfile(int step) const; // wraps around

    bool addProfile(const QString &name, const QString &copyFrom = {});
    bool renameProfile(const QString &from, const QString &to);
    bool removeProfile(const QString &name);

    // Bindings of the active profile.
    DeviceProfile device(const QString &key) const { return m_profiles.value(m_active).value(key); }
    void setDevice(const QString &key, const DeviceProfile &profile) { m_profiles[m_active].insert(key, profile); }

private:
    void ensureProfile();

    QString m_path;
    QStringList m_order;
    QString m_active;
    QHash<QString, QMap<QString, DeviceProfile>> m_profiles;
};
