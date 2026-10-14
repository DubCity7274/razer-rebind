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

QMap<QString, DeviceProfile> devicesFromJson(const QJsonObject &devices)
{
    QMap<QString, DeviceProfile> out;
    for (auto it = devices.begin(); it != devices.end(); ++it) {
        const QJsonObject obj = it.value().toObject();
        DeviceProfile dp;
        dp.enabled = obj.value(QStringLiteral("enabled")).toBool(true);
        const QJsonObject bindings = obj.value(QStringLiteral("bindings")).toObject();
        for (auto b = bindings.begin(); b != bindings.end(); ++b) {
            const int source = Keys::fromName(b.key());
            if (source < 0)
                continue;
            QList<int> targets;
            bool valid = true;
            for (const QJsonValue &v : b.value().toArray()) {
                const int code = Keys::fromName(v.toString());
                valid = valid && Keys::isValidTarget(code);
                targets << code;
            }
            if (valid)
                dp.bindings.insert(source, targets);
        }
        out.insert(it.key(), dp);
    }
    return out;
}

QJsonObject devicesToJson(const QMap<QString, DeviceProfile> &devices)
{
    QJsonObject out;
    for (auto it = devices.cbegin(); it != devices.cend(); ++it) {
        QList<int> sources = it->bindings.keys();
        std::sort(sources.begin(), sources.end());
        QJsonObject bindings;
        for (int source : sources) {
            QJsonArray targets;
            for (int t : it->bindings.value(source))
                targets.append(Keys::name(t));
            bindings.insert(Keys::name(source), targets);
        }
        out.insert(it.key(), QJsonObject{{QStringLiteral("enabled"), it->enabled},
                                         {QStringLiteral("bindings"), bindings}});
    }
    return out;
}

} // namespace

QString Hotkey::actionLabel() const
{
    switch (action) {
    case SwitchProfile: return QCoreApplication::translate("Hotkey", "Switch to “%1”").arg(profile);
    case NextProfile: return QCoreApplication::translate("Hotkey", "Next profile");
    case PreviousProfile: return QCoreApplication::translate("Hotkey", "Previous profile");
    case ToggleRemapping: return QCoreApplication::translate("Hotkey", "Turn remapping on/off");
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
    m_order.clear();
    m_profiles.clear();
    hotkeys.clear();

    QFile f(path);
    if (!f.exists()) {
        ensureProfile();
        return true;
    }
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        ensureProfile();
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseError);
    if (!doc.isObject()) {
        if (error)
            *error = QStringLiteral("%1: %2").arg(path, parseError.errorString());
        ensureProfile();
        return false;
    }

    const QJsonObject root = doc.object();
    remappingEnabled = root.value(QStringLiteral("remappingEnabled")).toBool(true);
    notifications = root.value(QStringLiteral("notifications")).toBool(true);

    if (root.contains(QStringLiteral("profiles"))) {
        for (const QJsonValue &v : root.value(QStringLiteral("profiles")).toArray()) {
            const QJsonObject p = v.toObject();
            const QString name = p.value(QStringLiteral("name")).toString().trimmed();
            if (name.isEmpty() || m_profiles.contains(name))
                continue;
            m_order << name;
            m_profiles.insert(name, devicesFromJson(p.value(QStringLiteral("devices")).toObject()));
        }
    } else {
        // Version 1 files had a single set of bindings.
        m_order << DefaultProfile;
        m_profiles.insert(DefaultProfile, devicesFromJson(root.value(QStringLiteral("devices")).toObject()));
    }
    m_active = root.value(QStringLiteral("activeProfile")).toString();

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
        if (known)
            hotkeys << h;
    }

    ensureProfile();
    return true;
}

bool Config::save(QString *error) const
{
    QJsonArray profiles;
    for (const QString &name : m_order)
        profiles.append(QJsonObject{{QStringLiteral("name"), name},
                                    {QStringLiteral("devices"), devicesToJson(m_profiles.value(name))}});

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
        hotkeyArray.append(o);
    }

    const QJsonObject root{{QStringLiteral("version"), 2},
                           {QStringLiteral("remappingEnabled"), remappingEnabled},
                           {QStringLiteral("notifications"), notifications},
                           {QStringLiteral("activeProfile"), m_active},
                           {QStringLiteral("profiles"), profiles},
                           {QStringLiteral("hotkeys"), hotkeyArray}};

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

void Config::ensureProfile()
{
    if (m_order.isEmpty()) {
        m_order << DefaultProfile;
        m_profiles.insert(DefaultProfile, {});
    }
    if (!m_profiles.contains(m_active))
        m_active = m_order.first();
}

bool Config::setActiveProfile(const QString &name)
{
    if (!m_profiles.contains(name))
        return false;
    m_active = name;
    return true;
}

QString Config::adjacentProfile(int step) const
{
    const int i = int(m_order.indexOf(m_active));
    const int n = int(m_order.size());
    return m_order.value(((i + step) % n + n) % n);
}

bool Config::addProfile(const QString &name, const QString &copyFrom)
{
    if (name.trimmed().isEmpty() || m_profiles.contains(name))
        return false;
    m_order << name;
    m_profiles.insert(name, m_profiles.value(copyFrom));
    return true;
}

bool Config::renameProfile(const QString &from, const QString &to)
{
    if (!m_profiles.contains(from) || to.trimmed().isEmpty() || m_profiles.contains(to))
        return false;
    m_profiles.insert(to, m_profiles.take(from));
    m_order[m_order.indexOf(from)] = to;
    if (m_active == from)
        m_active = to;
    for (Hotkey &h : hotkeys)
        if (h.action == Hotkey::SwitchProfile && h.profile == from)
            h.profile = to;
    return true;
}

bool Config::removeProfile(const QString &name)
{
    if (!m_profiles.contains(name) || m_order.size() <= 1)
        return false;
    const int index = int(m_order.indexOf(name));
    m_order.removeAt(index);
    m_profiles.remove(name);
    if (m_active == name)
        m_active = m_order.value(std::min(index, int(m_order.size()) - 1));
    hotkeys.erase(std::remove_if(hotkeys.begin(), hotkeys.end(),
                                 [&](const Hotkey &h) { return h.action == Hotkey::SwitchProfile && h.profile == name; }),
                  hotkeys.end());
    return true;
}
