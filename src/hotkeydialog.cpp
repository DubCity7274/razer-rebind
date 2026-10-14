#include "hotkeydialog.h"

#include "bindingdialog.h"
#include "inputdetector.h"
#include "remapmanager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <linux/input-event-codes.h>

namespace {

QList<int> triggerCodes()
{
    QList<int> out;
    for (int c : Keys::sourceCodes())
        if (!Keys::isModifier(c))
            out << c;
    return out;
}

} // namespace

// --- HotkeyEditDialog -----------------------------------------------------

HotkeyEditDialog::HotkeyEditDialog(RemapManager *manager, const Config *config, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Edit hotkey"));

    m_ctrl = new QCheckBox(tr("Ctrl"));
    m_shift = new QCheckBox(tr("Shift"));
    m_alt = new QCheckBox(tr("Alt"));
    m_super = new QCheckBox(tr("Super"));
    m_key = new KeyPicker(triggerCodes(), false);
    m_detect = new QPushButton(tr("Detect"));
    m_detect->setToolTip(tr("Hold the modifiers and press the key or button on a Razer device"));

    auto *triggerRow = new QHBoxLayout;
    for (QCheckBox *cb : {m_ctrl, m_shift, m_alt, m_super})
        triggerRow->addWidget(cb);
    triggerRow->addWidget(new QLabel(QStringLiteral("+")));
    triggerRow->addWidget(m_key, 1);
    triggerRow->addWidget(m_detect);
    auto *triggerNote = new QLabel(tr("Works with keys and buttons on your Razer devices, including Ctrl on the "
                                      "keyboard + a mouse button. A hotkey takes priority over a binding on the "
                                      "same key."));
    triggerNote->setWordWrap(true);
    triggerNote->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    auto *triggerLayout = new QVBoxLayout;
    triggerLayout->addLayout(triggerRow);
    triggerLayout->addWidget(triggerNote);
    auto *triggerBox = new QGroupBox(tr("Shortcut"));
    triggerBox->setLayout(triggerLayout);

    m_action = new QComboBox;
    m_action->addItem(tr("Switch to profile"), Hotkey::SwitchProfile);
    m_action->addItem(tr("Next profile"), Hotkey::NextProfile);
    m_action->addItem(tr("Previous profile"), Hotkey::PreviousProfile);
    m_action->addItem(tr("Turn remapping on/off"), Hotkey::ToggleRemapping);
    m_profile = new QComboBox;
    m_profile->addItems(config->profileNames());
    m_profile->setCurrentText(config->activeProfile());
    auto *actionLayout = new QFormLayout;
    actionLayout->addRow(tr("Action:"), m_action);
    actionLayout->addRow(tr("Profile:"), m_profile);
    auto *actionBox = new QGroupBox(tr("Does"));
    actionBox->setLayout(actionLayout);

    m_preview = new QLabel;
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setMinimumHeight(28);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &HotkeyEditDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &HotkeyEditDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(triggerBox);
    layout->addWidget(actionBox);
    layout->addWidget(m_preview);
    layout->addWidget(buttons);

    m_detector = new InputDetector(manager, m_detect, this);
    m_detector->setCollectModifiers(true);
    m_detector->setSwallow(true, true);
    connect(m_detector, &InputDetector::detected, this, [this](int code, unsigned mods) {
        m_ctrl->setChecked(mods & Keys::ModCtrl);
        m_shift->setChecked(mods & Keys::ModShift);
        m_alt->setChecked(mods & Keys::ModAlt);
        m_super->setChecked(mods & Keys::ModSuper);
        m_key->setCode(code);
    });

    for (QCheckBox *cb : {m_ctrl, m_shift, m_alt, m_super})
        connect(cb, &QCheckBox::toggled, this, &HotkeyEditDialog::updateState);
    connect(m_key, &KeyPicker::codeChanged, this, &HotkeyEditDialog::updateState);
    connect(m_action, &QComboBox::currentIndexChanged, this, &HotkeyEditDialog::updateState);
    connect(m_profile, &QComboBox::currentIndexChanged, this, &HotkeyEditDialog::updateState);

    m_ctrl->setChecked(true);
    m_alt->setChecked(true);
    m_key->setCurrentIndex(-1);
    updateState();
}

void HotkeyEditDialog::setHotkey(const Hotkey &h)
{
    m_ctrl->setChecked(h.modifiers & Keys::ModCtrl);
    m_shift->setChecked(h.modifiers & Keys::ModShift);
    m_alt->setChecked(h.modifiers & Keys::ModAlt);
    m_super->setChecked(h.modifiers & Keys::ModSuper);
    m_key->setCode(h.code);
    m_action->setCurrentIndex(m_action->findData(h.action));
    if (h.action == Hotkey::SwitchProfile)
        m_profile->setCurrentText(h.profile);
    updateState();
}

Hotkey HotkeyEditDialog::hotkey() const
{
    Hotkey h;
    if (m_ctrl->isChecked()) h.modifiers |= Keys::ModCtrl;
    if (m_shift->isChecked()) h.modifiers |= Keys::ModShift;
    if (m_alt->isChecked()) h.modifiers |= Keys::ModAlt;
    if (m_super->isChecked()) h.modifiers |= Keys::ModSuper;
    h.code = m_key->code();
    h.action = Hotkey::Action(m_action->currentData().toInt());
    if (h.action == Hotkey::SwitchProfile)
        h.profile = m_profile->currentText();
    return h;
}

