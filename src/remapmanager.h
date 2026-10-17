#pragma once

#include "config.h"
#include "razerdevice.h"

#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QElapsedTimer>
#include <QTimer>

class QLocalServer;
class Remapper;

// Owns one Remapper per connected device, follows hotplug, pushes the active
// profile and hotkeys to the remappers, and carries out hotkey actions.
class RemapManager : public QObject
{
    Q_OBJECT

public:
    explicit RemapManager(Config *config, QObject *parent = nullptr);
    ~RemapManager() override;

    QList<RazerDevice> devices() const { return m_devices; }
    Remapper *remapper(const QString &hidPath) const { return m_remappers.value(hidPath); }
    QString errorFor(const QString &hidPath) const { return m_errors.value(hidPath); }

    bool remappingEnabled() const;
    void setRemappingEnabled(bool enabled);
    bool switchProfile(const QString &device, const QString &name);
    QString profileSummary() const; // "Huntsman Elite: Gaming · Viper Ultimate: Default"

    // Run a hotkey action on one device, or all when `device` is empty;
    // `notify` shows a desktop notification about it.
    QString perform(Hotkey::Action action, const QString &profile, const QString &device, bool notify);
    // Switch several devices at once (device key -> profile), with one
    // notification that says why.
    void switchProfiles(const QHash<QString, QString> &profiles, const QString &reason, bool notify);

    // Exact device key, or a unique case-insensitive part of one ("viper").
    QString resolveDevice(const QString &query, QString *error) const;

    // Call after changing the config.
    void applyProfile();

    // Accept commands from `razer-rebind --profile ...` and friends.
    bool listen();
    static QString serverName();

public slots:
    void rescan();

signals:
    void devicesChanged();
    void stateChanged();
    void profileChanged();
    void showRequested();
    void inputEvent(const QString &profileKey, int code, bool pressed);
    void message(const QString &text);

private slots:
    void onNotificationClosed(uint id, uint reason);

private:
    void addRemapper(const RazerDevice &dev);
    void handleCommand(const QString &command, QString *reply);
    void notify(const QString &summary, const QString &body = {});

    Config *m_config;
    QList<RazerDevice> m_devices;
    QHash<QString, Remapper *> m_remappers; // by hidPath
    QHash<QString, QString> m_errors;       // by hidPath
    QFileSystemWatcher m_watcher;
    QTimer m_rescanTimer;
    int m_retries = 0;
    QLocalServer *m_server = nullptr;
    uint m_notificationId = 0;     // popup currently on screen, 0 if none
    QElapsedTimer m_notificationShown;
};
