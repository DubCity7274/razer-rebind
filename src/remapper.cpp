#include "remapper.h"

#include <QDBusConnection>
#include <QDBusMessage>

#include <algorithm>
#include <cstdlib>
#include <limits>

#include <libevdev/libevdev-uinput.h>
#include <libevdev/libevdev.h>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <sched.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

namespace {

constexpr size_t bitsToBytes(size_t bits) { return (bits + 7) / 8; }

bool testBit(const unsigned char *bits, int bit) { return bits[bit / 8] & (1 << (bit % 8)); }

QString errnoText(int err) { return QString::fromLocal8Bit(std::strerror(err)); }

// Physical modifiers held across all Razer devices, so a hotkey can combine
// Ctrl on the keyboard with a mouse button. Indexed by modifier bit position.
std::atomic<int> g_heldModifiers[4];

unsigned heldModifierMask()
{
    unsigned mask = 0;
    for (int i = 0; i < 4; ++i)
        if (g_heldModifiers[i].load() > 0)
            mask |= 1u << i;
    return mask;
}

int modifierIndex(unsigned bit) { return __builtin_ctz(bit); }

// In the order they're lifted and put back for combos.
constexpr int ModifierCodes[] = {KEY_LEFTCTRL, KEY_RIGHTCTRL, KEY_LEFTSHIFT, KEY_RIGHTSHIFT,
                                 KEY_LEFTALT,  KEY_RIGHTALT,  KEY_LEFTMETA,  KEY_RIGHTMETA};

// Hypershift keys held across all Razer devices: holding one on the mouse
// switches the keyboard to its Hypershift layer too.
std::atomic<int> g_hypershiftHeld{0};

qint64 monotonicNs()
{
    timespec ts;
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return qint64(ts.tv_sec) * 1000000000 + ts.tv_nsec;
}

// Under heavy CPU load (a game, shader compilation) an ordinary thread can wait
// a millisecond or more for its turn, delaying input. Ask for a modest
// real-time priority like PipeWire does for audio: directly if the user is
// allowed, otherwise through RealtimeKit. The thread sleeps in poll() almost
// all the time, and RLIMIT_RTTIME stops it from ever hogging a CPU.
QString raiseThreadPriority()
{
    constexpr int priority = 10;
    sched_param param = {};
    param.sched_priority = priority;
    if (::sched_setscheduler(0, SCHED_FIFO | SCHED_RESET_ON_FORK, &param) == 0)
        return {};

    const rlimit rtTime = {200000, 200000}; // microseconds of CPU time without sleeping
    ::setrlimit(RLIMIT_RTTIME, &rtTime);
    QDBusMessage call = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.RealtimeKit1"), QStringLiteral("/org/freedesktop/RealtimeKit1"),
        QStringLiteral("org.freedesktop.RealtimeKit1"), QStringLiteral("MakeThreadRealtime"));
    call << quint64(::gettid()) << quint32(priority);
    const QDBusMessage reply = QDBusConnection::systemBus().call(call, QDBus::Block, 2000);
    return reply.type() == QDBusMessage::ReplyMessage ? QString() : reply.errorMessage();
}

} // namespace

Remapper::Remapper(RazerDevice device, QObject *parent)
    : QObject(parent)
    , m_device(std::move(device))
{
}

Remapper::~Remapper()
{
    stop();
}

