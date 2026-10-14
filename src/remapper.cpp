#include "remapper.h"

#include <cstdlib>

#include <libevdev/libevdev-uinput.h>
#include <libevdev/libevdev.h>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <sys/ioctl.h>
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

void Remapper::setBindings(const BindingMap &bindings)
{
    std::lock_guard lock(m_bindingsMutex);
    m_bindings = bindings;
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

        const int rc = ::poll(pfds.data(), pfds.size(), m_pendingGrab ? 30 : -1);
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
    // Never leave anything pressed on the virtual device.
    for (auto it = m_outputCount.cbegin(); it != m_outputCount.cend(); ++it)
        write(EV_KEY, it.key(), 0);
    syn();
    m_outputCount.clear();
    m_held.clear();

    for (const Source &s : m_sources)
        ::ioctl(s.fd, EVIOCGRAB, 0);
    libevdev_uinput_destroy(m_uinput);
    m_uinput = nullptr;
    m_grabbed = false;
}

bool Remapper::createVirtualDevice()
{
    libevdev *tmpl = libevdev_new();
    const QByteArray name = ("Razer Rebind: " + m_device.name).toUtf8();
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

QList<int> Remapper::lookup(int code, bool *bound)
{
    std::lock_guard lock(m_bindingsMutex);
    const auto it = m_bindings.constFind(code);
    *bound = it != m_bindings.constEnd();
    return *bound ? it.value() : QList<int>();
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

        if (ev.value == 1) {
            bool bound = false;
            QList<int> targets = lookup(ev.code, &bound);
            if (!bound)
                targets = {ev.code};
            m_held.insert(ev.code, targets);
            for (int t : targets) {
                press(t);
                if (targets.size() > 1)
                    syn(); // let modifiers register before the key they modify
            }
        } else if (ev.value == 0) {
            // Release exactly what this key pressed, even if its binding has
            // changed while it was held.
            const QList<int> targets = m_held.take(ev.code);
            for (auto it = targets.crbegin(); it != targets.crend(); ++it) {
                release(*it);
                if (targets.size() > 1)
                    syn();
            }
        } else if (ev.value == 2) {
            const QList<int> targets = m_held.value(ev.code);
            if (!targets.isEmpty())
                write(EV_KEY, targets.last(), 2);
        }
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
    const QList<int> targets = lookup(pseudo, &bound);
    if (!bound) {
        write(EV_REL, ev.code, ev.value);
        return;
    }
    // The kernel reports each notch twice (REL_WHEEL and the high-resolution
    // REL_WHEEL_HI_RES); act on the classic one and swallow both.
    if (ev.code == REL_WHEEL)
        for (int i = 0; i < std::abs(ev.value); ++i)
            tap(targets);
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
    if (m_outputCount[code]++ == 0)
        write(EV_KEY, code, 1);
}

void Remapper::release(int code)
{
    auto it = m_outputCount.find(code);
    if (it == m_outputCount.end())
        return;
    if (--it.value() <= 0) {
        m_outputCount.erase(it);
        write(EV_KEY, code, 0);
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

bool Remapper::matchHotkey(int code, bool trigger)
{
    const unsigned mods = heldModifierMask();
    std::lock_guard lock(m_bindingsMutex);
    for (const Hotkey &h : std::as_const(m_hotkeys)) {
        if (h.code == code && h.modifiers == mods) {
            if (trigger)
                emit hotkeyTriggered(int(h.action), h.profile);
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
