#include "processes.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <sys/stat.h>
#include <unistd.h>

namespace Processes {

namespace {

QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// Lower case, without a trailing ".exe", for comparing names.
QString key(const QString &name)
{
    QString k = name.trimmed().toLower();
    if (k.endsWith(QLatin1String(".exe")))
        k.chop(4);
    return k;
}

} // namespace

QString baseName(const QString &path)
{
    const qsizetype slash = std::max(path.lastIndexOf(QLatin1Char('/')), path.lastIndexOf(QLatin1Char('\\')));
    return path.mid(slash + 1).trimmed();
}

QList<Process> list()
{
    QList<Process> out;
    const uid_t uid = ::getuid();
    const QStringList pids = QDir(QStringLiteral("/proc")).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : pids) {
        bool ok = false;
        const int pid = entry.toInt(&ok);
        if (!ok)
            continue;
        const QString dir = QStringLiteral("/proc/") + entry;
        struct stat st;
        if (::stat(dir.toLocal8Bit().constData(), &st) != 0 || st.st_uid != uid)
            continue;

        // Kernel threads have no command line.
        const QByteArray cmdline = readFile(dir + QStringLiteral("/cmdline"));
        if (cmdline.isEmpty())
            continue;

        Process p;
        p.pid = pid;
        // Field 22 of stat, counted after the parenthesised name (which may
        // contain spaces), is the start time.
        const QByteArray stat = readFile(dir + QStringLiteral("/stat"));
        const qsizetype close = stat.lastIndexOf(')');
        if (close >= 0) {
            const QList<QByteArray> fields = stat.mid(close + 2).split(' ');
            if (fields.size() > 19)
                p.startTime = fields[19].toULongLong();
        }

        // Wine and Proton games show their Windows path in argv[0], while the
        // executable is the Wine loader, so argv[0] names the program best.
        // Browsers and Electron apps overwrite their command line with one
        // string ("browser 12 tab", "app.asar --type=..."); for those the
        // executable's name is the useful one.
        QString arg0 = QString::fromLocal8Bit(cmdline.left(cmdline.indexOf('\0')));
        const bool rewritten = cmdline.count('\0') <= 1 && arg0.contains(QLatin1Char(' '))
                               && !arg0.contains(QLatin1Char('\\'));
        if (rewritten)
            arg0 = arg0.section(QLatin1Char(' '), 0, 0);
        const QString argv0 = baseName(arg0);
        const QString exe = baseName(QFileInfo(dir + QStringLiteral("/exe")).symLinkTarget());
        const QString comm = QString::fromLocal8Bit(readFile(dir + QStringLiteral("/comm"))).trimmed();
        if (rewritten)
            p.name = !exe.isEmpty() ? exe : !comm.isEmpty() ? comm : argv0;
        else
            p.name = !argv0.isEmpty() ? argv0 : !exe.isEmpty() ? exe : comm;
        for (const QString &n : {argv0, exe, comm})
            if (!n.isEmpty() && !p.aliases.contains(n))
                p.aliases << n;
        out << p;
    }
    return out;
}

bool matches(const Process &process, const QString &program)
{
    const QString want = key(baseName(program));
    if (want.isEmpty())
        return false;
    for (const QString &alias : process.aliases) {
        const QString have = key(alias);
        // The kernel's name (comm) is cut to 15 characters.
        if (have == want || (alias.size() == 15 && want.startsWith(have)))
            return true;
    }
    return false;
}

} // namespace Processes
