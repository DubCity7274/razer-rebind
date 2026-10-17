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
        g_output.push_back({dev, int(code), value});
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
    kp.bindings.insert(KEY_A, Binding{{KEY_B}, 0, {}, 0});
    kp.bindings.insert(KEY_D, Binding{{KEY_E}, 0, {}, 0});
    kp.bindings.insert(KEY_T, Binding{{KEY_T}, 20, {}, 0});
    kp.bindings.insert(KEY_CAPSLOCK, Binding{{Keys::Hypershift}, 0, {}, 0});
    kp.hypershift.insert(KEY_A, Binding{{KEY_C}, 0, {}, 0});
    kp.hypershift.insert(KEY_F, Binding{{KEY_LEFTCTRL, KEY_V}, 0, {}, 0});
    kp.hypershift.insert(KEY_G, Binding{{KEY_G}, 25, {}, 0});
    kbd.remapper->setProfile(kp);

    DeviceProfile mp;
    mp.bindings.insert(BTN_SIDE, Binding{{Keys::Hypershift}, 0, {}, 0});
    mp.hypershift.insert(BTN_LEFT, Binding{{BTN_RIGHT}, 0, {}, 0});
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
        th.bindings.insert(KEY_TAB, Binding{{KEY_TAB}, 0, {KEY_LEFTCTRL}, 300});
        th.bindings.insert(KEY_SPACE, Binding{{KEY_SPACE}, 0, {Keys::Hypershift}, 300});
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
        th.bindings.insert(KEY_CAPSLOCK, Binding{{KEY_ESC}, 0, {KEY_LEFTCTRL}, 0});
        th.bindings.insert(KEY_SPACE, Binding{{KEY_SPACE}, 0, {Keys::Hypershift}, 350});
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

    // Export and import: everything comes back, under a free name, without
    // programs another profile already has.
    {
        Config c;
        c.load(dir.filePath(QStringLiteral("share.json")), nullptr);
        const QString k = QStringLiteral("kbd");
        c.ensureDevice(k);
        DeviceProfile th = kp;
        th.bindings.insert(KEY_CAPSLOCK, Binding{{KEY_ESC}, 0, {KEY_LEFTCTRL}, 350});
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
