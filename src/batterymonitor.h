#pragma once

#include <QHash>
#include <QObject>
#include <QTimer>

#include <atomic>

class Config;
class RemapManager;

// Polls the charge of wireless Razer devices through OpenRazer's sysfs
// attributes and raises a desktop notification when one runs low: once when
// it drops to the configured threshold, then again every 5% lower until it
// is charged.
class BatteryMonitor : public QObject
{
    Q_OBJECT

public:
    struct Level
    {
        int percent = -1;   // -1 = unknown (no battery, or the device is asleep)
        bool charging = false;
    };

    BatteryMonitor(RemapManager *manager, Config *config, QObject *parent = nullptr);

    Level level(const QString &hidPath) const { return m_levels.value(hidPath); }
    QString summary() const; // "Viper Ultimate 58%", for tooltips

    void sendTestNotification();

public slots:
    void poll();

signals:
    void levelsChanged();

private:
    void apply(const QHash<QString, Level> &levels);
    void alert(const QString &deviceKey, int percent);

    RemapManager *m_manager;
    Config *m_config;
    QTimer m_timer;
    QHash<QString, Level> m_levels;      // by hidPath
    QHash<QString, int> m_lastAlert;     // by hidPath: percent of the last alert
    std::atomic<bool> m_polling{false};
};
