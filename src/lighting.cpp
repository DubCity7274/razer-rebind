#include "lighting.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QFile>
#include <QFileInfo>

#include "ledmap.h"

#include <algorithm>

namespace {

const QString Service = QStringLiteral("org.razer");

const struct { LightingSettings::Effect effect; const char *id; const char *name; } Effects[] = {
    {LightingSettings::Off, "off", "Off"},
    {LightingSettings::Static, "static", "Static"},
    {LightingSettings::Breathing, "breathing", "Breathing"},
    {LightingSettings::Spectrum, "spectrum", "Spectrum"},
    {LightingSettings::Wave, "wave", "Wave"},
    {LightingSettings::Reactive, "reactive", "Reactive"},
    {LightingSettings::Starlight, "starlight", "Starlight"},
    {LightingSettings::Ripple, "ripple", "Ripple"},
    {LightingSettings::Custom, "custom", "Custom (per key)"},
};

QDBusMessage call(const QString &path, const QString &interface, const QString &method, const QVariantList &args = {})
{
    QDBusMessage m = QDBusMessage::createMethodCall(Service, path, interface, method);
    m.setArguments(args);
    return QDBusConnection::sessionBus().call(m, QDBus::Block, 3000);
}

bool ok(const QDBusMessage &reply, QString *error)
{
    if (reply.type() == QDBusMessage::ReplyMessage)
        return true;
    if (error)
        *error = reply.errorMessage();
    return false;
}

QVariantList rgb(const QColor &c)
{
    return {QVariant::fromValue(uchar(c.red())), QVariant::fromValue(uchar(c.green())), QVariant::fromValue(uchar(c.blue()))};
}

QByteArray rgbBytes(const QColor &c)
{
    return QByteArray{} + char(c.red()) + char(c.green()) + char(c.blue());
}

} // namespace

bool LightingSettings::operator==(const LightingSettings &o) const
{
    return effect == o.effect && colours == o.colours && colour1 == o.colour1 && colour2 == o.colour2
           && speed == o.speed && direction == o.direction && brightness == o.brightness && keys == o.keys
           && unlit == o.unlit && edge == o.edge;
}

QString LightingSettings::effectName(Effect e)
{
    for (const auto &x : Effects)
        if (x.effect == e)
            return QString::fromLatin1(x.name);
    return {};
}

QString LightingSettings::effectId(Effect e)
{
    for (const auto &x : Effects)
        if (x.effect == e)
            return QString::fromLatin1(x.id);
    return {};
}

LightingSettings::Effect LightingSettings::effectFromId(const QString &id)
{
    for (const auto &x : Effects)
        if (id == QLatin1String(x.id))
            return x.effect;
    return None;
}

Lighting::Lighting(const RazerDevice &device)
    : m_device(device)
    , m_logo(device.kind == RazerDevice::Mouse)
{
    const QString serial = device.attribute("device_serial");
    if (!serial.isEmpty())
        m_daemonPath = QStringLiteral("/org/razer/device/") + serial;
}

QString Lighting::attrPath(const char *name) const
{
    return m_device.hidPath + QLatin1Char('/') + (m_logo ? QStringLiteral("logo_") : QString()) + QLatin1String(name);
}

bool Lighting::hasAttr(const char *name) const
{
    return QFileInfo::exists(attrPath(name));
}

bool Lighting::daemonAvailable() const
{
    if (m_daemonPath.isEmpty())
        return false;
    QDBusConnectionInterface *bus = QDBusConnection::sessionBus().interface();
    return bus && bus->isServiceRegistered(Service);
}

