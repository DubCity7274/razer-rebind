#include "mainwindow.h"

#include "autoswitcher.h"
#include "batterymonitor.h"
#include "bindingdialog.h"
#include "deviceview.h"
#include "hotkeydialog.h"
#include "programsdialog.h"
#include "config.h"
#include "remapmanager.h"
#include "remapper.h"
#include "settingsdialog.h"

#include <QActionGroup>
#include <QButtonGroup>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QInputDialog>
#include <QCloseEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

QIcon appIcon(bool active)
{
    QIcon icon;
    for (int size : {16, 22, 32, 48, 64, 128}) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        const qreal s = size;
        p.setPen(Qt::NoPen);
        p.setBrush(active ? QColor(0x44, 0xd6, 0x2c) : QColor(0x80, 0x80, 0x80));
        p.drawRoundedRect(QRectF(s * 0.06, s * 0.06, s * 0.88, s * 0.88), s * 0.2, s * 0.2);
        // A keycap with a curved arrow: "this key does something else".
        p.setBrush(QColor(20, 20, 20));
        p.drawRoundedRect(QRectF(s * 0.2, s * 0.2, s * 0.6, s * 0.6), s * 0.1, s * 0.1);
        QPen pen(active ? QColor(0x44, 0xd6, 0x2c) : QColor(0xc0, 0xc0, 0xc0), std::max(1.5, s * 0.08));
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(s * 0.33, s * 0.33, s * 0.34, s * 0.34), 30 * 16, 270 * 16);
        p.drawLine(QPointF(s * 0.64, s * 0.38), QPointF(s * 0.66, s * 0.47));
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

