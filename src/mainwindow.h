#pragma once

#include "razerdevice.h"

#include <QHash>
#include <QMainWindow>

class QAction;
class QCheckBox;
class QLabel;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QSystemTrayIcon;
class QTableWidget;
class DeviceView;
class Profile;
class RemapManager;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(Profile *profile, RemapManager *manager, QWidget *parent = nullptr);

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

    const RazerDevice *current() const;
    DeviceView *currentView() const;

    Profile *m_profile;
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
    QSystemTrayIcon *m_tray = nullptr;
    bool m_trayHintShown = false;

    QHash<QString, QString> m_detailsCache;
};
