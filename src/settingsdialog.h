#pragma once

#include <QDialog>

class QLabel;
class BatteryMonitor;
class Config;

// Notification and battery alert settings; changes are saved immediately.
class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    SettingsDialog(Config *config, BatteryMonitor *battery, QWidget *parent = nullptr);

private:
    void save();
    void refreshLevels();

    Config *m_config;
    BatteryMonitor *m_battery;
    QLabel *m_levels;
};
