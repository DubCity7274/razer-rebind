#include "remapmanager.h"

#include "notifications.h"
#include "remapper.h"
#include "sensor.h"

#include <QDBusConnection>
#include <QDir>
#include <QLocalServer>
#include <QStandardPaths>
#include <QLocalSocket>
#include <QProcess>
#include <QSet>

#include <unistd.h>

RemapManager::RemapManager(Config *config, QObject *parent)
    : QObject(parent)
    , m_config(config)
{
    // Event nodes come and go when devices are plugged in, the wireless mouse
    // reconnects, or the OpenRazer modules are reloaded. udev needs a moment
    // to set permissions on new nodes, hence the delay.
    m_rescanTimer.setSingleShot(true);
    m_rescanTimer.setInterval(800);
    connect(&m_rescanTimer, &QTimer::timeout, this, &RemapManager::rescan);
    m_watcher.addPath(QStringLiteral("/dev/input"));
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] {
        m_retries = 0;
        m_rescanTimer.start();
    });

    QDBusConnection::sessionBus().connect(QStringLiteral("org.freedesktop.Notifications"),
                                          QStringLiteral("/org/freedesktop/Notifications"),
                                          QStringLiteral("org.freedesktop.Notifications"),
                                          QStringLiteral("NotificationClosed"), this,
                                          SLOT(onNotificationClosed(uint,uint)));
}

RemapManager::~RemapManager()
{
    // Stop explicitly so every grab is released and held keys are let go
    // before the virtual devices disappear.
    qDeleteAll(m_remappers);
}

bool RemapManager::remappingEnabled() const
{
    return m_config->remappingEnabled;
}

void RemapManager::setRemappingEnabled(bool enabled)
{
    m_config->remappingEnabled = enabled;
    m_config->save(nullptr);
    applyProfile();
}

bool RemapManager::switchProfile(const QString &device, const QString &name)
{
    if (!m_config->setActiveProfile(device, name))
        return false;
    m_config->save(nullptr);
    applyProfile();
    emit profileChanged();
    return true;
}

QString RemapManager::profileSummary() const
{
    QStringList parts;
    for (const QString &d : m_config->deviceKeys())
        parts << QStringLiteral("%1: %2").arg(shortDeviceName(d), m_config->activeProfile(d));
    return parts.join(QStringLiteral(" · "));
}

QString RemapManager::resolveDevice(const QString &query, QString *error) const
{
    const QStringList keys = m_config->deviceKeys();
    if (query.isEmpty() || keys.contains(query))
        return query;
    QStringList matches;
    for (const QString &k : keys)
        if (k.contains(query, Qt::CaseInsensitive))
            matches << k;
    if (matches.size() == 1)
        return matches.first();
    *error = matches.isEmpty() ? tr("No device matches “%1”").arg(query)
                               : tr("“%1” matches several devices: %2").arg(query, matches.join(QStringLiteral(", ")));
    return {};
}

QString RemapManager::perform(Hotkey::Action action, const QString &profile, const QString &device,
                              bool notifyUser)
{
    if (action == Hotkey::ToggleRemapping) {
        setRemappingEnabled(!remappingEnabled());
        const QString state = remappingEnabled() ? tr("Remapping on") : tr("Remapping off");
        if (notifyUser)
            notify(state, remappingEnabled() ? profileSummary() : tr("Razer devices work normally"));
        return state;
    }

    // One device, or every device that has a matching profile.
    const QStringList devices = device.isEmpty() ? m_config->deviceKeys() : QStringList{device};
    QStringList changed;
    QSet<QString> names;
    for (const QString &d : devices) {
        const QString name = action == Hotkey::SwitchProfile
                                 ? profile
                                 : m_config->adjacentProfile(d, action == Hotkey::NextProfile ? 1 : -1);
        if (m_config->setActiveProfile(d, name)) {
            changed << QStringLiteral("%1: %2").arg(shortDeviceName(d), name);
            names.insert(name);
        }
    }
    if (changed.isEmpty()) {
        const QString err = device.isEmpty() ? tr("No device has a profile named “%1”").arg(profile)
                                             : tr("%1 has no profile named “%2”").arg(shortDeviceName(device), profile);
        emit message(err);
        return err;
    }
    m_config->save(nullptr);
    applyProfile();
    emit profileChanged();

    if (notifyUser) {
        QString summary = changed.size() == 1 ? changed.first()
                          : names.size() == 1 ? tr("Profile: %1").arg(*names.cbegin())
                                              : tr("Profiles switched");
        QString body = changed.size() == 1 ? QString() : changed.join(QLatin1Char('\n'));
        if (!remappingEnabled())
            body += (body.isEmpty() ? QString() : QStringLiteral("\n")) + tr("Remapping is turned off");
        notify(summary, body);
    }
    const QString text = tr("Switched %1").arg(changed.join(QStringLiteral(", ")));
    emit message(text);
    return text;
}