MainWindow::MainWindow(Config *config, RemapManager *manager, BatteryMonitor *battery, AutoSwitcher *autoSwitcher,
                       QWidget *parent)
    : QMainWindow(parent)
    , m_config(config)
    , m_manager(manager)
    , m_battery(battery)
    , m_autoSwitcher(autoSwitcher)
{
    setWindowTitle(tr("Razer Rebind"));
    setWindowIcon(appIcon(true));
    resize(1180, 760);

    // Left: devices
    m_devices = new QListWidget;
    m_devices->setIconSize(QSize(32, 32));
    m_devices->setMinimumWidth(220);
    m_devices->setSpacing(2);

    // Right: selected device
    m_title = new QLabel;
    QFont titleFont = m_title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.5);
    titleFont.setBold(true);
    m_title->setFont(titleFont);
    m_details = new QLabel;
    m_details->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_details->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    m_state = new QLabel;
    m_state->setWordWrap(true);
    m_state->setTextFormat(Qt::RichText);
    m_state->setOpenExternalLinks(false);
    connect(m_state, &QLabel::linkActivated, this, &MainWindow::showPermissionHelp);
    m_deviceEnabled = new QCheckBox(tr("Remap this device"));

    // Each device has its own profiles; the selector sits with the device.
    m_profileCombo = new QComboBox;
    m_profileCombo->setMinimumContentsLength(14);
    m_profileCombo->setToolTip(tr("This device's active profile"));
    connect(m_profileCombo, &QComboBox::activated, this, [this](int i) {
        if (const RazerDevice *dev = current())
            m_manager->switchProfile(dev->profileKey, m_profileCombo->itemText(i));
    });
    m_deviceProfileMenu = new QMenu(this);
    auto *profileButton = new QToolButton;
    profileButton->setText(tr("Manage"));
    profileButton->setToolTip(tr("New, duplicate, rename or delete this device's profiles"));
    profileButton->setMenu(m_deviceProfileMenu);
    profileButton->setPopupMode(QToolButton::InstantPopup);
    m_programsButton = new QPushButton(tr("Programs…"));
    m_programsButton->setAutoDefault(false);
    m_programsButton->setToolTip(tr("Programs that switch this device to this profile while they run"));
    connect(m_programsButton, &QPushButton::clicked, this, &MainWindow::editPrograms);
    auto *profileRow = new QHBoxLayout;
    profileRow->addWidget(new QLabel(tr("Profile:")));
    profileRow->addWidget(m_profileCombo);
    profileRow->addWidget(profileButton);
    profileRow->addWidget(m_programsButton);
    profileRow->addSpacing(16);
    profileRow->addWidget(m_deviceEnabled);

    // Which programs switch to this profile, or which one switched to it.
    m_autoInfo = new QLabel;
    m_autoInfo->setTextFormat(Qt::RichText);
    m_autoInfo->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    connect(m_autoInfo, &QLabel::linkActivated, this, &MainWindow::editPrograms);

    auto *header = new QGridLayout;
    header->addWidget(m_title, 0, 0);
    header->addLayout(profileRow, 0, 1, Qt::AlignRight);
    header->addWidget(m_details, 1, 0, 1, 2);
    header->addWidget(m_state, 2, 0);
    header->addWidget(m_autoInfo, 2, 1, Qt::AlignRight | Qt::AlignTop);

    // Which layer the picture and the list edit.
    m_layerNormal = new QPushButton(tr("Normal"));
    m_layerHypershift = new QPushButton(tr("Hypershift"));
    m_layerHypershift->setToolTip(tr("Bindings used while a Hypershift key is held"));
    auto *layerGroup = new QButtonGroup(this);
    for (QPushButton *b : {m_layerNormal, m_layerHypershift}) {
        b->setCheckable(true);
        b->setAutoDefault(false);
        layerGroup->addButton(b);
    }
    m_layerNormal->setChecked(true);
    // A segmented control: the selected layer is filled solid, Normal in the
    // accent colour and Hypershift in the purple used for Hypershift keys.
    const QString segment = QStringLiteral(
        "QPushButton { padding: 4px 14px; border: 1px solid palette(mid); background: palette(button); }"
        "QPushButton:hover:!checked { background: palette(midlight); }"
        "QPushButton:checked { color: white; font-weight: bold; border-color: %1; background: %1; }");
    m_layerNormal->setStyleSheet(segment.arg(QStringLiteral("palette(highlight)"))
                                 + QStringLiteral("QPushButton { border-top-left-radius: 4px; border-bottom-left-radius: 4px; }"));
    m_layerHypershift->setStyleSheet(segment.arg(QStringLiteral("#965adc"))
                                     + QStringLiteral("QPushButton { border-left: none; border-top-right-radius: 4px;"
                                                      " border-bottom-right-radius: 4px; }"));
    connect(m_layerHypershift, &QPushButton::toggled, this, &MainWindow::setHypershiftLayer);
    m_layerHint = new QLabel;
    m_layerHint->setWordWrap(true);
    m_layerHint->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    auto *layerRow = new QHBoxLayout;
    layerRow->setSpacing(0);
    layerRow->addWidget(new QLabel(tr("Layer:")));
    layerRow->addSpacing(8);
    layerRow->addWidget(m_layerNormal);
    layerRow->addWidget(m_layerHypershift);
    layerRow->addSpacing(12);
    layerRow->addWidget(m_layerHint, 1);
    header->addLayout(layerRow, 3, 0, 1, 2);

    m_keyboardView = new KeyboardView;
    m_mouseView = new MouseView;
    m_views = new QStackedWidget;
    auto *placeholder = new QLabel(tr("No Razer keyboard or mouse found.\n\n"
                                      "Make sure the OpenRazer kernel modules are loaded\n"
                                      "(razerkbd, razermouse) and the device is connected."));
    placeholder->setAlignment(Qt::AlignCenter);
    m_views->addWidget(placeholder);
    m_views->addWidget(m_keyboardView);
    m_views->addWidget(m_mouseView);

    m_table = new QTableWidget(0, 2);
    m_table->setHorizontalHeaderLabels({tr("Input"), tr("Output")});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->verticalHeader()->hide();
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setMinimumHeight(140);

    auto *addButton = new QPushButton(tr("Add binding…"));
    m_editButton = new QPushButton(tr("Edit…"));
    m_removeButton = new QPushButton(tr("Remove"));
    m_clearButton = new QPushButton(tr("Clear all"));
    auto *buttonRow = new QVBoxLayout;
    for (QPushButton *b : {addButton, m_editButton, m_removeButton, m_clearButton})
        buttonRow->addWidget(b);
    buttonRow->addStretch();

    auto *bindingRow = new QHBoxLayout;
    bindingRow->addWidget(m_table, 1);
    bindingRow->addLayout(buttonRow);

    auto *rightSplit = new QSplitter(Qt::Vertical);
    rightSplit->addWidget(m_views);
    auto *bindingPane = new QWidget;
    bindingPane->setLayout(bindingRow);
    bindingRow->setContentsMargins(0, 0, 0, 0);
    rightSplit->addWidget(bindingPane);
    rightSplit->setStretchFactor(0, 1);
    rightSplit->setStretchFactor(1, 0);
    rightSplit->setSizes({480, 180});

    auto *right = new QWidget;
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->addLayout(header);
    rightLayout->addWidget(rightSplit, 1);

    auto *split = new QSplitter;
    split->addWidget(m_devices);
    split->addWidget(right);
    split->setStretchFactor(1, 1);
    split->setSizes({240, 940});
    setCentralWidget(split);

    // Actions
    m_toggleAction = new QAction(tr("Remapping enabled"), this);
    m_toggleAction->setCheckable(true);
    m_toggleAction->setChecked(m_manager->remappingEnabled());
    m_toggleAction->setShortcut(QKeySequence(tr("Ctrl+E")));
    connect(m_toggleAction, &QAction::toggled, m_manager, &RemapManager::setRemappingEnabled);

    auto *rescanAction = new QAction(QIcon::fromTheme(QStringLiteral("view-refresh")), tr("Rescan devices"), this);
    rescanAction->setShortcut(QKeySequence::Refresh);
    connect(rescanAction, &QAction::triggered, m_manager, &RemapManager::rescan);

    auto *quitAction = new QAction(QIcon::fromTheme(QStringLiteral("application-exit")), tr("Quit"), this);
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);

    auto *toolbar = addToolBar(tr("Main"));
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolbar->addAction(m_toggleAction);
    toolbar->addAction(rescanAction);
    auto *hotkeysAction = new QAction(QIcon::fromTheme(QStringLiteral("preferences-desktop-keyboard-shortcuts")),
                                      tr("Hotkeys…"), this);
    connect(hotkeysAction, &QAction::triggered, this, &MainWindow::showHotkeys);
    auto *settingsAction = new QAction(QIcon::fromTheme(QStringLiteral("configure")), tr("Settings…"), this);
    settingsAction->setShortcut(QKeySequence::Preferences);
    connect(settingsAction, &QAction::triggered, this, [this] {
        SettingsDialog dlg(m_config, m_battery, this);
        dlg.exec();
        m_autoSwitcher->update();
        refreshAutoInfo();
    });
    toolbar->addSeparator();
    toolbar->addAction(hotkeysAction);
    toolbar->addAction(settingsAction);
    m_profileMenu = new QMenu(tr("&Profiles"), this);

    auto *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(m_toggleAction);
    fileMenu->addAction(rescanAction);
    fileMenu->addAction(settingsAction);
    fileMenu->addSeparator();
    fileMenu->addAction(quitAction);
    menuBar()->addMenu(m_profileMenu);
    auto *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("Device permissions…"), this, &MainWindow::showPermissionHelp);
    helpMenu->addAction(tr("About"), this, [this] {
        QMessageBox::about(this, tr("About Razer Rebind"),
                           tr("<b>Razer Rebind</b><p>Key and button rebinding for Razer devices driven by the "
                              "OpenRazer kernel modules.</p><p>Bindings are stored in<br><code>%1</code></p>")
                               .arg(m_config->path().toHtmlEscaped()));
    });

    // Wiring
    connect(m_devices, &QListWidget::currentRowChanged, this, &MainWindow::selectDevice);
    connect(m_deviceEnabled, &QCheckBox::toggled, this, [this](bool on) {
        const RazerDevice *dev = current();
        if (!dev)
            return;
        DeviceProfile p = m_config->device(dev->profileKey);
        if (p.enabled == on)
            return;
        p.enabled = on;
        m_config->setDevice(dev->profileKey, p);
        saveProfile();
    });
    for (DeviceView *v : {m_keyboardView, m_mouseView})
        connect(v, &DeviceView::keyClicked, this, &MainWindow::editBinding);
    connect(addButton, &QPushButton::clicked, this, [this] { editBinding(-1); });
    connect(m_editButton, &QPushButton::clicked, this, [this] {
        if (auto *item = m_table->item(m_table->currentRow(), 0))
            editBinding(item->data(Qt::UserRole).toInt());
    });
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row) {
        editBinding(m_table->item(row, 0)->data(Qt::UserRole).toInt());
    });
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this] {
        const bool any = m_table->currentRow() >= 0 && !m_table->selectedItems().isEmpty();
        m_editButton->setEnabled(any);
        m_removeButton->setEnabled(any);
    });
    connect(m_removeButton, &QPushButton::clicked, this, &MainWindow::removeSelectedBinding);
    connect(m_clearButton, &QPushButton::clicked, this, &MainWindow::clearBindings);

    connect(m_manager, &RemapManager::devicesChanged, this, &MainWindow::rebuildDeviceList);
    connect(m_manager, &RemapManager::stateChanged, this, &MainWindow::refreshState);
    connect(m_manager, &RemapManager::profileChanged, this, [this] {
        refreshProfiles();
        selectDevice();
    });
    connect(m_autoSwitcher, &AutoSwitcher::changed, this, &MainWindow::refreshAutoInfo);
    connect(m_manager, &RemapManager::showRequested, this, &MainWindow::showFromBackground);
    connect(m_battery, &BatteryMonitor::levelsChanged, this, [this] {
        refreshState();
        refreshDetails();
        refreshProfiles(); // tray tooltip
    });
    connect(m_manager, &RemapManager::message, this, [this](const QString &msg) {
        statusBar()->showMessage(msg, 8000);
    });
    connect(m_manager, &RemapManager::inputEvent, this, [this](const QString &key, int code, bool pressed) {
        const RazerDevice *dev = current();
        if (!dev || dev->profileKey != key)
            return;
        if (code == Keys::WheelUp || code == Keys::WheelDown) {
            // Wheel "presses" are instantaneous; flash them briefly.
            if (pressed) {
                currentView()->setPressed(code, true);
                QTimer::singleShot(120, this, [this, code] {
                    if (DeviceView *v = currentView())
                        v->setPressed(code, false);
                });
            }
            return;
        }
        currentView()->setPressed(code, pressed);
    });

    m_profileStatus = new QLabel;
    statusBar()->addPermanentWidget(m_profileStatus);

    setupTray();
    refreshProfiles();
    rebuildDeviceList();
}

