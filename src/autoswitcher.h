#pragma once

#include <QHash>
#include <QObject>
#include <QTimer>

class Config;
class RemapManager;

// Switches a device to a profile while one of the programs linked to that
// profile is running, and back to the profile it had before when the last one
// exits. When several linked programs run, the most recently started wins.
// Switching by hand while a program runs is respected until it exits.
class AutoSwitcher : public QObject
{
    Q_OBJECT

public:
    AutoSwitcher(RemapManager *manager, Config *config, QObject *parent = nullptr);

    // The program that switched the device to its current profile, or empty.
    QString activeProgram(const QString &device) const { return m_program.value(device); }

public slots:
    // Re-check now, e.g. after the programs or the setting changed.
    void update();

signals:
    void changed();

private:
    RemapManager *m_manager;
    Config *m_config;
    QTimer m_timer;
    QHash<QString, QString> m_program; // device -> program it was switched for
    QHash<QString, QString> m_profile; // device -> profile it was switched to
};
