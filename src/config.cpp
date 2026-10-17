#include "config.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

namespace {

const QString DefaultProfile = QStringLiteral("Default");

const struct { Hotkey::Action action; const char *id; } ActionIds[] = {
    {Hotkey::SwitchProfile, "switch-profile"},
    {Hotkey::NextProfile, "next-profile"},
    {Hotkey::PreviousProfile, "previous-profile"},
    {Hotkey::ToggleRemapping, "toggle-remapping"},
};

const struct { unsigned bit; const char *name; } ModifierNames[] = {
    {Keys::ModCtrl, "Ctrl"}, {Keys::ModShift, "Shift"}, {Keys::ModAlt, "Alt"}, {Keys::ModSuper, "Super"},
};

BindingMap bindingsFromJson(const QJsonObject &bindings)
{
    // A binding is a list of key names, or {"keys": [...], "turbo": n} when it
    // has options.
    BindingMap out;
    for (auto b = bindings.begin(); b != bindings.end(); ++b) {
        const int source = Keys::fromName(b.key());
        if (source < 0)
            continue;
        Binding binding;
        QJsonArray keys = b.value().toArray();
        if (b.value().isObject()) {
            const QJsonObject o = b.value().toObject();
            keys = o.value(QStringLiteral("keys")).toArray();
            binding.turbo = std::clamp(o.value(QStringLiteral("turbo")).toInt(0), 0, Keys::TurboMax);
        }
        bool valid = true;
        for (const QJsonValue &v : std::as_const(keys)) {
            const int code = Keys::fromName(v.toString());
            valid = valid && (Keys::isValidTarget(code) || (code == Keys::Hypershift && keys.size() == 1));
            binding.targets << code;
        }
        if (binding.isHypershift() || Keys::isWheel(source))
            binding.turbo = 0;
        if (valid)
            out.insert(source, binding);
    }
    return out;
}

QJsonObject bindingsToJson(const BindingMap &bindings)
{
    QList<int> sources = bindings.keys();
    std::sort(sources.begin(), sources.end());
    QJsonObject out;
    for (int source : sources) {
        const Binding &b = bindings[source];
        QJsonArray targets;
        for (int t : b.targets)
            targets.append(Keys::name(t));
        if (b.turbo > 0)
            out.insert(Keys::name(source), QJsonObject{{QStringLiteral("keys"), targets},
                                                       {QStringLiteral("turbo"), b.turbo}});
        else
            out.insert(Keys::name(source), targets);
    }
    return out;
}

QString tr(const char *text)
{
    return QCoreApplication::translate("Hotkey", text);
}

} // namespace

bool DeviceProfile::hasHypershiftKey() const
{
    return std::any_of(bindings.cbegin(), bindings.cend(), [](const Binding &b) { return b.isHypershift(); });
}

QString shortDeviceName(const QString &deviceKey)
{
    return deviceKey.startsWith(QLatin1String("Razer ")) ? deviceKey.mid(6) : deviceKey;
}

QString Hotkey::actionLabel() const
{
    const QString who = device.isEmpty() ? tr("all devices") : shortDeviceName(device);
    switch (action) {
    case SwitchProfile:
        return device.isEmpty() ? tr("Switch all devices to “%1”").arg(profile)
                                : tr("Switch %1 to “%2”").arg(who, profile);
    case NextProfile: return tr("Next profile (%1)").arg(who);
    case PreviousProfile: return tr("Previous profile (%1)").arg(who);
    case ToggleRemapping: return tr("Turn remapping on/off");
    }
    return {};
}

QString Config::defaultPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QStringLiteral("/bindings.json");
}

