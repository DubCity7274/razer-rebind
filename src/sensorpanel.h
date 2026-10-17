#pragma once

#include "razerdevice.h"
#include "sensor.h"

#include <QTimer>

#include <atomic>
#include <memory>
#include <QWidget>

class QButtonGroup;
class QComboBox;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QRadioButton;
class QSpinBox;
class Config;
class RemapManager;

// DPI stages and polling rate of the selected mouse, saved with its active
// profile. Hidden for devices without an adjustable DPI.
class SensorPanel : public QWidget
{
    Q_OBJECT

public:
    SensorPanel(Config *config, RemapManager *manager, QWidget *parent = nullptr);

    // Show the device's settings; reads the mouse, so call on selection or
    // profile changes, not on every refresh.
    void setDevice(const RazerDevice *device);

private:
    void display(const SensorSettings &settings);
    void rebuildStages(int count);
    SensorSettings fromWidgets() const;
    void commit();
    void updateNote();
    void poll();
    void onPolled(const QString &hidPath, const SensorSettings &now);

    Config *m_config;
    RemapManager *m_manager;
    RazerDevice m_device;
    int m_maxDpi = 16000;
    bool m_hasStages = true;
    SensorSettings m_current; // read from the mouse

    QHBoxLayout *m_stageRow;
    QButtonGroup *m_stageGroup;
    QList<QRadioButton *> m_stageActive;
    QList<QSpinBox *> m_stageDpi;
    QPushButton *m_add;
    QPushButton *m_remove;
    QComboBox *m_pollRate;
    QLabel *m_note;
    QTimer m_commitTimer;
    QTimer m_pollTimer;
    std::shared_ptr<std::atomic<bool>> m_polling = std::make_shared<std::atomic<bool>>(false);
};
