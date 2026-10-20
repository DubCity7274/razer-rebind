#include "autoswitcher.h"
#include "batterymonitor.h"
#include "idlelights.h"
#include "config.h"
#include "mainwindow.h"
#include "remapmanager.h"
#include "udevrule.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QLocalSocket>
#include <QLockFile>
#include <QMessageBox>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <QTextStream>

#include <csignal>
#include <memory>
#include <sys/socket.h>
#include <unistd.h>

namespace {

int signalFds[2] = {-1, -1};

void onSignal(int)
{
    const char c = 1;
    [[maybe_unused]] auto n = ::write(signalFds[1], &c, 1);
}

// Quit through the event loop on SIGINT/SIGTERM so every grab is released and
// no key is left held on the virtual devices.
void installSignalHandlers(QCoreApplication *app)
{
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, signalFds) != 0)
        return;
    auto *notifier = new QSocketNotifier(signalFds[0], QSocketNotifier::Read, app);
    QObject::connect(notifier, &QSocketNotifier::activated, app, &QCoreApplication::quit);
    struct sigaction sa = {};
    sa.sa_handler = onSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGHUP, &sa, nullptr);
}

// Send one command to the running instance. Returns false if none is running.
bool sendCommand(const QString &command, QString *reply)
{
    QLocalSocket socket;
    socket.connectToServer(RemapManager::serverName());
    if (!socket.waitForConnected(1000))
        return false;
    socket.write(command.toUtf8() + '\n');
    socket.flush();
    while (socket.state() == QLocalSocket::ConnectedState && socket.waitForReadyRead(3000)) {}
    *reply = QString::fromUtf8(socket.readAll()).trimmed();
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    // Only the GUI needs a display; everything else must work from a script,
    // a desktop shortcut, or a systemd service.
    bool gui = true;
    for (int i = 1; i < argc; ++i) {
        const QByteArray arg = argv[i];
        if (arg == "--headless" || arg == "--profile" || arg.startsWith("--profile=") || arg == "--next-profile" || arg == "--device" || arg.startsWith("--device=") || arg == "-d" || arg == "-p"
            || arg == "--previous-profile" || arg == "--toggle" || arg == "--list-profiles" || arg == "--print-udev-rule" || arg == "-h"
            || arg == "--help" || arg == "-v" || arg == "--version")
            gui = false;
    }

    std::unique_ptr<QCoreApplication> app(gui ? new QApplication(argc, argv) : new QCoreApplication(argc, argv));
    QCoreApplication::setApplicationName(QStringLiteral("razer-rebind"));
    QCoreApplication::setApplicationVersion(QStringLiteral(RAZER_REBIND_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Key and button rebinding for Razer devices using the OpenRazer drivers.\n\n"
                                                    "The profile options control the running instance, so they can be "
                                                    "bound to shortcuts in your desktop's keyboard settings."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("headless"), QStringLiteral("Apply saved bindings without a window (for autostart / systemd).")});
    parser.addOption({QStringLiteral("minimized"), QStringLiteral("Start hidden in the system tray.")});
    parser.addOption({{QStringLiteral("c"), QStringLiteral("config")}, QStringLiteral("Bindings file to use."),
                      QStringLiteral("file"), Config::defaultPath()});
    parser.addOption({{QStringLiteral("p"), QStringLiteral("profile")},
                      QStringLiteral("Switch the running instance to profile <name> (on every device that has it, "
                                     "or only on --device)."),
                      QStringLiteral("name")});
    parser.addOption({{QStringLiteral("d"), QStringLiteral("device")},
                      QStringLiteral("Limit --profile, --next-profile, --previous-profile and --list-profiles to one "
                                     "device. Any unique part of its name works, e.g. \"viper\"."),
                      QStringLiteral("device")});
    parser.addOption({QStringLiteral("next-profile"), QStringLiteral("Switch the running instance to the next profile.")});
    parser.addOption({QStringLiteral("previous-profile"), QStringLiteral("Switch the running instance to the previous profile.")});
    parser.addOption({QStringLiteral("toggle"), QStringLiteral("Turn remapping on or off in the running instance.")});
    parser.addOption({QStringLiteral("list-profiles"), QStringLiteral("List profiles; * marks the active one.")});
    parser.addOption({QStringLiteral("print-udev-rule"),
                      QStringLiteral("Print the device permission rule, for installing it by hand:\n"
                                     "razer-rebind --print-udev-rule | sudo tee /etc/udev/rules.d/70-razer-rebind.rules")});
    parser.process(*app);

    QTextStream out(stdout);
    QTextStream err(stderr);

    if (parser.isSet(QStringLiteral("print-udev-rule"))) {
        out << UdevRule;
        return 0;
    }

    // Remote control of the running instance.
    // Protocol: verb TAB device TAB argument (device empty = all devices).
    QString verb, argument;
    if (parser.isSet(QStringLiteral("profile"))) {
        verb = QStringLiteral("profile");
        argument = parser.value(QStringLiteral("profile"));
    } else if (parser.isSet(QStringLiteral("next-profile"))) {
        verb = QStringLiteral("next");
    } else if (parser.isSet(QStringLiteral("previous-profile"))) {
        verb = QStringLiteral("previous");
    } else if (parser.isSet(QStringLiteral("toggle"))) {
        verb = QStringLiteral("toggle");
    } else if (parser.isSet(QStringLiteral("list-profiles"))) {
        verb = QStringLiteral("list");
    }
    if (verb.isEmpty() && parser.isSet(QStringLiteral("device"))) {
        err << "--device needs one of --profile, --next-profile, --previous-profile or --list-profiles." << Qt::endl;
        return 2;
    }
    if (!verb.isEmpty()) {
        const QString command = verb + QLatin1Char('\t') + parser.value(QStringLiteral("device")) + QLatin1Char('\t') + argument;
        QString reply;
        if (!sendCommand(command, &reply)) {
            err << "Razer Rebind is not running." << Qt::endl;
            return 1;
        }
        out << reply << Qt::endl;
        const bool failed = reply.startsWith(QLatin1String("No ")) || reply.contains(QLatin1String(" has no profile"))
                            || reply.contains(QLatin1String("matches several")) || reply.startsWith(QLatin1String("unknown"));
        return failed ? 1 : 0;
    }

    // Two instances would fight over the exclusive device grabs. A second GUI
    // launch just brings the running one to the front.
    const QString runtimeDir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    QLockFile lock(QDir(runtimeDir.isEmpty() ? QDir::tempPath() : runtimeDir).filePath(QStringLiteral("razer-rebind.lock")));
    lock.setStaleLockTime(0);
    if (!lock.tryLock()) {
        QString reply;
        if (gui && sendCommand(QStringLiteral("show\t\t"), &reply))
            return 0;
        const QString msg = QStringLiteral("Razer Rebind is already running.");
        if (gui)
            QMessageBox::information(nullptr, QStringLiteral("Razer Rebind"), msg);
        else
            err << msg << Qt::endl;
        return 1;
    }

    installSignalHandlers(app.get());

    Config config;
    QString error;
    if (!config.load(parser.value(QStringLiteral("config")), &error))
        err << "Could not load bindings: " << error << Qt::endl;

    RemapManager manager(&config);
    QObject::connect(&manager, &RemapManager::message, [&err](const QString &msg) { err << msg << Qt::endl; });
    manager.listen();
    BatteryMonitor battery(&manager, &config);
    AutoSwitcher autoSwitcher(&manager, &config);
    IdleLights idleLights(&manager, &config);

    if (!gui) {
        QObject::connect(&manager, &RemapManager::devicesChanged, [&manager, &err] {
            for (const RazerDevice &d : manager.devices())
                err << "found " << d.name << " (" << d.eventNodes.join(QStringLiteral(", ")) << ")" << Qt::endl;
        });
        manager.rescan();
        err << "profiles: " << manager.profileSummary() << Qt::endl;
        return app->exec();
    }

    static_cast<QApplication *>(app.get())->setQuitOnLastWindowClosed(false);
    manager.rescan();
    MainWindow window(&config, &manager, &battery, &autoSwitcher);
    if (!(parser.isSet(QStringLiteral("minimized")) && window.hasTray()))
        window.show();
    return app->exec();
}
