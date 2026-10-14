#include "batterymonitor.h"

#include "config.h"
#include "notifications.h"
#include "remapmanager.h"

#include <QCoreApplication>
#include <QPointer>
#include <QStringList>

#include <thread>

namespace {

constexpr int PollIntervalMs = 60 * 1000;
constexpr int ReminderStep = 5;  // percent
constexpr int Hysteresis = 3;    // percent above the threshold before re-arming

struct Probe
{
    QString hidPath;
    RazerDevice device;
};

} // namespace

BatteryMonitor::BatteryMonitor(RemapManager *manager, Config *config, QObject *parent)
    : QObject(parent)
    , m_manager(manager)
    , m_config(config)
{
    m_timer.setInterval(PollIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &BatteryMonitor::poll);
    m_timer.start();
    // A new or reconnected device gets checked right away.
    connect(m_manager, &RemapManager::devicesChanged, this, &BatteryMonitor::poll);
    QTimer::singleShot(0, this, &BatteryMonitor::poll);
}

void BatteryMonitor::poll()
{
    if (m_polling.exchange(true))
        return;

    QList<Probe> probes;
    for (const RazerDevice &d : m_manager->devices())
        if (d.hasBattery())
            probes << Probe{d.hidPath, d};
    if (probes.isEmpty()) {
        m_polling = false;
        apply({});
        return;
    }

    // Each read is a USB round trip to the receiver and on to the device,
    // which can take a while for a wireless mouse: keep it off the UI thread.
    QPointer<BatteryMonitor> self(this);
    std::thread([self, probes] {
        QHash<QString, Level> levels;
        for (const Probe &p : probes) {
            bool ok = false;
            const int raw = p.device.attribute("charge_level").toInt(&ok);
            Level level;
            level.charging = p.device.attribute("charge_status").toInt() == 1;
            // OpenRazer reports 0 when the mouse is asleep or out of range.
            level.percent = ok && raw > 0 ? qRound(raw * 100.0 / 255.0) : -1;
            levels.insert(p.hidPath, level);
        }
        QMetaObject::invokeMethod(qApp, [self, levels] {
            if (self) {
                self->m_polling = false;
                self->apply(levels);
            }
        }, Qt::QueuedConnection);
    }).detach();
}

void BatteryMonitor::apply(const QHash<QString, Level> &levels)
{
    m_levels = levels;
    for (auto it = levels.cbegin(); it != levels.cend(); ++it) {
        const Level &l = it.value();
        if (l.percent < 0)
            continue;
        const int threshold = m_config->batteryThreshold;
        if (l.charging || l.percent > threshold + Hysteresis) {
            m_lastAlert.remove(it.key());
            continue;
        }
        if (!m_config->batteryAlerts || l.percent > threshold)
            continue;
        const auto last = m_lastAlert.constFind(it.key());
        if (last == m_lastAlert.constEnd() || l.percent <= last.value() - ReminderStep) {
            m_lastAlert.insert(it.key(), l.percent);
            for (const RazerDevice &d : m_manager->devices())
                if (d.hidPath == it.key())
                    alert(d.profileKey, l.percent);
        }
    }
    emit levelsChanged();
}

void BatteryMonitor::alert(const QString &deviceKey, int percent)
{
    Notifications::send(this, tr("%1 battery low").arg(shortDeviceName(deviceKey)),
                        tr("%1% left. Time to charge it.").arg(percent),
                        percent <= 10 ? QStringLiteral("battery-caution") : QStringLiteral("battery-low"),
                        false);
}

void BatteryMonitor::sendTestNotification()
{
    QString device = tr("Mouse");
    for (const RazerDevice &d : m_manager->devices())
        if (d.hasBattery())
            device = d.profileKey;
    alert(device, m_config->batteryThreshold);
}

QString BatteryMonitor::summary() const
{
    QStringList parts;
    for (const RazerDevice &d : m_manager->devices()) {
        const Level l = level(d.hidPath);
        if (l.percent >= 0)
            parts << tr("%1 %2%").arg(shortDeviceName(d.profileKey)).arg(l.percent)
                         + (l.charging ? tr(" (charging)") : QString());
    }
    return parts.join(QStringLiteral(", "));
}
