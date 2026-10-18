#include "macrodialog.h"

#include "bindingdialog.h"
#include "remapmanager.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include <linux/input-event-codes.h>

namespace {

// Asks for a key or button; -1 when cancelled.
int pickKey(QWidget *parent, const QString &title, int current)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(title);
    auto *picker = new KeyPicker(Keys::targetCodes(), false);
    if (current > 0)
        picker->setCode(current);
    else
        picker->setCurrentIndex(-1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    auto *layout = new QVBoxLayout(&dlg);
    layout->addWidget(picker);
    layout->addWidget(buttons);
    return dlg.exec() == QDialog::Accepted && picker->code() > 0 ? picker->code() : -1;
}

} // namespace

MacroDialog::MacroDialog(RemapManager *manager, const QList<MacroStep> &steps, QWidget *parent)
    : QDialog(parent)
    , m_steps(steps)
{
    setWindowTitle(tr("Edit macro"));

    m_list = new QListWidget;
    m_list->setMinimumSize(300, 320);
    m_summary = new QLabel;
    m_summary->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));

    m_record = new QPushButton(tr("Record"));
    m_record->setToolTip(tr("Press keys and buttons on your Razer devices; they're added after the selected step"));
    m_recordPauses = new QCheckBox(tr("Record pauses"));
    m_recordPauses->setChecked(true);
    m_recordPauses->setToolTip(tr("Keep the time between presses; untick for steps right after each other"));
    auto *addKey = new QPushButton(tr("Add key…"));
    addKey->setToolTip(tr("A press and release of one key or button"));
    auto *addPause = new QPushButton(tr("Add pause…"));
    auto *edit = new QPushButton(tr("Edit…"));
    auto *up = new QPushButton(tr("Move up"));
    auto *down = new QPushButton(tr("Move down"));
    auto *remove = new QPushButton(tr("Remove"));
    auto *clear = new QPushButton(tr("Clear"));
    m_editButtons = {addKey, addPause, edit, up, down, remove, clear};

    auto *side = new QVBoxLayout;
    side->addWidget(m_record);
    side->addWidget(m_recordPauses);
    side->addSpacing(12);
    for (QPushButton *b : std::as_const(m_editButtons))
        side->addWidget(b);
    side->addStretch();
    for (QPushButton *b : std::initializer_list<QPushButton *>{m_record, addKey, addPause, edit, up, down, remove, clear})
        b->setAutoDefault(false);

    auto *row = new QHBoxLayout;
    row->addWidget(m_list, 1);
    row->addLayout(side);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &MacroDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(row);
    layout->addWidget(m_summary);
    layout->addWidget(buttons);

    connect(m_record, &QPushButton::clicked, this, [this] { m_recording ? stopRecording() : startRecording(); });
    connect(manager, &RemapManager::inputEvent, this, &MacroDialog::onInput);
    connect(addKey, &QPushButton::clicked, this, [this] {
        const int code = pickKey(this, tr("Add key"), 0);
        if (code > 0)
            insert({{MacroStep::Press, code}, {MacroStep::Release, code}});
    });
    connect(addPause, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const int ms = QInputDialog::getInt(this, tr("Add pause"), tr("Pause (ms):"), 50, 1, Keys::MacroMaxDelay, 10, &ok);
        if (ok)
            insert({{MacroStep::Delay, ms}});
    });
    connect(edit, &QPushButton::clicked, this, [this] { editStep(m_list->currentRow()); });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this] { editStep(m_list->currentRow()); });
    connect(up, &QPushButton::clicked, this, [this] { move(-1); });
    connect(down, &QPushButton::clicked, this, [this] { move(1); });
    connect(remove, &QPushButton::clicked, this, [this] {
        const int row = m_list->currentRow();
        if (row < 0)
            return;
        m_steps.removeAt(row);
        refresh(std::min(row, int(m_steps.size()) - 1));
    });
    connect(clear, &QPushButton::clicked, this, [this] {
        m_steps.clear();
        refresh();
    });
    refresh(int(m_steps.size()) - 1);
}