void HotkeyEditDialog::updateState()
{
    const Hotkey h = hotkey();
    m_profile->setEnabled(h.action == Hotkey::SwitchProfile);
    m_preview->setText(QStringLiteral("<b>%1</b> &nbsp;→&nbsp; %2")
                           .arg(h.code > 0 ? h.triggerLabel().toHtmlEscaped() : tr("<i>choose a key</i>"),
                                h.actionLabel().toHtmlEscaped()));
}

void HotkeyEditDialog::accept()
{
    const Hotkey h = hotkey();
    if (h.code <= 0) {
        QMessageBox::warning(this, windowTitle(), tr("Choose the key or button for the shortcut."));
        return;
    }
    if (h.modifiers == 0 && (h.code == BTN_LEFT || h.code == BTN_RIGHT)) {
        QMessageBox::warning(this, windowTitle(),
                             tr("A plain left or right click can't be a hotkey: you couldn't click anymore. "
                                "Add a modifier."));
        return;
    }
    if (h.modifiers == 0 && h.code < BTN_MISC
        && QMessageBox::question(this, windowTitle(),
                                 tr("Without a modifier, %1 will stop typing its normal key everywhere. "
                                    "Use it anyway?").arg(Keys::label(h.code)))
               != QMessageBox::Yes)
        return;
    m_detector->stop();
    QDialog::accept();
}

// --- HotkeysDialog --------------------------------------------------------

HotkeysDialog::HotkeysDialog(RemapManager *manager, Config *config, QWidget *parent)
    : QDialog(parent)
    , m_manager(manager)
    , m_config(config)
{
    setWindowTitle(tr("Profile hotkeys"));
    resize(560, 360);

    m_table = new QTableWidget(0, 2);
    m_table->setHorizontalHeaderLabels({tr("Shortcut"), tr("Action")});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->verticalHeader()->hide();
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);

    auto *add = new QPushButton(tr("Add…"));
    m_edit = new QPushButton(tr("Edit…"));
    m_remove = new QPushButton(tr("Remove"));
    auto *buttonCol = new QVBoxLayout;
    for (QPushButton *b : {add, m_edit, m_remove})
        buttonCol->addWidget(b);
    buttonCol->addStretch();
    auto *row = new QHBoxLayout;
    row->addWidget(m_table, 1);
    row->addLayout(buttonCol);

    auto *notifications = new QCheckBox(tr("Show a desktop notification when a hotkey switches profile"));
    notifications->setChecked(m_config->notifications);
    connect(notifications, &QCheckBox::toggled, this, [this](bool on) {
        m_config->notifications = on;
        commit();
    });

    auto *close = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(close, &QDialogButtonBox::rejected, this, &QDialog::accept);

    auto *intro = new QLabel(tr("Hotkeys work in every profile and in any application."));
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addLayout(row, 1);
    layout->addWidget(notifications);
    layout->addWidget(close);

    connect(add, &QPushButton::clicked, this, [this] { edit(-1); });
    connect(m_edit, &QPushButton::clicked, this, [this] { edit(m_table->currentRow()); });
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int r) { edit(r); });
    connect(m_remove, &QPushButton::clicked, this, [this] {
        const int r = m_table->currentRow();
        if (r < 0 || r >= m_config->hotkeys.size())
            return;
        m_config->hotkeys.removeAt(r);
        commit();
    });
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this] {
        const bool any = !m_table->selectedItems().isEmpty();
        m_edit->setEnabled(any);
        m_remove->setEnabled(any);
    });
    refresh();
}

void HotkeysDialog::refresh()
{
    m_table->setRowCount(0);
    for (const Hotkey &h : std::as_const(m_config->hotkeys)) {
        const int r = m_table->rowCount();
        m_table->insertRow(r);
        m_table->setItem(r, 0, new QTableWidgetItem(h.triggerLabel()));
        m_table->setItem(r, 1, new QTableWidgetItem(h.actionLabel()));
    }
    m_edit->setEnabled(false);
    m_remove->setEnabled(false);
}

void HotkeysDialog::edit(int index)
{
    HotkeyEditDialog dlg(m_manager, m_config, this);
    if (index >= 0)
        dlg.setHotkey(m_config->hotkeys.value(index));
    if (dlg.exec() != QDialog::Accepted)
        return;
    const Hotkey h = dlg.hotkey();
    for (int i = 0; i < m_config->hotkeys.size(); ++i) {
        const Hotkey &other = m_config->hotkeys[i];
        if (i != index && other.code == h.code && other.modifiers == h.modifiers) {
            if (QMessageBox::question(this, windowTitle(),
                                      tr("%1 is already used for “%2”. Replace it?")
                                          .arg(h.triggerLabel(), other.actionLabel()))
                != QMessageBox::Yes)
                return;
            m_config->hotkeys.removeAt(i);
            if (index > i)
                --index;
            break;
        }
    }
    if (index >= 0)
        m_config->hotkeys[index] = h;
    else
        m_config->hotkeys << h;
    commit();
}

void HotkeysDialog::commit()
{
    QString error;
    if (!m_config->save(&error))
        QMessageBox::warning(this, windowTitle(), tr("Could not save: %1").arg(error));
    m_manager->applyProfile();
    emit m_manager->profileChanged(); // menus show the hotkeys next to profiles
    refresh();
}
