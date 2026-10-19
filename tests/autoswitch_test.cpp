// Exercises automatic profile switching against real processes: stand-in
// "games" are `sleep` started under another name, the way Proton games show up
// as their Windows .exe path.

#include "autoswitcher.h"
#include "config.h"
#include "processes.h"
#include "remapmanager.h"

#include <QCoreApplication>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>

#include <cstdio>
#include <memory>

namespace {

int g_failures = 0;

#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) {                                                                \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            ++g_failures;                                                             \
        }                                                                             \
    } while (0)

// Starts `sleep` with argv[0] set to `name`.
std::unique_ptr<QProcess> startGame(const QString &name)
{
    auto p = std::make_unique<QProcess>();
    p->start(QStringLiteral("bash"), {QStringLiteral("-c"), QStringLiteral("exec -a \"$0\" sleep 60"), name});
    p->waitForStarted();
    QThread::msleep(100); // let exec happen
    return p;
}

void stop(std::unique_ptr<QProcess> &p)
{
    p->kill();
    p->waitForFinished();
    p.reset();
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    const QString kbd = QStringLiteral("Razer Test Keyboard"), mouse = QStringLiteral("Razer Test Mouse");

    // Name matching.
    {
        Processes::Process p;
        p.aliases = {QStringLiteral("EldenRing.exe"), QStringLiteral("wine64-preloader"), QStringLiteral("EldenRing.exe")};
        CHECK(Processes::matches(p, QStringLiteral("eldenring.exe")));
        CHECK(Processes::matches(p, QStringLiteral("EldenRing")));
        CHECK(Processes::matches(p, QStringLiteral("C:\\Games\\eldenring.exe")));
        CHECK(!Processes::matches(p, QStringLiteral("elden")));
        Processes::Process longName;
        longName.aliases = {QStringLiteral("averylongprogra")}; // comm, cut to 15
        CHECK(Processes::matches(longName, QStringLiteral("averylongprogramname")));
        CHECK(Processes::baseName(QStringLiteral("Z:\\home\\me\\game.exe")) == QStringLiteral("game.exe"));
    }

    Config config;
    config.load(dir.filePath(QStringLiteral("bindings.json")), nullptr);
    config.notifications = false;
    config.remappingEnabled = false;
    config.ensureDevice(kbd);
    config.ensureDevice(mouse);
    config.addProfile(kbd, QStringLiteral("Gaming"));
    config.addProfile(kbd, QStringLiteral("Other"));
    config.addProfile(mouse, QStringLiteral("Aim"));
    config.setPrograms(kbd, QStringLiteral("Gaming"), {QStringLiteral("rrtestgame.exe")});
    config.setPrograms(kbd, QStringLiteral("Other"), {QStringLiteral("rrothergame")});
    config.setPrograms(mouse, QStringLiteral("Aim"), {QStringLiteral("RRTestGame")});
    config.save(nullptr);

    RemapManager manager(&config);
    auto switcher = std::make_unique<AutoSwitcher>(&manager, &config);

    switcher->update();
    CHECK(config.activeProfile(kbd) == QStringLiteral("Default"));

    // A game starting switches every device that has a profile for it.
    auto game = startGame(QStringLiteral("C:\\Games\\RRTestGame.exe"));
    switcher->update();
    CHECK(config.activeProfile(kbd) == QStringLiteral("Gaming"));
    CHECK(config.activeProfile(mouse) == QStringLiteral("Aim"));
    CHECK(config.autoRestore(kbd) == QStringLiteral("Default"));
    CHECK(switcher->activeProgram(kbd) == QStringLiteral("RRTestGame.exe"));

    // Switching by hand while it runs sticks.
    manager.switchProfile(kbd, QStringLiteral("Default"));
    switcher->update();
    CHECK(config.activeProfile(kbd) == QStringLiteral("Default"));

    // A newer game wins; when it exits, the older one's profile comes back.
    auto other = startGame(QStringLiteral("rrothergame"));
    switcher->update();
    CHECK(config.activeProfile(kbd) == QStringLiteral("Other"));
    CHECK(config.activeProfile(mouse) == QStringLiteral("Aim"));
    stop(other);
    switcher->update();
    CHECK(config.activeProfile(kbd) == QStringLiteral("Gaming"));

    // The last game exiting restores what was active before it started.
    stop(game);
    switcher->update();
    CHECK(config.activeProfile(kbd) == QStringLiteral("Default"));
    CHECK(config.activeProfile(mouse) == QStringLiteral("Default"));
    CHECK(config.autoRestore(kbd).isEmpty());

    // A restart in the middle of a game still switches back afterwards.
    game = startGame(QStringLiteral("rrtestgame.exe"));
    switcher->update();
    CHECK(config.activeProfile(kbd) == QStringLiteral("Gaming"));
    switcher = std::make_unique<AutoSwitcher>(&manager, &config);
    Config reloaded;
    reloaded.load(config.path(), nullptr);
    CHECK(reloaded.autoRestore(kbd) == QStringLiteral("Default"));
    CHECK(reloaded.programs(kbd, QStringLiteral("Gaming")) == QStringList{QStringLiteral("rrtestgame.exe")});
    switcher->update();
    CHECK(config.activeProfile(kbd) == QStringLiteral("Gaming"));
    stop(game);
    switcher->update();
    CHECK(config.activeProfile(kbd) == QStringLiteral("Default"));

    // A profile picked by hand during the game stays after it exits, even
    // when the game was linked while its profile was already active.
    manager.switchProfile(kbd, QStringLiteral("Gaming"));
    game = startGame(QStringLiteral("rrtestgame.exe"));
    switcher->update();
    CHECK(config.autoRestore(kbd) == QStringLiteral("Gaming"));
    manager.switchProfile(kbd, QStringLiteral("Default"));
    switcher->update();
    stop(game);
    switcher->update();
    CHECK(config.activeProfile(kbd) == QStringLiteral("Default"));
    CHECK(config.autoRestore(kbd).isEmpty());
    manager.switchProfile(mouse, QStringLiteral("Default"));

    // Turning the setting off goes back right away.
    game = startGame(QStringLiteral("rrtestgame.exe"));
    switcher->update();
    CHECK(config.activeProfile(kbd) == QStringLiteral("Gaming"));
    config.autoSwitch = false;
    switcher->update();
    CHECK(config.activeProfile(kbd) == QStringLiteral("Default"));
    stop(game);

    // A binding's command runs through the shell, in the background.
    {
        const QString marker = dir.filePath(QStringLiteral("ran it"));
        manager.runCommand(QStringLiteral("sleep 0.2; echo hi > '%1'").arg(marker));
        CHECK(!QFile::exists(marker)); // didn't wait for it
        for (int i = 0; i < 50 && !QFile::exists(marker); ++i)
            QThread::msleep(20);
        CHECK(QFile::exists(marker));
    }

    if (g_failures)
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    else
        std::printf("all auto-switch checks passed\n");
    return g_failures ? 1 : 0;
}
