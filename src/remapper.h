#pragma once

#include "config.h"
#include "razerdevice.h"

#include <QObject>
#include <QSet>

#include <atomic>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

struct input_event;
struct libevdev_uinput;

// Remaps one physical Razer device.
//
// A worker thread reads every event node of the device. While "active" the
// nodes are grabbed exclusively (EVIOCGRAB) so the rest of the system no longer
// sees them, and events are re-emitted, with bindings applied, through a
// virtual uinput device. While inactive the nodes are only monitored so the UI
// can show live key presses and detect keys for new bindings.
class Remapper : public QObject
{
    Q_OBJECT

public:
    explicit Remapper(RazerDevice device, QObject *parent = nullptr);
    ~Remapper() override;

    const RazerDevice &device() const { return m_device; }

    // When any Razer device last sent input (keys, buttons, movement), in
    // monotonic ns, and a one-shot activity() on the next input from any.
    static qint64 lastActivityNs();
    static void requestActivitySignal();

    bool start(QString *error);
    void stop();

    // Bindings and Hypershift layer of the active profile.
    void setProfile(const DeviceProfile &profile);
    void setHotkeys(const QList<Hotkey> &hotkeys);
    // Whether any of the device's nodes can report this key, button or wheel code.
    bool canProduce(int code) const { return m_capabilities.contains(code); }
    void setActive(bool active);
    bool isGrabbed() const { return m_grabbed.load(); }
    bool isWaitingForRelease() const { return m_pendingGrab.load(); }

signals:
    // Raw input from the physical device, before remapping. Emitted from the
    // worker thread; connect with the default (queued) connection type.
    void inputEvent(int code, bool pressed);
    void stateChanged();
    void hotkeyTriggered(int action, const QString &profile, const QString &device);
    // A binding asks to run this shell command. From the worker thread.
    void commandTriggered(const QString &command);
    // A DPI binding (Binding::DpiAction) was pressed or, for the clutch,
    // released. From the worker thread.
    void dpiTriggered(int action, int clutchDpi, bool pressed);
    // Input arrived after requestActivitySignal(). From the worker thread.
    void activity();
    // The keyboard's game mode key (Fn+F10, which the driver reports as
    // KEY_MACRO29) was pressed. From the worker thread.
    void gameModeKey();
    void failed(const QString &message);
    void disconnected();

private:
    struct Source
    {
        int fd = -1;
        bool writable = false;
        bool hasLeds = false;
    };

    void run();
    bool anyKeyDown() const;
    bool grab();
    void ungrab();
    bool createVirtualDevice();
    bool readSource(const Source &src);
    void handleEvent(const input_event &ev);
    void dispatchKey(int code, int value);
    void processKey(int code, int value);
    void resolveTapHold(bool hold);
    void handleWheel(const input_event &ev);
    void forwardLeds();

    void write(int type, int code, int value);
    void syn();
    void press(int code);
    void release(int code);
    void pressAll(const QList<int> &targets);
    void releaseAll(const QList<int> &targets);
    void tap(const QList<int> &targets);
    // The binding for an input, preferring one for the modifiers held now;
    // *combo gets those modifiers when it's a combo binding.
    Binding lookup(int code, bool *bound, unsigned *combo = nullptr);
    unsigned outputModifierMask() const;
    void suspendModifiers(unsigned mask, int source);
    void resumeModifiers();
    void startTurbo(int source, const Binding &binding);
    void startMacro(int source, const Binding &binding);
    void stopMacro(int source);
    void runMacros();
    void runTurbo();
    long nextTurboTimeout() const; // nanoseconds until the next turbo edge, -1 if none
    bool matchHotkey(int code, bool trigger);
    void trackModifier(int code, bool pressed);
    void releaseModifiers();

    RazerDevice m_device;
    std::vector<Source> m_sources;
    int m_wakePipe[2] = {-1, -1};
    std::thread m_thread;

    std::mutex m_bindingsMutex;
    BindingMap m_bindings;
    BindingMap m_hypershift;
    GameMode m_gameMode;
    QList<Hotkey> m_hotkeys;
    QSet<int> m_capabilities; // written before the thread starts

    std::atomic<bool> m_wantActive{false};
    std::atomic<bool> m_grabbed{false};
    std::atomic<bool> m_pendingGrab{false};
    std::atomic<bool> m_quit{false};

    // Worker-thread state.
    libevdev_uinput *m_uinput = nullptr;
    QHash<int, int> m_outputCount;     // output code -> number of sources holding it
    QHash<int, QList<int>> m_held;     // source code -> outputs it pressed
    QSet<int> m_heldModifiers;         // physical modifier keys down on this device

    struct Turbo
    {
        QList<int> targets;
        qint64 halfPeriod = 0; // ns; the outputs are down for half of each period
        qint64 next = 0;       // monotonic ns of the next press or release
        bool down = false;
    };
    QHash<int, Turbo> m_turbo; // source code -> repeating binding while held

    struct TapHold
    {
        int code = 0;
        Binding binding;
        qint64 deadline = 0;  // monotonic ns when it becomes a hold
        QSet<int> pressedSince; // keys pressed while undecided
    };
    struct Macro
    {
        QList<MacroStep> steps;
        Binding::MacroMode mode = Binding::PlayOnce;
        int index = 0;       // next step
        qint64 next = 0;     // monotonic ns when the next step is due
        QSet<int> down;      // keys the macro pressed and hasn't released
        bool paced = false;  // its pauses add up to at least Keys::MacroMinLoopGap
    };
    QHash<int, Macro> m_macros; // source code -> playing macro
    QSet<int> m_clutchKeys;     // inputs holding the sensitivity clutch
    QSet<int> m_suspended;      // modifiers lifted for a combo while still held
    int m_suspendSource = -1;   // the input whose combo lifted them

    std::optional<TapHold> m_tapHold;          // key pressed, not yet a tap or a hold
    std::vector<std::pair<int, int>> m_queued; // key events waiting for it: code, value
};
