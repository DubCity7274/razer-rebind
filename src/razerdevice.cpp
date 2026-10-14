#include "razerdevice.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

static QString readSysfs(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QByteArray data = f.readAll();
    data.replace('\0', "");
    return QString::fromUtf8(data).trimmed();
}

QString RazerDevice::attribute(const char *attr) const
{
    return readSysfs(hidPath + QLatin1Char('/') + QLatin1String(attr));
}

namespace {

struct Interface
{
    QString hidName;   // "0003:1532:0226.0003"
    QString realPath;  // canonical sysfs path of the HID device
    QString usbDevice; // canonical sysfs path of the parent USB device
    QString vidPid;    // "1532:0226"
};

QStringList eventNodesOf(const QString &hidPath)
{
    QStringList nodes;
    const QDir inputDir(hidPath + QStringLiteral("/input"));
    for (const QString &input : inputDir.entryList({QStringLiteral("input*")}, QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QDir dir(inputDir.filePath(input));
        for (const QString &ev : dir.entryList({QStringLiteral("event*")}, QDir::Dirs | QDir::NoDotAndDotDot))
            nodes << QStringLiteral("/dev/input/") + ev;
    }
    return nodes;
}

} // namespace

QList<RazerDevice> discoverRazerDevices()
{
    static const QRegularExpression connectionSuffix(QStringLiteral("\\s*\\((Wired|Wireless|Receiver)\\)\\s*$"));
    const struct { const char *driver; RazerDevice::Kind kind; } drivers[] = {
        {"razerkbd", RazerDevice::Keyboard},
        {"razermouse", RazerDevice::Mouse},
    };

    QList<RazerDevice> result;
    for (const auto &drv : drivers) {
        const QDir driverDir(QStringLiteral("/sys/bus/hid/drivers/") + QLatin1String(drv.driver));
        QList<Interface> interfaces;
        for (const QString &entry : driverDir.entryList({QStringLiteral("0003:*")}, QDir::AllEntries | QDir::System)) {
            Interface iface;
            iface.hidName = entry;
            iface.realPath = QFileInfo(driverDir.filePath(entry)).canonicalFilePath();
            iface.usbDevice = QFileInfo(iface.realPath + QStringLiteral("/../..")).canonicalFilePath();
            iface.vidPid = entry.section(QLatin1Char(':'), 1, 2).section(QLatin1Char('.'), 0, 0);
            if (!iface.realPath.isEmpty())
                interfaces << iface;
        }

        QSet<QString> seenUsbDevices;
        for (const Interface &main : interfaces) {
            // Only one interface per device carries the OpenRazer control attributes.
            const QString type = readSysfs(main.realPath + QStringLiteral("/device_type"));
            if (type.isEmpty() || seenUsbDevices.contains(main.usbDevice))
                continue;
            seenUsbDevices.insert(main.usbDevice);

            RazerDevice dev;
            dev.kind = drv.kind;
            dev.name = type;
            dev.profileKey = QString(type).remove(connectionSuffix);
            dev.driver = QLatin1String(drv.driver);
            dev.hidPath = main.realPath;
            dev.vendorId = main.vidPid.section(QLatin1Char(':'), 0, 0).toUShort(nullptr, 16);
            dev.productId = main.vidPid.section(QLatin1Char(':'), 1, 1).toUShort(nullptr, 16);

            // Keys and buttons are spread across all of the device's HID
            // interfaces (e.g. media keys arrive on a different node than
            // letters), so collect the event nodes of every sibling interface.
            for (const Interface &sibling : interfaces)
                if (sibling.usbDevice == main.usbDevice && sibling.vidPid == main.vidPid)
                    dev.eventNodes << eventNodesOf(sibling.realPath);
            dev.eventNodes.sort();

            if (!dev.eventNodes.isEmpty())
                result << dev;
        }
    }
    return result;
}
