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
        const int source = Keys::triggerFromName(b.key());
        if (source < 0)
            continue;
        Binding binding;
        QJsonArray keys = b.value().toArray();
        QJsonArray hold;
        if (b.value().isObject()) {
            const QJsonObject o = b.value().toObject();
            keys = o.value(QStringLiteral("keys")).toArray();
            hold = o.value(QStringLiteral("hold")).toArray();
            binding.turbo = std::clamp(o.value(QStringLiteral("turbo")).toInt(0), 0, Keys::TurboMax);
            const int holdTime = o.value(QStringLiteral("holdTime")).toInt(0);
            binding.holdTime = holdTime > 0 ? std::clamp(holdTime, Keys::HoldTimeMin, Keys::HoldTimeMax) : 0;
        }
        bool valid = true;
        auto parse = [&valid](const QJsonArray &names, QList<int> *out) {
            for (const QJsonValue &v : names) {
                const int code = Keys::fromName(v.toString());
                valid = valid && (Keys::isValidTarget(code) || (code == Keys::Hypershift && names.size() == 1));
                *out << code;
            }
        };
        parse(keys, &binding.targets);
        parse(hold, &binding.hold);
        if (b.value().isObject()) {
            // "macro": [{"press": "KEY_A"}, {"delay": 50}, {"release": "KEY_A"}]
            const QJsonObject o = b.value().toObject();
            for (const QJsonValue &v : o.value(QStringLiteral("macro")).toArray()) {
                const QJsonObject st = v.toObject();
                MacroStep step;
                if (st.contains(QStringLiteral("delay"))) {
                    step = {MacroStep::Delay, std::clamp(st.value(QStringLiteral("delay")).toInt(), 0, Keys::MacroMaxDelay)};
                } else {
                    const bool press = st.contains(QStringLiteral("press"));
                    const int code = Keys::fromName(st.value(press ? QStringLiteral("press") : QStringLiteral("release")).toString());
                    if (!Keys::isValidTarget(code))
                        continue;
                    step = {press ? MacroStep::Press : MacroStep::Release, code};
                }
                if (binding.macro.size() < Keys::MacroMaxSteps)
                    binding.macro << step;
            }
            const QString mode = o.value(QStringLiteral("macroMode")).toString();
            binding.macroMode = mode == QLatin1String("while-held") ? Binding::RepeatWhileHeld
                                : mode == QLatin1String("toggle")   ? Binding::Toggle
                                                                    : Binding::PlayOnce;
            binding.command = o.value(QStringLiteral("command")).toString().trimmed();
            const QString dpi = o.value(QStringLiteral("dpi")).toString();
            binding.dpi = dpi == QLatin1String("clutch") ? Binding::DpiClutch
                          : dpi == QLatin1String("up")     ? Binding::DpiStageUp
                          : dpi == QLatin1String("down")   ? Binding::DpiStageDown
                          : dpi == QLatin1String("cycle")  ? Binding::DpiStageCycle
                                                           : Binding::NoDpi;
            binding.clutchDpi = std::clamp(o.value(QStringLiteral("clutchDpi")).toInt(400), Sensor::MinDpi, 30000);
            if (binding.isDpi()) {
                binding.command.clear();
                binding.macro.clear();
            }
            if (binding.isMacro() || binding.isCommand() || binding.isDpi()) {
                binding.turbo = 0;
                binding.hold.clear();
            }
            if (binding.isCommand())
                binding.macro.clear();
        }
        // A wheel notch can't be held; a tap can't be Hypershift.
        if (Keys::isWheel(Keys::triggerCode(source)))
            binding.hold.clear();
        if (binding.isTapHold() && binding.targets.contains(Keys::Hypershift))
            valid = false;
        if (binding.isHypershift() || binding.isTapHold() || Keys::isWheel(Keys::triggerCode(source)))
            binding.turbo = 0;
        if (!binding.isTapHold())
            binding.holdTime = 0;
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
        if (b.turbo > 0 || b.isTapHold() || b.isMacro() || b.isCommand() || b.isDpi()) {
            QJsonObject o{{QStringLiteral("keys"), targets}};
            if (b.isDpi()) {
                const char *ids[] = {"", "clutch", "up", "down", "cycle"};
                o.insert(QStringLiteral("dpi"), QLatin1String(ids[b.dpi]));
                if (b.dpi == Binding::DpiClutch)
                    o.insert(QStringLiteral("clutchDpi"), b.clutchDpi);
            }
            if (b.isCommand())
                o.insert(QStringLiteral("command"), b.command);
            if (b.isMacro()) {
                QJsonArray steps;
                for (const MacroStep &st : b.macro) {
                    switch (st.kind) {
                    case MacroStep::Press: steps.append(QJsonObject{{QStringLiteral("press"), Keys::name(st.value)}}); break;
                    case MacroStep::Release: steps.append(QJsonObject{{QStringLiteral("release"), Keys::name(st.value)}}); break;
                    case MacroStep::Delay: steps.append(QJsonObject{{QStringLiteral("delay"), st.value}}); break;
                    }
                }
                o.insert(QStringLiteral("macro"), steps);
                if (b.macroMode != Binding::PlayOnce)
                    o.insert(QStringLiteral("macroMode"), b.macroMode == Binding::Toggle ? QStringLiteral("toggle")
                                                                                        : QStringLiteral("while-held"));
            }
            if (b.turbo > 0)
                o.insert(QStringLiteral("turbo"), b.turbo);
            if (b.isTapHold()) {
                QJsonArray hold;
                for (int t : b.hold)
                    hold.append(Keys::name(t));
                o.insert(QStringLiteral("hold"), hold);
                if (b.holdTime > 0 && b.holdTime != Keys::HoldTimeDefault)
                    o.insert(QStringLiteral("holdTime"), b.holdTime);
            }
            out.insert(Keys::triggerName(source), o);
        } else {
            out.insert(Keys::triggerName(source), targets);
        }
    }
    return out;
}

