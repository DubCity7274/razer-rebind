#include "sensor.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QFile>
#include <QFileInfo>

#include <algorithm>

namespace {

const QString Service = QStringLiteral("org.razer");
const QString DpiInterface = QStringLiteral("razer.device.dpi");
const QString MiscInterface = QStringLiteral("razer.device.misc");

struct DpiPair
{
    quint16 x = 0;
    quint16 y = 0;
};

} // namespace

Q_DECLARE_METATYPE(DpiPair)

namespace {

QDBusArgument &operator<<(QDBusArgument &a, const DpiPair &p)
{
    a.beginStructure();
    a << p.x << p.y;
    a.endStructure();
    return a;
}

const QDBusArgument &operator>>(const QDBusArgument &a, DpiPair &p)
{
    a.beginStructure();
    a >> p.x >> p.y;
    a.endStructure();
    return a;
}

void registerTypes()
{
    static const bool done = [] {
        qDBusRegisterMetaType<DpiPair>();
        qDBusRegisterMetaType<QList<DpiPair>>();
        return true;
    }();
    Q_UNUSED(done);
}

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

QByteArray readRaw(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

bool writeRaw(const QString &path, const QByteArray &data, QString *error)
{
    QFile f(path);
    if (f.open(QIODevice::WriteOnly) && f.write(data) == data.size())
        return true;
    if (error)
        *error = QStringLiteral("%1: %2").arg(path, f.errorString());
    return false;
}

void appendBigEndian16(QByteArray &out, int value)
{
    out.append(char((value >> 8) & 0xff));
    out.append(char(value & 0xff));
}

} // namespace

Sensor::Sensor(const RazerDevice &device)
    : m_device(device)
{
    registerTypes();
    // openrazer-daemon names devices by serial number.
    const QString serial = device.attribute("device_serial");
    if (!serial.isEmpty())
        m_daemonPath = QStringLiteral("/org/razer/device/") + serial;
}

bool Sensor::daemonAvailable() const
{
    if (m_daemonPath.isEmpty())
        return false;
    QDBusConnectionInterface *bus = QDBusConnection::sessionBus().interface();
    if (!bus || !bus->isServiceRegistered(Service))
        return false;
    return call(m_daemonPath, DpiInterface, QStringLiteral("maxDPI")).type() == QDBusMessage::ReplyMessage;
}

bool Sensor::isSupported() const
{
    return m_device.kind == RazerDevice::Mouse && QFileInfo::exists(m_device.hidPath + QStringLiteral("/dpi"));
}

bool Sensor::hasStages() const
{
    return QFileInfo::exists(m_device.hidPath + QStringLiteral("/dpi_stages"));
}

int Sensor::maxDpi() const
{
    if (daemonAvailable()) {
        const QDBusMessage reply = call(m_daemonPath, DpiInterface, QStringLiteral("maxDPI"));
        if (reply.type() == QDBusMessage::ReplyMessage && reply.arguments().value(0).toInt() > 0)
            return reply.arguments().value(0).toInt();
    }
    // Without the daemon there's no list of sensors; 16000 is what most
    // current Razer mice reach, and the driver clamps what's too high.
    switch (m_device.productId) {
    case 0x007a: case 0x007b: // Viper Ultimate, wired and wireless
        return 20000;
    default:
        return 16000;
    }
}

QList<int> Sensor::pollRates() const
{
    return {125, 500, 1000};
}

bool Sensor::read(SensorSettings *out, QString *error) const
{
    SensorSettings s;
    if (daemonAvailable()) {
        const QDBusMessage rate = call(m_daemonPath, MiscInterface, QStringLiteral("getPollRate"));
        if (!ok(rate, error))
            return false;
        s.pollRate = rate.arguments().value(0).toInt();
        if (hasStages()) {
            const QDBusMessage reply = call(m_daemonPath, DpiInterface, QStringLiteral("getDPIStages"));
            if (!ok(reply, error))
                return false;
            // (y a(qq)): the active stage counted from 1, then x/y per stage.
            const QDBusArgument arg = reply.arguments().value(0).value<QDBusArgument>();
            uchar active = 0;
            arg.beginStructure();
            arg >> active;
            arg.beginArray();
            while (!arg.atEnd()) {
                DpiPair p;
                arg >> p;
                s.stages << p.x;
            }
            arg.endArray();
            arg.endStructure();
            s.activeStage = std::clamp(int(active) - 1, 0, std::max(0, int(s.stages.size()) - 1));
        }
        if (s.stages.isEmpty()) {
            const QDBusMessage reply = call(m_daemonPath, DpiInterface, QStringLiteral("getDPI"));
            if (!ok(reply, error))
                return false;
            const QList<int> dpi = qdbus_cast<QList<int>>(reply.arguments().value(0));
            if (!dpi.isEmpty())
                s.stages << dpi.first();
        }
    } else {
        s.pollRate = m_device.attribute("poll_rate").toInt();
        // dpi_stages: the active stage counted from 1, then x and y of each
        // stage as big-endian 16-bit numbers.
        const QByteArray stages = readRaw(m_device.hidPath + QStringLiteral("/dpi_stages"));
        if (stages.size() >= 5) {
            for (int i = 1; i + 3 < stages.size(); i += 4)
                s.stages << ((uchar(stages[i]) << 8) | uchar(stages[i + 1]));
            s.activeStage = std::clamp(int(uchar(stages[0])) - 1, 0, int(s.stages.size()) - 1);
        } else {
            // dpi reads as "x:y".
            const int dpi = m_device.attribute("dpi").section(QLatin1Char(':'), 0, 0).toInt();
            if (dpi > 0)
                s.stages << dpi;
        }
    }
    if (!s.isValid()) {
        if (error && error->isEmpty())
            *error = QStringLiteral("The mouse didn't report its DPI (asleep or switched off?)");
        return false;
    }
    *out = s;
    return true;
}

bool Sensor::write(const SensorSettings &in, QString *error) const
{
    if (!in.isValid())
        return true;
    SensorSettings s = in;
    const int max = maxDpi();
    for (int &d : s.stages)
        d = std::clamp(d, MinDpi, max);
    s.activeStage = std::clamp(s.activeStage, 0, int(s.stages.size()) - 1);

    if (daemonAvailable()) {
        QDBusMessage reply;
        if (hasStages()) {
            QList<DpiPair> pairs;
            for (int d : std::as_const(s.stages))
                pairs << DpiPair{quint16(d), quint16(d)};
            reply = call(m_daemonPath, DpiInterface, QStringLiteral("setDPIStages"),
                         {QVariant::fromValue(uchar(s.activeStage + 1)), QVariant::fromValue(pairs)});
        } else {
            reply = call(m_daemonPath, DpiInterface, QStringLiteral("setDPI"),
                         {QVariant::fromValue(quint16(s.dpi())), QVariant::fromValue(quint16(s.dpi()))});
        }
        if (!ok(reply, error))
            return false;
        if (s.pollRate > 0
            && !ok(call(m_daemonPath, MiscInterface, QStringLiteral("setPollRate"), {QVariant::fromValue(quint16(s.pollRate))}),
                   error))
            return false;
        return true;
    }

    if (hasStages()) {
        QByteArray data(1, char(s.activeStage + 1));
        for (int d : std::as_const(s.stages)) {
            appendBigEndian16(data, d);
            appendBigEndian16(data, d);
        }
        if (!writeRaw(m_device.hidPath + QStringLiteral("/dpi_stages"), data, error))
            return false;
    } else {
        QByteArray data;
        appendBigEndian16(data, s.dpi());
        appendBigEndian16(data, s.dpi());
        if (!writeRaw(m_device.hidPath + QStringLiteral("/dpi"), data, error))
            return false;
    }
    if (s.pollRate > 0
        && !writeRaw(m_device.hidPath + QStringLiteral("/poll_rate"), QByteArray::number(s.pollRate), error))
        return false;
    return true;
}
