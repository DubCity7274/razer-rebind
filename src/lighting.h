#pragma once

#include "razerdevice.h"

#include <QColor>
#include <QHash>
#include <QStringList>

// A device's lighting: one effect for its main LEDs (a keyboard's keys, a
// mouse's logo) and their brightness.
struct LightingSettings
{
    enum Effect { None, Off, Static, Breathing, Spectrum, Wave, Reactive, Starlight, Ripple, Custom };
    enum Colours { OneColour, TwoColours, RandomColours };

    Effect effect = None;  // None = leave the lighting alone
    Colours colours = OneColour;
    QColor colour1 = QColor(0x44, 0xd6, 0x2c);
    QColor colour2 = QColor(0x00, 0x80, 0xff);
    int speed = 2;         // 1 fast .. 3 slow (reactive: how long a key stays lit)
    int direction = 1;     // wave: 1 = right, 2 = left
    int brightness = 100;  // percent
    // Custom: a colour per key (evdev code), the rest in `unlit`, and the
    // light strips in `edge`.
    QHash<int, QColor> keys;
    QColor unlit = Qt::black;
    QColor edge = Qt::black;

    bool isValid() const { return effect != None; }
    bool operator==(const LightingSettings &o) const;
    bool operator!=(const LightingSettings &o) const { return !(*this == o); }

    static QString effectName(Effect e); // "Breathing"
    static QString effectId(Effect e);   // "breathing", for the config
    static Effect effectFromId(const QString &id);
};

// Reads and sets a device's lighting through openrazer-daemon when it runs
// (it then remembers the lighting across reconnects), otherwise through
// OpenRazer's sysfs attributes.
class Lighting
{
public:
    explicit Lighting(const RazerDevice &device);

    // The effects the device offers; empty when it has no lighting.
    QList<LightingSettings::Effect> effects() const;
    bool hasColours(LightingSettings::Effect e) const; // two colours or random
    bool hasBrightness() const;

    bool read(LightingSettings *out, QString *error = nullptr) const;
    bool write(const LightingSettings &settings, QString *error = nullptr) const;

private:
    bool daemonAvailable() const;
    bool hasAttr(const char *keyboardName) const; // matrix_effect_X, or the logo_ one on a mouse
    QString attrPath(const char *name) const;

    RazerDevice m_device;
    QString m_daemonPath;
    bool m_logo = false; // mice: the logo is the lit part
};