bool MainWindow::hasTray() const
{
    return m_tray && m_tray->isVisible();
}

void MainWindow::setupTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;
    m_tray = new QSystemTrayIcon(appIcon(m_manager->remappingEnabled()), this);
    m_tray->setToolTip(tr("Razer Rebind"));
    auto *menu = new QMenu(this);
    menu->addAction(tr("Show window"), this, &MainWindow::showFromBackground);
    m_trayProfileMenu = menu->addMenu(tr("Profile"));
    menu->addAction(m_toggleAction);
    menu->addSeparator();
    menu->addAction(tr("Quit"), qApp, &QApplication::quit);
    m_tray->setContextMenu(menu);
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason != QSystemTrayIcon::Trigger)
            return;
        if (isVisible() && !isMinimized()) {
            hide();
        } else {
            showNormal();
            raise();
            activateWindow();
        }
    });
    m_tray->show();
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (hasTray()) {
        // Keep remapping in the background.
        hide();
        if (!m_trayHintShown) {
            m_tray->showMessage(tr("Razer Rebind"), tr("Still running in the tray so your bindings stay active."),
                                QSystemTrayIcon::Information, 4000);
            m_trayHintShown = true;
        }
        e->ignore();
        return;
    }
    QMainWindow::closeEvent(e);
    qApp->quit();
}

