#include "autoswitcher.h"

#include "config.h"
#include "processes.h"
#include "remapmanager.h"

AutoSwitcher::AutoSwitcher(RemapManager *manager, Config *config, QObject *parent)
    : QObject(parent)
    , m_manager(manager)
    , m_config(config)
{
    // Reading /proc takes a few milliseconds; every two seconds is quick
    // enough that the profile is in place before a game has finished loading.
    m_timer.setInterval(2000);
    connect(&m_timer, &QTimer::timeout, this, &AutoSwitcher::update);
    m_timer.start();
    QTimer::singleShot(0, this, &AutoSwitcher::update);
}

void AutoSwitcher::update()
{
    // Which profile each device should use, and for which program.
    bool anyRules = false;
    for (const QString &device : m_config->deviceKeys())
        for (const QString &profile : m_config->profileNames(device))
            anyRules |= !m_config->programs(device, profile).isEmpty();
    const QList<Processes::Process> running = (m_config->autoSwitch && anyRules) ? Processes::list()
                                                                                : QList<Processes::Process>();

    QHash<QString, QString> switches;
    QStringList started, exited;
    for (const QString &device : m_config->deviceKeys()) {
        QString wantProfile, wantProgram;
        quint64 newest = 0;
        for (const QString &profile : m_config->profileNames(device))
            for (const QString &program : m_config->programs(device, profile))
                for (const Processes::Process &p : running)
                    if (Processes::matches(p, program) && (wantProfile.isEmpty() || p.startTime > newest)) {
                        wantProfile = profile;
                        wantProgram = p.name;
                        newest = p.startTime;
                    }

        const QString restore = m_config->autoRestore(device);
        const QString active = m_config->activeProfile(device);
        if (!wantProfile.isEmpty()) {
            // Only act when the program changes, so a profile picked by hand
            // while it runs stays.
            if (m_profile.value(device) == wantProfile && m_program.value(device) == wantProgram)
                continue;
            if (restore.isEmpty())
                m_config->setAutoRestore(device, active);
            if (!m_profile.contains(device) || m_program.value(device) != wantProgram)
                started << wantProgram;
            m_profile.insert(device, wantProfile);
            m_program.insert(device, wantProgram);
            switches.insert(device, wantProfile);
        } else if (!restore.isEmpty()) {
            // The program exited (or a restart found it gone): go back, unless
            // a profile was picked by hand in the meantime. After a restart
            // the switched-to profile isn't known; any profile with programs
            // counts as one.
            const bool stillAuto = m_profile.contains(device) ? active == m_profile.value(device)
                                                              : !m_config->programs(device, active).isEmpty();
            if (m_program.contains(device))
                exited << m_program.value(device);
            m_config->setAutoRestore(device, QString());
            m_profile.remove(device);
            m_program.remove(device);
            if (stillAuto && m_config->hasProfile(device, restore))
                switches.insert(device, restore);
        }
    }

    if (switches.isEmpty() && started.isEmpty() && exited.isEmpty())
        return;
    started.removeDuplicates();
    exited.removeDuplicates();
    QStringList why;
    if (!started.isEmpty())
        why << tr("%1 started").arg(started.join(QStringLiteral(", ")));
    if (!exited.isEmpty())
        why << tr("%1 exited").arg(exited.join(QStringLiteral(", ")));
    if (why.isEmpty())
        why << tr("switched automatically");
    m_config->save(nullptr);
    m_manager->switchProfiles(switches, why.join(QStringLiteral("; ")), m_config->notifications);
    emit changed();
}
