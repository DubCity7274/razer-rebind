#include "mainwindow.h"
#include "profile.h"
#include "remapmanager.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
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

} // namespace

int main(int argc, char *argv[])
{
    bool headless = false;
    for (int i = 1; i < argc; ++i)
        if (qstrcmp(argv[i], "--headless") == 0)
            headless = true;

    std::unique_ptr<QCoreApplication> app(headless ? new QCoreApplication(argc, argv) : new QApplication(argc, argv));
    QCoreApplication::setApplicationName(QStringLiteral("razer-rebind"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Key and button rebinding for Razer devices using the OpenRazer drivers."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("headless"), QStringLiteral("Apply saved bindings without a window (for autostart / systemd).")});
    parser.addOption({QStringLiteral("minimized"), QStringLiteral("Start hidden in the system tray.")});
    parser.addOption({{QStringLiteral("c"), QStringLiteral("config")}, QStringLiteral("Bindings file to use."),
                      QStringLiteral("file"), Profile::defaultPath()});
    parser.process(*app);

    QTextStream err(stderr);

    // Two instances would fight over the exclusive device grabs.
    const QString runtimeDir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    QLockFile lock(QDir(runtimeDir.isEmpty() ? QDir::tempPath() : runtimeDir).filePath(QStringLiteral("razer-rebind.lock")));
    lock.setStaleLockTime(0);
    if (!lock.tryLock()) {
        const QString msg = QStringLiteral("Razer Rebind is already running (look for it in the system tray).");
        if (headless)
            err << msg << Qt::endl;
        else
            QMessageBox::information(nullptr, QStringLiteral("Razer Rebind"), msg);
        return 1;
    }

    installSignalHandlers(app.get());

    Profile profile;
    QString error;
    if (!profile.load(parser.value(QStringLiteral("config")), &error))
        err << "Could not load bindings: " << error << Qt::endl;

    RemapManager manager(&profile);
    QObject::connect(&manager, &RemapManager::message, [&err](const QString &msg) { err << msg << Qt::endl; });

    if (headless) {
        QObject::connect(&manager, &RemapManager::devicesChanged, [&manager, &err] {
            for (const RazerDevice &d : manager.devices())
                err << "found " << d.name << " (" << d.eventNodes.join(QStringLiteral(", ")) << ")" << Qt::endl;
        });
        manager.rescan();
        return app->exec();
    }

    static_cast<QApplication *>(app.get())->setQuitOnLastWindowClosed(false);
    manager.rescan();
    MainWindow window(&profile, &manager);
    if (!(parser.isSet(QStringLiteral("minimized")) && window.hasTray()))
        window.show();
    return app->exec();
}