const RazerDevice *MainWindow::current() const
{
    const QList<RazerDevice> &devices = m_manager->devices();
    for (const RazerDevice &d : devices)
        if (d.hidPath == m_currentPath)
            return &d;
    return nullptr;
}

DeviceView *MainWindow::currentView() const
{
    const RazerDevice *dev = current();
    if (!dev)
        return m_keyboardView;
    return dev->kind == RazerDevice::Mouse ? m_mouseView : m_keyboardView;
}

void MainWindow::rebuildDeviceList()
{
    const QString keep = m_currentPath;
    QSignalBlocker block(m_devices);
    m_devices->clear();
    int select = 0;
    const QList<RazerDevice> devices = m_manager->devices();
    for (int i = 0; i < devices.size(); ++i) {
        const RazerDevice &d = devices[i];
        auto *item = new QListWidgetItem(
            QIcon::fromTheme(d.kind == RazerDevice::Mouse ? QStringLiteral("input-mouse") : QStringLiteral("input-keyboard")),
            d.name);
        item->setData(Qt::UserRole, d.hidPath);
        item->setSizeHint(QSize(0, 48));
        m_devices->addItem(item);
        if (d.hidPath == keep)
            select = i;
    }
    block.unblock();
    if (devices.isEmpty()) {
        m_currentPath.clear();
        selectDevice();
    } else {
        m_devices->setCurrentRow(select);
        selectDevice();
    }
}

void MainWindow::selectDevice()
{
    QListWidgetItem *item = m_devices->currentItem();
    m_currentPath = item ? item->data(Qt::UserRole).toString() : QString();
    const RazerDevice *dev = current();

    m_keyboardView->clearPressed();
    m_mouseView->clearPressed();

    if (!dev) {
        m_title->setText(tr("No device"));
        m_details->clear();
        m_views->setCurrentIndex(0);
        m_deviceEnabled->setEnabled(false);
        refreshBindings();
        refreshState();
        return;
    }

    m_title->setText(dev->profileKey);
    // Serial / firmware queries go to the device over USB; do them once.
    if (!m_detailsCache.contains(dev->hidPath)) {
        QStringList parts;
        parts << QStringLiteral("%1 · %2:%3")
                     .arg(dev->driver)
                     .arg(dev->vendorId, 4, 16, QLatin1Char('0'))
                     .arg(dev->productId, 4, 16, QLatin1Char('0'));
        const QString serial = dev->attribute("device_serial");
        const QString firmware = dev->attribute("firmware_version");
        if (!serial.isEmpty())
            parts << tr("serial %1").arg(serial);
        if (!firmware.isEmpty())
            parts << tr("firmware %1").arg(firmware);
        parts << dev->eventNodes.join(QStringLiteral(", "));
        m_detailsCache.insert(dev->hidPath, parts.join(QStringLiteral("  ·  ")));
    }
    refreshDetails();
    m_views->setCurrentIndex(dev->kind == RazerDevice::Mouse ? 2 : 1);
    {
        QSignalBlocker b(m_deviceEnabled);
        m_deviceEnabled->setEnabled(true);
        m_deviceEnabled->setChecked(m_config->device(dev->profileKey).enabled);
    }
    refreshBindings();
    refreshState();
    refreshProfiles();
}

