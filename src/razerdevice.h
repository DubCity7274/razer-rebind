#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// A keyboard or mouse bound to one of the OpenRazer kernel drivers
// (razerkbd / razermouse). Found through the driver's sysfs tree.
struct RazerDevice
{
    enum Kind { Keyboard, Mouse };

    Kind kind = Keyboard;
    QString name;        // OpenRazer device_type, e.g. "Razer Viper Ultimate (Wireless)"
    QString profileKey;  // name without the "(Wired)/(Wireless)" suffix
    QString driver;      // "razerkbd" or "razermouse"
    QString hidPath;     // sysfs dir of the interface that exposes the OpenRazer attributes
    quint16 vendorId = 0;
    quint16 productId = 0;
    QStringList eventNodes;  // every /dev/input/eventN belonging to the device

    QString signature() const { return hidPath + QLatin1Char('|') + eventNodes.join(QLatin1Char(',')); }

    // Read an OpenRazer sysfs attribute (device_serial, firmware_version, ...).
    // Some of these do a USB round trip, so call them on demand only.
    QString attribute(const char *attr) const;
    // Wireless devices expose their charge through OpenRazer.
    bool hasBattery() const;
};

QList<RazerDevice> discoverRazerDevices();