void RemapManager::switchProfiles(const QHash<QString, QString> &profiles, const QString &reason, bool notifyUser)
{
    QStringList changed;
    for (const QString &d : m_config->deviceKeys()) {
        const auto it = profiles.constFind(d);
        if (it != profiles.constEnd() && *it != m_config->activeProfile(d) && m_config->setActiveProfile(d, *it))
            changed << QStringLiteral("%1: %2").arg(shortDeviceName(d), *it);
    }
    if (changed.isEmpty())
        return;
    m_config->save(nullptr);
    applyProfile();
    emit profileChanged();
    emit message(QStringLiteral("%1 (%2)").arg(changed.join(QStringLiteral(", ")), reason));
    if (notifyUser)
        notify(changed.join(QLatin1Char('\n')), reason);
}

void RemapManager::runCommand(const QString &command)
{
    // Through the shell, so arguments, quotes, pipes and ~ work as in a
    // terminal. Detached: it keeps running on its own and never holds up
    // input. Its output goes nowhere.
    QProcess p;
    p.setProgram(QStringLiteral("/bin/sh"));
    p.setArguments({QStringLiteral("-c"), command});
    p.setWorkingDirectory(QDir::homePath());
    p.setStandardInputFile(QProcess::nullDevice());
    p.setStandardOutputFile(QProcess::nullDevice());
    p.setStandardErrorFile(QProcess::nullDevice());
    if (!p.startDetached())
        emit message(tr("Could not run “%1”: %2").arg(command, p.errorString()));
}

void RemapManager::rescan()
{
    const QList<RazerDevice> found = discoverRazerDevices();

    QHash<QString, RazerDevice> byPath;
    for (const RazerDevice &d : found)
        byPath.insert(d.hidPath, d);

    // Drop remappers whose device vanished or changed its set of nodes.
    for (auto it = m_remappers.begin(); it != m_remappers.end();) {
        const auto d = byPath.constFind(it.key());
        if (d == byPath.constEnd() || d->signature() != it.value()->device().signature()) {
            delete it.value();
            it = m_remappers.erase(it);
        } else {
            ++it;
        }
    }

    m_errors.clear();
    bool newDevice = false;
    for (const RazerDevice &d : found) {
        newDevice |= m_config->ensureDevice(d.profileKey);
        if (!m_remappers.contains(d.hidPath))
            addRemapper(d);
    }
    if (newDevice)
        m_config->save(nullptr);

    // A device that went away (the wireless mouse sleeping, unplugged) gets
    // its DPI set again when it's back.
    for (auto it = m_appliedSensor.begin(); it != m_appliedSensor.end();)
        it = byPath.contains(it.key()) ? std::next(it) : m_appliedSensor.erase(it);

    m_devices = found;
    applyProfile();
    emit devicesChanged();

    // Freshly created nodes may still be root-only until udev catches up.
    if (!m_errors.isEmpty() && m_retries++ < 5)
        m_rescanTimer.start(2000);
}

void RemapManager::addRemapper(const RazerDevice &dev)
{
    auto *r = new Remapper(dev);
    QString error;
    if (!r->start(&error)) {
        delete r;
        m_errors.insert(dev.hidPath, error);
        emit message(error);
        return;
    }
    const QString key = dev.profileKey;
    const QString hidPath = dev.hidPath;
    connect(r, &Remapper::inputEvent, this, [this, key](int code, bool pressed) {
        emit inputEvent(key, code, pressed);
    });
    connect(r, &Remapper::stateChanged, this, &RemapManager::stateChanged);
    connect(r, &Remapper::hotkeyTriggered, this, [this](int action, const QString &profile, const QString &device) {
        perform(Hotkey::Action(action), profile, device, m_config->notifications);
    });
    connect(r, &Remapper::commandTriggered, this, &RemapManager::runCommand);
    connect(r, &Remapper::failed, this, [this, hidPath](const QString &msg) {
        m_errors.insert(hidPath, msg);
        emit message(msg);
        emit stateChanged();
    });
    connect(r, &Remapper::disconnected, this, [this] { m_rescanTimer.start(); });
    m_remappers.insert(dev.hidPath, r);
}