QString MainWindow::batteryText(const QString &hidPath) const
{
    const BatteryMonitor::Level l = m_battery->level(hidPath);
    if (l.percent < 0)
        return {};
    return l.charging ? tr("battery %1% (charging)").arg(l.percent) : tr("battery %1%").arg(l.percent);
}

void MainWindow::refreshDetails()
{
    const RazerDevice *dev = current();
    if (!dev)
        return;
    QString text = m_detailsCache.value(dev->hidPath);
    const QString battery = batteryText(dev->hidPath);
    if (!battery.isEmpty())
        text = battery + QStringLiteral("  ·  ") + text;
    m_details->setText(text);
}

void MainWindow::refreshState()
{
    {
        QSignalBlocker b(m_toggleAction);
        m_toggleAction->setChecked(m_manager->remappingEnabled());
    }
    if (m_tray)
        m_tray->setIcon(appIcon(m_manager->remappingEnabled()));

    // Device list status line
    const QList<RazerDevice> devices = m_manager->devices();
    for (int i = 0; i < m_devices->count(); ++i) {
        QListWidgetItem *item = m_devices->item(i);
        const QString path = item->data(Qt::UserRole).toString();
        auto dev = std::find_if(devices.cbegin(), devices.cend(), [&](const RazerDevice &d) { return d.hidPath == path; });
        if (dev == devices.cend())
            continue;
        Remapper *r = m_manager->remapper(path);
        QString status;
        if (!m_manager->errorFor(path).isEmpty() || !r)
            status = tr("⚠ error");
        else if (r->isGrabbed())
            status = tr("● remapping");
        else
            status = tr("○ idle");
        status += QStringLiteral(" · ") + m_config->activeProfile(dev->profileKey);
        const BatteryMonitor::Level battery = m_battery->level(path);
        if (battery.percent >= 0)
            status += QStringLiteral(" · %1%%2").arg(battery.percent).arg(battery.charging ? QStringLiteral("⚡") : QString());
        item->setText(dev->name + QLatin1Char('\n') + status);
    }

    const RazerDevice *dev = current();
    if (!dev) {
        m_state->clear();
        return;
    }
    Remapper *r = m_manager->remapper(dev->hidPath);
    const QString error = m_manager->errorFor(dev->hidPath);
    const DeviceProfile p = m_config->device(dev->profileKey);
    QString html;
    if (!error.isEmpty() || !r) {
        html = QStringLiteral("<span style='color:#d33'>⚠ %1</span> — <a href='#'>%2</a>")
                   .arg(error.toHtmlEscaped(), tr("how to fix permissions"));
    } else if (r->isGrabbed()) {
        html = tr("<span style='color:#2a2'>● Remapping active</span> — %n binding(s) applied", nullptr,
                  int(p.bindings.size() + p.hypershift.size()));
    } else if (r->isWaitingForRelease()) {
        html = tr("Waiting for all keys and buttons to be released before taking over the device…");
    } else if (!m_manager->remappingEnabled()) {
        html = tr("Remapping is turned off (Ctrl+E). The device works normally.");
    } else if (!p.enabled) {
        html = tr("Remapping is off for this device. It works normally.");
    } else {
        html = tr("No bindings yet. The device works normally until you add one.");
    }
    m_state->setText(html);
}

void MainWindow::setHypershiftLayer(bool on)
{
    m_hypershiftLayer = on;
    // Bound keys take the selected layer's colour, so the picture shows
    // which layer is being edited.
    for (DeviceView *v : {m_keyboardView, m_mouseView})
        v->setAccent(on ? QColor(0x96, 0x5a, 0xdc) : QColor());
    refreshBindings();
}

QString MainWindow::hypershiftKeysText() const
{
    // Hypershift keys of every device's active profile, since any of them
    // switches every device.
    QStringList keys;
    for (const QString &device : m_config->deviceKeys()) {
        const BindingMap bindings = m_config->device(device).bindings;
        QList<int> codes;
        for (auto it = bindings.cbegin(); it != bindings.cend(); ++it)
            if (it->isHypershift())
                codes << it.key();
        std::sort(codes.begin(), codes.end());
        for (int code : codes)
            keys << tr("%1 (%2)").arg(Keys::label(code), shortDeviceName(device));
    }
    return keys.join(tr(" or "));
}

