#pragma once

#include "lighting.h"
#include "razerdevice.h"

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

private:
    void display(const LightingSettings &settings);
    LightingSettings fromWidgets() const;
    void updateVisibility();
    void commit();
    void setColour(QPushButton *button, const QColor &colour);

    Config *m_config;
    RemapManager *m_manager;
    RazerDevice m_device;
    LightingSettings m_current; // read from the device
    QColor m_colour1, m_colour2;

    QComboBox *m_effect;
    QComboBox *m_colours;
    QPushButton *m_colour1Button;
    QPushButton *m_colour2Button;
    QComboBox *m_speed;
    QComboBox *m_direction;
    QLabel *m_brightnessLabel;
    QSlider *m_brightness;
    QLabel *m_note;
    QTimer m_commitTimer;
};
