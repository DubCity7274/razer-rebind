#include "mainwindow.h"

#include "bindingdialog.h"
#include "deviceview.h"
#include "profile.h"
#include "remapmanager.h"
#include "remapper.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
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

MainWindow::MainWindow(Profile *profile, RemapManager *manager, QWidget *parent)
    : QMainWindow(parent)
    , m_profile(profile)
    , m_manager(manager)
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

    auto *header = new QGridLayout;
    header->addWidget(m_title, 0, 0);
    header->addWidget(m_deviceEnabled, 0, 1, Qt::AlignRight);
    header->addWidget(m_details, 1, 0, 1, 2);
    header->addWidget(m_state, 2, 0, 1, 2);

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

    auto *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(m_toggleAction);
    fileMenu->addAction(rescanAction);
    fileMenu->addSeparator();
    fileMenu->addAction(quitAction);
    auto *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("Device permissions…"), this, &MainWindow::showPermissionHelp);
    helpMenu->addAction(tr("About"), this, [this] {
        QMessageBox::about(this, tr("About Razer Rebind"),
                           tr("<b>Razer Rebind</b><p>Key and button rebinding for Razer devices driven by the "
                              "OpenRazer kernel modules.</p><p>Bindings are stored in<br><code>%1</code></p>")
                               .arg(m_profile->path().toHtmlEscaped()));
    });

    // Wiring
    connect(m_devices, &QListWidget::currentRowChanged, this, &MainWindow::selectDevice);
    connect(m_deviceEnabled, &QCheckBox::toggled, this, [this](bool on) {
        const RazerDevice *dev = current();
        if (!dev)
            return;
        DeviceProfile p = m_profile->device(dev->profileKey);
        if (p.enabled == on)
            return;
        p.enabled = on;
        m_profile->setDevice(dev->profileKey, p);
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

    setupTray();
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
    menu->addAction(tr("Show window"), this, [this] {
        showNormal();
        raise();
        activateWindow();
    });
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
        if (dev->kind == RazerDevice::Mouse) {
            const QString charge = dev->attribute("charge_level");
            if (!charge.isEmpty())
                parts << tr("battery %1%").arg(qRound(charge.toInt() * 100.0 / 255.0));
        }
        parts << dev->eventNodes.join(QStringLiteral(", "));
        m_detailsCache.insert(dev->hidPath, parts.join(QStringLiteral("  ·  ")));
    }
    m_details->setText(m_detailsCache.value(dev->hidPath));
    m_views->setCurrentIndex(dev->kind == RazerDevice::Mouse ? 2 : 1);
    {
        QSignalBlocker b(m_deviceEnabled);
        m_deviceEnabled->setEnabled(true);
        m_deviceEnabled->setChecked(m_profile->device(dev->profileKey).enabled);
    }
    refreshBindings();
    refreshState();
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
        item->setText(dev->name + QLatin1Char('\n') + status);
    }

    const RazerDevice *dev = current();
    if (!dev) {
        m_state->clear();
        return;
    }
    Remapper *r = m_manager->remapper(dev->hidPath);
    const QString error = m_manager->errorFor(dev->hidPath);
    const DeviceProfile p = m_profile->device(dev->profileKey);
    QString html;
    if (!error.isEmpty() || !r) {
        html = QStringLiteral("<span style='color:#d33'>⚠ %1</span> — <a href='#'>%2</a>")
                   .arg(error.toHtmlEscaped(), tr("how to fix permissions"));
    } else if (r->isGrabbed()) {
        html = tr("<span style='color:#2a2'>● Remapping active</span> — %n binding(s) applied", nullptr,
                  int(p.bindings.size()));
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

void MainWindow::refreshBindings()
{
    const RazerDevice *dev = current();
    const BindingMap bindings = dev ? m_profile->device(dev->profileKey).bindings : BindingMap();

    m_keyboardView->setBindings(dev && dev->kind == RazerDevice::Keyboard ? bindings : BindingMap());
    m_mouseView->setBindings(dev && dev->kind == RazerDevice::Mouse ? bindings : BindingMap());

    QList<int> sources = bindings.keys();
    std::sort(sources.begin(), sources.end());
    m_table->setRowCount(0);
    for (int src : sources) {
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        auto *in = new QTableWidgetItem(Keys::label(src));
        in->setData(Qt::UserRole, src);
        in->setToolTip(Keys::name(src));
        auto *out = new QTableWidgetItem(Keys::comboLabel(bindings.value(src)));
        if (bindings.value(src).isEmpty())
            out->setForeground(QColor(0xd3, 0x44, 0x44));
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
    const BindingMap bindings = m_profile->device(device.profileKey).bindings;

    BindingDialog dlg(m_manager, device, this);
    if (sourceCode > 0) {
        dlg.setSource(sourceCode);
        // An unbound key starts out mapped to itself, the natural starting point.
        if (bindings.contains(sourceCode))
            dlg.setTargets(bindings.value(sourceCode));
        else if (Keys::isValidTarget(sourceCode))
            dlg.setTargets({sourceCode});
    }
    if (dlg.exec() != QDialog::Accepted)
        return;

    const int src = dlg.source();
    if (src != sourceCode && bindings.contains(src)
        && QMessageBox::question(this, tr("Replace binding?"),
                                 tr("%1 already has a binding. Replace it?").arg(Keys::label(src)))
               != QMessageBox::Yes)
        return;
    storeBinding(sourceCode, src, dlg.targets());
}

void MainWindow::storeBinding(int oldSource, int source, const QList<int> &targets)
{
    const RazerDevice *dev = current();
    if (!dev)
        return;
    DeviceProfile p = m_profile->device(dev->profileKey);
    if (oldSource > 0)
        p.bindings.remove(oldSource);
    // Mapping a key to exactly itself is the same as having no binding.
    if (targets == QList<int>{source})
        p.bindings.remove(source);
    else
        p.bindings.insert(source, targets);
    m_profile->setDevice(dev->profileKey, p);
    saveProfile();
}

void MainWindow::removeSelectedBinding()
{
    const RazerDevice *dev = current();
    QTableWidgetItem *item = m_table->item(m_table->currentRow(), 0);
    if (!dev || !item)
        return;
    DeviceProfile p = m_profile->device(dev->profileKey);
    p.bindings.remove(item->data(Qt::UserRole).toInt());
    m_profile->setDevice(dev->profileKey, p);
    saveProfile();
}

void MainWindow::clearBindings()
{
    const RazerDevice *dev = current();
    if (!dev)
        return;
    if (QMessageBox::question(this, tr("Clear all bindings"),
                              tr("Remove every binding for the %1?").arg(dev->profileKey))
        != QMessageBox::Yes)
        return;
    DeviceProfile p = m_profile->device(dev->profileKey);
    p.bindings.clear();
    m_profile->setDevice(dev->profileKey, p);
    saveProfile();
}

void MainWindow::saveProfile()
{
    QString error;
    if (!m_profile->save(&error))
        QMessageBox::warning(this, tr("Could not save bindings"), error);
    m_manager->applyProfile();
    refreshBindings();
    refreshState();
}

void MainWindow::showPermissionHelp()
{
    QMessageBox box(this);
    box.setWindowTitle(tr("Device permissions"));
    box.setTextFormat(Qt::RichText);
    box.setText(tr("<p>Razer Rebind needs to read the Razer event devices in <code>/dev/input</code> "
                   "and create a virtual device through <code>/dev/uinput</code>.</p>"
                   "<p>Install the bundled udev rule, which grants this to the user logged in at the "
                   "seat (not to other users or remote sessions):</p>"
                   "<pre>sudo install -m644 data/70-razer-rebind.rules /etc/udev/rules.d/\n"
                   "sudo udevadm control --reload\n"
                   "sudo udevadm trigger</pre>"
                   "<p>Then press <b>Rescan devices</b> (F5).</p>"));
    box.exec();
}