void MainWindow::refreshBindings()
{
    const RazerDevice *dev = current();
    const DeviceProfile p = dev ? m_config->device(dev->profileKey) : DeviceProfile();
    const BindingMap &bindings = m_hypershiftLayer ? p.hypershift : p.bindings;
    const BindingMap fallback = m_hypershiftLayer ? p.bindings : BindingMap();

    m_keyboardView->setBindings(dev && dev->kind == RazerDevice::Keyboard ? bindings : BindingMap(),
                                dev && dev->kind == RazerDevice::Keyboard ? fallback : BindingMap());
    m_mouseView->setBindings(dev && dev->kind == RazerDevice::Mouse ? bindings : BindingMap(),
                             dev && dev->kind == RazerDevice::Mouse ? fallback : BindingMap());

    const QString keys = hypershiftKeysText();
    if (!m_hypershiftLayer)
        m_layerHint->setText(keys.isEmpty() ? QString() : tr("Hypershift: hold %1").arg(keys));
    else if (keys.isEmpty())
        m_layerHint->setText(tr("No Hypershift key yet. In the Normal layer, bind a key or button to "
                                "“Hypershift”; holding it then switches every device to these bindings."));
    else
        m_layerHint->setText(tr("Used while %1 is held. Keys without a binding here keep their Normal one.").arg(keys));

    QList<int> sources = bindings.keys();
    std::sort(sources.begin(), sources.end());
    m_table->setRowCount(0);
    for (int src : sources) {
        const Binding &b = bindings[src];
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        auto *in = new QTableWidgetItem(Keys::label(src));
        in->setData(Qt::UserRole, src);
        in->setToolTip(Keys::name(src));
        auto *out = new QTableWidgetItem(b.isHypershift() ? tr("Hypershift (hold)") : Keys::bindingLabel(b));
        if (b.targets.isEmpty())
            out->setForeground(QColor(0xd3, 0x44, 0x44));
        else if (b.isHypershift())
            out->setForeground(QColor(0x96, 0x5a, 0xdc));
        m_table->setItem(row, 0, in);
        m_table->setItem(row, 1, out);
    }
    m_editButton->setEnabled(false);
    m_removeButton->setEnabled(false);
    m_clearButton->setEnabled(!sources.isEmpty());
    m_table->setEnabled(dev != nullptr);
}

void MainWindow::editBinding(int sourceCode)
{
    const RazerDevice *dev = current();
    if (!dev)
        return;
    const RazerDevice device = *dev; // the list may change while the dialog is open
    const DeviceProfile p = m_config->device(device.profileKey);
    const BindingMap bindings = m_hypershiftLayer ? p.hypershift : p.bindings;

    BindingDialog dlg(m_manager, device, m_hypershiftLayer, this);
    if (sourceCode > 0) {
        dlg.setSource(sourceCode);
        // An unbound key starts out mapped to itself, the natural starting point.
        if (bindings.contains(sourceCode))
            dlg.setBinding(bindings.value(sourceCode));
        else if (Keys::isValidTarget(sourceCode))
            dlg.setBinding(Binding{{sourceCode}, 0});
    }
    if (dlg.exec() != QDialog::Accepted)
        return;

    const int src = dlg.source();
    if (src != sourceCode && bindings.contains(src)
        && QMessageBox::question(this, tr("Replace binding?"),
                                 tr("%1 already has a binding. Replace it?").arg(Keys::label(src)))
               != QMessageBox::Yes)
        return;
    storeBinding(sourceCode, src, dlg.binding());
}

void MainWindow::storeBinding(int oldSource, int source, const Binding &binding)
{
    const RazerDevice *dev = current();
    if (!dev)
        return;
    DeviceProfile p = m_config->device(dev->profileKey);
    BindingMap &bindings = m_hypershiftLayer ? p.hypershift : p.bindings;
    if (oldSource > 0)
        bindings.remove(oldSource);
    // Mapping a key to exactly itself is the same as having no binding,
    // unless it overrides a Normal binding in the Hypershift layer.
    const bool identity = binding == Binding{{source}, 0};
    if (identity && !(m_hypershiftLayer && p.bindings.contains(source)))
        bindings.remove(source);
    else
        bindings.insert(source, binding);
    m_config->setDevice(dev->profileKey, p);
    saveProfile();
}

void MainWindow::removeSelectedBinding()
{
    const RazerDevice *dev = current();
    QTableWidgetItem *item = m_table->item(m_table->currentRow(), 0);
    if (!dev || !item)
        return;
    DeviceProfile p = m_config->device(dev->profileKey);
    (m_hypershiftLayer ? p.hypershift : p.bindings).remove(item->data(Qt::UserRole).toInt());
    m_config->setDevice(dev->profileKey, p);
    saveProfile();
}

