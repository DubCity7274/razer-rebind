#include "inputdetector.h"

#include "keys.h"
#include "remapmanager.h"

#include <QApplication>
#include <QKeyEvent>
#include <QPushButton>

InputDetector::InputDetector(RemapManager *manager, QPushButton *button, QObject *parent)
    : QObject(parent)
    , m_button(button)
    , m_buttonText(button->text())
{
    connect(button, &QPushButton::clicked, this, [this] { m_active ? stop() : start(); });
    connect(manager, &RemapManager::inputEvent, this, &InputDetector::onInput);
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        if (--m_countdown <= 0)
            stop();
        else
            m_button->setText(tr("Waiting… %1").arg(m_countdown));
    });
}

void InputDetector::setSwallow(bool keys, bool mouse)
{
    m_swallowKeys = keys;
    m_swallowMouse = mouse;
}

void InputDetector::start()
{
    if (m_active)
        return;
    m_active = true;
    m_detected = -1;
    m_heldModifiers.clear();
    m_countdown = 10;
    m_button->setText(tr("Waiting… %1").arg(m_countdown));
    m_timer.start();
    if (!m_filtering) {
        qApp->installEventFilter(this);
        m_filtering = true;
    }
}

void InputDetector::stop()
{
    if (!m_active)
        return;
    m_active = false;
    m_timer.stop();
    m_button->setText(m_buttonText);
    // Keep swallowing a little longer: the window system can deliver its copy
    // of the release after ours.
    QTimer::singleShot(200, this, [this] {
        if (!m_active && m_filtering) {
            qApp->removeEventFilter(this);
            m_filtering = false;
        }
    });
}

void InputDetector::onInput(const QString &profileKey, int code, bool pressed)
{
    if (!m_active || (!m_device.isEmpty() && profileKey != m_device))
        return;

    if (m_collectModifiers && Keys::isModifier(code)) {
        if (pressed)
            m_heldModifiers.insert(code);
        else
            m_heldModifiers.remove(code);
        return;
    }
    if (pressed && m_detected < 0) {
        m_detected = code;
        unsigned mods = 0;
        for (int m : std::as_const(m_heldModifiers))
            mods |= Keys::modifierBit(m);
        emit detected(code, mods);
    }
    if (!pressed && code == m_detected)
        stop();
}

bool InputDetector::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::KeyPress:
        // With keys left alone, Esc still cancels.
        if (!m_swallowKeys && m_active && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
            stop();
            return true;
        }
        [[fallthrough]];
    case QEvent::KeyRelease:
    case QEvent::ShortcutOverride:
        if (m_swallowKeys)
            return true;
        break;
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::Wheel:
        if (m_swallowMouse)
            return true;
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}
