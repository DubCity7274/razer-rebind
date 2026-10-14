#pragma once

#include <QString>

#include <functional>

class QObject;

namespace Notifications {

enum class Urgency : unsigned char { Low = 0, Normal = 1, Critical = 2 };

// Show a freedesktop desktop notification. `replacesId` updates an existing
// popup instead of adding one (0 = new). `onShown` receives the popup's id.
// Transient notifications don't stay in the notification history.
void send(QObject *context, const QString &summary, const QString &body, const QString &icon, bool transient,
          Urgency urgency = Urgency::Normal, uint replacesId = 0, int timeoutMs = -1,
          const std::function<void(uint)> &onShown = {});

} // namespace Notifications