void MainWindow::clearBindings()
{
    const RazerDevice *dev = current();
    if (!dev)
        return;
    const QString question = m_hypershiftLayer ? tr("Remove every Hypershift binding for the %1?")
                                               : tr("Remove every Normal binding for the %1? Its Hypershift "
                                                    "bindings are kept.");
    if (QMessageBox::question(this, tr("Clear all bindings"), question.arg(dev->profileKey)) != QMessageBox::Yes)
        return;
    DeviceProfile p = m_config->device(dev->profileKey);
    (m_hypershiftLayer ? p.hypershift : p.bindings).clear();
    m_config->setDevice(dev->profileKey, p);
    saveProfile();
}

void MainWindow::saveProfile()
{
    QString error;
    if (!m_config->save(&error))
        QMessageBox::warning(this, tr("Could not save bindings"), error);
    m_manager->applyProfile();
    refreshBindings();
    refreshState();
}

void MainWindow::showPermissionHelp()
{
    // Inside an AppImage the binary lives in a temporary mount; $APPIMAGE is
    // the file the user actually has.
    QString exe = qEnvironmentVariable("APPIMAGE");
    if (exe.isEmpty())
        exe = QCoreApplication::applicationFilePath();
    const QString command = QStringLiteral("'%1' --print-udev-rule | sudo tee /etc/udev/rules.d/70-razer-rebind.rules "
                                           ">/dev/null && sudo udevadm control --reload && sudo udevadm trigger")
                                .arg(exe);

    QMessageBox box(this);
    box.setWindowTitle(tr("Device permissions"));
    box.setTextFormat(Qt::RichText);
    box.setText(tr("<p>Razer Rebind needs to read the Razer event devices in <code>/dev/input</code> "
                   "and create a virtual device through <code>/dev/uinput</code>.</p>"
                   "<p>The RPM and .deb packages and the tarball's installer set this up. Otherwise "
                   "(AppImage, source build), install the permission rule by running this once in a "
                   "terminal. It grants access to the user logged in at this computer only:</p>"
                   "<pre>%1</pre>"
                   "<p>Then press <b>Rescan devices</b> (F5).</p>")
                    .arg(command.toHtmlEscaped()));
    QPushButton *copy = box.addButton(tr("Copy command"), QMessageBox::ActionRole);
    box.addButton(QMessageBox::Close);
    box.exec();
    if (box.clickedButton() == copy)
        QGuiApplication::clipboard()->setText(command);
}

// --- profiles -------------------------------------------------------------

void MainWindow::showFromBackground()
{
    showNormal();
    raise();
    activateWindow();
}

void MainWindow::refreshProfiles()
{
    const RazerDevice *dev = current();
    const QString key = dev ? dev->profileKey : QString();
    {
        QSignalBlocker b(m_profileCombo);
        m_profileCombo->clear();
        m_profileCombo->addItems(m_config->profileNames(key));
        m_profileCombo->setCurrentText(m_config->activeProfile(key));
        m_profileCombo->setEnabled(dev != nullptr);
    }
    fillProfileMenu(m_profileMenu);
    if (m_trayProfileMenu)
        fillProfileMenu(m_trayProfileMenu);

    m_deviceProfileMenu->clear();
    m_deviceProfileMenu->addAction(tr("New profile…"), this, [this] { newProfile(false); });
    m_deviceProfileMenu->addAction(tr("Duplicate this profile…"), this, [this] { newProfile(true); });
    m_deviceProfileMenu->addAction(tr("Rename this profile…"), this, &MainWindow::renameProfile);
    QAction *del = m_deviceProfileMenu->addAction(tr("Delete this profile…"), this, &MainWindow::deleteProfile);
    del->setEnabled(m_config->profileNames(key).size() > 1);
    m_deviceProfileMenu->setEnabled(dev != nullptr);

    refreshAutoInfo();
    const QString summary = m_manager->profileSummary();
    m_profileStatus->setText(summary);
    if (m_tray) {
        QString tip = tr("Razer Rebind") + QLatin1Char('\n') + summary;
        const QString battery = m_battery->summary();
        if (!battery.isEmpty())
            tip += QLatin1Char('\n') + tr("Battery: %1").arg(battery);
        m_tray->setToolTip(tip);
    }
}

