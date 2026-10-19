// Exercises Remapper's event handling (bindings, Hypershift, turbo) without
// real hardware: each "device" is a FIFO the test writes input events into,
// the evdev ioctls are stubbed, and uinput output is recorded instead of
// reaching the kernel.

#include "remapper.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include <libevdev/libevdev-uinput.h>

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <linux/input.h>
#include <mutex>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

// --- stubs -------------------------------------------------------------------

namespace {

struct Output
{
    const void *device;
    int code;
    int value;
    std::chrono::steady_clock::time_point time;
};

std::mutex g_outputMutex;
std::vector<Output> g_output;
int g_quietPipe[2] = {-1, -1}; // stands in for the uinput fd; never readable

} // namespace

// Defined here, these take precedence over libc's and libevdev's for calls
// from the remapper compiled into this test.
extern "C" int ioctl(int, unsigned long request, ...) noexcept
{
    va_list ap;
    va_start(ap, request);
    void *arg = va_arg(ap, void *);
    va_end(ap);
    if (_IOC_DIR(request) & _IOC_READ) {
        // EVIOCGBIT, EVIOCGKEY, EVIOCGNAME: nothing set, no keys held.
        std::memset(arg, 0, _IOC_SIZE(request));
        return 0;
    }
    return 0; // EVIOCGRAB
}

extern "C" int libevdev_uinput_create_from_device(const libevdev *, int, libevdev_uinput **out)
{
    *out = reinterpret_cast<libevdev_uinput *>(new char);
    return 0;
}

extern "C" void libevdev_uinput_destroy(libevdev_uinput *dev)
{
    delete reinterpret_cast<char *>(dev);
}

extern "C" int libevdev_uinput_get_fd(const libevdev_uinput *)
{
    return g_quietPipe[0];
}

extern "C" int libevdev_uinput_write_event(const libevdev_uinput *dev, unsigned int type, unsigned int code, int value)
{
    if (type == EV_KEY) {
        std::lock_guard lock(g_outputMutex);
        g_output.push_back({dev, int(code), value, std::chrono::steady_clock::now()});
    }
    return 0;
}

// --- harness -----------------------------------------------------------------

namespace {

int g_failures = 0;

#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) {                                                                \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            ++g_failures;                                                             \
        }                                                                             \
    } while (0)

void settle(int ms = 60)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

std::vector<Output> takeOutput()
{
    std::lock_guard lock(g_outputMutex);
    return std::exchange(g_output, {});
}

// Key events only, as "code=value" pairs, for compact comparisons.
std::vector<std::pair<int, int>> keys(const std::vector<Output> &out)
{
    std::vector<std::pair<int, int>> k;
    for (const Output &o : out)
        k.emplace_back(o.code, o.value);
    if (qEnvironmentVariableIsSet("DEBUG_KEYS")) {
        for (auto &[c, v] : k)
            std::fprintf(stderr, "%d=%d ", c, v);
        std::fprintf(stderr, "\n");
    }
    return k;
}

int count(const std::vector<Output> &out, int code, int value)
{
    int n = 0;
    for (const Output &o : out)
        n += o.code == code && o.value == value;
    return n;
}

class FakeDevice
{
public:
    FakeDevice(const QString &dir, const QString &name, RazerDevice::Kind kind)
    {
        m_path = dir + QLatin1Char('/') + name;
        ::mkfifo(m_path.toLocal8Bit().constData(), 0600);
        RazerDevice d;
        d.kind = kind;
        d.name = d.profileKey = name;
        d.hidPath = m_path;
        d.eventNodes = {m_path};
        remapper = new Remapper(d);
    }
    ~FakeDevice()
    {
        delete remapper;
        if (m_writer >= 0)
            ::close(m_writer);
    }

    bool start()
    {
        QString error;
        if (!remapper->start(&error)) {
            std::fprintf(stderr, "start: %s\n", qPrintable(error));
            return false;
        }
        m_writer = ::open(m_path.toLocal8Bit().constData(), O_WRONLY | O_NONBLOCK);
        remapper->setActive(true);
        for (int i = 0; i < 100 && !remapper->isGrabbed(); ++i)
            settle(10);
        return remapper->isGrabbed();
    }

    void key(int code, int value)
    {
        input_event ev[2] = {};
        ev[0].type = EV_KEY;
        ev[0].code = code;
        ev[0].value = value;
        ev[1].type = EV_SYN;
        ev[1].code = SYN_REPORT;
        [[maybe_unused]] auto n = ::write(m_writer, ev, sizeof ev);
        settle();
    }

