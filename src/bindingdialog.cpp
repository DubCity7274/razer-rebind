#include "bindingdialog.h"

#include "inputdetector.h"
#include "keys.h"
#include "macrodialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QRadioButton>
#include <QSpinBox>
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

BindingDialog::BindingDialog(RemapManager *manager, const RazerDevice &device, bool hypershiftLayer,
                             QWidget *parent)
    : QDialog(parent)
    , m_manager(manager)
{
    setWindowTitle(hypershiftLayer ? tr("Edit Hypershift binding — %1").arg(device.profileKey)
                                   : tr("Edit binding — %1").arg(device.profileKey));

    // Input
    m_source = new KeyPicker(Keys::sourceCodes(), false);
    m_detect = new QPushButton(tr("Detect"));
    m_detect->setToolTip(tr("Press the key or button on the %1 itself").arg(device.profileKey));
    auto *inputRow = new QHBoxLayout;
    inputRow->addWidget(m_source, 1);
    inputRow->addWidget(m_detect);
    // Combos: only while exactly these modifiers are held.
    m_srcCtrl = new QCheckBox(tr("Ctrl"));
    m_srcShift = new QCheckBox(tr("Shift"));
    m_srcAlt = new QCheckBox(tr("Alt"));
    m_srcSuper = new QCheckBox(tr("Super"));
    auto *comboRow = new QHBoxLayout;
    comboRow->addWidget(new QLabel(tr("Only while holding:")));
    for (QCheckBox *cb : {m_srcCtrl, m_srcShift, m_srcAlt, m_srcSuper}) {
        cb->setToolTip(tr("For a combo such as Ctrl + Shift + S; the key alone keeps its own binding"));
        comboRow->addWidget(cb);
    }
    comboRow->addStretch();
    auto *inputLayout = new QVBoxLayout;
    inputLayout->addLayout(inputRow);
    inputLayout->addLayout(comboRow);
    auto *inputBox = new QGroupBox(hypershiftLayer
                                       ? tr("When this is pressed on the %1 while Hypershift is held").arg(device.profileKey)
                                       : tr("When this is pressed on the %1").arg(device.profileKey));
    inputBox->setLayout(inputLayout);

    // Output
    m_remap = new QRadioButton(tr("Send key or shortcut"));
    m_disable = new QRadioButton(tr("Do nothing (disable)"));
    m_hypershift = new QRadioButton(tr("Hypershift: while held, all devices use their Hypershift bindings"));
    m_hypershift->setVisible(!hypershiftLayer);
    m_macro = new QRadioButton(tr("Play a macro"));
    m_macroEdit = new QPushButton(tr("Edit macro…"));
    m_macroEdit->setAutoDefault(false);
    m_macroMode = new QComboBox;
    m_macroMode->addItem(tr("Play once"), Binding::PlayOnce);
    m_macroMode->addItem(tr("Repeat while held"), Binding::RepeatWhileHeld);
    m_macroMode->addItem(tr("Press to start, press again to stop"), Binding::Toggle);
    m_command = new QRadioButton(tr("Run a command"));
    m_commandEdit = new QLineEdit;
    m_commandEdit->setPlaceholderText(tr("e.g. steam, or ~/bin/toggle-vpn.sh, or playerctl play-pause"));
    m_commandEdit->setToolTip(tr("Runs through the shell, as you, in the background, once per press"));
    m_commandBrowse = new QPushButton(tr("Browse…"));
    m_commandBrowse->setAutoDefault(false);
    m_macroSummary = new QLabel;
    m_macroSummary->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    m_remap->setChecked(true);
    m_turbo = new QCheckBox(tr("Turbo: repeat while held"));
    m_turbo->setToolTip(tr("Press and release the output over and over for as long as the input is held"));
    m_turboRate = new QSpinBox;
    m_turboRate->setRange(Keys::TurboMin, Keys::TurboMax);
    m_turboRate->setValue(Keys::TurboDefault);
    m_turboRate->setSuffix(tr(" per second"));
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
    recordRow->addWidget(m_turbo);
    recordRow->addWidget(m_turboRate);
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
    outLayout->addWidget(m_hypershift);
    outLayout->addWidget(m_macro);
    auto *macroRow = new QHBoxLayout;
    macroRow->setContentsMargins(24, 0, 0, 0);
    macroRow->addWidget(m_macroEdit);
    macroRow->addWidget(m_macroMode);
    macroRow->addWidget(m_macroSummary, 1);
    outLayout->addLayout(macroRow);
    outLayout->addWidget(m_command);
    auto *commandRow = new QHBoxLayout;
    commandRow->setContentsMargins(24, 0, 0, 0);
    commandRow->addWidget(m_commandEdit, 1);
    commandRow->addWidget(m_commandBrowse);
    outLayout->addLayout(commandRow);
    auto *outputBox = new QGroupBox(tr("Do this instead"));
    outputBox->setLayout(outLayout);

    // When held: tap does the above, hold does this.
    m_holdBox = new QGroupBox(tr("Do something else when held"));
    m_holdBox->setCheckable(true);
    m_holdBox->setChecked(false);
    m_holdBox->setToolTip(tr("A tap does what's chosen above; holding the input does this instead"));
    m_holdKeys = new QRadioButton(tr("Send"));
    m_holdKeys->setChecked(true);
    m_holdHypershift = new QRadioButton(tr("Hypershift"));
    m_holdHypershift->setVisible(!hypershiftLayer);
    m_holdCtrl = new QCheckBox(tr("Ctrl"));
    m_holdShift = new QCheckBox(tr("Shift"));
    m_holdAlt = new QCheckBox(tr("Alt"));
    m_holdSuper = new QCheckBox(tr("Super"));
    m_holdKey = new KeyPicker(Keys::targetCodes(), true);
    m_holdKey->setCode(0);
    m_holdTime = new QSpinBox;
    m_holdTime->setRange(Keys::HoldTimeMin, Keys::HoldTimeMax);
    m_holdTime->setSingleStep(25);
    m_holdTime->setValue(Keys::HoldTimeDefault);
    m_holdTime->setSuffix(tr(" ms"));
    auto *holdKeysRow = new QHBoxLayout;
    holdKeysRow->addWidget(m_holdKeys);
    for (QCheckBox *cb : {m_holdCtrl, m_holdShift, m_holdAlt, m_holdSuper})
        holdKeysRow->addWidget(cb);
    holdKeysRow->addWidget(new QLabel(QStringLiteral("+")));
    holdKeysRow->addWidget(m_holdKey, 1);
    auto *holdTimeRow = new QHBoxLayout;
    holdTimeRow->addWidget(m_holdHypershift);
    holdTimeRow->addStretch();
    holdTimeRow->addWidget(new QLabel(tr("Counts as held after")));
    holdTimeRow->addWidget(m_holdTime);
    auto *holdNote = new QLabel(tr("Pressing and releasing another key while holding also counts as held, "
                                   "so a quick Caps Lock + C gives Ctrl + C."));
    holdNote->setWordWrap(true);
    holdNote->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    auto *holdLayout = new QVBoxLayout(m_holdBox);
    holdLayout->addLayout(holdKeysRow);
    holdLayout->addLayout(holdTimeRow);
    holdLayout->addWidget(holdNote);

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
    layout->addWidget(m_holdBox);
    layout->addWidget(m_preview);
    layout->addWidget(buttons);

    connect(m_source, &KeyPicker::codeChanged, this, &BindingDialog::updateState);
    connect(m_key, &KeyPicker::codeChanged, this, &BindingDialog::updateState);
    for (QAbstractButton *b : std::initializer_list<QAbstractButton *>{m_remap, m_disable, m_hypershift, m_macro, m_command, m_ctrl, m_shift, m_alt, m_super, m_turbo, m_holdKeys,
          m_holdHypershift, m_holdCtrl, m_holdShift, m_holdAlt, m_holdSuper})
        connect(b, &QAbstractButton::toggled, this, &BindingDialog::updateState);
    connect(m_holdBox, &QGroupBox::toggled, this, &BindingDialog::updateState);
    connect(m_macroMode, &QComboBox::currentIndexChanged, this, &BindingDialog::updateState);
    connect(m_commandEdit, &QLineEdit::textEdited, this, [this] {
        m_command->setChecked(true);
        updateState();
    });
    connect(m_commandBrowse, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Choose a program or script"), QDir::homePath());
        if (path.isEmpty())
            return;
        // Quoted for the shell, in case of spaces.
        QString quoted = path;
        quoted.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
        m_commandEdit->setText(QLatin1Char('\'') + quoted + QLatin1Char('\''));
        m_command->setChecked(true);
        updateState();
    });
    connect(m_macroEdit, &QPushButton::clicked, this, [this] {
        // Detecting the input and recording would both listen to the device.
        m_detector->stop();
        MacroDialog dlg(m_manager, m_macroSteps, this);
        if (dlg.exec() == QDialog::Accepted) {
            m_macroSteps = dlg.steps();
            m_macro->setChecked(true);
        }
        updateState();
    });
    connect(m_holdKey, &KeyPicker::codeChanged, this, &BindingDialog::updateState);
    connect(m_turboRate, &QSpinBox::valueChanged, this, &BindingDialog::updateState);
    connect(m_record, &KeyRecorder::recorded, this, [this](const QList<int> &codes) {
        Binding b = binding();
        b.targets = codes;
        setBinding(b);
    });
    // Detecting on the keyboard swallows typing into the dialog (Enter would
    // accept it); on the mouse it swallows clicks, and Esc cancels.
    const bool keyboard = device.kind == RazerDevice::Keyboard;
    m_detector = new InputDetector(manager, m_detect, this);
    m_detector->setDevice(device.profileKey);
    m_detector->setSwallow(keyboard, !keyboard);
    // Modifiers held while detecting make a combo; a modifier pressed on its
    // own is the input itself.
    m_detector->setCollectModifiers(true);
    m_detector->setModifierAlone(true);
    connect(m_detector, &InputDetector::detected, this,
            [this](int code, unsigned mods) { setSource(Keys::trigger(mods, code)); });
    for (QCheckBox *cb : {m_srcCtrl, m_srcShift, m_srcAlt, m_srcSuper})
        connect(cb, &QCheckBox::toggled, this, &BindingDialog::updateState);

    m_key->setCode(0);
    m_source->setCurrentIndex(-1);
    updateState();
}

