#include "idlelights.h"

#include "config.h"
#include "remapmanager.h"
#include "remapper.h"

#include <QDBusConnection>
#include <QDBusMessage>

#include <time.h>

namespace {

qint64 nowNs()
{
    timespec ts;
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return qint64(ts.tv_sec) * 1000000000 + ts.tv_nsec;
}

} // namespace

IdleLights::IdleLights(RemapManager *manager, Config *config, QObject *parent)
    : QObject(parent)
    , m_manager(manager)
    , m_config(config)
{
    m_timer.setInterval(5000);
    connect(&m_timer, &QTimer::timeout, this, &IdleLights::update);
    m_timer.start();
    connect(m_manager, &RemapManager::activity, this, &IdleLights::onActivity);

    // The desktop's screen locker (KDE, GNOME and others implement this).
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.connect(QString(), QString(), QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("ActiveChanged"),
                this, SLOT(onScreenSaverActive(bool)));
    const QDBusMessage reply = bus.call(
        QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("/ScreenSaver"),
                                       QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("GetActive")),
        QDBus::Block, 1000);
    if (reply.type() == QDBusMessage::ReplyMessage)
        m_locked = reply.arguments().value(0).toBool();
}

void IdleLights::update()
{
    const qint64 idleNs = nowNs() - Remapper::lastActivityNs();
    m_idle = m_config->lightsOffIdle && idleNs > qint64(m_config->lightsOffIdleMinutes) * 60 * 1000000000;
    const bool off = m_idle || (m_locked && m_config->lightsOffLocked);
    if (off != m_manager->lightsOff()) {
        m_manager->setLightsOff(off);
        const int minutes = m_config->lightsOffIdleMinutes;
        emit m_manager->message(off ? (m_idle ? (minutes == 1 ? tr("Lighting off: no input for a minute")
                                                              : tr("Lighting off: no input for %1 minutes").arg(minutes))
                                              : tr("Lighting off while the screen is locked"))
                                    : tr("Lighting back on"));
    }
    // Wake up on the very next input instead of the next check.
    if (m_idle)
        Remapper::requestActivitySignal();
}

void IdleLights::onActivity()
{
    m_idle = false;
    update();
}

void IdleLights::onScreenSaverActive(bool active)
{
    m_locked = active;
    update();
}
