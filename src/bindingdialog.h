#pragma once

#include "razerdevice.h"

#include <QComboBox>
#include <QDialog>
#include <QPushButton>
#include <QSet>

class QCheckBox;
class QLabel;
class QRadioButton;
class InputDetector;
class RemapManager;

// Searchable list of key / button names.
class KeyPicker : public QComboBox
{
    Q_OBJECT
public:
    KeyPicker(const QList<int> &codes, bool allowNone, QWidget *parent = nullptr);
    int code() const; // -1 when the text matches nothing, 0 for "(none)"
    void setCode(int code);

signals:
    void codeChanged();
};

// Button that records the next shortcut typed on any keyboard.
class KeyRecorder : public QPushButton
{
    Q_OBJECT
public:
    explicit KeyRecorder(QWidget *parent = nullptr);

signals:
    void recorded(const QList<int> &codes);

protected:
    bool event(QEvent *e) override;
    void focusOutEvent(QFocusEvent *e) override;

private:
    void begin();
    void end();

    bool m_recording = false;
    QList<int> m_order;
    QSet<int> m_down;
};

class BindingDialog : public QDialog
{
    Q_OBJECT
public:
    BindingDialog(RemapManager *manager, const RazerDevice &device, QWidget *parent = nullptr);

    void setSource(int code);
    void setTargets(const QList<int> &targets);
    int source() const;
    QList<int> targets() const;

    void accept() override;

private:
    void updateState();

    KeyPicker *m_source;
    QPushButton *m_detect;
    QRadioButton *m_remap;
    QRadioButton *m_disable;
    QCheckBox *m_ctrl;
    QCheckBox *m_shift;
    QCheckBox *m_alt;
    QCheckBox *m_super;
    KeyPicker *m_key;
    KeyRecorder *m_record;
    QLabel *m_preview;
    InputDetector *m_detector;
};