void MainWindow::fillProfileMenu(QMenu *menu)
{
    // One section per device, then "All devices" for names several devices share.
    menu->clear();
    auto hotkeyFor = [this](const QString &device, const QString &name) {
        for (const Hotkey &h : std::as_const(m_config->hotkeys))
            if (h.action == Hotkey::SwitchProfile && h.profile == name && h.device == device)
                return QLatin1Char('\t') + h.triggerLabel();
        return QString();
    };
    const QStringList devices = m_config->deviceKeys();
    for (const QString &device : devices) {
        menu->addSection(shortDeviceName(device));
        auto *group = new QActionGroup(menu);
        for (const QString &name : m_config->profileNames(device)) {
            QAction *a = menu->addAction(name + hotkeyFor(device, name), this,
                                         [this, device, name] { m_manager->switchProfile(device, name); });
            a->setCheckable(true);
            a->setChecked(name == m_config->activeProfile(device));
            group->addAction(a);
        }
    }
    if (devices.size() > 1) {
        menu->addSection(tr("All devices"));
        for (const QString &name : m_config->allProfileNames())
            menu->addAction(name + hotkeyFor(QString(), name), this, [this, name] {
                m_manager->perform(Hotkey::SwitchProfile, name, QString(), false);
            });
    }
    if (menu == m_trayProfileMenu)
        return;
    menu->addSeparator();
    menu->addAction(tr("Hotkeys…"), this, &MainWindow::showHotkeys);
}

void MainWindow::newProfile(bool duplicate)
{
    const RazerDevice *dev = current();
    if (!dev)
        return;
    const QString device = dev->profileKey;
    const QString active = m_config->activeProfile(device);
    bool ok = false;
    const QString name = QInputDialog::getText(this, duplicate ? tr("Duplicate profile") : tr("New profile"),
                                               tr("Name of the new %1 profile:").arg(shortDeviceName(device)),
                                               QLineEdit::Normal, duplicate ? tr("%1 copy").arg(active) : QString(), &ok)
                             .trimmed();
    if (!ok || name.isEmpty())
        return;
    if (!m_config->addProfile(device, name, duplicate ? active : QString())) {
        QMessageBox::warning(this, tr("New profile"), tr("The %1 already has a profile named “%2”.").arg(device, name));
        return;
    }
    m_manager->switchProfile(device, name); // saves
}

void MainWindow::renameProfile()
{
    const RazerDevice *dev = current();
    if (!dev)
        return;
    const QString device = dev->profileKey;
    const QString active = m_config->activeProfile(device);
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename profile"), tr("Profile name:"),
                                               QLineEdit::Normal, active, &ok)
                             .trimmed();
    if (!ok || name.isEmpty() || name == active)
        return;
    if (!m_config->renameProfile(device, active, name)) {
        QMessageBox::warning(this, tr("Rename profile"), tr("The %1 already has a profile named “%2”.").arg(device, name));
        return;
    }
    m_manager->switchProfile(device, name);
}

void MainWindow::deleteProfile()
{
    const RazerDevice *dev = current();
    if (!dev)
        return;
    const QString device = dev->profileKey;
    const QString active = m_config->activeProfile(device);
    if (m_config->profileNames(device).size() <= 1)
        return;
    if (QMessageBox::question(this, tr("Delete profile"),
                              tr("Delete the %1 profile “%2” and all of its bindings? Hotkeys that switch "
                                 "to it are removed too.").arg(shortDeviceName(device), active))
        != QMessageBox::Yes)
        return;
    m_config->removeProfile(device, active);
    m_manager->switchProfile(device, m_config->activeProfile(device));
}

void MainWindow::showHotkeys()
{
    HotkeysDialog dlg(m_manager, m_config, this);
    dlg.exec();
}

void MainWindow::editPrograms()
{
    const RazerDevice *dev = current();
    if (!dev)
        return;
    const QString device = dev->profileKey;
    ProgramsDialog dlg(m_config, device, m_config->activeProfile(device), this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    QString error;
    if (!m_config->save(&error))
        QMessageBox::warning(this, tr("Could not save"), error);
    m_autoSwitcher->update();
    refreshAutoInfo();
}

void MainWindow::refreshAutoInfo()
{
    const RazerDevice *dev = current();
    m_programsButton->setEnabled(dev != nullptr);
    if (!dev) {
        m_autoInfo->clear();
        return;
    }
    const QString device = dev->profileKey;
    const QString active = m_config->activeProfile(device);
    const QString running = m_autoSwitcher->activeProgram(device);
    const QStringList programs = m_config->programs(device, active);
    const QString link = QStringLiteral(" <a href='#'>%1</a>").arg(tr("Edit"));
    if (!running.isEmpty() && !m_config->autoRestore(device).isEmpty())
        m_autoInfo->setText(tr("Switched automatically: %1 is running").arg(running.toHtmlEscaped()));
    else if (!programs.isEmpty())
        m_autoInfo->setText(tr("Switches automatically for %1").arg(programs.join(QStringLiteral(", ")).toHtmlEscaped())
                            + (m_config->autoSwitch ? QString() : tr(" (turned off in Settings)")) + link);
    else
        m_autoInfo->clear();
}
