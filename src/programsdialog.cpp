#include "programsdialog.h"

#include "config.h"
#include "processes.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

ProgramsDialog::ProgramsDialog(Config *config, const QString &device, const QString &profile, QWidget *parent)
    : QDialog(parent)
    , m_config(config)
    , m_device(device)
    , m_profile(profile)
{
    setWindowTitle(tr("Switch automatically — %1").arg(profile));

    auto *intro = new QLabel(tr("Switch the %1 to “%2” while one of these programs is running. When the "
                                "last one exits, the profile it had before comes back.")
                                 .arg(device, profile));
    intro->setWordWrap(true);

    m_list = new QListWidget;
    m_list->addItems(config->programs(device, profile));
    m_list->setMinimumHeight(140);

    auto *pick = new QPushButton(tr("Pick running program…"));
    auto *byName = new QPushButton(tr("Add by name…"));
    m_remove = new QPushButton(tr("Remove"));
    m_remove->setEnabled(false);
    auto *side = new QVBoxLayout;
    for (QPushButton *b : {pick, byName, m_remove})
        side->addWidget(b);
    side->addStretch();
    auto *row = new QHBoxLayout;
    row->addWidget(m_list, 1);
    row->addLayout(side);

    auto *note = new QLabel(tr("Start the game, then use <b>Pick running program</b>. Games running through "
                               "Wine or Proton (Steam, Lutris, Heroic) are listed by their <code>.exe</code> "
                               "name."));
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    if (!config->autoSwitch)
        note->setText(note->text() + tr("<br><b>Automatic switching is turned off in Settings.</b>"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &ProgramsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &ProgramsDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addLayout(row);
    layout->addWidget(note);
    layout->addWidget(buttons);
    resize(520, sizeHint().height());

    connect(pick, &QPushButton::clicked, this, &ProgramsDialog::pickRunning);
    connect(byName, &QPushButton::clicked, this, &ProgramsDialog::addByName);
    connect(m_remove, &QPushButton::clicked, this, [this] { delete m_list->currentItem(); });
    connect(m_list, &QListWidget::itemSelectionChanged, this,
            [this] { m_remove->setEnabled(!m_list->selectedItems().isEmpty()); });
}

void ProgramsDialog::addProgram(const QString &name)
{
    const QString program = Processes::baseName(name);
    if (program.isEmpty())
        return;
    for (int i = 0; i < m_list->count(); ++i)
        if (m_list->item(i)->text().compare(program, Qt::CaseInsensitive) == 0)
            return;
    m_list->addItem(program);
    m_list->setCurrentRow(m_list->count() - 1);
}

void ProgramsDialog::addByName()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Add program"),
                                               tr("Program name, as in a task manager (e.g. <code>cs2</code> or "
                                                  "<code>eldenring.exe</code>):"),
                                               QLineEdit::Normal, QString(), &ok);
    if (ok)
        addProgram(name);
}

void ProgramsDialog::pickRunning()
{
    // One entry per program name, newest first: the game just started is
    // likely near the top.
    QList<Processes::Process> running = Processes::list();
    std::sort(running.begin(), running.end(),
              [](const Processes::Process &a, const Processes::Process &b) { return a.startTime > b.startTime; });
    QStringList names;
    for (const Processes::Process &p : std::as_const(running))
        if (!names.contains(p.name, Qt::CaseInsensitive))
            names << p.name;

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Pick running program"));
    auto *filter = new QLineEdit;
    filter->setPlaceholderText(tr("Search"));
    filter->setClearButtonEnabled(true);
    auto *list = new QListWidget;
    list->addItems(names);
    list->setCurrentRow(0);
    auto *hint = new QLabel(tr("Most recently started first."));
    hint->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(list, &QListWidget::itemDoubleClicked, &dlg, &QDialog::accept);
    connect(filter, &QLineEdit::textChanged, list, [list](const QString &text) {
        int first = -1;
        for (int i = 0; i < list->count(); ++i) {
            const bool show = list->item(i)->text().contains(text, Qt::CaseInsensitive);
            list->item(i)->setHidden(!show);
            if (show && first < 0)
                first = i;
        }
        list->setCurrentRow(first);
    });
    auto *layout = new QVBoxLayout(&dlg);
    layout->addWidget(filter);
    layout->addWidget(list);
    layout->addWidget(hint);
    layout->addWidget(buttons);
    dlg.resize(380, 460);
    filter->setFocus();
    if (dlg.exec() == QDialog::Accepted && list->currentItem() && !list->currentItem()->isHidden())
        addProgram(list->currentItem()->text());
}

void ProgramsDialog::accept()
{
    QStringList programs;
    for (int i = 0; i < m_list->count(); ++i)
        programs << m_list->item(i)->text();

    // A program can switch a device to only one profile.
    QStringList taken;
    for (const QString &other : m_config->profileNames(m_device)) {
        if (other == m_profile)
            continue;
        for (const QString &p : m_config->programs(m_device, other))
            if (programs.contains(p, Qt::CaseInsensitive))
                taken << tr("%1 (now “%2”)").arg(p, other);
    }
    if (!taken.isEmpty()
        && QMessageBox::question(this, windowTitle(),
                                 tr("These programs already switch the %1 to another profile:\n%2\n\nMove them to “%3”?")
                                     .arg(m_device, taken.join(QLatin1Char('\n')), m_profile))
               != QMessageBox::Yes)
        return;
    for (const QString &other : m_config->profileNames(m_device)) {
        if (other == m_profile)
            continue;
        QStringList keep = m_config->programs(m_device, other);
        keep.erase(std::remove_if(keep.begin(), keep.end(),
                                  [&](const QString &p) { return programs.contains(p, Qt::CaseInsensitive); }),
                   keep.end());
        m_config->setPrograms(m_device, other, keep);
    }
    m_config->setPrograms(m_device, m_profile, programs);
    QDialog::accept();
}