SensorSettings sensorFromJson(const QJsonObject &o)
{
    SensorSettings s;
    for (const QJsonValue &v : o.value(QStringLiteral("dpiStages")).toArray())
        if (s.stages.size() < Sensor::MaxStages && v.toInt() >= Sensor::MinDpi)
            s.stages << v.toInt();
    s.activeStage = std::clamp(o.value(QStringLiteral("activeStage")).toInt(1) - 1, 0,
                               std::max(0, int(s.stages.size()) - 1));
    s.pollRate = std::max(0, o.value(QStringLiteral("pollRate")).toInt());
    return s;
}

QJsonObject sensorToJson(const SensorSettings &s)
{
    QJsonArray stages;
    for (int d : s.stages)
        stages.append(d);
    QJsonObject o{{QStringLiteral("dpiStages"), stages}, {QStringLiteral("activeStage"), s.activeStage + 1}};
    if (s.pollRate > 0)
        o.insert(QStringLiteral("pollRate"), s.pollRate);
    return o;
}

LightingSettings lightingFromJson(const QJsonObject &o)
{
    LightingSettings l;
    l.effect = LightingSettings::effectFromId(o.value(QStringLiteral("effect")).toString());
    const QString colours = o.value(QStringLiteral("colours")).toString();
    l.colours = colours == QLatin1String("two")      ? LightingSettings::TwoColours
                : colours == QLatin1String("random") ? LightingSettings::RandomColours
                                                     : LightingSettings::OneColour;
    if (const QColor c(o.value(QStringLiteral("colour1")).toString()); c.isValid())
        l.colour1 = c;
    if (const QColor c(o.value(QStringLiteral("colour2")).toString()); c.isValid())
        l.colour2 = c;
    l.speed = std::clamp(o.value(QStringLiteral("speed")).toInt(2), 1, 3);
    l.direction = o.value(QStringLiteral("direction")).toString() == QLatin1String("left") ? 2 : 1;
    l.brightness = std::clamp(o.value(QStringLiteral("brightness")).toInt(100), 0, 100);
    const QJsonObject keys = o.value(QStringLiteral("keys")).toObject();
    for (auto it = keys.begin(); it != keys.end(); ++it)
        if (const int code = Keys::fromName(it.key()); code > 0)
            if (const QColor c(it.value().toString()); c.isValid())
                l.keys.insert(code, c);
    if (const QColor c(o.value(QStringLiteral("unlit")).toString()); c.isValid())
        l.unlit = c;
    if (const QColor c(o.value(QStringLiteral("edge")).toString()); c.isValid())
        l.edge = c;
    return l;
}

