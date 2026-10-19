#include "sensorpanel.h"

#include "config.h"
#include "remapmanager.h"

#include <QApplication>
#include <QButtonGroup>
#include <QPointer>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <thread>

SensorPanel::SensorPanel(Config *config, RemapManager *manager, QWidget *parent)
    : QWidget(parent)
    , m_config(config)
    , m_manager(manager)
{
    m_stageGroup = new QButtonGroup(this);
    m_stageRow = new QHBoxLayout;
    m_stageRow->setSpacing(4);
    m_add = new QPushButton(tr("Add stage"));
    m_remove = new QPushButton(tr("Remove stage"));
    for (QPushButton *b : {m_add, m_remove})
        b->setAutoDefault(false);
    m_add->setToolTip(tr("The DPI button under the mouse cycles through the stages"));
    m_remove->setToolTip(tr("Remove the selected stage"));
    m_pollRate = new QComboBox;
    m_note = new QLabel;
    m_note->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));

    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(new QLabel(tr("DPI:")));
    row->addLayout(m_stageRow);
    row->addWidget(m_add);
    row->addWidget(m_remove);
    row->addSpacing(16);
    row->addWidget(new QLabel(tr("Polling rate:")));
    row->addWidget(m_pollRate);
    row->addSpacing(16);
    row->addWidget(m_note, 1);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 4);
    layout->addLayout(row);

    // Typing a DPI value goes through several numbers; apply once it settles.
    m_commitTimer.setSingleShot(true);
    m_commitTimer.setInterval(500);
    connect(&m_commitTimer, &QTimer::timeout, this, &SensorPanel::commit);
    connect(m_add, &QPushButton::clicked, this, [this] {
        SensorSettings s = fromWidgets();
        s.stages << std::min(m_maxDpi, (s.stages.isEmpty() ? 800 : s.stages.last()) * 2);
        display(s);
        commit();
    });
    connect(m_remove, &QPushButton::clicked, this, [this] {
        SensorSettings s = fromWidgets();
        if (s.stages.size() <= 1)
            return;
        s.stages.removeAt(s.activeStage);
        s.activeStage = std::min(s.activeStage, int(s.stages.size()) - 1);
        display(s);
        commit();
    });
    connect(m_pollRate, &QComboBox::activated, this, &SensorPanel::commit);

    // The DPI button under the mouse is handled by its firmware, which
    // doesn't announce the change; look while the panel is on screen.
    m_pollTimer.setInterval(1000);
    connect(&m_pollTimer, &QTimer::timeout, this, &SensorPanel::poll);
    m_pollTimer.start();
    hide();
}

void SensorPanel::setDevice(const RazerDevice *device)
{
    m_commitTimer.stop();
    const Sensor sensor = device ? Sensor(*device) : Sensor(RazerDevice());
    if (!device || !sensor.isSupported()) {
        hide();
        return;
    }
    m_device = *device;
    m_maxDpi = sensor.maxDpi();
    m_hasStages = sensor.hasStages();
    QString error;
    if (!sensor.read(&m_current, &error))
        m_current = SensorSettings();

    m_pollRate->clear();
    QList<int> rates = sensor.pollRates();
    if (m_current.pollRate > 0 && !rates.contains(m_current.pollRate))
        rates << m_current.pollRate;
    std::sort(rates.begin(), rates.end());
    for (int r : rates)
        m_pollRate->addItem(tr("%1 Hz").arg(r), r);

    DeviceProfile p = m_config->device(m_device.profileKey);
    if (p.sensor.isValid() && m_current.isValid() && p.sensor.stages == m_current.stages
        && p.sensor.pollRate == m_current.pollRate && p.sensor.activeStage != m_current.activeStage) {
        // The DPI button under the mouse picked another stage; keep it.
        p.sensor.activeStage = m_current.activeStage;
        m_config->setDevice(m_device.profileKey, p);
        m_config->save(nullptr);
        m_manager->markSensorApplied(m_device.hidPath, p.sensor);
    }
    display(p.sensor.isValid() ? p.sensor : m_current);
    if (!m_current.isValid() && !p.sensor.isValid())
        m_note->setText(tr("The mouse didn't answer; is it asleep? %1").arg(error));
    QWidget::show();
}

