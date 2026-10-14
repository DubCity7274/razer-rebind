#include "notifications.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QVariantMap>

namespace Notifications {

void send(QObject *context, const QString &summary, const QString &body, const QString &icon, bool transient,
          Urgency urgency, uint replacesId, int timeoutMs, const std::function<void(uint)> &onShown)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("/org/freedesktop/Notifications"),
        QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("Notify"));
    QVariantMap hints{{QStringLiteral("urgency"), QVariant::fromValue(uchar(urgency))}};
    if (transient)
        hints.insert(QStringLiteral("transient"), true);
    msg << QStringLiteral("Razer Rebind") << replacesId << icon << summary << body << QStringList() << hints
        << timeoutMs;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), context);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, context, [onShown](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<uint> reply = *w;
        if (reply.isValid() && onShown)
            onShown(reply.value());
        w->deleteLater();
    });
}

} // namespace Notifications
