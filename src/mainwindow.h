#pragma once

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
class Config;
class RemapManager;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(Config *config, RemapManager *manager, QWidget *parent = nullptr);

    bool hasTray() const;

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    void rebuildDeviceList();
    void selectDevice();
    void refreshState();
    void refreshBindings();
    void editBinding(int sourceCode);
    void removeSelectedBinding();
    void clearBindings();
    void storeBinding(int oldSource, int source, const QList<int> &targets);
    void saveProfile();
    void showPermissionHelp();
    void setupTray();
    void refreshProfiles();
    void fillProfileMenu(QMenu *menu);
    void newProfile(bool duplicate);
    void renameProfile();
    void deleteProfile();
    void showHotkeys();
    void showFromBackground();

    const RazerDevice *current() const;
    DeviceView *currentView() const;

    Config *m_config;
    RemapManager *m_manager;
    QString m_currentPath; // hidPath of the selected device

    QListWidget *m_devices;
    QLabel *m_title;
    QLabel *m_details;
    QLabel *m_state;
    QCheckBox *m_deviceEnabled;
    QStackedWidget *m_views;
    DeviceView *m_keyboardView;
    DeviceView *m_mouseView;
    QTableWidget *m_table;
    QPushButton *m_editButton;
    QPushButton *m_removeButton;
    QPushButton *m_clearButton;
    QAction *m_toggleAction;
    QComboBox *m_profileCombo;
    QMenu *m_profileMenu;
    QMenu *m_trayProfileMenu = nullptr;
    QLabel *m_profileStatus;
    QSystemTrayIcon *m_tray = nullptr;
    bool m_trayHintShown = false;

    QHash<QString, QString> m_detailsCache;
};
