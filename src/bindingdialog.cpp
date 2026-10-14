#include "bindingdialog.h"

#include "keys.h"
#include "remapmanager.h"

#include <QApplication>
#include <QCheckBox>
#include <QCompleter>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QRadioButton>
#include <QVBoxLayout>

#include <linux/input-event-codes.h>

#include <algorithm>

// --- KeyPicker --------------------------------------------------------------

KeyPicker::KeyPicker(const QList<int> &codes, bool allowNone, QWidget *parent)
    : QComboBox(parent)
{
    setEditable(true);
    setInsertPolicy(QComboBox::NoInsert);
    setMaxVisibleItems(20);
    setMinimumContentsLength(22);
    if (allowNone)
        addItem(tr("(none)"), 0);
    for (int c : codes)
        addItem(QStringLiteral("%1   [%2]").arg(Keys::label(c), Keys::name(c)), c);

    auto *completer = new QCompleter(model(), this);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    completer->setCompletionMode(QCompleter::PopupCompletion);
    setCompleter(completer);
    lineEdit()->setPlaceholderText(tr("Type to search, e.g. \"ctrl\" or \"volume\""));

    connect(this, &QComboBox::currentIndexChanged, this, &KeyPicker::codeChanged);
    connect(this, &QComboBox::editTextChanged, this, &KeyPicker::codeChanged);
}

int KeyPicker::code() const
{
    const int i = findText(currentText(), Qt::MatchFixedString);
    return i >= 0 ? itemData(i).toInt() : -1;
}

void KeyPicker::setCode(int code)
{
    const int i = findData(code);
    if (i >= 0)
        setCurrentIndex(i);
}

// --- KeyRecorder ------------------------------------------------------------

KeyRecorder::KeyRecorder(QWidget *parent)
    : QPushButton(tr("Record shortcut"), parent)
{
    setToolTip(tr("Click, then press the key combination to send"));
    connect(this, &QPushButton::clicked, this, [this] { m_recording ? end() : begin(); });
}

void KeyRecorder::begin()
{
    m_recording = true;
    m_order.clear();
    m_down.clear();
    setText(tr("Press keys…"));
    setFocus();
    grabKeyboard();
}

void KeyRecorder::end()
{
    m_recording = false;
    releaseKeyboard();
    setText(tr("Record shortcut"));
}

bool KeyRecorder::event(QEvent *e)
{
    if (!m_recording)
        return QPushButton::event(e);

    switch (e->type()) {
    case QEvent::ShortcutOverride:
        e->accept();
        return true;
    case QEvent::KeyPress:
    case QEvent::KeyRelease: {
        auto *ke = static_cast<QKeyEvent *>(e);
        // On both X11 and Wayland the native scan code is the XKB keycode,
        // which is the evdev code + 8.
        const int code = int(ke->nativeScanCode()) - 8;
        if (ke->isAutoRepeat() || code <= 0 || !Keys::isValidTarget(code))
            return true;
        if (e->type() == QEvent::KeyPress) {
            m_down.insert(code);
            if (!m_order.contains(code))
                m_order << code;
        } else {
            m_down.remove(code);
            if (m_down.isEmpty() && !m_order.isEmpty()) {
                QList<int> combo = m_order;
                std::stable_partition(combo.begin(), combo.end(), Keys::isModifier);
                end();
                emit recorded(combo);
            }
        }
        return true;
    }
    default:
        return QPushButton::event(e);
    }
}

void KeyRecorder::focusOutEvent(QFocusEvent *e)
{
    if (m_recording)
        end();
    QPushButton::focusOutEvent(e);
}

// --- BindingDialog ----------------------------------------------------------

