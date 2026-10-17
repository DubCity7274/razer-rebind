#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// The current user's running programs, read from /proc.
namespace Processes {

struct Process
{
    int pid = 0;
    quint64 startTime = 0; // clock ticks since boot; later = started later
    QString name;          // what to show and to link a profile to: "cs2", "eldenring.exe"
    QStringList aliases;   // every name it matches by: argv[0], executable and kernel names
};

QList<Process> list();

// Whether a program name from a profile ("EldenRing", "eldenring.exe")
// refers to this process. Case-insensitive; ".exe" is optional.
bool matches(const Process &process, const QString &program);

// "C:\\Games\\Elden Ring\\eldenring.exe" or "/usr/bin/cs2" -> the file name.
QString baseName(const QString &path);

} // namespace Processes
