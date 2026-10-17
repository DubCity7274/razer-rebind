#include "settingsdialog.h"

#include "batterymonitor.h"
#include "config.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(Config *config, BatteryMonitor *battery, QWidget *parent)
    : QDialog(parent)
    , m_config(config)
    , m_battery(battery)
{
    setWindowTitle(tr("Settings"));

    // Profile switching
    auto *autoSwitch = new QCheckBox(tr("Switch profiles automatically while their programs run"));
    autoSwitch->setToolTip(tr("Link programs to a profile with the Programs… button next to it"));
    autoSwitch->setChecked(config->autoSwitch);
    connect(autoSwitch, &QCheckBox::toggled, this, [this](bool on) {
        m_config->autoSwitch = on;
        save();
    });
    auto *profileNotes = new QCheckBox(tr("Show a notification when a hotkey, command or program switches profile"));
    profileNotes->setChecked(config->notifications);
    connect(profileNotes, &QCheckBox::toggled, this, [this](bool on) {
        m_config->notifications = on;
        save();
    });
    auto *profileLayout = new QVBoxLayout;
    profileLayout->addWidget(autoSwitch);
    profileLayout->addWidget(profileNotes);
    auto *profileBox = new QGroupBox(tr("Profiles"));
    profileBox->setLayout(profileLayout);

    // Battery
    auto *alerts = new QCheckBox(tr("Warn me when a wireless device's battery is low"));
    alerts->setChecked(config->batteryAlerts);
    auto *threshold = new QSpinBox;
    threshold->setRange(5, 50);
    threshold->setSingleStep(5);
    threshold->setSuffix(QStringLiteral("%"));
    threshold->setValue(config->batteryThreshold);
    threshold->setEnabled(config->batteryAlerts);
    auto *thresholdRow = new QHBoxLayout;
    thresholdRow->addWidget(new QLabel(tr("Warn at:")));
    thresholdRow->addWidget(threshold);
    thresholdRow->addStretch();
    auto *test = new QPushButton(tr("Send test notification"));
    thresholdRow->addWidget(test);
    auto *note = new QLabel(tr("Battery levels are checked every minute. After the first warning you're "
                               "reminded every 5% until you plug the device in."));
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    m_levels = new QLabel;

    connect(alerts, &QCheckBox::toggled, this, [this, threshold](bool on) {
        threshold->setEnabled(on);
        m_config->batteryAlerts = on;
        save();
    });
    connect(threshold, &QSpinBox::valueChanged, this, [this](int value) {
        m_config->batteryThreshold = value;
        save();
    });
    connect(test, &QPushButton::clicked, m_battery, &BatteryMonitor::sendTestNotification);
    connect(m_battery, &BatteryMonitor::levelsChanged, this, &SettingsDialog::refreshLevels);

    auto *batteryLayout = new QVBoxLayout;
    batteryLayout->addWidget(alerts);
    batteryLayout->addLayout(thresholdRow);
    batteryLayout->addWidget(note);
    batteryLayout->addWidget(m_levels);
    auto *batteryBox = new QGroupBox(tr("Battery"));
    batteryBox->setLayout(batteryLayout);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(profileBox);
    layout->addWidget(batteryBox);
    layout->addStretch();
    layout->addWidget(buttons);
    resize(520, sizeHint().height());
    refreshLevels();
}

void SettingsDialog::save()
{
    QString error;
    if (!m_config->save(&error))
        QMessageBox::warning(this, windowTitle(), tr("Could not save: %1").arg(error));
}

void SettingsDialog::refreshLevels()
{
    const QString summary = m_battery->summary();
    m_levels->setText(summary.isEmpty() ? tr("No wireless device connected right now.")
                                        : tr("Now: %1").arg(summary));
}