    Remapper *remapper;

private:
    QString m_path;
    int m_writer = -1;
};

using Keys2 = std::vector<std::pair<int, int>>;

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    if (::pipe(g_quietPipe) < 0)
        return 1;

    FakeDevice kbd(dir.path(), QStringLiteral("kbd"), RazerDevice::Keyboard);
    FakeDevice mouse(dir.path(), QStringLiteral("mouse"), RazerDevice::Mouse);

    DeviceProfile kp;
    kp.bindings.insert(KEY_A, makeBinding({KEY_B}, 0, {}, 0));
    kp.bindings.insert(KEY_D, makeBinding({KEY_E}, 0, {}, 0));
    kp.bindings.insert(KEY_T, makeBinding({KEY_T}, 20, {}, 0));
    kp.bindings.insert(KEY_CAPSLOCK, makeBinding({Keys::Hypershift}, 0, {}, 0));
    kp.hypershift.insert(KEY_A, makeBinding({KEY_C}, 0, {}, 0));
    kp.hypershift.insert(KEY_F, makeBinding({KEY_LEFTCTRL, KEY_V}, 0, {}, 0));
    kp.hypershift.insert(KEY_G, makeBinding({KEY_G}, 25, {}, 0));
    kbd.remapper->setProfile(kp);

    DeviceProfile mp;
    mp.bindings.insert(BTN_SIDE, makeBinding({Keys::Hypershift}, 0, {}, 0));
    mp.hypershift.insert(BTN_LEFT, makeBinding({BTN_RIGHT}, 0, {}, 0));
    mouse.remapper->setProfile(mp);

    if (!kbd.start() || !mouse.start()) {
        std::fprintf(stderr, "FAIL: remappers did not grab\n");
        return 1;
    }
    takeOutput();

    // Plain binding and an unbound key.
    kbd.key(KEY_A, 1);
    kbd.key(KEY_A, 0);
    kbd.key(KEY_Z, 1);
    kbd.key(KEY_Z, 0);
    CHECK((keys(takeOutput()) == Keys2{{KEY_B, 1}, {KEY_B, 0}, {KEY_Z, 1}, {KEY_Z, 0}}));

    // Hypershift on the mouse switches the keyboard's layer; the Hypershift
    // key itself sends nothing. Keys without a layer binding keep theirs.
    mouse.key(BTN_SIDE, 1);
    kbd.key(KEY_A, 1);
    kbd.key(KEY_A, 0);
    kbd.key(KEY_D, 1);
    kbd.key(KEY_D, 0);
    kbd.key(KEY_F, 1);
    kbd.key(KEY_F, 0);
    mouse.key(BTN_LEFT, 1);
    mouse.key(BTN_LEFT, 0);
    mouse.key(BTN_SIDE, 0);
    CHECK((keys(takeOutput()) == Keys2{{KEY_C, 1}, {KEY_C, 0}, {KEY_E, 1}, {KEY_E, 0},
                                       {KEY_LEFTCTRL, 1}, {KEY_V, 1}, {KEY_V, 0}, {KEY_LEFTCTRL, 0},
                                       {BTN_RIGHT, 1}, {BTN_RIGHT, 0}}));

    // Back to normal after release.
    kbd.key(KEY_A, 1);
    kbd.key(KEY_A, 0);
    mouse.key(BTN_LEFT, 1);
    mouse.key(BTN_LEFT, 0);
    CHECK((keys(takeOutput()) == Keys2{{KEY_B, 1}, {KEY_B, 0}, {BTN_LEFT, 1}, {BTN_LEFT, 0}}));

    // A key pressed in the layer releases what it pressed, even when
    // Hypershift is let go first.
    kbd.key(KEY_CAPSLOCK, 1);
    kbd.key(KEY_A, 1);
    kbd.key(KEY_CAPSLOCK, 0);
    kbd.key(KEY_A, 0);
    CHECK((keys(takeOutput()) == Keys2{{KEY_C, 1}, {KEY_C, 0}}));

    // Key repeat of a Hypershift key emits nothing.
    kbd.key(KEY_CAPSLOCK, 1);
    kbd.key(KEY_CAPSLOCK, 2);
    kbd.key(KEY_CAPSLOCK, 0);
    CHECK(takeOutput().empty());

    // Turbo: 20/s gives one press per 50 ms held, each released, and nothing
    // more after the key is let go. Repeat events don't add any. Expected
    // counts come from the measured hold time, so a slow machine can't fail.
    auto expected = [](std::chrono::steady_clock::time_point since, int rate) {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - since).count() * rate;
    };
    auto held = std::chrono::steady_clock::now();
    kbd.key(KEY_T, 1);
    settle(200);
    kbd.key(KEY_T, 2);
    settle(170);
    double want = expected(held, 20);
    kbd.key(KEY_T, 0);
    settle(150);
    {
        const auto out = takeOutput();
        const int presses = count(out, KEY_T, 1);
        CHECK(presses >= want - 2 && presses <= want + 2);
        CHECK(count(out, KEY_T, 0) == presses);
        CHECK(count(out, KEY_T, 2) == 0);
        CHECK(!out.empty() && out.back().code == KEY_T && out.back().value == 0);
        std::printf("turbo 20/s: %d presses, %.1f expected\n", presses, want);
    }

    // Turbo inside the Hypershift layer.
    kbd.key(KEY_CAPSLOCK, 1);
    held = std::chrono::steady_clock::now();
    kbd.key(KEY_G, 1);
    settle(340);
    want = expected(held, 25);
    kbd.key(KEY_G, 0);
    kbd.key(KEY_CAPSLOCK, 0);
    {
        const auto out = takeOutput();
        const int presses = count(out, KEY_G, 1);
        CHECK(presses >= want - 2 && presses <= want + 2);
        CHECK(count(out, KEY_G, 0) == presses);
    }

    // Tap vs hold: Tab taps Tab, holds Ctrl; Space taps space, holds
    // Hypershift. Hold time 300 ms, so the quick sequences below (60 ms per
    // event) stay well inside it.
    {
        DeviceProfile th = kp;
        th.bindings.insert(KEY_TAB, makeBinding({KEY_TAB}, 0, {KEY_LEFTCTRL}, 300));
        th.bindings.insert(KEY_SPACE, makeBinding({KEY_SPACE}, 0, {Keys::Hypershift}, 300));
        kbd.remapper->setProfile(th);
        takeOutput();

        kbd.key(KEY_TAB, 1);
        kbd.key(KEY_TAB, 0);
        CHECK((keys(takeOutput()) == Keys2{{KEY_TAB, 1}, {KEY_TAB, 0}}));

        // Held past the hold time; key repeat then repeats the hold output,
        // like a real Ctrl key held down.
        kbd.key(KEY_TAB, 1);
        settle(300);
        kbd.key(KEY_TAB, 2);
        kbd.key(KEY_TAB, 0);
        CHECK((keys(takeOutput()) == Keys2{{KEY_LEFTCTRL, 1}, {KEY_LEFTCTRL, 2}, {KEY_LEFTCTRL, 0}}));

        // Key repeat while still undecided sends nothing.
        kbd.key(KEY_TAB, 1);
        kbd.key(KEY_TAB, 2);
        kbd.key(KEY_TAB, 0);
        CHECK((keys(takeOutput()) == Keys2{{KEY_TAB, 1}, {KEY_TAB, 0}}));

        // Another key pressed and released meanwhile makes it a hold at once,
        // and that key comes out after Ctrl.
        kbd.key(KEY_TAB, 1);
        kbd.key(KEY_Z, 1);
        kbd.key(KEY_Z, 0);
        CHECK((keys(takeOutput()) == Keys2{{KEY_LEFTCTRL, 1}, {KEY_Z, 1}, {KEY_Z, 0}}));
        kbd.key(KEY_TAB, 0);
        CHECK((keys(takeOutput()) == Keys2{{KEY_LEFTCTRL, 0}}));

        // Typing fast (Tab released before the next key) is a tap, and the
        // next key still comes out, after the tap.
        kbd.key(KEY_TAB, 1);
        kbd.key(KEY_Z, 1);
        kbd.key(KEY_TAB, 0);
        kbd.key(KEY_Z, 0);
        CHECK((keys(takeOutput()) == Keys2{{KEY_TAB, 1}, {KEY_TAB, 0}, {KEY_Z, 1}, {KEY_Z, 0}}));

        // A key pressed meanwhile but released only after the hold time.
        kbd.key(KEY_TAB, 1);
        kbd.key(KEY_Z, 1);
        settle(300);
        CHECK((keys(takeOutput()) == Keys2{{KEY_LEFTCTRL, 1}, {KEY_Z, 1}}));
        kbd.key(KEY_Z, 0);
        kbd.key(KEY_TAB, 0);
        CHECK((keys(takeOutput()) == Keys2{{KEY_Z, 0}, {KEY_LEFTCTRL, 0}}));

        // Hold for Hypershift: queued keys use the Hypershift layer.
        kbd.key(KEY_SPACE, 1);
        kbd.key(KEY_A, 1);
        kbd.key(KEY_A, 0);
        kbd.key(KEY_SPACE, 0);
        CHECK((keys(takeOutput()) == Keys2{{KEY_C, 1}, {KEY_C, 0}}));
        kbd.key(KEY_SPACE, 1);
        kbd.key(KEY_SPACE, 0);
        kbd.key(KEY_A, 1);
        kbd.key(KEY_A, 0);
        CHECK((keys(takeOutput()) == Keys2{{KEY_SPACE, 1}, {KEY_SPACE, 0}, {KEY_B, 1}, {KEY_B, 0}}));

        kbd.remapper->setProfile(kp);
    }

    // Macros.
    {
        auto macro = [](QList<MacroStep> steps, Binding::MacroMode mode) {
            Binding b;
            b.macro = steps;
            b.macroMode = mode;
            return b;
        };
        using S = MacroStep;
        DeviceProfile mp2 = kp;
        mp2.bindings.insert(KEY_M, macro({{S::Press, KEY_A}, {S::Delay, 100}, {S::Release, KEY_A},
                                          {S::Press, KEY_B}, {S::Release, KEY_B}}, Binding::PlayOnce));
        mp2.bindings.insert(KEY_N, macro({{S::Press, KEY_C}, {S::Delay, 30}, {S::Release, KEY_C}, {S::Delay, 30}},
                                         Binding::RepeatWhileHeld));
        mp2.bindings.insert(KEY_O, macro({{S::Press, KEY_D}, {S::Release, KEY_D}}, Binding::Toggle));
        mp2.bindings.insert(KEY_P, macro({{S::Release, KEY_X}, {S::Press, KEY_Y}, {S::Release, KEY_Y}}, Binding::PlayOnce));
        kbd.remapper->setProfile(mp2);
        takeOutput();

        // Plays to the end even when let go at once, with its pause.
        kbd.key(KEY_M, 1);
        kbd.key(KEY_M, 0);
        settle(120);
        {
            const auto out = takeOutput();
            CHECK((keys(out) == Keys2{{KEY_A, 1}, {KEY_A, 0}, {KEY_B, 1}, {KEY_B, 0}}));
            if (out.size() == 4) {
                const auto pause = std::chrono::duration_cast<std::chrono::milliseconds>(out[1].time - out[0].time).count();
                CHECK(pause >= 95 && pause < 160);
            }
        }

        // Repeats while held, stops when let go, nothing left down.
        auto held = std::chrono::steady_clock::now();
        kbd.key(KEY_N, 1);
        settle(300);
        double rounds = std::chrono::duration<double>(std::chrono::steady_clock::now() - held).count() / 0.060;
        kbd.key(KEY_N, 0);
        settle(100);
        {
            const auto out = takeOutput();
            const int presses = count(out, KEY_C, 1);
            CHECK(presses >= rounds - 2 && presses <= rounds + 2);
            CHECK(count(out, KEY_C, 0) == presses);
        }

        // Toggle: one press starts it, the next stops it. Without pauses it
        // still waits between rounds instead of spinning.
        kbd.key(KEY_O, 1);
        kbd.key(KEY_O, 0);
        settle(100);
        kbd.key(KEY_O, 1);
        kbd.key(KEY_O, 0);
        {
            const auto out = takeOutput();
            const int presses = count(out, KEY_D, 1);
            CHECK(presses >= 8 && presses <= 25);
            CHECK(count(out, KEY_D, 0) == presses);
        }
        settle(100);
        CHECK(takeOutput().empty());

        // A release of a key the macro didn't press is skipped.
        kbd.key(KEY_P, 1);
        kbd.key(KEY_P, 0);
        CHECK((keys(takeOutput()) == Keys2{{KEY_Y, 1}, {KEY_Y, 0}}));

        // A macro holding a key when it's stopped lets go of it.
        mp2.bindings.insert(KEY_N, macro({{S::Press, KEY_LEFTSHIFT}, {S::Delay, 1000}, {S::Release, KEY_LEFTSHIFT}},
                                         Binding::RepeatWhileHeld));
        kbd.remapper->setProfile(mp2);
        kbd.key(KEY_N, 1);
        kbd.key(KEY_N, 0);
        CHECK((keys(takeOutput()) == Keys2{{KEY_LEFTSHIFT, 1}, {KEY_LEFTSHIFT, 0}}));

        // Saved and loaded unchanged.
        const QString path = dir.filePath(QStringLiteral("macro.json"));
        Config c;
        c.load(path, nullptr);
        c.setDevice(QStringLiteral("kbd"), mp2);
        CHECK(c.save(nullptr));
        Config loaded;
        CHECK(loaded.load(path, nullptr));
        CHECK(loaded.device(QStringLiteral("kbd")).bindings == mp2.bindings);

        kbd.remapper->setProfile(kp);
    }

    // Combos: Ctrl + Shift + S sends F5 without the modifiers, which come
    // back while still held; S alone and Ctrl + S stay as they were.
    {
        DeviceProfile cp = kp;
        cp.bindings.insert(Keys::trigger(Keys::ModCtrl | Keys::ModShift, KEY_S), makeBinding({KEY_F5}));
        cp.bindings.insert(KEY_CAPSLOCK, makeBinding({KEY_LEFTCTRL}));
        cp.bindings.insert(Keys::trigger(Keys::ModCtrl, KEY_J), makeBinding({KEY_K}));
        Binding mac;
        mac.macro = {{MacroStep::Press, KEY_G}, {MacroStep::Delay, 250}, {MacroStep::Release, KEY_G}};
        cp.bindings.insert(Keys::trigger(Keys::ModAlt, KEY_M), mac);
        kbd.remapper->setProfile(cp);
        takeOutput();

        kbd.key(KEY_LEFTCTRL, 1);
        kbd.key(KEY_LEFTSHIFT, 1);
        kbd.key(KEY_S, 1);
        kbd.key(KEY_S, 0);
        kbd.key(KEY_LEFTSHIFT, 0);
        kbd.key(KEY_LEFTCTRL, 0);
        CHECK((keys(takeOutput()) == Keys2{{KEY_LEFTCTRL, 1}, {KEY_LEFTSHIFT, 1},
                                           {KEY_LEFTCTRL, 0}, {KEY_LEFTSHIFT, 0}, {KEY_F5, 1},
                                           {KEY_F5, 0}, {KEY_LEFTCTRL, 1}, {KEY_LEFTSHIFT, 1},
                                           {KEY_LEFTSHIFT, 0}, {KEY_LEFTCTRL, 0}}));

        kbd.key(KEY_S, 1);
        kbd.key(KEY_S, 0);
        kbd.key(KEY_LEFTCTRL, 1);
        kbd.key(KEY_S, 1);
        kbd.key(KEY_S, 0);
        kbd.key(KEY_LEFTCTRL, 0);
        CHECK((keys(takeOutput()) == Keys2{{KEY_S, 1}, {KEY_S, 0}, {KEY_LEFTCTRL, 1}, {KEY_S, 1},
                                           {KEY_S, 0}, {KEY_LEFTCTRL, 0}}));

        // A remapped modifier counts: Caps Lock is Ctrl here.
        kbd.key(KEY_CAPSLOCK, 1);
        kbd.key(KEY_J, 1);
        kbd.key(KEY_J, 0);
        kbd.key(KEY_CAPSLOCK, 0);
        CHECK((keys(takeOutput()) == Keys2{{KEY_LEFTCTRL, 1}, {KEY_LEFTCTRL, 0}, {KEY_K, 1}, {KEY_K, 0},
                                           {KEY_LEFTCTRL, 1}, {KEY_LEFTCTRL, 0}}));

        // A macro on a combo: Alt stays up until the macro is done, even
        // when the key is let go first, and only comes back if still held.
        kbd.key(KEY_LEFTALT, 1);
        kbd.key(KEY_M, 1);
        kbd.key(KEY_M, 0);
        settle(250);
        kbd.key(KEY_LEFTALT, 0);
        CHECK((keys(takeOutput()) == Keys2{{KEY_LEFTALT, 1}, {KEY_LEFTALT, 0}, {KEY_G, 1}, {KEY_G, 0},
                                           {KEY_LEFTALT, 1}, {KEY_LEFTALT, 0}}));
        kbd.key(KEY_LEFTALT, 1);
        kbd.key(KEY_M, 1);
        kbd.key(KEY_M, 0);
        kbd.key(KEY_LEFTALT, 0);
        settle(250);
        CHECK((keys(takeOutput()) == Keys2{{KEY_LEFTALT, 1}, {KEY_LEFTALT, 0}, {KEY_G, 1}, {KEY_LEFTALT, 0},
                                           {KEY_G, 0}}));

        // Saved by name.
        Config c;
        c.load(dir.filePath(QStringLiteral("combo.json")), nullptr);
        c.setDevice(QStringLiteral("kbd"), cp);
        CHECK(c.save(nullptr));
        Config loaded;
        CHECK(loaded.load(c.path(), nullptr));
        CHECK(loaded.device(QStringLiteral("kbd")).bindings == cp.bindings);
        CHECK(Keys::triggerName(Keys::trigger(Keys::ModCtrl | Keys::ModShift, KEY_S)) == QStringLiteral("Ctrl+Shift+KEY_S"));
        CHECK(Keys::triggerLabel(Keys::trigger(Keys::ModCtrl | Keys::ModShift, KEY_S)) == QStringLiteral("Ctrl + Shift + S"));

        kbd.remapper->setProfile(kp);
    }

    // Commands: once per press, nothing typed, key repeat ignored; also on
    // a combo, which still lifts and restores its modifiers.
    {
        std::mutex m;
        QStringList ran;
        QObject::connect(kbd.remapper, &Remapper::commandTriggered, kbd.remapper,
                         [&](const QString &c) { std::lock_guard l(m); ran << c; }, Qt::DirectConnection);
        DeviceProfile cmd = kp;
        Binding run;
        run.command = QStringLiteral("steam");
        cmd.bindings.insert(KEY_F9, run);
        run.command = QStringLiteral("playerctl play-pause");
        cmd.hypershift.insert(KEY_F9, run);
        run.command = QStringLiteral("echo combo");
        cmd.bindings.insert(Keys::trigger(Keys::ModCtrl, KEY_F10), run);
        kbd.remapper->setProfile(cmd);
        takeOutput();

        kbd.key(KEY_F9, 1);
        kbd.key(KEY_F9, 2);
        kbd.key(KEY_F9, 2);
        kbd.key(KEY_F9, 0);
        CHECK(takeOutput().empty());
        kbd.key(KEY_CAPSLOCK, 1); // Hypershift
        kbd.key(KEY_F9, 1);
        kbd.key(KEY_F9, 0);
        kbd.key(KEY_CAPSLOCK, 0);
        kbd.key(KEY_LEFTCTRL, 1);
        kbd.key(KEY_F10, 1);
        kbd.key(KEY_F10, 0);
        kbd.key(KEY_LEFTCTRL, 0);
        CHECK((keys(takeOutput()) == Keys2{{KEY_LEFTCTRL, 1}, {KEY_LEFTCTRL, 0}, {KEY_LEFTCTRL, 1}, {KEY_LEFTCTRL, 0}}));
        {
            std::lock_guard l(m);
            CHECK((ran == QStringList{QStringLiteral("steam"), QStringLiteral("playerctl play-pause"), QStringLiteral("echo combo")}));
        }
        QObject::disconnect(kbd.remapper, &Remapper::commandTriggered, kbd.remapper, nullptr);

        // Saved, and found when importing a shared profile.
        Config c;
        c.load(dir.filePath(QStringLiteral("cmd.json")), nullptr);
        c.ensureDevice(QStringLiteral("kbd"));
        c.setDevice(QStringLiteral("kbd"), cmd);
        CHECK(c.save(nullptr));
        Config loaded;
        CHECK(loaded.load(c.path(), nullptr));
        CHECK(loaded.device(QStringLiteral("kbd")).bindings == cmd.bindings);
        CHECK(loaded.device(QStringLiteral("kbd")).hypershift == cmd.hypershift);
        const QStringList found = Config::profileFileCommands(c.exportProfile(QStringLiteral("kbd"), QStringLiteral("Default")));
        CHECK(found.size() == 3 && found.contains(QStringLiteral("playerctl play-pause")));
        CHECK(Config::profileFileCommands(Config().exportProfile(QStringLiteral("x"), QStringLiteral("y"))).isEmpty());

        kbd.remapper->setProfile(kp);
    }

    // DPI bindings: the clutch reports press and release, a stage step only
    // the press (also from a wheel notch); neither types anything.
    {
        std::mutex m;
        QList<std::tuple<int, int, bool>> got;
        QObject::connect(kbd.remapper, &Remapper::dpiTriggered, kbd.remapper,
                         [&](int a, int v, bool p) { std::lock_guard l(m); got << std::tuple{a, v, p}; },
                         Qt::DirectConnection);
        DeviceProfile dp = kp;
        Binding clutch;
        clutch.dpi = Binding::DpiClutch;
        clutch.clutchDpi = 450;
        dp.bindings.insert(KEY_F11, clutch);
        Binding up;
        up.dpi = Binding::DpiStageUp;
        dp.bindings.insert(KEY_F12, up);
        kbd.remapper->setProfile(dp);
        takeOutput();
        kbd.key(KEY_F11, 1);
        kbd.key(KEY_F11, 2);
        kbd.key(KEY_F11, 0);
        kbd.key(KEY_F12, 1);
        kbd.key(KEY_F12, 0);
        CHECK(takeOutput().empty());
        {
            std::lock_guard l(m);
            CHECK((got == QList<std::tuple<int, int, bool>>{{Binding::DpiClutch, 450, true}, {Binding::DpiClutch, 0, false},
                                                           {Binding::DpiStageUp, 400, true}}));
        }
        QObject::disconnect(kbd.remapper, &Remapper::dpiTriggered, kbd.remapper, nullptr);

        Config c;
        c.load(dir.filePath(QStringLiteral("dpi.json")), nullptr);
        c.ensureDevice(QStringLiteral("kbd"));
        c.setDevice(QStringLiteral("kbd"), dp);
        CHECK(c.save(nullptr));
        Config loaded;
        CHECK(loaded.load(c.path(), nullptr));
        CHECK(loaded.device(QStringLiteral("kbd")).bindings == dp.bindings);
        kbd.remapper->setProfile(kp);
    }

    // A device that goes away while its Hypershift key is held doesn't leave
    // the other devices stuck in the layer.
    mouse.key(BTN_SIDE, 1);
    mouse.remapper->stop();
    kbd.key(KEY_A, 1);
    kbd.key(KEY_A, 0);
    CHECK((keys(takeOutput()) == Keys2{{KEY_B, 1}, {KEY_B, 0}}));

    kbd.remapper->stop();

    // Turbo and the Hypershift layer survive saving and loading; plain
    // bindings keep the short list form older versions read.
    {
        const QString path = dir.filePath(QStringLiteral("bindings.json"));
        Config c;
        c.load(path, nullptr);
        c.setDevice(QStringLiteral("kbd"), kp);
        CHECK(c.save(nullptr));
        Config loaded;
        CHECK(loaded.load(path, nullptr));
        const DeviceProfile p = loaded.device(QStringLiteral("kbd"));
        CHECK(p.bindings == kp.bindings);
        CHECK(p.hypershift == kp.hypershift);
        CHECK(p.hasHypershiftKey());
        QFile f(path);
        CHECK(f.open(QIODevice::ReadOnly));
        const QByteArray json = f.readAll();
        CHECK(json.contains("\"KEY_A\": [\n"));
        CHECK(json.contains("\"turbo\": 20"));
        CHECK(json.contains("\"HYPERSHIFT\""));

        // Tap vs hold round trip.
        DeviceProfile th = kp;
        th.bindings.insert(KEY_CAPSLOCK, makeBinding({KEY_ESC}, 0, {KEY_LEFTCTRL}, 0));
        th.bindings.insert(KEY_SPACE, makeBinding({KEY_SPACE}, 0, {Keys::Hypershift}, 350));
        c.setDevice(QStringLiteral("kbd"), th);
        CHECK(c.save(nullptr));
        Config again;
        CHECK(again.load(path, nullptr));
        CHECK(again.device(QStringLiteral("kbd")).bindings == th.bindings);
        CHECK(again.device(QStringLiteral("kbd")).hasHypershiftKey());
    }

    // Sensor settings: saved per profile; the first one set is copied to the
    // device's other profiles, and new profiles start from the active one.
    {
        const QString path = dir.filePath(QStringLiteral("sensor.json"));
        const QString m = QStringLiteral("mouse");
        Config c;
        c.load(path, nullptr);
        c.ensureDevice(m);
        c.addProfile(m, QStringLiteral("Game"));
        SensorSettings before{{800}, 0, 1000};
        c.fillSensor(m, before);
        c.setActiveProfile(m, QStringLiteral("Game"));
        DeviceProfile p = c.device(m);
        CHECK(p.sensor == before);
        p.sensor = SensorSettings{{400, 1600, 3200}, 2, 500};
        c.setDevice(m, p);
        c.addProfile(m, QStringLiteral("Game 2"));
        CHECK(c.save(nullptr));
        Config loaded;
        CHECK(loaded.load(path, nullptr));
        CHECK(loaded.device(m).sensor == p.sensor);
        loaded.setActiveProfile(m, QStringLiteral("Default"));
        CHECK(loaded.device(m).sensor == before);
        loaded.setActiveProfile(m, QStringLiteral("Game 2"));
        CHECK(loaded.device(m).sensor == p.sensor);
    }

    // Lighting: saved per profile, the first one set copied to the other
    // profiles, new profiles starting from the active one's.
    {
        Config c;
        c.load(dir.filePath(QStringLiteral("light.json")), nullptr);
        const QString k = QStringLiteral("kbd");
        c.ensureDevice(k);
        c.addProfile(k, QStringLiteral("Game"));
        LightingSettings before;
        before.effect = LightingSettings::Spectrum;
        before.brightness = 75;
        c.fillLighting(k, before);
        c.setActiveProfile(k, QStringLiteral("Game"));
        DeviceProfile p = c.device(k);
        CHECK(p.lighting == before);
        p.lighting.effect = LightingSettings::Starlight;
        p.lighting.colours = LightingSettings::TwoColours;
        p.lighting.colour1 = QColor(255, 0, 0);
        p.lighting.colour2 = QColor(0, 0, 255);
        p.lighting.speed = 3;
        p.lighting.direction = 2;
        p.lighting.brightness = 40;
        c.setDevice(k, p);
        c.addProfile(k, QStringLiteral("Game 2"));
        CHECK(c.save(nullptr));
        Config loaded;
        CHECK(loaded.load(c.path(), nullptr));
        CHECK(loaded.device(k).lighting == p.lighting);
        loaded.setActiveProfile(k, QStringLiteral("Default"));
        CHECK(loaded.device(k).lighting == before);
        loaded.setActiveProfile(k, QStringLiteral("Game 2"));
        CHECK(loaded.device(k).lighting == p.lighting);
        CHECK(LightingSettings::effectFromId(QStringLiteral("starlight")) == LightingSettings::Starlight);

        // Per-key colours.
        DeviceProfile cp = loaded.device(k);
        cp.lighting = LightingSettings();
        cp.lighting.effect = LightingSettings::Custom;
        cp.lighting.keys.insert(KEY_W, QColor(255, 0, 0));
        cp.lighting.keys.insert(KEY_PLAYPAUSE, QColor(0, 255, 0));
        cp.lighting.keys.insert(KEY_FN, QColor(0, 0, 255));
        cp.lighting.keys.insert(Keys::edgeLed(8, 19), QColor(255, 128, 0));
        cp.lighting.unlit = QColor(10, 10, 60);
        cp.lighting.edge = QColor(160, 60, 255);
        loaded.setDevice(k, cp);
        CHECK(loaded.save(nullptr));
        Config again;
        CHECK(again.load(loaded.path(), nullptr));
        CHECK(again.device(k).lighting == cp.lighting);
        CHECK(LightingSettings::effectFromId(QStringLiteral("custom")) == LightingSettings::Custom);
        CHECK(Keys::fromName(Keys::name(Keys::edgeLed(6, 14))) == Keys::edgeLed(6, 14));
        CHECK(Keys::fromName(QStringLiteral("EDGE_99_1")) == -1);
        CHECK(LightingSettings::effectFromId(QStringLiteral("nonsense")) == LightingSettings::None);
    }

    // Export and import: everything comes back, under a free name, without
    // programs another profile already has.
    {
        Config c;
        c.load(dir.filePath(QStringLiteral("share.json")), nullptr);
        const QString k = QStringLiteral("kbd");
        c.ensureDevice(k);
        DeviceProfile th = kp;
        th.bindings.insert(KEY_CAPSLOCK, makeBinding({KEY_ESC}, 0, {KEY_LEFTCTRL}, 350));
        c.setDevice(k, th);
        c.setPrograms(k, QStringLiteral("Default"), {QStringLiteral("game.exe"), QStringLiteral("other")});
        const QByteArray file = c.exportProfile(k, QStringLiteral("Default"));
        CHECK(!file.isEmpty());

        QString name, from, error;
        CHECK(Config::readProfileFile(file, &name, &from, &error));
        CHECK(name == QStringLiteral("Default") && from == k);
        CHECK(!Config::readProfileFile("{\"version\": 3}", nullptr, nullptr, &error) && !error.isEmpty());
        CHECK(!Config::readProfileFile("not json", nullptr, nullptr, &error));

        const QString added = c.importProfile(k, file, &error);
        CHECK(added == QStringLiteral("Default (2)"));
        CHECK(c.setActiveProfile(k, added));
        CHECK(c.device(k).bindings == th.bindings);
        CHECK(c.device(k).hypershift == th.hypershift);
        CHECK(c.programs(k, added).isEmpty()); // still linked to "Default"
        CHECK(c.importProfile(k, file, &error) == QStringLiteral("Default (3)"));

        // On a device without those programs they come along.
        CHECK(c.importProfile(QStringLiteral("other kbd"), file, &error) == QStringLiteral("Default (2)"));
        CHECK(c.programs(QStringLiteral("other kbd"), QStringLiteral("Default (2)"))
              == (QStringList{QStringLiteral("game.exe"), QStringLiteral("other")}));
    }

    if (g_failures)
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    else
        std::printf("all remapper checks passed\n");
    return g_failures ? 1 : 0;
}
