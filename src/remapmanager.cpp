#include "remapmanager.h"

#include "remapper.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLocalServer>
#include <QLocalSocket>

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

bool RemapManager::switchProfile(const QString &name)
{
    if (!m_config->setActiveProfile(name))
        return false;
    m_config->save(nullptr);
    applyProfile();
    emit profileChanged();
    return true;
}

QString RemapManager::perform(Hotkey::Action action, const QString &profile, bool notifyUser)
{
    QString target;
    switch (action) {
    case Hotkey::SwitchProfile:
        target = profile;
        break;
    case Hotkey::NextProfile:
        target = m_config->adjacentProfile(1);
        break;
    case Hotkey::PreviousProfile:
        target = m_config->adjacentProfile(-1);
        break;
    case Hotkey::ToggleRemapping: {
        setRemappingEnabled(!remappingEnabled());
        const QString state = remappingEnabled() ? tr("Remapping on") : tr("Remapping off");
        if (notifyUser)
            notify(state, remappingEnabled() ? tr("Profile: %1").arg(m_config->activeProfile())
                                             : tr("Razer devices work normally"));
        return state;
    }
    }

    if (!switchProfile(target)) {
        const QString err = tr("No profile named “%1”").arg(target);
        emit message(err);
        return err;
    }
    if (notifyUser)
        notify(tr("Profile: %1").arg(target),
               remappingEnabled() ? QString() : tr("Remapping is turned off"));
    emit message(tr("Switched to profile “%1”").arg(target));
    return tr("Switched to profile “%1”").arg(target);
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
    for (const RazerDevice &d : found)
        if (!m_remappers.contains(d.hidPath))
            addRemapper(d);

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
    connect(r, &Remapper::hotkeyTriggered, this, [this](int action, const QString &profile) {
        perform(Hotkey::Action(action), profile, m_config->notifications);
    });
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
        r->setBindings(p.bindings);
        r->setHotkeys(m_config->hotkeys);
        // Only take a device away from the system when there is something to
        // do with it: a binding, or a hotkey it can trigger.
        const bool hotkeyHere = std::any_of(m_config->hotkeys.cbegin(), m_config->hotkeys.cend(),
                                            [r](const Hotkey &h) { return r->canProduce(h.code); });
        r->setActive(m_config->remappingEnabled && p.enabled && (!p.bindings.isEmpty() || hotkeyHere));
    }
    emit stateChanged();
}

// --- remote control ----------------------------------------------------------

QString RemapManager::serverName()
{
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
    const QString verb = command.section(QLatin1Char(' '), 0, 0);
    const QString arg = command.section(QLatin1Char(' '), 1);
    if (verb == QLatin1String("show")) {
        emit showRequested();
        *reply = QStringLiteral("ok");
    } else if (verb == QLatin1String("profile")) {
        *reply = perform(Hotkey::SwitchProfile, arg, m_config->notifications);
    } else if (verb == QLatin1String("next")) {
        *reply = perform(Hotkey::NextProfile, {}, m_config->notifications);
    } else if (verb == QLatin1String("previous")) {
        *reply = perform(Hotkey::PreviousProfile, {}, m_config->notifications);
    } else if (verb == QLatin1String("toggle")) {
        *reply = perform(Hotkey::ToggleRemapping, {}, m_config->notifications);
    } else if (verb == QLatin1String("list")) {
        QStringList lines;
        for (const QString &name : m_config->profileNames())
            lines << (name == m_config->activeProfile() ? QStringLiteral("* ") : QStringLiteral("  ")) + name;
        *reply = lines.join(QLatin1Char('\n'));
    } else {
        *reply = tr("unknown command: %1").arg(command);
    }
}

// --- notifications -------------------------------------------------------

void RemapManager::notify(const QString &summary, const QString &body)
{
    // freedesktop notification; replaces the previous one so rapid switching
    // doesn't pile up a stack of popups.
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("/org/freedesktop/Notifications"),
        QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("Notify"));
    msg << QStringLiteral("Razer Rebind") << m_notificationId << QStringLiteral("input-keyboard") << summary << body
        << QStringList() << QVariantMap{{QStringLiteral("transient"), true}} << 2500;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<uint> reply = *w;
        if (reply.isValid())
            m_notificationId = reply.value();
        w->deleteLater();
    });
}
