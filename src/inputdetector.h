#pragma once

#include <QObject>
#include <QSet>
#include <QTimer>

class QPushButton;
class RemapManager;

// "Press the key on the device" helper behind a Detect button. Listens to the
// physical Razer devices through the RemapManager, so it sees mouse buttons and
// keys alike, even while they are grabbed, and keeps the press from also
// clicking or typing into the dialog.
class InputDetector : public QObject
{
    Q_OBJECT

public:
    InputDetector(RemapManager *manager, QPushButton *button, QObject *parent = nullptr);

    void setDevice(const QString &profileKey) { m_device = profileKey; } // empty: any device
    // Collect held modifiers and finish on the first other key (hotkeys),
    // instead of finishing on the very first press (bindings).
    void setCollectModifiers(bool collect) { m_collectModifiers = collect; }
    void setSwallow(bool keys, bool mouse);

    bool isActive() const { return m_active; }
    void start();
    void stop();

signals:
    void detected(int code, unsigned modifiers);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void onInput(const QString &profileKey, int code, bool pressed);

    QPushButton *m_button;
    QString m_buttonText;
    QString m_device;
    bool m_collectModifiers = false;
    bool m_swallowKeys = true;
    bool m_swallowMouse = false;
    bool m_active = false;
    bool m_filtering = false;
    int m_detected = -1;
    int m_countdown = 0;
    QSet<int> m_heldModifiers;
    QTimer m_timer;
};