bool Config::load(const QString &path, QString *error)
{
    m_path = path;
    m_deviceOrder.clear();
    m_devices.clear();
    hotkeys.clear();

    QFile f(path);
    if (!f.exists())
        return true;
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseError);
    if (!doc.isObject()) {
        if (error)
            *error = QStringLiteral("%1: %2").arg(path, parseError.errorString());
        return false;
    }

    const QJsonObject root = doc.object();
    const int version = root.value(QStringLiteral("version")).toInt(1);
    remappingEnabled = root.value(QStringLiteral("remappingEnabled")).toBool(true);
    notifications = root.value(QStringLiteral("notifications")).toBool(true);
    const QJsonObject battery = root.value(QStringLiteral("battery")).toObject();
    batteryAlerts = battery.value(QStringLiteral("alerts")).toBool(true);
    batteryThreshold = std::clamp(battery.value(QStringLiteral("threshold")).toInt(20), 1, 99);

    auto addDevice = [this](const QString &key) -> DeviceSettings & {
        if (!m_devices.contains(key))
            m_deviceOrder << key;
        return m_devices[key];
    };

    if (version >= 3) {
        for (const QJsonValue &dv : root.value(QStringLiteral("devices")).toArray()) {
            const QJsonObject d = dv.toObject();
            const QString key = d.value(QStringLiteral("name")).toString();
            if (key.isEmpty() || m_devices.contains(key))
                continue;
            DeviceSettings &ds = addDevice(key);
            ds.enabled = d.value(QStringLiteral("enabled")).toBool(true);
            ds.active = d.value(QStringLiteral("activeProfile")).toString();
            for (const QJsonValue &pv : d.value(QStringLiteral("profiles")).toArray()) {
                const QJsonObject p = pv.toObject();
                const QString name = p.value(QStringLiteral("name")).toString().trimmed();
                if (!name.isEmpty() && !findProfile(key, name))
                    ds.profiles << Profile{name, bindingsFromJson(p.value(QStringLiteral("bindings")).toObject()),
                                           bindingsFromJson(p.value(QStringLiteral("hypershift")).toObject())};
            }
        }
    } else if (version == 2) {
        // Global profiles: every device gets every profile, active = the global one.
        const QString active = root.value(QStringLiteral("activeProfile")).toString();
        const QJsonArray profiles = root.value(QStringLiteral("profiles")).toArray();
        for (const QJsonValue &pv : profiles)
            for (const QString &key : pv.toObject().value(QStringLiteral("devices")).toObject().keys())
                addDevice(key);
        for (const QString &key : std::as_const(m_deviceOrder)) {
            DeviceSettings &ds = m_devices[key];
            for (const QJsonValue &pv : profiles) {
                const QJsonObject p = pv.toObject();
                const QString name = p.value(QStringLiteral("name")).toString().trimmed();
                const QJsonObject d = p.value(QStringLiteral("devices")).toObject().value(key).toObject();
                if (name.isEmpty() || findProfile(key, name))
                    continue;
                ds.profiles << Profile{name, bindingsFromJson(d.value(QStringLiteral("bindings")).toObject()), {}};
                if (name == active) {
                    ds.active = name;
                    ds.enabled = d.value(QStringLiteral("enabled")).toBool(true);
                }
            }
        }
    } else {
        // Version 1: one set of bindings per device.
        const QJsonObject devices = root.value(QStringLiteral("devices")).toObject();
        for (auto it = devices.begin(); it != devices.end(); ++it) {
            const QJsonObject d = it.value().toObject();
            DeviceSettings &ds = addDevice(it.key());
            ds.enabled = d.value(QStringLiteral("enabled")).toBool(true);
            ds.profiles << Profile{DefaultProfile, bindingsFromJson(d.value(QStringLiteral("bindings")).toObject()), {}};
        }
    }

    for (auto it = m_devices.begin(); it != m_devices.end(); ++it) {
        if (it->profiles.isEmpty())
            it->profiles << Profile{DefaultProfile, {}, {}};
        if (!findProfile(it.key(), it->active))
            it->active = it->profiles.first().name;
    }

    for (const QJsonValue &v : root.value(QStringLiteral("hotkeys")).toArray()) {
        const QJsonObject o = v.toObject();
        Hotkey h;
        h.code = Keys::fromName(o.value(QStringLiteral("key")).toString());
        if (h.code <= 0)
            continue;
        for (const QJsonValue &m : o.value(QStringLiteral("modifiers")).toArray())
            for (const auto &mn : ModifierNames)
                if (m.toString().compare(QLatin1String(mn.name), Qt::CaseInsensitive) == 0)
                    h.modifiers |= mn.bit;
        const QString action = o.value(QStringLiteral("action")).toString();
        bool known = false;
        for (const auto &a : ActionIds)
            if (action == QLatin1String(a.id)) {
                h.action = a.action;
                known = true;
            }
        h.profile = o.value(QStringLiteral("profile")).toString();
        h.device = o.value(QStringLiteral("device")).toString();
        if (known)
            hotkeys << h;
    }
    return true;
}

