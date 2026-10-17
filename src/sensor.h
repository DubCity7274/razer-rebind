#pragma once

#include "razerdevice.h"

#include <QList>
#include <QString>

// A mouse's DPI stages and polling rate.
struct SensorSettings
{
    QList<int> stages;   // DPI of each stage, 1 to 5 of them
    int activeStage = 0; // index into stages
    int pollRate = 0;    // Hz; 0 = leave as it is

    bool isValid() const { return !stages.isEmpty(); }
    int dpi() const { return stages.value(activeStage); }
    bool operator==(const SensorSettings &o) const
    {
        return stages == o.stages && activeStage == o.activeStage && pollRate == o.pollRate;
    }
    bool operator!=(const SensorSettings &o) const { return !(*this == o); }
};

// Reads and changes a mouse's DPI and polling rate. Goes through
// openrazer-daemon when it runs, so the daemon remembers the settings and
// restores them when the mouse reconnects; otherwise writes OpenRazer's sysfs
// attributes directly. Every call is a USB round trip to the mouse.
class Sensor
{
public:
    explicit Sensor(const RazerDevice &device);

    bool isSupported() const;   // the mouse has an adjustable DPI
    bool hasStages() const;     // ...and DPI stages (the button under the mouse)
    int maxDpi() const;
    QList<int> pollRates() const;

    bool read(SensorSettings *out, QString *error = nullptr) const;
    bool write(const SensorSettings &settings, QString *error = nullptr) const;

    static constexpr int MinDpi = 100;
    static constexpr int MaxStages = 5;

private:
    bool daemonAvailable() const;

    RazerDevice m_device;
    QString m_daemonPath; // D-Bus object of the device in openrazer-daemon, if any
};