bool Remapper::start(QString *error)
{
    if (m_thread.joinable())
        return true;

    for (const QString &node : m_device.eventNodes) {
        const QByteArray path = node.toLocal8Bit();
        Source src;
        src.fd = ::open(path.constData(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
        src.writable = src.fd >= 0;
        if (src.fd < 0 && (errno == EACCES || errno == EPERM))
            src.fd = ::open(path.constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (src.fd < 0) {
            const int err = errno;
            if (error)
                *error = tr("Cannot open %1: %2").arg(node, errnoText(err));
            for (const Source &s : m_sources)
                ::close(s.fd);
            m_sources.clear();
            return false;
        }
        unsigned char evBits[bitsToBytes(EV_CNT)] = {};
        if (::ioctl(src.fd, EVIOCGBIT(0, sizeof evBits), evBits) >= 0)
            src.hasLeds = testBit(evBits, EV_LED);
        unsigned char keyBits[bitsToBytes(KEY_CNT)] = {};
        if (::ioctl(src.fd, EVIOCGBIT(EV_KEY, sizeof keyBits), keyBits) >= 0)
            for (int c = 1; c < KEY_CNT; ++c)
                if (testBit(keyBits, c))
                    m_capabilities.insert(c);
        unsigned char relBits[bitsToBytes(REL_CNT)] = {};
        if (::ioctl(src.fd, EVIOCGBIT(EV_REL, sizeof relBits), relBits) >= 0 && testBit(relBits, REL_WHEEL))
            m_capabilities << Keys::WheelUp << Keys::WheelDown;
        m_sources.push_back(src);
    }

    if (::pipe2(m_wakePipe, O_NONBLOCK | O_CLOEXEC) < 0) {
        if (error)
            *error = tr("pipe2 failed: %1").arg(errnoText(errno));
        for (const Source &s : m_sources)
            ::close(s.fd);
        m_sources.clear();
        return false;
    }

    m_quit = false;
    m_thread = std::thread(&Remapper::run, this);
    return true;
}

void Remapper::stop()
{
    if (m_thread.joinable()) {
        m_quit = true;
        const char c = 'q';
        [[maybe_unused]] auto n = ::write(m_wakePipe[1], &c, 1);
        m_thread.join();
    }
    for (const Source &s : m_sources)
        ::close(s.fd);
    m_sources.clear();
    for (int &fd : m_wakePipe) {
        if (fd >= 0)
            ::close(fd);
        fd = -1;
    }
}

void Remapper::setProfile(const DeviceProfile &profile)
{
    std::lock_guard lock(m_bindingsMutex);
    m_bindings = profile.bindings;
    m_hypershift = profile.hypershift;
}

void Remapper::setHotkeys(const QList<Hotkey> &hotkeys)
{
    std::lock_guard lock(m_bindingsMutex);
    m_hotkeys = hotkeys;
}

void Remapper::setActive(bool active)
{
    if (m_wantActive.exchange(active) == active)
        return;
    if (m_wakePipe[1] >= 0) {
        const char c = 'w';
        [[maybe_unused]] auto n = ::write(m_wakePipe[1], &c, 1);
    }
}

// --- worker thread --------------------------------------------------------

void Remapper::run()
{
    std::vector<pollfd> pfds;
    bool lost = false;

    const QString priorityError = raiseThreadPriority();
    if (!priorityError.isEmpty())
        qWarning("%s: no real-time priority for input (%s); input may lag under heavy CPU load",
                 qPrintable(m_device.name), qPrintable(priorityError));

    while (!m_quit && !lost) {
        const bool want = m_wantActive.load();
        if (want && !m_grabbed) {
            // Grabbing while a key or button is physically held would leave it
            // stuck "down" for the compositor (it never sees the release), so
            // wait until everything is released.
            if (anyKeyDown()) {
                if (!m_pendingGrab.exchange(true))
                    emit stateChanged();
            } else {
                m_pendingGrab = false;
                if (!grab())
                    m_wantActive = false;
                emit stateChanged();
            }
        } else if (!want && (m_grabbed || m_pendingGrab)) {
            m_pendingGrab = false;
            ungrab();
            emit stateChanged();
        }

        pfds.clear();
        for (const Source &s : m_sources)
            pfds.push_back({s.fd, POLLIN, 0});
        pfds.push_back({m_wakePipe[0], POLLIN, 0});
        if (m_uinput)
            pfds.push_back({libevdev_uinput_get_fd(m_uinput), POLLIN, 0});

        // Wake up for the next turbo press or release, or every 30 ms while
        // waiting to grab.
        long timeout = nextTurboTimeout();
        if (m_tapHold) {
            const long wait = long(std::max<qint64>(0, m_tapHold->deadline - monotonicNs()));
            timeout = timeout < 0 ? wait : std::min(timeout, wait);
        }
        if (m_pendingGrab && (timeout < 0 || timeout > 30000000))
            timeout = 30000000;
        timespec ts{timeout / 1000000000, timeout % 1000000000};
        const int rc = ::ppoll(pfds.data(), pfds.size(), timeout >= 0 ? &ts : nullptr, nullptr);
        if (rc < 0) {
            if (errno == EINTR)
                continue;
            emit failed(tr("poll failed: %1").arg(errnoText(errno)));
            break;
        }

        if (pfds[m_sources.size()].revents & POLLIN) {
            char buf[64];
            while (::read(m_wakePipe[0], buf, sizeof buf) > 0) {}
        }

        for (size_t i = 0; i < m_sources.size() && !lost; ++i) {
            if (pfds[i].revents & POLLIN)
                lost = !readSource(m_sources[i]);
            if (pfds[i].revents & (POLLERR | POLLHUP | POLLNVAL))
                lost = true;
        }

        if (m_uinput && pfds.size() > m_sources.size() + 1 && (pfds.back().revents & POLLIN))
            forwardLeds();
        runTurbo();
        runMacros();
        if (m_tapHold && monotonicNs() >= m_tapHold->deadline)
            resolveTapHold(true);
    }

    m_pendingGrab = false;
    ungrab();
    releaseModifiers();
    if (lost)
        emit disconnected();
    emit stateChanged();
}

bool Remapper::anyKeyDown() const
{
    for (const Source &s : m_sources) {
        unsigned char keys[bitsToBytes(KEY_CNT)] = {};
        if (::ioctl(s.fd, EVIOCGKEY(sizeof keys), keys) < 0)
            continue;
        for (unsigned char byte : keys)
            if (byte)
                return true;
    }
    return false;
}

bool Remapper::grab()
{
    if (!createVirtualDevice())
        return false;

    for (size_t i = 0; i < m_sources.size(); ++i) {
        if (::ioctl(m_sources[i].fd, EVIOCGRAB, 1) < 0) {
            const int err = errno;
            for (size_t j = 0; j < i; ++j)
                ::ioctl(m_sources[j].fd, EVIOCGRAB, 0);
            libevdev_uinput_destroy(m_uinput);
            m_uinput = nullptr;
            emit failed(err == EBUSY
                            ? tr("%1 is already grabbed by another program (another remapper running?)")
                                  .arg(m_device.eventNodes.value(int(i)))
                            : tr("Cannot grab %1: %2").arg(m_device.eventNodes.value(int(i)), errnoText(err)));
            return false;
        }
    }
    m_grabbed = true;
    return true;
}

void Remapper::ungrab()
{
    if (!m_grabbed)
        return;
    // Never leave anything pressed on the virtual device, or a Hypershift
    // layer switched on for the other devices.
    for (auto it = m_outputCount.cbegin(); it != m_outputCount.cend(); ++it)
        write(EV_KEY, it.key(), 0);
    syn();
    m_outputCount.clear();
    for (const QList<int> &targets : std::as_const(m_held))
        if (targets == QList<int>{Keys::Hypershift})
            --g_hypershiftHeld;
    m_held.clear();
    m_turbo.clear();
    m_macros.clear();
    m_suspended.clear();
    m_suspendSource = -1;
    m_tapHold.reset();
    m_queued.clear();

    for (const Source &s : m_sources)
        ::ioctl(s.fd, EVIOCGRAB, 0);
    libevdev_uinput_destroy(m_uinput);
    m_uinput = nullptr;
    m_grabbed = false;
}

bool Remapper::createVirtualDevice()
{
    libevdev *tmpl = libevdev_new();
    // Take the physical device's own name: together with the same vendor
    // and product id, KDE and libinput then apply the settings made for the
    // real device (pointer acceleration profile, speed, quirks) to this one.
    char sourceName[256] = {};
    if (m_sources.empty() || ::ioctl(m_sources.front().fd, EVIOCGNAME(sizeof sourceName - 1), sourceName) <= 0)
        qstrncpy(sourceName, ("Razer Rebind: " + m_device.name).toUtf8().constData(), sizeof sourceName);
    const QByteArray name(sourceName);
    libevdev_set_name(tmpl, name.constData());
    libevdev_set_id_bustype(tmpl, BUS_VIRTUAL);
    libevdev_set_id_vendor(tmpl, m_device.vendorId);
    libevdev_set_id_product(tmpl, m_device.productId);
    libevdev_set_id_version(tmpl, 1);

    // Every key or button a binding can produce, on both kinds of device, so a
    // mouse button can type a shortcut and a key can click.
    libevdev_enable_event_type(tmpl, EV_KEY);
    for (int code : Keys::targetCodes())
        libevdev_enable_event_code(tmpl, EV_KEY, code, nullptr);

    libevdev_enable_event_type(tmpl, EV_REL);
    for (int code : {REL_X, REL_Y, REL_HWHEEL, REL_WHEEL, REL_WHEEL_HI_RES, REL_HWHEEL_HI_RES})
        libevdev_enable_event_code(tmpl, EV_REL, code, nullptr);

    if (m_device.kind == RazerDevice::Keyboard) {
        libevdev_enable_event_type(tmpl, EV_LED);
        for (int code : {LED_NUML, LED_CAPSL, LED_SCROLLL})
            libevdev_enable_event_code(tmpl, EV_LED, code, nullptr);
    }

    const int rc = libevdev_uinput_create_from_device(tmpl, LIBEVDEV_UINPUT_OPEN_MANAGED, &m_uinput);
    libevdev_free(tmpl);
    if (rc < 0) {
        m_uinput = nullptr;
        emit failed(tr("Cannot create virtual device through /dev/uinput: %1").arg(errnoText(-rc)));
        return false;
    }
    const int ufd = libevdev_uinput_get_fd(m_uinput);
    ::fcntl(ufd, F_SETFL, ::fcntl(ufd, F_GETFL) | O_NONBLOCK);
    return true;
}

bool Remapper::readSource(const Source &src)
{
    input_event evs[64];
    for (;;) {
        const ssize_t n = ::read(src.fd, evs, sizeof evs);
        if (n < 0)
            return errno == EAGAIN || errno == EINTR;
        if (n == 0)
            return false;
        for (size_t i = 0; i < size_t(n) / sizeof(input_event); ++i)
            handleEvent(evs[i]);
    }
}

unsigned Remapper::outputModifierMask() const
{
    unsigned mask = 0;
    for (auto it = m_outputCount.cbegin(); it != m_outputCount.cend(); ++it)
        mask |= Keys::modifierBit(it.key());
    return mask;
}

Binding Remapper::lookup(int code, bool *bound, unsigned *combo)
{
    // Modifiers held on any Razer device, or sent by this one (Caps Lock
    // bound to Ctrl counts as Ctrl), select a combo binding such as
    // Ctrl + Shift + S over the plain one.
    const unsigned mods = Keys::isModifier(code) ? 0 : (heldModifierMask() | outputModifierMask());
    const bool layer = g_hypershiftHeld.load() > 0;
    if (combo)
        *combo = 0;
    std::lock_guard lock(m_bindingsMutex);
    auto find = [&](const BindingMap &map, int trigger) -> const Binding * {
        const auto it = map.constFind(trigger);
        return it != map.constEnd() ? &it.value() : nullptr;
    };
    const Binding *b = nullptr;
    if (mods) {
        if (layer)
            b = find(m_hypershift, Keys::trigger(mods, code));
        if (!b)
            b = find(m_bindings, Keys::trigger(mods, code));
        if (b && combo)
            *combo = mods;
    }
    if (!b && layer)
        b = find(m_hypershift, code);
    if (!b)
        b = find(m_bindings, code);
    *bound = b != nullptr;
    return b ? *b : Binding();
}

void Remapper::suspendModifiers(unsigned mask, int source)
{
    // A combo's output shouldn't come out with the combo's own modifiers:
    // Ctrl + Shift + S bound to F5 sends F5, not Ctrl + Shift + F5.
    resumeModifiers();
    for (int code : ModifierCodes)
        if ((Keys::modifierBit(code) & mask) && m_outputCount.contains(code)) {
            write(EV_KEY, code, 0);
            m_suspended.insert(code);
        }
    m_suspendSource = source;
    if (!m_suspended.isEmpty())
        syn();
}

void Remapper::resumeModifiers()
{
    // Modifiers still held go back down, so holding Ctrl for the next
    // shortcut still works.
    bool any = false;
    for (int code : ModifierCodes)
        if (m_suspended.contains(code) && m_outputCount.contains(code)) {
            write(EV_KEY, code, 1);
            any = true;
        }
    m_suspended.clear();
    m_suspendSource = -1;
    if (any)
        syn();
}

void Remapper::handleEvent(const input_event &ev)
{
    switch (ev.type) {
    case EV_KEY: {
        if (ev.value != 2) {
            emit inputEvent(ev.code, ev.value == 1);
            trackModifier(ev.code, ev.value == 1);
        }
        // Hotkeys fire even while the device is not grabbed (e.g. to turn
        // remapping back on); they are only swallowed when it is.
        if (ev.value == 1 && !Keys::isModifier(ev.code) && matchHotkey(ev.code, true)) {
            if (m_grabbed)
                m_held.insert(ev.code, {});
            return;
        }
        if (!m_grabbed)
            return;
        dispatchKey(ev.code, ev.value);
        return;
    }
    case EV_REL:
        if (ev.code == REL_WHEEL || ev.code == REL_WHEEL_HI_RES) {
            handleWheel(ev);
            return;
        }
        if (m_grabbed)
            write(EV_REL, ev.code, ev.value);
        return;
    case EV_SYN:
        if (m_grabbed && ev.code == SYN_REPORT)
            syn();
        return;
    default:
        // EV_MSC scan codes would describe the physical key, not the remapped
        // one, so they are dropped along with anything else exotic.
        return;
    }
}

void Remapper::dispatchKey(int code, int value)
{
    if (!m_tapHold) {
        processKey(code, value);
        return;
    }
    // A tap-or-hold key is undecided: its own release makes it a tap; other
    // keys of this device wait, so they come out after whatever it becomes.
    if (code == m_tapHold->code) {
        if (value == 0)
            resolveTapHold(false);
        return;
    }
    if (value == 2)
        return;
    m_queued.push_back({code, value});
    if (value == 1)
        m_tapHold->pressedSince.insert(code);
    else if (m_tapHold->pressedSince.contains(code))
        resolveTapHold(true); // another key went down and up meanwhile: Caps + C means Ctrl + C
}

void Remapper::resolveTapHold(bool hold)
{
    const TapHold t = *m_tapHold;
    m_tapHold.reset();
    if (!hold) {
        tap(t.binding.targets);
        if (t.code == m_suspendSource)
            resumeModifiers();
    } else {
        m_held.insert(t.code, t.binding.hold);
        if (t.binding.hold == QList<int>{Keys::Hypershift})
            ++g_hypershiftHeld;
        else
            pressAll(t.binding.hold);
        syn();
    }
    // Replay what waited. A queued tap-or-hold key starts its own wait and
    // queues the rest again.
    const std::vector<std::pair<int, int>> queued = std::exchange(m_queued, {});
    for (const auto &[code, value] : queued) {
        dispatchKey(code, value);
        syn();
    }
}

void Remapper::processKey(int code, int value)
{
    if (value == 1) {
        bool bound = false;
        unsigned combo = 0;
        Binding b = lookup(code, &bound, &combo);
        if (!bound)
            b.targets = {code};
        if (combo)
            suspendModifiers(combo, code);
        if (b.isMacro()) {
            m_held.insert(code, {});
            const bool playing = m_macros.contains(code);
            if (playing && b.macroMode == Binding::Toggle)
                stopMacro(code);
            else if (!playing)
                startMacro(code, b);
            return;
        }
        if (b.isTapHold()) {
            m_tapHold = TapHold{code, b, monotonicNs() + qint64(b.effectiveHoldTime()) * 1000000, {}};
            return;
        }
        m_held.insert(code, b.targets);
        if (b.isHypershift())
            ++g_hypershiftHeld;
        else if (b.turbo > 0 && !b.targets.isEmpty())
            startTurbo(code, b);
        else
            pressAll(b.targets);
    } else if (value == 0) {
        // Release exactly what this key pressed, even if its binding has
        // changed while it was held.
        const QList<int> targets = m_held.take(code);
        if (auto m = m_macros.constFind(code); m != m_macros.constEnd() && m->mode == Binding::RepeatWhileHeld) {
            stopMacro(code);
        } else if (targets == QList<int>{Keys::Hypershift}) {
            --g_hypershiftHeld;
        } else if (auto t = m_turbo.find(code); t != m_turbo.end()) {
            if (t->down)
                releaseAll(t->targets);
            m_turbo.erase(t);
        } else {
            releaseAll(targets);
        }
        // A combo's modifiers come back once it's done; a macro still
        // playing brings them back when it ends.
        if (code == m_suspendSource && !m_macros.contains(code))
            resumeModifiers();
    } else if (value == 2) {
        // Key repeat: pass it on for plain bindings. Turbo makes its own.
        const QList<int> targets = m_held.value(code);
        if (!targets.isEmpty() && targets.last() != Keys::Hypershift && !m_turbo.contains(code)
            && !m_suspended.contains(targets.last()))
            write(EV_KEY, targets.last(), 2);
    }
}

void Remapper::handleWheel(const input_event &ev)
{
    if (ev.value == 0)
        return;
    const int pseudo = ev.value > 0 ? Keys::WheelUp : Keys::WheelDown;
    if (ev.code == REL_WHEEL) {
        emit inputEvent(pseudo, true);
        emit inputEvent(pseudo, false);
    }
    if (matchHotkey(pseudo, ev.code == REL_WHEEL) || !m_grabbed)
        return;

    bool bound = false;
    unsigned combo = 0;
    const Binding b = lookup(pseudo, &bound, &combo);
    if (!bound) {
        write(EV_REL, ev.code, ev.value);
        return;
    }
    // The kernel reports each notch twice (REL_WHEEL and the high-resolution
    // REL_WHEEL_HI_RES); act on the classic one and swallow both. A notch
    // can't be held, so Hypershift and turbo don't apply.
    if (ev.code == REL_WHEEL && !b.isHypershift()) {
        if (combo)
            suspendModifiers(combo, pseudo);
        for (int i = 0; i < std::abs(ev.value); ++i)
            tap(b.targets);
        if (combo)
            resumeModifiers();
    }
}

void Remapper::forwardLeds()
{
    // The compositor sets Caps/Num/Scroll Lock LEDs on the virtual keyboard;
    // pass them on to the real one.
    input_event ev;
    const int ufd = libevdev_uinput_get_fd(m_uinput);
    while (::read(ufd, &ev, sizeof ev) == ssize_t(sizeof ev)) {
        if (ev.type != EV_LED)
            continue;
        for (const Source &s : m_sources) {
            if (!s.hasLeds || !s.writable)
                continue;
            input_event out[2] = {};
            out[0].type = EV_LED;
            out[0].code = ev.code;
            out[0].value = ev.value;
            out[1].type = EV_SYN;
            out[1].code = SYN_REPORT;
            [[maybe_unused]] auto n = ::write(s.fd, out, sizeof out);
        }
    }
}

void Remapper::write(int type, int code, int value)
{
    if (m_uinput)
        libevdev_uinput_write_event(m_uinput, type, code, value);
}

void Remapper::syn()
{
    write(EV_SYN, SYN_REPORT, 0);
}

void Remapper::press(int code)
{
    // A modifier lifted for a combo goes down again if something presses it.
    if (m_outputCount[code]++ == 0 || m_suspended.remove(code))
        write(EV_KEY, code, 1);
}

void Remapper::release(int code)
{
    auto it = m_outputCount.find(code);
    if (it == m_outputCount.end())
        return;
    if (--it.value() <= 0) {
        m_outputCount.erase(it);
        m_suspended.remove(code);
        write(EV_KEY, code, 0);
    }
}

void Remapper::pressAll(const QList<int> &targets)
{
    for (int t : targets) {
        press(t);
        if (targets.size() > 1)
            syn(); // let modifiers register before the key they modify
    }
}

void Remapper::releaseAll(const QList<int> &targets)
{
    for (auto it = targets.crbegin(); it != targets.crend(); ++it) {
        release(*it);
        if (targets.size() > 1)
            syn();
    }
}

void Remapper::tap(const QList<int> &targets)
{
    for (int t : targets) {
        press(t);
        syn();
    }
    for (auto it = targets.crbegin(); it != targets.crend(); ++it) {
        release(*it);
        syn();
    }
}

void Remapper::startTurbo(int source, const Binding &binding)
{
    // Press right away, then alternate: down for half of each period, up for
    // the other half. Games that sample key state once per frame see every
    // press, unlike instant taps.
    Turbo t;
    t.targets = binding.targets;
    t.halfPeriod = 500000000 / std::clamp(binding.turbo, Keys::TurboMin, Keys::TurboMax);
    t.next = monotonicNs() + t.halfPeriod;
    t.down = true;
    pressAll(t.targets);
    m_turbo.insert(source, t);
}

void Remapper::runTurbo()
{
    if (m_turbo.isEmpty())
        return;
    const qint64 now = monotonicNs();
    bool changed = false;
    for (Turbo &t : m_turbo) {
        if (t.next > now)
            continue;
        if (t.down)
            releaseAll(t.targets);
        else
            pressAll(t.targets);
        t.down = !t.down;
        t.next += t.halfPeriod;
        if (t.next <= now) // fell behind (suspend, long stall): don't burst to catch up
            t.next = now + t.halfPeriod;
        changed = true;
    }
    if (changed)
        syn();
}

void Remapper::startMacro(int source, const Binding &binding)
{
    Macro m;
    m.steps = binding.macro;
    m.mode = binding.macroMode;
    m.next = monotonicNs();
    int pause = 0;
    for (const MacroStep &st : binding.macro)
        if (st.kind == MacroStep::Delay)
            pause += st.value;
    m.paced = pause >= Keys::MacroMinLoopGap;
    m_macros.insert(source, m);
    runMacros(); // the first steps go out right away
}

void Remapper::stopMacro(int source)
{
    // Let go of whatever it holds, so nothing stays stuck down.
    const Macro m = m_macros.take(source);
    for (int code : m.down)
        release(code);
    if (!m.down.isEmpty())
        syn();
    if (source == m_suspendSource && !m_held.contains(source))
        resumeModifiers();
}

void Remapper::runMacros()
{
    if (m_macros.isEmpty())
        return;
    const qint64 now = monotonicNs();
    QList<int> finished;
    for (auto it = m_macros.begin(); it != m_macros.end(); ++it) {
        Macro &m = *it;
        while (m.next <= now) {
            if (m.index >= m.steps.size()) {
                if (m.mode == Binding::PlayOnce) {
                    finished << it.key();
                    break;
                }
                m.index = 0;
                if (!m.paced) {
                    // Without pauses a repeating macro would spin a core.
                    m.next = now + qint64(Keys::MacroMinLoopGap) * 1000000;
                    break;
                }
                continue;
            }
            const MacroStep &st = m.steps[m.index++];
            switch (st.kind) {
            case MacroStep::Press:
                if (!m.down.contains(st.value)) {
                    press(st.value);
                    m.down.insert(st.value);
                    syn();
                }
                break;
            case MacroStep::Release:
                // Only what the macro pressed: a recording can start with the
                // release of a key that was down before it.
                if (m.down.remove(st.value)) {
                    release(st.value);
                    syn();
                }
                break;
            case MacroStep::Delay:
                // Keep time from the schedule, not from when we woke, but
                // don't rush to catch up after a stall.
                m.next = std::max(m.next, now - 5000000) + qint64(st.value) * 1000000;
                break;
            }
        }
    }
    for (int source : std::as_const(finished))
        stopMacro(source);
}

long Remapper::nextTurboTimeout() const
{
    if (m_turbo.isEmpty() && m_macros.isEmpty())
        return -1;
    qint64 next = std::numeric_limits<qint64>::max();
    for (const Turbo &t : m_turbo)
        next = std::min(next, t.next);
    for (const Macro &m : m_macros)
        next = std::min(next, m.next);
    return long(std::max<qint64>(0, next - monotonicNs()));
}

bool Remapper::matchHotkey(int code, bool trigger)
{
    const unsigned mods = heldModifierMask();
    std::lock_guard lock(m_bindingsMutex);
    for (const Hotkey &h : std::as_const(m_hotkeys)) {
        if (h.code == code && h.modifiers == mods) {
            if (trigger)
                emit hotkeyTriggered(int(h.action), h.profile, h.device);
            return true;
        }
    }
    return false;
}

void Remapper::trackModifier(int code, bool pressed)
{
    const unsigned bit = Keys::modifierBit(code);
    if (!bit)
        return;
    if (pressed && !m_heldModifiers.contains(code)) {
        m_heldModifiers.insert(code);
        ++g_heldModifiers[modifierIndex(bit)];
    } else if (!pressed && m_heldModifiers.remove(code)) {
        --g_heldModifiers[modifierIndex(bit)];
    }
}

void Remapper::releaseModifiers()
{
    // The device is going away; don't leave its modifiers counted as held.
    for (int code : std::as_const(m_heldModifiers))
        --g_heldModifiers[modifierIndex(Keys::modifierBit(code))];
    m_heldModifiers.clear();
}