QList<LightingSettings::Effect> Lighting::effects() const
{
    using L = LightingSettings;
    QList<L::Effect> out;
    if (!hasAttr("matrix_effect_none"))
        return out;
    out << L::Off;
    if (hasAttr("matrix_effect_static")) out << L::Static;
    if (hasAttr("matrix_effect_breath")) out << L::Breathing;
    if (hasAttr("matrix_effect_spectrum")) out << L::Spectrum;
    if (hasAttr("matrix_effect_wave")) out << L::Wave;
    if (hasAttr("matrix_effect_reactive")) out << L::Reactive;
    if (hasAttr("matrix_effect_starlight")) out << L::Starlight;
    // Ripple is drawn by the daemon frame by frame, on keyboards with a
    // per-key matrix.
    if (!m_logo && hasAttr("matrix_custom_frame") && daemonAvailable())
        out << L::Ripple;
    if (!m_logo && hasAttr("matrix_custom_frame") && !ledMap(m_device).isEmpty())
        out << L::Custom;
    return out;
}

bool Lighting::hasColours(LightingSettings::Effect e) const
{
    return e == LightingSettings::Breathing || e == LightingSettings::Starlight;
}

bool Lighting::hasBrightness() const
{
    return QFileInfo::exists(m_device.hidPath + (m_logo ? QStringLiteral("/logo_led_brightness")
                                                         : QStringLiteral("/matrix_brightness")));
}

bool Lighting::read(LightingSettings *out, QString *error) const
{
    using L = LightingSettings;
    L s;
    // Only the daemon knows the current effect; the driver can't be asked.
    if (!daemonAvailable()) {
        if (error)
            *error = QStringLiteral("openrazer-daemon isn't running");
        return false;
    }
    const QString chroma = m_logo ? QStringLiteral("razer.device.lighting.logo") : QStringLiteral("razer.device.lighting.chroma");
    const QString prefix = m_logo ? QStringLiteral("getLogo") : QStringLiteral("get");
    const QDBusMessage effect = call(m_daemonPath, chroma, prefix + QStringLiteral("Effect"));
    if (!ok(effect, error))
        return false;
    const QString name = effect.arguments().value(0).toString();
    const QByteArray colours = call(m_daemonPath, chroma, prefix + QStringLiteral("EffectColors")).arguments().value(0).toByteArray();
    if (colours.size() >= 3)
        s.colour1 = QColor(uchar(colours[0]), uchar(colours[1]), uchar(colours[2]));
    if (colours.size() >= 6)
        s.colour2 = QColor(uchar(colours[3]), uchar(colours[4]), uchar(colours[5]));
    s.speed = std::clamp(call(m_daemonPath, chroma, prefix + QStringLiteral("EffectSpeed")).arguments().value(0).toInt(), 1, 3);
    s.direction = std::clamp(call(m_daemonPath, chroma, prefix + QStringLiteral("WaveDir")).arguments().value(0).toInt(), 1, 2);

    // The daemon's names: none, static, breathSingle/Dual/Random, spectrum,
    // wave, reactive, starlightSingle/Dual/Random, ripple(Random)...
    s.effect = name == QLatin1String("none")            ? L::Off
               : name == QLatin1String("static")         ? L::Static
               : name.startsWith(QLatin1String("breath")) ? L::Breathing
               : name == QLatin1String("spectrum")       ? L::Spectrum
               : name == QLatin1String("wave")           ? L::Wave
               : name == QLatin1String("reactive")       ? L::Reactive
               : name.startsWith(QLatin1String("starlight")) ? L::Starlight
               : name.startsWith(QLatin1String("ripple"))    ? L::Ripple
               : name == QLatin1String("custom")             ? L::Custom
                                                              : L::Spectrum;
    s.colours = name.endsWith(QLatin1String("Dual"))     ? L::TwoColours
                : name.endsWith(QLatin1String("Random")) ? L::RandomColours
                                                         : L::OneColour;
    const QDBusMessage b = m_logo ? call(m_daemonPath, chroma, QStringLiteral("getLogoBrightness"))
                                  : call(m_daemonPath, QStringLiteral("razer.device.lighting.brightness"), QStringLiteral("getBrightness"));
    if (b.type() == QDBusMessage::ReplyMessage)
        s.brightness = std::clamp(int(b.arguments().value(0).toDouble() + 0.5), 0, 100);
    *out = s;
    return true;
}

