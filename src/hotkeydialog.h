#pragma once

#include "config.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;
class InputDetector;
class KeyPicker;
class RemapManager;

// Edits one hotkey: trigger (modifiers + key/button) and action.
class HotkeyEditDialog : public QDialog
{
    Q_OBJECT
public:
    HotkeyEditDialog(RemapManager *manager, const Config *config, QWidget *parent = nullptr);

    void setHotkey(const Hotkey &hotkey);
    Hotkey hotkey() const;

    void accept() override;

private:
    void updateState();
    void fillProfiles();

    QCheckBox *m_ctrl;
    QCheckBox *m_shift;
    QCheckBox *m_alt;
    QCheckBox *m_super;
    KeyPicker *m_key;
    QPushButton *m_detect;
    InputDetector *m_detector;
    QComboBox *m_action;
    QComboBox *m_device;
    QComboBox *m_profile;
    const Config *m_config;
    QLabel *m_preview;
};

// Lists all hotkeys; changes are saved and applied immediately.
class HotkeysDialog : public QDialog
{
    Q_OBJECT
public:
    HotkeysDialog(RemapManager *manager, Config *config, QWidget *parent = nullptr);

private:
    void refresh();
    void edit(int index); // -1 adds
    void commit();

    RemapManager *m_manager;
    Config *m_config;
    QTableWidget *m_table;
    QPushButton *m_edit;
    QPushButton *m_remove;
};