QJsonObject lightingToJson(const LightingSettings &l)
{
    QJsonObject o = {{QStringLiteral("effect"), LightingSettings::effectId(l.effect)},
            {QStringLiteral("colours"), l.colours == LightingSettings::TwoColours      ? QStringLiteral("two")
                                        : l.colours == LightingSettings::RandomColours ? QStringLiteral("random")
                                                                                       : QStringLiteral("one")},
            {QStringLiteral("colour1"), l.colour1.name()},
            {QStringLiteral("colour2"), l.colour2.name()},
            {QStringLiteral("speed"), l.speed},
            {QStringLiteral("direction"), l.direction == 2 ? QStringLiteral("left") : QStringLiteral("right")},
            {QStringLiteral("brightness"), l.brightness}};
    if (!l.keys.isEmpty() || l.effect == LightingSettings::Custom) {
        QJsonObject keys;
        for (auto it = l.keys.cbegin(); it != l.keys.cend(); ++it)
            keys.insert(Keys::name(it.key()), it->name());
        o.insert(QStringLiteral("keys"), keys);
        o.insert(QStringLiteral("unlit"), l.unlit.name());
        o.insert(QStringLiteral("edge"), l.edge.name());
    }
    return o;
}

QString tr(const char *text)
{
    return QCoreApplication::translate("Hotkey", text);
}

} // namespace

