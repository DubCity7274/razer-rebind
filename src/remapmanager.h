#pragma once

#include "razerdevice.h"

#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QTimer>

class Profile;
class Remapper;

// Owns one Remapper per connected device, follows hotplug, and pushes the
// profile's bindings to the remappers.
class RemapManager : public QObject
{
    Q_OBJECT

public:
    explicit RemapManager(Profile *profile, QObject *parent = nullptr);
    ~RemapManager() override;

    QList<RazerDevice> devices() const { return m_devices; }
    Remapper *remapper(const QString &hidPath) const { return m_remappers.value(hidPath); }
    QString errorFor(const QString &hidPath) const { return m_errors.value(hidPath); }

    bool remappingEnabled() const;
    void setRemappingEnabled(bool enabled);

    // Call after changing the profile.
    void applyProfile();

public slots:
    void rescan();

signals:
    void devicesChanged();
    void stateChanged();
    void inputEvent(const QString &profileKey, int code, bool pressed);
    void message(const QString &text);

private:
    void addRemapper(const RazerDevice &dev);

    Profile *m_profile;
    QList<RazerDevice> m_devices;
    QHash<QString, Remapper *> m_remappers; // by hidPath
    QHash<QString, QString> m_errors;       // by hidPath
    QFileSystemWatcher m_watcher;
    QTimer m_rescanTimer;
    int m_retries = 0;
};