void SensorPanel::poll()
{
    // Hidden (another device selected, window in the tray) or mid-edit.
    if (!isVisible() || m_commitTimer.isActive() || m_polling->exchange(true))
        return;
    // A sleeping wireless mouse can take seconds to answer, so read it off
    // the GUI thread.
    const RazerDevice device = m_device;
    QPointer<SensorPanel> self(this);
    std::thread([self, device, polling = m_polling] {
        SensorSettings now;
        const bool ok = Sensor(device).read(&now);
        polling->store(false);
        if (ok)
            QMetaObject::invokeMethod(qApp, [self, hidPath = device.hidPath, now] {
                if (self)
                    self->onPolled(hidPath, now);
            });
    }).detach();
}

void SensorPanel::onPolled(const QString &hidPath, const SensorSettings &now)
{
    if (hidPath != m_device.hidPath || m_commitTimer.isActive() || now == m_current)
        return;
    m_current = now;
    DeviceProfile p = m_config->device(m_device.profileKey);
    if (p.sensor.isValid() && p.sensor.stages == now.stages) {
        // The DPI button picked another stage: keep it in the profile. A
        // DPI binding has already done that, so compare with what's shown.
        if (p.sensor.activeStage != now.activeStage) {
            p.sensor.activeStage = now.activeStage;
            m_config->setDevice(m_device.profileKey, p);
            m_config->save(nullptr);
            m_manager->markSensorApplied(m_device.hidPath, p.sensor);
        }
        if (fromWidgets().activeStage != now.activeStage)
            display(p.sensor);
    } else if (!p.sensor.isValid()) {
        display(now);
    }
}

void SensorPanel::rebuildStages(int count)
{
    while (QLayoutItem *item = m_stageRow->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    m_stageActive.clear();
    m_stageDpi.clear();
    for (int i = 0; i < count; ++i) {
        auto *active = new QRadioButton;
        active->setToolTip(tr("Use this stage now"));
        active->setVisible(m_hasStages && count > 1);
        m_stageGroup->addButton(active);
        auto *dpi = new QSpinBox;
        dpi->setRange(Sensor::MinDpi, m_maxDpi);
        dpi->setSingleStep(50);
        dpi->setAccelerated(true);
        dpi->setToolTip(m_hasStages ? tr("DPI of stage %1").arg(i + 1) : tr("DPI"));
        m_stageRow->addWidget(active);
        m_stageRow->addWidget(dpi);
        if (i + 1 < count)
            m_stageRow->addSpacing(6);
        m_stageActive << active;
        m_stageDpi << dpi;
        connect(active, &QRadioButton::toggled, this, [this](bool on) {
            if (on)
                commit();
        });
        connect(dpi, &QSpinBox::valueChanged, this, [this] { m_commitTimer.start(); });
    }
}

void SensorPanel::display(const SensorSettings &s)
{
    const int count = std::max(1, int(s.stages.size()));
    if (m_stageDpi.size() != count)
        rebuildStages(count);
    for (int i = 0; i < count; ++i) {
        QSignalBlocker a(m_stageActive[i]), d(m_stageDpi[i]);
        m_stageActive[i]->setChecked(i == s.activeStage);
        m_stageDpi[i]->setValue(s.stages.value(i, 800));
    }
    m_add->setVisible(m_hasStages);
    m_remove->setVisible(m_hasStages);
    m_add->setEnabled(count < Sensor::MaxStages);
    m_remove->setEnabled(count > 1);
    const int rate = m_pollRate->findData(s.pollRate);
    m_pollRate->setCurrentIndex(rate >= 0 ? rate : m_pollRate->count() - 1);
    updateNote();
}

SensorSettings SensorPanel::fromWidgets() const
{
    SensorSettings s;
    for (int i = 0; i < m_stageDpi.size(); ++i) {
        s.stages << m_stageDpi[i]->value();
        if (m_stageActive[i]->isChecked())
            s.activeStage = i;
    }
    s.pollRate = m_pollRate->currentData().toInt();
    return s;
}

void SensorPanel::commit()
{
    m_commitTimer.stop();
    const QString key = m_device.profileKey;
    // The first change in any profile pins the mouse's current settings to
    // the other profiles, so switching back to them undoes it.
    if (!m_config->device(key).sensor.isValid() && m_current.isValid())
        m_config->fillSensor(key, m_current);
    DeviceProfile p = m_config->device(key);
    p.sensor = fromWidgets();
    m_config->setDevice(key, p);
    m_config->save(nullptr);
    m_manager->applyProfile();
    updateNote();
}

void SensorPanel::updateNote()
{
    const QString profile = m_config->activeProfile(m_device.profileKey);
    m_note->setText(m_config->device(m_device.profileKey).sensor.isValid()
                        ? tr("Saved with the “%1” profile").arg(profile)
                        : tr("Changes are saved with the “%1” profile").arg(profile));
}