void BindingDialog::setSource(int trigger)
{
    m_source->setCode(Keys::triggerCode(trigger));
    const unsigned mods = Keys::triggerModifiers(trigger);
    m_srcCtrl->setChecked(mods & Keys::ModCtrl);
    m_srcShift->setChecked(mods & Keys::ModShift);
    m_srcAlt->setChecked(mods & Keys::ModAlt);
    m_srcSuper->setChecked(mods & Keys::ModSuper);
    updateState();
}

void BindingDialog::setBinding(const Binding &binding)
{
    const QList<int> &targets = binding.targets;
    m_macroSteps = binding.macro;
    m_macroMode->setCurrentIndex(m_macroMode->findData(binding.macroMode));
    m_commandEdit->setText(binding.command);
    if (binding.isCommand()) {
        m_holdBox->setChecked(false);
        m_turbo->setChecked(false);
        m_command->setChecked(true);
        updateState();
        return;
    }
    if (binding.isMacro()) {
        m_holdBox->setChecked(false);
        m_turbo->setChecked(false);
        m_macro->setChecked(true);
        updateState();
        return;
    }
    m_holdBox->setChecked(binding.isTapHold());
    if (binding.isTapHold()) {
        m_holdTime->setValue(binding.effectiveHoldTime());
        const bool hyper = binding.hold == QList<int>{Keys::Hypershift};
        (hyper && !m_holdHypershift->isHidden() ? m_holdHypershift : m_holdKeys)->setChecked(true);
        m_holdCtrl->setChecked(binding.hold.contains(KEY_LEFTCTRL));
        m_holdShift->setChecked(binding.hold.contains(KEY_LEFTSHIFT));
        m_holdAlt->setChecked(binding.hold.contains(KEY_LEFTALT));
        m_holdSuper->setChecked(binding.hold.contains(KEY_LEFTMETA));
        int key = 0;
        for (int c : binding.hold)
            if (c != KEY_LEFTCTRL && c != KEY_LEFTSHIFT && c != KEY_LEFTALT && c != KEY_LEFTMETA && c != Keys::Hypershift)
                key = c;
        m_holdKey->setCode(key);
    }
    m_turbo->setChecked(binding.turbo > 0);
    if (binding.turbo > 0)
        m_turboRate->setValue(binding.turbo);
    if (binding.isHypershift() && !m_hypershift->isHidden()) {
        m_hypershift->setChecked(true);
        return;
    }
    if (targets.isEmpty() || binding.isHypershift()) {
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
    const int code = m_source->code();
    if (code <= 0 || Keys::isModifier(code))
        return code;
    unsigned mods = 0;
    if (m_srcCtrl->isChecked()) mods |= Keys::ModCtrl;
    if (m_srcShift->isChecked()) mods |= Keys::ModShift;
    if (m_srcAlt->isChecked()) mods |= Keys::ModAlt;
    if (m_srcSuper->isChecked()) mods |= Keys::ModSuper;
    return Keys::trigger(mods, code);
}

Binding BindingDialog::binding() const
{
    Binding b;
    if (m_command->isChecked()) {
        b.command = m_commandEdit->text().trimmed();
        return b;
    }
    if (m_macro->isChecked()) {
        b.macro = m_macroSteps;
        b.macroMode = Binding::MacroMode(m_macroMode->currentData().toInt());
        return b;
    }
    if (m_hypershift->isChecked()) {
        b.targets = {Keys::Hypershift};
        return b;
    }
    b.targets = targets();
    if (m_holdBox->isChecked() && !Keys::isWheel(Keys::triggerCode(source()))) {
        b.hold = holdTargets();
        b.holdTime = m_holdTime->value() == Keys::HoldTimeDefault ? 0 : m_holdTime->value();
        if (!b.hold.isEmpty())
            return b;
    }
    if (m_remap->isChecked() && m_turbo->isChecked() && !Keys::isWheel(Keys::triggerCode(source())))
        b.turbo = m_turboRate->value();
    return b;
}

QList<int> BindingDialog::holdTargets() const
{
    if (m_holdHypershift->isChecked())
        return {Keys::Hypershift};
    QList<int> out;
    if (m_holdCtrl->isChecked()) out << KEY_LEFTCTRL;
    if (m_holdShift->isChecked()) out << KEY_LEFTSHIFT;
    if (m_holdAlt->isChecked()) out << KEY_LEFTALT;
    if (m_holdSuper->isChecked()) out << KEY_LEFTMETA;
    const int key = m_holdKey->code();
    if (key > 0 && !out.contains(key))
        out << key;
    return out;
}

QList<int> BindingDialog::targets() const
{
    if (!m_remap->isChecked())
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
    const int src = source();
    // A modifier can't need itself held; combos are for other keys.
    const bool modifierInput = Keys::isModifier(m_source->code());
    for (QCheckBox *cb : {m_srcCtrl, m_srcShift, m_srcAlt, m_srcSuper})
        cb->setEnabled(!modifierInput);
    // A wheel notch can't be held, so it can't repeat or hold a layer.
    const bool wheel = Keys::isWheel(Keys::triggerCode(src));
    if (wheel && m_hypershift->isChecked())
        m_remap->setChecked(true);
    // A macro is its own thing: no tap/hold or turbo on top. A wheel notch
    // can't be held, so it can only play once.
    const bool macro = m_macro->isChecked() || m_command->isChecked(); // neither combines with tap/hold
    if (macro && m_holdBox->isChecked())
        m_holdBox->setChecked(false);
    if (wheel && m_macroMode->currentIndex() != 0)
        m_macroMode->setCurrentIndex(0);
    m_macroMode->setEnabled(m_macro->isChecked() && !wheel);
    m_macroEdit->setEnabled(true);
    m_macroSummary->setText(m_macroSteps.isEmpty() ? tr("no steps yet") : Keys::macroSummary(m_macroSteps));
    // A tap is over at once, so it can't hold the Hypershift layer or repeat.
    const bool tapHold = m_holdBox->isChecked() && !wheel && !macro;
    if (tapHold && m_hypershift->isChecked())
        m_remap->setChecked(true);
    m_hypershift->setEnabled(!wheel && !tapHold);
    m_holdBox->setEnabled(!wheel && !macro);
    const bool remap = m_remap->isChecked();
    for (QWidget *w : std::initializer_list<QWidget *>{m_ctrl, m_shift, m_alt, m_super, m_key, m_record})
        w->setEnabled(remap);
    m_turbo->setEnabled(remap && !wheel && !tapHold);
    m_turboRate->setEnabled(remap && !wheel && !tapHold && m_turbo->isChecked());
    for (QWidget *w : std::initializer_list<QWidget *>{m_holdCtrl, m_holdShift, m_holdAlt, m_holdSuper, m_holdKey})
        w->setEnabled(m_holdKeys->isChecked());

    const QList<int> out = targets();
    QString left = src > 0 ? Keys::triggerLabel(src).toHtmlEscaped() : tr("<i>choose an input</i>");
    QString right;
    if (m_command->isChecked())
        right = m_commandEdit->text().trimmed().isEmpty()
                    ? tr("<i>enter a command</i>")
                    : tr("run <code>%1</code>").arg(m_commandEdit->text().trimmed().toHtmlEscaped());
    else if (m_macro->isChecked())
        right = m_macroSteps.isEmpty() ? tr("<i>record or add steps</i>") : Keys::bindingLabel(binding()).toHtmlEscaped();
    else if (tapHold && !holdTargets().isEmpty())
        right = Keys::bindingLabel(binding()).toHtmlEscaped();
    else if (m_hypershift->isChecked())
        right = tr("<span style='color:#96e'>Hypershift while held</span>");
    else if (!remap)
        right = tr("<span style='color:#c44'>nothing</span>");
    else if (out.isEmpty() || m_key->code() < 0)
        right = tr("<i>choose a key</i>");
    else
        right = Keys::bindingLabel(binding()).toHtmlEscaped();
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
    if (m_command->isChecked() && m_commandEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, windowTitle(), tr("Enter the command to run."));
        return;
    }
    if (m_macro->isChecked() && m_macroSteps.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), tr("The macro has no steps yet. Use Edit macro to record or add them."));
        return;
    }
    if (m_holdBox->isChecked() && !Keys::isWheel(Keys::triggerCode(source())) && (holdTargets().isEmpty() || (m_holdKeys->isChecked() && m_holdKey->code() < 0))) {
        QMessageBox::warning(this, windowTitle(), tr("Choose what holding it sends, or untick \"Do something else when held\"."));
        return;
    }
    m_detector->stop();
    QDialog::accept();
}