bool DeviceProfile::hasHypershiftKey() const
{
    return std::any_of(bindings.cbegin(), bindings.cend(), [](const Binding &b) { return b.triggersHypershift(); });
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
    trayBattery = battery.value(QStringLiteral("tray")).toBool(true);
    autoSwitch = root.value(QStringLiteral("autoSwitch")).toBool(true);
    const QJsonObject lightsOff = root.value(QStringLiteral("lightsOff")).toObject();
    lightsOffIdle = lightsOff.value(QStringLiteral("idle")).toBool(true);
    lightsOffIdleMinutes = std::clamp(lightsOff.value(QStringLiteral("minutes")).toInt(15), 1, 240);
    lightsOffLocked = lightsOff.value(QStringLiteral("locked")).toBool(true);

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
            ds.autoRestore = d.value(QStringLiteral("autoRestore")).toString();
            for (const QJsonValue &pv : d.value(QStringLiteral("profiles")).toArray()) {
                const QJsonObject p = pv.toObject();
                const QString name = p.value(QStringLiteral("name")).toString().trimmed();
                if (name.isEmpty() || findProfile(key, name))
                    continue;
                ds.profiles << profileFromJson(p);
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
                ds.profiles << Profile{name, bindingsFromJson(d.value(QStringLiteral("bindings")).toObject()), {}, {}, {}, {}};
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
            ds.profiles << Profile{DefaultProfile, bindingsFromJson(d.value(QStringLiteral("bindings")).toObject()), {}, {}, {}, {}};
        }
    }

    for (auto it = m_devices.begin(); it != m_devices.end(); ++it) {
        if (it->profiles.isEmpty())
            it->profiles << Profile{DefaultProfile, {}, {}, {}, {}, {}};
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
        for (const Profile &p : ds.profiles)
            profiles.append(profileToJson(p));
        QJsonObject d{{QStringLiteral("name"), key},
                      {QStringLiteral("enabled"), ds.enabled},
                      {QStringLiteral("activeProfile"), ds.active},
                      {QStringLiteral("profiles"), profiles}};
        if (!ds.autoRestore.isEmpty())
            d.insert(QStringLiteral("autoRestore"), ds.autoRestore);
        devices.append(d);
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
        {QStringLiteral("autoSwitch"), autoSwitch},
        {QStringLiteral("lightsOff"), QJsonObject{{QStringLiteral("idle"), lightsOffIdle},
                                                  {QStringLiteral("minutes"), lightsOffIdleMinutes},
                                                  {QStringLiteral("locked"), lightsOffLocked}}},
        {QStringLiteral("battery"), QJsonObject{{QStringLiteral("alerts"), batteryAlerts},
                                                {QStringLiteral("threshold"), batteryThreshold},
                                                {QStringLiteral("tray"), trayBattery}}},
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

Config::Profile Config::profileFromJson(const QJsonObject &p)
{
    QStringList programs;
    for (const QJsonValue &v : p.value(QStringLiteral("programs")).toArray())
        if (const QString prog = v.toString().trimmed(); !prog.isEmpty() && !programs.contains(prog))
            programs << prog;
    return Profile{p.value(QStringLiteral("name")).toString().trimmed(),
                   bindingsFromJson(p.value(QStringLiteral("bindings")).toObject()),
                   bindingsFromJson(p.value(QStringLiteral("hypershift")).toObject()), programs,
                   sensorFromJson(p.value(QStringLiteral("sensor")).toObject()),
                   lightingFromJson(p.value(QStringLiteral("lighting")).toObject())};
}

QJsonObject Config::profileToJson(const Profile &p)
{
    QJsonObject o{{QStringLiteral("name"), p.name}, {QStringLiteral("bindings"), bindingsToJson(p.bindings)}};
    if (!p.hypershift.isEmpty())
        o.insert(QStringLiteral("hypershift"), bindingsToJson(p.hypershift));
    if (!p.programs.isEmpty())
        o.insert(QStringLiteral("programs"), QJsonArray::fromStringList(p.programs));
    if (p.sensor.isValid())
        o.insert(QStringLiteral("sensor"), sensorToJson(p.sensor));
    if (p.lighting.isValid())
        o.insert(QStringLiteral("lighting"), lightingToJson(p.lighting));
    return o;
}

QByteArray Config::exportProfile(const QString &device, const QString &name) const
{
    const Profile *p = findProfile(device, name);
    if (!p)
        return {};
    const QJsonObject root{{QStringLiteral("razerRebind"), QStringLiteral("profile")},
                           {QStringLiteral("version"), 1},
                           {QStringLiteral("device"), device},
                           {QStringLiteral("profile"), profileToJson(*p)}};
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool Config::readProfileFile(const QByteArray &data, QString *name, QString *fromDevice, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    const QJsonObject root = doc.object();
    if (!doc.isObject() || root.value(QStringLiteral("razerRebind")).toString() != QLatin1String("profile")) {
        if (error)
            *error = doc.isObject() ? QCoreApplication::translate("Config", "This isn't a Razer Rebind profile file.")
                                    : parseError.errorString();
        return false;
    }
    if (name)
        *name = root.value(QStringLiteral("profile")).toObject().value(QStringLiteral("name")).toString().trimmed();
    if (fromDevice)
        *fromDevice = root.value(QStringLiteral("device")).toString();
    return true;
}

QStringList Config::profileFileCommands(const QByteArray &data)
{
    const Profile p = profileFromJson(QJsonDocument::fromJson(data).object().value(QStringLiteral("profile")).toObject());
    QStringList out;
    for (const BindingMap *layer : {&p.bindings, &p.hypershift})
        for (auto it = layer->cbegin(); it != layer->cend(); ++it)
            if (it->isCommand() && !out.contains(it->command))
                out << it->command;
    return out;
}

QString Config::importProfile(const QString &device, const QByteArray &data, QString *error)
{
    if (!readProfileFile(data, nullptr, nullptr, error))
        return {};
    ensureDevice(device);
    Profile p = profileFromJson(QJsonDocument::fromJson(data).object().value(QStringLiteral("profile")).toObject());
    if (p.name.isEmpty())
        p.name = QCoreApplication::translate("Config", "Imported");
    // "Gaming" -> "Gaming (2)" if the device already has one.
    const QString base = p.name;
    for (int n = 2; findProfile(device, p.name); ++n)
        p.name = QStringLiteral("%1 (%2)").arg(base).arg(n);
    // A program can switch a device to only one profile: keep existing links.
    p.programs.erase(std::remove_if(p.programs.begin(), p.programs.end(),
                                    [&](const QString &prog) {
                                        for (const Profile &other : m_devices.value(device).profiles)
                                            if (other.programs.contains(prog, Qt::CaseInsensitive))
                                                return true;
                                        return false;
                                    }),
                     p.programs.end());
    // Without saved DPI, switching to it shouldn't change the mouse.
    if (const Profile *active = findProfile(device, activeProfile(device))) {
        if (!p.sensor.isValid())
            p.sensor = active->sensor;
        if (!p.lighting.isValid())
            p.lighting = active->lighting;
    }
    m_devices[device].profiles << p;
    return p.name;
}

bool Config::ensureDevice(const QString &device)
{
    if (device.isEmpty() || m_devices.contains(device))
        return false;
    m_deviceOrder << device;
    DeviceSettings &ds = m_devices[device];
    ds.profiles << Profile{DefaultProfile, {}, {}, {}, {}, {}};
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
        out.sensor = p->sensor;
        out.lighting = p->lighting;
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
            p.sensor = profile.sensor;
            p.lighting = profile.lighting;
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

QStringList Config::programs(const QString &device, const QString &profile) const
{
    const Profile *p = findProfile(device, profile);
    return p ? p->programs : QStringList();
}

void Config::setPrograms(const QString &device, const QString &profile, const QStringList &programs)
{
    for (Profile &p : m_devices[device].profiles)
        if (p.name == profile)
            p.programs = programs;
}

void Config::fillSensor(const QString &device, const SensorSettings &settings)
{
    for (Profile &p : m_devices[device].profiles)
        if (!p.sensor.isValid())
            p.sensor = settings;
}

void Config::fillLighting(const QString &device, const LightingSettings &settings)
{
    for (Profile &p : m_devices[device].profiles)
        if (!p.lighting.isValid())
            p.lighting = settings;
}

void Config::setAutoRestore(const QString &device, const QString &profile)
{
    if (m_devices.contains(device))
        m_devices[device].autoRestore = profile;
}

bool Config::addProfile(const QString &device, const QString &name, const QString &copyFrom)
{
    if (name.trimmed().isEmpty() || findProfile(device, name))
        return false;
    ensureDevice(device);
    const Profile *source = findProfile(device, copyFrom);
    // A copy doesn't take the programs: two profiles claiming one game would fight.
    const Profile *active = findProfile(device, activeProfile(device));
    const SensorSettings sensor = source ? source->sensor : active ? active->sensor : SensorSettings();
    const LightingSettings lighting = source ? source->lighting : active ? active->lighting : LightingSettings();
    m_devices[device].profiles << (source ? Profile{name, source->bindings, source->hypershift, {}, sensor, lighting}
                                          : Profile{name, {}, {}, {}, sensor, lighting});
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
    if (ds.autoRestore == from)
        ds.autoRestore = to;
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