BindingDialog::BindingDialog(RemapManager *manager, const RazerDevice &device, QWidget *parent)
    : QDialog(parent)
    , m_manager(manager)
    , m_device(device)
{
    setWindowTitle(tr("Edit binding — %1").arg(device.profileKey));

    // Input
    m_source = new KeyPicker(Keys::sourceCodes(), false);
    m_detect = new QPushButton(tr("Detect"));
    m_detect->setToolTip(tr("Press the key or button on the %1 itself").arg(device.profileKey));
    auto *inputRow = new QHBoxLayout;
    inputRow->addWidget(m_source, 1);
    inputRow->addWidget(m_detect);
    auto *inputBox = new QGroupBox(tr("When this is pressed on the %1").arg(device.profileKey));
    inputBox->setLayout(inputRow);

    // Output
    m_remap = new QRadioButton(tr("Send key or shortcut"));
    m_disable = new QRadioButton(tr("Do nothing (disable)"));
    m_remap->setChecked(true);
    m_ctrl = new QCheckBox(tr("Ctrl"));
    m_shift = new QCheckBox(tr("Shift"));
    m_alt = new QCheckBox(tr("Alt"));
    m_super = new QCheckBox(tr("Super"));
    m_key = new KeyPicker(Keys::targetCodes(), true);
    m_record = new KeyRecorder;

    auto *modRow = new QHBoxLayout;
    for (QCheckBox *cb : {m_ctrl, m_shift, m_alt, m_super})
        modRow->addWidget(cb);
    modRow->addWidget(new QLabel(QStringLiteral("+")));
    modRow->addWidget(m_key, 1);
    auto *recordRow = new QHBoxLayout;
    recordRow->addStretch();
    recordRow->addWidget(m_record);

    auto *outLayout = new QVBoxLayout;
    outLayout->addWidget(m_remap);
    auto *indent = new QVBoxLayout;
    indent->setContentsMargins(24, 0, 0, 0);
    indent->addLayout(modRow);
    indent->addLayout(recordRow);
    outLayout->addLayout(indent);
    outLayout->addWidget(m_disable);
    auto *outputBox = new QGroupBox(tr("Do this instead"));
    outputBox->setLayout(outLayout);

    m_preview = new QLabel;
    m_preview->setTextFormat(Qt::RichText);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setMinimumHeight(32);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &BindingDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &BindingDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(inputBox);
    layout->addWidget(outputBox);
    layout->addWidget(m_preview);
    layout->addWidget(buttons);

    connect(m_source, &KeyPicker::codeChanged, this, &BindingDialog::updateState);
    connect(m_key, &KeyPicker::codeChanged, this, &BindingDialog::updateState);
    for (QAbstractButton *b : std::initializer_list<QAbstractButton *>{m_remap, m_disable, m_ctrl, m_shift, m_alt, m_super})
        connect(b, &QAbstractButton::toggled, this, &BindingDialog::updateState);
    connect(m_record, &KeyRecorder::recorded, this, [this](const QList<int> &codes) { setTargets(codes); });
    connect(m_detect, &QPushButton::clicked, this, &BindingDialog::startDetect);
    connect(m_manager, &RemapManager::inputEvent, this, &BindingDialog::onInput);

    m_detectTimer.setInterval(1000);
    connect(&m_detectTimer, &QTimer::timeout, this, [this] {
        if (--m_countdown <= 0)
            stopDetect();
        else
            m_detect->setText(tr("Waiting… %1").arg(m_countdown));
    });

    m_key->setCode(0);
    m_source->setCurrentIndex(-1);
    updateState();
}

void BindingDialog::setSource(int code)
{
    m_source->setCode(code);
    updateState();
}

void BindingDialog::setTargets(const QList<int> &targets)
{
    if (targets.isEmpty()) {
        m_disable->setChecked(true);
        return;
    }
    m_remap->setChecked(true);
    m_ctrl->setChecked(targets.contains(KEY_LEFTCTRL));
    m_shift->setChecked(targets.contains(KEY_LEFTSHIFT));
    m_alt->setChecked(targets.contains(KEY_LEFTALT));
    m_super->setChecked(targets.contains(KEY_LEFTMETA));
    int key = 0;
    for (int c : targets)
        if (c != KEY_LEFTCTRL && c != KEY_LEFTSHIFT && c != KEY_LEFTALT && c != KEY_LEFTMETA)
            key = c;
    m_key->setCode(key);
    updateState();
}

