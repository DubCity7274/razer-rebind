#pragma once

#include <QObject>
#include <QTimer>

class Config;
class RemapManager;

// Turns every device's lighting off after a while without input from the
// Razer devices, and while the screen is locked; the next input (or the
// unlock) brings it back.
class IdleLights : public QObject
{
    Q_OBJECT

public:
    IdleLights(RemapManager *manager, Config *config, QObject *parent = nullptr);

public slots:
    void update(); // re-check, e.g. after the settings changed

private slots:
    void onScreenSaverActive(bool active);

private:
    void onActivity();

    RemapManager *m_manager;
    Config *m_config;
    QTimer m_timer;
    bool m_locked = false;
    bool m_idle = false;
};
