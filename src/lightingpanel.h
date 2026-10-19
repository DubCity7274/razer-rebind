#pragma once

#include "lighting.h"
#include "razerdevice.h"

#include <QColor>
#include <QHash>
#include <QTimer>
#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;
class QSlider;
class Config;
class RemapManager;

// The selected device's lighting effect, saved with its active profile.
// Hidden for devices without lighting.
class LightingPanel : public QWidget
{
    Q_OBJECT

public:
    LightingPanel(Config *config, RemapManager *manager, QWidget *parent = nullptr);

    void setDevice(const RazerDevice *device);

    // Custom effect: while painting, clicking a key on the picture paints it
    // with the brush colour, right-clicking makes it unlit again.
    bool isPainting() const;
    void paintKey(int code, bool erase);

signals:
    void paintingChanged(bool painting);
    // What the keys and edges look like now, to show on the picture; empty
    // when the effect isn't a fixed colour per key.
    void previewChanged(const QHash<int, QColor> &keys);

private:
    void display(const LightingSettings &settings);
    LightingSettings fromWidgets() const;
    void updateVisibility();
    void commit();
    void setColour(QPushButton *button, const QColor &colour);
    void emitPreview();

    Config *m_config;
    RemapManager *m_manager;
    RazerDevice m_device;
    LightingSettings m_current; // read from the device
    QColor m_colour1, m_colour2;
    QColor m_brush = QColor(0xff, 0x30, 0x30);
    QColor m_unlit = Qt::black;
    QColor m_edge = Qt::black;
    QHash<int, QColor> m_keys;

    QComboBox *m_effect;
    QComboBox *m_colours;
    QPushButton *m_colour1Button;
    QPushButton *m_colour2Button;
    QComboBox *m_speed;
    QComboBox *m_direction;
    QPushButton *m_paint;
    QPushButton *m_brushButton;
    QPushButton *m_unlitButton;
    QPushButton *m_edgeButton;
    QPushButton *m_clearKeys;
    QLabel *m_brightnessLabel;
    QSlider *m_brightness;
    QLabel *m_note;
    QTimer m_commitTimer;
};
