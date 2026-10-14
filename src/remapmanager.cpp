#include "remapmanager.h"

#include "profile.h"
#include "remapper.h"

RemapManager::RemapManager(Profile *profile, QObject *parent)
    : QObject(parent)
    , m_profile(profile)
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
    return m_profile->remappingEnabled;
}

void RemapManager::setRemappingEnabled(bool enabled)
{
    m_profile->remappingEnabled = enabled;
    m_profile->save(nullptr);
    applyProfile();
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
        const DeviceProfile p = m_profile->device(r->device().profileKey);
        r->setBindings(p.bindings);
        // Only take the device away from the system when there is something to remap.
        r->setActive(m_profile->remappingEnabled && p.enabled && !p.bindings.isEmpty());
    }
    emit stateChanged();
}
