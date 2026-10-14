#pragma once

#include "keys.h"

#include <QMap>
#include <QString>

struct DeviceProfile
{
    bool enabled = true;
    BindingMap bindings;
};

// Bindings for every device, stored as JSON in
// ~/.config/razer-rebind/bindings.json keyed by device name, using evdev key
// names so the file is readable and hand-editable.
class Profile
{
public:
    static QString defaultPath();

    bool load(const QString &path, QString *error);
    bool save(QString *error) const;
    QString path() const { return m_path; }

    bool remappingEnabled = true;

    DeviceProfile device(const QString &key) const { return m_devices.value(key); }
    void setDevice(const QString &key, const DeviceProfile &profile) { m_devices.insert(key, profile); }

private:
    QString m_path;
    QMap<QString, DeviceProfile> m_devices;
};