void MacroDialog::refresh(int select)
{
    m_list->clear();
    qint64 total = 0;
    for (const MacroStep &st : std::as_const(m_steps)) {
        auto *item = new QListWidgetItem(Keys::macroStepLabel(st));
        if (st.kind == MacroStep::Delay) {
            item->setForeground(palette().color(QPalette::PlaceholderText));
            total += st.value;
        }
        m_list->addItem(item);
    }
    if (select >= 0 && select < m_list->count())
        m_list->setCurrentRow(select);
    m_list->scrollToItem(m_list->currentItem());
    m_summary->setText(m_steps.isEmpty()
                           ? tr("No steps yet. Press Record and use your keyboard or mouse, or add steps by hand.")
                           : tr("%1, %2 ms of pauses").arg(Keys::macroSummary(m_steps)).arg(total));
}

void MacroDialog::insert(const QList<MacroStep> &steps)
{
    int at = m_list->currentRow() < 0 ? int(m_steps.size()) : m_list->currentRow() + 1;
    for (const MacroStep &st : steps) {
        if (m_steps.size() >= Keys::MacroMaxSteps)
            break;
        m_steps.insert(at++, st);
    }
    refresh(at - 1);
}

void MacroDialog::startRecording()
{
    m_recording = true;
    m_record->setText(tr("Stop recording"));
    for (QPushButton *b : std::as_const(m_editButtons))
        b->setEnabled(false);
    m_recordPauses->setEnabled(false);
    m_sinceLast.invalidate();
    // Keys go into the macro, not into this dialog.
    qApp->installEventFilter(this);
}

void MacroDialog::stopRecording()
{
    if (!m_recording)
        return;
    m_recording = false;
    qApp->removeEventFilter(this);
    m_record->setText(tr("Record"));
    for (QPushButton *b : std::as_const(m_editButtons))
        b->setEnabled(true);
    m_recordPauses->setEnabled(true);
    // The click on "Stop recording" got recorded too: drop its press (and
    // its release, if that arrived first), with the pause before each.
    int row = m_list->currentRow();
    auto dropIf = [this, &row](MacroStep::Kind kind) {
        if (row < 0 || m_steps[row].kind != kind || m_steps[row].value != BTN_LEFT)
            return;
        m_steps.removeAt(row--);
        if (row >= 0 && m_steps[row].kind == MacroStep::Delay)
            m_steps.removeAt(row--);
    };
    dropIf(MacroStep::Release);
    dropIf(MacroStep::Press);
    refresh(row);
}

void MacroDialog::onInput(const QString &, int code, bool pressed)
{
    if (!m_recording || !Keys::isValidTarget(code))
        return; // the scroll wheel can't be replayed as a key
    QList<MacroStep> steps;
    if (m_recordPauses->isChecked() && m_sinceLast.isValid() && m_sinceLast.elapsed() > 0)
        steps << MacroStep{MacroStep::Delay, int(std::min<qint64>(m_sinceLast.elapsed(), Keys::MacroMaxDelay))};
    m_sinceLast.start();
    steps << MacroStep{pressed ? MacroStep::Press : MacroStep::Release, code};
    insert(steps);
}

void MacroDialog::editStep(int row)
{
    if (row < 0 || row >= m_steps.size())
        return;
    MacroStep &st = m_steps[row];
    if (st.kind == MacroStep::Delay) {
        bool ok = false;
        const int ms = QInputDialog::getInt(this, tr("Edit pause"), tr("Pause (ms):"), st.value, 0, Keys::MacroMaxDelay,
                                            10, &ok);
        if (ok)
            st.value = ms;
    } else {
        const int code = pickKey(this, st.kind == MacroStep::Press ? tr("Press which key?") : tr("Release which key?"),
                                 st.value);
        if (code > 0)
            st.value = code;
    }
    refresh(row);
}

void MacroDialog::move(int by)
{
    const int row = m_list->currentRow();
    const int to = row + by;
    if (row < 0 || to < 0 || to >= m_steps.size())
        return;
    m_steps.swapItemsAt(row, to);
    refresh(to);
}

bool MacroDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (m_recording)
        switch (event->type()) {
        case QEvent::KeyPress:
        case QEvent::KeyRelease:
        case QEvent::ShortcutOverride:
            return true;
        default:
            break;
        }
    return QDialog::eventFilter(watched, event);
}

void MacroDialog::reject()
{
    stopRecording();
    QDialog::reject();
}
