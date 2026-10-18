#pragma once

#include "keys.h"

#include <QDialog>
#include <QElapsedTimer>

class QCheckBox;
class QLabel;
class QListWidget;
class QPushButton;
class RemapManager;

// Records and edits the steps of a macro.
class MacroDialog : public QDialog
{
    Q_OBJECT

public:
    MacroDialog(RemapManager *manager, const QList<MacroStep> &steps, QWidget *parent = nullptr);

    QList<MacroStep> steps() const { return m_steps; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void reject() override;

private:
    void refresh(int select = -1);
    void insert(const QList<MacroStep> &steps);
    void startRecording();
    void stopRecording();
    void onInput(const QString &device, int code, bool pressed);
    void editStep(int row);
    void move(int by);

    QList<MacroStep> m_steps;
    QListWidget *m_list;
    QLabel *m_summary;
    QPushButton *m_record;
    QCheckBox *m_recordPauses;
    QList<QPushButton *> m_editButtons; // disabled while recording
    bool m_recording = false;
    QElapsedTimer m_sinceLast;
};