int BindingDialog::source() const
{
    return m_source->code();
}

QList<int> BindingDialog::targets() const
{
    if (m_disable->isChecked())
        return {};
    QList<int> out;
    if (m_ctrl->isChecked()) out << KEY_LEFTCTRL;
    if (m_shift->isChecked()) out << KEY_LEFTSHIFT;
    if (m_alt->isChecked()) out << KEY_LEFTALT;
    if (m_super->isChecked()) out << KEY_LEFTMETA;
    const int key = m_key->code();
    if (key > 0 && !out.contains(key))
        out << key;
    return out;
}

void BindingDialog::updateState()
{
    const bool remap = m_remap->isChecked();
    for (QWidget *w : std::initializer_list<QWidget *>{m_ctrl, m_shift, m_alt, m_super, m_key, m_record})
        w->setEnabled(remap);

    const int src = source();
    const QList<int> out = targets();
    QString left = src > 0 ? Keys::label(src).toHtmlEscaped() : tr("<i>choose an input</i>");
    QString right;
    if (!remap)
        right = tr("<span style='color:#c44'>nothing</span>");
    else if (out.isEmpty() || m_key->code() < 0)
        right = tr("<i>choose a key</i>");
    else
        right = Keys::comboLabel(out).toHtmlEscaped();
    m_preview->setText(QStringLiteral("<b>%1</b> &nbsp;→&nbsp; <b>%2</b>").arg(left, right));
}

void BindingDialog::accept()
{
    if (source() <= 0) {
        QMessageBox::warning(this, windowTitle(), tr("Choose the key or button to rebind."));
        return;
    }
    if (m_remap->isChecked() && (targets().isEmpty() || m_key->code() < 0)) {
        QMessageBox::warning(this, windowTitle(), tr("Choose the key or shortcut to send, or pick \"Do nothing\"."));
        return;
    }
    stopDetect();
    QDialog::accept();
}

// Detection listens to the physical device through its Remapper, so it works
// for mouse buttons and keys alike, and even while the device is grabbed.
void BindingDialog::startDetect()
{
    if (m_detecting) {
        stopDetect();
        return;
    }
    m_detecting = true;
    m_detected = -1;
    m_countdown = 10;
    m_detect->setText(tr("Waiting… %1").arg(m_countdown));
    m_source->setEnabled(false);
    m_detectTimer.start();
    // Keep the press from also triggering things in this dialog (Enter would
    // accept it, a click would hit a button).
    qApp->installEventFilter(this);
}

void BindingDialog::stopDetect()
{
    if (!m_detecting)
        return;
    m_detecting = false;
    m_detectTimer.stop();
    m_detect->setText(tr("Detect"));
    m_source->setEnabled(true);
    // Swallow input a little longer: the window system can deliver its copy of
    // the release after ours.
    QTimer::singleShot(200, this, [this] {
        if (!m_detecting)
            qApp->removeEventFilter(this);
    });
}

void BindingDialog::onInput(const QString &profileKey, int code, bool pressed)
{
    if (!m_detecting || profileKey != m_device.profileKey)
        return;
    if (pressed && m_detected < 0) {
        m_detected = code;
        setSource(code);
    }
    if (!pressed && code == m_detected)
        stopDetect();
}

bool BindingDialog::eventFilter(QObject *watched, QEvent *event)
{
    const bool keyboard = m_device.kind == RazerDevice::Keyboard;
    switch (event->type()) {
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
    case QEvent::ShortcutOverride:
        if (keyboard)
            return true;
        if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
            stopDetect();
            return true;
        }
        break;
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::Wheel:
        if (!keyboard)
            return true;
        break;
    default:
        break;
    }
    return QDialog::eventFilter(watched, event);
}
