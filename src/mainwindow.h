#pragma once

#include "keys.h"
#include "razerdevice.h"

#include <QHash>
#include <QMainWindow>

class QAction;
class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QMenu;
class QPushButton;
class QStackedWidget;
class QSystemTrayIcon;
class QTableWidget;
class DeviceView;
class GenericView;
class SensorPanel;
class AutoSwitcher;
class BatteryMonitor;
class Config;
class RemapManager;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(Config *config, RemapManager *manager, BatteryMonitor *battery, AutoSwitcher *autoSwitcher,
               QWidget *parent = nullptr);

    bool hasTray() const;

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    void rebuildDeviceList();
    void selectDevice();
    void refreshState();
    void refreshDetails();
    QString batteryText(const QString &hidPath) const;
    void refreshBindings();
    void editBinding(int sourceCode);
    void removeSelectedBinding();
    void clearBindings();
    void storeBinding(int oldSource, int source, const Binding &binding);
    void setHypershiftLayer(bool on);
    QString hypershiftKeysText() const;
    void saveProfile();
    void showPermissionHelp();
    void setupTray();
    void refreshProfiles();
    void fillProfileMenu(QMenu *menu);
    void newProfile(bool duplicate);
    void renameProfile();
    void deleteProfile();
    void exportProfile();
    void importProfile();
    void editPrograms();
    void refreshAutoInfo();
    void showHotkeys();
    void showFromBackground();
    void refreshTrayIcon();

    const RazerDevice *current() const;
    DeviceView *currentView() const;

    Config *m_config;
    RemapManager *m_manager;
    BatteryMonitor *m_battery;
    AutoSwitcher *m_autoSwitcher;
    QLabel *m_autoInfo;
    QPushButton *m_programsButton;
    QString m_currentPath; // hidPath of the selected device

    QListWidget *m_devices;
    QLabel *m_title;
    QLabel *m_details;
    QLabel *m_state;
    QCheckBox *m_deviceEnabled;
    QStackedWidget *m_views;
    DeviceView *m_keyboardView;
    DeviceView *m_mouseView;
    GenericView *m_genericView;
    QTableWidget *m_table;
    SensorPanel *m_sensor;
    QPushButton *m_editButton;
    QPushButton *m_removeButton;
    QPushButton *m_clearButton;
    QPushButton *m_layerNormal;
    QPushButton *m_layerHypershift;
    QLabel *m_layerHint;
    bool m_hypershiftLayer = false; // editing the Hypershift layer instead of the normal one
    QAction *m_toggleAction;
    QComboBox *m_profileCombo;
    QMenu *m_profileMenu;
    QMenu *m_trayProfileMenu = nullptr;
    QMenu *m_deviceProfileMenu;
    QLabel *m_profileStatus;
    QSystemTrayIcon *m_tray = nullptr;
    bool m_trayHintShown = false;
    QString m_trayIconKey; // what the tray icon shows, to redraw only on change

    QHash<QString, QString> m_detailsCache;
};
