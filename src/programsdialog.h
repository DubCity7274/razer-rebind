#pragma once

#include <QDialog>

class QListWidget;
class QPushButton;
class Config;

// Edits the programs that switch a device to one of its profiles.
class ProgramsDialog : public QDialog
{
    Q_OBJECT

public:
    ProgramsDialog(Config *config, const QString &device, const QString &profile, QWidget *parent = nullptr);

    void accept() override;

private:
    void addProgram(const QString &name);
    void pickRunning();
    void addByName();

    Config *m_config;
    QString m_device;
    QString m_profile;
    QListWidget *m_list;
    QPushButton *m_remove;
};