bool Config::save(QString *error) const
{
    QJsonArray devices;
    for (const QString &key : m_deviceOrder) {
        const DeviceSettings &ds = m_devices[key];
        QJsonArray profiles;
        for (const Profile &p : ds.profiles) {
            QJsonObject o{{QStringLiteral("name"), p.name}, {QStringLiteral("bindings"), bindingsToJson(p.bindings)}};
            if (!p.hypershift.isEmpty())
                o.insert(QStringLiteral("hypershift"), bindingsToJson(p.hypershift));
            profiles.append(o);
        }
        devices.append(QJsonObject{{QStringLiteral("name"), key},
                                   {QStringLiteral("enabled"), ds.enabled},
                                   {QStringLiteral("activeProfile"), ds.active},
                                   {QStringLiteral("profiles"), profiles}});
    }

    QJsonArray hotkeyArray;
    for (const Hotkey &h : hotkeys) {
        QJsonArray mods;
        for (const auto &mn : ModifierNames)
            if (h.modifiers & mn.bit)
                mods.append(QLatin1String(mn.name));
        QJsonObject o{{QStringLiteral("modifiers"), mods}, {QStringLiteral("key"), Keys::name(h.code)}};
        for (const auto &a : ActionIds)
            if (a.action == h.action)
                o.insert(QStringLiteral("action"), QLatin1String(a.id));
        if (h.action == Hotkey::SwitchProfile)
            o.insert(QStringLiteral("profile"), h.profile);
        if (!h.device.isEmpty() && h.action != Hotkey::ToggleRemapping)
            o.insert(QStringLiteral("device"), h.device);
        hotkeyArray.append(o);
    }

    const QJsonObject root{
        {QStringLiteral("version"), 3},
        {QStringLiteral("remappingEnabled"), remappingEnabled},
        {QStringLiteral("notifications"), notifications},
        {QStringLiteral("battery"), QJsonObject{{QStringLiteral("alerts"), batteryAlerts},
                                                {QStringLiteral("threshold"), batteryThreshold}}},
        {QStringLiteral("devices"), devices},
        {QStringLiteral("hotkeys"), hotkeyArray},
    };

    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile f(m_path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return true;
}

bool Config::ensureDevice(const QString &device)
{
    if (device.isEmpty() || m_devices.contains(device))
        return false;
    m_deviceOrder << device;
    DeviceSettings &ds = m_devices[device];
    ds.profiles << Profile{DefaultProfile, {}, {}};
    ds.active = DefaultProfile;
    return true;
}

const Config::Profile *Config::findProfile(const QString &device, const QString &name) const
{
    const auto it = m_devices.constFind(device);
    if (it == m_devices.constEnd())
        return nullptr;
    for (const Profile &p : it->profiles)
        if (p.name == name)
            return &p;
    return nullptr;
}

DeviceProfile Config::device(const QString &device) const
{
    DeviceProfile out;
    const auto it = m_devices.constFind(device);
    if (it == m_devices.constEnd())
        return out;
    out.enabled = it->enabled;
    if (const Profile *p = findProfile(device, it->active)) {
        out.bindings = p->bindings;
        out.hypershift = p->hypershift;
    }
    return out;
}

void Config::setDevice(const QString &device, const DeviceProfile &profile)
{
    ensureDevice(device);
    DeviceSettings &ds = m_devices[device];
    ds.enabled = profile.enabled;
    for (Profile &p : ds.profiles)
        if (p.name == ds.active) {
            p.bindings = profile.bindings;
            p.hypershift = profile.hypershift;
        }
}

QStringList Config::profileNames(const QString &device) const
{
    QStringList out;
    for (const Profile &p : m_devices.value(device).profiles)
        out << p.name;
    return out;
}

QString Config::activeProfile(const QString &device) const
{
    return m_devices.value(device).active;
}

bool Config::hasProfile(const QString &device, const QString &name) const
{
    return findProfile(device, name) != nullptr;
}

bool Config::setActiveProfile(const QString &device, const QString &name)
{
    if (!findProfile(device, name))
        return false;
    m_devices[device].active = name;
    return true;
}

QString Config::adjacentProfile(const QString &device, int step) const
{
    const QStringList names = profileNames(device);
    if (names.isEmpty())
        return {};
    const int i = int(names.indexOf(activeProfile(device)));
    const int n = int(names.size());
    return names.value(((i + step) % n + n) % n);
}

QStringList Config::allProfileNames() const
{
    QStringList out;
    for (const QString &key : m_deviceOrder)
        for (const Profile &p : m_devices[key].profiles)
            if (!out.contains(p.name))
                out << p.name;
    return out;
}

bool Config::anyDeviceHasProfile(const QString &name) const
{
    for (const QString &key : m_deviceOrder)
        if (findProfile(key, name))
            return true;
    return false;
}

bool Config::addProfile(const QString &device, const QString &name, const QString &copyFrom)
{
    if (name.trimmed().isEmpty() || findProfile(device, name))
        return false;
    ensureDevice(device);
    const Profile *source = findProfile(device, copyFrom);
    m_devices[device].profiles << (source ? Profile{name, source->bindings, source->hypershift} : Profile{name, {}, {}});
    return true;
}

bool Config::renameProfile(const QString &device, const QString &from, const QString &to)
{
    if (!findProfile(device, from) || to.trimmed().isEmpty() || findProfile(device, to))
        return false;
    DeviceSettings &ds = m_devices[device];
    for (Profile &p : ds.profiles)
        if (p.name == from)
            p.name = to;
    if (ds.active == from)
        ds.active = to;
    // Hotkeys aimed at this device follow the rename; all-device hotkeys
    // follow it only once no other device uses the old name.
    const bool stillUsed = anyDeviceHasProfile(from);
    for (Hotkey &h : hotkeys)
        if (h.action == Hotkey::SwitchProfile && h.profile == from && (h.device == device || (h.device.isEmpty() && !stillUsed)))
            h.profile = to;
    return true;
}

bool Config::removeProfile(const QString &device, const QString &name)
{
    if (!findProfile(device, name) || m_devices[device].profiles.size() <= 1)
        return false;
    DeviceSettings &ds = m_devices[device];
    int index = 0;
    for (int i = 0; i < ds.profiles.size(); ++i)
        if (ds.profiles[i].name == name)
            index = i;
    ds.profiles.removeAt(index);
    if (ds.active == name)
        ds.active = ds.profiles[std::min(index, int(ds.profiles.size()) - 1)].name;
    const bool stillUsed = anyDeviceHasProfile(name);
    hotkeys.erase(std::remove_if(hotkeys.begin(), hotkeys.end(),
                                 [&](const Hotkey &h) {
                                     return h.action == Hotkey::SwitchProfile && h.profile == name
                                            && (h.device == device || (h.device.isEmpty() && !stillUsed));
                                 }),
                  hotkeys.end());
    return true;
}