void RemapManager::applyProfile()
{
    for (Remapper *r : std::as_const(m_remappers)) {
        const DeviceProfile p = m_config->device(r->device().profileKey);
        r->setProfile(p);
        r->setHotkeys(m_config->hotkeys);
        // Only take a device away from the system when there is something to
        // do with it: a binding (in either layer), or a hotkey it can trigger.
        const bool hotkeyHere = std::any_of(m_config->hotkeys.cbegin(), m_config->hotkeys.cend(),
                                            [r](const Hotkey &h) { return r->canProduce(h.code); });
        r->setActive(m_config->remappingEnabled && p.enabled && (!p.bindings.isEmpty() || !p.hypershift.isEmpty() || hotkeyHere));
    }

    // DPI and polling rate go to the mouse over USB, so only when they change:
    // a profile switch, an edit, or the mouse reconnecting.
    for (const RazerDevice &d : std::as_const(m_devices)) {
        const SensorSettings wanted = m_config->device(d.profileKey).sensor;
        if (d.kind != RazerDevice::Mouse || !wanted.isValid() || m_appliedSensor.value(d.hidPath) == wanted)
            continue;
        QString error;
        if (!Sensor(d).write(wanted, &error))
            emit message(tr("Could not set the DPI of the %1: %2").arg(d.profileKey, error));
        m_appliedSensor.insert(d.hidPath, wanted);
    }
    emit stateChanged();
}

void RemapManager::markSensorApplied(const QString &hidPath, const SensorSettings &settings)
{
    m_appliedSensor.insert(hidPath, settings);
}

// --- remote control ----------------------------------------------------------

QString RemapManager::serverName()
{
    // The per-user runtime directory (/run/user/<uid>) is private to the user;
    // fall back to a name in the temp directory without one.
    const QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (!runtime.isEmpty())
        return QDir(runtime).filePath(QStringLiteral("razer-rebind.sock"));
    return QStringLiteral("razer-rebind-%1").arg(::getuid());
}

bool RemapManager::listen()
{
    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    QLocalServer::removeServer(serverName()); // stale socket; the lock file guarantees we're alone
    if (!m_server->listen(serverName())) {
        emit message(tr("Command socket unavailable: %1").arg(m_server->errorString()));
        return false;
    }
    connect(m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *socket = m_server->nextPendingConnection()) {
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
                if (!socket->canReadLine())
                    return;
                QString reply;
                handleCommand(QString::fromUtf8(socket->readLine()).trimmed(), &reply);
                socket->write(reply.toUtf8() + '\n');
                socket->disconnectFromServer();
            });
        }
    });
    return true;
}

void RemapManager::handleCommand(const QString &command, QString *reply)
{
    // verb TAB device TAB argument; device may be empty for "all devices".
    const QStringList fields = command.split(QLatin1Char('\t'));
    const QString verb = fields.value(0);
    QString error;
    const QString device = resolveDevice(fields.value(1), &error);
    if (!error.isEmpty()) {
        *reply = error;
        return;
    }
    const QString arg = fields.value(2);
    if (verb == QLatin1String("show")) {
        emit showRequested();
        *reply = QStringLiteral("ok");
    } else if (verb == QLatin1String("profile")) {
        *reply = perform(Hotkey::SwitchProfile, arg, device, m_config->notifications);
    } else if (verb == QLatin1String("next")) {
        *reply = perform(Hotkey::NextProfile, {}, device, m_config->notifications);
    } else if (verb == QLatin1String("previous")) {
        *reply = perform(Hotkey::PreviousProfile, {}, device, m_config->notifications);
    } else if (verb == QLatin1String("toggle")) {
        *reply = perform(Hotkey::ToggleRemapping, {}, {}, m_config->notifications);
    } else if (verb == QLatin1String("list")) {
        QStringList lines;
        for (const QString &d : m_config->deviceKeys()) {
            if (!device.isEmpty() && d != device)
                continue;
            lines << d;
            for (const QString &name : m_config->profileNames(d))
                lines << (name == m_config->activeProfile(d) ? QStringLiteral("  * ") : QStringLiteral("    ")) + name;
        }
        *reply = lines.join(QLatin1Char('\n'));
    } else {
        *reply = tr("unknown command: %1").arg(verb);
    }
}

// --- notifications -------------------------------------------------------

void RemapManager::onNotificationClosed(uint id, uint)
{
    if (id == m_notificationId)
        m_notificationId = 0;
}

void RemapManager::notify(const QString &summary, const QString &body)
{
    // While our previous popup is still on screen, replace it so rapid
    // switching doesn't pile up popups. Once it has gone, always start a new
    // one: KDE Plasma applies a replacement of an expired notification
    // silently to its history, so nothing would pop up.
    constexpr int timeoutMs = 2500;
    if (!m_notificationShown.isValid() || m_notificationShown.elapsed() > timeoutMs)
        m_notificationId = 0;
    m_notificationShown.start();
    Notifications::send(this, summary, body, QStringLiteral("input-keyboard"), true, Notifications::Urgency::Normal,
                        m_notificationId, timeoutMs, [this](uint id) { m_notificationId = id; });
}