namespace {

// Each row of a custom frame: the colours of its columns.
QList<QByteArray> customFrame(const LedMap &map, const LightingSettings &s)
{
    QList<QByteArray> rows(map.rows);
    for (int r = 0; r < map.rows; ++r) {
        const QColor fill = map.edgeRows.contains(r) ? s.edge : s.unlit;
        for (int c = 0; c < map.columns; ++c)
            rows[r] += rgbBytes(fill);
    }
    for (auto it = map.edges.cbegin(); it != map.edges.cend(); ++it) {
        const QColor c = s.keys.value(it.key(), s.edge);
        QByteArray &row = rows[it->y()];
        row[it->x() * 3] = char(c.red());
        row[it->x() * 3 + 1] = char(c.green());
        row[it->x() * 3 + 2] = char(c.blue());
    }
    for (auto it = map.keys.cbegin(); it != map.keys.cend(); ++it) {
        const QColor c = s.keys.value(it.key(), s.unlit);
        QByteArray &row = rows[it->y()];
        row[it->x() * 3] = char(c.red());
        row[it->x() * 3 + 1] = char(c.green());
        row[it->x() * 3 + 2] = char(c.blue());
    }
    return rows;
}

} // namespace

bool Lighting::write(const LightingSettings &s, QString *error) const
{
    using L = LightingSettings;
    if (!s.isValid())
        return true;
    // Reactive takes 1 (short) .. 4 (long); starlight 1 (fast) .. 3 (slow).
    const int speed = std::clamp(s.speed, 1, 3);

    if (daemonAvailable()) {
        const QString iface = m_logo ? QStringLiteral("razer.device.lighting.logo") : QStringLiteral("razer.device.lighting.chroma");
        const QString set = m_logo ? QStringLiteral("setLogo") : QStringLiteral("set");
        QDBusMessage reply;
        switch (s.effect) {
        case L::None: return true;
        case L::Off: reply = call(m_daemonPath, iface, set + QStringLiteral("None")); break;
        case L::Static: reply = call(m_daemonPath, iface, set + QStringLiteral("Static"), rgb(s.colour1)); break;
        case L::Spectrum: reply = call(m_daemonPath, iface, set + QStringLiteral("Spectrum")); break;
        case L::Wave: reply = call(m_daemonPath, iface, set + QStringLiteral("Wave"), {s.direction}); break;
        case L::Reactive:
            reply = call(m_daemonPath, iface, set + QStringLiteral("Reactive"), rgb(s.colour1) << QVariant::fromValue(uchar(speed)));
            break;
        case L::Breathing:
            if (s.colours == L::RandomColours)
                reply = call(m_daemonPath, iface, set + QStringLiteral("BreathRandom"));
            else if (s.colours == L::TwoColours)
                reply = call(m_daemonPath, iface, set + QStringLiteral("BreathDual"), rgb(s.colour1) + rgb(s.colour2));
            else
                reply = call(m_daemonPath, iface, set + QStringLiteral("BreathSingle"), rgb(s.colour1));
            break;
        case L::Starlight:
            if (s.colours == L::RandomColours)
                reply = call(m_daemonPath, iface, set + QStringLiteral("StarlightRandom"), {QVariant::fromValue(uchar(speed))});
            else if (s.colours == L::TwoColours)
                reply = call(m_daemonPath, iface, set + QStringLiteral("StarlightDual"),
                             rgb(s.colour1) + rgb(s.colour2) << QVariant::fromValue(uchar(speed)));
            else
                reply = call(m_daemonPath, iface, set + QStringLiteral("StarlightSingle"),
                             rgb(s.colour1) << QVariant::fromValue(uchar(speed)));
            break;
        case L::Custom: {
            // One row at a time (row, first column, last column, colours),
            // then show the frame.
            const LedMap map = ledMap(m_device);
            const QList<QByteArray> rows = customFrame(map, s);
            for (int r = 0; r < rows.size(); ++r) {
                const QByteArray row = QByteArray{} + char(r) + char(0) + char(map.columns - 1) + rows[r];
                reply = call(m_daemonPath, iface, QStringLiteral("setKeyRow"), {row});
                if (!ok(reply, error))
                    return false;
            }
            reply = call(m_daemonPath, iface, QStringLiteral("setCustom"));
            break;
        }
        case L::Ripple:
            // Frames per second of the daemon's animation.
            reply = s.colours == L::RandomColours
                        ? call(m_daemonPath, QStringLiteral("razer.device.lighting.custom"), QStringLiteral("setRippleRandomColour"), {0.05})
                        : call(m_daemonPath, QStringLiteral("razer.device.lighting.custom"), QStringLiteral("setRipple"),
                               rgb(s.colour1) << 0.05);
            break;
        }
        if (!ok(reply, error))
            return false;
        if (hasBrightness()) {
            reply = m_logo ? call(m_daemonPath, iface, QStringLiteral("setLogoBrightness"), {double(s.brightness)})
                           : call(m_daemonPath, QStringLiteral("razer.device.lighting.brightness"), QStringLiteral("setBrightness"),
                                  {double(s.brightness)});
            if (!ok(reply, error))
                return false;
        }
        return true;
    }

    // Without the daemon: the driver's attributes take raw bytes.
    auto writeAttr = [&](const char *name, const QByteArray &data) {
        QFile f(attrPath(name));
        if (f.open(QIODevice::WriteOnly) && f.write(data) == data.size())
            return true;
        if (error)
            *error = QStringLiteral("%1: %2").arg(f.fileName(), f.errorString());
        return false;
    };
    bool done = false;
    switch (s.effect) {
    case L::None: return true;
    case L::Off: done = writeAttr("matrix_effect_none", "1"); break;
    case L::Static: done = writeAttr("matrix_effect_static", rgbBytes(s.colour1)); break;
    case L::Spectrum: done = writeAttr("matrix_effect_spectrum", "1"); break;
    case L::Wave: done = writeAttr("matrix_effect_wave", QByteArray::number(s.direction)); break;
    case L::Reactive: done = writeAttr("matrix_effect_reactive", char(speed) + rgbBytes(s.colour1)); break;
    case L::Breathing:
        done = writeAttr("matrix_effect_breath", s.colours == L::RandomColours ? QByteArray("1")
                                                 : s.colours == L::TwoColours  ? rgbBytes(s.colour1) + rgbBytes(s.colour2)
                                                                               : rgbBytes(s.colour1));
        break;
    case L::Starlight:
        done = writeAttr("matrix_effect_starlight", s.colours == L::RandomColours ? QByteArray(1, char(speed))
                                                    : s.colours == L::TwoColours
                                                        ? char(speed) + rgbBytes(s.colour1) + rgbBytes(s.colour2)
                                                        : char(speed) + rgbBytes(s.colour1));
        break;
    case L::Ripple:
        if (error)
            *error = QStringLiteral("Ripple needs openrazer-daemon");
        return false;
    case L::Custom: {
        const LedMap map = ledMap(m_device);
        const QList<QByteArray> rows = customFrame(map, s);
        done = true;
        for (int r = 0; r < rows.size() && done; ++r)
            done = writeAttr("matrix_custom_frame", QByteArray{} + char(r) + char(0) + char(map.columns - 1) + rows[r]);
        done = done && writeAttr("matrix_effect_custom", "1");
        break;
    }
    }
    if (!done)
        return false;
    if (hasBrightness()) {
        QFile f(m_device.hidPath + (m_logo ? QStringLiteral("/logo_led_brightness") : QStringLiteral("/matrix_brightness")));
        if (!f.open(QIODevice::WriteOnly) || f.write(QByteArray::number(s.brightness * 255 / 100)) < 0) {
            if (error)
                *error = f.errorString();
            return false;
        }
    }
    return true;
}
